/* 
 * Copyright (c) 2022-2023 Cascoda Ltd
 * Copyright (c) 2024-2026 KNX Association
 *            
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 *
 * KNX-IoT demo EITT certification application, for more details see 'knx_iot_application_template' c-file.
 *
 */

#include "oc_api.h"
#include "apps/knx_iot_datapoint.h" 
#include "apps/knx/knx_iot_knx.h" 

/*
 * EITT definitions
 *
 * Note that all below values are statically defined since they do not change during
 * device lifetime or are predefined in the KNX IoT specification.
 *
 */
const char application_name[] KNX_TOOL_WEAK = "KNX-IoT demo EITT certification application";
const char sn_lower_case[] KNX_TOOL_WEAK = "00fa10020800";  // same as eitt test template, deliberated incorrect serial number
const char hw_type[] KNX_TOOL_WEAK = "Windows";             // 12 string chars, same as eitt test template
const char dev_model[] KNX_TOOL_WEAK = "KNX Certification"; // same as eitt test template, see notes (a)
const uint32_t mid KNX_TOOL_WEAK = 667;                     // same as eitt test template

/*
  (a) according to the specification it can be a device model or order number, a vendor has to device what to return.
    - for the EITT we use eitt test template value
    - for a real application this can be adapted to a more meaningful value, e.g. a specific product name or order number
*/

/*
 *
 * Below defined (artificial) datapoints and test parameters for functional block
 * 417 (LSAB) and 421 (LSBB) EITT test template defaults.
 * 
 * Note:
 * Set instance to 1, even if there are no more instances for the specific FB;
 * for EITT test 5.7.2.1/ 5.3.20 with default template test value 00417_01
 * 
 * For details see 'lsxb_channel_t' definition.
 *  
 */

// LSAB/LSSB channel 0..1 + included EPs switch control/status
lsxb_channel_t lsxb[LSXB_NUM_CHANNELS] = {
  {
    // LSAB
    417,1, 2,
    {
      {false, "/p/1", "urn:knx:dpa.417.61", ":dpt.switch", (0 << 8) + 0}, 
      {false, "/p/2", "urn:knx:dpa.417.62", ":dpt.switch", (0 << 8) + 1}
    }
  },
  {
    // LSSB
    421, 1, 2,
    {
      {false, "/p/3", "urn:knx:dpa.421.61", ":dpt.switch", (1 << 8) + 0},
      {false, "/p/4", "urn:knx:dpa.421.62", ":dpt.switch", (1 << 8) + 1}
    }
  },
};

// additional parameter
int_datapoint_t test_parameter = {
  0, "/p/p1", "urn:knx:dpa.65500.201", ":dpt.propDataType", "Global Test Parameter"
};

/* KNX-IoT datapoint functions */
/*
 * Note:
 * GET also handles the query metadata request, regardless if it may be an 'input', see Callback Notes.
 */
void knx_iot_register_resources(void)
{
  PRINT("Register LSAB/LSSB 0...1 channel control/status resources");

  // LSAB - SOO
  knx_iot_register_functional_block_datapoint(
          lsxb[LSAB].fb_number, lsxb[LSAB].fb_instance, lsxb[LSAB].fb_number_of_datapoints,
          lsxb[LSAB].point[SOO].resource_path, lsxb[LSAB].point[SOO].dpa, lsxb[LSAB].point[SOO].dpt,
          OC_DISCOVERABLE + OC_OBSERVABLE,
          // Define user data for GET/PUT, needed to distinguish the call source
          (void*)(uintptr_t)lsxb[LSAB].point[SOO].id,
          // Note:
          // No interface type if.p/if.d is set in addition, the points are only used for s-mode runtime communication.
          knx_iot_get_lsxb, OC_ACL_I, OC_IF_I,
          knx_iot_put_lsab, OC_ACL_I, OC_IF_I
  );

  // LSAB - IOO
  knx_iot_register_functional_block_datapoint(
          lsxb[LSAB].fb_number, lsxb[LSAB].fb_instance, lsxb[LSAB].fb_number_of_datapoints,
          lsxb[LSAB].point[IOO].resource_path, lsxb[LSAB].point[IOO].dpa, lsxb[LSAB].point[IOO].dpt,
          OC_DISCOVERABLE + OC_OBSERVABLE,
          // Define user data for GET/PUT, needed to distinguish the call source
          (void*)(uintptr_t)lsxb[LSAB].point[IOO].id,
          // Note:
          // No interface type if.p/if.d is set in addition, the points are only used for s-mode runtime communication.
          knx_iot_get_lsxb, OC_ACL_O, OC_IF_O,
          // LSAB - IOO is an output and has no PUT handler.
          NULL, OC_ACL_NONE, OC_IF_NONE
  );

  // LSSB - SOO
  knx_iot_register_functional_block_datapoint(
          lsxb[LSSB].fb_number, lsxb[LSSB].fb_instance, lsxb[LSSB].fb_number_of_datapoints,
          lsxb[LSSB].point[SOO].resource_path, lsxb[LSSB].point[SOO].dpa, lsxb[LSSB].point[SOO].dpt,
          OC_DISCOVERABLE + OC_OBSERVABLE,
          // Define user data for GET/PUT, needed to distinguish the call source
          (void*)(uintptr_t)lsxb[LSSB].point[SOO].id,
          // Note:
          // No interface type if.p/if.d is set in addition, the points are only used for s-mode runtime communication.
          knx_iot_get_lsxb, OC_ACL_O, OC_IF_O,
          // LSSB - SOO is an output and has no PUT handler.
          NULL, OC_ACL_NONE, OC_IF_NONE
  );

  // LSSB - IOO
  knx_iot_register_functional_block_datapoint(
          lsxb[LSSB].fb_number, lsxb[LSSB].fb_instance, lsxb[LSSB].fb_number_of_datapoints,
          lsxb[LSSB].point[IOO].resource_path, lsxb[LSSB].point[IOO].dpa, lsxb[LSSB].point[IOO].dpt,
          OC_DISCOVERABLE + OC_OBSERVABLE,
          // Define user data for GET/PUT, needed to distinguish the call source
          (void*)(uintptr_t)lsxb[LSSB].point[IOO].id,
          // Note:
          // No interface type if.p/if.d is set in addition, the points are only used for s-mode runtime communication.
          knx_iot_get_lsxb, OC_ACL_I, OC_IF_I,
          knx_iot_put_lssb, OC_ACL_I, OC_IF_I
  );

  PRINT("Register test parameter");
  knx_iot_register_datapoint(
          test_parameter.resource_path, test_parameter.dpa, test_parameter.dpt,
          OC_DISCOVERABLE + OC_OBSERVABLE + OC_WRITE_AFFECTS_FP,
          NULL,
          knx_iot_get_test_parameter, OC_ACL_D, OC_IF_D,  // r/w, see EP handler
          knx_iot_put_test_parameter, OC_ACL_P, OC_IF_P   // r/w, see EP handler
  );
}
