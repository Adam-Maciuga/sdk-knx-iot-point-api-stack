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

// about this app and its datapoints
/**
 * CEM stands for Central Energy Manager.
 *
 * The current implementation contains two datapoints and foresees two operation modes.
 *
 * The first datapoint serves the role of capturing the present DC power from the medium, which is typically send out to the
 * medium by Invertor PV control devices.
 *
 * The two operation modes are:
 *  - sun mode: the principle is to only charge the electric car at 4 kW if at least 4kW DC power is procuded by sun light
 * (through PV panels)
 *  - mix mode: charge the electric car at 4 kW regardless of the present produced DC power by sun light
 * The operation mode is represented by a dedicated button, wich allows the user to toggle its value, the current value is
 * indicated inside the button, either 'sun' or 'mix'.
 *
 * The second datapoint sends, depending on the operation mode out to the medium the requested (calculated) charge rate,
 * eihter at 0 kW or at 4 kW
 *
 * References
 * - PV: description of the functional block(s): 7/8/1 Photovoltaics
 * - Charger: description of the functional block(s): Application_EVSE
 * - Datapoints: DPT 14.056: 3/7/2 Datapoint Types
 */
// implemented demo configuration concept
/**
 * The following concept has been implemented:
 * - The CEM is considered being the central 'unit'
 * - The configuration for all three devices is derived from the serial number of the CEM device
 * - The configuration algorithm can be found in the source code of all three devices, it sets the:
 *   - IA: individual address
 *   - IID: installation identifier
 *   - Group Object Table
 *   - Publisher Table
 *   - Recipient Table
 *   - Authentication Table
 * - The PV and the Charger device come with an extra configuration datapoint (not standardized)
 *   - PV: its auth/at table comes with a specific pre-configured entry based on the serial number of the PV device
 *   - Charger: its auth/at table comes with a specific pre-configured entry based on the serial number of the Charger device
 * - The CEM device
 *   - comes with an extra link datapoint (not standardized)
 *   - this link object allows the CEM to make a connection to any target device, based on the serial number of the device to
 * be connected
 * - In practise:
 *   - click either the 'settings' or the 'usage' icon of the target device (either the PV or the Charger device)
 *   - click the 'copy serial number' button
 *   - click either the 'settings' or the 'usage' icon of the CEM device
 *   - paste the previously copied (target) serial number into the input field of the CEM device
 *   - then click in the CEM device the 'Link' button, this will:
 *     - update the auth/at table of the CEM device so that data between the link object of the CEM device and the
 * configuration object of the target device can be exchanged
 *     - transmit the serial number of the CEM device to the target device (in this case the PV device)
 *     - the target device uses this transmitted serial number to set up its data object(s) according the above mentioned
 * algorithm
 *   - the 'UnLink' button clears the auth/at entry in the CEM device
 */
