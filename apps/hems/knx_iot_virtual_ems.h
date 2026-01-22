/*
-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
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

/*
  Note that the file 'knx_iot_virtual_ems.c' is NOT a part of the stack or not intended to be an
  'application' library. It hosts only for the below described EMS application commonly used
  functionality in one place.
*/

#ifndef KNX_IOT_VIRTUAL_EMS_H
#define KNX_IOT_VIRTUAL_EMS_H

#include "apps/knx_iot_virtual.h"

typedef enum cem_mode_t
{
  sun_mode = 0,
  mix_mode = 1
} cem_mode_t;


#define CEM_INVERTER (0)
#define CEM_CHARGER (1)
#define CEM_INVERTER_TRESHOLD (3990) // float compare is tricky, so make it easy


/*
 for functional block details see Functional Block Notes in 'knx_iot_virtual.h'
*/

#ifdef __cplusplus
extern "C"
{
#endif

  /*
     Collection of all proto definitions of the - by stack demos - used PUT/GET methods.
     For handler details see Callback Notes in 'knx_iot_virtual.h'
  */

  // charger
  void get_charger(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data);
  void put_charger(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data);

  // inverter
  void get_inverter(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data);

  // cem 
  void put_cem_inverter(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data);
  void get_cem_inverter(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data);
  void get_cem_charger(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data);

  // ems local functions

  // cem
  cem_mode_t get_cem_mode(void);
  void set_cem_mode(cem_mode_t mode);

  float get_cem_inverter_value(void);
  float get_cem_charger_value(void);
  void set_cem_charger_value(float value);

  uint8_t cem_charger_flags(void);
  uint8_t cem_inverter_flags(void);

  void clear_cem_charger_flags(uint8_t flags);
  void clear_cem_inverter_flags(uint8_t flags);

  char* app_retrieve_href_from_cem_inverter(void);
  char* app_retrieve_href_from_cem_charger(void);

  // inverter
  void set_inverter_value(float value);

  uint8_t inverter_flags(void);
  void clear_inverter_flags(uint8_t flags);
  
  char* app_retrieve_href_from_inverter(void);
  

  // charger
  float get_charger_value(void);

  uint8_t charger_flags(void);
  void clear_charger_flags(uint8_t flags);

  char* app_retrieve_href_from_charger(void);


  // common

  /**
   * @brief Application callback handler, called on 'restart' command
   *
   * @note The callback handler is individual per applications are call
   *
   * @param data callback user data
   */
  void app_restart_handler(void* data);

#ifdef __cplusplus
}
#endif
#endif
