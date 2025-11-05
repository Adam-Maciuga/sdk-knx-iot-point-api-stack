/*
-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
 Copyright (c) 2022-2023 Cascoda Ltd
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

/**
 * @file
 *
 * KNX virtual sensor
 *
 * ## Application Design
 *
 * - app_init, initializes the stack values.
 *
 * - register_resources, function that registers all endpoints, e.g. sets the GET/.../DELETE
 *   handlers for each end point
 *
 * - main, starts the stack, with the registered resources, can be compiled out with NO_MAIN
 *
 * - callback handlers for the implemented methods, see callback handler 'Callback Notes'
 *   
 * ## stack specific defines
 * - __linux__, build for Linux
 * - WIN32,  build for Windows
 * - OC_OSCORE, oscore is enabled as compile flag
 *
 * ## File specific defines
 * - NO_MAIN
 *   compile out the function main()
 * - KNX_GUI
 *   build the GUI with console option, so that all
 *   logging can be seen in the command window
 */

#include "oc_api.h"
#include "port/oc_storage.h"
#include <stdio.h> // defines FILENAME_MAX
#include "apps/ems/knx_iot_virtual_ems.h" 

const char application_name[] = "Charger";
const char sn_lower_case[] = "00fa10020d00";  // deliberated incorrect serial numbers
const char hostname[] = "knx-00fa10020d00";   // default host name (reset uses this default)
const char hw_type[] = "000102030405";        // 12 string chars, MSB = 00
const char dev_model[] = "6800";              // reuse mask version from iot device
const uint32_t mid = 0x00fa;                  // manufacturer id, here KNXA

int_functional_block_t charger = 
{421, 0,1,
  {
    0,
    "/p/charger",
    "urn:knx:dpa.xxx.xx",
    ":dpt.value_power", /* IEEE 754 single float, KNX DPT: 14.056 */
    "CHARGER"
  }
};

void register_resources(void)
{
  oc_resource_t* CHARGER_resource = oc_new_resource(charger.point.resource_path, 1);

  oc_resource_bind_resource_type(CHARGER_resource, charger.point.dpa);
  oc_resource_bind_dpt(CHARGER_resource, charger.point.dpt);
  oc_resource_bind_content_type(CHARGER_resource, APPLICATION_CBOR, CONTENT_NONE);

  oc_resource_set_properties(CHARGER_resource, OC_OBSERVABLE + OC_DISCOVERABLE);

  oc_resource_set_request_handler(CHARGER_resource, OC_GET, get_charger, NULL, OC_ACL_I | OC_ACL_D, OC_IF_I | OC_IF_D);
  oc_resource_set_request_handler(CHARGER_resource, OC_PUT, put_charger, NULL, OC_ACL_I | OC_ACL_P, OC_IF_O | OC_IF_D); 

  oc_add_resource(CHARGER_resource);
}

int app_initialize_stack(void)
{
  /*
    The final storage folder depends on the build system/ current directory on Linux/ Windows,
    the folder name is defined by the file name + serial number.
    Code below should work both on Linux/Windows.

    For a specific embedded OS usually this functionality needs to be adapted.
  */

  char storage[64];

  #if defined(_WIN32) || defined(__unix__) || defined(__APPLE__)

  char dir[FILENAME_MAX] = "";
  GetCurrentDir(dir, FILENAME_MAX);
  (void)snprintf(storage, sizeof(storage), "%s/knx_iot_virtual_charger_%s", dir, sn_lower_case);
  OC_INF("Current path is: '%s'", dir);

  #endif

  oc_storage_config(storage);

  // initialize the 'application' runtime variables
  initialize_variables(); 

  // set the stack handler callbacks, details for each handler see oc_handler_t
  static oc_handler_t handler = {.init = app_init,
                                 .signal_event_loop = signal_event_loop,
                                 .register_resources = register_resources,
                                 .requests_entry = NULL};

  // set the application handler callbacks
  oc_set_hostname_cb(hostname_cb, NULL);
  oc_set_factory_presets_cb(factory_presets_cb, NULL);
  oc_set_swu_cb(swu_cb, NULL);

  // start the stack, calls directly also the .init handler from above
  return oc_main_init(&handler);
}
