/* 
 * Copyright (c) 2022-2023 Cascoda Ltd
 * Copyright (c) 2024-2026 KNX Association
 *            
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 *
 * KNX-IoT LSAB actuator, for more details see 'knx_iot_application_template' c-file.
 *
 */

#include "oc_api.h"
#include "apps/knx_iot_datapoint.h"
#include "apps/knx/knx_iot_knx.h"

/*
 * LSAB definitions
 *
 * Note that all below values are statically defined since they do not change during
 * device lifetime or are predefined in the KNX IoT specification.
 *
 */
const char application_name[] KNX_TOOL_WEAK = "KNX-IoT demo actuator (LSAB)";
const char sn_lower_case[] KNX_TOOL_WEAK = "00fa10020900";  // deliberated incorrect serial number
const char hw_type[] KNX_TOOL_WEAK = "000102030405";        // 12 string char, MSB = 00
const char dev_model[] KNX_TOOL_WEAK = "6800";              // mask version for KNX IoT device
const uint32_t mid KNX_TOOL_WEAK = 0x00fa;                  // manufacturer id, here KNXA

/*

 Below defined datapoints and test parameters for functional block 417 (LSAB) command/control.
 Details see on 'lsxb_channel_t' definition.

 */

// define LSAB channel 0..1 + included EPs switch control/status
lsxb_channel_t lsxb[LSXB_NUM_CHANNELS] = {
  {
    417, 1, NUM_POINTS,
    {
      {false, "/p/lsab/0/soo", "urn:knx:dpa.417.52", ":dpt.switch", (0 << 8) + 0},	 // TODO FIXME .52 vs. .61 in EITT example
      {false, "/p/lsab/0/ioo", "urn:knx:dpa.417.51", ":dpt.switch", (0 << 8) + 1}	 // TODO FIXME .51 vs. .62 in EITT example
    }
  }, 
  {
    417, 2, NUM_POINTS,
    {
      {false, "/p/lsab/1/soo", "urn:knx:dpa.417.52", ":dpt.switch", (1 << 8) + 0},
      {false, "/p/lsab/1/ioo", "urn:knx:dpa.417.51", ":dpt.switch", (1 << 8) + 1}
    }
  }
};

// additional parameters
int_datapoint_t test_parameter = {
  0, "/p/globalTestParameter", "urn:knx:dpa.65500.201", ":dpt.value2Ucount", "Global Test Parameter"
};

/* KNX-IoT datapoint functions */
/*  
 * Note:
 * GET also handles the query metadata request, regardless if it may be an 'input', see Callback Notes.
 */
void knx_iot_register_resources(void)
{
  PRINT("Register LSAB 0...1 channel control/status resource");

  for (int i = 0; i < LSXB_NUM_CHANNELS; i++)
  {
    // Note:
    // We have 2 x an FB with the same id

    // LSAB - SOO
    knx_iot_register_functional_block_datapoint(
            lsxb[i].fb_number, lsxb[i].fb_instance, lsxb[i].fb_number_of_datapoints,
            lsxb[i].point[SOO].resource_path, lsxb[i].point[SOO].dpa, lsxb[i].point[SOO].dpt,
            OC_DISCOVERABLE + OC_OBSERVABLE,
            // Define user data for GET/PUT, needed to distinguish the call source
            (void*)(uintptr_t)lsxb[i].point[SOO].id,
            // Note:
            // No interface type if.p/if.d is set in addition, the points are only used for s-mode runtime communication.
            knx_iot_get_lsxb, OC_ACL_I, OC_IF_I,
            knx_iot_put_lsab, OC_ACL_I, OC_IF_I
    );

    // LSAB - IOO
    knx_iot_register_functional_block_datapoint(
            lsxb[i].fb_number, lsxb[i].fb_instance, lsxb[i].fb_number_of_datapoints,
            lsxb[i].point[IOO].resource_path, lsxb[i].point[IOO].dpa, lsxb[i].point[IOO].dpt,
            OC_DISCOVERABLE + OC_OBSERVABLE,
            // Define user data for GET/PUT, needed to distinguish the call source
            (void*)(uintptr_t)lsxb[i].point[IOO].id,
            // Note:
            // No interface type if.p/if.d is set in addition, the points are only used for s-mode runtime communication.
            knx_iot_get_lsxb, OC_ACL_O, OC_IF_O,
            // LSAB - IOO is an output and has no PUT handler.
            NULL, OC_ACL_NONE, OC_IF_NONE
    );
  }

  PRINT("Register test parameter");
  knx_iot_register_datapoint(
          test_parameter.resource_path, test_parameter.dpa, test_parameter.dpt,
          OC_DISCOVERABLE + OC_OBSERVABLE + OC_WRITE_AFFECTS_FP,
          NULL,
          knx_iot_get_test_parameter, OC_ACL_D, OC_IF_D,  // r/w, see EP handler
          knx_iot_put_test_parameter, OC_ACL_P, OC_IF_P   // r/w, see EP handler
  );
}
