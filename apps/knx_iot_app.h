/* 
 * Copyright (c) 2022-2023 Cascoda Ltd
 * Copyright (c) 2024-2026 KNX Association
 *            
 * SPDX-License-Identifier: Apache-2.0
 */     

/*
 * Note that the file 'knx_iot_app.c/h' is NOT a part of the stack or not intended to be an 
 * 'application' library. It hosts only for the application demos commonly used functionality in one place.
 */

#ifndef KNX_IOT_APP_H
#define KNX_IOT_APP_H

#include "oc_api.h"
#include "apps/knx_iot_datapoint.h"

// use it in upper case (min 6, max 32), IMPORTANT consider the notes for the PASE Resource Object (oc_pase_t)
#define PASSWORD "2X4W3TE0DFLLS19Y1FCH"	// TODO FIXME move/handle this somewhere/somehow better

/* Reduced Datapoints for Examples */
typedef struct
{
  volatile bool value;  // the actual datapoint type, see notes above
  char* resource_path;  // the resource path such as /p/...
  char* dpa;            // annotated datapoint, see in KNX ioT specification 3/10/5 
  char* dpt;            // datapoint type, see in KNX ioT specification 3/10/5
  uint16_t id;          // see note above
} bool_datapoint_no_name_no_flags_t;

typedef struct
{
  volatile int value; 
  char* resource_path;
  char* dpa;
  char* dpt;
  char* name;
} int_datapoint_no_flags_t;

#define LSXB_NUM_CHANNELS (2)  // common data for LSAB/LSSB/EITT
#define NUM_CEM_POINTS (2)  // common data for CEM

/* Reduced and Special Function Blocks for Examples */
typedef struct
{
  uint16_t fb_number;
  uint8_t fb_instance;
  uint8_t fb_number_of_datapoints;
  int_datapoint_no_flags_t point; // reduced int datapoint for example
} int_functional_block_no_flags_t; // see Functional Block Notes

typedef struct
{
  uint16_t fb_number;
  uint8_t fb_instance;
  uint8_t fb_number_of_datapoints;
  float_datapoint_t point[NUM_CEM_POINTS];
} float_array_functional_block_t; // see Functional Block Notes

typedef struct
{
  uint16_t fb_number;
  uint8_t fb_instance;
  uint8_t fb_number_of_datapoints;
  bool_datapoint_no_name_no_flags_t point[LSXB_NUM_CHANNELS];  // minimal bool datapoint type for example
} bool_array_functional_block_no_name_no_flags_t, lsxb_channel_t; // see Functional Block Notes

/**
 * Definition to get the current working directory depending on platform.
 */
#ifdef _WIN32                                  // Windows
#include <direct.h>
#define GetCurrentDir _getcwd
#elif defined(__linux__) || defined(__APPLE__) // Linux or Apple
#include <unistd.h>
#define GetCurrentDir getcwd
#endif

/*
 * Definition of weak symbol for cross-platform compatibility.
 * - on Windows with MSVC the weak symbol is not supported for functions, hence we define an empty macro for now
 * - TODO find a better way to handle weak symbols cross-platform
 * 
 */ // TODO FIXME AB verify everything still compiles with MSVC after reworked app initialization
#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)
#define KNX_TOOL_WEAK
#elif defined(__GNUC__)
#define KNX_TOOL_WEAK __attribute__((weak))
#endif

