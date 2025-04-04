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
 * KNX virtual Switching Actuator

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
#include "oc_api.h"
#include "oc_core_res.h"
#include "oc_rep.h"
#include "oc_helpers.h"
#include "api/oc_knx_fp.h"
#include "api/oc_knx_dev.h"
#include "port/oc_clock.h"

#ifdef OC_SPAKE
#include "security/oc_spake2plus.h" // security enrollment by password
#endif

#include "knx_iot_virtual_sa.h" // application constants
#include <signal.h>             // test purpose only; commandline reset
#include <stdlib.h>
#include <stdio.h> // defines FILENAME_MAX
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
#else                         // linux,mac specific code
#include <unistd.h>
#define GetCurrentDir getcwd // path of current working directory, LINUX, MAC
#endif

// Global variables

volatile int quit = 0;       // stop variable, used by handle_signal
bool g_reset = false;        // reset variable, set by commandline arguments
char g_serial_number[] = SN; // startup SN, maybe overwritten by CL option

// functional block 417 LSAB command/control are needed for certification tests
volatile bool g_OnOff_1;
volatile bool g_InfoOnOff_1;

// functional block 421 LSSB command/control are needed for certification tests
volatile bool g_OnOff_2;
volatile bool g_InfoOnOff_2;

// additional objects
volatile uint16_t g_OnOff_3;
volatile uint16_t g_InfoOnOff_3;
volatile bool g_OnOff_4;
volatile bool g_InfoOnOff_4;

volatile bool g_fault_OnOff_1;
volatile bool g_fault_OnOff_2;
volatile bool g_fault_OnOff_3;
volatile bool g_fault_OnOff_4;

// additional parameters
volatile uint16_t g_test_parameter;

// BOOLEAN code

void app_set_bool_variable(const char* url, const bool value)
{
	if (strcmp(url, _1_url_value) == 0)
	{
		g_OnOff_1 = value; /**< global variable for OnOff_1 */
		return;
	}
	if (strcmp(url, _1_url_status) == 0)
	{
		g_InfoOnOff_1 = value; /**< global variable for InfoOnOff_1 */
		return;
	}
	if (strcmp(url, _2_url_value) == 0)
	{
		g_OnOff_2 = value; /**< global variable for OnOff_2 */
		return;
	}
	if (strcmp(url, _2_url_status) == 0)
	{
		g_InfoOnOff_2 = value; /**< global variable for InfoOnOff_2 */
		return;
	}
	if (strcmp(url, _4_url_value) == 0)
	{
		g_OnOff_4 = value; /**< global variable for OnOff_4 */
		return;
	}
	if (strcmp(url, _4_url_status) == 0)
	{
		g_InfoOnOff_4 = value; /**< global variable for InfoOnOff_4 */
	}
}

bool app_retrieve_bool_variable(const char* url)
{
	if (strcmp(url, _1_url_value) == 0)
	{
		return g_OnOff_1; /**< global variable for OnOff_1 */
	}
	if (strcmp(url, _1_url_status) == 0)
	{
		return g_InfoOnOff_1; /**< global variable for InfoOnOff_1 */
	}
	if (strcmp(url, _2_url_value) == 0)
	{
		return g_OnOff_2; /**< global variable for OnOff_2 */
	}
	if (strcmp(url, _2_url_status) == 0)
	{
		return g_InfoOnOff_2; /**< global variable for InfoOnOff_2 */
	}
	if (strcmp(url, _4_url_value) == 0)
	{
		return g_OnOff_4; /**< global variable for OnOff_4 */
	}
	if (strcmp(url, _4_url_status) == 0)
	{
		return g_InfoOnOff_4; /**< global variable for InfoOnOff_4 */
	}

	return false;
}

// INT code

void app_set_int_variable(const char* url, const int value)
{
	if (strcmp(url, _3_url_value) == 0)
	{
		g_OnOff_3 = value;
		return;
	}
	if (strcmp(url, _3_url_status) == 0)
	{
		g_InfoOnOff_3 = value;
	}
}

int app_retrieve_int_variable(const char* url)
{
	if (strcmp(url, _3_url_value) == 0)
	{
		return g_OnOff_3;
	}
	if (strcmp(url, _3_url_status) == 0)
	{
		return g_InfoOnOff_3;
	}

	return -1;
}

// FAULT code

void app_set_fault_variable(const char* url, const bool value)
{
	if (strcmp(url, _1_url_value) == 0)
	{
		/* handling fault of OnOff_1 */
		g_fault_OnOff_1 = value; /**< global fault variable for OnOff_1 */
		if (value == true)
		{
			/* this is a fault is set the info variable on fault */
			app_set_bool_variable("/p/2", false);
		}
		else
		{
			/* restore the value from the current data*/
			app_set_bool_variable("/p/2", g_OnOff_1);
		}
	}
	if (strcmp(url, _2_url_value) == 0)
	{
		/* handling fault of OnOff_2 */
		g_fault_OnOff_2 = value; /**< global fault variable for OnOff_2 */
		if (value == true)
		{
			/* this is a fault is set the info variable on fault */
			app_set_bool_variable("/p/4", false);
		}
		else
		{
			/* restore the value from the current data*/
			app_set_bool_variable("/p/4", g_OnOff_2);
		}
	}
	if (strcmp(url, _3_url_value) == 0)
	{
		g_fault_OnOff_3 = value;
		if (value == true)
		{
			app_set_int_variable("/p/6", -1);
		}
		else
		{
			app_set_int_variable("/p/6", g_OnOff_3);
		}
	}
	if (strcmp(url, _4_url_value) == 0)
	{
		/* handling fault of OnOff_4 */
		g_fault_OnOff_4 = value; /**< global fault variable for OnOff_4 */
		if (value == true)
		{
			/* this is a fault is set the info variable on fault */
			app_set_bool_variable("/p/8", false);
		}
		else
		{
			/* restore the value from the current data*/
			app_set_bool_variable("/p/8", g_OnOff_4);
		}
	}
}

