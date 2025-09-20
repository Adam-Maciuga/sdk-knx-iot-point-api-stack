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
 * KNX virtual actuator LSAB, for more details see 'knx_iot_application_template' c-file.
 *
 */

#include "oc_api.h"
#include "port/oc_storage.h"
#include <stdio.h> // defines FILENAME_MAX
#include "apps/knx_iot_virtual.h" // application constants + methods

#ifdef _WIN32
#include <direct.h>
#define GetCurrentDir _getcwd // path of current working directory, WIN
#else // linux,mac specific code
#include <unistd.h>
#define GetCurrentDir getcwd // path of current working directory, LINUX, MAC
#endif

// LSAB definitions
const char application_name[] = "KNX virtual actuator (LSAB)";
const char sn_lower_case[] = "00fa10020900";  // deliberated incorrect serial numbers
const char hostname[] = "knx-00fa10020900";   // default host name (reset uses this default)
const char hw_type[] = "000102030405";        // 12 string chars, MSB = 00
const char dev_model[] = "6800";              // reuse mask version from iot device
const uint32_t mid = 0x00fa;                  // first 4 digits of sn_lower_case

/*

 Below defined datapoints and test parameters for functional block 417 (LSAB) command/control.

  EP's

  - resource path, details see callback handler 'Callback Notes'
  - functional block 417 (LSAB) command/control
  - the datapoints

  URN's

  - the dpa type is in FULL URN notation
  - on a GET {ipv6-unicast}/{point-path}?m it is specified with SHORT URN (see handler)
  - on a GET {ipv6-multicast}/.well-known/core it is specified with SHORT or FULL URN

 */

// define LSAB channel 0..1 + included EPs switch control/status
lsxb_channel_t lsxb[NUM_CHANNELS] = {
  {{
    {false, "/p/lsab/0/soo", "urn:knx:dpa.417.52", ":dpt.switch", "LSAB soo", (0 << 16) + 0},
    {false, "/p/lsab/0/ioo", "urn:knx:dpa.417.51", ":dpt.switch", "LSAB ioo", (0 << 16) + 1}}}, 
  {{
    {false, "/p/lsab/1/soo", "urn:knx:dpa.417.52", ":dpt.switch", "LSAB soo", (1 << 16) + 0},
    {false, "/p/lsab/1/ioo", "urn:knx:dpa.417.51", ":dpt.switch", "LSAB ioo", (1 << 16) + 1}}}
  };

// additional parameters
int_datapoint_t test_parameter = {
  0, "/p/globalTestParameter", "urn:knx:dpa.65500.201", ":dpt.value2Ucount", "Global Test Parameter"};

void register_resources(void)
{
  PRINT("Register LSAB 0...1 channel control/status resource");

  for (int i = 0; i < NUM_CHANNELS; i++)
  {
    oc_resource_t* soo_resource = oc_new_resource(lsxb[i].point[SOO].name, lsxb[i].point[SOO].resource_path, 1, 0);
    oc_resource_t* ioo_resource = oc_new_resource(lsxb[i].point[IOO].name, lsxb[i].point[IOO].resource_path, 1, 0);

    oc_resource_bind_resource_type(soo_resource, lsxb[i].point[SOO].dpa);
    oc_resource_bind_resource_type(ioo_resource, lsxb[i].point[IOO].dpa);

    oc_resource_bind_dpt(soo_resource, lsxb[i].point[SOO].dpt);
    oc_resource_bind_dpt(ioo_resource, lsxb[i].point[IOO].dpt);

    oc_resource_bind_content_type(soo_resource, APPLICATION_CBOR, CONTENT_NONE);
    oc_resource_bind_content_type(ioo_resource, APPLICATION_CBOR, CONTENT_NONE);

    oc_resource_set_function_block_instance(soo_resource, 1);
    oc_resource_set_function_block_instance(ioo_resource, 1);

    oc_resource_set_discoverable(soo_resource, true);
    oc_resource_set_discoverable(ioo_resource, true);

    oc_resource_set_observable(soo_resource, true);
    oc_resource_set_observable(ioo_resource, true);

    // define user data for PUT/GET, needed to distinguish the call source
    void* soo_user_data = (void*)(uintptr_t)lsxb[i].point[SOO].id;
    void* ioo_user_data = (void*)(uintptr_t)lsxb[i].point[IOO].id;

    // LSAB defines
    // soo
    // - GET note that a GET also handles the query metadata request, regardless if it is an 'input'
    // - PUT
    oc_resource_set_request_handler(soo_resource, OC_GET, get_lsxb, soo_user_data, OC_ACL_I | OC_ACL_D, OC_IF_I | OC_IF_D);
    oc_resource_set_request_handler(soo_resource, OC_PUT, put_lsab, soo_user_data, OC_ACL_I | OC_ACL_P, OC_IF_I | OC_IF_P);
    // ioo
    // - GET
    oc_resource_set_request_handler(ioo_resource, OC_GET, get_lsxb, ioo_user_data, OC_ACL_O | OC_ACL_D, OC_IF_O | OC_IF_D);

    oc_add_resource(soo_resource);
    oc_add_resource(ioo_resource);
  }

  PRINT("Register test parameter");
  {
    oc_resource_t* tp0 = oc_new_resource(test_parameter.name, test_parameter.resource_path, 1, 0);

    oc_resource_bind_resource_type(tp0, test_parameter.dpa);

    oc_resource_bind_dpt(tp0, test_parameter.dpt);

    oc_resource_bind_content_type(tp0, APPLICATION_CBOR, CONTENT_NONE);

    oc_resource_set_function_block_instance(tp0, 1);

    oc_resource_set_discoverable(tp0, true);

    oc_resource_set_observable(tp0, true);

    oc_resource_set_request_handler(tp0, OC_GET, get_test_parameter, NULL, OC_ACL_D, OC_IF_D); // r/w, see EP handler
    oc_resource_set_request_handler(tp0, OC_PUT, put_test_parameter, NULL, OC_ACL_P, OC_IF_P); // r/w, see EP handler 

    oc_add_resource(tp0);
  }
}

int app_initialize_stack(void)
{
  // set SN before stack initialization
  app_set_serial_number(sn_lower_case);

  /*
    The final storage folder depends on the build system/ current directory on Linux/ Windows,
    the folder name is defined by the file name + serial number.
    Code below should work both on Linux/Windows.

    For a specific embedded OS usually this functionality needs to be adapted. 
  */

  char storage[400];
  char dir[FILENAME_MAX] = "";
  GetCurrentDir(dir, FILENAME_MAX);
  (void)snprintf(storage, sizeof(storage), "./knx_iot_virtual_lsab_%s", app_get_serial_number());
  OC_INF("Current path is: '%s'", dir);
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

/**
 * @brief signal the event loop, GUI build: wxTimer drives oc_main_poll(),
 * so we don't need to wake up a blocking loop.
 */
void signal_event_loop(void)
{
  //DO NOTHING, wxTimer drives oc_main_poll()
}