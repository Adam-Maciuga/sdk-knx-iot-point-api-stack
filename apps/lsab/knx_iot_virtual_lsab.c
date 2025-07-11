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

 * ## Application Design
 *
 * support functions:
 *
 * - app_init
 *   initializes the stack values.
 * - register_resources
 *   function that registers all endpoints,
 *   e.g. sets the GET/PUT/POST/DELETE
 *      handlers for each end point
 *
 * - main
 *   starts the stack, with the registered resources.
 *   can be compiled out with NO_MAIN
 *
 *  handlers for the implemented methods (get/put):
 *   - get_[path]
 *     function that is being called when a GET is called on [path]
 *     set the global variables in the output
 *   - put_[path]
 *     function that is being called when a PUT is called on [path]
 *     if input data is of the correct type
 *       updates the global variables
 *
 * ## stack specific defines
 * - __linux__
 *   build for Linux
 * - WIN32
 *   build for Windows
 * - OC_OSCORE
 *   oscore is enabled as compile flag
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

#ifdef OC_SPAKE
#include "security/oc_spake2plus.h" // security enrollment by password
#endif

#include <signal.h> // test purpose only; commandline reset
#include <stdio.h> // defines FILENAME_MAX
#include <stdlib.h>
#include "apps/knx_iot_virtual.h" // application constants + methods
#include "oc_knx_client.h"

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

// global variables

volatile int quit = 0; // stop variable, used by handle_signal
bool g_reset = false; // reset variable, set by commandline arguments
char g_serial_number[] = SN_LOWER_CASE_LSAB; // startup SN, maybe overwritten by CL option

/*

 Below defined datapoints and test parameters for functional block 417 (LSAB) command/control.

  EP's

  - href, this application example uses a leading '/p', for details see callback handler 'Callback Notes'
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
    {false, "/p/lsab/0/soo", "urn:knx:dpa.417.52", ":dpt.switch", "LSAB soo", "0000"},   // 0 << 8 + 0 
    {false, "/p/lsab/0/ioo", "urn:knx:dpa.417.51", ":dpt.switch", "LSAB ioo", "0001"}}}, // 0 << 8 + 1  
  {{
    {false, "/p/lsab/1/soo", "urn:knx:dpa.417.52", ":dpt.switch", "LSAB soo", "0100"},   // 1 << 8 + 0 
    {false, "/p/lsab/1/ioo", "urn:knx:dpa.417.51", ":dpt.switch", "LSAB ioo", "0101"}}}  // 1 << 8 + 1 
  };

// additional parameters
int_datapoint_t test_parameter = {
  0, "/p/globalTestParameter", "urn:knx:dpa.65500.201", ":dpt.value2Ucount", "Global Test Parameter"};

// need to define prototype, used by an init method
void signal_event_loop(void);

/**
 * @brief s-mode response callback
 * will be called when a response is received on an s-mode read request
 *
 * @param url the url
 * @param rep the full response
 * @param rep_value the parsed value of the response
 */
void oc_s_mode_response_cb(char* url, oc_rep_t* rep, oc_rep_t* rep_value)
{
  (void)rep;
  (void)rep_value;

  PRINT("oc_s_mode_response_cb %s", url);
}

/**
 * @brief function to set up the device.
 *
 */
int app_init(void)
{
  // set provider, no callback/no data
  int ret = oc_init_platform("KNX Association", NULL, NULL);

  // set the application name, version, base url, device serial number
  // init also the device resources such as /dev, /.well-known/core, ...
  ret |= oc_add_device(APPLICATION_NAME_LSAB, "1.0.0", "//", g_serial_number, NULL, NULL);

  // set the hardware version 0.0.1, value used from EITT for testing
  oc_core_set_device_hwv(0, 0, 0, 1);

  // set the hardware version 0.0.1, value used from EITT for testing
  oc_core_set_device_fwv(0, 0, 0, 1);

  // set manufacturer id, value used from EITT for testing
  oc_core_set_device_mid(0, MID);

  // set the hardware type -> 12 chars, value used from EITT for testing
  oc_core_set_device_hwt(0, HW_TYPE_ETS6);

  // set device model, value used from EITT for testing
  oc_core_set_device_model(0, DEV_MODEL_ETS6);

  // set host name, value used from EITT for testing
  oc_core_set_device_hostname(0, HOST_NAME_LSAB);

  oc_set_s_mode_response_cb(oc_s_mode_response_cb);

#ifdef OC_SPAKE

  // if current (negotiated) pwd is not set (e.g. on a handover), use application definition
  if (strlen(oc_spake_get_password()) == 0)
    oc_spake_set_password(PASSWORD);

  // make lower to upper case
  char sn_upper[] = SN_LOWER_CASE_LSAB;
  app_str_to_upper(sn_upper);

  OC_DBG_SPAKE("=== QR Code: KNX:S:%s;P:%s ===", sn_upper, oc_spake_get_password());

#endif

  return ret;
}

