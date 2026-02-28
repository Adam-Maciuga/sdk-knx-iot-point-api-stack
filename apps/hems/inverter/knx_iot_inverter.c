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
 * Inverter definitions
 *
 * Note that all below values are statically defined since they do not change during
 * device lifetime or are predefined in the KNX IoT specification.
 *
 */
const char application_name[] KNX_TOOL_WEAK = "Inverter";
const char sn_lower_case[] KNX_TOOL_WEAK = "00fa10020b00";  // deliberated incorrect serial numbers
const char hw_type[] KNX_TOOL_WEAK = "000102030405";        // 12 string chars, MSB = 00
const char dev_model[] KNX_TOOL_WEAK = "6800";              // reuse mask version from iot device
const uint32_t mid KNX_TOOL_WEAK = 0x00fa;                  // manufacturer id, here KNXA

float_functional_block_t inverter = {
  1250, 1, 1,
  {
    0, /* IEEE 754 single float, KNX DPT: 14.056 */
    "/p/inverter",
    "urn:knx:dpa.1250.60",
    ":dpt.value_power",
    "Inverter Output to CEM",
    DPH_NO_ERROR}
};

/* KNX-IoT datapoint functions */
// inverter has GET (is output)
static void knx_iot_get_inverter(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
  knx_iot_get_handler(request, interfaces, user_data, OC_REP_FLOAT, &inverter.point.value, &inverter.point.flags, inverter.point.name);
}

void knx_iot_register_resources(void)
{   
  oc_resource_t* power_dc_resource_inverter_out = oc_new_resource(inverter.point.resource_path, 1);
  
  oc_resource_bind_resource_type(power_dc_resource_inverter_out, inverter.point.dpa);
  oc_resource_bind_dpt(power_dc_resource_inverter_out, inverter.point.dpt);
  oc_resource_bind_content_type(power_dc_resource_inverter_out, APPLICATION_CBOR, CONTENT_NONE);

  oc_resource_set_functional_block_data(power_dc_resource_inverter_out, inverter.fb_number, inverter.fb_instance, inverter.fb_number_of_datapoints);

  oc_resource_set_properties(power_dc_resource_inverter_out, OC_OBSERVABLE + OC_DISCOVERABLE);

  /* Inverter defines
       GET**
       Interface type if.d is set in addition, the point can also be used as parameter point (in add. to s-mode).

       **note that a GET also handles the query metadata request, regardless if it may be an 'input', see Callback Notes
  */
  oc_resource_set_request_handler(power_dc_resource_inverter_out, OC_GET, knx_iot_get_inverter, NULL, OC_ACL_O | OC_ACL_D, OC_IF_O | OC_IF_D);
  
  oc_add_resource(power_dc_resource_inverter_out);  
}

/* KNX-IoT app interface functions */
// inverter KNX-IoT app interface functions
void set_inverter_value(float value) { inverter.point.value = value; }
char* get_inverter_href(void) { return inverter.point.resource_path; }
uint8_t get_inverter_flags(void) { return inverter.point.flags; }
void clear_inverter_flags(uint8_t flags) { UNSET_BIT(inverter.point.flags, flags); }