// PARAMETER code

bool app_is_url_parameter(char* url)
{
	return false;
}

char* app_get_parameter_url(int index)
{
	return NULL;
}

char* app_get_parameter_name(int index)
{
	return NULL;
}

bool app_is_secure(void)
{
	// may produce a warning if OC_OSCORE is not specified ...
	// but here it is integral part of CMake
	return OC_OSCORE ? true : false;
}

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
void oc_add_s_mode_response_cb(char* url, oc_rep_t* rep, oc_rep_t* rep_value)
{
	(void) rep;
	(void) rep_value;

	PRINT("oc_add_s_mode_response_cb %s", url);
}

/**
 * @brief function to set the input string to upper case
 *
 * @param str the string to make upper case
 *
 */
void app_str_to_upper(char* str)
{
	while (*str != '\0')
	{
		*str = toupper(*str);
		str++;
	}
}

/**
 * @brief function to set up the device.
 *
 * sets the:
 * - manufacturer     : cascoda
 * - serial number    : 00FA10010700
 * - base path
 * - knx spec version
 * - hardware version : [0, 7, 0]
 * - firmware version : [0, 7, 0]
 * - hardware type    : 000000000002
 * - device model     : KNX virtual - SA
 *
 */
int	app_init(void)
{
	// set provider, no callback/no data
	int ret = oc_init_platform("KNX Association", NULL, NULL);

	// set the application name, version, base url, device serial number
	// init also the device resources such as /dev, /.well-known/core, ...
	ret |=
		oc_add_device(APPLICATION_NAME, "1.0.0", "//", g_serial_number, NULL, NULL);

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
	oc_core_set_device_hostname(0, HOST_NAME);

	oc_set_s_mode_response_cb(oc_add_s_mode_response_cb);

	#ifdef OC_SPAKE

	// if current (negotiated) pwd is not set, use definition from test
	// application
	if (strlen(oc_spake_get_password()) == 0)
		oc_spake_set_password(PASSWORD);

	OC_DBG_SPAKE("=== QR Code: KNX:S:%s;P:%s ===", SN, oc_spake_get_password());

	#endif

	return ret;
}

/**
 * @brief returns the password, used from external application hence defined as
 * separate method.
 */
char* app_get_password(void)
{
	return PASSWORD;
}

/*

 8 x CoAP GET/PUT methods for data point resource.
 Setup defines urls (such as /p/1), resource types and other (see register
 resources). Initialization of the returned values are done from the global
 property values.

 @param request the request representation.
 @param interfaces the interface used for this call
 @param user_data the user data.

 @note The (generic) callback 'call' handler uses always above defined 3
 parameters, provide them even if not used

*/

void get_switch_1(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
	(void) user_data;
	(void) interfaces;

	bool error_state = true;

	PRINT("-- Begin GET %s Control at %s ", _1_name, _1_url_value);

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}

	oc_device_info_t* device = oc_core_get_device_info(0);

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

			PRINT("Query Parameter: %.*s", (int) m_len, m);

			oc_init_query_iterator();

			// check query parameter
			while (oc_iterate_query(request, &m_key, &m_key_len, &m, &m_len) != -1)
			{
				// unique identifier
				if (strncmp(m, "id", m_len) == 0 || strncmp(m, "*", m_len) == 0)
				{
					// knx://sn: + max len SN + path + \0 = ~ 65
					char serial_number[65];
					(void) snprintf(serial_number, 65, "knx://sn:%s%s",
													oc_string(device->serialnumber),
													oc_string(request->resource->uri));
					oc_rep_i_set_text_string(root, 0, serial_number);

					error_state = false;
				}
				// value
				if (strncmp(m, "value", m_len) == 0 || strncmp(m, "*", m_len) == 0)
				{
					oc_rep_text_set_int(root, value, g_OnOff_1);

					error_state = false;
				}
				// resource types
				if (strncmp(m, "rt", m_len) == 0 || strncmp(m, "*", m_len) == 0)
				{
					oc_rep_text_set_text_string(root, rt, _1_dpa_value_short);

					error_state = false;
				}
				// interfaces (array of text strings)
				if (strncmp(m, "if", m_len) == 0 || strncmp(m, "*", m_len) == 0)
				{
					// key
					oc_rep_set_key(oc_rep_object(root), "if");

					// one if type only
					oc_rep_begin_array(oc_rep_object(root), if_types);
					oc_rep_add_text_string(if_types, _1_if_value);
					oc_rep_end_array(oc_rep_object(root), if_types);

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
					int index = oc_core_find_group_object_table_url(oc_string(request->resource->uri));
					if (index > -1)
					{
						oc_group_object_table_t* got_table_entry =
							oc_core_get_group_object_table_entry(index);
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
					oc_rep_text_set_text_string(root, desc, _1_des);

					error_state = false;
				}
			}
		}
		else
		{ // ... no query parameter 'm' present at all, set value
			oc_rep_i_set_boolean(root, 1, g_OnOff_1);

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

	// wrong device, cbor error or unknown 'm' query parameter value(s)
	if (error_state)
		oc_prepare_no_format_response_no_payload(request, OC_STATUS_NOT_FOUND);
	else
		oc_prepare_cbor_response(request, OC_STATUS_OK);

	PRINT("-- End GET %s Control at %s ", _1_name, _1_url_value);
}

