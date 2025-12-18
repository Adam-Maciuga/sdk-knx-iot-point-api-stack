/*
 // Copyright (c) 2021-2022 Cascoda Ltd
 // Copyright (c) 2024-2025 KNX Association
 //
 // Licensed under the Apache License, Version 2.0 (the "License");
 // you may not use this file except in compliance with the License.
 // You may obtain a copy of the License at
 //
 //      http://www.apache.org/licenses/LICENSE-2.0
 //
 // Unless required by applicable law or agreed to in writing, software
 // distributed under the License is distributed on an "AS IS" BASIS,
 // WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 // See the License for the specific language governing permissions and
 // limitations under the License.
 */

#include "oc_api.h"
#include "api/oc_knx_client.h"
#include "api/oc_knx_fp.h"
#include "api/oc_knx_sec.h"
#ifdef OC_SPAKE
#include "oc_spake2plus.h"
#endif
#include "oc_core_res.h"
#include "port/oc_clock.h"
#include <stdio.h>
#include <string.h>
#define __STDC_FORMAT_MACROS  // defined to use format specifiers also in C++
#include <inttypes.h>

// External declarations


// Internal helper function to find recipient table entry by GA
// Note: Not in header - used only within this file and oc_knx.c/oc_knx_fp.c via direct access
oc_group_table_t* oc_find_recipient_by_ga(uint32_t ga)
{
  int total = oc_core_get_recipient_table_size();
  for (int i = 0; i < total; i++) {
    oc_group_table_t* entry = oc_core_get_recipient_table_entry(i);
    if (entry && entry->id >= 0) {
      for (int j = 0; j < entry->ga_len; j++) {
        if (entry->ga[j] == ga) {
          return entry;
        }
      }
    }
  }
  return NULL;
}

// Pending message queue for unresolved unicast sends
#define MAX_PENDING_MESSAGES 10
#define PENDING_MESSAGE_TIMEOUT_SECONDS 10  /**< Timeout for queued messages */

typedef struct pending_s_mode_message_t
{
  uint32_t ga;                                     /**< group address */
  char service_type[3];                            /**< "w", "r", or "a" */
  uint8_t value_data[OC_MAX_APP_DATA_SIZE_STATIC]; /**< CBOR encoded value */
  bool in_use;
  int value_size;                                  /**< size of value_data */
  oc_group_table_t* recipient;
  uint64_t timestamp;                              /**< when this was queued (for timeout) */
  
} pending_s_mode_message_t;

static pending_s_mode_message_t g_pending_messages[MAX_PENDING_MESSAGES] = {0};

// external definitions

void oc_issue_s_mode_message(oc_endpoint_t* endpoint, char* path, uint32_t group_address, const char* service_type,
                             const uint8_t* value_data, int value_size, bool non_confirmable);

static int oc_s_mode_get_resource_value(const char* resource_path, uint8_t* buffer, int buffer_size);

int oc_knx_queue_pending_message(uint32_t ga, uint32_t sia, const char* service_type, const uint8_t* value_data,
                                 int value_size, oc_group_table_t* recipient);

void oc_knx_release_pending_message(int slot);

int oc_is_redirected_request_from(const oc_request_t* request)
{
  if (!request || request->uri_path_len == 0)
  {
    return -1;
  }

  // check POST to '/k' -> s-mode message
  // - note that the stack uri's works without leading '/', e.g.; also when calling the callbacks
  if (request->uri_path[0] == 'k')
  {
    return 0;
  }

  // check GET/PUT to '/p/{point-path}' or POST to '/p' -> both are property messages
  // - note that the stack uri's works without leading '/', e.g.; also when calling the callbacks
  // - we don't care of total uri length, at least 'p' must be present
  if (request->uri_path[0] == 'p')
  {
    return 1;
  }

  // anything else 
  return 2;
}

