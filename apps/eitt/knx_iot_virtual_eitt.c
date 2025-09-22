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
 * KNX virtual EITT, for more details see 'knx_iot_application_template' c-file.
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

// for the mixture of EITT channel definitions 
#define LSSB (1)
#define LSAB (0)

// EITT definitions 
const char application_name[] = "KNX virtual EITT certification application";
const char sn_lower_case[] = "00fa10020800";  // same as eitt test template, deliberated incorrect serial numbers
const char hostname[] = "knx-00fa10020800";   // default host name (reset uses this default)
const char hw_type[] = "Windows";             // 12 string chars, same as eitt test template
const char dev_model[] = "KNX Certification"; // same as eitt test template
const uint32_t mid = 667;                     // same as eitt test template

/*

 Below defined datapoints and test parameters for the EITT test template defaults. 
 
  EP's

  - resource path, details see callback handler 'Callback Notes'
  - functional block 417 (LSAB) and 421 (LSBB) command/control
  - the datapoints are artificial such as dpa 417.61/62
  
  URN's

  - the dpa type is in FULL URN notation
  - on a GET {ipv6-unicast}/{point-path}?m it is specified with SHORT URN (see handler)
  - on a GET {ipv6-multicast}/.well-known/core it is specified with SHORT or FULL URN
 
 */

// LSAB/LSSB channel 0..1 + included EPs switch control/status
lsxb_channel_t lsxb[NUM_CHANNELS] = {
  {{
    {false, "/p/1", "urn:knx:dpa.417.61", ":dpt.switch", (0 << 16) + 0}, 
    {false, "/p/2", "urn:knx:dpa.417.62", ":dpt.switch", (0 << 16) + 1}}},
  {{
    {false, "/p/3", "urn:knx:dpa.421.61", ":dpt.switch", (1 << 16) + 0},
    {false, "/p/4", "urn:knx:dpa.421.62", ":dpt.switch", (1 << 16) + 1}}},
};

// additional parameters
int_datapoint_t test_parameter = {0, "/p/p1", "urn:knx:dpa.65500.201", ":dpt.propDataType", "Global Test Parameter"};

void register_resources(void)
{
  PRINT("Register LSAB/LSSB 0...1 channel control/status resource");
  {
    oc_resource_t* soo_resource_lsab = oc_new_resource(NULL, lsxb[LSAB].point[SOO].resource_path, 1, 0);
    oc_resource_t* ioo_resource_lsab = oc_new_resource(NULL, lsxb[LSAB].point[IOO].resource_path, 1, 0);
    oc_resource_t* soo_resource_lssb = oc_new_resource(NULL, lsxb[LSSB].point[SOO].resource_path, 1, 0);
    oc_resource_t* ioo_resource_lssb = oc_new_resource(NULL, lsxb[LSSB].point[IOO].resource_path, 1, 0);

    oc_resource_bind_resource_type(soo_resource_lsab, lsxb[LSAB].point[SOO].dpa);
    oc_resource_bind_resource_type(ioo_resource_lsab, lsxb[LSAB].point[IOO].dpa);
    oc_resource_bind_resource_type(soo_resource_lssb, lsxb[LSSB].point[SOO].dpa);
    oc_resource_bind_resource_type(ioo_resource_lssb, lsxb[LSSB].point[IOO].dpa);

    oc_resource_bind_dpt(soo_resource_lsab, lsxb[LSAB].point[SOO].dpt);
    oc_resource_bind_dpt(ioo_resource_lsab, lsxb[LSAB].point[IOO].dpt);
    oc_resource_bind_dpt(soo_resource_lssb, lsxb[LSSB].point[SOO].dpt);
    oc_resource_bind_dpt(ioo_resource_lssb, lsxb[LSSB].point[IOO].dpt);

    oc_resource_bind_content_type(soo_resource_lsab, APPLICATION_CBOR, CONTENT_NONE);
    oc_resource_bind_content_type(ioo_resource_lsab, APPLICATION_CBOR, CONTENT_NONE);
    oc_resource_bind_content_type(soo_resource_lssb, APPLICATION_CBOR, CONTENT_NONE);
    oc_resource_bind_content_type(ioo_resource_lssb, APPLICATION_CBOR, CONTENT_NONE);

    oc_resource_set_function_block_instance(soo_resource_lsab, 1);
    oc_resource_set_function_block_instance(ioo_resource_lsab, 1);
    oc_resource_set_function_block_instance(soo_resource_lssb, 1);
    oc_resource_set_function_block_instance(ioo_resource_lssb, 1);

    oc_resource_set_discoverable(soo_resource_lsab, true);
    oc_resource_set_discoverable(ioo_resource_lsab, true);
    oc_resource_set_discoverable(soo_resource_lssb, true);
    oc_resource_set_discoverable(ioo_resource_lssb, true);

    oc_resource_set_observable(soo_resource_lsab, true);
    oc_resource_set_observable(ioo_resource_lsab, true);
    oc_resource_set_observable(soo_resource_lssb, true);
    oc_resource_set_observable(ioo_resource_lssb, true);

    // define user data for PUT/GET, needed to distinguish the call source
    void* soo_user_data_lsab = (void*)(uintptr_t)lsxb[LSAB].point[SOO].id;
    void* ioo_user_data_lsab = (void*)(uintptr_t)lsxb[LSAB].point[IOO].id;
    void* soo_user_data_lssb = (void*)(uintptr_t)lsxb[LSSB].point[SOO].id;
    void* ioo_user_data_lssb = (void*)(uintptr_t)lsxb[LSSB].point[IOO].id;

    // LSAB defines
    // soo
    // - GET note that a GET also handles the query metadata request, regardless if it is an 'input'
    // - PUT
    // ioo
    // - GET
    oc_resource_set_request_handler(soo_resource_lsab, OC_GET, get_lsxb, soo_user_data_lsab, OC_ACL_I | OC_ACL_D, OC_IF_I | OC_IF_D);
    oc_resource_set_request_handler(soo_resource_lsab, OC_PUT, put_lsab, soo_user_data_lsab, OC_ACL_I | OC_ACL_P, OC_IF_I | OC_IF_P);
    oc_resource_set_request_handler(ioo_resource_lsab, OC_GET, get_lsxb, ioo_user_data_lsab, OC_ACL_O | OC_ACL_D, OC_IF_O | OC_IF_D);

    // LSSB defines
    // soo
    // - GET
    oc_resource_set_request_handler(soo_resource_lssb, OC_GET, get_lsxb, soo_user_data_lssb, OC_ACL_O | OC_ACL_D, OC_IF_O | OC_IF_D);
    // ioo
    // - GET note that a GET also handles the query metadata request, regardless if it is an 'input'
    // - PUT 
    oc_resource_set_request_handler(ioo_resource_lssb, OC_GET, get_lsxb, ioo_user_data_lssb, OC_ACL_I | OC_ACL_D, OC_IF_I | OC_IF_D);
    oc_resource_set_request_handler(ioo_resource_lssb, OC_PUT, put_lssb, ioo_user_data_lssb, OC_ACL_I | OC_ACL_P, OC_IF_I | OC_IF_P);

    oc_add_resource(soo_resource_lsab);
    oc_add_resource(ioo_resource_lsab);
    oc_add_resource(soo_resource_lssb);
    oc_add_resource(ioo_resource_lssb);
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

    // parameter defines GET and PUT 
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
  */

  char storage[400];
  char dir[FILENAME_MAX] = "";
  GetCurrentDir(dir, FILENAME_MAX);
  (void)snprintf(storage, sizeof(storage), "%s/knx_iot_virtual_eitt_%s", dir, app_get_serial_number());
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