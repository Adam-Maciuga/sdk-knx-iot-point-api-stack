/*
 // Copyright (c) 2021-2022 Cascoda Ltd
 //
 // Licensed under the Apache License, Version 2.0 (the "License");
 // you may not use this file except in compliance with the License.
 // You may obtain a copy of the License at
 //
 //      http://www.apache.org/licenses/LICENSE-2.0
 //
 // Unless required by applicable law or agreed to in writing, software
 // distributed under the License is distributed on an "AS IS" BASIS,
 // WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 // See the License for the specific language governing permissions and
 // limitations under the License.
 */

#include "oc_api.h"
#include "oc_core_res.h"
#include "oc_knx.h"
#include "oc_knx_client.h"
#include "oc_knx_dev.h"
#include "oc_knx_fp.h"
#include "oc_knx_gm.h"        // only used if iot router is enabled
#include "oc_knx_sec.h"
#include "oc_main.h"
#include "oc_rep.h"
#include <oc_storage.h>
#include "api/oc_knx_helpers.h"
#include "oc_oscore_context.h"
#include "port/dns-sd.h"

#define __STDC_FORMAT_MACROS  // defined to use format specifiers also in C++

#ifdef OC_SPAKE
#include "security/oc_spake2plus.h"
#endif

#define FINGERPRINT_STORE "dev_knx_fingerprint"

 // ---------------------------Variables --------------------------------------

static bool g_ignore_smessage_from_self = false; // prevent to handle own send out messages 
static uint64_t g_fingerprint = 0;               // covers GO/PUB/SUB table and 'P' parameters
static oc_pase_t g_pase;
static oc_string_t g_idevid;
static oc_string_t g_ldevid;
static int valid_request = 0;

// ----------------------------------------------------------------------------

enum SpakeKeys
{
	SPAKE_ID = 0,
	SPAKE_SALT = 5,
	SPAKE_PW = 8, // For device handover, not implemented yet
	SPAKE_PA_SHARE_P = 10,
	SPAKE_PB_SHARE_V = 11,
	SPAKE_PBKDF2 = 12,
	SPAKE_CB_CONFIRM_V = 13,
	SPAKE_CA_CONFIRM_P = 14,
	SPAKE_RND = 15,
	SPAKE_IT = 16,
};

static int convert_cmd(char* cmd)
{
	#define RESTART_DEVICE 2
	#define RESET_DEVICE 1

	if (strncmp(cmd, "reset", strlen("reset")) == 0)
	{
		return RESET_DEVICE;
	}
	if (strncmp(cmd, "restart", strlen("restart")) == 0)
	{
		return RESTART_DEVICE;
	}

	OC_DBG("convert_cmd command not recognized: %s", cmd);
	return 0;
}

int oc_reset_device(const size_t device_index, const int reset_mode)
{
	PRINT("reset device: %d", reset_mode);

	// application preset callback handler 
	const oc_factory_presets_t* my_preset_cb = oc_get_factory_presets_cb();
	if (my_preset_cb && my_preset_cb->cb)
	{
		PRINT("PRE-set callback handler is called");
		my_preset_cb->cb(device_index, my_preset_cb->data);
	}

	// delete data
	oc_knx_device_storage_reset(device_index, reset_mode);

	// application reset callback handler 
	const oc_reset_t* my_reset_cb = oc_get_reset_cb();
	if (my_reset_cb && my_reset_cb->cb)
	{
		PRINT("RE-set callback handler is called");
		my_reset_cb->cb(device_index, reset_mode, my_reset_cb->data);
	}

	return 0;
}

/*
	payload example:
	{
		"api": {
			"version" : "1.0.0",
			"base": "/"
		}
	}
	note that base path and version cannot be set from outside, hence below used as fixed constants
*/
static void oc_core_knx_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;

	// this EP MUST support JSON in addition (KNX IoT specification clause 5.1.3)
	if (!oc_accept_header_is_ok(request, APPLICATION_JSON) && !oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}

	if (request->accept == APPLICATION_JSON)
	{
		// no begin/end object is needed, it raps only the raw content
		oc_rep_add_line_to_buffer("{\"api\": {\"base\": \"/\", \"version\": \"1.0.0\" }}");

		oc_prepare_json_response(request, OC_STATUS_OK);
	}
	else
	{
		oc_rep_begin_root_object();
		oc_rep_set_object(root, api);
		oc_rep_set_text_string(api, version, "1.0.0");
		oc_rep_set_text_string(api, base, "/");
		oc_rep_close_object(root, api);
		oc_rep_end_root_object();

		oc_prepare_cbor_response(request, OC_STATUS_OK);
	}
}

// cache device_index and reset value, original values may be void when callbacks are executed (due to clean up resources)
static size_t cached_device_index;
static int cached_value;

static oc_event_callback_retval_t reset(void* context)
{
	PRINT("reset device");

	// use cached value
	oc_reset_device(cached_device_index, cached_value);
	return OC_EVENT_DONE;
}

static oc_event_callback_retval_t restart(void* context)
{
	PRINT("restart device");

	// Specification demands
	// - reset a possible PRG mode
	// - terminate a possible PASE key
	// - apply (changed) configuration parameters latest after 30s

	// use cached value 
	oc_device_info_t* device = oc_core_get_device_info(cached_device_index);

	// PRG mode
	if (device == NULL)
	{
		OC_ERR("device not found %d", (int) cached_device_index);
	}
	else
	{
		device->pm = false;
	}

	// PASE key (check only one hit ...) 
	int auth_at_index_pase = auth_at_index_pase = oc_core_find_pase_entry(cached_device_index);
	if (auth_at_index_pase < 0)
	{
		PRINT("PASE key not found");
	}
	else
	{
		PRINT("PASE key invalidated");
		oc_at_delete_entry(cached_device_index, auth_at_index_pase); // delete from table
		oc_oscore_free_contexts_at_id(auth_at_index_pase);           // invalidate (usually the data are restored after startup) 
	}

	// CFG parameters
	oc_init_datapoints_at_initialization();

	// application restart callback handler 
	oc_restart_t* my_restart = oc_get_restart_cb();
	if (my_restart && my_restart->cb)
	{
		my_restart->cb(cached_device_index, my_restart->data);
	}

	return OC_EVENT_DONE;
}

