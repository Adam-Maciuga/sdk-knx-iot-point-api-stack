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

/*
 network

 A network router may not allow to send multicast with scope 5 (site local),
 hence the DEMO applications use scope 2 instead. If needed, sendout with scope 2 and 5
 separately may be an option (2 messages).

 */
#define SENDER_SCOPE (2)

typedef struct
{
  uint16_t fb_number;
  uint8_t fb_instance;
  uint8_t fb_number_of_datapoints;

  int_datapoint_t point;
} int_functional_block_t;

#ifdef __cplusplus
extern "C"
{
#endif

  /*
     Collection of all proto definitions of the - by stack demos - used PUT/GET methods.
     For more details see the methods as such.

   */

  // EMS
  void get_charger(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data);
  void put_charger(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data);

  
#ifdef __cplusplus
}
#endif
#endif
