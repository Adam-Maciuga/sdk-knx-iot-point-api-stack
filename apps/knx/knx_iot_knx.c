/* 
 * Copyright (c) 2024-2026 KNX Association
 *            
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ctype.h"
#include "oc_api.h"
#include "knx_iot_knx.h"
#include "oc_core_res.h"
#include "oc_helpers.h"
#include "api/oc_knx_fp.h"
#include "oc_knx_client.h"

// defied individually in the corresponding LSAB/LSSB/EITT application code
extern lsxb_channel_t lsxb[];
extern int_datapoint_no_flags_t test_parameter;

/* KNX-IoT datapoint functions */

// user data is the pointer to a 16 bit encoded channel/datapoint
// Note:
// skip compiler warning by cast from 64 bit	// TODO shouldn't we fix this?
#define USER_DATA_TO_CHANNEL_AND_POINT(user_data) \
  const size_t channel_and_datapoint = (uintptr_t)user_data; \
  const uint8_t channel = channel_and_datapoint >> 8 & 0xFF; \
  const uint8_t point = channel_and_datapoint & 0xFF;

// application callback for LSAB PUT handler
static void knx_iot_put_lsab_callback(void* user_data, const oc_rep_value_type_t value_type, volatile void* value, const char* description)
{
  USER_DATA_TO_CHANNEL_AND_POINT(user_data)

  // correct data retrieved
  // set LSAB status (note, for a real hw device the status usually needs to be determined from the actual hw relay)
  PRINT("received no error, update %s status to %d", description, lsxb[channel].point[SOO].value);
  lsxb[channel].point[IOO].value = lsxb[channel].point[SOO].value;

  // trigger the LSAB status on a specific resource path (ioo)
  PRINT("send status to %s with flag: 'w'", lsxb[channel].point[IOO].resource_path);
  oc_send_s_mode_mc_or_uc_message(OC_SENDER_MULTICAST_SCOPE, lsxb[channel].point[IOO].resource_path, 'w');
}

// generic GET for LSSB/LSAB/EITT applications for SOO and IOO
void knx_iot_get_lsxb(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
  USER_DATA_TO_CHANNEL_AND_POINT(user_data)

  // Note:
  // For the example bool_datapoint_no_name_no_flags_t is used so description "LSxB" is hardcoded.
  knx_iot_get_handler(request, interfaces, user_data, OC_REP_BOOL, &lsxb[channel].point[point].value, NULL, "LSxB");
}

// specific PUT for LSAB/EITT applications for SOO (SOO write - IOO will be updated ... )
void knx_iot_put_lsab(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
  USER_DATA_TO_CHANNEL_AND_POINT(user_data)

  // Note:
  // For the example bool_datapoint_no_name_no_flags_t is used so description "LSAB" is hardcoded.
  // Also LSAB uses a custom callback.
  knx_iot_put_handler(request, interfaces, user_data, OC_REP_BOOL, &lsxb[channel].point[point].value, knx_iot_put_lsab_callback, NULL, "LSAB");
}

// specific PUT for LSSB/EITT applications for IOO (IOO write - nothing will be updated ... )
void knx_iot_put_lssb(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
  USER_DATA_TO_CHANNEL_AND_POINT(user_data)

  // Note:
  // For the example bool_datapoint_no_name_no_flags_t is used so description "LSSB" is hardcoded.
  knx_iot_put_handler(request, interfaces, user_data, OC_REP_BOOL, &lsxb[channel].point[point].value, NULL, NULL, "LSSB");
}

// generic GET for LSSB/LSAB/EITT applications 
void knx_iot_get_test_parameter(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
  knx_iot_get_handler(request, interfaces, user_data, OC_REP_INT, &test_parameter.value, NULL, test_parameter.name);
}

// generic PUT for LSSB/LSAB/EITT applications
void knx_iot_put_test_parameter(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
  knx_iot_put_handler(request, interfaces, user_data, OC_REP_INT, &test_parameter.value, NULL, NULL, test_parameter.name);
}

/* General KNX-IoT Stack Callbacks */
void knx_iot_restart_cb(void *data)
{
  (void)data;

  for (int i = 0; i < NUM_CHANNELS; i++)
  {
    // set default runtime values after restart, note
    lsxb[i].point[0].value = false;
    lsxb[i].point[1].value = false;
  }
}

/* KNX-IoT app interface functions */
// DATAPOINT common code 
bool get_channel_value(uint8_t channel, uint8_t point) { return lsxb[channel].point[point].value; }
void set_channel_value(uint8_t channel, uint8_t point, bool value) { lsxb[channel].point[point].value = value; }
char* get_channel_href(uint8_t channel, uint8_t point) { return lsxb[channel].point[point].resource_path; }

// PARAMETER code - needs to be defined in case of specific parameter handling
char* app_get_parameter_url(int index) { return index == 0 ?  test_parameter.resource_path: NULL; }	// TODO FIXME href vs. url
char* app_get_parameter_name(int index) { return index == 0 ? test_parameter.name : NULL; }
