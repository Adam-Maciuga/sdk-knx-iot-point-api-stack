/*
-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
 Copyright (c) 2024-2025 KNX Association
-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
*/

#include "ctype.h"
#include "oc_api.h"
#include "knx_iot_virtual.h"
#include "oc_core_res.h"
#include "oc_helpers.h"
#include "api/oc_knx_fp.h"
#include "oc_knx_client.h"

bool app_is_secure(void)
{
  // may produce a warning if OC_OSCORE is not specified ...
  // but here it is integral part of CMake
  return OC_OSCORE ? true : false;
}

void app_str_to_upper(char* str)
{
  while (*str != '\0')
  {
    *str = (char)toupper(*str);
    str++;
  }
}



static oc_event_callback_retval_t send_delayed_response(void* context)
{
  oc_separate_response_t* response = context;

  if (response->active)
  {
    oc_set_separate_response_buffer(response);
    oc_send_separate_response(response, OC_STATUS_CHANGED);
    OC_DBG("Delayed response sent");
  }
  else
  {
    OC_DBG("Delayed response NOT active");
  }

  return OC_EVENT_DONE;
}

void swu_cb(oc_separate_response_t* response, size_t binary_size, size_t offset, uint8_t* payload, size_t len, void* data)
{
  (void)binary_size;
  (void)data;

  char filename[] = "./downloaded.bin";
  OC_DBG("swu_cb %s block=%d size=%d ", filename, (int)offset, (int)len);

  FILE* write_ptr = fopen("downloaded_bin", "ab");
  const size_t n = fwrite(payload, sizeof(*payload), len, write_ptr);
  const size_t r = fclose(write_ptr);
  OC_DBG("written data: %llu, operation ok (=0): %llu", n, r);

  oc_set_delayed_callback(response, &send_delayed_response, 0);
}

void add_all_interface_short_urns_for_a_resource(const oc_resource_t* resource)
{
  // get all if's
  oc_interface_mask_t res_interfaces = OC_IF_NONE;
  oc_resource_get_all_interfaces_for_a_resource(resource, &res_interfaces);

  // create interface list, n elements
  const unsigned int nr_entries = oc_count_total_interfaces_in_mask(res_interfaces);
  oc_string_array_t interface_list;
  oc_new_string_array(&interface_list, nr_entries);

  // put all if's to string array
  oc_put_all_interface_short_urns_from_a_mask_in_string_array(res_interfaces, interface_list);

  // add strings by key 'if'
  oc_rep_set_string_array(root, if, interface_list);

  // release interface list
  oc_free_string_array(&interface_list);
}

// defied individually in specific LSAB/LSSB/EITT application code
extern lsxb_channel_t lsxb[];
extern int_datapoint_t test_parameter;

/*

 n x CoAP GET/PUT methods for data point resource.
 Setup defines urls, resource types and other (see register
 resources). Initialization of the returned values are done from the global
 property values.

 @param request the request representation.
 @param interfaces the interface used for this call
 @param user_data the user data.

 @note The (generic) callback 'call' handler uses always above defined 3
 parameters, provide them even if not used

*/

