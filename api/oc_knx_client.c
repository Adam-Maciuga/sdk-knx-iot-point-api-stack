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

#define MAX_SECRET_LEN (32)
#define MAX_PASSWORD_LEN (30)


typedef struct broker_s_mode_userdata_t
{
  int ia;                 /**< internal address of the destination */
  char path[20];          /**< the path on the device designated with ia */
  uint32_t ga;            /**< group address to use */
  char service_type[3];   /**< mode to send the message "w"  = 1  "r" = 2  "a" = 3 */
  char resource_url[20];  /**< the url to pull the data from. */
} broker_s_mode_userdata_t;

oc_s_mode_response_cb_t m_s_mode_cb = NULL;

static void oc_send_s_mode(oc_endpoint_t* endpoint, char* path, uint32_t sia_value, uint32_t group_address, char* service_type, uint8_t* value_data, int value_size);

static int oc_s_mode_get_resource_value(const char* resource_url, uint8_t* buf, int buf_size);

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

  int value_size = oc_s_mode_get_resource_value(cb_data->resource_url, buffer, 100);

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
  return -1;
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

void oc_issue_s_mode(int ipv6_adr_scope, uint16_t sia_value, const uint32_t grpid,
                     const uint32_t group_address, const uint64_t iid, char* mode,
                     uint8_t* value_data, const int value_size)
{
  PRINT("oc_issue_s_mode : ipv6 address scope %d", ipv6_adr_scope);

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
  oc_send_s_mode(&group_multicast_local_endpoint, "/k", sia_value, group_address, mode, value_data, value_size);
}

static void oc_send_s_mode(oc_endpoint_t* endpoint, char* path, const uint32_t sia_value,
                           const uint32_t group_address, char* service_type, uint8_t* value_data,
                           const int value_size)
{

  OC_INF("oc_send_s_mode : ");
  PRINTipaddr(*endpoint);

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

    oc_rep_i_set_key(&root_map, 5)                    // 5:  

    CborEncoder value_map;
    cbor_encoder_create_map(&root_map, &value_map, CborIndefiniteLength);
    oc_rep_i_set_int(value, 7, group_address);        // ga

    oc_rep_i_set_text_string(value, 6, service_type); // st code(w/r/a)

    if (value_size > 2)
    {
      // [0] = open object / [size] = close object
      oc_rep_encode_raw_encoder(&value_map, &value_data[1], value_size - 2);
    }

    cbor_encoder_close_container_checked(&root_map, &value_map);

    oc_rep_end_root_object();

    // debugging
    OC_INF("oc_send_s_mode: S-MODE Payload Size: %d", oc_rep_get_encoded_payload_size());
    OC_LOGbytes_OSCORE(oc_rep_get_encoder_buf(), oc_rep_get_encoded_payload_size());

  #ifndef OC_OSCORE
    if (oc_do_post_ex(APPLICATION_CBOR, APPLICATION_CBOR))
    {
      PRINT("Sent POST request\n");
    #else
    if (oc_do_multicast_update())
    {
      OC_INF("Sent oc_do_multicast_update update");
    #endif
    }
    else
    {
      OC_ERR("Could not send POST request");
    }
  }
}

// copies the resource data and returns the data len
static int oc_s_mode_get_resource_value(const char* resource_url, uint8_t * buf, const int buf_size)
{
  // max value size of a resource value
  uint8_t buffer[50];

  if (resource_url == NULL)
  {
    return 0;
  }

  const oc_resource_t* my_resource = oc_ri_get_app_resource_by_uri(resource_url, strlen(resource_url), 0);
  if (my_resource == NULL)
  {
    PRINT("oc_do_s_mode : error no URL found %s", resource_url);
    return 0;
  }

  // local messages
  oc_request_t request;
  oc_response_t response;
  oc_response_buffer_t response_buffer;

  // assign data buffer
  response_buffer.buffer = buffer;
  response_buffer.buffer_size = 50;

  // same initialization as oc_ri.c
  response_buffer.code = 0;
  response_buffer.response_length = 0;
  response_buffer.content_format = 0;
  response_buffer.max_age = 0;

  response.separate_response = NULL;
  response.response_buffer = &response_buffer;

  request.response = &response;
  request.request_payload = NULL;
  request.query = NULL;
  request.query_len = 0;
  request.resource = NULL;
  request.origin = NULL;
  request._payload = NULL;
  request._payload_len = 0;
  request.request_method = OC_POST;

  request.content_format = APPLICATION_CBOR;
  request.accept = APPLICATION_CBOR;
  request.uri_path = resource_url;
  request.uri_path_len = strlen(resource_url);

  // init CBOR buffer 
  oc_rep_new(response_buffer.buffer, (int) response_buffer.buffer_size);

  // set callback handler for the GET with callback, interface type and user data
  my_resource->get_handler.cb(&request, OC_IF_NONE, my_resource->get_handler.user_data);

  // get the size (see above)
  int value_size = oc_rep_get_encoded_payload_size();
  uint8_t* value_data = request.response->response_buffer->buffer;

  // cache value data to handed over 'buffer', as it gets overwritten in oc_issue_do_s_mode
  if (value_size < buf_size)
  {
    memcpy(buf, value_data, value_size);
    return value_size;
  }
  OC_ERR(" allocated buffer too small to contain s-mode value");
  return 0;
}