/**
 * @brief returns the password, used from external application hence defined as
 * separate method.
 */
char* app_get_password(void) { return PASSWORD; }

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
    oc_resource_t* soo_resource = oc_new_resource(lsxb[i].point[SOO].desc, lsxb[i].point[SOO].resource_path, 1, 0);
    oc_resource_t* ioo_resource = oc_new_resource(lsxb[i].point[IOO].desc, lsxb[i].point[IOO].resource_path, 1, 0);

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
    oc_resource_t* tp0 = oc_new_resource(test_parameter.desc, test_parameter.resource_path, 1, 0);

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

/**
 * @brief
 * Application factory preset callback handler for the device

 * @param device_index the device identifier of the list of devices
 * @param data the supplied data.
 */
void factory_presets_cb(size_t device_index, void* data)
{
  (void)device_index;
  (void)data;

  PRINT("factory preset callback called :");
}

/**
 * @brief
 * Application host name callback handler for the device
 *
 * @param device_index the device identifier of the list of devices
 * @param host_name the host name of the device to be maintained (check/set,
 * print, ...)
 * @param data the supplied data.
 */
void hostname_cb(const size_t device_index, const oc_string_t host_name, void* data)
{
  (void)device_index;
  (void)data;

  PRINT("host name callback called with host name: %s", oc_string(host_name));

  /*
   * The application callback needs to handle a changed host name such as to
   * announce it to a border router or local daemon.
   */
}

/**
 * @brief initializes the global variables
 * for the resources
 * for the parameters
 */
void initialize_variables(void)
{
  /* initialize global variables for resources */
  /* if wanted to be read them from persistent storage */
}

int app_set_serial_number(const char* serial_number)
{
  // don't copy more than size of SN
  return strncpy(g_serial_number, serial_number, sizeof(g_serial_number)) != NULL ? 0 : -1;
}

int app_initialize_stack(void)
{
/*
 The storage folder depends on the build system the folder is created
 in the makefile, with $target as name with _cred as post fix.
*/
#ifdef WIN32

  char storage[400];
  char dir[FILENAME_MAX] = "";
  GetCurrentDir(dir, FILENAME_MAX);
  (void)sprintf(storage, "./knx_iot_virtual_lsab_%s", g_serial_number);
  PRINT("Current path is: '%s'", dir);
  oc_storage_config(storage);

#else

  char storage[400];
  char dir[FILENAME_MAX] = "";
  GetCurrentDir(dir, FILENAME_MAX);
  oc_storage_config("./knx_iot_virtual_lsab");

#endif

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
  WinMain(GetModuleHandle(NULL), NULL, GetCommandLine(), SW_SHOWNORMAL);
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
      PRINTF("usage: no arguments starts the server;              \
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

  PRINT("KNX-IOT server name : \"%s\"", APPLICATION_NAME_LSAB);

  // ... before this call devices and resources are not existing, return code
  // issued by .init handler
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
    oc_knx_device_storage_reset(0, 2);
  }

  const oc_device_info_t* device = oc_core_get_device_info(0);

  // may produce a warning if OC_OSCORE is not specified ...
  PRINT("OSCORE - %s", OC_OSCORE ? "Enabled" : "Disabled");
  PRINT("serial number: %s", oc_string(device->serialnumber));
  PRINT("host name: %s", oc_string(device->hostname));

  // used to refresh (and print) IP addresses
  oc_connectivity_get_endpoints(0);

  PRINT("Server '%s' is now running, waiting on incoming connections...", APPLICATION_NAME_LSAB);

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