/*

	JSON      CBOR
	value =   1       Unsigned    // erase codes for 'reset'
	cmd =     2       String      // 'restart', 'reset'
	status =  3       Unsigned
	code =    "code"  Unsigned
	time =    "time"  Unsigned

	CBOR payload example:
	{ 2: "restart" }
	{ 2: "reset", 1: <erase code> }
	<erase code>:
	- 2=m (delete all security parameters + network parameters)
	- 3=o (delete IA)
	- 7=m (delete all security parameters except with if.sec, don't delete network parameters)

*/
static void oc_core_knx_post_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;

	int value = -1; // JSON key
	int cmd = -1;   // JSON key

	// all values init to '0', 200 byte size is sufficient for request data
	char buffer[200] = { 0 };
	oc_rep_to_json(request->request_payload, buffer, 200, true);

	PRINT("oc_core_knx_post_handler with data %s", buffer);

	oc_rep_t* rep = request->request_payload;
	while (rep != NULL)
	{
		switch (rep->type)
		{ // note, type does not reflect a 1:1 meaning of the CBOR major types

			case OC_REP_STRING:
			{
				if (rep->iname == 2) // CBOR key
				{
					// the command
					cmd = convert_cmd(oc_string(rep->value.string));
				}
			} break;
			case OC_REP_INT:
			{
				if (rep->iname == 1) // CBOR key
				{
					// the value 
					value = (int) rep->value.integer;
				}
			} break;
			default:
				break;
		}
		rep = rep->next;
	}

	PRINT("cmd: %d value: %d", cmd, value);

	const size_t device_index = request->resource->device;

	if (cmd == RESTART_DEVICE)
	{
		// safe device# and '-1' value (restart don't use a value)
		// device_index may be void when using the data again (cleanup resources)
		cached_device_index = device_index;
		cached_value = value;

		oc_set_delayed_callback_ms(NULL, restart, 100);
		PRINT("oc_core_knx_post_handler - end, restart");		return;
	}
	if (cmd == RESET_DEVICE)
	{
		// safe device# and 'erase code' value (reset may use a value)
		// device may be void when using the data again 
		cached_device_index = device_index;
		cached_value = value;

		oc_set_delayed_callback_ms(NULL, reset, 100);

		// Before executing the reset function, the KNX IoT device MUST return a
		// response with CoAP response code 2.04 CHANGED and with payload containing
		// Error Code and Process Time in seconds as defined for the Response
		// to a Master Reset Request for KNX Classic devices, see [09].

		// check erase code value for response error (0:no error, 2:unsupported erase code, others not used here)
		const unsigned int response_code = value == 2 || value == 3 || value == 7 ? 0 : 2;
		// response time (fixed value, need to be set in relation of the used hardware)
		const unsigned int response_time = 2;

		oc_rep_begin_root_object();
		oc_rep_set_int(root, code, response_code);
		oc_rep_set_int(root, time, response_time);
		oc_rep_end_root_object();

		oc_prepare_cbor_response(request, OC_STATUS_CHANGED);
		PRINT("oc_core_knx_post_handler - end, reset");
		return;
	}

	PRINT("invalid command");
	oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(knx, knx_fp_g, 0, "/.well-known/knx",
																		 APPLICATION_LINK_FORMAT, CONTENT_NONE,
																		 OC_DISCOVERABLE,
																		 oc_core_knx_get_handler, OC_ACL_NONE, OC_IF_NONE, // unsecured EP
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 oc_core_knx_post_handler, OC_ACL_C | OC_ACL_SEC, OC_IF_C | OC_IF_SEC,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_SIZE_ZERO());

void oc_create_knx_resource(int resource_idx, size_t device)
{
	OC_DBG("create /knx resources");
	oc_core_populate_resource(resource_idx, device, "/.well-known/knx",
														APPLICATION_LINK_FORMAT, CONTENT_NONE,
														OC_DISCOVERABLE,
														oc_core_knx_get_handler, 0, oc_core_knx_post_handler,
														0, 0);
}



oc_lsm_state_t oc_a_lsm_state(size_t device_index)
{
	oc_device_info_t* device = oc_core_get_device_info(device_index);
	if (device == NULL)
	{
		OC_ERR("device not found %d", (int) device_index);
		return LSM_S_UNLOADED;
	}

	return device->lsm_s;
}

/*
 * function will store the new state
 */
int
oc_a_lsm_set_state(size_t device_index, oc_lsm_state_t new_state)
{
	oc_device_info_t* device = oc_core_get_device_info(device_index);
	if (device == NULL)
	{
		OC_ERR("device not found %d", (int) device_index);
		return -1;
	}
	device->lsm_s = new_state;

	oc_storage_write(KNX_STORAGE_LSM, (uint8_t*) &device->lsm_s, sizeof(device->lsm_s));

	return 0;
}

const char*
oc_core_get_lsm_state_as_string(oc_lsm_state_t lsm)
{
	// states
	if (lsm == LSM_S_UNLOADED)
	{
		return "unloaded";
	}
	if (lsm == LSM_S_LOADED)
	{
		return "loaded";
	}
	if (lsm == LSM_S_LOADING)
	{
		return "loading";
	}
	if (lsm == LSM_S_UNLOADING)
	{
		return "unloading";
	}
	if (lsm == LSM_S_LOADCOMPLETING)
	{
		return "load completing";
	}

	return "";
}

const char* oc_core_get_lsm_event_as_string(oc_lsm_event_t lsm)
{
	// commands
	if (lsm == LSM_E_NOP)
	{
		return "nop";
	}
	if (lsm == LSM_E_STARTLOADING)
	{
		return "startLoading";
	}
	if (lsm == LSM_E_LOADCOMPLETE)
	{
		return "loadComplete";
	}
	if (lsm == LSM_E_UNLOAD)
	{
		return "unload";
	}

	return "";
}


// LSM handler, stores the new state and returns if event was OK (true)
bool oc_lsm_event_to_state(oc_lsm_event_t lsm_e, size_t device_index)
{
	if (lsm_e == LSM_E_NOP)
	{
		// do nothing
		return true;
	}
	if (lsm_e == LSM_E_STARTLOADING)
	{
		oc_a_lsm_set_state(device_index, LSM_S_LOADING);
		return true;
	}
	if (lsm_e == LSM_E_LOADCOMPLETE)
	{
		oc_a_lsm_set_state(device_index, LSM_S_LOADED);
		return true;
	}
	if (lsm_e == LSM_E_UNLOAD)
	{

		// LSM (first to prevent any runtime messaging in/out)
		oc_a_lsm_set_state(device_index, LSM_S_UNLOADED);

		// do a reset like erase code 2 but not the AT table, ia, iid, fid -> EITT test 
		oc_delete_group_tables();
		oc_delete_group_object_table();

		return true;
	}
	return false;
}

static void oc_core_a_lsm_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;

	PRINT("oc_core_a_lsm_get_handler - start");

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}

	// get from the request the addressed device as index
	size_t device_index = request->resource->device;
	oc_device_info_t* device = oc_core_get_device_info(device_index);

	if (device == NULL)
	{
		oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);		return;
	}

	oc_lsm_state_t lsm = oc_a_lsm_state(device_index);

	oc_rep_begin_root_object();
	oc_rep_i_set_int(root, 3, lsm);
	oc_rep_end_root_object();

	oc_prepare_cbor_response(request, OC_STATUS_OK);

	PRINT("oc_core_a_lsm_get_handler - end");
}

