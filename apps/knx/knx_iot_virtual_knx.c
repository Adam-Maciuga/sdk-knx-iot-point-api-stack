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
#include "knx_iot_virtual_knx.h"
#include "oc_core_res.h"
#include "oc_helpers.h"
#include "api/oc_knx_fp.h"
#include "oc_knx_client.h"

// defied individually in the corresponding LSAB/LSSB/EITT application code
extern lsxb_channel_t lsxb[];
extern int_datapoint_t test_parameter;

/*
 Callback Notes 

 Below the CoAP GET/PUT callback handlers are defined for the data point resources.

 Note that this is a collection of all by the by stack DEMOS used PUT/GET methods.
 For an own development the methods have to be adapted or extended, such as to define get/put
 methods for float / long int ord combined datapoints.

 For the resource path, resource types and other see 'register resources'.

 @param request    the request representation
 @param interfaces the interface mask, as specified for the application resource and method (GET, ...)
 @param user_data  the user data

 A callback 'call' handler demands the above defined 3 parameters when called by the stack,
 provide them even if they are not used.

 Details
 -------

 The callbacks are called from the stack for an 's-mode' call (/k) and for a parameter and diagnostic
 'property' call (/p).

 *Caller*

 For a POST 's-mode' call
 - the group object table configuration flags (cflags) and service type (w/r/a)
   are considered by the stack, but not for the 'property' call.

 - the corresponding (r/w/a) application GET/PUT callback handlers are called
   by the stack. The request payload (on w/a ->PUT) points to the actual value object.  

 For a GET/PUT 'property' call the GET/PUT callback handler are called directly.

 *Resource Path*

 - A KNX related resource path for the 's-mode' and 'property' communication SHALL be defined with
   a leading '/p' (e.g.; '/p/lssb/soo'). The resource path SHALL NOT be empty. Hence, the LSAB/LSSB
   application examples uses the leading '/p' with some application specific extension,
   also the EITT test application requires a leading '/p' for the EITT certification tests.

 - The /p callbacks MUST implement also additional required functionality.

   * Depending on your application and hardware you may not allow to write (PUT) values to an output
     datapoint (GO), this can damage your hardware. Reading an input datapoint is less critical,
     but requires a kind of caching the value. Hence, the specification demands:

     - GET is mandatory for 's-mode' and 'property' communication /w and w/o metadata m= m/o parameters 
       - mandatory parameters (id, value, rt, if, dpt, ga, href)
       - optional parameters (desc, unit, min, max, mrt, cov, hbt, sns)
     - PUT is optional for 's-mode'
     - PUT is mandatory for 'property'

     An EXAMPLE how to handle/distinguish the 's-mode' and 'property' calls and options how to react
     is given below in the callback handler code. Another option to circumvent the problem is to not
     declare the PUT handler for those GOs where a PUT is not possible.

     Note that in the examples a generic (GET) handler is used, to allow a channel based approach with one
     get handler.

 - A NON KNX related resource path can be defined for any vendor specific (configuration) purpose. In this case
   the device configuration is also vendor specific, e.g; by a vendor client. It MAY also be supported in the future by
   a KNX MaC's, such as via an extension of the product SDK. 

*/

// generic GET for LSSB/LSAB/EITT applications for SOO and IOO
void get_lsxb(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
  (void)interfaces;
  bool error_state = true;

  // user data host the pointer to a 16 bit encoded channel/datapoint, skip compiler warning by cast from 64 bit 
  const size_t channel_and_datapoint = (uintptr_t)user_data;
  const uint8_t channel = channel_and_datapoint >> 8 & 0xFF;
  const uint8_t point = channel_and_datapoint & 0xFF;

  PRINT("-- Begin GET at %s ", oc_string(request->resource->uri));

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  // handle different caller sources, here included as an example to distinguish
  // the caller source (e.g.; called by '/p' or '/k')
  if (oc_is_redirected_request_from(request) == 1)
  {
    PRINT("redirected_request %.*s", (int)request->uri_path_len, request->uri_path);
  }

  const oc_device_info_t* const  device = oc_core_get_device_info();

  // open CBOR
  oc_rep_begin_root_object();

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
      // id (mandatory)
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
      // value (mandatory)
      if (strncmp(m, "value", m_len) == 0 || strncmp(m, "*", m_len) == 0)
      {
        // see 'Callback Notes' above
        oc_rep_text_set_boolean(root, value, lsxb[channel].point[point].value);
        error_state = false;
      }
      // resource types (mandatory)
      if (strncmp(m, "rt", m_len) == 0 || strncmp(m, "*", m_len) == 0)
      {
        // use the first type, skip urn:knx (=7), if more types are used
        // - add a next in the data structure
        // - add an extra line
        const char* first_type = oc_string_array_get_item(request->resource->types, 0);

        oc_rep_text_set_text_string(root, rt, first_type + 7);
        error_state = false;
      }
      // interfaces (array of text strings) (mandatory)
      if (strncmp(m, "if", m_len) == 0 || strncmp(m, "*", m_len) == 0)
      {
        add_all_interface_short_urns_for_a_resource(request->resource);
        error_state = false;
      }
      // dpt (mandatory)
      if (strncmp(m, "dpt", m_len) == 0 || strncmp(m, "*", m_len) == 0)
      {
        oc_rep_text_set_text_string(root, dpt, oc_string(request->resource->dpt));
        error_state = false;
      }
      // ga (mandatory)
      if (strncmp(m, "ga", m_len) == 0 || strncmp(m, "*", m_len) == 0)
      {
        const int index = oc_core_find_first_group_object_table_index_from_href(oc_string(request->resource->uri));
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
      // href (mandatory)
      if (strncmp(m, "href", m_len) == 0 || strncmp(m, "*", m_len) == 0)
      {
        oc_rep_text_set_text_string(root, href, oc_string(request->resource->uri));

        error_state = false;
      }
    }
  }
  else
  { // ... no query parameter 'm' present at all, set value for the GET

    // see 'Callback Notes' above
    oc_rep_i_set_boolean(root, 1, lsxb[channel].point[point].value);
    error_state = false;
  }

  // close CBOR
  oc_rep_end_root_object();

  // check CBOR encoding errors
  if (g_err != CborNoError)
  {
    error_state = true;
  }

  PRINT("CBOR encoder size %d", oc_rep_get_encoded_payload_size());

  // wrong device, cbor error or unknown 'm' query parameter key values
  if (error_state)
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_NOT_FOUND);
  else
    oc_prepare_cbor_response(request, OC_STATUS_OK);

  PRINT("-- End GET at %s ", oc_string(request->resource->uri));
}

