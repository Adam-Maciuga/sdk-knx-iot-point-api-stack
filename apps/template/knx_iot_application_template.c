/* 
 * Copyright (c) 2024-2026 KNX Association
 *            
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 *
 * KNX application template 
 * ========================
 *
 * This (NON GUI) application demonstrates the general stack usage, in contrast to the EITT/ETS/EMS GUI (C++) demo applications.
 * Hence, this template includes the int main(void) function. The - by this template - supported OS is windows and linux,
 * for a specific embedded platform OS the code may need to be adapted.
 *
 * - app_init, initializes the stack values
 * - register_resources, function that registers all endpoints, e.g. sets the GET/.../DELETE handlers for each end point
 * - main, starts the stack, with the registered resources
 * - callback handlers for the implemented methods, see callback handler 'Callback Notes'
 * - oc_main_shutdown, exit application and stack
 *
 * Note:
 * - the template is NOT defined as a CMake build target in CMakeLists.txt, if needed it must be added.
 * - the template may not be able to compile/build out of the box without adapting some methods. 
 *
 */

#include "oc_api.h"
#include <stdio.h> // defines FILENAME_MAX
#include "apps/knx_iot_virtual.h" // application constants + methods (also 'GetCurrentDir') 
#include <signal.h>
#include <stdlib.h>
#include "port/oc_clock.h"

#ifdef (_WIN32)
#include <windows.h>
static CONDITION_VARIABLE event_is_pending;
static CRITICAL_SECTION critical_section;
#elif defined(__linux__)
#include <pthread.h>
static pthread_mutex_t mutex;
static pthread_cond_t event_is_pending;
static struct timespec ts;
#endif

// application template definitions
const char application_name[] KNX_TOOL_WEAK = "KNX virtual template application";
const char sn_lower_case[] KNX_TOOL_WEAK = "00fa10020600";  // deliberated incorrect serial numbers
const char hw_type[] KNX_TOOL_WEAK = "000102030405";        // 12 string chars, MSB = 00
const char dev_model[] KNX_TOOL_WEAK = "6800";              // reuse mask version from iot device
const uint32_t mid KNX_TOOL_WEAK = 0x00fa;                  // manufacturer id, here KNXA

// global variables

volatile int quit = 0; // stop variable, used by handle_signal

void knx_iot_register_resources(void)
{
  PRINT("Register my datapoints");

  // register here your own resources, similar as in the examples of LSAB/LSSB 

}

#ifdef _WIN32
/**
 * @brief signal the event loop (windows version)
 * wakes up the main function to handle the next callback
 */
void knx_iot_signal_event_loop(void)
{
  WakeConditionVariable(&event_is_pending);
}
#elif defined(__linux__)
/**
 * @brief signal the event loop (Linux)
 * wakes up the main function to handle the next callback
 */
void knx_iot_signal_event_loop(void)
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
  knx_iot_signal_event_loop();
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
#elif defined(__linux__)
  struct sigaction sa;
  sigfillset(&sa.sa_mask);
  sa.sa_flags = 0;
  sa.sa_handler = handle_signal;
  // install Ctrl-C handler
  sigaction(SIGINT, &sa, NULL);
#endif

  // before this call devices and resources are not existing, return code issued by .init handler
  const int code = knx_iot_initialize_stack("my_device_storage_folder");
  if (code < 0)
  {
    PRINT("stack initialization failed with %d, exiting.", code);
    // may need to be adapted according the underlying OS
    exit(-1);
  }

  // refresh device IP addresses
  oc_connectivity_get_endpoints();

#ifdef _WIN32
  // check on Ctrl-C
  while (quit != 1)
  {
    // loops over all events
    next_event = oc_main_poll();

    if (next_event == 0)
    { 
      // no event (timer) is pending, all done
      SleepConditionVariableCS(&event_is_pending, &critical_section, INFINITE);
    }
    else
    {
      // time in ticks (tick = ms)
      const oc_clock_time_t now = oc_clock_time();
      if (now < next_event)
      {
        // next event lays in the future, sleep until next
        // pending event timer is reached (in ticks/ms)
        SleepConditionVariableCS(&event_is_pending, &critical_section, (next_event - now) * (1000 / OC_CLOCK_SECOND));
      }
      // next event is now ...
    }
  }
#elif defined(__linux__)
  // check on Ctrl-C
  while (quit != 1)
  {
    // loops over all events
    next_event = oc_main_poll();
    pthread_mutex_lock(&mutex);
    if (next_event == 0)
    {
      // no event (timer) is pending, all done
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

