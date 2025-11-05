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

#ifndef KNX_IOT_VIRTUAL_KNX_H
#define KNX_IOT_VIRTUAL_KNX_H

#include "apps/knx_iot_virtual.h"

// common data
#define NUM_CHANNELS (2)
#define NUM_POINTS   (2)
#define SOO  (0)
#define IOO  (1)


/*
  Defines the basic (channel oriented) structure of a LSAB/LSSB/EITT functional block definition.

  An FB consists of a number of datapoints with its values, endpoints (EP) and URNs.

  - the FB number, despite any DPA scheme that is used from the in FB included datapoints
    (note that an FB such as 417 may also reuse predefined datapoints from other FB's with DPA type 312.xx, 2nn.xx
     or similar, FB 421 is NOT only using 'self defined' 417.xx types)

  - the FB instance, 0...n, 0 = only one instance, > 0 more than one instance, see also 'oc_resource_set_function_block_data'

  - the number of 'visible' datapoint in an FB, note that if this number WOULD change e.g.; when adding/deleting resources
    or make some invisible   
    - caused by ETS (e.g, partial download with changed parameter setting)
    - caused by own application at runtime (e.g, HMI parameter adjustment by user)
    the correct number must be applied by the application to the FB. 

  - the datapoints, see above

*/
typedef struct
{
  uint16_t fb_number;
  uint8_t fb_instance;
  uint8_t fb_number_of_datapoints;

  bool_datapoint_t point[NUM_POINTS];
} lsxb_channel_t, bool_functional_block_t;


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

  // LSAB/LSSB
  void get_lsxb(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data);
  void put_lsab(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data);
  void put_lssb(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data);

  void get_test_parameter(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data);
  void put_test_parameter(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data);

#ifdef __cplusplus
}
#endif
#endif
