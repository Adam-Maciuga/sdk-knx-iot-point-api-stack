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

#include "knx_iot_virtual_EMS.h"

#include "api/oc_knx_fp.h"
#include "api/oc_knx_sec.h"

#include "api/oc_knx_fp.c"
#include "api/oc_knx_sec.c"

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

const char application_name[] = "Charger";
const char sn_lower_case[] = "00fa10020d00";  // deliberated incorrect serial numbers
const char hostname[] = "knx-00fa10020d00";   // default host name (reset uses this default)
const char hw_type[] = "000102030405";        // 12 string chars, MSB = 00
const char dev_model[] = "6800";              // reuse mask version from iot device
const uint32_t mid = 0x00fa;                  // first 4 digits of sn_lower_case

// global variables
volatile int quit = 0; // stop variable, used by handle_signal
bool g_reset = false; // reset variable, set by commandline arguments

int datapoint_charger = 0;

datapoint_t Charger_datapoint[1] = {
  {"/p/charger", "urn:knx:dpa.666.03", ":dpt.value_power", "CHARGER", "0"} // DPT: 14.056
};

lsxb_channel_t lsxb[NUM_CHANNELS] = {{{{false, "/p/lsab/0/soo", "urn:knx:dpa.417.52", ":dpt.switch", (0 << 16) + 0},
                                       {false, "/p/lsab/0/ioo", "urn:knx:dpa.417.51", ":dpt.switch", (0 << 16) + 1}}},
                                     {{{false, "/p/lsab/1/soo", "urn:knx:dpa.417.52", ":dpt.switch", (1 << 16) + 0},
                                       {false, "/p/lsab/1/ioo", "urn:knx:dpa.417.51", ":dpt.switch", (1 << 16) + 1}}}};

// additional parameters
int_datapoint_t test_parameter = {
  0, "/p/globalTestParameter", "urn:knx:dpa.65500.201", ":dpt.value2Ucount", "Global Test Parameter"};