static void oc_core_a_lsm_post_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;
	oc_rep_t* rep = NULL;

	PRINT("oc_core_lsm_post_handler - start");

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{		return;
	}

	// get from the request the addressed device as index
	size_t device_index = request->resource->device;
	oc_device_info_t* device = oc_core_get_device_info(device_index);

	if (device == NULL)
	{
		PRINT("oc_core_lsm_post_handler - end");
		oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
		return;
	}

	// default setting if nothing will be found
	int event = LSM_E_NOP;

	rep = request->request_payload;
	while (rep != NULL)
	{
		if (rep->type == OC_REP_INT)
		{
			// cmd = CBOR KEY 2, status = int 
			if (rep->iname == 2)
			{
				event = (int) rep->value.integer;
				break;
			}
		}
		rep = rep->next;
	}

	PRINT("load event %d [%s]", event, oc_core_get_lsm_event_as_string(event));

	// LSM state changed correctly ?
	if (oc_lsm_event_to_state(event, device_index))
	{
		const oc_loadstate_t* my_cb = oc_get_lsm_change_cb();

		// application LSM mode callback handler
		if (my_cb && my_cb->cb)
		{
			my_cb->cb(device_index, oc_a_lsm_state(device_index), my_cb->data);
		}

		// if running ... 
		if (oc_is_device_in_runtime(device_index))
		{
			oc_register_group_multicasts();
			oc_init_datapoints_at_initialization();
			knx_publish_service(oc_string(device->serialnumber), device->iid, device->ia, device->pm);
		}

		// create response 
		oc_rep_new(request->response->response_buffer->buffer, (int) request->response->response_buffer->buffer_size);
		oc_rep_begin_root_object();
		oc_rep_i_set_int(root, 3, oc_a_lsm_state(device_index));
		oc_rep_end_root_object();

		// note that also on event 'NOP' a 'changed' is returned 
		oc_prepare_cbor_response(request, OC_STATUS_CHANGED);		return;
	}
	// invalid event
	oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(a_lsm, knx_spake, 0, "/a/lsm",
																		 APPLICATION_CBOR, CONTENT_NONE,
																		 OC_DISCOVERABLE,
																		 oc_core_a_lsm_get_handler, OC_ACL_C, OC_IF_C,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 oc_core_a_lsm_post_handler, OC_ACL_C, OC_IF_C,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_SIZE_ZERO());

void oc_create_a_lsm_resource(int resource_idx, size_t device)
{
	OC_DBG("create /a/lsm resources");

	oc_core_populate_resource(resource_idx, device, "/a/lsm",
														APPLICATION_CBOR, CONTENT_NONE, OC_DISCOVERABLE, oc_core_a_lsm_get_handler,
														0, oc_core_a_lsm_post_handler, 0, 0);
}

static void oc_core_knx_k_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;

	PRINT("oc_core_knx_k_get_handler");

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}

	size_t device_index = request->resource->device;

	oc_device_info_t* device = oc_core_get_device_info(device_index);
	if (device == NULL)
	{
		oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);		return;
	}

	// { 4: "ia of device" }, see #32 of KNX clarifications

	oc_rep_begin_root_object();
	oc_rep_i_set_int(root, 4, device->ia);
	oc_rep_end_root_object();

	oc_prepare_cbor_response(request, OC_STATUS_OK);

	PRINT("oc_core_knx_k_get_handler - done");
}