// generic GET for LSSB/LSAB/EITT applications 
void get_lsxb(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
  (void)interfaces;

  bool error_state = true;

  // for the GET ...
  const bool is_output_datapoint = request->resource->get_handler.interface_mask & OC_IF_O;

  // user data host the HEX encoded channel/datapoint 
  const int32_t channel_and_datapoint = strtol(user_data,NULL,16);
  const uint16_t c = channel_and_datapoint >> 16;
  const uint16_t p = channel_and_datapoint & 0x0000FFFF;

  PRINT("-- Begin GET %s at %s ", oc_string(request->resource->name), oc_string(request->resource->uri));

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  oc_device_info_t* device = oc_core_get_device_info(0);

  // open CBOR
  oc_rep_begin_root_object();

  if (device != NULL)
  {
    if (oc_query_value_exists(request, "m") != -1)
    {
      // ... query parameter 'm' is present, check the various values
      char* m;
      char* m_key;
      size_t m_key_len;
      size_t m_len = oc_get_query_value(request, "m", &m);

      PRINT("Query Parameter: %.*s", (int)m_len, m);

      oc_init_query_iterator();

      // check query parameter
      while (oc_iterate_query(request, &m_key, &m_key_len, &m, &m_len) != -1)
      {
        // unique identifier
        if (strncmp(m, "id", m_len) == 0 || strncmp(m, "*", m_len) == 0)
        {
          // knx://sn: + max len SN + uri path + \0 = ~ 65
          char serial_number[65];

          (void)snprintf(serial_number, 65, "knx://sn:%s%s", 
                         oc_string(device->serialnumber),
                         oc_string(request->resource->uri));

          oc_rep_i_set_text_string(root, 0, serial_number);

          error_state = false;
        }
        // value
        if (strncmp(m, "value", m_len) == 0 || strncmp(m, "*", m_len) == 0)
        {
          if (!is_output_datapoint)
          {
            // don't allow to read a no 'output'
            oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
            return;
          }

          oc_rep_text_set_boolean(root, value, lsxb[c].point[p].value);
          error_state = false;
        }
        // resource types
        if (strncmp(m, "rt", m_len) == 0 || strncmp(m, "*", m_len) == 0)
        {
          // use the first type, skip urn:knx (=7), if more types are used
          // - add a next in the data structure
          // - add an extra line
          const char* first_type = oc_string_array_get_item(request->resource->types, 0);

          oc_rep_text_set_text_string(root, rt, first_type + 7);
          error_state = false;
        }
        // interfaces (array of text strings)
        if (strncmp(m, "if", m_len) == 0 || strncmp(m, "*", m_len) == 0)
        {
          add_all_interface_short_urns_for_a_resource(request->resource);
          error_state = false;
        }
        // dpt
        if (strncmp(m, "dpt", m_len) == 0 || strncmp(m, "*", m_len) == 0)
        {
          oc_rep_text_set_text_string(root, dpt, oc_string(request->resource->dpt));
          error_state = false;
        }
        // ga
        if (strncmp(m, "ga", m_len) == 0 || strncmp(m, "*", m_len) == 0)
        {
          const int index = oc_core_find_group_object_table_url(oc_string(request->resource->uri));
          if (index > -1)
          {
            oc_group_object_table_t* got_table_entry = oc_core_get_group_object_table_entry(index);
            if (got_table_entry)
            {
              oc_rep_set_int_array(root, ga, got_table_entry->ga, got_table_entry->ga_len);
            }
          }
          error_state = false;
        }
        // description
        if (strncmp(m, "desc", m_len) == 0 || strncmp(m, "*", m_len) == 0)
        {
          oc_rep_text_set_text_string(root, desc, oc_string(request->resource->name));

          error_state = false;
        }
      }
    }
    else
    { // ... no query parameter 'm' present at all, set value for the GET

      if (!is_output_datapoint)
      {
        // don't allow to read a no 'output'
        oc_prepare_no_format_response_no_payload(request, OC_STATUS_METHOD_NOT_ALLOWED);
        return;
      }

      oc_rep_i_set_boolean(root, 1, lsxb[c].point[p].value);
      error_state = false;
    }
  }

  // close CBOR
  oc_rep_end_root_object();

  // check CBOR encoding errors
  if (g_err != CborNoError)
  {
    error_state = true;
  }

  PRINT("CBOR encoder size %d", oc_rep_get_encoded_payload_size());

  // wrong device, cbor error or unknown 'm' query parameter value(s)
  if (error_state)
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_NOT_FOUND);
  else
    oc_prepare_cbor_response(request, OC_STATUS_OK);

  PRINT("-- End GET %s at %s ", oc_string(request->resource->name), oc_string(request->resource->uri));
}

// specific PUT for LSAB/EITT applications (SOO write - IOO will be updated ... )
void put_lsab(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
  (void)interfaces;

  bool error_state = true;

  // for the PUT ...
  const bool is_input_datapoint = request->resource->put_handler.interface_mask & OC_IF_I;

  // sets the pointer to the (/k or /p) handed over object
  const oc_rep_t* rep = request->request_payload;

  // user data host the HEX encoded channel/datapoint 
  const int32_t channel_and_datapoint = strtol(user_data, NULL, 16);
  const uint16_t c = channel_and_datapoint >> 16;
  const uint16_t p = channel_and_datapoint & 0x0000FFFF;

  PRINT("-- Begin PUT %s at %s ", oc_string(request->resource->name), oc_string(request->resource->uri));

  // handle the different requests, here only included as example to
  // identify if extra data needs to be processed in the endpoint
  if (oc_is_redirected_request_from(request) > -1)
  {
    PRINT("redirected_request %.*s", (int)request->uri_path_len, request->uri_path);
  }

  // loop over object
  while (rep)
  {
    // this EP accepts only a bool, a faulty construct such as {1: 2, 1: true}
    // may need to be skipped
    if (rep->iname == 1 && rep->type == OC_REP_BOOL)
    {
      if (!is_input_datapoint)
      {
        // don't allow to write a no 'input'
        oc_prepare_no_format_response_no_payload(request, OC_STATUS_METHOD_NOT_ALLOWED);
        return;
      }

      PRINT("set LSAB soo to %d", rep->value.boolean);
      lsxb[c].point[p].value = rep->value.boolean;
      error_state = false;
      break;
    }
    rep = rep->next;
  }

  // correct data retrieved
  if (!error_state)
  {
    // inform the stack on status
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_CHANGED);

    // set LSAB status
    PRINT("received no error, update LSAB status to %d", lsxb[c].point[SOO].value);
    lsxb[c].point[IOO].value = lsxb[c].point[SOO].value;

    // this is the 'simple' option to trigger a status on a specific EP
    PRINT("send status to %s with flag: 'w'", lsxb[c].point[IOO].href);
    oc_do_s_mode_with_scope_and_check(SENDER_SCOPE, lsxb[c].point[IOO].href, "w", true);

    PRINT("-- End PUT %s at %s ", oc_string(request->resource->name), oc_string(request->resource->uri));
    return;
  }

  // bad request status
  oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
  PRINT("-- End PUT %s at %s ", oc_string(request->resource->name), oc_string(request->resource->uri));
}

