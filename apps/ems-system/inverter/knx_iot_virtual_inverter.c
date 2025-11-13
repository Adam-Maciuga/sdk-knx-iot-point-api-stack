/*
-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
 Copyright (c) 2022-2023 Cascoda Ltd
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

/**
 * @file
 *
 * KNX virtual sensor
 *
 * ## Application Design
 *
 * - app_init, initializes the stack values.
 *
 * - register_resources, function that registers all endpoints, e.g. sets the GET/.../DELETE
 *   handlers for each end point
 *
 * - main, starts the stack, with the registered resources, can be compiled out with NO_MAIN
 *
 * - callback handlers for the implemented methods, see callback handler 'Callback Notes'
 *   
 * ## stack specific defines
 * - __linux__, build for Linux
 * - WIN32,  build for Windows
 * - OC_OSCORE, oscore is enabled as compile flag
 *
 * ## File specific defines
 * - NO_MAIN
 *   compile out the function main()
 * - KNX_GUI
 *   build the GUI with console option, so that all
 *   logging can be seen in the command window
 */
#include "oc_api.h"
#include <stdio.h> // defines FILENAME_MAX
#include "apps/ems/knx_iot_virtual_ems.h"
#include "oc_core_res.h"
#include "oc_helpers.h"
#include "api/oc_knx_fp.h"
#include "oc_knx_client.h"

const char application_name[] = "Inverter";
const char sn_lower_case[] = "00fa10020b00";  // deliberated incorrect serial numbers
const char hostname[] = "knx-00fa10020b00";   // default host name (reset uses this default)
const char hw_type[] = "000102030405";        // 12 string chars, MSB = 00
const char dev_model[] = "6800";              // reuse mask version from iot device
const uint32_t mid = 0x00fa;                  // first 4 digits of sn_lower_case

float_functional_block_t inverter = {
  425, 1, 1,
  {
    0, /* IEEE 754 single float, KNX DPT: 14.056 */
    "/p/inverter",
    "urn:knx:dpa.425.60",
    ":dpt.value_power",
    "Inverter Output"}
};

void register_resources(void)
{   
  oc_resource_t* power_dc_resource_inverter_out = oc_new_resource(inverter.point.resource_path, 1);
  
  oc_resource_bind_resource_type(power_dc_resource_inverter_out, inverter.point.dpa);
  oc_resource_bind_dpt(power_dc_resource_inverter_out, inverter.point.dpt);
  oc_resource_bind_content_type(power_dc_resource_inverter_out, APPLICATION_CBOR, CONTENT_NONE);

  oc_resource_set_functional_block_data(power_dc_resource_inverter_out, inverter.fb_number, inverter.fb_instance, inverter.fb_number_of_datapoints);

  oc_resource_set_properties(power_dc_resource_inverter_out, OC_OBSERVABLE + OC_DISCOVERABLE);

  oc_resource_set_request_handler(power_dc_resource_inverter_out, OC_GET, get_inverter, NULL, OC_ACL_I | OC_ACL_D, OC_IF_I);
  
  oc_add_resource(power_dc_resource_inverter_out);  
}

// inverter local functions
void set_inverter_value(float value) { inverter.point.value = value; }

char* app_retrieve_href_from_inverter(void) { return inverter.point.resource_path; }

// inverter has GET (has only output)
void get_inverter(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
  (void)interfaces;
  bool error_state = true;

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

  const oc_device_info_t* const device = oc_core_get_device_info();

  // open CBOR
  oc_rep_begin_root_object();

  if (oc_query_value_exists(request, "m") != -1)
  {
    // ... query parameter 'm' is present, check the various values
    char* m_value;
    char* m_key;
    size_t m_value_len = oc_get_query_value(request, "m", &m_value);
    size_t m_key_len;

    const bool wildcard = strncmp(m_value, "*", m_value_len) == 0;

    PRINT("Query Parameter: %.*s", (int)m_value_len, m_value);

    oc_init_query_iterator();

    // check (0..n) query parameter
    while (oc_iterate_query(request, &m_key, &m_key_len, &m_value, &m_value_len) != -1)
    {
      // id
      if (strncmp(m_value, "id", m_value_len) == 0 || wildcard)
      {
        // knx://sn: + max len SN + uri path + \0 = ~ 65
        char serial_number[65];

        (void)snprintf(serial_number, 65, "knx://sn:%s%s", oc_string(device->serialnumber),
                       oc_string(request->resource->uri));

        oc_rep_i_set_text_string(root, 0, serial_number);

        error_state = false;
      }
      // value
      if (strncmp(m_value, "value", m_value_len) == 0 || wildcard)
      {
        // see 'Callback Notes'
        oc_rep_i_set_float(root, 1, inverter.point.value);
        error_state = false;
      }
      // rt
      if (strncmp(m_value, "rt", m_value_len) == 0 || wildcard)
      {
        // use the first type, skip urn:knx (=7), if more types are used
        // - add a next in the data structure
        // - add an extra line
        const char* first_type = oc_string_array_get_item(request->resource->types, 0);

        oc_rep_text_set_text_string(root, rt, first_type + 7);
        error_state = false;
      }
      // if (array of text strings)
      if (strncmp(m_value, "if", m_value_len) == 0 || wildcard)
      {
        add_all_interface_short_urns_for_a_resource(request->resource);
        error_state = false;
      }
      // dpt
      if (strncmp(m_value, "dpt", m_value_len) == 0 || wildcard)
      {
        oc_rep_text_set_text_string(root, dpt, oc_string(request->resource->dpt));
        error_state = false;
      }
      // ga
      if (strncmp(m_value, "ga", m_value_len) == 0 || wildcard)
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
      // href (MAY omit in the response, here not for a '*')
      if (strncmp(m_value, "href", m_value_len) == 0 || wildcard)
      {
        oc_rep_text_set_text_string(root, href, oc_string(request->resource->uri));

        error_state = false;
      }
    }
  }
  else
  { // ... no query parameter 'm' present at all, set value for the GET

    // see 'Callback Notes'
    oc_rep_i_set_float(root, 1, inverter.point.value);
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