void get_switch_2(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
	(void) user_data;
	(void) interfaces;

	bool error_state = true;

	PRINT("-- Begin GET %s Control at %s ", _2_name, _2_url_value);

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}

	oc_device_info_t* device = oc_core_get_device_info(0);

	// open CBOR
	oc_rep_begin_root_object();

	if (device != NULL)
	{
		if (oc_query_value_exists(request, "m") !=
				-1)
		{ // ... query parameter 'm' is present, check the various values

			char* m;
			char* m_key;
			size_t m_key_len;
			size_t m_len = oc_get_query_value(request, "m", &m);

			PRINT("Query Parameter: %.*s", (int) m_len, m);

			oc_init_query_iterator();

			// check query parameter
			while (oc_iterate_query(request, &m_key, &m_key_len, &m, &m_len) != -1)
			{
				// unique identifier
				if (strncmp(m, "id", m_len) == 0 || strncmp(m, "*", m_len) == 0)
				{
					// knx://sn: + max len SN + path + \0 = ~ 65
					char serial_number[65];
					(void) snprintf(serial_number, 65, "knx://sn:%s%s",
													oc_string(device->serialnumber),
													oc_string(request->resource->uri));
					oc_rep_i_set_text_string(root, 0, serial_number);

					error_state = false;
				}
				// value
				if (strncmp(m, "value", m_len) == 0 || strncmp(m, "*", m_len) == 0)
				{
					oc_rep_text_set_int(root, value, g_OnOff_2);

					error_state = false;
				}
				// resource types
				if (strncmp(m, "rt", m_len) == 0 || strncmp(m, "*", m_len) == 0)
				{
					oc_rep_text_set_text_string(root, rt, _2_dpa_value_short);

					error_state = false;
				}
				// interfaces (array of text strings)
				if (strncmp(m, "if", m_len) == 0 || strncmp(m, "*", m_len) == 0)
				{
					// key
					oc_rep_set_key(oc_rep_object(root), "if");

					// one if type only
					oc_rep_begin_array(oc_rep_object(root), if_types);
					oc_rep_add_text_string(if_types, _2_if_value);
					oc_rep_end_array(oc_rep_object(root), if_types);

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
					int index = oc_core_find_group_object_table_url(
						oc_string(request->resource->uri));
					if (index > -1)
					{
						oc_group_object_table_t* got_table_entry =
							oc_core_get_group_object_table_entry(index);
						if (got_table_entry)
						{
							oc_rep_set_int_array(root, ga, got_table_entry->ga,
																	 got_table_entry->ga_len);
						}
					}
					error_state = false;
				}
				// description
				if (strncmp(m, "desc", m_len) == 0 || strncmp(m, "*", m_len) == 0)
				{
					oc_rep_text_set_text_string(root, desc, _2_des);

					error_state = false;
				}
			}
		}
		else
		{ // ... no query parameter 'm' present at all, set value
			oc_rep_i_set_boolean(root, 1, g_OnOff_2);

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

	// wrong device, cbor error or unknown 'm' query parameter value(s)
	if (error_state)
		oc_prepare_no_format_response_no_payload(request, OC_STATUS_NOT_FOUND);
	else
		oc_prepare_cbor_response(request, OC_STATUS_OK);

	PRINT("-- End GET %s Control at %s ", _2_name, _2_url_value);
}

void get_switch_3(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
	(void) user_data;
	(void) interfaces;

	bool error_state = true;

	PRINT("-- Begin GET %s Control at %s ", _3_name, _3_url_value);

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}

	oc_device_info_t* device = oc_core_get_device_info(0);

	// open CBOR
	oc_rep_begin_root_object();

	if (device != NULL)
	{
		if (oc_query_value_exists(request, "m") !=
				-1)
		{ // ... query parameter 'm' is present, check the various values

			char* m;
			char* m_key;
			size_t m_key_len;
			size_t m_len = oc_get_query_value(request, "m", &m);

			PRINT("Query Parameter: %.*s", (int) m_len, m);

			oc_init_query_iterator();

			// check query parameter
			while (oc_iterate_query(request, &m_key, &m_key_len, &m, &m_len) != -1)
			{
				// unique identifier
				if (strncmp(m, "id", m_len) == 0 || strncmp(m, "*", m_len) == 0)
				{
					// knx://sn: + max len SN + path + \0 = ~ 65
					char serial_number[65];
					(void) snprintf(serial_number, 65, "knx://sn:%s%s",
													oc_string(device->serialnumber),
													oc_string(request->resource->uri));
					oc_rep_i_set_text_string(root, 0, serial_number);

					error_state = false;
				}
				// value
				if (strncmp(m, "value", m_len) == 0 || strncmp(m, "*", m_len) == 0)
				{
					oc_rep_text_set_int(root, value, g_OnOff_3);

					error_state = false;
				}
				// resource types
				if (strncmp(m, "rt", m_len) == 0 || strncmp(m, "*", m_len) == 0)
				{
					oc_rep_text_set_text_string(root, rt, _3_dpa_value_short);

					error_state = false;
				}
				// interfaces (array of text strings)
				if (strncmp(m, "if", m_len) == 0 || strncmp(m, "*", m_len) == 0)
				{
					// key
					oc_rep_set_key(oc_rep_object(root), "if");

					// one if type only
					oc_rep_begin_array(oc_rep_object(root), if_types);
					oc_rep_add_text_string(if_types, _3_if_value);
					oc_rep_end_array(oc_rep_object(root), if_types);

					error_state = false;
				}
				// dpt
				if (strncmp(m, "dpt", m_len) == 0 || strncmp(m, "*", m_len) == 0)
				{
					oc_rep_text_set_text_string(root, dpt,
																			oc_string(request->resource->dpt));

					error_state = false;
				}
				// ga
				if (strncmp(m, "ga", m_len) == 0 || strncmp(m, "*", m_len) == 0)
				{
					int index = oc_core_find_group_object_table_url(
						oc_string(request->resource->uri));
					if (index > -1)
					{
						oc_group_object_table_t* got_table_entry =
							oc_core_get_group_object_table_entry(index);
						if (got_table_entry)
						{
							oc_rep_set_int_array(root, ga, got_table_entry->ga,
																	 got_table_entry->ga_len);
						}
					}
					error_state = false;
				}
				// description
				if (strncmp(m, "desc", m_len) == 0 || strncmp(m, "*", m_len) == 0)
				{
					oc_rep_text_set_text_string(root, desc, _3_des);

					error_state = false;
				}
			}
		}
		else
		{ // ... no query parameter 'm' present at all, set value
			oc_rep_i_set_int(root, 1, g_OnOff_3);

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

	// wrong device, cbor error or unknown 'm' query parameter value(s)
	if (error_state)
		oc_prepare_no_format_response_no_payload(request, OC_STATUS_NOT_FOUND);
	else
		oc_prepare_cbor_response(request, OC_STATUS_OK);

	PRINT("-- End GET %s Control at %s ", _3_name, _3_url_value);
}

void put_switch_1(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
	(void) interfaces;
	(void) user_data;

	PRINT("-- Begin PUT %s Control at %s ", _1_name, _1_url_value);

	// handle the different requests, here only included as example to
	// identify if extra data needs to be processed in the endpoint
	if (oc_is_redirected_request_from(request) > -1)
	{
		PRINT("redirected_request %.*s", (int) request->uri_path_len,
					request->uri_path);
	}

	// sets the pointer to the (/k or /p) handed over object
	const oc_rep_t* rep = request->request_payload;
	bool error_state = true;

	// loop over object
	while (rep)
	{
		// this EP accepts only a bool, a faulty construct such as {1: 2, 1: true}
		// may need to be skipped
		if (rep->iname == 1 && rep->type == OC_REP_BOOL)
		{
			PRINT("put_switch_1 received : %d", rep->value.boolean);
			g_OnOff_1 = rep->value.boolean;
			error_state = false;
			break;
		}
		rep = rep->next;
	}

	// correct data retrieved
	if (!error_state)
	{
		// inform the stack on status
		oc_prepare_no_format_response_no_payload(request, OC_STATUS_CHANGED);

		// update the status information
		if (g_fault_OnOff_1 == false)
		{
			PRINT("No Fault update feedback to %d'", g_OnOff_1);
			g_InfoOnOff_1 = g_OnOff_1;
		}
		else
		{
			PRINT("Fault'");
			g_InfoOnOff_1 = false;
		}

		// this is the 'simple' option to trigger a status on a specific EP
		PRINT("Send status to %s with flag: 'w'", _1_url_status);
		oc_do_s_mode_with_scope(5, _1_url_status, "w");

		PRINT("-- End PUT %s Control at %s ", _1_name, _1_url_value);
		return;
	}

	// bad request status
	oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
	PRINT("-- End PUT %s at %s ", _1_name, _1_url_value);
}

void put_switch_2(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
	(void) interfaces;
	(void) user_data;

	PRINT("-- Begin PUT %s Control at %s ", _2_name, _2_url_value);

	// handle the different requests, here only included as example to
	// identify if extra data needs to be processed in the endpoint
	if (oc_is_redirected_request_from(request) > -1)
	{
		PRINT("redirected_request %.*s", (int) request->uri_path_len,
					request->uri_path);
	}

	// sets the pointer to the (/k or /p) handed over object
	const oc_rep_t* rep = request->request_payload;
	bool error_state = true;

	// loop over object
	while (rep)
	{
		// this EP accepts only a bool, a faulty construct such as {1: 2, 1: true}
		// may need to be skipped
		if (rep->iname == 1 && rep->type == OC_REP_BOOL)
		{
			PRINT("put_switch_2 received : %d", rep->value.boolean);
			g_OnOff_1 = rep->value.boolean;
			error_state = false;
			break;
		}
		rep = rep->next;
	}

	// correct data retrieved
	if (!error_state)
	{
		// inform the stack on status
		oc_prepare_no_format_response_no_payload(request, OC_STATUS_CHANGED);

		// update the status information
		if (g_fault_OnOff_2 == false)
		{
			PRINT("No Fault update feedback to %d'", g_OnOff_2);
			g_InfoOnOff_2 = g_OnOff_2;
		}
		else
		{
			PRINT("Fault'");
			g_InfoOnOff_2 = false;
		}

		// this is the 'simple' option to trigger a status on a specific EP
		PRINT("Send status to %s with flag: 'w'", _2_url_status);
		oc_do_s_mode_with_scope(5, _2_url_status, "w");

		PRINT("-- End PUT %s Control at %s ", _2_name, _2_url_value);
		return;
	}

	// bad request status
	oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
	PRINT("-- End PUT %s at %s ", _2_name, _2_url_value);
}

void put_switch_3(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
	(void) interfaces;
	(void) user_data;

	PRINT("-- Begin PUT %s Control at %s ", _3_name, _3_url_value);

	// handle the different requests, here only included as example to
	// identify if extra data needs to be processed in the endpoint
	if (oc_is_redirected_request_from(request) > -1)
	{
		PRINT("redirected_request %.*s", (int) request->uri_path_len,
					request->uri_path);
	}

	// sets the pointer to the (/k /p) handed over object
	oc_rep_t* rep = request->request_payload;
	bool error_state = true;

	// loop over object
	while (rep)
	{
		// this EP accepts only a bool, a faulty construct such as {1: 2, 1: true}
		// may need to be skipped
		if (rep->iname == 1 && rep->type == OC_REP_INT)
		{
			PRINT("-- put_OnOff_3 received : %lld", rep->value.integer);
			g_OnOff_3 = (uint16_t) rep->value.integer;
			error_state = false;
			break;
		}
		rep = rep->next;
	}

	if (!error_state)
	{
		oc_prepare_no_format_response_no_payload(request, OC_STATUS_CHANGED);
		/* update the status information of InfoOnOff_3*/
		if (g_fault_OnOff_3 == false)
		{
			PRINT("-- No Fault update feedback to %d'", g_OnOff_3);
			/* no fault hence update the feedback with the current state of the
			 * actuator */
			g_InfoOnOff_3 = g_OnOff_3;
		}
		else
		{
			/* fault hence update the feedback with "false" */
			PRINT("-- Fault'");
			g_InfoOnOff_3 = false;
		}
		/* send the status information InfoOnOff_3 to '/p/6' with flag 'w' */
		PRINT("Send status to %s with flag: 'w'", _3_url_status);
		oc_do_s_mode_with_scope(5, _3_url_status, "w");

		PRINT("-- End PUT %s Control at %s ", _3_name, _3_url_value);
		return;
	}

	// bad request status
	oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
	PRINT("-- End PUT %s at %s ", _3_name, _3_url_value);
}

void get_status_1(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
	(void) user_data;
	(void) interfaces;

	bool error_state = true;

	PRINT("-- Begin GET %s Status at %s ", _1_name, _1_url_status);

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}

	oc_device_info_t* device = oc_core_get_device_info(0);

	// open CBOR
	oc_rep_begin_root_object();

	if (device != NULL)
	{
		if (oc_query_value_exists(request, "m") != -1)
		{ // ... query parameter 'm' is present, check the various values

			char* m;
			char* m_key;
			size_t m_key_len;
			size_t m_len = oc_get_query_value(request, "m", &m);

			PRINT("Query Parameter: %.*s", (int) m_len, m);

			oc_init_query_iterator();

			// check query parameter
			while (oc_iterate_query(request, &m_key, &m_key_len, &m, &m_len) != -1)
			{
				// unique identifier
				if (strncmp(m, "id", m_len) == 0 || strncmp(m, "*", m_len) == 0)
				{
					// knx://sn: + max len SN + path + \0 = ~ 65
					char serial_number[65];
					(void) snprintf(serial_number, 65, "knx://sn:%s%s",
													oc_string(device->serialnumber),
													oc_string(request->resource->uri));
					oc_rep_i_set_text_string(root, 0, serial_number);

					error_state = false;
				}
				// value
				if (strncmp(m, "value", m_len) == 0 || strncmp(m, "*", m_len) == 0)
				{
					oc_rep_text_set_int(root, value, g_InfoOnOff_1);

					error_state = false;
				}
				// resource types
				if (strncmp(m, "rt", m_len) == 0 || strncmp(m, "*", m_len) == 0)
				{
					oc_rep_text_set_text_string(root, rt, _1_dpa_status_short);

					error_state = false;
				}
				// interfaces (array of text strings)
				if (strncmp(m, "if", m_len) == 0 || strncmp(m, "*", m_len) == 0)
				{
					// key
					oc_rep_set_key(oc_rep_object(root), "if");

					// one if type only
					oc_rep_begin_array(oc_rep_object(root), if_types);
					oc_rep_add_text_string(if_types, _1_if_status);
					oc_rep_end_array(oc_rep_object(root), if_types);

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
					int index = oc_core_find_group_object_table_url(oc_string(request->resource->uri));
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
					oc_rep_text_set_text_string(root, desc, _1_des);

					error_state = false;
				}
			}
		}
		else
		{ // ... no query parameter 'm' present at all, set value
			oc_rep_i_set_int(root, 1, g_InfoOnOff_1);

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

	// wrong device, cbor error or unknown 'm' query parameter value(s)
	if (error_state)
		oc_prepare_no_format_response_no_payload(request, OC_STATUS_NOT_FOUND);
	else
		oc_prepare_cbor_response(request, OC_STATUS_OK);

	PRINT("-- End GET %s Status at %s ", _1_name, _1_url_status);
}

void get_status_2(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
	(void) user_data;
	(void) interfaces;

	bool error_state = true;

	PRINT("-- Begin GET %s Status at %s ", _2_name, _2_url_status);

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}

	oc_device_info_t* device = oc_core_get_device_info(0);

	// open CBOR
	oc_rep_begin_root_object();

	if (device != NULL)
	{
		if (oc_query_value_exists(request, "m") != -1)
		{ // ... query parameter 'm' is present, check the various values

			char* m;
			char* m_key;
			size_t m_key_len;
			size_t m_len = oc_get_query_value(request, "m", &m);

			PRINT("Query Parameter: %.*s", (int) m_len, m);

			oc_init_query_iterator();

			// check query parameter
			while (oc_iterate_query(request, &m_key, &m_key_len, &m, &m_len) != -1)
			{
				// unique identifier
				if (strncmp(m, "id", m_len) == 0 || strncmp(m, "*", m_len) == 0)
				{
					// knx://sn: + max len SN + path + \0 = ~ 65
					char serial_number[65];
					(void) snprintf(serial_number, 65, "knx://sn:%s%s",
													oc_string(device->serialnumber),
													oc_string(request->resource->uri));
					oc_rep_i_set_text_string(root, 0, serial_number);

					error_state = false;
				}
				// value
				if (strncmp(m, "value", m_len) == 0 || strncmp(m, "*", m_len) == 0)
				{
					oc_rep_text_set_int(root, value, g_InfoOnOff_2);

					error_state = false;
				}
				// resource types
				if (strncmp(m, "rt", m_len) == 0 || strncmp(m, "*", m_len) == 0)
				{
					oc_rep_text_set_text_string(root, rt, _2_dpa_status_short);

					error_state = false;
				}
				// interfaces (array of text strings)
				if (strncmp(m, "if", m_len) == 0 || strncmp(m, "*", m_len) == 0)
				{
					// key
					oc_rep_set_key(oc_rep_object(root), "if");

					// one if type only
					oc_rep_begin_array(oc_rep_object(root), if_types);
					oc_rep_add_text_string(if_types, _2_if_status);
					oc_rep_end_array(oc_rep_object(root), if_types);

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
					int index = oc_core_find_group_object_table_url(oc_string(request->resource->uri));
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
					oc_rep_text_set_text_string(root, desc, _2_des);

					error_state = false;
				}
			}
		}
		else
		{ // ... no query parameter 'm' present at all, set value
			oc_rep_i_set_int(root, 1, g_InfoOnOff_2);

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

	// wrong device, cbor error or unknown 'm' query parameter value(s)
	if (error_state)
		oc_prepare_no_format_response_no_payload(request, OC_STATUS_NOT_FOUND);
	else
		oc_prepare_cbor_response(request, OC_STATUS_OK);

	PRINT("-- End GET %s Status at %s ", _2_name, _2_url_status);
}

// parameters handling (empty)

void get_parameter_0(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
	(void) user_data;
	(void) interfaces;

	bool error_state = true;

	PRINT("-- Begin GET %s Control at %s ", _0_name, _0_url_value);

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}

	oc_device_info_t* device = oc_core_get_device_info(0);

	// open CBOR
	oc_rep_begin_root_object();

	if (device != NULL)
	{
		if (oc_query_value_exists(request, "m") != -1)
		{ // ... query parameter 'm' is present, check the various values

			char* m;
			char* m_key;
			size_t m_key_len;
			size_t m_len = oc_get_query_value(request, "m", &m);

			PRINT("Query Parameter: %.*s", (int) m_len, m);

			oc_init_query_iterator();

			// check query parameter
			while (oc_iterate_query(request, &m_key, &m_key_len, &m, &m_len) != -1)
			{
				// unique identifier
				if (strncmp(m, "id", m_len) == 0 || strncmp(m, "*", m_len) == 0)
				{
					// knx://sn: + max len SN + path + \0 = ~ 65
					char serial_number[65];
					(void) snprintf(serial_number, 65, "knx://sn:%s%s",
													oc_string(device->serialnumber),
													oc_string(request->resource->uri));
					oc_rep_i_set_text_string(root, 0, serial_number);

					error_state = false;
				}
				// value
				if (strncmp(m, "value", m_len) == 0 || strncmp(m, "*", m_len) == 0)
				{
					oc_rep_text_set_int(root, value, g_test_parameter);

					error_state = false;
				}
				// resource types
				if (strncmp(m, "rt", m_len) == 0 || strncmp(m, "*", m_len) == 0)
				{
					oc_rep_text_set_text_string(root, rt, _0_dpa_value_short);

					error_state = false;
				}
				// interfaces (array of text strings)
				if (strncmp(m, "if", m_len) == 0 || strncmp(m, "*", m_len) == 0)
				{
					// key
					oc_rep_set_key(oc_rep_object(root), "if");

					// one if type only
					oc_rep_begin_array(oc_rep_object(root), if_types);
					oc_rep_add_text_string(if_types, _0_if_value);
					oc_rep_end_array(oc_rep_object(root), if_types);

					error_state = false;
				}
				// dpt
				if (strncmp(m, "dpt", m_len) == 0 || strncmp(m, "*", m_len) == 0)
				{
					oc_rep_text_set_text_string(root, dpt,
																			oc_string(request->resource->dpt));

					error_state = false;
				}
				// ga
				if (strncmp(m, "ga", m_len) == 0 || strncmp(m, "*", m_len) == 0)
				{
					int index = oc_core_find_group_object_table_url(
						oc_string(request->resource->uri));
					if (index > -1)
					{
						oc_group_object_table_t* got_table_entry =
							oc_core_get_group_object_table_entry(index);
						if (got_table_entry)
						{
							oc_rep_set_int_array(root, ga, got_table_entry->ga,
																	 got_table_entry->ga_len);
						}
					}
					error_state = false;
				}
				// description
				if (strncmp(m, "desc", m_len) == 0 || strncmp(m, "*", m_len) == 0)
				{
					oc_rep_text_set_text_string(root, desc, _0_des);

					error_state = false;
				}
			}
		}
		else
		{ // ... no query parameter 'm' present at all, set value
			oc_rep_i_set_int(root, 1, g_test_parameter);

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

	// wrong device, cbor error or unknown 'm' query parameter value(s)
	if (error_state)
		oc_prepare_no_format_response_no_payload(request, OC_STATUS_NOT_FOUND);
	else
		oc_prepare_cbor_response(request, OC_STATUS_OK);

	PRINT("-- End GET %s Control at %s ", _0_name, _0_url_value);
}

void put_parameter_0(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
	(void) interfaces;
	(void) user_data;

	PRINT("-- Begin PUT %s Control at %s ", _0_name, _0_url_value);

	// handle the different requests, here only included as example to
	// identify if extra data needs to be processed in the endpoint
	if (oc_is_redirected_request_from(request) > -1)
	{
		PRINT("redirected_request %.*s", (int) request->uri_path_len,
					request->uri_path);
	}

	// sets the pointer to the (/k /p) handed over object
	oc_rep_t* rep = request->request_payload;
	bool error_state = true;

	// loop over object
	while (rep)
	{
		// this EP accepts only a bool, a faulty construct such as {1: 2, 1: true}
		// may need to be skipped
		if (rep->iname == 1 && rep->type == OC_REP_INT)
		{
			PRINT("-- put_test parameter received : %lld", rep->value.integer);
			g_test_parameter = (uint16_t) rep->value.integer;
			error_state = false;
			break;
		}
		rep = rep->next;
	}

	if (!error_state)
	{
		oc_prepare_no_format_response_no_payload(request, OC_STATUS_CHANGED);

		PRINT("-- End PUT %s Control at %s ", _0_name, _0_url_value);
		return;
	}

	// bad request status
	oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
	PRINT("-- End PUT %s at %s ", _0_name, _0_url_value);
}

/**
 * @brief register all the data point resources to the stack this function registers
 * all data point level resources:
 * each resource path is bind to a specific function for the supported methods:
 *   - GET (called from /p
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
	PRINT("Register channel 1 control/status resource");
	{
		oc_resource_t* soo1 = oc_new_resource(_1_name, _1_url_value, 1, 0);
		oc_resource_t* ioo1 = oc_new_resource(_1_name, _1_url_status, 1, 0);

		oc_resource_bind_resource_type(soo1, _1_dpa_value_long);
		oc_resource_bind_resource_type(ioo1, _1_dpa_status_long);

		oc_resource_bind_dpt(soo1, _1_dpt);
		oc_resource_bind_dpt(ioo1, _1_dpt);

		oc_resource_bind_content_type(soo1, APPLICATION_CBOR, CONTENT_NONE);
		oc_resource_bind_content_type(ioo1, APPLICATION_CBOR, CONTENT_NONE);

		oc_resource_set_function_block_instance(soo1, 1);
		oc_resource_set_function_block_instance(ioo1, 1);

		oc_resource_set_discoverable(soo1, true);
		oc_resource_set_discoverable(ioo1, true);

		oc_resource_set_observable(soo1, true);
		oc_resource_set_observable(ioo1, true);

		oc_resource_set_request_handler(soo1, OC_GET, get_switch_1, NULL, OC_ACL_O, OC_IF_O);
		oc_resource_set_request_handler(soo1, OC_PUT, put_switch_1, NULL, OC_ACL_I, OC_IF_I);
		oc_resource_set_request_handler(ioo1, OC_GET, get_status_1, NULL, OC_ACL_O, OC_IF_O);

		oc_add_resource(soo1);
		oc_add_resource(ioo1);
	}

	PRINT("Register channel 2 control/status resource");
	{
		oc_resource_t* soo2 = oc_new_resource(_2_name, _2_url_value, 1, 0);
		oc_resource_t* ioo2 = oc_new_resource(_2_name, _2_url_status, 1, 0);

		oc_resource_bind_resource_type(soo2, _2_dpa_value_long);
		oc_resource_bind_resource_type(ioo2, _2_dpa_status_long);

		oc_resource_bind_dpt(soo2, _2_dpt);
		oc_resource_bind_dpt(ioo2, _2_dpt);

		oc_resource_bind_content_type(soo2, APPLICATION_CBOR, CONTENT_NONE);
		oc_resource_bind_content_type(ioo2, APPLICATION_CBOR, CONTENT_NONE);

		oc_resource_set_function_block_instance(soo2, 1);
		oc_resource_set_function_block_instance(ioo2, 1);

		oc_resource_set_discoverable(soo2, true);
		oc_resource_set_discoverable(ioo2, true);

		oc_resource_set_observable(soo2, true);
		oc_resource_set_observable(ioo2, true);

		oc_resource_set_request_handler(soo2, OC_GET, get_switch_2, NULL, OC_ACL_O, OC_IF_O);
		oc_resource_set_request_handler(soo2, OC_PUT, put_switch_2, NULL, OC_ACL_I, OC_IF_I);
		oc_resource_set_request_handler(ioo2, OC_GET, get_status_2, NULL, OC_ACL_O, OC_IF_O);

		oc_add_resource(soo2);
		oc_add_resource(ioo2);
	}

	PRINT("Register channel 3 control resource (no status)");
	{
		oc_resource_t* soo3 = oc_new_resource(_3_name, _3_url_value, 1, 0);

		oc_resource_bind_resource_type(soo3, _3_dpa_value_long);

		oc_resource_bind_dpt(soo3, _3_dpt);

		oc_resource_bind_content_type(soo3, APPLICATION_CBOR, CONTENT_NONE);

		oc_resource_set_function_block_instance(soo3, 1);

		oc_resource_set_discoverable(soo3, true);

		oc_resource_set_observable(soo3, true);

		oc_resource_set_request_handler(soo3, OC_GET, get_switch_3, NULL, OC_ACL_O, OC_IF_O);
		oc_resource_set_request_handler(soo3, OC_PUT, put_switch_3, NULL, OC_ACL_I, OC_IF_I);

		oc_add_resource(soo3);
	}

	PRINT("Register test parameter");
	{
		oc_resource_t* tp0 = oc_new_resource(_0_name, _0_url_value, 1, 0);

		oc_resource_bind_resource_type(tp0, _0_dpa_value_long);

		oc_resource_bind_dpt(tp0, _0_dpt);

		oc_resource_bind_content_type(tp0, APPLICATION_CBOR, CONTENT_NONE);

		oc_resource_set_function_block_instance(tp0, 1);

		oc_resource_set_discoverable(tp0, true);

		oc_resource_set_observable(tp0, true);

		oc_resource_set_request_handler(tp0, OC_GET, get_parameter_0, NULL, OC_ACL_D, OC_IF_D);
		oc_resource_set_request_handler(tp0, OC_PUT, put_parameter_0, NULL, OC_ACL_P, OC_IF_P);

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
	(void) device_index;
	(void) data;

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
	(void) device_index;
	(void) data;

	PRINT("host name callback called with host name: %s", oc_string(host_name));

	/*
	 * The application callback needs to handle a changed host name such as to
	 * announce it to a border router or local daemon.
	 */
}

static oc_event_callback_retval_t send_delayed_response(void* context)
{
	oc_separate_response_t* response = context;

	if (response->active)
	{
		oc_set_separate_response_buffer(response);
		oc_send_separate_response(response, OC_STATUS_CHANGED);
		PRINT("Delayed response sent");
	}
	else
	{
		PRINT("Delayed response NOT active");
	}

	return OC_EVENT_DONE;
}

/**
 * @brief software update callback
 *
 * @param device the device index
 * @param response the instance of an internal struct that is used to track the
 * state of the separate response
 * @param binary_size the full size of the binary
 * @param offset the offset of the image
 * @param payload the image data
 * @param len the length of the image data
 * @param data the user data
 */
void swu_cb(const size_t device, oc_separate_response_t* response,
						const size_t binary_size, const size_t offset, uint8_t* payload,
						const size_t len, void* data)
{
	(void) device;
	(void) binary_size;
	(void) data;

	char filename[] = "./downloaded.bin";
	PRINT("swu_cb %s block=%d size=%d ", filename, (int) offset, (int) len);

	FILE* write_ptr = fopen("downloaded_bin", "ab");
	const size_t n = fwrite(payload, sizeof(*payload), len, write_ptr);
	const size_t r = fclose(write_ptr);
	PRINT("written data: %llu, operation ok (=0): %llu", n, r);

	oc_set_delayed_callback(response, &send_delayed_response, 0);
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
	return strncpy(g_serial_number, serial_number, sizeof(g_serial_number)) !=
		NULL
		? 0
		: -1;
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
	(void) sprintf(storage, "./knx_iot_virtual_sa_%s", g_serial_number);
	PRINT("Current path is: '%s'", dir);
	oc_storage_config(storage);

	#else

	char storage[400];
	char dir[FILENAME_MAX] = "";
	GetCurrentDir(dir, FILENAME_MAX);
	PRINT("storage at 'knx_iot_virtual_sa_creds' "); // TODO
	oc_storage_config("./knx_iot_virtual_sa_creds"); // TODO

	#endif

	// initialize the 'application' runtime variables
	initialize_variables();

	// set the stack handler callbacks
	static oc_handler_t handler = {
		.init = app_init,                         // called always
		.signal_event_loop = signal_event_loop,   // called always
		.register_resources = register_resources, // called for a server
		.requests_entry = NULL
	}; // called for a client

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
void
signal_event_loop(void)
{
	#ifndef NO_MAIN
	pthread_mutex_lock(&mutex);
	pthread_cond_signal(&event_is_pending);
	pthread_mutex_unlock(&mutex);
	#endif /* NO_MAIN */
}
#endif

// used to run as standalone command line (with main) or embed it in parent code 
#ifndef NO_MAIN

/**
 * @brief handle Ctrl-C
 * @param signal the captured signal
 */
static void handle_signal(const int signal)
{
	(void) signal;
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
	InitializeCriticalSection(&critical_section);			// init , but not used in main actively
	InitializeConditionVariable(&event_is_pending);		// init
	(void) signal(SIGINT, handle_signal);							// install Ctrl-C handler
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

	PRINT("KNX-IOT server name : \"%s\"", APPLICATION_NAME);

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

	PRINT("Server '%s' is now running, waiting on incoming connections...", APPLICATION_NAME);

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