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

// include file, used for all CL/GUI applications (EITT/LSAB/LSSB)


#ifndef KNX_IOT_VIRTUAL_H
#define KNX_IOT_VIRTUAL_H

// network

/*
 The network router may not allow to send multicast with scope 5 (site local),
 hence the DEMO applications use scope 2 instead. If needed,
 sendout with scope 2 and 5 separately may be an option (2 messages). 
 */
#define SENDER_SCOPE (2)

// common data
#define NUM_CHANNELS (2)
#define NUM_POINTS   (2)
#define SOO  (0)
#define IOO  (1)
#define LSSB (1)
#define LSAB (0)

#define FIRMWARE_NAME "KNX stack image"
#define HW_TYPE_ETS6 "000102030405" // 12 string chars, MSB = 00
#define DEV_MODEL_ETS6 "6800" // reuse mask version from iot device
#define MID (0x00FA) // first 4 digits of SN_LOWER_CASE
#define PASSWORD "2X4W3TE0DFLLS19Y1FCH"

#define _0_name "Global Test Parameter"
#define _0_des "global test parameter as 16 bit uint"

// Sensor
#define APPLICATION_NAME_LSSB "KNX virtual sensor (LSSB)"
#define SN_LOWER_CASE_LSSB "00fa10020700" // default SN if not overwritten by CL option -s, deliberated incorrect serial numbers
#define HOST_NAME_LSSB (SN_LOWER_CASE_LSSB) // default host name (reset uses SN_LOWER_CASE as default)
#define QRCODE_ETS6_LSSB "KNX:S:00FA10020700;P:2X4W3TE0DFLLS19Y1FCH"

#define _0_url_value_lssb "/p/globalTestParameter"
#define _0_dpt_lssb ":dpt.value2Ucount"
#define _0_dpa_switch_short_lssb ":dpa.void"


// Actuator
#define APPLICATION_NAME_LSAB "KNX virtual actuator (LSAB)"
#define SN_LOWER_CASE_LSAB "00fa10020900" // default SN if not overwritten by CL option -s, deliberated incorrect serial numbers
#define HOST_NAME_LSAB (SN_LOWER_CASE_LSAB) // default host name (reset uses SN_LOWER_CASE as default)
#define QRCODE_ETS6_LSAB "KNX:S:00FA10020900;P:2X4W3TE0DFLLS19Y1FCH"

#define _0_url_value_lsab "/p/globalTestParameter"
#define _0_dpt_lsab ":dpt.value2Ucount"
#define _0_dpa_switch_short_lsab ":dpa.void"


// EITT
#define APPLICATION_NAME_EITT "KNX virtual EITT certification application"
#define SN_LOWER_CASE_EITT "00fa10020800" // same as eitt test template, deliberated incorrect serial numbers
#define HOST_NAME_EITT (SN_LOWER_CASE_EITT) // default host name (reset uses SN_LOWER_CASE as default)
#define QRCODE_ETS6_EITT "KNX:S:00FA10020800;P:2X4W3TE0DFLLS19Y1FCH"
#define MID_EITT (667) // same as eitt test template
#define HW_TYPE_EITT "Windows" // 12 string chars, same as eitt test template
#define DEV_MODEL_EITT "KNX Certification" // same as eitt test template

// define URL Parameter Page/ Test Parameter (same as eitt test template)
#define _0_url_value_eitt "/p/p1"
#define _0_dpt_eitt ":dpt.propDataType"
#define _0_dpa_switch_short_eitt ":dpa.65500.201"

typedef struct datapoint
{
  volatile bool value;
  char* href;
  char* dpa;
  char* dpt;
  char* desc;
} datapoint_t;

typedef struct channel
{
  char* name;
  datapoint_t point[NUM_POINTS];
} channel_t;


#ifdef __cplusplus
extern "C"
{
#endif

  /**
   * @brief initialize the stack
   *
   * @return int 0 == success
   */
  int app_initialize_stack(void);

  /**
   * @brief sets the serial number
   * should be called before app_initialize_stack()
   *
   * @note used from several applications, hence define it as method.
   *
   * @param serial_number the serial number as string
   * @return int 0 == success, -1 error
   */
  int app_set_serial_number(const char* serial_number);

  /**
   * @brief Set a bool
   *
   * @param channel the channel for the bool to get
   * @param point the point of the channel for the bool to get
   * @param value value to set
   */
  void app_set_bool_variable_from_channel(uint16_t channel, uint16_t point, bool value);

  /**
   * @brief Set an int
   *
   * @param url the url for the int to set
   * @param value value to set
   */
  void app_set_int_variable(const char* url, int value);

  /**
   * @brief Get a bool
   *
   * @param channel the channel for the bool to get
   * @param point the point of the channel for the bool to get
   * @return boolean variable
   */
  bool app_retrieve_bool_variable_from_channel(uint16_t channel, uint16_t point);

  /**
   * @brief Get a URL
   *
   * @param channel the channel for the URL to get
   * @param point the point of the channel for the URL to get
   * @return boolean variable
   */
  char* app_retrieve_href_from_channel(uint16_t channel, uint16_t point);

  /**
   * @brief Get an int
   *
   * @param url the url for the bool to get
   * @return int variable
   */
  int app_retrieve_int_variable(const char* url);

  /**
   * @brief checks if the url represents a parameter
   *
   * @param url the url
   * @return true the url represents a parameter
   */
  bool app_is_url_parameter(char* url);

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
   * @brief retrieve the fault (boolean) variable at the url
   *
   * @param url the url indicating the fault variable
   * @return the value of the fault variable
   */
  bool app_retrieve_fault_variable(const char* url);

  /**
   * @brief function to report if the (oscore) security is turn on for this
   * instance
   *
   * @return true is secure
   * @return false is not secure
   */
  bool app_is_secure(void);

  /**
   * @brief retrieves the password for showing on screen
   *
   * @return password (as string)
   */
  char* app_get_password(void);

  /**
   * @brief function to set the input string to upper case
   *
   * @param str the string to make upper case
   *
   */
  void app_str_to_upper(char* str);

  /**
   * @brief generic code to define the array's indexes for a channel + datapoint
   *
   * @param channel the to be scanned channel 
   * @param user_data the user data handed over to the application callback handler
   *
   */
  int32_t app_get_channel_and_point(const channel_t* channel, const void* user_data);

  /**
   * @brief software update callback
   *
   * @param response the instance of an internal struct that is used to track the
   * state of the separate response
   * @param binary_size the full size of the binary
   * @param offset the offset of the image
   * @param payload the image data
   * @param len the length of the image data
   * @param data the user data
   */
  void swu_cb(oc_separate_response_t* response, const size_t binary_size, const size_t offset, uint8_t* payload,
              const size_t len, void* data);

#ifdef __cplusplus
}
#endif
#endif