int oc_send_s_mode_unicast_message(uint32_t group_address, const char* service_type,
                                   const uint8_t* value_data, int value_size, oc_group_table_t* recipient)
{
  if (!recipient) 
  {
    OC_ERR("Cannot send unicast: recipient is NULL");
    return -1;
  }

  if (recipient->ia == -1)
  {
    OC_ERR("Cannot send unicast: invalid IA in recipient for GA %u", group_address);
    return -1;
  }
  
  // get local device info (sia and iid)
  const oc_device_info_t* device = oc_core_get_device_info();
  const uint32_t sia = device->ia;
  const uint64_t iid = device->iid;

  // check if IPv6 is resolved
  if (recipient->ipadd.init_status != OC_IP_STATUS_RESOLVED) 
  {
    OC_INF("IPv6 not resolved for IA 0x%04x, queuing message and triggering resolution", (uint16_t)recipient->ia);

    // queue the message
    const int slot = oc_knx_queue_pending_message(group_address, sia, service_type, value_data, value_size, recipient);
    if (slot == -1)
    {
      return -1;
    }
    
    // trigger resolution - send discovery to both scopes
    bool ret2 = false;
    bool ret5 = false;

    #ifdef OC_USE_MULTICAST_SCOPE_2
    ret2 = knx_resolve_via_coap_discovery(2, recipient->ia, iid, recipient);
    #endif
    ret5 = knx_resolve_via_coap_discovery(5, recipient->ia, iid, recipient);
    
    // fail only if both scopes failed
    if (!ret2 && !ret5) 
    {
      OC_ERR("Failed to trigger IPv6 resolution for IA 0x%04x on all scopes", (uint16_t)recipient->ia);

      // release the occupied pending slot
      oc_knx_release_pending_message(slot);

      return -1;
    }
    
    // success - message is queued
    OC_INF("IPv6 resolution started for IA 0x%04x, message queued for later delivery", (uint16_t)recipient->ia);
    return 0; 
  }

  // create unicast endpoint from resolved IPv6 address + port
  oc_endpoint_t group_ucast_endpoint = {0};
  group_ucast_endpoint = oc_create_unicast_group_address_with_port(group_ucast_endpoint, recipient->ipadd.ipv6, recipient->ipadd.port);

  // set for the EP the sending group_address
  group_ucast_endpoint.group_address = group_address;
  group_ucast_endpoint.interface_index = recipient->ipadd.interface_index;

  PRINT("Sending %s unicast %s to IA %04x via resolved IPv6 (interface %d)", 
        recipient->non ? "NON" : "CON", service_type,
        (uint16_t)recipient->ia, recipient->ipadd.interface_index);

  // send unicast message (confirmable or non-confirmable)
  oc_issue_s_mode_message(&group_ucast_endpoint, "/k", group_address, service_type, value_data, value_size, recipient->non);
  
  return 0;
}

void oc_send_s_mode_non_confirmable_multicast_message(uint8_t scope, uint32_t grpid,
                                                      uint32_t group_address, const char* service_type,
                                                      uint8_t* value_data, int value_size)
{
  // get local device info (iid) -> always the same
  const uint64_t iid = oc_core_get_device_info()->iid;
  
  // create multicast endpoint from grpid/iid and coap default + port
  oc_endpoint_t group_mcast_endpoint = {0};
  group_mcast_endpoint = oc_create_multicast_group_address_with_port(group_mcast_endpoint, grpid, iid, scope, COAP_DEFAULT_PORT);

  // set for the EP the sending group_address
  group_mcast_endpoint.group_address = group_address;

  PRINT("Sending non-confirmable multicast %s", service_type);

  // send non-confirmable message
  oc_issue_s_mode_message(&group_mcast_endpoint, "/k", group_address, service_type, value_data, value_size, true);
}