// specific PUT for LSSB/EITT applications (IOO write - nothing will be updated ... )
void put_lssb(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
  (void)interfaces;

  bool error_state = true;

  // for the PUT ...
  const bool is_input_datapoint = request->resource->put_handler.interface_mask & OC_IF_I;

  // sets the pointer to the (/k or /p) handed over object
  const oc_rep_t* rep = request->request_payload;

  // user data host the HEX encoded channel/datapoint
  const int32_t channel_and_datapoint = strtol(user_data, NULL, 16);
  const uint16_t c = channel_and_datapoint >> 16;
  const uint16_t p = channel_and_datapoint & 0x0000FFFF;

  PRINT("-- Begin PUT %s Control at %s ", oc_string(request->resource->name), oc_string(request->resource->uri));

  // handle the different requests, here only included as example to
  // identify if extra data needs to be processed in the endpoint
  if (oc_is_redirected_request_from(request) > -1)
  {
    PRINT("redirected_request %.*s", (int)request->uri_path_len, request->uri_path);
  }

  // loop over object
  while (rep)
  {
    // this EP accepts only a bool, a faulty construct such as {1: 2, 1: true}
    // may need to be skipped
    if (rep->iname == 1 && rep->type == OC_REP_BOOL)
    {
      if (!is_input_datapoint)
      {
        // don't allow to write a no 'input'
        oc_prepare_no_format_response_no_payload(request, OC_STATUS_METHOD_NOT_ALLOWED);
        return;
      }

      PRINT("set LSSB ioo to %d", rep->value.boolean);
      lsxb[c].point[p].value = rep->value.boolean;
      error_state = false;
      break;
    }
    rep = rep->next;
  }

  // correct data retrieved
  if (!error_state)
  {
    // inform the stack on status
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_CHANGED);

    PRINT("-- End PUT %s at %s ", oc_string(request->resource->name), oc_string(request->resource->uri));
    return;
  }

  // bad request status
  oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
  PRINT("-- End PUT %s at %s ", oc_string(request->resource->name), oc_string(request->resource->uri));
}

