/*
 // Copyright (c) 2021-2022 Cascoda Ltd
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
#include <stdio.h>
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

oc_s_mode_response_cb_t m_s_mode_cb = NULL;

// external definition
static void oc_send_s_mode(oc_endpoint_t* endpoint, char* path, uint32_t sia_value, uint32_t group_address, const char* service_type, uint8_t* value_data, int value_size);

static int oc_s_mode_get_resource_value(const char* resource_path, uint8_t* buffer, int buffer_size);

static oc_discovery_flags_t discovery_ia_cb(const char* payload, const int len, oc_endpoint_t* endpoint, void* user_data)
{
  (void) payload;
  (void) len;
  uint8_t buffer[100];

  // debugging
  OC_DBG("discovery_ia_cb");
  oc_endpoint_print(endpoint);

  oc_device_info_t* device = oc_core_get_device_info(0);
  uint32_t sender_ia = device->ia;

  broker_s_mode_userdata_t* cb_data = user_data;

  if (cb_data->resource_url == NULL)
  {
    return OC_STOP_DISCOVERY;
  }
  if (cb_data->path == NULL)
  {
    return OC_STOP_DISCOVERY;
  }

  int value_size = oc_s_mode_get_resource_value(cb_data->resource_url, buffer, sizeof(buffer));

  oc_send_s_mode(endpoint, cb_data->path, sender_ia, cb_data->ga, cb_data->service_type, buffer, value_size);

  if (cb_data)
  {
    free(user_data);
  }

  return OC_STOP_DISCOVERY;
}

int oc_knx_client_do_broker_request(const char* resource_url, const uint64_t iid, const uint16_t ia, char* destination, char* service_type)
{
  char query[50] = "";

  char prefix[20];
  (void) snprintf(prefix, 13, "ep=knx://ia.");
  strcat(query, prefix);

  char iid_hex[20];
  oc_conv_uint64_to_hex_string(iid_hex, iid);
  strcat(query, iid_hex);

  char ia_str[11];
  (void) snprintf(ia_str, 11, ".%x", ia);
  strcat(query, ia_str);

  PRINT("oc_knx_client_do_broker_request: query=%s", query);

  // not sure if we should use a malloc here, what would happen if there are no
  // devices found? because that causes a memory leak
  broker_s_mode_userdata_t* cb_data = malloc(sizeof(broker_s_mode_userdata_t));
  if (cb_data != NULL)
  {
    memset(cb_data, 0, sizeof(broker_s_mode_userdata_t));
    cb_data->ia = ia;
    strncpy(cb_data->service_type, service_type, 2);
    strncpy(cb_data->resource_url, resource_url, 20);
    strncpy(cb_data->path, destination, 20);

    oc_do_wk_discovery_all(query, 2, discovery_ia_cb, cb_data);
    oc_do_wk_discovery_all(query, 3, discovery_ia_cb, cb_data);
    oc_do_wk_discovery_all(query, 5, discovery_ia_cb, cb_data);
  }
  else
  {
    OC_ERR("cb_data is NULL");
    return -1;
  }
  return 0;
}

int oc_is_redirected_request_from(const oc_request_t* request)
{
  if (!request)
  {
    return -1;
  }

  // /k handler set 'k' and len = 2
  if (strncmp("/k", request->uri_path, request->uri_path_len) == 0)
  {
    return 0;
  }
  // /p handler set '/p' and len = 2
  if (strncmp("/p", request->uri_path, request->uri_path_len) == 0)
  {
    return 1;
  }

  // anything else 
  return 2;
}

oc_rep_t* oc_s_mode_get_value_object(oc_request_t* request)
{

  // loop over the request 
  oc_rep_t* rep = request->request_payload;

  while (rep)
  {
    switch (rep->type)
    {
      case OC_REP_OBJECT:
      {
        // get the storage index for this object
        oc_rep_t* object = rep->value.object;
        while (object)
        {
          // search for "value" (1)
          if (object->iname == 1)
          {
            // returns the object that contains the value
            return object;
          }
          object = object->next;
        }
      } break;
      default:
        break;
    }
    rep = rep->next;
  }
  return NULL;
}

void oc_issue_s_mode(int ipv6_adr_scope, uint16_t sia_value, uint32_t grpid,
                     uint32_t group_address, uint64_t iid, const char* service_type,
                     uint8_t* value_data, int value_size)
{
  PRINT("ipv6 address scope %d", ipv6_adr_scope);

#ifdef S_MODE_ALL_COAP_NODES
#ifdef OC_OSCORE
  oc_make_ipv6_endpoint(group_mcast, IPV6 | MULTICAST | OSCORE, COAP_PORT, 0xff, -ipv6_adr_scope, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                        0, 0, 0, 0x00, 0xfd);
#else
  oc_make_ipv6_endpoint(group_mcast, IPV6 | DISCOVERY | MULTICAST, COAP_PORT, 0xff, ipv6_adr_scope, 0, 0, 0, 0, 0, 0, 0, 0,
                        0, 0, 0, 0, 0x00, 0xfd);

#endif

#else

  // using group addressing 
  oc_endpoint_t group_multicast_local_endpoint = { 0 };
  group_multicast_local_endpoint = oc_create_multicast_group_address(group_multicast_local_endpoint, grpid, iid, ipv6_adr_scope);

#endif

  // set the EP group_address, since this field is used to find the OSCORE context id
  group_multicast_local_endpoint.group_address = group_address;
  oc_send_s_mode(&group_multicast_local_endpoint, "/k", sia_value, group_address, service_type, value_data, value_size);
}

static void oc_send_s_mode(oc_endpoint_t* endpoint, char* path, uint32_t sia_value,
                           uint32_t group_address, const char* service_type, uint8_t* value_data,
                           int value_size)
{

  OC_INF("oc_send_s_mode : "); PRINTipaddr(*endpoint);

#ifndef OC_OSCORE
  if (oc_init_post(path, endpoint, NULL, NULL, LOW_QOS, NULL))
  {
#else  

  // set since method is called also with empty EP data (oc_issue_s_mode)
  endpoint->flags = endpoint->flags | OSCORE;

  if (oc_init_multicast_update(endpoint, path, NULL))
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
    OC_INF("S-MODE payload size: %d , payload = ", oc_rep_get_encoded_payload_size());
    OC_LOGbytes_OSCORE(oc_rep_get_encoder_buf(), oc_rep_get_encoded_payload_size());

  #ifndef OC_OSCORE
    if (oc_do_post_ex(APPLICATION_CBOR, APPLICATION_CBOR))
    {
      PRINT("Sent POST request\n");
    #else

    if (oc_do_multicast_update())
    {
      OC_INF("sent multicast message");

    #endif
    }
    else
    {
      OC_ERR("Could not send POST request");
    }
  }
}

// copies the resource data and returns the data len by invoking the GET resource callback handler 
static int oc_s_mode_get_resource_value(const char* resource_path, uint8_t * buffer, const int buffer_size)
{
  // max value size of a resource value
  uint8_t resource_value[50];

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
  response_buffer.content_format = 0;
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

int oc_do_s_mode_with_scope_and_check(int scope, const char* resource_path, const char* srv_type)
{
  PRINT("scope = %d url = %s service type = %s", scope, resource_path, srv_type);

  // max resource value size
  uint8_t resource_value_buffer[50];

  if (resource_path == NULL)
  {
    OC_ERR("oc_do_s_mode_with_scope_internal: resource url is NULL");
    return -1;
  }

  const oc_device_info_t* device = oc_core_get_device_info(0);
  if (device == NULL)
  {
    PRINT("device is NULL");
    return -1;
  }

  if (!oc_is_device_in_runtime(0))
  {
    PRINT("device '0' is not running, load state is: %d", device->lsm_s);
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
      // DON'T need to copy resource value to buffer

      const uint32_t grpid = oc_find_grpid_in_recipient_table(sending_ga);
      if (grpid == 0)
      {
        PRINT("error, grpid for the resource path %s not found", resource_path);
        return -1;
      }

      // multicast read, NO value data needed
      oc_issue_s_mode(scope, device->ia, grpid, sending_ga, device->iid, srv_type, resource_value_buffer, 0);
      return 0;
      
    }
    PRINT("error, sending group address for the resource path %s not found", resource_path);
    return -1;
    
  }
  if (strcmp(srv_type, "w") == 0)
  { // issue a write request

    oc_cflag_mask_t sending_cflags; 
    const int sending_ga = oc_core_find_sending_ga_in_pos_zero_for_href(resource_path, &sending_cflags);
    if (sending_ga != -1 && sending_cflags & OC_CFLAG_TRANSMISSION)
    {
      // copy resource value to buffer, return value size
      const int resource_value_size = oc_s_mode_get_resource_value(resource_path, resource_value_buffer, sizeof(resource_value_buffer));

      uint32_t grpid = oc_find_grpid_in_recipient_table(sending_ga);
      if (grpid == 0)
      {
        PRINT("error, grpid for the resource path %s not found", resource_path);
        return -1;
      }

      // multicast write, value data needed
      oc_issue_s_mode(scope, device->ia, grpid, sending_ga, device->iid, srv_type, resource_value_buffer, resource_value_size);

      // update internal GOs on a write request
      {
        PRINT("checking & updating internal group objects");

        // get FIRST GO index with that GA included (one out of 0...max of GO array)
        int go_table_index_where_ga_is_used = oc_core_find_first_go_table_index_with_ga(sending_ga);

        while (go_table_index_where_ga_is_used != -1)
        {
          // for all GOs (with the GA) with the href from the original update the values
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
            { // update the resource internally, BUT only all GOs with type if input!

              // create a CBOR object and place the CBOR encoded response data in 
              oc_rep_t* cbor_object_ptr;
              struct oc_memb cbor_object = {sizeof(oc_rep_t), 0, 0, 0, 0};
              oc_rep_set_pool(&cbor_object);
              oc_parse_rep(resource_value_buffer, resource_value_size, &cbor_object_ptr);

              oc_request_t request_obj; 

              // prepare new request from "void" with data needed for the callback PUT
              // NOT the same initialization as oc_ri.c
              request_obj.response = NULL; // no response expected
              request_obj.request_payload = cbor_object_ptr;  // place CBOR payload pointer for PUT 
              request_obj.query = NULL;
              request_obj.query_len = 0;
              request_obj.resource = application_resource_with_href_match; // allows (a generic) application callback to identify the caller
              request_obj.origin = NULL; // not known at this point
              request_obj._payload = NULL;
              request_obj._payload_len = 0;
              request_obj.request_method = OC_PUT; // A PUT handler MAY check this
              request_obj.content_format = APPLICATION_CBOR;
              request_obj.accept = APPLICATION_CBOR; // a PUT does not need it
              request_obj.uri_path = oc_string(go_href); // allows (a generic) application callback to identify the caller
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
    PRINT("error, sending group address for the resource path %s not found", resource_path);
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