// { sia: 5678, s: {st: write, ga: 1, value: 100 }}, note : value can be anything incl. a string
static void oc_core_knx_k_post_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;

	PRINT("oc_core_knx_k_post_handler - start");

	// define and clear a temporary object notification 
	oc_group_object_notification_t received_notification =
	{ {NULL,0,NULL},
		0,
	 {NULL,0,NULL},
		0
	};

	// debugging
	PRINT("decoded payload: ");
	oc_print_rep_as_json(request->request_payload, true);

	// debugging
	PRINT("payload Size: %d", (int) request->_payload_len);
	OC_LOGbytes_OSCORE(request->_payload, (int) request->_payload_len);

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}

	if (g_ignore_smessage_from_self)
	{
		// check if incoming message is from myself, If so, then return with bad request
		// Note that the same device can have multiple IP addresses, so all endpoints
		// for this device need to be compared against.

		oc_endpoint_t* origin = request->origin;

		if (origin != NULL)
		{
			PRINT("k post : origin of message:");
			PRINTipaddr(*origin);
		}

		oc_endpoint_t* my_ep = oc_connectivity_get_endpoints(0);
		oc_endpoint_t* ep_i = NULL;

		for (ep_i = my_ep; ep_i != NULL; ep_i = ep_i->next)
		{
			PRINTipaddr(*ep_i);

			if (oc_endpoint_compare_address(origin, ep_i) == 0)
			{
				if (origin->addr.ipv6.port == ep_i->addr.ipv6.port)
				{
					request->response->response_buffer->code = oc_status_code(OC_IGNORE);
					PRINT("same address and port: not handling message");					return;
				}
			}
		}
	}

	size_t device_index = request->resource->device;
	oc_device_info_t* device = oc_core_get_device_info(device_index);
	if (device == NULL)
	{
		oc_prepare_no_format_response_no_payload(request, OC_IGNORE);
		return;
	}

	// scan received payload 
	oc_rep_t* rep = request->request_payload;
	while (rep != NULL)
	{
		switch (rep->type)
		{
			case OC_REP_INT:
			{
				// sia  
				if (rep->iname == 4)
				{
					received_notification.sia = (uint32_t) rep->value.integer;
				}
			} break;
			case OC_REP_OBJECT:
			{
				// s map with st/ga/value
				oc_rep_t* object = rep->value.object;

				while (object != NULL)
				{
					switch (object->type)
					{
						case OC_REP_STRING:
						{
							// st
							if (object->iname == 6)
							{
								oc_free_string(&received_notification.st);
								oc_new_string(&received_notification.st, oc_string(object->value.string), oc_string_len(object->value.string));
							}
						} break;

						case OC_REP_INT:
						{
							// ga
							if (object->iname == 7)
							{
								received_notification.ga = (uint32_t) object->value.integer;
							}
						} break;
						default:
							break;
					}
					object = object->next;
				}
			} break;
			default:
				break;
		}
		rep = rep->next;
	}


	#ifdef OC_IOT_ROUTER
	// gateway functionality: call back for all s-mode calls
	oc_gateway_t* my_gw = oc_get_gateway_cb();
	if (my_gw != NULL && my_gw->cb)
	{
		if (my_gw->data)
		{
			// call the gateway function
			my_gw->cb(device_index, ip_address, &g_received_notification,
								my_gw->data);
		}
		else
		{
			// if data is NULL, pass json payload as data
			char buffer[300];
			memset(buffer, 300, 0);
			oc_rep_to_json(request->request_payload, (char*) &buffer, 300, true);

			my_gw->cb(device_index, ip_address, &g_received_notification, buffer);
		}
	}
	#endif

	if (oc_is_device_in_runtime(device_index) == false)
	{
		PRINT("Device not in runtime state:%d - ignore message", device->lsm_s);
		oc_prepare_no_format_response_no_payload(request, OC_IGNORE);
		return;
	}

	// debugging ... 
	char ip_address[100];
	SNPRINTFipaddr(ip_address, 100 - 1, *request->origin);
	// handle the request loop over the group addresses of the /fp/r (recipient table)
	PRINT("k : origin:%s sia: %u ga: %u st: %s", ip_address, received_notification.sia, received_notification.ga, oc_string_checked(received_notification.st));

	// set request-flags, only one out of a/w/r is possible
	oc_cflag_mask_t request_type = OC_CFLAG_NONE;

	if (strcmp(oc_string_checked(received_notification.st), "w") == 0)
	{
		// write, any ga => cflags = w -> overwrite object value
		request_type = OC_CFLAG_WRITE;
	}
	else if (strcmp(oc_string_checked(received_notification.st), "a") == 0)
	{
		// update, any ga => cflags = w -> overwrite object value
		request_type = OC_CFLAG_UPDATE;
	}
	else if (strcmp(oc_string_checked(received_notification.st), "r") == 0)
	{
		/// read, any ga => cflags = r -> read object value (group speaker principle, one 'r' flag should be set ...)
		request_type = OC_CFLAG_READ;
	}

	// get GO with that GA included (one out of 1...n of GO array)
	int go_table_index = oc_core_find_first_group_object_table_index(received_notification.ga);

	PRINT("k : index %d", go_table_index);
	if (go_table_index == -1)
	{
		// if nothing is found (initially) then ignore
		oc_prepare_no_format_response_no_payload(request, OC_IGNORE);
		return;
	}

	// EACH application callback handler gets an own copy of the request + new response buffer  
	oc_request_t new_request = { 0 };
	oc_response_buffer_t response_buffer = { 0 };
	oc_response_t response_obj = { 0 };

	// define summary callback handler status
	oc_status_t summary_handler_status = OC_STATUS_OK;

	// internal callback handler, updates all to a GO index assigned GAs
	while (go_table_index != -1)
	{
		// get href for the GO index  
		oc_string_t go_href = oc_core_get_href_from_group_object_table_index(go_table_index);

		PRINT("k : url  %s", oc_string_checked(go_href));

		// device EP present (sanity check, GO without href, product problem)?
		if (oc_string_len(go_href) > 0)
		{
			// len > 0 = no error, get the application resource to do the fake post on
			const oc_resource_t* my_resource = oc_ri_get_app_resource_by_uri(oc_string(go_href), oc_string_len(go_href), device_index);
			if (!my_resource)
			{
				// TODO silently ignored on unicast ? (multicast anyhow = IGNORE)
				return;
			}

			// get c-flags
			oc_cflag_mask_t cflags = oc_core_group_object_table_cflag_entries(go_table_index);

			// if corresponding c-flag and the (only one possible) request service type are set ...
			request_type &= cflags;

			if (request_type & OC_CFLAG_WRITE)
			{
				PRINT("WRITE: index %d handled due to flags %d", go_table_index, cflags);

				// call application PUT handler (for /k only a POST is defined,
				// application handler needs to end up in one (PUT) handler for /k and /p)  
				if (my_resource->put_handler.cb)
				{
					// copy request to new request
					oc_ri_new_request_from_request(&new_request, request, &response_buffer, &response_obj);

					// sets the payload pointer to the 'value' OBJECT,
					// used by /p and /k that calls the same application callback handlers 
					new_request.request_payload = oc_s_mode_get_value_object(request);

					// set src to /k for a redirect check in application callback handler 
					new_request.uri_path = "/k";
					new_request.uri_path_len = 2;

					// use new request (not received one with POST), user data are possible
					my_resource->put_handler.cb(&new_request, iface_mask, my_resource->put_handler.user_data);

					// collect the max 'bad' status code 
					collect_and_rank_status(new_request.response->response_buffer->code, &summary_handler_status);
				}
			}
			if (request_type & OC_CFLAG_UPDATE)
			{
				PRINT("UPDATE: index %d handled due to flags %d", go_table_index, cflags);

				// call application PUT handler (for /k only a POST is defined,
				// application handler needs to end up in one (PUT) handler for /k and /p) 
				if (my_resource->put_handler.cb)
				{
					// copy request to new request
					oc_ri_new_request_from_request(&new_request, request, &response_buffer, &response_obj);

					// sets the payload pointer to the 'value' OBJECT,
					// used by /p and /k that calls the same application callback handlers 
					new_request.request_payload = oc_s_mode_get_value_object(request);

					// set src to /k for a redirect check in application callback handler 
					new_request.uri_path = "/k";
					new_request.uri_path_len = 2;

					// use new request (not received one with POST), user data are possible
					my_resource->put_handler.cb(&new_request, iface_mask, my_resource->put_handler.user_data);

					// collect the max 'bad' status code 
					collect_and_rank_status(new_request.response->response_buffer->code, &summary_handler_status);
				}
			}
			if (request_type & OC_CFLAG_READ)
			{
				PRINT("READ: index %d handled due to flags %d", go_table_index, cflags);

				if (my_resource->get_handler.cb)
				{
					// copy request to new request
					oc_ri_new_request_from_request(&new_request, request, &response_buffer, &response_obj);

					// set src to /k for a redirect check in application callback handler 
					new_request.uri_path = "/k";
					new_request.uri_path_len = 2;

					// use new request (not received one with POST), user data are not possible for read
					my_resource->get_handler.cb(&new_request, iface_mask, NULL);
				}

				// collect the max 'bad' status code 
				collect_and_rank_status(new_request.response->response_buffer->code, &summary_handler_status);

				// send from uc/mc read request the read responses in multicast as POST with st='a'
				// data are prepared by application callback handler, c-flag transmit will be ignored 
				#ifdef OC_USE_MULTICAST_SCOPE_2
				oc_do_s_mode_with_scope_no_check(2, oc_string(go_href), "a");
				#endif
				oc_do_s_mode_with_scope_no_check(5, oc_string(go_href), "a");
			}
		}

		// get the next index in the table to get the url from
		go_table_index = oc_core_find_next_group_object_table_index(received_notification.ga, go_table_index);
	}

	if (request->origin && request->origin->flags & MULTICAST)
	{// multicast request: don't send anything ELSE back
	 // if configured in PUB table a read was answered with multicast beforehand

		PRINT("multicast - not sending response");
		oc_prepare_no_format_response_no_payload(request, OC_IGNORE);
	}
	else
	{// unicast request: send status back
	 // if configured in PUB table a read was answered with multicast beforehand

		PRINT("unicast - sending response");
		oc_prepare_no_format_response_no_payload(request, summary_handler_status);
	}

	PRINT("oc_core_knx_k_post_handler - end");
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(knx_k, knx_fingerprint, 0, "/k",
																		 APPLICATION_CBOR, CONTENT_NONE,
																		 OC_DISCOVERABLE,
																		 oc_core_knx_k_get_handler, OC_ACL_G, OC_IF_G,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 oc_core_knx_k_post_handler, OC_ACL_G, OC_IF_G,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_SIZE_MANY(1), "urn:knx:g.s");

