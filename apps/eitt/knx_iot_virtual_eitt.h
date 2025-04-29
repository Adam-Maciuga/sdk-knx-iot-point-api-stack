/*
-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
 Copyright (c) 2025-2025 KNXA Association
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
 * header file for the generated application.
 * header file contains functions to use the generated application with an
 external main.
 * e.g. if the c code is compiled without main then
 * these functions can be used to call all generated code
 *

 */

#define NUM_CHANNELS (2)
#define NUM_POINTS   (2)
#define SOO (0)
#define IOO (1)
#define LSAB0 (0)
#define LSAB1 (1)

typedef struct datapoint
{
  volatile bool value;
  char* url;
  char* dpa;
  char* dpt;
  char* ift;
} datapoint_t;

typedef struct channel
{
  char* name;
  char* desc;
  datapoint_t point[NUM_POINTS];
} channel_t;


#ifdef __cplusplus
extern "C"
{
#endif

#define APPLICATION_NAME "KNX virtual EITT certification application"
#define FIRMWARE_NAME "KNX stack image"
#define SN_LOWER_CASE "00fa10020800"  // same as eitt test template 
#define HOST_NAME "knx-00fa10020800" // default host name, same as eitt test template  
#define PASSWORD "2X4W3TE0DFLLS19Y1FCH"
#define QRCODE_ETS6 "KNX:S:00FA10020800;P:2X4W3TE0DFLLS19Y1FCH"
#define HW_TYPE_ETS6 "Windows" // 12 string chars, MSB = 00 , here same as eitt test template
#define DEV_MODEL_ETS6 "KNX Certification" // same as eitt test template

#define MID (667) // first 4 digits of SN_LOWER_CASE (same as eitt test template) 

// URL defines

// define URL Parameter Page/ Test Parameter (same as eitt test template) 
#define _0_url_value "/p/p1"
#define _0_name "Global Test Parameter"
#define _0_dpt ":dpt.propDataType"
#define _0_dpa_switch_short ":dpa.65500.201"
#define _0_des "global test parameter as 16 bit uint"
#define _0_if_p ":if.p"

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
  char* app_retrieve_url_from_channel(uint16_t channel, uint16_t point);

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

#ifdef __cplusplus
}
#endif
