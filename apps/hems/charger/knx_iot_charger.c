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
 * Charger definitions
 *
 * Note that all below values are statically defined since they do not change during
 * device lifetime or are predefined in the KNX IoT specification.
 *
 */
const char application_name[] KNX_TOOL_WEAK = "Charger";
const char sn_lower_case[] KNX_TOOL_WEAK = "00fa10020d00";  // deliberated incorrect serial numbers
const char hw_type[] KNX_TOOL_WEAK = "000102030405";        // 12 string chars, MSB = 00
const char dev_model[] KNX_TOOL_WEAK = "6800";              // reuse mask version from iot device
const uint32_t mid KNX_TOOL_WEAK = 0x00fa;                  // manufacturer id, here KNXA

float_functional_block_t charger = 
{1254, 0,1,
  {
    0, /* IEEE 754 single float, KNX DPT: 14.056 */
    "/p/charger",
    "urn:knx:dpa.1254.52",
    ":dpt.value_power", 
    "Charger Input from CEM",
    DPH_NO_ERROR
  }
};

/* KNX-IoT datapoint functions */
// charger has GET + PUT (is input)
static void knx_iot_get_charger(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
  knx_iot_get_handler(request, interfaces, user_data, OC_REP_FLOAT, &charger.point.value, &charger.point.flags, charger.point.name);
}

static void knx_iot_put_charger(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
  knx_iot_put_handler(request, interfaces, user_data, OC_REP_FLOAT, &charger.point.value, NULL, &charger.point.flags, charger.point.name);
}

void knx_iot_register_resources(void)
{
  oc_resource_t* active_power_limit_resource_charger_in = oc_new_resource(charger.point.resource_path, 1);

  oc_resource_bind_resource_type(active_power_limit_resource_charger_in, charger.point.dpa);
  oc_resource_bind_dpt(active_power_limit_resource_charger_in, charger.point.dpt);
  oc_resource_bind_content_type(active_power_limit_resource_charger_in, APPLICATION_CBOR, CONTENT_NONE);

  oc_resource_set_functional_block_data(active_power_limit_resource_charger_in, charger.fb_number, charger.fb_instance, charger.fb_number_of_datapoints);

  oc_resource_set_properties(active_power_limit_resource_charger_in, OC_OBSERVABLE + OC_DISCOVERABLE);

  /* Charger defines
       GET**, PUT
       Interface type if.p is set in addition, the point can also be used as parameter point (in add. to s-mode).

       **note that a GET also handles the query metadata request, regardless if it may be an 'input', see Callback Notes
  */
  oc_resource_set_request_handler(active_power_limit_resource_charger_in, OC_GET, knx_iot_get_charger, NULL, OC_ACL_I , OC_IF_I);
  oc_resource_set_request_handler(active_power_limit_resource_charger_in, OC_PUT, knx_iot_put_charger, NULL, OC_ACL_I | OC_ACL_P, OC_IF_I | OC_IF_P); 

  oc_add_resource(active_power_limit_resource_charger_in);
}

/* KNX-IoT app interface functions */
// charger KNX-Iot app interface functions
float get_charger_value(void) { return charger.point.value; }
char* get_charger_href(void) { return charger.point.resource_path; }
uint8_t get_charger_flags(void) { return charger.point.flags; }
void clear_charger_flags(uint8_t flags) { UNSET_BIT(charger.point.flags, flags); }