void
oc_create_knx_k_resource(int resource_idx, size_t device)
{
	OC_DBG("create /k resources");
	oc_core_populate_resource(resource_idx, device, "/k",
														APPLICATION_CBOR, CONTENT_NONE, OC_DISCOVERABLE,
														oc_core_knx_k_get_handler, 0, oc_core_knx_k_post_handler,
														0, 1, "urn:knx:g.s");
}

void oc_knx_knx_ignore_smode_message_from_self(bool ignore)
{
	g_ignore_smessage_from_self = ignore;
}

// ----------------------------------------------------------------------------

static void oc_core_knx_fingerprint_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;
	PRINT("oc_core_knx_fingerprint_get_handler");

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}

	// check if the state is loaded
	size_t device_index = request->resource->device;
	if (oc_a_lsm_state(device_index) != LSM_S_LOADED)
	{
		OC_ERR("not in loaded state");
		oc_prepare_no_format_response_no_payload(request, OC_STATUS_SERVICE_UNAVAILABLE);
		return;
	}

	oc_rep_begin_root_object();
	oc_rep_i_set_int(root, 1, g_fingerprint);
	oc_rep_end_root_object();

	PRINT("oc_core_knx_fingerprint_get_handler - done");
	oc_prepare_cbor_response(request, OC_STATUS_OK);
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(knx_fingerprint, knx_ia, 0, "/.well-known/knx/f",
																		 APPLICATION_CBOR, CONTENT_NONE,
																		 OC_DISCOVERABLE,
																		 oc_core_knx_fingerprint_get_handler, OC_ACL_C, OC_IF_C,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_SIZE_ZERO());

void
oc_create_knx_fingerprint_resource(int resource_idx, size_t device)
{
	OC_DBG("create /k/f resources");
	oc_core_populate_resource(resource_idx, device, "/.well-known/knx/f",
														APPLICATION_CBOR, CONTENT_NONE,
														OC_DISCOVERABLE, oc_core_knx_fingerprint_get_handler,
														0, 0, 0, 0);
}

// ----------------------------------------------------------------------------

static void oc_core_knx_ia_post_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;

	bool ia_set = false;
	bool iid_set = false;

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}

	size_t device_index = request->resource->device;
	oc_rep_t* rep = request->request_payload;

	while (rep != NULL)
	{
		if (rep->type == OC_REP_INT)
		{
			if (rep->iname == 12)
			{
				PRINT("oc_core_knx_ia_post_handler received 12 (ia) : %d", (int) rep->value.integer);
				oc_core_set_device_ia(device_index, (uint32_t) rep->value.integer);
				int temp = (int) rep->value.integer;
				oc_storage_write(KNX_STORAGE_IA, (uint8_t*) &temp, sizeof(temp));
				ia_set = true;
			}
			else if (rep->iname == 25)
			{
				PRINT("oc_core_knx_ia_post_handler received 25 (fid): %llu", (uint64_t) rep->value.integer);
				oc_core_set_device_fid(device_index, (uint64_t) rep->value.integer);
				uint64_t temp = (uint64_t) rep->value.integer;
				oc_storage_write(KNX_STORAGE_FID, (uint8_t*) &temp, sizeof(temp));
			}
			else if (rep->iname == 26)
			{
				PRINT("oc_core_knx_ia_post_handler received 26 (iid): %llu", (uint64_t) rep->value.integer);
				oc_core_set_device_iid(device_index, (uint64_t) rep->value.integer);
				uint64_t temp = (uint64_t) rep->value.integer;
				oc_storage_write(KNX_STORAGE_IID, (uint8_t*) &temp, sizeof(temp));
				iid_set = true;
			}
		}
		rep = rep->next;
	}

	// iid/ia are mandatory
	if (iid_set && ia_set)
	{
		if (oc_is_device_in_runtime(device_index))
		{
			oc_register_group_multicasts();
			oc_init_datapoints_at_initialization();
			oc_device_info_t* device = oc_core_get_device_info(device_index);
			knx_publish_service(oc_string(device->serialnumber), device->iid, device->ia, device->pm);
		}
		oc_prepare_cbor_response(request, OC_STATUS_CHANGED);
	}
	else
	{
		oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
	}
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(knx_ia, knx, 0, "/.well-known/knx/ia",
																		 APPLICATION_CBOR, CONTENT_NONE,
																		 OC_DISCOVERABLE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 oc_core_knx_ia_post_handler, OC_ACL_C | OC_ACL_SEC, OC_IF_C | OC_IF_SEC,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_SIZE_ZERO());

void
oc_create_knx_ia(int resource_idx, size_t device)
{
	OC_DBG("create /knx/ia resources");
	oc_core_populate_resource(resource_idx, device, "/.well-known/knx/ia",
														APPLICATION_CBOR, CONTENT_NONE,
														OC_DISCOVERABLE, NULL, 0, oc_core_knx_ia_post_handler,
														0, 0);
}


static void oc_core_knx_ldevid_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;
	size_t response_length = 0;

	PRINT("oc_core_knx_ldevid_get_handler");

	if (!oc_accept_header_is_ok(request, APPLICATION_PKCS7_CMC_REQUEST))
	{
		return;
	}
	response_length = oc_string_len(g_ldevid);
	oc_rep_encode_raw((const uint8_t*) oc_string(g_ldevid),
										(size_t) response_length);

	request->response->response_buffer->content_format =
		APPLICATION_PKCS7_CMC_RESPONSE;
	request->response->response_buffer->code = oc_status_code(OC_STATUS_OK);
	request->response->response_buffer->response_length = response_length;

	PRINT("oc_core_knx_ldevid_get_handler- done");
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(knx_ldevid, knx_k, 0, "/.well-known/knx/ldevid",
																		 APPLICATION_PKCS7_CMC_REQUEST, CONTENT_NONE,
																		 OC_DISCOVERABLE,
																		 oc_core_knx_ldevid_get_handler, OC_ACL_D, OC_IF_D,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_SIZE_MANY(1), ":dpt.a[n]");