// specific PUT for LSAB/EITT applications for SOO (SOO write - IOO will be updated ... )
void put_lsab(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
  bool error_state = true;

  // get interfaces for the resource PUT method ...
  bool is_input_datapoint = interfaces & OC_IF_I;

  // sets the pointer to the (/k or /p) handed over 'value' object, note it may be also NULL
  const oc_rep_t* rep = request->request_payload;

  // user data host the pointer to a 16 bit encoded channel/datapoint, skip compiler warning by cast from 64 bit
  const size_t channel_and_datapoint = (uintptr_t)user_data;
  const uint8_t channel = channel_and_datapoint >> 8 & 0xFF;
  const uint8_t point = channel_and_datapoint & 0xFF;

  PRINT("-- Begin PUT at %s ", oc_string(request->resource->uri));

  // handle different caller sources, here included as an example to distinguish
  // the caller source (e.g.; called by '/p' or '/k')
  if (oc_is_redirected_request_from(request) == 1)
  {
    // caller '/p' -> always allow to write (see 'Callback Notes' above)
    is_input_datapoint = true;
    PRINT("redirected_request %.*s", (int)request->uri_path_len, request->uri_path);
  }

  // loop over object
  while (rep)
  {
    // this EP accepts only a bool,
    // a faulty construct such as {..., 1: true, 1: false} is not handled
    if (rep->iname == 1 && rep->type == OC_REP_BOOL)
    {
      if (!is_input_datapoint)
      {
        // see 'Callback Notes' above
        oc_prepare_no_format_response_no_payload(request, OC_STATUS_METHOD_NOT_ALLOWED);
        return;
      }

      // see 'Callback Notes' above
      lsxb[channel].point[point].value = rep->value.boolean;
      error_state = false;

      PRINT("set LSAB to %d", rep->value.boolean);
      break;
    }
    rep = rep->next;
  }

  // correct data retrieved
  if (!error_state)
  {
    // set LSAB status (note, for a real hw device the staus usually needs to be determined from the actual hw relay)
    PRINT("received no error, update LSAB status to %d", lsxb[channel].point[SOO].value);
    lsxb[channel].point[IOO].value = lsxb[channel].point[SOO].value;

    // trigger the LSAB status on a specific resource path (ioo)
    PRINT("send status to %s with flag: 'w'", lsxb[channel].point[IOO].resource_path);
    oc_send_s_mode_mc_or_uc_message(SENDER_SCOPE, lsxb[channel].point[IOO].resource_path, "w");

    // inform the stack on status
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_CHANGED);

    PRINT("-- End PUT at %s ", oc_string(request->resource->uri));
    return;
  }

  // bad request status
  oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
  PRINT("-- End PUT at %s ", oc_string(request->resource->uri));
}