// implemented demo configuration algorithm
/**
 * this eihter based on the own serial number: device->serialnumber
 * -> in this case sn_link is set to "000000000000"
 *
 * or both the own serial number and serial number the to be linked target device
 * -> in this case sn_link is NOT set to "000000000000"
 *
 * Details:
 *
 * IA (and serial number)
 *
 * The IA of the three devices are set as follows, ETS notation:
 * -  PV: 15.15.15 (serial number = 00fa:1002:0b00)
 * -  CEM: 15.15.15 (serial number = 00fa:1002:0c00)
 * -  Charger: 15.15.15 (serial number = 00fa:1002:0d00)
 *
 * IID
 *
 * The IID of the three devices is set to: 00fa:0000
 *
 * Group Object Table
 *
 * The Group Object Tables of the three devices are set as follows:
 *
 * PV:
 * -  url: '/p/pv'      cflags : '64' ...t.  ga : [ 0/0/1 ]
 * -  url: '/p/CEM'     cflags : '16' .w...  ga : [ 0/0/3 ]
 *
 * CEM:
 * -  url: '/p/pv'      cflags : '16' .w...  ga : [ 0/0/1 ]
 * -  url: '/p/charger' cflags : '64' ...t.  ga : [ 0/0/2 ]
 * -  url: '/p/link'    cflags : '64' ...t.  ga : [ 0/0/3 ]
 *
 * Charger:
 * -  url: '/p/charger' cflags : '16' .w...  ga : [ 0/0/2 ]
 * -  url: '/p/CEM'     cflags : '16' .w...  ga : [ 0/0/3 ]
 *
 * Publisher and Recipient Table
 *
 * Both the Publisher and Recipeint Tables of the three devices are set as follows:
 *
 * PV:
 * -  grpid:  00fa:0000  ga : [ 0/0/1 0/0/3]
 *
 * CEM:
 * -  grpid:  00fa:0000  ga : [ 0/0/1 0/0/2 0/0/3]
 *
 * Charger:
 * -  grpid:  00fa:0000  ga : [ 0/0/2 0/0/3]
 *
 * Authentication Table
 *
 * The Authentication Tables of the three devices are set as follows:
 *
 * PV:
 * -  ga 0/0/1 osc_id [2]: 0001  osc_ms [16]: 00000000000000000000000000000000  osc_contextid (o)[6]: 000000000000
 * -  ga 0/0/3 osc_id [2]: 0003  osc_ms [16]: 00fa10020b00060708090a0b0c0d0e0f  osc_contextid (o)[6]: 00fa00000003
 *
 * CEM:
 * -  ga 0/0/1 osc_id [2]: 0001  osc_ms [16]: 000102030405060708090a0b0c0d0e0f  osc_contextid (o)[6]: 10020c000001
 * -  ga 0/0/2 osc_id [2]: 0002  osc_ms [16]: 000102030405060708090a0b0c0d0e0f  osc_contextid (o)[6]: 10020c000002
 * -  ga 0/0/3 osc_id [2]: 0003  osc_ms [16]: 00000000000000000000000000000000  osc_contextid (o)[6]: 000000000000
 *
 * Charger:
 * -  ga 0/0/2 osc_id [2]: 0002  osc_ms [16]: 00000000000000000000000000000000  osc_contextid (o)[6]: 000000000000
 * -  ga 0/0/3 osc_id [2]: 0003  osc_ms [16]: 00fa10020d00060708090a0b0c0d0e0f  osc_contextid (o)[6]: 00fa00000003
 *
 */

#include "oc_api.h"
#include "apps/hems/knx_iot_virtual_ems.h"
#include "oc_core_res.h"
#include "oc_helpers.h"
#include "api/oc_knx_fp.h"
#include "oc_knx_client.h"

const char application_name[] = "Central Energy Manager";
const char sn_lower_case[] = "00fa10020c00";  // deliberated incorrect serial numbers
const char hostname[] = "knx-00fa10020c00";   // default host name (reset uses this default)
const char hw_type[] = "000102030405";        // 12 string chars, MSB = 00
const char dev_model[] = "6800";              // reuse mask version from iot device
const uint32_t mid = 0x00fa;                  // first 4 digits of sn_lower_case

cem_mode_t cem_mode = sun_mode;

int_array_functional_block_t cem = {
  427,
  1,
  1,
  {
    {0, /* IEEE 754 single float, KNX DPT: 14.056 */
      "/p/inverter",
      "urn:knx:dpa.427.60",
      ":dpt.value_power",
    "CEM Input from Inverter"},
    {0, /* IEEE 754 single float, KNX DPT: 14.056 */
     "/p/charger",
     "urn:knx:dpa.427.52",
     ":dpt.value_power",
     "CEM Output to Charger"}
  }
};

void register_resources(void)
{
  oc_resource_t* power_dc_resource_inverter_in = oc_new_resource(cem.point[CEM_INVERTER].resource_path, 1);
  oc_resource_t* power_dc_resource_charger_out = oc_new_resource(cem.point[CEM_CHARGER].resource_path, 1);
  

  oc_resource_bind_resource_type(power_dc_resource_inverter_in, cem.point[CEM_INVERTER].dpa);
  oc_resource_bind_resource_type(power_dc_resource_charger_out, cem.point[CEM_CHARGER].dpa);

  oc_resource_bind_dpt(power_dc_resource_inverter_in, cem.point[CEM_INVERTER].dpt);
  oc_resource_bind_dpt(power_dc_resource_charger_out, cem.point[CEM_CHARGER].dpt);

  oc_resource_bind_content_type(power_dc_resource_inverter_in, APPLICATION_CBOR, CONTENT_NONE);
  oc_resource_bind_content_type(power_dc_resource_charger_out, APPLICATION_CBOR, CONTENT_NONE);

  oc_resource_set_functional_block_data(power_dc_resource_inverter_in, cem.fb_number, cem.fb_instance, cem.fb_number_of_datapoints);
  oc_resource_set_functional_block_data(power_dc_resource_charger_out, cem.fb_number, cem.fb_instance, cem.fb_number_of_datapoints);

  oc_resource_set_properties(power_dc_resource_inverter_in, OC_OBSERVABLE + OC_DISCOVERABLE);
  oc_resource_set_properties(power_dc_resource_charger_out, OC_OBSERVABLE + OC_DISCOVERABLE);

  oc_resource_set_request_handler(power_dc_resource_inverter_in, OC_GET, get_cem_inverter, NULL, OC_ACL_I | OC_ACL_D, OC_IF_I | OC_IF_D);
  oc_resource_set_request_handler(power_dc_resource_inverter_in, OC_PUT, put_cem_inverter, NULL, OC_ACL_I | OC_ACL_P, OC_IF_I | OC_IF_P);

  oc_resource_set_request_handler(power_dc_resource_charger_out, OC_GET, get_cem_charger, NULL, OC_ACL_I | OC_ACL_D, OC_IF_I | OC_IF_D);

  oc_add_resource(power_dc_resource_inverter_in);
  oc_add_resource(power_dc_resource_charger_out);
}