void register_resources(void)
{
  oc_resource_t* CHARGER_resource = oc_new_resource(Charger_datapoint[0].resource_path, 1);

  oc_resource_bind_resource_type(CHARGER_resource, Charger_datapoint[0].dpa);
  oc_resource_bind_dpt(CHARGER_resource, Charger_datapoint[0].dpt);
  oc_resource_bind_content_type(CHARGER_resource, APPLICATION_CBOR, CONTENT_NONE);
  oc_resource_set_function_block_instance(CHARGER_resource, 1);
  oc_resource_set_discoverable(CHARGER_resource, true);
  oc_resource_set_observable(CHARGER_resource, true);
  void* CHARGER_user_data = Charger_datapoint[0].id;

  oc_resource_set_request_handler(CHARGER_resource, OC_PUT, Charger_put_charger, CHARGER_user_data, OC_ACL_I, OC_IF_I);    

  oc_add_resource(CHARGER_resource); 

  PRINT("Register test parameter");
  {
    oc_resource_t* tp0 = oc_new_resource(test_parameter.resource_path, 1);

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
  //app_set_serial_number(sn_lower_case);

  /*
    The final storage folder depends on the build system/ current directory on Linux/ Windows,
    the folder name is defined by the file name + serial number.
    Code below should work both on Linux/Windows.
  */

  char storage[400];
  char dir[FILENAME_MAX] = "";
  GetCurrentDir(dir, FILENAME_MAX);
  //(void)sprintf(storage, "./knx_iot_virtual_PV_%s", app_get_serial_number());
  (void)snprintf(storage, sizeof(storage), "%s/knx_iot_virtual_Charger_%s", dir, sn_lower_case);
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

int Charger_init_tables()
{
  int ia = 0x2004;
  uint64_t iid = 0x7dff7f6c9a;
  int fid = 0xfa;
  uint32_t grpid = 0xc285fba0;

  oc_core_set_and_store_device_ia(ia);
  oc_core_set_and_store_device_iid(iid);
  oc_core_set_and_store_device_fid(fid);
  
  oc_init_tables();

  int entry;
  int ga_array_size;
  uint32_t* ga_array;
  
  entry = 0;
  
  ga_array_size = 1;
  ga_array = malloc(ga_array_size * sizeof(uint32_t));
  if (ga_array != NULL)
  {
    ga_array[0] = 0x0002;
  }

  // object table
  g_got[entry].id = 0;
  oc_string_t href_charger;
  oc_new_string(&href_charger, "/p/charger", strlen("/p/charger"));
  g_got[entry].href = href_charger;
  g_got[entry].cflags = OC_CFLAG_WRITE;
  g_got[entry].ga_len = ga_array_size;
  g_got[entry].ga = ga_array;  

  // rcp table
  g_grt[entry].id = 0;
  g_grt[entry].grpid = grpid;
  g_grt[entry].ga_len = ga_array_size;
  g_grt[entry].ga = ga_array;

  // pub table
  g_gpt[entry].id = 0;
  g_gpt[entry].grpid = grpid;
  g_gpt[entry].ga_len = ga_array_size;
  g_gpt[entry].ga = ga_array;

  // auth table
  oc_new_string(&g_at_entries[entry].id, "0/0/2", strlen("0/0/2"));
  g_at_entries[entry].profile = OC_PROFILE_COAP_OSCORE;
  g_at_entries[entry].scope = OC_ACL_GA;
  g_at_entries[entry].ga_len = ga_array_size;
  g_at_entries[entry].ga = ga_array;
  BYTE byteArray12[2] = {0x00, 0x02};
  oc_new_byte_string(&g_at_entries[entry].osc_id, (char*)byteArray12, 2);
  BYTE byteArray1f[16] = {0xE7, 0xCF, 0x5D, 0xDA, 0x0D, 0x26, 0xB5, 0xD4, 0x6C, 0xA9, 0xDB, 0xFB, 0x0A, 0xF0, 0x2E, 0x96};
  oc_new_byte_string(&g_at_entries[entry].osc_ms, (char*)byteArray1f, 16);
  BYTE byteArray16[6] = {0x00, 0x00, 0x00, 0x00, 0x09, 0x00};
  oc_new_byte_string(&g_at_entries[entry].osc_contextid, (char*)byteArray16, 6);

  oc_load_group_object_table();
  oc_load_object_table();
  oc_load_at_table(); 

  subscribe_group_to_multicast_with_port(grpid, iid, 2, COAP_DEFAULT_PORT);

  return 0;
}

int Charger_init_tables_QR(char* sn_s)
{
  unsigned char sn_b[6];

  for (size_t i = 0; i < 6; i++)
    sn_b[i] = 0x00;

  int len = strlen(sn_s);

  if (len % 2 == 0)
  {
    if (len / 2 == 6)
    {
      for (size_t i = 0; i < 6; i++)
      {
        char buf[3] = {sn_s[2 * i], sn_s[2 * i + 1], '\0'}; // 2 hex chars
        sn_b[i] = (unsigned char)strtol(buf, NULL, 16);
      }
    }
    else
    {
      return -1;
    }
  }
  else
  {
    return -1;
  }

  char A = (sn_b[1] & 0xf0) >> 4;
  char L = sn_b[1] & 0x0f;

  int ia = 0x2003;
  uint64_t iid = 0x7dff7f6c9a;
  int fid = 0xfa;
  uint32_t grpid = 0xc285fba0;

  ia = (sn_b[1] * 0x100) + 0x03;

  iid = sn_b[1];
  iid <<= 8;
  iid += sn_b[2];
  iid <<= 8;
  iid += sn_b[3];
  iid <<= 8;
  iid += sn_b[4];
  iid <<= 8;
  iid += sn_b[5];

  fid = sn_b[1];

  grpid = L;
  grpid <<= 8;
  grpid += sn_b[4];
  grpid <<= 8;
  grpid += sn_b[3];
  grpid <<= 8;
  grpid += A;

  int ga0 = 0x0100 + L;
  int ga1 = 0x0200 + L;

  oc_core_set_and_store_device_ia(ia);
  oc_core_set_and_store_device_iid(iid);
  oc_core_set_and_store_device_fid(fid);

  oc_init_tables();

  int entry;
  int ga_array_size;
  uint32_t* ga_array;

  entry = 0;

  ga_array_size = 1;
  ga_array = malloc(ga_array_size * sizeof(uint32_t));
  if (ga_array != NULL)
  {
    ga_array[0] = ga1;
  }

  // object table
  g_got[entry].id = 0;
  oc_string_t href_charger;
  oc_new_string(&href_charger, "/p/charger", strlen("/p/charger"));
  g_got[entry].href = href_charger;
  g_got[entry].cflags = OC_CFLAG_WRITE;
  g_got[entry].ga_len = ga_array_size;
  g_got[entry].ga = ga_array;

  // rcp table
  g_grt[entry].id = 0;
  g_grt[entry].grpid = grpid;
  g_grt[entry].ga_len = ga_array_size;
  g_grt[entry].ga = ga_array;

  // pub table
  g_gpt[entry].id = 0;
  g_gpt[entry].grpid = grpid;
  g_gpt[entry].ga_len = ga_array_size;
  g_gpt[entry].ga = ga_array;

  // auth table
  oc_new_string(&g_at_entries[entry].id, "0/2/10", strlen("0/2/10"));
  g_at_entries[entry].profile = OC_PROFILE_COAP_OSCORE;
  g_at_entries[entry].scope = OC_ACL_GA;
  g_at_entries[entry].ga_len = ga_array_size;
  g_at_entries[entry].ga = ga_array;
  BYTE byteArray12[2] = {0x02, L};
  oc_new_byte_string(&g_at_entries[entry].osc_id, (char*)byteArray12, 2);
  BYTE byteArray1f[16] = {(L * 0x10) + A, 0x22,    sn_b[5], sn_b[4], sn_b[3], sn_b[2], sn_b[1], sn_b[0],
                          sn_b[0],        sn_b[1], sn_b[2], sn_b[3], sn_b[4], sn_b[5], 0x22,    sn_b[1]};
  oc_new_byte_string(&g_at_entries[entry].osc_ms, (char*)byteArray1f, 16);
  BYTE byteArray16[6] = {0x00, 0x00, 0x00, 0x00, (L * 0x10) + A, 0x22};
  oc_new_byte_string(&g_at_entries[entry].osc_contextid, (char*)byteArray16, 6);

  oc_load_group_object_table();
  oc_load_object_table();
  oc_load_at_table();

  subscribe_group_to_multicast_with_port(grpid, iid, 2, COAP_DEFAULT_PORT);

  return 0;
}



int Charger_retrieve_charger() { return datapoint_charger; }

void Charger_put_charger(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
  bool error_state = true;

  // get interfaces for the resource PUT method ...
  bool is_input_datapoint = interfaces & OC_IF_I;

  // sets the pointer to the (/k or /p) handed over object
  const oc_rep_t* rep = request->request_payload;

  // user data host the HEX encoded channel/datapoint
  const int32_t channel_and_datapoint = strtol(user_data, NULL, 16);
  const uint16_t c = channel_and_datapoint >> 16;
  const uint16_t p = channel_and_datapoint & 0x0000FFFF;

  //PRINT("-- Begin PUT %s Control at %s ", oc_string(request->resource->name), oc_string(request->resource->uri));

  // handle the different request sources, here included as an example to distinguish
  // the caller source (e.g.; called by /p or /k s-mode message EP)
  if (oc_is_redirected_request_from(request) == 1)
  {
    // caller /p --> always allow to write (see 'Callback Notes' above)
    is_input_datapoint = true;
    PRINT("redirected_request %.*s", (int)request->uri_path_len, request->uri_path);
  }

  // loop over object
  while (rep)
  {
    if (rep->iname == 1 && rep->type == OC_REP_INT)
    {
      if (!is_input_datapoint)
      {
        // see 'Callback Notes' above
        oc_prepare_no_format_response_no_payload(request, OC_STATUS_METHOD_NOT_ALLOWED);
        return;
      }

      // see 'Callback Notes' above
      datapoint_charger = rep->value.integer;
      error_state = false;

      PRINT("set LSSB to %d", rep->value.integer);
      break;
    }
    rep = rep->next;
  }

  // correct data retrieved
  if (!error_state)
  {
    // inform the stack on status
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_CHANGED);

    //PRINT("-- End PUT %s at %s ", oc_string(request->resource->name), oc_string(request->resource->uri));

    return;
  }

  // bad request status
  oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
  //PRINT("-- End PUT %s at %s ", oc_string(request->resource->name), oc_string(request->resource->uri));
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
      //app_set_serial_number(argv[2]);
    }
  }

  PRINT("KNX-IOT server name : \"%s\"", application_name);

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
    oc_knx_device_storage_reset(RESET_TO_DEFAULT_STATE);
  }

  const oc_device_info_t* device = oc_core_get_device_info();

  // may produce a warning if OC_OSCORE is not specified ...
  PRINT("OSCORE - %s", OC_OSCORE ? "Enabled" : "Disabled");
  PRINT("serial number: %s", oc_string(device->serialnumber));
  //PRINT("host name: %s", oc_string(device->hostname));

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