// generic GET for LSSB/LSAB/EITT applications
void get_test_parameter(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
  (void)user_data;
  (void)interfaces;

  bool error_state = true;

  // for the GET on a parameter is it not tested
  // - if this is an input or output
  // - usually a parameter has interface type if.p and if.g

  PRINT("-- Begin GET %s at %s ", oc_string(request->resource->name), oc_string(request->resource->uri));

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  oc_device_info_t* device = oc_core_get_device_info(0);

  // open CBOR
  oc_rep_begin_root_object();

  if (device != NULL)
  {
    if (oc_query_value_exists(request, "m") != -1)
    { // ... query parameter 'm' is present, check the various values

      char* m;
      char* m_key;
      size_t m_key_len;
      size_t m_len = oc_get_query_value(request, "m", &m);

      PRINT("Query Parameter: %.*s", (int)m_len, m);

      oc_init_query_iterator();

      // check query parameter
      while (oc_iterate_query(request, &m_key, &m_key_len, &m, &m_len) != -1)
      {
        // unique identifier
        if (strncmp(m, "id", m_len) == 0 || strncmp(m, "*", m_len) == 0)
        {
          // knx://sn: + max len SN + uri path + \0 = ~ 65
          char serial_number[65];

          (void)snprintf(serial_number, 65, "knx://sn:%s%s", 
                         oc_string(device->serialnumber),
                         oc_string(request->resource->uri));

          oc_rep_i_set_text_string(root, 0, serial_number);

          error_state = false;
        }
        // value
        if (strncmp(m, "value", m_len) == 0 || strncmp(m, "*", m_len) == 0)
        {
          oc_rep_text_set_int(root, value, test_parameter.value);
          error_state = false;
        }
        // resource types
        if (strncmp(m, "rt", m_len) == 0 || strncmp(m, "*", m_len) == 0)
        {
          // use the first type, skip urn:knx (=7), if more types are used
          // - add a next in the data structure 
          // - add an extra line
          const char* first_type = oc_string_array_get_item(request->resource->types, 0);

          oc_rep_text_set_text_string(root, rt, first_type + 7);
          error_state = false;
        }
        // interfaces (array of text strings)
        if (strncmp(m, "if", m_len) == 0 || strncmp(m, "*", m_len) == 0)
        {
          add_all_interface_short_urns_for_a_resource(request->resource);
          error_state = false;
        }
        // dpt
        if (strncmp(m, "dpt", m_len) == 0 || strncmp(m, "*", m_len) == 0)
        {
          oc_rep_text_set_text_string(root, dpt, oc_string(request->resource->dpt));
          error_state = false;
        }
        // ga
        if (strncmp(m, "ga", m_len) == 0 || strncmp(m, "*", m_len) == 0)
        {
          int index = oc_core_find_group_object_table_url(oc_string(request->resource->uri));
          if (index > -1)
          {
            oc_group_object_table_t* got_table_entry = oc_core_get_group_object_table_entry(index);
            if (got_table_entry)
            {
              oc_rep_set_int_array(root, ga, got_table_entry->ga, got_table_entry->ga_len);
            }
          }
          error_state = false;
        }
        // description
        if (strncmp(m, "desc", m_len) == 0 || strncmp(m, "*", m_len) == 0)
        {
          oc_rep_text_set_text_string(root, desc, oc_string(request->resource->name));

          error_state = false;
        }
      }
    }
    else
    { // ... no query parameter 'm' present at all, set value

      oc_rep_i_set_int(root, 1, test_parameter.value);
      error_state = false;
    }
  }

  // close CBOR
  oc_rep_end_root_object();

  // check CBOR encoding errors
  if (g_err != CborNoError)
  {
    error_state = true;
  }

  PRINT("CBOR encoder size %d", oc_rep_get_encoded_payload_size());

  // wrong device, cbor error or unknown 'm' query parameter value(s)
  if (error_state)
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_NOT_FOUND);
  else
    oc_prepare_cbor_response(request, OC_STATUS_OK);

  PRINT("-- End GET %s at %s ", oc_string(request->resource->name), oc_string(request->resource->uri));
}

// generic PUT for LSSB/LSAB/EITT applications
void put_test_parameter(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
  (void)interfaces;
  (void)user_data;

  // for the PUT on a parameter is it not tested
  // - if this is an input or output
  // - usually a parameter has interface type if.p and if.g

  PRINT("-- Begin PUT %s at %s ", oc_string(request->resource->name), oc_string(request->resource->uri));

  // handle the different requests, here only included as example to
  // identify if extra data needs to be processed in the endpoint
  if (oc_is_redirected_request_from(request) > -1)
  {
    PRINT("redirected_request %.*s", (int)request->uri_path_len, request->uri_path);
  }

  // sets the pointer to the (/k /p) handed over object
  oc_rep_t* rep = request->request_payload;
  bool error_state = true;

  // loop over object
  while (rep)
  {
    // this EP accepts only a bool, a faulty construct such as {1: 2, 1: true}
    // may need to be skipped
    if (rep->iname == 1 && rep->type == OC_REP_INT)
    {
      PRINT("set test parameter to : %lld", rep->value.integer);
      test_parameter.value = (uint16_t)rep->value.integer;
      error_state = false;
      break;
    }
    rep = rep->next;
  }

  if (!error_state)
  {
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_CHANGED);

    PRINT("-- End PUT %s at %s ", oc_string(request->resource->name), oc_string(request->resource->uri));
    return;
  }

  // bad request status
  oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
  PRINT("-- End PUT %s at %s ", oc_string(request->resource->name), oc_string(request->resource->uri));
}

char* app_retrieve_href_from_channel(uint16_t channel, uint16_t point)
{
  return lsxb[channel].point[point].href;
}

// PARAMETER code - needs to be defined in case of specific parameter handing

char* app_get_parameter_url(int index) { return NULL; }
char* app_get_parameter_name(int index) { return NULL; }

// INT code - needs to be defined in case of such GOs

// BOOLEAN code

void app_set_bool_variable_from_channel(uint16_t channel, uint16_t point, bool value)
{
  lsxb[channel].point[point].value = value;
}

bool app_retrieve_bool_variable_from_channel(uint16_t channel, uint16_t point)
{
  return lsxb[channel].point[point].value;
}
