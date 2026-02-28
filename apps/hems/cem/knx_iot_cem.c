/* 
 * Copyright (c) 2022-2023 Cascoda Ltd
 * Copyright (c) 2024-2026 KNX Association
 *            
 * SPDX-License-Identifier: Apache-2.0
 */
 
#include "oc_api.h"
#include "apps/hems/knx_iot_ems.h"
#include "oc_core_res.h"
#include "oc_helpers.h"
#include "api/oc_knx_fp.h"
#include "oc_knx_client.h"

/*
 * CEM definitions
 *
 * Note that all below values are statically defined since they do not change during
 * device lifetime or are predefined in the KNX IoT specification.
 *
 */
const char application_name[] KNX_TOOL_WEAK = "Customer Energy Manager";
const char sn_lower_case[] KNX_TOOL_WEAK = "00fa10020c00";  // deliberated incorrect serial numbers
const char hw_type[] KNX_TOOL_WEAK = "000102030405";        // 12 string chars, MSB = 00
const char dev_model[] KNX_TOOL_WEAK = "6800";              // reuse mask version from iot device
const uint32_t mid KNX_TOOL_WEAK = 0x00fa;                  // manufacturer id, here KNXA

cem_mode_t cem_mode = sun_mode;

bool cem_inverter_put_called = false; // binary (toggle) marker to tell parent c++ code an action
bool cem_inverter_get_called = false; // binary (toggle) marker to tell parent c++ code an action
bool cem_charger_put_called = false; // binary (toggle) marker to tell parent c++ code an action

float_array_functional_block_t cem = {
  427,
  1,
  1,
  {
    {0, /* IEEE 754 single float, KNX DPT: 14.056 */
      "/p/inverter",
      "urn:knx:dpa.427.60",
      ":dpt.value_power",
      "CEM Input from Inverter",
      DPH_NO_ERROR},
    {0, /* IEEE 754 single float, KNX DPT: 14.056 */
     "/p/charger",
     "urn:knx:dpa.427.52",
     ":dpt.value_power",
     "CEM Output to Charger",
     DPH_NO_ERROR}
  }
};

/* KNX-IoT datapoint functions */
// CEM inverter has GET + PUT (is input)
static void knx_iot_put_cem_inverter(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
  knx_iot_put_handler(request, interfaces, user_data, OC_REP_FLOAT, &cem.point[CEM_INVERTER].value, NULL, &cem.point[CEM_INVERTER].flags, cem.point[CEM_INVERTER].name);
}

static void knx_iot_get_cem_inverter(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
  knx_iot_get_handler(request, interfaces, user_data, OC_REP_FLOAT, &cem.point[CEM_INVERTER].value, &cem.point[CEM_INVERTER].flags, cem.point[CEM_INVERTER].name);
}

// CEM charger has GET (is output)
static void knx_iot_get_cem_charger(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
  knx_iot_get_handler(request, interfaces, user_data, OC_REP_FLOAT, &cem.point[CEM_CHARGER].value, &cem.point[CEM_CHARGER].flags, cem.point[CEM_CHARGER].name);
}

void knx_iot_register_resources(void)
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

  /* CEM defines
       GET**, PUT
       GET**
       Interface type if.p/if.d is set in addition, the point can also be used as parameter point (in add. to s-mode).

       **note that a GET also handles the query metadata request, regardless if it may be an 'input', see Callback Notes
    */
  oc_resource_set_request_handler(power_dc_resource_inverter_in, OC_GET, knx_iot_get_cem_inverter, NULL, OC_ACL_I , OC_IF_I);
  oc_resource_set_request_handler(power_dc_resource_inverter_in, OC_PUT, knx_iot_put_cem_inverter, NULL, OC_ACL_I | OC_ACL_P, OC_IF_I | OC_IF_P);
  oc_resource_set_request_handler(power_dc_resource_charger_out, OC_GET, knx_iot_get_cem_charger, NULL, OC_ACL_O | OC_ACL_D, OC_IF_O | OC_IF_D);

  oc_add_resource(power_dc_resource_inverter_in);
  oc_add_resource(power_dc_resource_charger_out);
}

/* KNX-IoT app interface functions */
// CEM KNX-IoT app interface functions
cem_mode_t get_cem_mode(void) { return cem_mode; }
void set_cem_mode(cem_mode_t mode) { cem_mode = mode; }

float get_cem_inverter_value(void) { return cem.point[CEM_INVERTER].value; }
char* get_cem_inverter_href(void) { return cem.point[CEM_INVERTER].resource_path; }
uint8_t get_cem_inverter_flags(void) { return cem.point[CEM_INVERTER].flags; }
void clear_cem_inverter_flags(uint8_t flags) { UNSET_BIT(cem.point[CEM_INVERTER].flags, flags); }

float get_cem_charger_value(void) { return cem.point[CEM_CHARGER].value; }
void set_cem_charger_value(float value) { cem.point[CEM_CHARGER].value = value; }
char* get_cem_charger_href(void) { return cem.point[CEM_CHARGER].resource_path; }
uint8_t get_cem_charger_flags(void) { return cem.point[CEM_CHARGER].flags; }
void clear_cem_charger_flags(uint8_t flags) { UNSET_BIT(cem.point[CEM_CHARGER].flags, flags); }
