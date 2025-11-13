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

/*
  Note that the file 'knx_iot_virtual.c/h' is NOT a part of the stack or not intended to be an
  'application' library. It hosts only for the application demos commonly used functionality in one place.
*/

#include "ctype.h"
#include "oc_api.h"
#include "knx_iot_virtual.h"
#include "oc_core_res.h"
#include "oc_helpers.h"
#include "oc_knx_client.h"
#include "port/oc_storage.h"

extern const char application_name[];
extern const char sn_lower_case[];
extern const char hostname[];
extern const uint32_t mid;
extern const char hw_type[];
extern const char dev_model[];

void app_str_to_upper(char* str)
{
  while (*str != '\0')
  {
    *str = (char)toupper(*str);
    str++;
  }
}

// IMPORTANT consider the notes for the PASE Resource Object (oc_pase_t)
char* app_get_password(void) { return PASSWORD; }

#ifdef OC_DEBUG
#ifndef _MSC_VER
// Periodic stdout flush callback for debug builds
static oc_event_callback_retval_t flush_stdout_callback(void* context)
{
  (void)context;
  fflush(stdout);
  // Schedule next flush in 2 seconds
  oc_set_delayed_callback(NULL, flush_stdout_callback, 2);
  return OC_EVENT_DONE;
}
#endif
#endif

// the delayed swu callback handler 
static oc_event_callback_retval_t send_delayed_response(void* context)
{
  oc_separate_response_t* response = context;

  if (response->active)
  {
    // alloc buffer for response
    oc_set_separate_response_buffer(response);

    // no payload data for a swu response, only 2.04 changed status
    oc_send_separate_response(response, OC_STATUS_CHANGED);

    OC_DBG("delayed response (still) active -> sent it out");
  }
  else
  {
    OC_DBG("delayed response NOT active (anymore) -> ignored");
  }

  return OC_EVENT_DONE;
}

void swu_cb(oc_separate_response_t* response, size_t binary_size, size_t block_offset, const uint8_t* block_data, size_t block_len, void* data)
{
  (void)binary_size;
  (void)data;

  char filename[] = "./downloaded.bin";
  OC_DBG("swu_cb %s block offset=%d block size=%d ", filename, (int)block_offset, (int)block_len);

  // 'ab' = add to the end of file (a) in binary mode (b)
  FILE* write_ptr = fopen("downloaded_bin", "ab");
  const size_t n = fwrite(block_data, sizeof(*block_data), block_len, write_ptr);
  const size_t r = fclose(write_ptr);
  OC_DBG("written data: %llu, operation ok (=0): %llu", n, r);

  // 
  oc_set_delayed_callback(response, &send_delayed_response, 1);
}

void add_all_interface_short_urns_for_a_resource(const oc_resource_t* resource)
{
  // get all if's
  oc_interface_mask_t res_interfaces = OC_IF_NONE;
  oc_resource_get_all_interfaces_for_a_resource(resource, &res_interfaces);

  // create interface list, n elements
  const unsigned int nr_entries = oc_count_total_interfaces_in_mask(res_interfaces);
  oc_string_array_t interface_list;
  oc_new_string_array(&interface_list, nr_entries);

  // put all if's to string array
  oc_put_all_interface_short_urns_from_a_mask_in_string_array(res_interfaces, interface_list);

  // add strings by key 'if'
  oc_rep_set_string_array(root, if, interface_list);

  // release interface list
  oc_free_string_array(&interface_list);
}

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

void factory_presets_cb(void* data)
{
  (void)data;
}

void hostname_cb(const oc_string_t host_name, void* data)
{
  (void)data;

  PRINT("host name callback called with host name: %s", oc_string(host_name));

  /*
   * The application callback needs to handle a changed host name such as to
   * announce it to a border router or local daemon.
   */
}

void initialize_variables(void)
{
  /* initialize global variables for resources */
  /* if wanted to be read them from persistent storage */
}

int app_init(void)
{
  /*
    define 4kb Stdout write buffer
    - needed for faster console output on gcc debug builds
    - can be skipped (see below) when using a msvc (windows) debug build

    Note that on using the buffering, shorter console output logs may be
    delayed until the buffer is full. We add periodic flushing to ensure
    timely output while maintaining performance.
   
  */

  // set up periodic stdout flushing for debug builds
  #ifdef OC_DEBUG
  #ifndef _MSC_VER

  (void)setvbuf(stdout, NULL, _IOFBF, 4096);
  // flush stdout every 2 seconds to prevent delayed output
  oc_set_delayed_callback(NULL, flush_stdout_callback, 2);

  #endif
  #endif

  // set the device 
  oc_core_set_device(sn_lower_case, application_name);

  // set the hardware version 0.0.1, value used from EITT for testing
  oc_core_set_device_hwv(0, 0, 1);

  // set the hardware version 0.0.1, value used from EITT for testing
  oc_core_set_device_fwv(0, 0, 1);

  // set the application version 1.0.0, value may be overwritten at runtime by MaC PUT
  oc_core_set_device_apv(1, 0, 0);

  // set manufacturer id, value used from EITT for testing
  oc_core_set_device_mid(mid);

  // set the hardware type -> 12 chars, value used from EITT for testing
  oc_core_set_device_hwt(hw_type);

  // set device model, value used from EITT for testing
  oc_core_set_device_model(dev_model);

  // set host name, value used from EITT for testing
  oc_core_set_device_hostname(hostname);

  // set response callback (if needed must be filled with code)
  oc_set_s_mode_response_cb(oc_s_mode_response_cb);

#if defined (OC_SPAKE) && defined (OC_DEBUG) 

  // convert in upper case (12 x char + /0)
  char sn_upper_case[SERIAL_NUM_SIZE + 1];
  memcpy(sn_upper_case, sn_lower_case, SERIAL_NUM_SIZE +1);
  app_str_to_upper(sn_upper_case);

  OC_DBG_SPAKE("=== QR Code: KNX:S:%s;P:%s ===", sn_upper_case, app_get_password());

#endif

  return 0;
}

/**
 * @brief signal the event loop, GUI build: wxTimer drives oc_main_poll(),
 * so we don't need to wake up a blocking loop.
 */
void signal_event_loop(void)
{
  // DO NOTHING, wxTimer drives oc_main_poll()
}

int app_initialize_stack(const char* storage_folder_name)
{
  /*
    The final storage folder depends on the build system/ current directory on Linux/ Windows,
    the folder name is defined by the file name + serial number. The data are stored in the current directory

    Code below should work both on Linux/Windows.

    For a specific embedded OS usually this functionality needs to be adapted.
  */

  char storage[64];
  

  #if defined(_WIN32) || defined(__unix__) || defined(__APPLE__)

  // current directory, folder name appended with serial number
  char folder[64] = "./";
  strcat(folder, storage_folder_name);
  strcat(folder, "_%s");

  (void)snprintf(storage, sizeof(storage), folder, sn_lower_case);

  #ifdef OC_DEBUG

  char dir[FILENAME_MAX] = "";
  GetCurrentDir(dir, FILENAME_MAX);
  OC_INF("Current path is: '%s'", dir);

  #endif

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