#ifdef __cplusplus
extern "C"
{
#endif

  /**
   * @brief Function to set up the device on stack startup.
   *        - sn, application name, hwv, hwt, device model -> permanent
   *        - fwv, application version, hostname -> volatile, may be changed by MaC configuration
   *
   * @note
   * - It initializes data with permanent values,
   *   but also data with their (volatile) default values that may be changed or where
   *   already changed at runtime (see above).
   * - It is called at the end of 'knx_iot_initialize_stack', before reading device storage data.
   *   Hence, written default values on 'init' may be overwritten (again) with storage values
   *   such as the storage hostname (if present on storage, then this would be correct).
   *     
   */
  int app_init(void);
  
  /**
   * @brief initialize the stack
   *
   * @param storage_name the storage name, max 64 chars, more chars are cut
   *
   * @note the storage name will be appended by the device serial number,
   *       the storage as such is used to save the device configuration data,
   *       the data is stored in the current directory
   *       for Windows or Linux the storage name is the folder name
   *                            
   * @return int 0 == success
   */
  int knx_iot_initialize_stack(const char* storage_name);
  
  /**
   * @brief retrieves the url of a parameter
   * index starts at 1
   * @param index the index to retrieve the url from
   * @return the url or NULL
   */
  char* app_get_parameter_url(int index);
  
  /**
   * @brief retrieves the name of a parameter
   * index starts at 1
   * @param index the index to retrieve the parameter name from
   * @return the name or NULL
   */
  char* app_get_parameter_name(int index);
  
  /**
   * @brief returns the SPAKE2+ client password, used from external application hence defined as
   *        separate method.
   *
   * @note  IMPORTANT consider the notes for the PASE Resource Object (oc_pase_t)
   */
  char* app_get_password(void);
  
  /**
   * @brief
   * Application factory preset callback handler for the device
   * @param data the supplied data.
   */
  void knx_iot_factory_presets_cb(void* data);
 
  /**
   * @brief
   * Application restart callback handler for the device
   * @param data the supplied data.
   */
  void knx_iot_restart_cb(void* data);

  /**
   * @brief initializes the global variables
   * for the resources
   * for the parameters
   */
  void knx_iot_initialize_variables(void);
  
  /**
   * @brief
   * Application set hostname callback handler for the device
   *
   * @param hostname the new hostname of the device to be maintained.
   * @param data the supplied data.
   */
  void knx_iot_set_hostname_cb(const oc_string_t hostname, void* data);

  /*** Device Commissioning ***/
  /**
   * @brief Get the programming mode of the KNX device
   *
   * @return true if device is in programming mode
   * @return false if device is not in programming mode
   */
  bool knx_get_programming_mode(void);
  
  /**
   * @brief Set the programming mode of the KNX device
   *
   * @return true if device is in programming mode
   * @return false if device is not in programming mode
   */
  bool knx_set_programming_mode(const bool programming_mode);
  
  /**
   * @brief Toggle the programming mode of the KNX device
   *
   * @return true if device is in programming mode
   * @return false if device is not in programming mode
   */
  bool knx_toggle_programming_mode(void);
  
    /*** Firmware Update ***/
  /**
   * @brief software update callback
   *
   * @param response the instance of an internal struct that is used to track the state of the separate response
   * @param binary_size the full size of the binary
   * @param block_offset the offset of the image
   * @param block_data the image data
   * @param block_len the length of the image data
   * @param data the user data
   */
  void swu_cb(oc_separate_response_t* response, size_t binary_size, size_t block_offset, const uint8_t* block_data, size_t block_len, void* data);
  
  /**
   * @brief software update upgrade trigger callback
   * 
   * Called when /swu/update receives a PUT request, indicating the device should
   * start the firmware upgrade process
   * 
   * @param defer_time requested defer time in seconds before starting upgrade
   * @param data user data
   */
  void swu_upgrade_cb(int defer_time, void* data);
  
  /**
   * @brief s-mode response callback,
   *        will be called when a response is received on an s-mode read request
   *
   * @param url the url
   * @param rep the full response
   * @param rep_value the parsed value of the response
   */
  void oc_s_mode_response_cb(char* url, oc_rep_t* rep, oc_rep_t* rep_value);
  
  // TODO add documentation
  void knx_iot_signal_event_loop(void);

  // TODO add documentation
  int knx_iot_initialize_app(void);
 
#ifdef __cplusplus
}
#endif

#endif
