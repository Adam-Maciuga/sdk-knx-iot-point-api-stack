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

const char application_name[] = "Inverter PV control";
const char sn_lower_case[] = "00fa10020b00";  // deliberated incorrect serial numbers
const char hostname[] = "knx-00fa10020b00";   // default host name (reset uses this default)
const char hw_type[] = "PV Inverter ";        // 12 string chars, MSB = 00
const char dev_model[] = "6800";              // reuse mask version from iot device
const uint32_t mid = 0x00fa;                  // first 4 digits of sn_lower_case

// global variables
volatile int quit = 0; // stop variable, used by handle_signal
bool g_reset = false; // reset variable, set by commandline arguments

int datapoint_pv = 0;
uint64_t datapoint_link = 0;

datapoint_t PV_datapoint[1] = {
  {"/p/pv", "urn:knx:dpa.xxx.xx", ":dpt.value_power", "PV", "0"} // DPT: 14.056
};

// due to stuff added in knx_iot_virtual.c
lsxb_channel_t lsxb[1] = {NULL};

// additional parameters
int_datapoint_t test_parameter = {
  0, "/p/globalTestParameter", "urn:knx:dpa.65500.201", ":dpt.value2Ucount", "Global Test Parameter"};


void register_resources(void)
{   
  oc_resource_t* PV_resource = oc_new_resource(PV_datapoint[0].resource_path, 1);
  
  oc_resource_bind_resource_type(PV_resource, PV_datapoint[0].dpa);
  oc_resource_bind_dpt(PV_resource, PV_datapoint[0].dpt);
  oc_resource_bind_content_type(PV_resource, APPLICATION_CBOR, CONTENT_NONE);
  oc_resource_set_discoverable(PV_resource, true);
  oc_resource_set_observable(PV_resource, true);
  void* PV_user_data = PV_datapoint[0].id;

  oc_resource_set_request_handler(PV_resource, OC_GET, PV_get_PV, PV_user_data, OC_ACL_I, OC_IF_I);  

  oc_add_resource(PV_resource);  

  PRINT("Register test parameter");
  {
    oc_resource_t* tp0 = oc_new_resource(test_parameter.resource_path, 1);

    oc_resource_bind_resource_type(tp0, test_parameter.dpa);

    oc_resource_bind_dpt(tp0, test_parameter.dpt);

    oc_resource_bind_content_type(tp0, APPLICATION_CBOR, CONTENT_NONE);

    //oc_resource_set_function_block_instance(tp0, 1);

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
  (void)snprintf(storage, sizeof(storage), "%s/knx_iot_virtual_PV_%s", dir, sn_lower_case);
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


int PV_init_auth_table()
{
  int entry = 0;

  oc_delete_at_table();

  // auth table
  oc_new_string(&g_at_entries[entry].id, "0c00fa10020b00", strlen("0c00fa10020b00"));
  g_at_entries[entry].profile = OC_PROFILE_COAP_OSCORE;
  g_at_entries[entry].scope = OC_IF_C | OC_IF_P | OC_IF_D | OC_IF_SEC | OC_IF_SWU;

  BYTE byteArray07[7] = {0x0c, 0x00, 0xfa, 0x10, 0x02, 0x0b, 0x00};
  oc_new_byte_string(&g_at_entries[entry].osc_id, (char*)byteArray07, 7);
  BYTE byteArray0f[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
  oc_new_byte_string(&g_at_entries[entry].osc_ms, (char*)byteArray0f, 16);

  oc_load_at_table();

  return 0;
}


int PV_init_tables_QR(char* sn_cem)
{
  // the LINK resource is fixed to the sn of this very device
  // the sn_cem parameter sets the CHARGER resource to the sn received via the LINK resource -> triggered via a bus update

  oc_device_info_t* device = oc_core_get_device_info();
  char* sn_pv = oc_string(device->serialnumber);

  unsigned char sn_D[6]; // store the sn of this very device
  unsigned char sn_L[6]; // store the sn of the device to be linked (CEM)
  int len;

  for (size_t i = 0; i < 6; i++)
    sn_D[i] = 0x00;

  len = strlen(sn_pv);

  if (len % 2 == 0)
  {
    if (len / 2 == 6)
    {
      for (size_t i = 0; i < 6; i++)
      {
        char buf[3] = {sn_pv[2 * i], sn_pv[2 * i + 1], '\0'}; // 2 hex chars
        sn_D[i] = (unsigned char)strtol(buf, NULL, 16);
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


  for (size_t i = 0; i < 6; i++)
    sn_L[i] = 0x00;

  len = strlen(sn_cem);

  if (len % 2 == 0)
  {
    if (len / 2 == 6)
    {
      for (size_t i = 0; i < 6; i++)
      {
        char buf[3] = {sn_cem[2 * i], sn_cem[2 * i + 1], '\0'}; // 2 hex chars
        sn_L[i] = (unsigned char)strtol(buf, NULL, 16);
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

  int ia = 0xffff;
  uint64_t iid;
  int fid;
  uint32_t grpid;

  iid = 0x00;
  iid <<= 8;
  iid += sn_D[0];
  iid <<= 8;
  iid += sn_D[1];
  iid <<= 8;
  iid += 0x00;
  iid <<= 8;
  iid += 0x00;

  fid = sn_D[0];
  fid <<= 8;
  fid += sn_D[1];
  fid <<= 8;

  grpid = 0x00;
  grpid += sn_D[0];
  grpid <<= 8;
  grpid += sn_D[1];
  grpid <<= 8;
  grpid += 0x00;
  grpid <<= 8;
  grpid += 0x00;

  int ga0 = 0x0001;
  int ga1 = 0x0002;

  oc_core_set_and_store_device_ia(ia);
  oc_core_set_and_store_device_iid(iid);
  oc_core_set_and_store_device_fid(fid);

  oc_delete_group_object_table();
  oc_init_tables();
  oc_delete_at_table();
  unsubscribe_group_to_multicast_with_port(grpid, iid, 2, COAP_DEFAULT_PORT);

  int entry;
  int ga_array_size;
  uint32_t* ga_array;

  // object table: 0
  entry = 0;
  ga_array_size = 1;
  ga_array = malloc(ga_array_size * sizeof(uint32_t));
  if (ga_array != NULL)
  {
    ga_array[0] = ga0;
  }  
  g_got[entry].id = 0;
  oc_string_t href_pv;
  oc_new_string(&href_pv, "/p/pv", strlen("/p/pv"));
  g_got[entry].href = href_pv;
  g_got[entry].cflags = OC_CFLAG_TRANSMISSION;
  g_got[entry].ga_len = ga_array_size;
  g_got[entry].ga = ga_array;

  // rcp+pub tables
  entry = 0;
  ga_array_size = 1;
  ga_array = malloc(ga_array_size * sizeof(uint32_t));
  if (ga_array != NULL)
  {
    ga_array[0] = ga0;
  }
  g_grt[entry].id = 0;
  g_grt[entry].grpid = grpid;
  g_grt[entry].ga_len = ga_array_size;
  g_grt[entry].ga = ga_array;
  g_gpt[entry].id = 0;
  g_gpt[entry].grpid = grpid;
  g_gpt[entry].ga_len = ga_array_size;
  g_gpt[entry].ga = ga_array;

  // auth table: 0
  entry = 0;
  oc_new_string(&g_at_entries[entry].id, "0/0/1", strlen("0/0/1"));
  g_at_entries[entry].profile = OC_PROFILE_COAP_OSCORE;
  g_at_entries[entry].scope = OC_ACL_GA;
  g_at_entries[entry].ga_len = ga_array_size;
  g_at_entries[entry].ga = ga_array;
  BYTE byteArray02[2] = {0, 1};
  oc_new_byte_string(&g_at_entries[entry].osc_id, (char*)byteArray02, 2);

  if (strncmp(sn_cem, "000000000000", 12) == 0)
  {
    BYTE byteArray0f[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    oc_new_byte_string(&g_at_entries[entry].osc_ms, (char*)byteArray0f, 16);
    BYTE byteArray06[6] = {0, 0, 0, 0, 0, 0};
    oc_new_byte_string(&g_at_entries[entry].osc_contextid, (char*)byteArray06, 6);
  }
  else
  {
    BYTE byteArray0f[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    oc_new_byte_string(&g_at_entries[entry].osc_ms, (char*)byteArray0f, 16);
    BYTE byteArray06[6] = {sn_L[2], sn_L[3], sn_L[4], sn_L[5], 0, 1};
    oc_new_byte_string(&g_at_entries[entry].osc_contextid, (char*)byteArray06, 6);
  }

  // auth table: 1
  entry = 1;
  oc_new_string(&g_at_entries[entry].id, "0c00fa10020b00", strlen("0c00fa10020b00"));
  g_at_entries[entry].profile = OC_PROFILE_COAP_OSCORE;
  g_at_entries[entry].scope = OC_IF_C | OC_IF_P | OC_IF_D | OC_IF_SEC | OC_IF_SWU;

  BYTE byteArray17[7] = {0x0c, 0x00, 0xfa, 0x10, 0x02, 0x0b, 0x00};
  oc_new_byte_string(&g_at_entries[entry].osc_id, (char*)byteArray17, 7);
  BYTE byteArray1f[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
  oc_new_byte_string(&g_at_entries[entry].osc_ms, (char*)byteArray1f, 16);

  oc_load_group_object_table();
  oc_load_object_table();
  oc_load_at_table();
  subscribe_group_to_multicast_with_port(grpid, iid, 2, COAP_DEFAULT_PORT);

  return 0;
}



char* PV_retrieve_href(uint16_t point) { return PV_datapoint[point].resource_path; }

void PV_set_PV(int v) { datapoint_pv = v * 1000; }

void PV_get_PV(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
  (void)interfaces;
  bool error_state = true;

  // user data host the HEX encoded channel/datapoint
  const uint32_t channel_and_datapoint = strtol(user_data, NULL, 16);
  const uint16_t c = channel_and_datapoint >> 16;
  const uint16_t p = channel_and_datapoint & 0x0000FFFF;

  //PRINT("-- Begin GET %s at %s ", oc_string(request->resource->name), oc_string(request->resource->uri));

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  // handle the different request sources, here included as an example to distinguish
  // the caller source (e.g.; called by p/ or /k s-mode message EP)
  if (oc_is_redirected_request_from(request) == 1)
  {
    PRINT("redirected_request %.*s", (int)request->uri_path_len, request->uri_path);
  }

  oc_device_info_t* device = oc_core_get_device_info();

  // open CBOR
  oc_rep_begin_root_object();

  if (device != NULL)
  {
    if (oc_query_value_exists(request, "m") != -1)
    {
      // ... query parameter 'm' is present, check the various values
      char* m;
      char* m_key;
      size_t m_key_len;
      size_t m_len = oc_get_query_value(request, "m", &m);

      PRINT("Query Parameter: %.*s", (int)m_len, m);

      oc_init_query_iterator();

      // check query parameter
      while (oc_iterate_query(request, &m_key, &m_key_len, &m, &m_len) != -1)
      {
        // unique identifier
        if (strncmp(m, "id", m_len) == 0 || strncmp(m, "*", m_len) == 0)
        {
          // knx://sn: + max len SN + uri path + \0 = ~ 65
          char serial_number[65];

          (void)snprintf(serial_number, 65, "knx://sn:%s%s", oc_string(device->serialnumber),
                         oc_string(request->resource->uri));

          oc_rep_i_set_text_string(root, 0, serial_number);

          error_state = false;
        }
        // value
        if (strncmp(m, "value", m_len) == 0 || strncmp(m, "*", m_len) == 0)
        {
          // see 'Callback Notes' above
          oc_rep_set_uint(root, value, datapoint_pv);
          error_state = false;
        }
        // resource types
        if (strncmp(m, "rt", m_len) == 0 || strncmp(m, "*", m_len) == 0)
        {
          // use the first type, skip urn:knx (=7), if more types are used
          // - add a next in the data structure
          // - add an extra line
          const char* first_type = oc_string_array_get_item(request->resource->types, 0);

          oc_rep_text_set_text_string(root, rt, first_type + 7);
          error_state = false;
        }
        // interfaces (array of text strings)
        if (strncmp(m, "if", m_len) == 0 || strncmp(m, "*", m_len) == 0)
        {
          add_all_interface_short_urns_for_a_resource(request->resource);
          error_state = false;
        }
        // dpt
        if (strncmp(m, "dpt", m_len) == 0 || strncmp(m, "*", m_len) == 0)
        {
          oc_rep_text_set_text_string(root, dpt, oc_string(request->resource->dpt));
          error_state = false;
        }
        // ga
        if (strncmp(m, "ga", m_len) == 0 || strncmp(m, "*", m_len) == 0)
        {
          const int index = oc_core_find_first_group_object_table_index_from_href(oc_string(request->resource->uri));
          if (index > -1)
          {
            oc_group_object_table_t* got_table_entry = oc_core_get_group_object_table_entry(index);
            if (got_table_entry)
            {
              oc_rep_set_int_array(root, ga, got_table_entry->ga, got_table_entry->ga_len);
            }
          }
          error_state = false;
        }
        // description
        if (strncmp(m, "desc", m_len) == 0 || strncmp(m, "*", m_len) == 0)
        {
          //oc_rep_text_set_text_string(root, desc, oc_string(request->resource->name));

          error_state = false;
        }
      }
    }
    else
    { // ... no query parameter 'm' present at all, set value for the GET

      // see 'Callback Notes' above
      oc_rep_i_set_uint(root, 1, datapoint_pv);

      error_state = false;
    }
  }

  // close CBOR
  oc_rep_end_root_object();

  // check CBOR encoding errors
  if (g_err != CborNoError)
  {
    error_state = true;
  }

  PRINT("CBOR encoder size %d", oc_rep_get_encoded_payload_size());

  // wrong device, cbor error or unknown 'm' query parameter key values
  if (error_state)
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_NOT_FOUND);
  else
    oc_prepare_cbor_response(request, OC_STATUS_OK);

  //PRINT("-- End GET %s at %s ", oc_string(request->resource->name), oc_string(request->resource->uri));
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
#ifdef _MSC_VER
  // MSVC - may not need cast
  WinMain(GetModuleHandle(NULL), NULL, GetCommandLine(), SW_SHOWNORMAL);
#else
  // GCC - needs cast to suppress warning
  WinMain(GetModuleHandle(NULL), NULL, (LPSTR)GetCommandLine(), SW_SHOWNORMAL);
#endif

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
