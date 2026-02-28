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
#define SOO (0)
#define IOO (1)
#define NUM_POINTS (2)

// common data for EITT, for the mixture of EITT (test template) channel definitions
#define LSSB (1)
#define LSAB (0)

#ifdef __cplusplus
extern "C"
{
#endif

  /* KNX-IoT datapoint functions */

  /*
   *  Collection of all proto definitions of the - by stack demos - used PUT/GET methods.
   *  For handler details see Callback Notes in 'knx_iot_app.h'
   */

  // LSAB/LSSB
  void knx_iot_get_lsxb(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data);
  void knx_iot_put_lsab(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data);
  void knx_iot_put_lssb(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data);

  // Testpoint
  void knx_iot_get_test_parameter(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data);
  void knx_iot_put_test_parameter(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data);

  /* LSAB/LSSB KNX-IoT app interface functions */

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
