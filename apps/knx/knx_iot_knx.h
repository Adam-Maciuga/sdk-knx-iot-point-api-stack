/*        
 * Copyright (c) 2022-2023 Cascoda Ltd
 * Copyright (c) 2024-2026 KNX Association
 *            
 * SPDX-License-Identifier: Apache-2.0
 */     

/*
 * Note that the file 'knx_iot_knx.c' is NOT a part of the stack or not intended to be an
 * 'application' library. It hosts only for the below described LSAB/LSSB/EITT application commonly used
 * functionality in one place.
 */

#ifndef KNX_IOT_KNX_H
#define KNX_IOT_KNX_H

#include "apps/knx_iot_app.h"

// common data for LSAB/LSSB 
#define NUM_POINTS (2)
#define SOO (0)
#define IOO (1)

// common data for EITT, for the mixture of EITT (test template) channel definitions
#define LSSB (1)
#define LSAB (0)

// user data is the pointer to a 16 bit encoded channel/datapoint
// Note:
// skip compiler warning by cast from 64 bit    // TODO shouldn't we fix this?
#define USER_DATA_TO_CHANNEL_AND_POINT(user_data) \
  const size_t channel_and_datapoint = (uintptr_t)user_data; \
  const uint8_t channel = channel_and_datapoint >> 8 & 0xFF; \
  const uint8_t point = channel_and_datapoint & 0xFF;

#ifdef __cplusplus
extern "C"
{
#endif

  /* KNX-IoT datapoint functions */

  /*
   *  Collection of all proto definitions of the - by stack demos - used PUT/GET methods.
   *  For handler details see Callback Notes in 'knx_iot_app.h'
   */

  // LSSB/LSAB
  void knx_iot_get_lsxb(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data);
  void knx_iot_put_lssb(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data);
  // Note:
  // knx_iot_put_lssb_callback() will be declared weak for embedded platform (which usually use GCC) depending function.
  // So the function can be overwritten there to take actions depending on the paramters and hardware.
  void knx_iot_put_lssb_callback(void* user_data, const oc_rep_value_type_t value_type, volatile void* value, const char* description);
  void knx_iot_put_lsab(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data);
  // Note:
  // knx_iot_put_lsab_callback() will be declared weak for embedded platform (which usually use GCC) depending function.
  // So the function can be overwritten there to take actions depending on the paramters and hardware.
  void knx_iot_put_lsab_callback(void* user_data, const oc_rep_value_type_t value_type, volatile void* value, const char* description);

  // Testpoint
  void knx_iot_get_test_parameter(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data);
  void knx_iot_put_test_parameter(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data);

  /* LSSB/LSAB KNX-IoT app interface functions */

  /**
   * @brief Get the value of a channel point
   *
   * @param channel the channel for the value to get
   * @param point the point of the channel for the value to get
   */
  bool get_channel_value(uint8_t channel, uint8_t point);

   /**
   * @brief Set the value of a channel point
   *
   * @param channel the channel for the value to set
   * @param point the point of the channel for the value to set
   * @param value value to set
   */
  void set_channel_value(uint8_t channel, uint8_t point, bool value);

   /**
   * @brief Get the URL of a channel point
   *
   * @param channel the channel for the URL to get
   * @param point the point of the channel for the URL to get
   * @return the URL of the channel point
   */
  char* get_channel_href(uint8_t channel, uint8_t point);

#ifdef __cplusplus
}
#endif
#endif