void oc_do_s_mode_with_scope_and_check(const int scope, const char* resource_url, char* srv_type, bool consider_transmission_flag)
{
  PRINT("oc_do_s_mode_with_scope_and_check scope = %d url = %s rp=%s", scope, resource_url, srv_type);

  bool error = true;
  uint8_t buffer[50];

  // must be one of w/r/a
  if (strcmp(srv_type, "w") == 0 || strcmp(srv_type, "r") == 0 || strcmp(srv_type, "a") == 0)
  {
    error = false;
  }

  if (error)
  {
    OC_ERR("oc_do_s_mode_with_scope_internal : service type value incorrect %s", srv_type);
    return;
  }

  if (resource_url == NULL)
  {
    OC_ERR("oc_do_s_mode_with_scope_internal: resource url is NULL");
    return;
  }

  const oc_device_info_t* device = oc_core_get_device_info(0);
  if (device == NULL)
  {
    PRINT("oc_do_s_mode_with_scope_internal : device is NULL");
    return;
  }

  if (!oc_is_device_in_runtime(0))
  {
    PRINT("oc_do_s_mode_with_scope_internal : device '0' is not running, load state is: %d", device->lsm_s);
    return;
  }

  const oc_resource_t* my_resource = oc_ri_get_app_resource_by_uri(resource_url, strlen(resource_url), 0);
  if (my_resource == NULL)
  {
    PRINT("oc_do_s_mode_with_scope_internal : error no URL found %s", resource_url);
    return;
  }

  oc_notify_observers(my_resource);

  // value size 
  const int value_size = oc_s_mode_get_resource_value(resource_url, buffer, sizeof(buffer));

  // get the sender ia + iid
  uint16_t sia_value = device->ia;
  uint64_t iid = device->iid;

  int index = oc_core_find_group_object_table_url(resource_url);
  if (index == -1)
  {
    PRINT("oc_do_s_mode_with_scope_internal : no table entry found for %s", resource_url);
    return;
  }

  // loop over all group addresses and issue the s-mode command
  while (index != -1)
  {
    const int ga_len = oc_core_find_group_object_table_number_group_entries(index);
    oc_cflag_mask_t cflags = oc_core_group_object_table_cflag_entries(index);

    PRINT("index %d service type = %s cflags %d with flags=", index, srv_type, cflags);
    oc_print_cflags(cflags);

    // send always on bool parameter is false, otherwise 't' flag must be set
    const bool do_send = consider_transmission_flag == false ? true : cflags & OC_CFLAG_TRANSMISSION;

    if (do_send)
    {
      PRINT("index %d rp = %s cflags %d flags=", index, srv_type, cflags);
      oc_print_cflags(cflags);

      // with a read command to a GO, the device send this GO's value
      PRINT("handling: index %d", index);

      for (int j = 0; j < ga_len; j++)
      {
        uint32_t group_address = oc_core_find_group_object_table_group_entry(index, j);
        PRINT("ga : %u ", group_address);

        if (strcmp(srv_type, "a") == 0)
        {
          // Check if any other GOT entries have the same GA with "w" flag
          PRINT("Checking & updating internal group objects");

          int other_index = oc_core_find_first_group_object_table_index(group_address);

          while (other_index != -1)
          {
            if (other_index != index)
            {
              oc_cflag_mask_t other_cflags = oc_core_group_object_table_cflag_entries(other_index);
              oc_string_t other_url = oc_core_get_href_from_group_object_table_index(other_index);
              const char* other_url_char = oc_string(other_url);
              const oc_resource_t* other_resource = oc_ri_get_app_resource_by_uri(other_url_char, strlen(other_url_char), 0);
              if (other_resource == NULL)
              {
                other_index = oc_core_find_next_group_object_table_index(group_address, other_index);
                continue;
              }
              if ((other_cflags & OC_CFLAG_WRITE) && other_resource->put_handler.cb)
              {
                // update the resource internally
                oc_request_t new_request = { 0 };

                oc_rep_t* rep;
                struct oc_memb rep_objects = { sizeof(oc_rep_t), 0, 0, 0, 0 };
                oc_rep_set_pool(&rep_objects);
                oc_parse_rep(buffer, value_size, &rep);

                new_request.request_payload = rep;
                new_request.uri_path = other_url_char;
                new_request.uri_path_len = strlen(other_url_char);

                other_resource->put_handler.cb(&new_request, OC_IF_NONE, other_resource->put_handler.user_data);
              }
            }

            other_index = oc_core_find_next_group_object_table_index(group_address, other_index);
          }
        }
        if (j == 0)
        {
          // issue the s-mode command, but only for the first ga entry
          uint32_t grpid = oc_find_grpid_in_recipient_table(group_address);
          if (grpid > 0)
          {
            oc_issue_s_mode(scope, sia_value, grpid, group_address, iid, srv_type, buffer, value_size);
          }
          else
          {
            // send to group address in multicast address
            oc_issue_s_mode(scope, sia_value, group_address, group_address, iid, srv_type, buffer, value_size);
          }
        }

        // - the recipient table contains the list of destinations that will receive data
        // - loop over the full recipient table and send a message if the group is there
        for (int jr = 0; jr < GRT_MAX_ENTRIES; jr++)
        {
          bool found = oc_core_check_recipient_index_on_group_address(jr, group_address);
          if (found)
          {
            char* url = oc_core_get_recipient_index_url(jr);
            if (url)
            {
              PRINT("broker send to url: %s", url);
              const uint16_t ia = oc_core_get_recipient_ia(jr);
              oc_knx_client_do_broker_request(resource_url, iid, ia, url, srv_type);
            }
          }
        }
      }
    }
    else
    {
      PRINT("not send due to flags");
    }

    index = oc_core_find_next_group_object_table_url(resource_url, index);
  }
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