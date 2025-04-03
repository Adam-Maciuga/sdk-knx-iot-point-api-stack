/*
-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
 Copyright (c) 2022-2023 Cascoda Ltd
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

#ifdef __cplusplus
extern "C" {
	#endif

	#define APPLICATION_NAME "KNX virtual switch actuator"
	#define FIRMWARE_NAME "KNX stack image"
	#define SN "00fa10020800"
	#define HOST_NAME (SN) // default host name (reset uses SN as default)
	#define PASSWORD "2X4W3TE0DFLLS19Y1FCH"
	#define QRCODE_ETS6 "KNX:S:00FA10020800;P:2X4W3TE0DFLLS19Y1FCH"
	#define HW_TYPE_ETS6 "000102030405" // 12 string chars, MSB = 00
	#define DEV_MODEL_ETS6 "6800"       // reuse mask version from iot device

	#define MID (0x00FA) // first 4 digits of SN

	// URL defines

	// define URL Parameter Page/ Test Parameter
	#define _0_url_value "/p/0"
	#define _0_name "Test Parameter"
	#define _0_dpt ":dpt.value2Ucount"
	#define _0_dpa_value_long "urn:knx:void"
	#define _0_dpa_value_short ":dpa.void"
	#define _0_des "test parameter 16 bit uint"
	#define _0_if_value ":if.i"
	#define _0_if_status ":if.o"

	// define channel 1..4 + included EPs control/status
	#define _1_url_value "/p/1"
	#define _1_url_status "/p/2"
	#define _1_name "OnOff 1"
	#define _1_dpt ":dpt.switch"
	#define _1_dpa_value_long "urn:knx:dpa.417.61"
	#define _1_dpa_value_short ":dpa.417.61"
	#define _1_dpa_status_long "urn:knx:dpa.417.62"
	#define _1_dpa_status_short ":dpa.417.62"
	#define _1_des "On/Off Channel 1"
	#define _1_if_value ":if.i"
	#define _1_if_status ":if.o"

	#define _2_url_value "/p/3"
	#define _2_url_status "/p/4"
	#define _2_name "OnOff 2"
	#define _2_dpt ":dpt.switch"
	#define _2_dpa_value_long "urn:knx:dpa.421.61"
	#define _2_dpa_value_short ":dpa.421.61"
	#define _2_dpa_status_long "urn:knx:dpa.421.62"
	#define _2_dpa_status_short ":dpa.421.62"
	#define _2_des "On/Off Channel 2"
	#define _2_if_value ":if.i"
	#define _2_if_status ":if.o"

	#define _3_url_value "/p/p1"
	#define _3_url_status "/p/6"
	#define _3_name "OnOff 3"
	#define _3_dpt ":dpt.value2Ucount"
	#define _3_dpa_value_long "urn:knx:dpa.417.255"
	#define _3_dpa_value_short ":dpa.417.255"
	#define _3_des "On/Off Channel 3"
	#define _3_if_value ":if.i"
	#define _3_if_status ":if.o"

	#define _4_url_value "/p/7"
	#define _4_url_status "/p/8"
	#define _4_name "OnOff 4"
	#define _4_dpt ":dpt.value2Ucount"
	#define _4_dpa_value_long "urn:knx:dpa.421.61"
	#define _4_dpa_value_short ":dpa.421.61"
	#define _4_dpa_status_long "urn:knx:dpa.421.62"
	#define _4_dpa_status_short ":dpa.421.62"
	#define _4_des "On/Off Channel 4"

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
	 * @param url the url for the bool to set
	 * @param value value to set
	 */
	void app_set_bool_variable(const char* url, bool value);

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
	 * @param url the url for the bool to get
	 * @return boolean variable
	 */
	bool app_retrieve_bool_variable(const char* url);

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
	 * @brief sets the fault (bool) state of the url/data point
	 * the caller needs to know if the resource/data point implements a fault
	 * situation
	 *
	 * @param url the url of the resource/data point
	 * @param value the boolean fault value to be set
	 */
	void app_set_fault_variable(const char* url, bool value);

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