// specific PUT for LSSB/EITT applications for IOO (IOO write - nothing will be updated ... )
void put_lssb(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
  bool error_state = true;

  // get interfaces for the resource PUT method ...
  bool is_input_datapoint = interfaces & OC_IF_I;

  // sets the pointer to the (/k or /p) handed over 'value' object, note it may be also NULL
  const oc_rep_t* rep = request->request_payload;

  // user data host the pointer to a 16 bit encoded channel/datapoint, skip compiler warning by cast from 64 bit
  const size_t channel_and_datapoint = (uintptr_t)user_data;
  const uint8_t channel = channel_and_datapoint >> 8 & 0xFF;
  const uint8_t point = channel_and_datapoint & 0xFF;

  PRINT("-- Begin PUT at %s ", oc_string(request->resource->uri));

  // handle different caller sources, here included as an example to distinguish
  // the caller source (e.g.; called by '/p' or '/k')
  if (oc_is_redirected_request_from(request) == 1)
  {
    // caller '/p' -> always allow to write (see 'Callback Notes' above)
    is_input_datapoint = true;
    PRINT("redirected_request %.*s", (int)request->uri_path_len, request->uri_path);
  }

  // loop over object
  while (rep)
  {
    // this EP accepts only a bool,
    // a faulty construct such as {..., 1: true, 1: false} is not handled
    if (rep->iname == 1 && rep->type == OC_REP_BOOL)
    {
      if (!is_input_datapoint)
      {
        // see 'Callback Notes' above
        oc_prepare_no_format_response_no_payload(request, OC_STATUS_METHOD_NOT_ALLOWED);
        return;
      }

      // see 'Callback Notes' above
      lsxb[channel].point[point].value = rep->value.boolean;
      error_state = false;

      PRINT("set LSSB to %d", rep->value.boolean);
      break;
    }
    rep = rep->next;
  }

  // correct data retrieved
  if (!error_state)
  {
    // inform the stack on status
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_CHANGED);

    PRINT("-- End PUT at %s ", oc_string(request->resource->uri));
    return;
  }

  // bad request status
  oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
  PRINT("-- End PUT at %s ", oc_string(request->resource->uri));
}

// generic GET for LSSB/LSAB/EITT applications 
void get_test_parameter(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
  (void)user_data;
  (void)interfaces;

  bool error_state = true;

  // - input or output, see 'Callback Notes' above
  // - a parameter has interface type GET if.d

  PRINT("-- Begin GET at %s ", oc_string(request->resource->uri));

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  const oc_device_info_t* const  device = oc_core_get_device_info();

  // open CBOR
  oc_rep_begin_root_object();

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
        int index = oc_core_find_first_group_object_table_index_from_href(oc_string(request->resource->uri));
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
    }
  }
  else
  { // ... no query parameter 'm' present at all, set value

    // see 'Callback Notes' above
    oc_rep_i_set_int(root, 1, test_parameter.value);

    PRINT("get test parameter to : %u", test_parameter.value);
    error_state = false;
  }

  // close CBOR
  oc_rep_end_root_object();

  // check CBOR encoding errors
  if (g_err != CborNoError)
  {
    error_state = true;
  }

  PRINT("CBOR encoder size %d", oc_rep_get_encoded_payload_size());

  // wrong device, cbor error or unknown 'm' query parameter key values
  if (error_state)
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_NOT_FOUND);
  else
    oc_prepare_cbor_response(request, OC_STATUS_OK);

  PRINT("-- End GET at %s ", oc_string(request->resource->uri));
}

// generic PUT for LSSB/LSAB/EITT applications
void put_test_parameter(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
  (void)interfaces;
  (void)user_data;

  // - input or output, see 'Callback Notes' above
  // - a parameter has interface type PUT if.p

  PRINT("-- Begin PUT at %s ", oc_string(request->resource->uri));

  // sets the pointer to the (/k /p) handed over object
  oc_rep_t* rep = request->request_payload;
  bool error_state = true;

  // loop over object
  while (rep)
  {
    // this EP accepts only an integer,
    // a faulty construct such as {..., 1: 2, 1: 5} is not handled
    if (rep->iname == 1 && rep->type == OC_REP_INT)
    {
      // see 'Callback Notes' above
      test_parameter.value = (unsigned int)rep->value.integer;
      error_state = false;

      PRINT("set test parameter to : %u", test_parameter.value);
      break;
    }
    rep = rep->next;
  }

  if (!error_state)
  {
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_CHANGED);

    PRINT("-- End PUT at %s ", oc_string(request->resource->uri));
    return;
  }

  // bad request status
  oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
  PRINT("-- End PUT at %s ", oc_string(request->resource->uri));
}

char* app_retrieve_href_from_channel(uint8_t channel, uint8_t point)
{
  return lsxb[channel].point[point].resource_path;
}

// PARAMETER code - needs to be defined in case of specific parameter handling

char* app_get_parameter_url(int index) { return NULL; }
char* app_get_parameter_name(int index) { return NULL; }

// INT code - must be filled if needed

// BOOLEAN code for LSAB/LSSB demos

void app_set_bool_variable_from_channel(uint8_t channel, uint8_t point, bool value)
{
  lsxb[channel].point[point].value = value;
}

bool app_retrieve_bool_variable_from_channel(uint8_t channel, uint8_t point)
{
  return lsxb[channel].point[point].value;
}
