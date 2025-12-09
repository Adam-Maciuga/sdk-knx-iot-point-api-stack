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

typedef struct broker_s_mode_userdata_t
{
  int ia;                 /**< internal address of the destination */
  char path[20];          /**< the path on the device designated with ia */
  uint32_t ga;            /**< group address to use */
  char service_type[3];   /**< mode to send the message "w"  = 1  "r" = 2  "a" = 3 */
  char resource_url[20];  /**< the url to pull the data from. */
} broker_s_mode_userdata_t;

// Pending message queue for unresolved unicast sends
#define MAX_PENDING_MESSAGES 10
#define PENDING_MESSAGE_TIMEOUT_SECONDS 10  /**< Timeout for queued messages */

typedef struct pending_s_mode_message_t
{
  bool in_use;
  uint32_t ia;                                     /**< individual address of recipient */
  uint32_t ga;                                     /**< group address */
  uint32_t sia;                                    /**< sender individual address */
  char service_type[3];                            /**< "w", "r", or "a" */
  uint8_t value_data[OC_MAX_APP_DATA_SIZE_STATIC]; /**< CBOR encoded value */
  int value_size;                                  /**< size of value_data */
  int recipient_index;                             /**< index in recipient table */
  uint64_t timestamp;                              /**< when this was queued (for timeout) */
} pending_s_mode_message_t;

// Use static array (OC_MEMB doesn't work with OC_DYNAMIC_ALLOCATION on Windows)
static pending_s_mode_message_t g_pending_messages[MAX_PENDING_MESSAGES] = {0};

oc_s_mode_response_cb_t m_s_mode_cb = NULL;

// external definitions

static void oc_issue_s_mode_non_confirmable_message(oc_endpoint_t* endpoint, char* path, uint32_t sia_value, uint32_t group_address, const char* service_type, uint8_t* value_data, int value_size);
static int oc_s_mode_get_resource_value(const char* resource_path, uint8_t* buffer, int buffer_size);
static int oc_knx_queue_pending_message(uint32_t ia, uint32_t ga, uint32_t sia, const char* service_type, uint8_t* value_data, int value_size, int recipient_index);

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


void oc_send_s_mode_non_confirmable_multicast_message(uint8_t scope, uint16_t sia, uint32_t grpid,
                        uint32_t group_address, uint64_t iid, const char* service_type,
                        uint8_t* value_data, int value_size)
{
  // using group addressing 
  oc_endpoint_t group_mcast_endpoint = {0};
  group_mcast_endpoint = oc_create_multicast_group_address_with_port(group_mcast_endpoint, grpid, iid, scope, COAP_DEFAULT_PORT);

  // set for the EP the sending group_address
  group_mcast_endpoint.group_address = group_address;

  oc_issue_s_mode_non_confirmable_message(&group_mcast_endpoint, "/k", sia, group_address, service_type, value_data, value_size);
}

