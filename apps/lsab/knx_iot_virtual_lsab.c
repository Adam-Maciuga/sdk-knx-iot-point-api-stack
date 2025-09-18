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
 * KNX virtual actuator
 *
 ## Application Design
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
#include "oc_rep.h"
#include "api/oc_knx_dev.h"
#include "oc_api.h"
#include "oc_core_res.h"
#include "oc_helpers.h"
#include "port/oc_clock.h"
#include "port/oc_storage.h"
#include <signal.h> // test purpose only; commandline reset
#include <stdio.h> // defines FILENAME_MAX
#include <stdlib.h>
#include "apps/knx_iot_virtual.h" // application constants + methods

#ifdef __linux__
#include <pthread.h>
#ifndef NO_MAIN
static pthread_mutex_t mutex;
static pthread_cond_t event_is_pending;
static struct timespec ts;
#endif
#endif

#ifdef WIN32
#include <windows.h>
static CONDITION_VARIABLE event_is_pending;
static CRITICAL_SECTION critical_section;
#include <direct.h>
#define GetCurrentDir _getcwd // path of current working directory, windows
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

// global variables

volatile int quit = 0; // stop variable, used by handle_signal
bool g_reset = false; // reset variable, set by commandline arguments

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

/**
 * @brief
 * register all the data point resources to the stack this function registers
 * all data point level resources:
 * each resource path is bind to a specific function for the supported methods:
 *   - GET (called from /p and /k)
 *   - PUT (called from /p and /k)
 *   - POST/DELETE/FETCH  (not supported from stack for the application)
 *
 * each resource is:
 *   - secure
 *   - observable
 *   - discoverable through well-known/core
 *   - used interfaces as: dpa.xxx.yyy
 *      - xxx : function block number
 *      - yyy : data point function number
 *
 * @note
 *	periodic observable to be used when one wants to send an event per time
    slice (period is 1 second) with oc_resource_set_periodic_observable(res_InfoOnOff_?, 1);
    Set observable events are send when oc_notify_observers(oc_resource_t *resource) is called.
    This function must be called when the value changes, preferable on an interrupt when
    something is read from the hardware.
 */
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
    void* soo_user_data = lsxb[i].point[SOO].id;
    void* ioo_user_data = lsxb[i].point[IOO].id;

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
  */

  char storage[400];
  char dir[FILENAME_MAX] = "";
  GetCurrentDir(dir, FILENAME_MAX);
  (void)sprintf(storage, "./knx_iot_virtual_lsab_%s", app_get_serial_number());
  OC_INF("Current path is: '%s'", dir);
  oc_storage_config(storage);

  // initialize the 'application' runtime variables
  initialize_variables();

  // set the stack handler callbacks
  static oc_handler_t handler = {.init = app_init, // called always
                                 .signal_event_loop = signal_event_loop, // called always
                                 .register_resources = register_resources, // called for a server (one time)
                                 .requests_entry = NULL}; // called for a client

  // set the application handler callbacks
  oc_set_hostname_cb(hostname_cb, NULL);
  oc_set_factory_presets_cb(factory_presets_cb, NULL);
  oc_set_swu_cb(swu_cb, NULL);

  // start the stack, calls directly also the .init handler from above
  return oc_main_init(&handler);
}

#ifdef WIN32
/**
 * @brief signal the event loop (windows version)
 * wakes up the main function to handle the next callback
 */
void signal_event_loop(void)
{

#ifndef NO_MAIN
  WakeConditionVariable(&event_is_pending);
#endif
}
#endif

#ifdef __linux__
/**
 * @brief signal the event loop (Linux)
 * wakes up the main function to handle the next callback
 */
void signal_event_loop(void)
{
#ifndef NO_MAIN
  pthread_mutex_lock(&mutex);
  pthread_cond_signal(&event_is_pending);
  pthread_mutex_unlock(&mutex);
#endif /* NO_MAIN */
}
#endif

/*
 used to run as standalone command line (with main) or embed it
 in a parent code (such as the corresponding GUI applications) 
*/
#ifndef NO_MAIN

/**
 * @brief handle Ctrl-C
 * @param signal the captured signal
 */
static void handle_signal(const int signal)
{
  (void)signal;
  signal_event_loop();
  quit = 1;
}