/* optional resource */
void
oc_create_knx_ldevid_resource(int resource_idx, size_t device)
{
	OC_DBG("oc_create_knx_ldevid_resource");
	oc_core_populate_resource(resource_idx, device, "/.well-known/knx/ldevid",
														APPLICATION_PKCS7_CMC_REQUEST, CONTENT_NONE, OC_DISCOVERABLE,
														oc_core_knx_ldevid_get_handler, 0, 0,
														0, 1, ":dpt.a[n]");
}



static void oc_core_knx_idevid_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;
	size_t response_length = 0;

	PRINT("oc_core_knx_idevid_get_handler");

	if (!oc_accept_header_is_ok(request, APPLICATION_PKCS7_CMC_REQUEST))
	{
		return;
	}
	response_length = oc_string_len(g_idevid);
	oc_rep_encode_raw((const uint8_t*) oc_string(g_idevid),
										(size_t) response_length);

	request->response->response_buffer->content_format =
		APPLICATION_PKCS7_CMC_RESPONSE;
	request->response->response_buffer->code = oc_status_code(OC_STATUS_OK);
	request->response->response_buffer->response_length = response_length;

	PRINT("oc_core_knx_idevid_get_handler- done");
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(knx_idevid, knx_ldevid, 0, "/.well-known/knx/idevid",
																		 APPLICATION_PKCS7_CMC_REQUEST, CONTENT_NONE,
																		 OC_DISCOVERABLE,
																		 oc_core_knx_idevid_get_handler, OC_ACL_D, OC_IF_D,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_SIZE_MANY(1), ":dpt.a[n]");

void
oc_create_knx_idevid_resource(int resource_idx, size_t device)
{
	OC_DBG("oc_create_knx_idevid_resource");
	oc_core_populate_resource(resource_idx, device, "/.well-known/knx/idevid",
														APPLICATION_PKCS7_CMC_REQUEST, CONTENT_NONE, OC_DISCOVERABLE,
														oc_core_knx_idevid_get_handler, 0, 0,
														0, 1, ":dpt.a[n]");
}



#ifdef OC_SPAKE
static spake_data_t spake_data = { 0 };
static int failed_handshake_count = 0;

static bool is_blocking = false;

static oc_event_callback_retval_t
decrement_counter(void* data)
{
	if (failed_handshake_count > 0)
	{
		--failed_handshake_count;
	}

	if (is_blocking && failed_handshake_count == 0)
	{
		is_blocking = false;
	}
	return OC_EVENT_CONTINUE;
}

static void
increment_counter(void)
{
	++failed_handshake_count;
}

static bool
is_handshake_blocked(void)
{
	if (is_blocking)
	{
		return true;
	}

	// after 10 failed attempts per minute, block the client for the
	// next minute
	if (failed_handshake_count > 10)
	{
		is_blocking = true;
		return true;
	}

	return false;
}

#endif 

static oc_separate_response_t spake_separate_rsp;
static oc_event_callback_retval_t oc_core_knx_spake_separate_post_handler(
	void* req_p);

static void
oc_core_knx_spake_post_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;

	PRINT("oc_core_knx_spake_post_handler - start");

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{		return;
	}
	// check if the state is unloaded
	size_t device_index = request->resource->device;
	if (oc_a_lsm_state(device_index) != LSM_S_UNLOADED)
	{
		OC_ERR(" not in unloaded state");
		oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
		return;
	}

	#ifdef OC_SPAKE
	if (is_handshake_blocked())
	{
		request->response->response_buffer->code =
			oc_status_code(OC_STATUS_SERVICE_UNAVAILABLE);

		request->response->response_buffer->max_age = failed_handshake_count * 10;		return;
	}
	#endif 

	oc_rep_t* rep = request->request_payload;

	// check input
	// note: no check if there are multiple byte strings in the request payload
	valid_request = 0;
	while (rep != NULL)
	{
		switch (rep->type)
		{
			case OC_REP_BYTE_STRING:
			{
				if (rep->iname == SPAKE_PA_SHARE_P)
				{
					valid_request = SPAKE_PA_SHARE_P;
				}
				if (rep->iname == SPAKE_CA_CONFIRM_P)
				{
					valid_request = SPAKE_CA_CONFIRM_P;
				}
				if (rep->iname == SPAKE_RND)
				{
					valid_request = SPAKE_RND;
				}
			} break;
			default:
				break;
		}
		rep = rep->next;
	}

	if (valid_request == 0)
	{
		oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
		return;
	}
	rep = request->request_payload;

	if (valid_request == SPAKE_RND)
	{
		// set the default id, in preparation for the response
		// this gets overwritten if the ID is present in the
		// request payload handled below
		oc_free_string(&g_pase.id);
		oc_new_byte_string(&g_pase.id, "rkey", strlen("rkey"));
	}
	// handle input
	while (rep != NULL)
	{
		switch (rep->type)
		{
			case OC_REP_BYTE_STRING:
			{
				if (rep->iname == SPAKE_CA_CONFIRM_P)
				{
					memcpy(g_pase.ca, oc_cast(rep->value.string, uint8_t),
								 sizeof(g_pase.ca));
				}
				if (rep->iname == SPAKE_PA_SHARE_P)
				{
					memcpy(g_pase.pa, oc_cast(rep->value.string, uint8_t),
								 sizeof(g_pase.pa));
				}
				if (rep->iname == SPAKE_RND)
				{
					memcpy(g_pase.rnd, oc_cast(rep->value.string, uint8_t),
								 sizeof(g_pase.rnd));
				}
				if (rep->iname == SPAKE_ID)
				{
					// if the ID is present, overwrite the default
					oc_free_string(&g_pase.id);
					oc_new_byte_string(&g_pase.id, oc_string(rep->value.string),
														 oc_string_len(rep->value.string));
					PRINT("==> CLIENT RECEIVES %d",
								(int) oc_byte_string_len(rep->value.string));
				}
			} break;
			case OC_REP_STRING:
			{
				if (rep->iname == SPAKE_ID)
				{
					// if the ID is present, overwrite the default
					oc_free_string(&g_pase.id);
					oc_new_byte_string(&g_pase.id, oc_string(rep->value.string),
														 oc_string_len(rep->value.string));
					PRINT("==> CLIENT RECEIVES %d",
								(int) oc_byte_string_len(rep->value.string));
				}
			} break;
			default:
				break;
		}
		rep = rep->next;
	}

	PRINT("oc_core_knx_spake_post_handler valid_request: %d", valid_request);
	oc_indicate_separate_response(request, &spake_separate_rsp);
	oc_set_delayed_callback(NULL, &oc_core_knx_spake_separate_post_handler, 0);
}