static void oc_issue_s_mode_non_confirmable_message(oc_endpoint_t* endpoint, char* path, 
                           uint32_t sia_value,
                           uint32_t group_address, 
                           const char* service_type, 
                           uint8_t* value_data, int value_size)
{

  #ifndef OC_OSCORE
  if (oc_init_post(path, endpoint, NULL, NULL, LOW_QOS, NULL))
  {
  #else  

  // set since method is called also with empty EP data (oc_issue_s_mode)
  endpoint->flags |= OSCORE;

  if (oc_init_update(endpoint, path))
  {
  #endif 

    // { 4: <sia>, 5: { 6: <st>, 7: <ga>, 1: <value> } }

    oc_rep_begin_root_object();
    oc_rep_i_set_int(root, 4, sia_value);             // 4: <sia> 

    oc_rep_i_set_key(&root_map, 5);                   // 5:  

    CborEncoder value_map;
    cbor_encoder_create_map(&root_map, &value_map, CborIndefiniteLength);

    oc_rep_i_set_int(value, 7, group_address);        // ga

    oc_rep_i_set_text_string(value, 6, service_type); // st code(w/r/a)

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

    // debugging
    OC_INF("send s-mode to ipv6 address       : ");
    PRINTipaddr(*endpoint);
    OC_INF("send s-mode (%d)with CBOR payload : ", oc_rep_get_encoded_payload_size());
    OC_LOGbytes_OSCORE(oc_rep_get_encoder_buf(), oc_rep_get_encoded_payload_size());

    // called only in case the static buffer was allocated 
    oc_do_update();
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

  if (strcmp(srv_type, "r") == 0)
  { // issue a read request
    
    oc_cflag_mask_t sending_cflags; 
    const int sending_ga = oc_core_find_sending_ga_in_pos_zero_for_href(resource_path, &sending_cflags);
    
    if (sending_ga != -1 && sending_cflags & OC_CFLAG_TRANSMISSION)
    {
      // grpid
      const uint32_t grpid = oc_find_grpid_in_recipient_table(sending_ga);
      
      if (grpid > 0)
      { // grpid is set in case of multicast in RCP table (configured by MaC)

        PRINT("grpid > 0, send mc via sending ga");

        // multicast read, NO value data needed
        oc_send_s_mode_non_confirmable_multicast_message(scope, device->ia, grpid, sending_ga, device->iid, srv_type, resource_value_buffer, 0);
      }
      else
      { // uc: request -> ia is used from RCP table (configured by MaC)

        // Find recipient table entry with this GA to get IA/IID
        int total = oc_core_get_recipient_table_size();
        oc_group_table_t* recipient_entry = NULL;
        int recipient_index = -1;
        
        for (int i = 0; i < total; i++) {
          oc_group_table_t* entry = oc_core_get_recipient_table_entry(i);
          if (entry && entry->id >= 0) {
            // Check if this entry has the sending GA
            for (int j = 0; j < entry->ga_len; j++) {
              if (entry->ga[j] == sending_ga) {
                recipient_entry = entry;
                recipient_index = i;
                break;
              }
            }
            if (recipient_entry) break;
          }
        }

        if (!recipient_entry || recipient_entry->ia <= 0) {
          OC_ERR("Cannot send unicast read: no recipient entry found for GA %d", sending_ga);
          return -1;
        }

        // Check if IPv6 is resolved
        if (recipient_entry->ipadd.init_status != OC_IP_STATUS_RESOLVED) {
          OC_INF("IPv6 not resolved for IA 0x%x, queuing message and triggering resolution", recipient_entry->ia);
          
          // Queue the message
          oc_knx_queue_pending_message(recipient_entry->ia, sending_ga, device->ia, 
                                        srv_type, resource_value_buffer, 0, recipient_index);
          
          // Trigger resolution
          int ret = knx_resolve_via_coap_discovery(recipient_entry->ia, device->iid, recipient_index);
          if (ret != 0) {
            OC_ERR("Failed to trigger IPv6 resolution for IA 0x%x", recipient_entry->ia);
            return -1;
          }
          
          OC_INF("IPv6 resolution started for IA 0x%x, message queued for later delivery", recipient_entry->ia);
          return 0; // Success - message is queued
        }

        // Create unicast endpoint from resolved IPv6
        oc_endpoint_t uc_endpoint = {0};
        uc_endpoint.flags = IPV6 | SECURED;
        uc_endpoint.addr.ipv6.port = COAP_DEFAULT_PORT;
        memcpy(uc_endpoint.addr.ipv6.address, recipient_entry->ipadd.ipv6, 16);
        uc_endpoint.interface_index = recipient_entry->ipadd.interface_index;  // Required for link-local
        uc_endpoint.group_address = sending_ga;  // Set GA for OSCORE context lookup
        uc_endpoint.auth_at_index = -1;  // Force OSCORE to use group_address (case c), not auth_at (case a)

        PRINT("Sending unicast read to IA 0x%x via resolved IPv6 (interface %d)", recipient_entry->ia, uc_endpoint.interface_index);

        // Send unicast read request (no value data needed)
        oc_issue_s_mode_non_confirmable_message(&uc_endpoint, "/k", device->ia, sending_ga, srv_type, resource_value_buffer, 0);
      }
      return 0;
    }

    /*
      no sending, this is not automatically an error
      - the 'resource path' cannot be found, the caller writes to 'something'
      - the t-flag is not set
    */

    OC_WRN("sending for the resource path %s not possible sending ga = %d , flags = %d", resource_path, sending_ga, sending_cflags);
    return -1;
    
  }
  if (strcmp(srv_type, "w") == 0)
  { // issue a write request

    oc_cflag_mask_t sending_cflags; 
    const int sending_ga = oc_core_find_sending_ga_in_pos_zero_for_href(resource_path, &sending_cflags);

    if (sending_ga != -1 && sending_cflags & OC_CFLAG_TRANSMISSION)
    { // here we have a sending GA that is able to transmit...

      // copy resource value to buffer, return value size
      const int resource_value_size = oc_s_mode_get_resource_value(resource_path, resource_value_buffer, sizeof(resource_value_buffer));

      // grpid
      const uint32_t grpid = oc_find_grpid_in_recipient_table(sending_ga);
      if (grpid > 0)
      { // mc: request -> grpid is used from RCP table (configured by MaC)

        // multicast write, value data needed
        oc_send_s_mode_non_confirmable_multicast_message(scope, device->ia, grpid, sending_ga, device->iid, srv_type, resource_value_buffer, resource_value_size);

      }
      else
      { // uc: request -> ia is used from RCP table (configured by MaC)

        // Find recipient table entry with this GA to get IA/IID
        int total = oc_core_get_recipient_table_size();
        oc_group_table_t* recipient_entry = NULL;
        int recipient_index = -1;
        
        for (int i = 0; i < total; i++) {
          oc_group_table_t* entry = oc_core_get_recipient_table_entry(i);
          if (entry && entry->id >= 0) {
            // Check if this entry has the sending GA
            for (int j = 0; j < entry->ga_len; j++) {
              if (entry->ga[j] == sending_ga) {
                recipient_entry = entry;
                recipient_index = i;
                break;
              }
            }
            if (recipient_entry) break;
          }
        }

        if (!recipient_entry || recipient_entry->ia <= 0) {
          OC_ERR("Cannot send unicast write: no recipient entry found for GA %d", sending_ga);
          return -1;
        }

        // Check if IPv6 is resolved
        if (recipient_entry->ipadd.init_status != OC_IP_STATUS_RESOLVED) {
          OC_INF("IPv6 not resolved for IA 0x%x, queuing message and triggering resolution", recipient_entry->ia);
          
          // Queue the message
          oc_knx_queue_pending_message(recipient_entry->ia, sending_ga, device->ia, 
                                        srv_type, resource_value_buffer, resource_value_size, recipient_index);
          
          // Trigger resolution
          int ret = knx_resolve_via_coap_discovery(recipient_entry->ia, device->iid, recipient_index);
          if (ret != 0) {
            OC_ERR("Failed to trigger IPv6 resolution for IA 0x%x", recipient_entry->ia);
            return -1;
          }
          
          OC_INF("IPv6 resolution started for IA 0x%x, message queued for later delivery", recipient_entry->ia);
          return 0; // Success - message is queued
        }

        // Create unicast endpoint from resolved IPv6
        oc_endpoint_t uc_endpoint = {0};
        uc_endpoint.flags = IPV6 | SECURED;
        uc_endpoint.addr.ipv6.port = COAP_DEFAULT_PORT;
        memcpy(uc_endpoint.addr.ipv6.address, recipient_entry->ipadd.ipv6, 16);
        uc_endpoint.interface_index = recipient_entry->ipadd.interface_index;  // Required for link-local
        uc_endpoint.group_address = sending_ga;  // Set GA for OSCORE context lookup
        uc_endpoint.auth_at_index = -1;  // Force OSCORE to use group_address (case c), not auth_at (case a)

        PRINT("Sending unicast write to IA 0x%x via resolved IPv6 (interface %d)", recipient_entry->ia, uc_endpoint.interface_index);

        // Send unicast write request with value data
        oc_issue_s_mode_non_confirmable_message(&uc_endpoint, "/k", device->ia, sending_ga, srv_type, resource_value_buffer, resource_value_size);
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

          const oc_resource_t* application_resource_with_href_match = oc_ri_get_app_resource_by_resource_path(oc_string(go_href), oc_string_len(go_href));

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
            const oc_cflag_mask_t cflags = oc_core_get_cflags_from_group_object_table_index(go_table_index_where_ga_is_used);

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
              request_obj.request_payload = cbor_object_ptr;  // place CBOR payload pointer for PUT 
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
      
      return 0 ;
    }

    /*
      no sending, this is not automatically an error
      - the 'resource path' cannot be found, the caller writes to 'something'
      - the t-flag is not set 
    */
    OC_WRN("sending for the resource path %s not possible", resource_path);
    return -1;
  }

  // must be one of w/r
  OC_ERR("service type value incorrect %s , allowed are only w+r", srv_type);
  return -1;
}

bool oc_set_s_mode_response_cb(oc_s_mode_response_cb_t my_func)
{
  m_s_mode_cb = my_func;
  return true;
}

oc_s_mode_response_cb_t oc_get_s_mode_response_cb(void)
{
  return m_s_mode_cb;
}

// ----------------------------------------------------------------------------
// Confirmable unicast s-mode send
// ----------------------------------------------------------------------------

int oc_send_s_mode_confirmable_unicast_message(oc_endpoint_t* endpoint, 
                                                 uint32_t sia,
                                                 uint32_t ga,
                                                 const char* service_type,
                                                 uint8_t* value_data,
                                                 int value_size)
{
  if (!endpoint || !service_type) {
    OC_ERR("Invalid parameters for confirmable unicast");
    return -1;
  }

  // Allocate message buffer
  oc_message_t *message = oc_internal_allocate_outgoing_message();
  if (!message) {
    OC_ERR("Failed to allocate outgoing message");
    return -1;
  }

  // Copy endpoint
  memcpy(&message->endpoint, endpoint, sizeof(oc_endpoint_t));
  message->endpoint.flags |= OSCORE;

  // Initialize CBOR encoder
  oc_rep_new(message->data + COAP_MAX_HEADER_SIZE, OC_BLOCK_SIZE);

  // Create CoAP confirmable message
  coap_packet_t request[1];
  coap_udp_init_message(request, COAP_TYPE_CON, OC_POST, coap_get_next_mid());
  coap_set_header_accept(request, APPLICATION_CBOR);
  coap_set_header_uri_path(request, "/k", 2);

  // Generate 8-byte token
  request->token_len = 8;
  uint32_t r1 = oc_random_value();
  uint32_t r2 = oc_random_value();
  memcpy(request->token, &r1, 4);
  memcpy(request->token + 4, &r2, 4);

  // Build s-mode CBOR payload: { 4: <sia>, 5: { 6: <st>, 7: <ga>, 1: <value> } }
  oc_rep_begin_root_object();
  oc_rep_i_set_int(root, 4, sia);
  oc_rep_i_set_key(&root_map, 5);

  CborEncoder value_map;
  cbor_encoder_create_map(&root_map, &value_map, CborIndefiniteLength);
  oc_rep_i_set_int(value, 7, ga);
  oc_rep_i_set_text_string(value, 6, service_type);

  if (value_size > 2) {
    oc_rep_encode_raw_encoder(&value_map, &value_data[1], value_size - 2);
  }

  cbor_encoder_close_container_checked(&root_map, &value_map);
  oc_rep_end_root_object();

  // Set payload
  int payload_size = oc_rep_get_encoded_payload_size();
  coap_set_payload(request, oc_rep_get_encoder_buf(), payload_size);

  OC_INF("Sending CON s-mode to IA, payload size: %d", payload_size);
  PRINTipaddr(*endpoint);

  // Serialize into message buffer
  message->length = coap_serialize_message(request, message->data);
  if (message->length == 0) {
    OC_ERR("Failed to serialize CoAP message");
    oc_message_unref(message);
    return -1;
  }

  // Send the message
  oc_send_buffer(message);
  oc_message_unref(message);

  return 0;
}

// ----------------------------------------------------------------------------
// CoAP Discovery for IPv6 Resolution
// ----------------------------------------------------------------------------

// Response handler for CoAP discovery
static void knx_coap_discovery_response_handler(oc_client_response_t *data)
{
  if (!data || !data->endpoint) {
    OC_ERR("CoAP discovery: Invalid response data");
    return;
  }

  // Extract IPv6 address from source endpoint
  if (!(data->endpoint->flags & IPV6)) {
    OC_ERR("CoAP discovery: Response not from IPv6 endpoint");
    return;
  }

  uint8_t resolved_ipv6[16];
  memcpy(resolved_ipv6, data->endpoint->addr.ipv6.address, 16);

  int recipient_index = (int)(intptr_t)data->user_data;
  
  OC_INF("CoAP discovery response: IPv6 %02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x (if=%d)",
         resolved_ipv6[0], resolved_ipv6[1], resolved_ipv6[2], resolved_ipv6[3],
         resolved_ipv6[4], resolved_ipv6[5], resolved_ipv6[6], resolved_ipv6[7],
         resolved_ipv6[8], resolved_ipv6[9], resolved_ipv6[10], resolved_ipv6[11],
         resolved_ipv6[12], resolved_ipv6[13], resolved_ipv6[14], resolved_ipv6[15],
         data->endpoint->interface_index);

  // Store resolved IPv6 in recipient table
  if (recipient_index < 0) {
    // Test mode: search for first unresolved entry
    int total = oc_core_get_recipient_table_size();
    for (int i = 0; i < total; i++) {
      oc_group_table_t* entry = oc_core_get_recipient_table_entry(i);
      if (entry && entry->ipadd.init_status != OC_IP_STATUS_RESOLVED) {
        memcpy(entry->ipadd.ipv6, resolved_ipv6, 16);
        entry->ipadd.init_status = OC_IP_STATUS_RESOLVED;
        entry->ipadd.interface_index = data->endpoint->interface_index;
        OC_INF("Stored IPv6 for IA 0x%x (test mode)", entry->ia);
        return;
      }
    }
    return;
  }

  // Production mode: use recipient_index directly
  oc_group_table_t* entry = oc_core_get_recipient_table_entry(recipient_index);
  if (!entry) {
    OC_ERR("Invalid recipient index %d", recipient_index);
    return;
  }

  // Store IPv6 address and interface index
  memcpy(entry->ipadd.ipv6, resolved_ipv6, 16);
  entry->ipadd.interface_index = data->endpoint->interface_index;
  entry->ipadd.init_status = OC_IP_STATUS_RESOLVED;

  OC_INF("Stored IPv6 for IA 0x%x (recipient index %d)", entry->ia, recipient_index);

  // Check for pending messages waiting for this resolution
  oc_knx_process_pending_messages_for_ia(entry->ia);
}

// Queue a message for later sending when IPv6 is resolved
static int oc_knx_queue_pending_message(uint32_t ia, uint32_t ga, uint32_t sia, 
                                         const char* service_type, 
                                         uint8_t* value_data, int value_size,
                                         int recipient_index)
{
  // Validate buffer size
  if (value_size > OC_MAX_APP_DATA_SIZE_STATIC) {
    OC_ERR("Value size %d exceeds maximum %d", value_size, OC_MAX_APP_DATA_SIZE_STATIC);
    return -1;
  }

  // Find empty slot
  for (int i = 0; i < MAX_PENDING_MESSAGES; i++) {
    if (!g_pending_messages[i].in_use) {
      memset(&g_pending_messages[i], 0, sizeof(pending_s_mode_message_t));
      g_pending_messages[i].in_use = true;
      g_pending_messages[i].ia = ia;
      g_pending_messages[i].ga = ga;
      g_pending_messages[i].sia = sia;
      strncpy(g_pending_messages[i].service_type, service_type, sizeof(g_pending_messages[i].service_type) - 1);
      
      if (value_data && value_size > 0) {
        memcpy(g_pending_messages[i].value_data, value_data, value_size);
        g_pending_messages[i].value_size = value_size;
      } else {
        g_pending_messages[i].value_size = 0;
      }
      
      g_pending_messages[i].recipient_index = recipient_index;
      g_pending_messages[i].timestamp = oc_clock_time();
      
      OC_INF("Queued pending message for IA 0x%x, GA %d, ST=%s", ia, ga, service_type);
      return 0;
    }
  }
  
  OC_WRN("Pending message queue full, cannot queue message for IA 0x%x", ia);
  return -1;
}

// Process all pending messages for a newly resolved IA
void oc_knx_process_pending_messages_for_ia(uint32_t ia)
{
  int processed_count = 0;
  uint64_t now = oc_clock_time();
  
  for (int i = 0; i < MAX_PENDING_MESSAGES; i++) {
    if (g_pending_messages[i].in_use && g_pending_messages[i].ia == ia) {
      // Check for timeout
      if ((now - g_pending_messages[i].timestamp) > (PENDING_MESSAGE_TIMEOUT_SECONDS * OC_CLOCK_SECOND)) {
        OC_WRN("Pending message for IA 0x%x timed out after %d seconds", 
               ia, PENDING_MESSAGE_TIMEOUT_SECONDS);
        g_pending_messages[i].in_use = false;
        continue;
      }
      
      // Get recipient entry
      oc_group_table_t* recipient_entry = oc_core_get_recipient_table_entry(g_pending_messages[i].recipient_index);
      
      if (!recipient_entry) {
        OC_ERR("Recipient entry %d is NULL", g_pending_messages[i].recipient_index);
        g_pending_messages[i].in_use = false;
        continue;
      }
      
      OC_INF("  Recipient status: %d (RESOLVED=%d)", 
             recipient_entry->ipadd.init_status, OC_IP_STATUS_RESOLVED);
      if (recipient_entry->ipadd.init_status == OC_IP_STATUS_RESOLVED) {
        // Build unicast endpoint
        oc_endpoint_t uc_endpoint = {0};
        uc_endpoint.flags = IPV6 | SECURED;
        uc_endpoint.addr.ipv6.port = COAP_DEFAULT_PORT;
        memcpy(uc_endpoint.addr.ipv6.address, recipient_entry->ipadd.ipv6, 16);
        uc_endpoint.interface_index = recipient_entry->ipadd.interface_index;
        uc_endpoint.group_address = g_pending_messages[i].ga;
        uc_endpoint.auth_at_index = -1;
        
        OC_INF("Sending queued message to IA 0x%x (GA %d) on interface %d", 
               ia, g_pending_messages[i].ga, uc_endpoint.interface_index);
        
        // Send the message
        oc_issue_s_mode_non_confirmable_message(&uc_endpoint, "/k", 
                                                 g_pending_messages[i].sia,
                                                 g_pending_messages[i].ga,
                                                 g_pending_messages[i].service_type,
                                                 g_pending_messages[i].value_data,
                                                 g_pending_messages[i].value_size);
        
        // Clear the slot
        g_pending_messages[i].in_use = false;
        processed_count++;
      }
    }
  }
  
  if (processed_count > 0) {
    OC_INF("Sent %d queued message(s) for IA 0x%x", processed_count, ia);
  }
}

// Send CoAP discovery multicast to resolve IA to IPv6
int knx_resolve_via_coap_discovery(uint32_t ia, uint64_t iid, int recipient_index)
{
  extern oc_message_t *oc_internal_allocate_outgoing_message(void);

  // Create multicast endpoint - ff02::fd (link-local all CoAP nodes, scope 2)
  oc_endpoint_t mcast_ep = {0};
  mcast_ep.flags = IPV6 | MULTICAST;
  mcast_ep.addr.ipv6.port = COAP_DEFAULT_PORT;
  
  // ff02::fd = link-local all CoAP nodes
  static const uint8_t ff02_fd[16] = {0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xfd};
  memcpy(mcast_ep.addr.ipv6.address, ff02_fd, 16);
  mcast_ep.interface_index = 0; // All interfaces

  // Allocate outgoing message
  oc_message_t *message = oc_internal_allocate_outgoing_message();
  if (!message) {
    OC_ERR("CoAP discovery: Failed to allocate message");
    return -1;
  }

  // Copy endpoint to message
  memcpy(&message->endpoint, &mcast_ep, sizeof(oc_endpoint_t));

  // Build URI and query: /.well-known/core?ep=knx://ia.<iid>.<ia>
  char uri[256];
  char query[128];
  snprintf(uri, sizeof(uri), "/.well-known/core");
  snprintf(query, sizeof(query), "ep=knx://ia.%llx.%x", (unsigned long long)iid, ia);

  OC_INF("Sending CoAP discovery for IA 0x%x (%s?%s)", ia, uri, query);

  // Register client callback
  oc_client_handler_t handler = {
    .response = knx_coap_discovery_response_handler,
    .discovery = NULL,
    .discovery_all = NULL
  };

  extern oc_client_cb_t* oc_ri_alloc_client_cb(const char* uri, oc_endpoint_t *endpoint,
                                                 oc_method_t method, const char* query,
                                                 oc_client_handler_t handler, oc_qos_t qos,
                                                 void* user_data);
  oc_client_cb_t *cb = oc_ri_alloc_client_cb(uri, &message->endpoint, OC_GET, query,
                                               handler, LOW_QOS,
                                               (void*)(intptr_t)recipient_index);
  if (!cb) {
    OC_ERR("CoAP discovery: Failed to register callback");
    oc_message_unref(message);
    return -1;
  }

  // Build CoAP GET request
  coap_packet_t request[1];
  coap_udp_init_message(request, COAP_TYPE_NON, COAP_GET, cb->mid);
  memcpy(request->token, cb->token, cb->token_len);
  request->token_len = cb->token_len;

  // Set URI-Path and URI-Query as separate options
  coap_set_header_uri_path(request, uri, strlen(uri));
  coap_set_header_uri_query(request, query);
  coap_set_header_accept(request, APPLICATION_LINK_FORMAT);

  // Serialize message
  message->length = coap_serialize_message(request, message->data);
  if (message->length == 0) {
    OC_ERR("CoAP discovery: Failed to serialize message");
    extern oc_event_callback_retval_t oc_ri_remove_client_cb(void* data);
    oc_ri_remove_client_cb(cb);
    oc_message_unref(message);
    return -1;
  }

  // Send via oc_send_discovery_request (handles multi-interface)
  extern void oc_send_discovery_request(oc_message_t *message);
  oc_send_discovery_request(message);
  oc_message_unref(message);

  return 0;
}

// ----------------------------------------------------------------------------