/**
 * @brief main application.
 * initializes the global variables
 * registers and starts the handler
 * handles (in a loop) the next event.
 * shuts down the stack
 */
int main(const int argc, char* argv[])
{
  oc_clock_time_t next_event;

#ifdef KNX_GUI
  WinMain(GetModuleHandle(NULL), NULL, (LPSTR)GetCommandLine(), SW_SHOWNORMAL);
#endif

#ifdef WIN32
  InitializeCriticalSection(&critical_section); // init , but not used in main actively
  InitializeConditionVariable(&event_is_pending); // init
  (void)signal(SIGINT, handle_signal); // install Ctrl-C handler
#endif
#ifdef __linux__
  /* Linux specific */
  struct sigaction sa;
  sigfillset(&sa.sa_mask);
  sa.sa_flags = 0;
  sa.sa_handler = handle_signal;
  /* install Ctrl-C */
  sigaction(SIGINT, &sa, NULL);
#endif

  for (int i = 0; i < argc; i++)
  {
    PRINT("argv[%d] = %s", i, argv[i]);
  }

  // arv[0]= file; reset/help uses one argument
  if (argc > 1)
  {
    if (strcmp(argv[1], "-reset") == 0)
    {
      g_reset = true; // ... helper, device is not initiated at this point
    }
    if (strcmp(argv[1], "-help") == 0)
    {
      PRINTF("usage: no arguments starts the server;                \
                -help shows this message;                           \
                -reset does an full device reset (erase code 2)     \
                -s <serial number> sets the device serial number");
      exit(0);
    }
  }
  // arv[0]= file; sn uses two arguments
  // set SN before stack initialization
  if (argc > 2)
  {
    if (strcmp(argv[1], "-s") == 0)
    {
      PRINT("use command line serial number %s", argv[2]);
      app_set_serial_number(argv[2]);
    }
  }

  PRINT("KNX-IOT server name : \"%s\"", application_name);

  // before this call devices and resources are not existing, return code issued by .init handler
  const int code = app_initialize_stack();
  if (code < 0)
  {
    PRINT("stack initialization failed with %d, exiting.", code);
    exit(-1);
  }

  // ... now device is initiated
  if (g_reset)
  {
    PRINT("execute command line reset for device '0' with erase code 2 ...");
    oc_knx_device_storage_reset(0, RESET_TO_DEFAULT_STATE);
  }

  const oc_device_info_t* device = oc_core_get_device_info(0);

  PRINT("serial number: %s", oc_string(device->serialnumber));
  PRINT("host name: %s", oc_string(device->hostname));

  // used to refresh (and print) IP addresses
  oc_connectivity_get_endpoints(0);

  PRINT("Server '%s' is now running, waiting on incoming connections...", application_name);

#ifdef WIN32
  while (quit != 1) // check on Ctrl-C
  {
    // loops over all events
    next_event = oc_main_poll();

    if (next_event == 0)
    { // no event (timer) is pending, all done
      SleepConditionVariableCS(&event_is_pending, &critical_section, INFINITE);
    }
    else
    {
      // time in ticks (tick = ms)
      const oc_clock_time_t now = oc_clock_time();
      if (now < next_event)
      { // next event lays in the future, sleep until next
        // pending event timer is reached (in ticks/ms)
        SleepConditionVariableCS(&event_is_pending, &critical_section, (next_event - now) * (1000 / OC_CLOCK_SECOND));
      }
      // next event is now ...
    }
  }
#endif

#ifdef __linux__
  /* Linux specific loop */
  while (quit != 1)
  {
    next_event = oc_main_poll();
    pthread_mutex_lock(&mutex);
    if (next_event == 0)
    {
      pthread_cond_wait(&event_is_pending, &mutex);
    }
    else
    {
      ts.tv_sec = (next_event / OC_CLOCK_SECOND);
      ts.tv_nsec = (next_event % OC_CLOCK_SECOND) * 1.e09 / OC_CLOCK_SECOND;
      pthread_cond_timedwait(&event_is_pending, &mutex, &ts);
    }
    pthread_mutex_unlock(&mutex);
  }
#endif

  // shut down the stack
  oc_main_shutdown();
  return 0;
}
#endif
