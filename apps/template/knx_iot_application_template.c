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

/**
 * @file
 *
 * KNX application template
 * ========================
 *
 * This (NON GUI) application demonstrates the general stack usage, in contrast to the EITT/ETS GUI demo applications.
 * Hence, this template includes the int main(void) function. The - by this template - supported OS is windows and linux,
 * for a specific embedded platform OS the code may need to be adapted.
 *
 * - app_init, initializes the stack values
 * - register_resources, function that registers all endpoints, e.g. sets the GET/.../DELETE handlers for each end point
 * - main, starts the stack, with the registered resources
 * - callback handlers for the implemented methods, see callback handler 'Callback Notes'
 * - oc_main_shutdown, exit application and stack
 *
 */

#include "oc_api.h"
#include "port/oc_storage.h"
#include <stdio.h> // defines FILENAME_MAX
#include "apps/knx_iot_virtual.h" // application constants + methods
#include <signal.h>
#include <stdlib.h>
#include "port/oc_clock.h"


#ifdef __linux__
#include <pthread.h>
static pthread_mutex_t mutex;
static pthread_cond_t event_is_pending;
static struct timespec ts;
#endif

#ifdef _WIN32
#include <windows.h>
static CONDITION_VARIABLE event_is_pending;
static CRITICAL_SECTION critical_section;
#include <direct.h>
#define GetCurrentDir _getcwd // path of current working directory, windows
#else 
#include <unistd.h>
#define GetCurrentDir getcwd // path of current working directory, LINUX, MAC
#endif

// application template definitions
const char application_name[] = "KNX virtual template application";
const char sn_lower_case[] = "00fa10020600"; // deliberated incorrect serial numbers
const char hostname[] = "knx-00fa10020600"; // default host name (reset uses this default)
const char hw_type[] = "000102030405"; // 12 string chars, MSB = 00
const char dev_model[] = "6800"; // reuse mask version from iot device
const uint32_t mid = 0x00fa; // first 4 digits of sn_lower_case

/*

 Below defined datapoints and test parameters are demo data.

  EP's

  - resource path, details see callback handler 'Callback Notes'
  - functional block demo number
  - the datapoints

  URN's

  - the dpa type is in FULL URN notation
  - on a GET {ipv6-unicast}/{point-path}?m it is specified with SHORT URN (see handler)
  - on a GET {ipv6-multicast}/.well-known/core it is specified with SHORT or FULL URN

 */

// define demo channel 0..1 + included EPs to allow a build
lsxb_channel_t lsxb[NUM_CHANNELS] = {
  {{
    {false, "/p/lssb/0/demo0", "urn:knx:dpa.x.y", ":dpt.na", "demo 0", (0 << 16) + 0},
    {false, "/p/lssb/0/demo1", "urn:knx:dpa.x.y", ":dpt.na", "demo 1", (0 << 16) + 1}}},
  {{
    {false, "/p/lssb/1/demo0", "urn:knx:dpa.x.y", ":dpt.na", "demo 0", (1 << 16) + 0},
    {false, "/p/lssb/1/demo1", "urn:knx:dpa.x.y", ":dpt.na", "demo 1", (1 << 16) + 1}}}};

// additional parameters
int_datapoint_t test_parameter = {0, "/p/demotest", "urn:knx:dpa.x.y", ":dpt.na", "Demo Test Parameter"};

// global variables

volatile int quit = 0; // stop variable, used by handle_signal

void register_resources(void)
{
  PRINT("Register my datapoints");

  // register here your own resources, similar as in the examples of LSAB/LSSB 

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
  (void)sprintf(storage, "./knx_iot_virtual_lssb_%s", app_get_serial_number());
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

#ifdef _WIN32
/**
 * @brief signal the event loop (windows version)
 * wakes up the main function to handle the next callback
 */
void signal_event_loop(void)
{
  WakeConditionVariable(&event_is_pending);
}
#endif

#ifdef __linux__
/**
 * @brief signal the event loop (Linux)
 * wakes up the main function to handle the next callback
 */
void signal_event_loop(void)
{
  pthread_mutex_lock(&mutex);
  pthread_cond_signal(&event_is_pending);
  pthread_mutex_unlock(&mutex);
}
#endif

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
 * @brief Main application that initializes the global variables, registers and starts the handler
 *        and handles (in a loop) the next event. Finally also shut down the stack. 
 */
int main(void)
{
  oc_clock_time_t next_event;

#ifdef _WIN32

  // init , but not used in main actively
  InitializeCriticalSection(&critical_section);
  // init
  InitializeConditionVariable(&event_is_pending); 
  // install Ctrl-C handler
  (void)signal(SIGINT, handle_signal);

#endif

#ifdef __linux__
  
  struct sigaction sa;
  sigfillset(&sa.sa_mask);
  sa.sa_flags = 0;
  sa.sa_handler = handle_signal;
  // install Ctrl-C handler
  sigaction(SIGINT, &sa, NULL);

#endif

  // before this call devices and resources are not existing, return code issued by .init handler
  const int code = app_initialize_stack();
  if (code < 0)
  {
    PRINT("stack initialization failed with %d, exiting.", code);
    // may need to be adapted according the underlying OS
    exit(-1);
  }

  // refresh device IP addresses
  oc_connectivity_get_endpoints(0);

  PRINT("Server '%s' is now running, waiting on incoming connections...", application_name);

#ifdef _WIN32

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

  while (quit != 1) // check on Ctrl-C
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