static oc_event_callback_retval_t
oc_core_knx_spake_separate_post_handler(void* req_p)
{
	(void) req_p;
	PRINT("oc_core_knx_spake_separate_post_handler");

	if (!spake_separate_rsp.active)
	{
		return OC_EVENT_DONE;
	}
	oc_set_separate_response_buffer(&spake_separate_rsp);

	if (valid_request == SPAKE_RND)
	{
		#ifdef OC_SPAKE
		// get random numbers for rnd, salt & it (# of iterations)
		oc_spake_get_pbkdf_params(g_pase.rnd, g_pase.salt, &g_pase.it);
		OC_DBG_SPAKE("Rnd:");
		OC_LOGbytes_SPAKE(g_pase.rnd, sizeof(g_pase.rnd));
		OC_DBG_SPAKE("Salt:");
		OC_LOGbytes_SPAKE(g_pase.salt, sizeof(g_pase.salt));
		OC_DBG_SPAKE("Iterations: %d", g_pase.it);

		#endif /* OC_SPAKE */
		oc_rep_begin_root_object();
		// id (0)
		// oc_rep_i_set_byte_string(root, SPAKE_ID, oc_cast(g_pase.id, uint8_t),
		//                         oc_byte_string_len(g_pase.id));
		// rnd (15)
		oc_rep_i_set_byte_string(root, SPAKE_RND, g_pase.rnd, 32);
		// pbkdf2
		oc_rep_i_set_key(&root_map, SPAKE_PBKDF2);
		oc_rep_begin_object(&root_map, pbkdf2);
		// it 16
		oc_rep_i_set_int(pbkdf2, SPAKE_IT, g_pase.it);
		// salt 5
		oc_rep_i_set_byte_string(pbkdf2, SPAKE_SALT, g_pase.salt, 32);
		oc_rep_end_object(&root_map, pbkdf2);
		oc_rep_end_root_object();
		oc_send_separate_response(&spake_separate_rsp, OC_STATUS_CHANGED);
		return OC_EVENT_DONE;
	}
	#ifdef OC_SPAKE
	else if (valid_request == SPAKE_PA_SHARE_P)
	{
		// return changed, frame pb (11) & cb (13)

		const char* password = oc_spake_get_password();
		mbedtls_mpi_free(&spake_data.w0);
		mbedtls_ecp_point_free(&spake_data.L);
		mbedtls_mpi_free(&spake_data.y);
		mbedtls_ecp_point_free(&spake_data.pub_y);

		mbedtls_mpi_init(&spake_data.w0);
		mbedtls_ecp_point_init(&spake_data.L);
		mbedtls_mpi_init(&spake_data.y);
		mbedtls_ecp_point_init(&spake_data.pub_y);

		int ret = oc_spake_get_w0_L(sizeof(g_pase.salt), g_pase.salt, g_pase.it, &spake_data.w0, &spake_data.L);
		if (ret != 0)
		{
			OC_ERR("oc_spake_get_w0_L failed with code %d", ret);
			goto error;
		}

		ret = oc_spake_gen_keypair(&spake_data.y, &spake_data.pub_y);
		if (ret != 0)
		{
			OC_ERR("oc_spake_gen_keypair failed with code %d", ret);
			goto error;
		}

		// next step: calculate pB, encode it into the struct
		mbedtls_ecp_point pB;
		mbedtls_ecp_point_init(&pB);
		if (ret = oc_spake_calc_shareV(&pB, &spake_data.pub_y, &spake_data.w0))
		{
			OC_ERR("oc_spake_calc_pB failed with code %d", ret);
			mbedtls_ecp_point_free(&pB);
			goto error;
		}

		if (ret = oc_spake_encode_pubkey(&pB, g_pase.pb))
		{
			OC_ERR("oc_spake_encode_pubkey failed with code %d", ret);
			mbedtls_ecp_point_free(&pB);
			goto error;
		}

		if (ret = oc_spake_calc_transcript_responder(&spake_data, g_pase.pa, &pB))
		{
			OC_ERR("oc_spake_calc_transcript_responder failed with code %d", ret);
			mbedtls_ecp_point_free(&pB);
			goto error;
		}

		oc_spake_calc_confirmV(spake_data.K_main, g_pase.cb, g_pase.pa);
		mbedtls_ecp_point_free(&pB);

		oc_rep_begin_root_object();
		// pb (11)
		oc_rep_i_set_byte_string(root, SPAKE_PB_SHARE_V, g_pase.pb,
														 sizeof(g_pase.pb));
		// cb (13)
		oc_rep_i_set_byte_string(root, SPAKE_CB_CONFIRM_V, g_pase.cb,
														 sizeof(g_pase.cb));
		oc_rep_end_root_object();
		oc_send_separate_response(&spake_separate_rsp, OC_STATUS_CHANGED);
		return OC_EVENT_DONE;
	}
	else if (valid_request == SPAKE_CA_CONFIRM_P)
	{
		// calculate expected cA
		uint8_t expected_ca[32];

		OC_DBG_SPAKE("KaKe & pB Bytes");
		OC_LOGbytes_OSCORE(spake_data.K_main, 32);
		OC_LOGbytes_OSCORE(g_pase.pb, sizeof(g_pase.pb));
		oc_spake_calc_confirmP(spake_data.K_main, expected_ca, g_pase.pb);
		OC_DBG_SPAKE("cA:");
		OC_LOGbytes_OSCORE(expected_ca, 32);

		if (memcmp(expected_ca, g_pase.ca, sizeof(g_pase.ca)) != 0)
		{
			OC_ERR("oc_spake_calc_confirmP failed");
			goto error;
		}

		// shared_key is 16-byte array - NOT NULL TERMINATED
		uint8_t shared_key[16];
		uint8_t shared_key_len = sizeof(shared_key);
		oc_spake_calc_K_shared(spake_data.K_main, shared_key);

		// set the /auth/at entry with the calculated shared key
		// size_t device_index = request->resource->device;

		// knx does not have multiple devices per instance (for now), so hardcode
		// the use of the first device
		oc_device_info_t* device = oc_core_get_device_info(0);
		// serial number should be supplied as string array
		PRINT("CLIENT: pase.id length: %d", (int) oc_byte_string_len(g_pase.id));
		oc_oscore_set_auth_device(oc_string(g_pase.id),
															oc_byte_string_len(g_pase.id), "", 0, shared_key,
															shared_key_len);

		// empty payload
		oc_send_empty_separate_response(&spake_separate_rsp, OC_STATUS_CHANGED);

		// handshake completed successfully - clear state
		memset(spake_data.K_main, 0, sizeof(spake_data.K_main));
		mbedtls_ecp_point_free(&spake_data.L);
		mbedtls_ecp_point_free(&spake_data.pub_y);
		mbedtls_mpi_free(&spake_data.w0);
		mbedtls_mpi_free(&spake_data.y);

		mbedtls_ecp_point_init(&spake_data.L);
		mbedtls_ecp_point_init(&spake_data.pub_y);
		mbedtls_mpi_init(&spake_data.w0);
		mbedtls_mpi_init(&spake_data.y);

		memset(g_pase.pa, 0, sizeof(g_pase.pa));
		memset(g_pase.pb, 0, sizeof(g_pase.pb));
		memset(g_pase.ca, 0, sizeof(g_pase.ca));
		memset(g_pase.cb, 0, sizeof(g_pase.cb));
		memset(g_pase.rnd, 0, sizeof(g_pase.rnd));
		memset(g_pase.salt, 0, sizeof(g_pase.salt));
		g_pase.it = 100000;
		return OC_EVENT_DONE;
	}
error:
	// be paranoid: wipe all global data after an error
	memset(spake_data.K_main, 0, sizeof(spake_data.K_main));
	mbedtls_ecp_point_free(&spake_data.L);
	mbedtls_ecp_point_free(&spake_data.pub_y);
	mbedtls_mpi_free(&spake_data.w0);
	mbedtls_mpi_free(&spake_data.y);

	mbedtls_ecp_point_init(&spake_data.L);
	mbedtls_ecp_point_init(&spake_data.pub_y);
	mbedtls_mpi_init(&spake_data.w0);
	mbedtls_mpi_init(&spake_data.y);
	#endif /* OC_SPAKE */

	memset(g_pase.pa, 0, sizeof(g_pase.pa));
	memset(g_pase.pb, 0, sizeof(g_pase.pb));
	memset(g_pase.ca, 0, sizeof(g_pase.ca));
	memset(g_pase.cb, 0, sizeof(g_pase.cb));
	memset(g_pase.rnd, 0, sizeof(g_pase.rnd));
	memset(g_pase.salt, 0, sizeof(g_pase.salt));
	g_pase.it = 100000;

	#ifdef OC_SPAKE
	increment_counter();
	#endif /* OC_SPAKE */
	oc_send_separate_response(&spake_separate_rsp, OC_STATUS_BAD_REQUEST);
	return OC_EVENT_DONE;
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(knx_spake, knx_idevid, 0, "/.well-known/knx/spake",
																		 APPLICATION_CBOR, CONTENT_NONE,
																		 OC_DISCOVERABLE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 oc_core_knx_spake_post_handler, OC_ACL_NONE, OC_IF_NONE, // unsecured EP
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_SIZE_ZERO());

void
oc_create_knx_spake_resource(int resource_idx, size_t device)
{
	OC_DBG("oc_create_knx_spake_resource");
	oc_core_populate_resource(resource_idx, device, "/.well-known/knx/spake",
														APPLICATION_CBOR, CONTENT_NONE,
														OC_DISCOVERABLE, 0, 0, oc_core_knx_spake_post_handler,
														0, 0);
}

#ifdef OC_SPAKE
void
oc_initialise_spake_data(void)
{
	// can fail if initialization of the RNG does not work
	int ret = oc_spake_init();
	assert(ret == 0);
	mbedtls_mpi_init(&spake_data.w0);
	mbedtls_ecp_point_init(&spake_data.L);
	mbedtls_mpi_init(&spake_data.y);
	mbedtls_ecp_point_init(&spake_data.pub_y);
	// start SPAKE brute force protection timer
	oc_set_delayed_callback(NULL, decrement_counter, 10);
}
#endif /* OC_SPAKE */

// ----------------------------------------------------------------------------

void
oc_knx_set_idevid(const char* idevid, int len)
{
	oc_free_string(&g_idevid);
	oc_new_string(&g_idevid, idevid, len);
}

void
oc_knx_set_ldevid(char* idevid, int len)
{
	oc_free_string(&g_ldevid);
	oc_new_string(&g_ldevid, idevid, len);
}

// ----------------------------------------------------------------------------

void oc_knx_load_fingerprint(void)
{
	g_fingerprint = 0; // set to zero for reading error cases
	oc_storage_read(FINGERPRINT_STORE, (uint8_t*) &g_fingerprint, sizeof(g_fingerprint));
}

void oc_knx_dump_fingerprint(void)
{
	oc_storage_write(FINGERPRINT_STORE, (uint8_t*) &g_fingerprint, sizeof(g_fingerprint));
}

void oc_knx_set_fingerprint(uint64_t fingerprint)
{
	g_fingerprint = fingerprint;
}

// update on create/delete of fp/p, fp/r, fp/g and /p 
void oc_knx_increase_fingerprint(void)
{
	g_fingerprint++; // must be only different
	oc_knx_dump_fingerprint();
}

// ----------------------------------------------------------------------------

void oc_knx_load_state(size_t device_index)
{
	oc_lsm_state_t lsm;
	PRINT("oc_knx_load_state: Loading Device Config from Persistent storage");

	oc_device_info_t* device = oc_core_get_device_info(device_index);
	if (device == NULL)
	{
		OC_ERR(" could not get device %d", (int) device_index);
		return;
	}

	int temp_size = oc_storage_read(KNX_STORAGE_LSM, (uint8_t*) &lsm, sizeof(lsm));
	if (temp_size > 0)
	{
		device->lsm_s = lsm;
		PRINT("load state (storage) %ld [%s]", (long) lsm,
					oc_core_get_lsm_state_as_string((oc_lsm_state_t) lsm));
	}

	oc_knx_load_fingerprint();
}

void oc_create_knx_resources(size_t device_index)
{
	OC_DBG("oc_create_knx_resources");
	if (device_index == 0)
	{
		OC_DBG("device 0: KNX common resources created statically");
		return;
	}

	oc_create_a_lsm_resource(OC_A_LSM, device_index);
	oc_create_knx_k_resource(OC_KNX_K, device_index);
	oc_create_knx_fingerprint_resource(OC_KNX_FINGERPRINT, device_index);
	oc_create_knx_ia(OC_KNX_IA, device_index);
	oc_create_knx_ldevid_resource(OC_KNX_LDEVID, device_index);
	oc_create_knx_idevid_resource(OC_KNX_IDEVID, device_index);
	oc_create_knx_spake_resource(OC_KNX_SPAKE, device_index);
	oc_create_knx_resource(OC_KNX, device_index);
}

// ----------------------------------------------------------------------------

bool oc_is_device_in_runtime(size_t device_index)
{
	oc_device_info_t* device = oc_core_get_device_info(device_index);

	if (device->iid == 0)
	{
		// EITT test this for a reset with code 2
		return false;
	}

	if (device->lsm_s != LSM_S_LOADED)
	{
		return false;
	}

	return true;
}