// cem local functions 

void cem_process_inverter_input(void)
{
  const int mode = retrieve_cem_mode();
  const int current_inverter_power = get_cem_inverter_value() / 1000; // kW
  int current_charger_power = 4; // default 4 kW

  if (mode == sun_mode)
  {
    if (current_inverter_power < 4)
    {
      // SUN mode, pause charging
      current_charger_power = 0;
    }
  }
  if (mode == mix_mode)
  {
    // MIX mode, continue charging
  }

  // set charger output value
  const char* url = app_retrieve_href_from_cem_charger();
  cem.point[CEM_CHARGER].value = current_charger_power;

  // send message
  oc_send_s_mode_mc_or_uc_message(SENDER_SCOPE, url, "w");
}

cem_mode_t retrieve_cem_mode(void) { return cem_mode; }
void set_cem_mode(cem_mode_t mode) { cem_mode = mode; }

int get_cem_inverter_value(void) { return cem.point[CEM_INVERTER].value; }
int get_cem_charger_value(void) { return cem.point[CEM_CHARGER].value; }

char* app_retrieve_href_from_cem_inverter(void) { return cem.point[CEM_INVERTER].resource_path; }
char* app_retrieve_href_from_cem_charger(void) { return cem.point[CEM_CHARGER].resource_path; }

// cem inverter has GET + PUT (has input)
void put_cem_inverter(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
  bool error_state = true;

  // get interfaces for the resource PUT method ...
  bool is_input_datapoint = interfaces & OC_IF_I;

  // sets the pointer to the (/k or /p) handed over object
  const oc_rep_t* rep = request->request_payload;

  PRINT("-- Begin PUT at %s ", oc_string(request->resource->uri));

  // handle the different request sources, here included as an example to distinguish
  // the caller source (e.g.; called by /p or /k s-mode message EP)
  if (oc_is_redirected_request_from(request) == 1)
  {
    // caller /p --> always allow to write (see 'Callback Notes' above)
    is_input_datapoint = true;
    PRINT("redirected_request %.*s", (int)request->uri_path_len, request->uri_path);
  }

  // loop over object
  while (rep)
  {
    if (rep->iname == 1 && rep->type == OC_REP_INT)
    {
      if (!is_input_datapoint)
      {
        // see 'Callback Notes'
        oc_prepare_no_format_response_no_payload(request, OC_STATUS_METHOD_NOT_ALLOWED);
        return;
      }

      // see 'Callback Notes'
      cem.point[CEM_INVERTER].value = (int)rep->value.integer;
      error_state = false;

      PRINT("set inverter input to %lld", rep->value.integer);
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

    // process input, may change the charger output
    cem_process_inverter_input();

    return;
  }

  // bad request status
  oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);

  PRINT("-- End PUT at %s ", oc_string(request->resource->uri));
}
void get_cem_inverter(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
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
        oc_rep_i_set_int(root, 1, cem.point[CEM_INVERTER].value);
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
    oc_rep_i_set_int(root, 1, cem.point[CEM_INVERTER].value);
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

// cem charger has GET (has only output)
void get_cem_charger(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
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
        oc_rep_i_set_int(root, 1, cem.point[CEM_CHARGER].value);
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
    oc_rep_i_set_int(root, 1, cem.point[CEM_CHARGER].value);
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