// sends a mc (non) or uc (con/non) s-mode message
void oc_issue_s_mode_message(oc_endpoint_t* endpoint, char* path, uint32_t group_address, const char* service_type, const uint8_t* value_data, int value_size, bool non_confirmable)
{

  #ifndef OC_OSCORE
  if (oc_init_post(path, endpoint, NULL, NULL, LOW_QOS, NULL))
  {
  #else  

  // set since method is called also with empty EP data
  endpoint->flags |= OSCORE;

  // get local device info (sia) -> always the same
  const uint16_t sia = oc_core_get_device_info()->ia;

  if (oc_init_s_mode_message_update(endpoint, path, non_confirmable, NULL))
  {
  #endif 

    // { 4: <sia>, 5: { 6: <st>, 7: <ga>, 1: <value> } }

    oc_rep_begin_root_object();
    oc_rep_i_set_int(root, 4, sia);                   // 4: <sia> 

    oc_rep_i_set_key(&root_map, 5);                   // 5:  

    CborEncoder value_map;
    cbor_encoder_create_map(&root_map, &value_map, CborIndefiniteLength);

    oc_rep_i_set_int(value, 7, group_address);        // ga

    oc_rep_i_set_text_string(value, 6, service_type); // st (w/r/a)

    if (value_size > 2)
    { // on value size > 2 it is a write request/read response with data,
      // otherwise a read request without data

      /*
       copies raw data
       [0] = open object = BF ; [1...size - 1] = data (1: xxx) ; [size] = close object = FF
       the data are prepared by the callback handler with the leading '1' such as with GET 
       to a bool = oc_rep_i_set_boolean (root, 1, true)
      */
      oc_rep_encode_raw_encoder(&value_map, &value_data[1], value_size - 2);
    }

    cbor_encoder_close_container_checked(&root_map, &value_map);

    oc_rep_end_root_object();

    #ifdef OC_DEBUG

    // debugging
    OC_INF("send s-mode to ipv6 address        : ");
    PRINTipaddr(*endpoint);
    OC_INF("send s-mode (%d) with CBOR payload : ", oc_rep_get_encoded_payload_size());
    OC_LOGbytes_OSCORE(oc_rep_get_encoder_buf(), oc_rep_get_encoded_payload_size());

    #endif

    // called only in case the static buffer was allocated 
    oc_do_s_mode_message_update();
  }
}

// copies the resource data and returns the data len by invoking the GET resource callback handler 
static int oc_s_mode_get_resource_value(const char* resource_path, uint8_t * buffer, const int buffer_size)
{
  // max value size of a resource value
  uint8_t resource_value[OC_MAX_APP_DATA_SIZE_STATIC];

  if (resource_path == NULL)
  {
    return 0;
  }

  const oc_resource_t* application_resource_with_href_match = oc_ri_get_app_resource_by_resource_path(resource_path, strlen(resource_path));
  if (!application_resource_with_href_match)
  {
    PRINT("error, application resource path not found %s", resource_path);
    return 0;
  }

  // create local request/response messages to call application callback handler
  // - local request message
  // - local response message
  // - local response buffer
  oc_request_t request_obj;
  oc_response_t response_obj; 
  oc_response_buffer_t response_buffer; 

  // empty response buffer, same initialization as oc_ri.c
  response_buffer.buffer = resource_value;
  response_buffer.buffer_size = 50;
  response_buffer.code = 0;
  response_buffer.response_length = 0;
  response_buffer.content_format = TEXT_PLAIN;
  response_buffer.max_age = 0;

  // empty response object,later filled, same initialization as oc_ri.c 
  response_obj.separate_response = NULL;
  response_obj.response_buffer = &response_buffer;

  // prepare new request from "void" with data needed for the callback GET
  // NOT the same initialization as oc_ri.c  
  request_obj.response = &response_obj;
  request_obj.request_payload = NULL;
  request_obj.query = NULL;
  request_obj.query_len = 0;
  request_obj.resource = application_resource_with_href_match; // allows (a generic) application callback to identify the caller
  request_obj.origin = NULL; // not known at this point
  request_obj._payload = NULL;
  request_obj._payload_len = 0;
  request_obj.request_method = OC_POST; // s-mode messaging via /k uses only POST, w/r/a flags define if it is a read/write/ update
  request_obj.content_format = APPLICATION_CBOR;
  request_obj.accept = APPLICATION_CBOR; // a GET handler WILL check this
  request_obj.uri_path = resource_path; // allows (a generic) application callback to identify the caller
  request_obj.uri_path_len = strlen(resource_path);

  // init CBOR response buffer,
  // callback handler will fill this buffer with 'oc_rep_i_set_boolean' or similar calls
  oc_rep_new(response_buffer.buffer, (int) response_buffer.buffer_size);

  // call application handler GET with own interface/ user data
  // (it makes no sense to call it with a fix vale)
  application_resource_with_href_match->get_handler.cb(&request_obj, 
                              application_resource_with_href_match->get_handler.interface_mask, 
                              application_resource_with_href_match->get_handler.user_data);

  // get the ptr + size of - from callback - filled data 
  int resource_value_size = oc_rep_get_encoded_payload_size();
  uint8_t* resource_value_data = request_obj.response->response_buffer->buffer;

  // cache resource value data to handed over 'buffer'
  if (resource_value_size < buffer_size)
  {
    // copy to 'buffer' from response buffer data
    memcpy(buffer, resource_value_data, resource_value_size);
    return resource_value_size;
  }
  OC_ERR(" allocated buffer too small to contain s-mode resource value");
  return 0;
}

int oc_send_s_mode_mc_or_uc_message(uint8_t scope, const char* resource_path, const char* srv_type)
{
  PRINT("scope = %d url = %s service type = %s", scope, resource_path, srv_type);

  // max resource application value size, note that here a literal with #define must be used
  uint8_t resource_value_buffer[OC_MAX_APP_DATA_SIZE_STATIC];

  if (!resource_path)
  {
    OC_ERR("oc_do_s_mode_with_scope_internal: resource url is NULL");
    return -1;
  }

  const oc_device_info_t* const  device = oc_core_get_device_info();

  if (!oc_is_device_in_runtime())
  {
    PRINT("device is not running, load state is: %d", device->lsm_s);
    return -1;
  }

  // find application resource by resource path
  const oc_resource_t* my_resource = oc_ri_get_app_resource_by_resource_path(resource_path, strlen(resource_path));
  if (!my_resource)
  {
    PRINT("error application callback with resource path %s not found", resource_path);
    return -1;
  }

  const oc_group_object_table_t* go_entry = oc_core_find_sending_ga_in_pos_zero_for_href(resource_path);
  if (go_entry && go_entry->cflags & OC_CFLAG_TRANSMISSION)
  { 
    // sending ga is always in position zero
    const uint32_t sending_ga = go_entry->ga[0];

    if (strcmp(srv_type, "r") == 0)
    { // issue a read request, with a sending GA that is able to transmit...

      // find recipient entry for sending ga (is always in  position zero), contains both grpid and non flag
      oc_group_table_t* recipient = oc_find_recipient_by_ga(sending_ga);
      if (recipient)
      {
        if (recipient->grpid > 0)
        { // grpid is set in case of multicast in RCP table (configured by MaC)

          PRINT("grpid > 0, send mc via sending ga");

          // multicast read, NO value data needed
          oc_send_s_mode_non_confirmable_multicast_message(scope, recipient->grpid, sending_ga, srv_type, NULL, 0);
        }
        else
        { // uc: request -> ia is used from RCP table (configured by MaC)

          PRINT("grpid = 0, send uc via sending ga");

          // unicast read, NO value data needed
          oc_send_s_mode_unicast_message(sending_ga, srv_type, NULL, 0, recipient);
        }
      }
      return 0;
    }
    if (strcmp(srv_type, "w") == 0)
    { // issue a write request, with a sending GA that is able to transmit...

      // copy resource value to buffer, return value size
      const int resource_value_size = oc_s_mode_get_resource_value(resource_path, resource_value_buffer, sizeof(resource_value_buffer));

      // Find recipient entry (contains both grpid and non flag)
      oc_group_table_t* recipient = oc_find_recipient_by_ga(sending_ga);
      if (recipient)
      {
        if (recipient->grpid > 0)
        { // mc: request -> grpid is used from RCP table (configured by MaC)

          // multicast write, value data needed
          oc_send_s_mode_non_confirmable_multicast_message(scope, recipient->grpid, sending_ga, srv_type,
                                                           resource_value_buffer, resource_value_size);
        }
        else
        { // uc: request -> ia is used from RCP table (configured by MaC)

          // unicast write, value data needed
          oc_send_s_mode_unicast_message(sending_ga, srv_type, resource_value_buffer, resource_value_size, recipient);
        }
      }

      // update internal GOs on a write request
      {
        PRINT("checking & updating internal group objects");

        // get FIRST GO index with that GA included (one out of 0...max of GO array)
        int go_table_index_where_ga_is_used = oc_core_find_first_go_table_index_with_ga(sending_ga);

        while (go_table_index_where_ga_is_used != -1)
        {
          // for all GOs with the GA included -> update the values
          oc_string_t go_href = oc_core_get_href_from_group_object_table_index(go_table_index_where_ga_is_used);

          const oc_resource_t* application_resource_with_href_match =
            oc_ri_get_app_resource_by_resource_path(oc_string(go_href), oc_string_len(go_href));

          if (!application_resource_with_href_match)
          {
            /*
           - group object table and application resource definition see above
           - POST /k with write on GA 39
           - if the first GO href entry does not have a matching application resource
             option 1 :
             1. first GO is GO5 -> no application resource
             2. stop and return
             option 2 (used):
             1. first GO is GO5 -> no application resource
             2. search GO table for next href with GA included -> GO6 -> AR3
             3. update AR3
             4. return

            */

            // get NEXT GO array index (NOT GO table id) with the GA included (out of last...max GO table entries)
            go_table_index_where_ga_is_used = oc_core_find_next_go_table_index_with_ga(sending_ga, go_table_index_where_ga_is_used);
            continue;
          }

          // device EP present (sanity check, GO without href is usually a product problem or MAC configuration error)
          if (oc_string_len(go_href) > 0)
          {
            // get GO c-flags
            const oc_cflag_mask_t cflags =
              oc_core_get_cflags_from_group_object_table_index(go_table_index_where_ga_is_used);

            if (cflags & OC_CFLAG_WRITE && application_resource_with_href_match->put_handler.cb)
            { // update the resource internally, BUT only all GOs with w-cflag set

              // copy in CBOR object the CBOR encoded resource data from original write request
              oc_rep_t* cbor_object_ptr;
              struct oc_memb cbor_object = {sizeof(oc_rep_t), 0, 0, 0, 0};
              oc_rep_set_pool(&cbor_object);
              oc_parse_rep(resource_value_buffer, resource_value_size, &cbor_object_ptr);

              // prepare new request from "void" with data needed for the callback PUT
              // NOT the same initialization as oc_ri.c
              oc_request_t request_obj;

              request_obj.response = NULL; // no response expected
              request_obj.request_payload = cbor_object_ptr; // place CBOR payload pointer for PUT
              request_obj.query = NULL;
              request_obj.query_len = 0;
              request_obj.resource = application_resource_with_href_match; // allows (a generic) application callback to identify the caller
              request_obj.origin = NULL; // not known here
              request_obj._payload = NULL;
              request_obj._payload_len = 0;
              request_obj.request_method = OC_PUT; // A PUT handler MAY check this
              request_obj.content_format = APPLICATION_CBOR;
              request_obj.accept = APPLICATION_CBOR; // a PUT MAY need it for response payload with 2.04
              request_obj.uri_path = oc_string(go_href); // allows (a generic) app. callback to identify the caller resource path
              request_obj.uri_path_len = oc_string_len(go_href);

              // call application handler with own interface/ user data
              // (it makes no sense to call it with a fix vale)
              application_resource_with_href_match->put_handler.cb(&request_obj,
                                                                   application_resource_with_href_match->put_handler.interface_mask,
                                                                   application_resource_with_href_match->put_handler.user_data);
            }

            go_table_index_where_ga_is_used = oc_core_find_next_go_table_index_with_ga(sending_ga, go_table_index_where_ga_is_used);
          }
        }
      }

      // notify on a (write) change on the original resource (not the internal updated resources)
      oc_notify_observers(my_resource);

      return 0;
    }
    
    // must be one of w/r
    OC_ERR("service type value incorrect %s , allowed are only w+r", srv_type);
   
    return -1;
  }

  /*
    no sending, this is not automatically an error
    - the 'resource path' cannot be found, the caller writes to 'something'
    - the t-flag is not set
  */
  
  OC_WRN("sending for the resource path %s not possible, path not found or cflags not in 't' mode", resource_path);
  return -1;
}

// ----------------------------------------------------------------------------
// CoAP Discovery for IPv6 Resolution
// ----------------------------------------------------------------------------

// response handler for CoAP discovery
static void knx_coap_discovery_response_handler(oc_client_response_t *data)
{
  if (!data || !data->endpoint) 
  {
    OC_ERR("CoAP discovery: Invalid response data");
    return;
  }

  // extract IPv6 address from source endpoint
  if (!(data->endpoint->flags & IPV6)) 
  {
    OC_ERR("CoAP discovery: Response not from IPv6 endpoint");
    return;
  }

  // get recipient (pointer) that was issued as suer data with the callback 
  oc_group_table_t* recipient = (oc_group_table_t*)data->user_data;
  
  OC_INF("CoAP discovery response: IPv6 %02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x (if=%d) ",
         data->endpoint->addr.ipv6.address[0], data->endpoint->addr.ipv6.address[1], data->endpoint->addr.ipv6.address[2],
         data->endpoint->addr.ipv6.address[3], data->endpoint->addr.ipv6.address[4], data->endpoint->addr.ipv6.address[5],
         data->endpoint->addr.ipv6.address[6], data->endpoint->addr.ipv6.address[7], data->endpoint->addr.ipv6.address[8],
         data->endpoint->addr.ipv6.address[9], data->endpoint->addr.ipv6.address[10], data->endpoint->addr.ipv6.address[11],
         data->endpoint->addr.ipv6.address[12], data->endpoint->addr.ipv6.address[13], data->endpoint->addr.ipv6.address[14],
         data->endpoint->addr.ipv6.address[15],
         data->endpoint->interface_index);

  // store resolved IPv6 in recipient table
  if (recipient) 
  {
    // TODO check if the response matches the beforehand request IA/IID? 

    // must be unresolved, otherwise no callback would be issued
    recipient->ipadd.init_status = OC_IP_STATUS_RESOLVED;

    // store IPv6 address, port and interface index
    memcpy(recipient->ipadd.ipv6, data->endpoint->addr.ipv6.address, 16);
    recipient->ipadd.port = data->endpoint->addr.ipv6.port;
    recipient->ipadd.interface_index = data->endpoint->interface_index;

    OC_INF("Stored IPv6 for IA 0x%04x (test mode)", (uint16_t)recipient->ia);

    // check for pending messages waiting for this resolution
    oc_knx_process_pending_messages_for_a_recipient_ia(recipient->ia);
  }
  else
  {
    OC_ERR("Recipient is NULL, callback (init) error");
  }
}

// release a pending message slot
void oc_knx_release_pending_message(int slot)
{
  g_pending_messages[slot].in_use = false;
}

// queue a message for later sending when IPv6 is resolved
int oc_knx_queue_pending_message(uint32_t ga, uint32_t sia,
                                 const char* service_type,
                                 const uint8_t* value_data, int value_size,
                                 oc_group_table_t* recipient)
{
  // validate buffer size
  if (value_size > OC_MAX_APP_DATA_SIZE_STATIC) 
  {
    OC_ERR("Value size %d exceeds maximum %d", value_size, OC_MAX_APP_DATA_SIZE_STATIC);
    return -1;
  }

  // find empty slot
  for (int i = 0; i < MAX_PENDING_MESSAGES; i++) 
  {
    if (!g_pending_messages[i].in_use) 
    { // not in use

      memset(&g_pending_messages[i], 0, sizeof(pending_s_mode_message_t));

      g_pending_messages[i].in_use = true;
      g_pending_messages[i].ga = ga;

      strncpy(g_pending_messages[i].service_type, service_type, sizeof(g_pending_messages[i].service_type) - 1);
      
      if (value_data && value_size > 0) 
      {
        memcpy(g_pending_messages[i].value_data, value_data, value_size);
        g_pending_messages[i].value_size = value_size;
      } 
      else 
      {
        g_pending_messages[i].value_size = 0;
      }
      
      g_pending_messages[i].recipient = recipient;
      g_pending_messages[i].timestamp = oc_clock_time();

      OC_INF("Queued pending %s message for IA 0x%04x, GA %u, ST=%s", recipient->non ? "NON" : "CON", (uint16_t)recipient->ia, ga, service_type);
      return i;
    }
  }

  OC_WRN("Pending message queue full, cannot queue message for IA 0x%04x", (uint16_t)recipient->ia);
  return -1;
}

// process all pending messages for a newly resolved IA
void oc_knx_process_pending_messages_for_a_recipient_ia(uint32_t ia)
{
  for (int i = 0; i < MAX_PENDING_MESSAGES; i++) 
  {
    if (!g_pending_messages[i].in_use) 
    {
      continue;
    }

    // check if this message is for the resolved IA
    if ((uint32_t)g_pending_messages[i].recipient->ia != ia)
    {
      continue;
    }
    
    const uint64_t now = oc_clock_time();

    // check for timeout
    if (now - g_pending_messages[i].timestamp > PENDING_MESSAGE_TIMEOUT_SECONDS * OC_CLOCK_SECOND) 
    {
      
      OC_WRN("Pending message for IA 0x%04x timed out after %d seconds", (uint16_t)ia, PENDING_MESSAGE_TIMEOUT_SECONDS);
      g_pending_messages[i].in_use = false;

      continue;
    }
    
    // check if IPv6 is resolved for this recipient
    if (g_pending_messages[i].recipient->ipadd.init_status == OC_IP_STATUS_RESOLVED) 
    {

      // since the IPV6 resolving is done, this below call does not end in an endless loop
      oc_send_s_mode_unicast_message(g_pending_messages[i].ga, g_pending_messages[i].service_type,
                                     g_pending_messages[i].value_data, g_pending_messages[i].value_size,
                                     g_pending_messages[i].recipient);

      OC_INF("Sending queued %s message to IA 0x%04x (GA %u) on interface %d", 
             g_pending_messages[i].recipient->non ? "NON" : "CON",
             (uint16_t)ia, 
             g_pending_messages[i].ga, 
             g_pending_messages[i].recipient->ipadd.interface_index);

      // clear the slot
      g_pending_messages[i].in_use = false;
    }
  }
}

// send CoAP discovery multicast to resolve IA to IPv6
bool knx_resolve_via_coap_discovery(uint8_t scope, uint32_t ia, uint64_t iid, oc_group_table_t* recipient)
{

  // register client callback
  const oc_client_handler_t handler = 
  {
    .response = knx_coap_discovery_response_handler, 
    .discovery = NULL, 
    .discovery_all = NULL
  };

  // flags, well-known is never secure ...
  const enum transport_flags my_transport_flags = IPV6 + MULTICAST + DISCOVERY;

  // create multicast endpoint - scope-dependent all CoAP nodes address, scope-dependent multicast address:
  // - scope 2: ff02::fd (link-local all CoAP nodes)
  // - scope 5: ff05::fd (site-local all CoAP nodes)
  oc_make_ipv6_endpoint(group_mcast_endpoint, my_transport_flags, COAP_DEFAULT_PORT, 
                        0xFF, scope, 0, 0, 
                        0,0,0,0, 
                        0,0,0,0, 
                        0,0,0,0xFD); 

  // all interfaces
  group_mcast_endpoint.interface_index = 0; 

  #define EP_STR_LEN_DOT_IA (12) // ep=knx://ia.
  #define IID_STR_LEN_MAX (10) // max IID length in hex coded ASCII if no leading zeros are omitted (5 octets = 40 bit)
  #define IA_STR_LEN_MAX (4) // max IA length in hex coded ASCII (2 octets = 16 bit)

  // build URI and query: /.well-known/core?ep=knx://ia.<iid>.<ia>
  const char uri[] = "/.well-known/core";
  char query[EP_STR_LEN_DOT_IA + IID_STR_LEN_MAX + 1 + IA_STR_LEN_MAX + 1];

  (void)snprintf(query, sizeof(query), "ep=knx://ia.%llx.%x", iid, ia);

  // user data are recipient table entry
  oc_client_cb_t* cb = oc_ri_alloc_client_cb(uri, &group_mcast_endpoint, OC_GET, query, handler, LOW_QOS, recipient);
  if (!cb)
  {
    OC_ERR("CoAP discovery: Failed to register callback");
    return false;
  }

  if (oc_init_well_known_message_update(&group_mcast_endpoint, uri, query, true, cb))
  {
    oc_do_well_known_message_update();
    return true;
  }

  OC_ERR("CoAP discovery: Failed to send discovery request");
  return false;
}