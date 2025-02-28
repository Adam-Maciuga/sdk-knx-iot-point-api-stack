/*
 // Copyright (c) 2021 Cascoda Ltd
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
#include "oc_knx_swu.h"
#include "oc_main.h"
#include "oc_knx_helpers.h"
#include "oc_discovery.h"
#include "oc_core_res.h"
#include "oc_storage.h"
#include "include/oc_helpers.h"
#include "include/oc_ri.h"

static oc_device_swu_t swu_device = {
	0,
	PUSH,
	{NULL,0,NULL},
	{NULL,0,NULL},
	0,
	{0,0,0},
	OC_SWU_STATE_IDLE,
	{NULL,0,NULL},
	OC_SWU_RESULT_INIT,
	false,
	CoAP
};

// below data can be set (PUT) all other can only be read
#define KNX_STORAGE_SWU_MAX_DEFER   "swu_knx_max_defer"
#define KNX_STORAGE_SWU_METHOD      "swu_knx_method"        
#define KNX_STORAGE_SWU_PROTOCOL    "swu_knx_protocol"     
#define KNX_STORAGE_QUERY_URL       "swu_knx_query_url"

static void oc_knx_swu_protocol_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}

	oc_rep_begin_root_object();
	oc_rep_i_set_int(root, 1, swu_device.protocol);
	oc_rep_end_root_object();

	oc_prepare_cbor_response(request, OC_STATUS_OK);
}

static void oc_knx_swu_protocol_put_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}

	const oc_rep_t* rep = request->request_payload;
	if ((rep != NULL) && (rep->type == OC_REP_INT))
	{
		PRINT("oc_knx_swu_protocol_put_handler received : %d", (int) rep->value.integer);

		if (rep->value.integer == CoAP)
		{ // allow only CoAP to be written, otherwise bad request
			// value is already set by init ... but store it again and save to storage

			swu_device.protocol = CoAP;
			oc_storage_write(KNX_STORAGE_SWU_PROTOCOL, (uint8_t*) &swu_device.protocol, sizeof(swu_device.protocol));

			oc_prepare_cbor_response(request, OC_STATUS_CHANGED);
			return;
		}
	}

	oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(knx_swu_protocol, knx_swu_maxdefer, 0, "/swu/protocol",
																		 APPLICATION_CBOR, CONTENT_NONE,
																		 OC_DISCOVERABLE,
																		 oc_knx_swu_protocol_get_handler, OC_ACL_D, OC_IF_D,
																		 oc_knx_swu_protocol_put_handler, OC_ACL_SWU, OC_IF_SWU,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 "urn:knx:dpt.protocols", OC_SIZE_ZERO());

void oc_create_knx_swu_protocol_resource(int resource_idx, size_t device)
{
	OC_DBG("oc_create_knx_swu_protocol_resource");
	oc_core_populate_resource(resource_idx, device, "/swu/protocol",
														APPLICATION_CBOR,CONTENT_NONE, 
														OC_DISCOVERABLE, oc_knx_swu_protocol_get_handler,
														oc_knx_swu_protocol_put_handler, 0, 0, 0);

	oc_core_bind_dpt_resource(resource_idx, device, "urn:knx:dpt.protocols");
}

static void oc_knx_swu_max_defer_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}

	oc_rep_begin_root_object();
	oc_rep_i_set_int(root, 1, swu_device.max_defer);
	oc_rep_end_root_object();

	oc_prepare_cbor_response(request, OC_STATUS_OK);
}

static void oc_knx_swu_max_defer_put_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}

	oc_rep_t* rep = request->request_payload;
	if ((rep != NULL) && (rep->type == OC_REP_INT))
	{
		PRINT("oc_knx_swu_max_defer_put_handler received : %d", (int) rep->value.integer);
		swu_device.max_defer = (int) rep->value.integer;
		oc_storage_write(KNX_STORAGE_SWU_MAX_DEFER, (uint8_t*) &swu_device.max_defer, sizeof(swu_device.max_defer));
		oc_prepare_cbor_response(request, OC_STATUS_OK);
		return;
	}

	oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(knx_swu_maxdefer, knx_swu_method, 0, "/swu/maxdefer",
																		 APPLICATION_CBOR, CONTENT_NONE,
																		 OC_DISCOVERABLE,
																		 oc_knx_swu_max_defer_get_handler, OC_ACL_D, OC_IF_D,
																		 oc_knx_swu_max_defer_put_handler, OC_ACL_SWU, OC_IF_SWU,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 "urn:knx:dpt.timePeriodSec",
																		 OC_SIZE_ZERO());

void oc_create_knx_swu_max_defer_resource(int resource_idx, size_t device)
{
	OC_DBG("oc_create_knx_swu_max_defer_resource");
	oc_core_populate_resource(resource_idx, device, "/swu/maxdefer",
														APPLICATION_CBOR,CONTENT_NONE, 
														OC_DISCOVERABLE, oc_knx_swu_max_defer_get_handler,
														oc_knx_swu_max_defer_put_handler, 0, 0, 0);

	oc_core_bind_dpt_resource(resource_idx, device, "urn:knx:dpt.timePeriodSec");
}

static void oc_knx_swu_method_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}

	oc_rep_begin_root_object();
	oc_rep_i_set_int(root, 1, swu_device.update_method);
	oc_rep_end_root_object();

	oc_prepare_cbor_response(request, OC_STATUS_OK);
}

static void oc_knx_swu_method_put_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}

	oc_rep_t* rep = request->request_payload;
	if ((rep != NULL) && (rep->type == OC_REP_INT))
	{
		PRINT("oc_knx_swu_method_put_handler received : %d", (int) rep->value.integer);

		if (rep->value.integer == PUSH)
		{
			swu_device.update_method = (int) rep->value.integer;
			oc_storage_write(KNX_STORAGE_SWU_METHOD, (uint8_t*) &swu_device.update_method, sizeof(swu_device.update_method));
			oc_prepare_cbor_response(request, OC_STATUS_OK);
			return;
		}
	}

	oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(knx_swu_method, knx_lastupdate, 0, "/swu/method",
																		 APPLICATION_CBOR, CONTENT_NONE, 
																		 OC_DISCOVERABLE,
																		 oc_knx_swu_method_get_handler, OC_ACL_D, OC_IF_D,
																		 oc_knx_swu_method_put_handler, OC_ACL_SWU, OC_IF_SWU,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 "urn:knx:dpt.transferMethod",
																		 OC_SIZE_ZERO());

void oc_create_knx_swu_method_resource(int resource_idx, size_t device)
{
	OC_DBG("oc_create_knx_swu_method_resource");
	oc_core_populate_resource(resource_idx, device, "/swu/method",
														APPLICATION_CBOR, CONTENT_NONE, OC_DISCOVERABLE, oc_knx_swu_method_get_handler,
														oc_knx_swu_method_put_handler, 0,
														0, 0, 0);

	oc_core_bind_dpt_resource(resource_idx, device, "urn:knx:dpt.transferMethod");
}

static void oc_knx_swu_last_update_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}

	oc_rep_begin_root_object();
	if (swu_device.downloaded_once)
	{
		// value = osv: true (no clock available after download)
		oc_rep_i_set_text_string(root, 1, "osv:true");
	}
	else
	{
		// initial value = date of manufacturing 
		oc_rep_i_set_text_string(root, 1, oc_string(swu_device.last_update));
	}
	oc_rep_end_root_object();

	oc_prepare_cbor_response(request, OC_STATUS_OK);
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(knx_lastupdate, knx_swu_result, 0, "/swu/lastupdate",
																		 APPLICATION_CBOR, CONTENT_NONE,
																		 OC_DISCOVERABLE,
																		 oc_knx_swu_last_update_get_handler, OC_ACL_D, OC_IF_D,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 "urn:knx:dpt.varString8859_1",
																		 OC_SIZE_ZERO());

void oc_create_knx_swu_last_update_resource(int resource_idx, size_t device)
{
	OC_DBG("oc_create_knx_swu_lastupdate_resource");
	oc_core_populate_resource(resource_idx, device, "/swu/lastupdate",
														APPLICATION_CBOR,CONTENT_NONE, 
														OC_DISCOVERABLE, oc_knx_swu_last_update_get_handler,
														0, 0, 0, 0);


	oc_core_bind_dpt_resource(resource_idx, device, "urn:knx:dpt.varString8859_1");
}

static void oc_knx_swu_result_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}

	oc_rep_begin_root_object();
	oc_rep_i_set_int(root, 1, swu_device.result);
	oc_rep_end_root_object();

	oc_prepare_cbor_response(request, OC_STATUS_OK);
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(knx_swu_result, knx_swu_state, 0, "/swu/result",
																		 APPLICATION_CBOR, CONTENT_NONE,
																		 OC_DISCOVERABLE,
																		 oc_knx_swu_result_get_handler, OC_ACL_D, OC_IF_D,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 "urn:knx:dpt.updateResult",
																		 OC_SIZE_ZERO());

void oc_create_knx_swu_result_resource(int resource_idx, size_t device)
{
	OC_DBG("oc_create_knx_swu_result_resource");
	oc_core_populate_resource(resource_idx, device, "/swu/result",
														APPLICATION_CBOR,CONTENT_NONE, 
														OC_DISCOVERABLE, oc_knx_swu_result_get_handler, 0, 0, 0, 0);

	oc_core_bind_dpt_resource(resource_idx, device, "urn:knx:dpt.updateResult");
}

static void oc_knx_swu_state_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}

	oc_rep_begin_root_object();
	oc_rep_i_set_int(root, 1, swu_device.state);
	oc_rep_end_root_object();

	oc_prepare_cbor_response(request, OC_STATUS_OK);
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(knx_swu_state, knx_swu_update, 0, "/swu/state",
																		 APPLICATION_CBOR, CONTENT_NONE,
																		 OC_DISCOVERABLE,
																		 oc_knx_swu_state_get_handler, OC_ACL_D, OC_IF_D,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 "urn:knx:dpt.dldState", OC_SIZE_ZERO());

void oc_create_knx_swu_state_resource(int resource_idx, size_t device)
{
	OC_DBG("oc_create_knx_swu_state_resource");
	oc_core_populate_resource(resource_idx, device, "/swu/state",
														APPLICATION_CBOR,CONTENT_NONE, 
														OC_DISCOVERABLE, oc_knx_swu_state_get_handler, 0, 0, 0, 0);

	oc_core_bind_dpt_resource(resource_idx, device, "urn:knx:dpt.dldState");
}

static void oc_knx_swu_update_put_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}
	// note we are not doing anything with the trigger.

	/* not sure what to do with request data, so we are just parsing it for now*/
	oc_rep_t* rep = request->request_payload;
	if ((rep != NULL) && (rep->type == OC_REP_INT))
	{
		PRINT("oc_knx_swu_update_put_handler received : %d", (int) rep->value.integer);
		oc_prepare_cbor_response(request, OC_STATUS_OK);
		return;
	}

	oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(knx_swu_update, knx_swu_pkgv, 0, "/swu/update",
																		 APPLICATION_CBOR, CONTENT_NONE, 
																		 OC_DISCOVERABLE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 oc_knx_swu_update_put_handler, OC_ACL_SWU, OC_IF_SWU,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 "urn:knx:dpt.timePeriodSecZ",
																		 OC_SIZE_ZERO());

void oc_create_knx_swu_update_resource(int resource_idx, size_t device)
{
	OC_DBG("oc_create_knx_swu_update_resource");
	oc_core_populate_resource(resource_idx, device, "/swu/update",
														APPLICATION_CBOR,CONTENT_NONE, 
														OC_DISCOVERABLE, 0, oc_knx_swu_update_put_handler, 0, 0, 0);

	oc_core_bind_dpt_resource(resource_idx, device, "urn:knx:dpt.timePeriodSecZ");
}

static void oc_knx_swu_pkg_version_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}

	if (swu_device.downloaded_once)
	{
		oc_rep_begin_root_object();
		const int64_t pkg_ver[3] = { swu_device.pkg_version.major, swu_device.pkg_version.minor, swu_device.pkg_version.patch };
		oc_rep_i_set_int_array(root, 1, pkg_ver, 3);
		oc_rep_end_root_object();

		oc_prepare_cbor_response(request, OC_STATUS_OK);
		return;
	}

	oc_prepare_no_format_response_no_payload(request, OC_STATUS_NOT_FOUND);

}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(knx_swu_pkgv, knx_swu_pkgcmd, 0, "/swu/pkgv",
																		 APPLICATION_CBOR, CONTENT_NONE,
																		 OC_DISCOVERABLE,
																		 oc_knx_swu_pkg_version_get_handler, OC_ACL_D, OC_IF_D,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 "urn:knx:dpt.version", OC_SIZE_ZERO());

void oc_create_knx_swu_pkg_version_resource(int resource_idx, size_t device)
{
	OC_DBG("oc_create_knx_swu_pkgv_resource");
	oc_core_populate_resource(resource_idx, device, "/swu/pkgv",
														APPLICATION_CBOR,CONTENT_NONE, 
														OC_DISCOVERABLE, oc_knx_swu_pkg_version_get_handler, 0, 0, 0, 0);

	oc_core_bind_dpt_resource(resource_idx, device, "urn:knx:dpt.version");
}

static void oc_knx_swu_a_put_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;

	int binary_size = 0;
	int block_size = 0;
	int block_offset = 0;
	char* key = 0;
	char* value = 0;
	size_t key_len = 0, value_len;

	oc_content_format_t content_format;
	const uint8_t* payload = NULL;
	size_t len = 0;

	static oc_separate_response_t s_delayed_response_swu;

	oc_swu_t* my_cb = oc_get_swu_cb();

	if (my_cb && my_cb->cb)
		oc_indicate_separate_response(request, &s_delayed_response_swu);
	else
		(void) s_delayed_response_swu;

	PRINT("oc_knx_swu_a_put_handler - start");

	if (!oc_accept_header_is_ok(request, APPLICATION_OCTET_STREAM))
	{
		return;
	}

	oc_init_query_iterator();
	while (oc_iterate_query(request, &key, &key_len, &value, &value_len) > 0)
	{
		if (strncmp(key, "po", key_len) == 0)
		{
			block_offset = atoi(value);
		}
		if (strncmp(key, "ps", key_len) == 0)
		{
			block_size = atoi(value);
		}
		if (strncmp(key, "pkgs", key_len) == 0)
		{
			binary_size = atoi(value);
		}
	}
	PRINT("binary_size: %d", binary_size);
	PRINT("block_size: %d", block_size);
	PRINT("block_offset: %d", block_offset);

	size_t device_index = request->resource->device;

	oc_get_request_payload_raw(request, &payload, &len, &content_format);

	if (my_cb && my_cb->cb)
	{ // call application handler
		my_cb->cb(device_index, &s_delayed_response_swu, binary_size, block_offset, (uint8_t*) payload, len, my_cb->data);
	}
	else
	{
		oc_prepare_cbor_response(request, OC_STATUS_OK);
	}

	PRINT("oc_knx_swu_a_put_handler - end");
}

static void oc_knx_swu_a_post_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}

	// triggers a software update query request (PULL on Software Update Server)
	// triggers a {cmd:start/cancel} with some add. data
	oc_rep_t* rep = request->request_payload;
	if (rep != NULL && rep->type == OC_REP_INT)
	{
		PRINT("oc_knx_swu_a_post_handler received : %d", (int) rep->value.integer);

		// not implemented 
		oc_prepare_no_format_response_no_payload(request, OC_STATUS_NOT_IMPLEMENTED);
		return;
	}

	oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(knx_swu_pkgcmd, knx_swu_pkgbytes, 0, "/a/swu",
																		 APPLICATION_CBOR, CONTENT_NONE,
																		 OC_DISCOVERABLE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 oc_knx_swu_a_put_handler, OC_ACL_P, OC_IF_P,
																		 oc_knx_swu_a_post_handler, OC_ACL_SWU, OC_IF_SWU,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 "urn:knx:dpt.file", OC_SIZE_ZERO());

void oc_create_knx_swu_a_resource(int resource_idx, size_t device)
{
	OC_DBG("oc_create_knx_swu_a_resource");
	oc_core_populate_resource(resource_idx, device, "/a/swu",
														APPLICATION_CBOR, CONTENT_NONE, 
														OC_DISCOVERABLE, 0,
														oc_knx_swu_a_put_handler, oc_knx_swu_a_post_handler,
														0, 0);

	oc_core_bind_dpt_resource(resource_idx, device, "urn:knx:dpt.file");
}

static void oc_knx_swu_bytes_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}

	oc_rep_begin_root_object();
	oc_rep_i_set_int(root, 1, swu_device.pkg_bytes);
	oc_rep_end_root_object();

	oc_prepare_cbor_response(request, OC_STATUS_OK);
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(knx_swu_pkgbytes, knx_swu_pkgqurl, 0, "/swu/pkgbytes",
																		 APPLICATION_CBOR, CONTENT_NONE, 
																		 OC_DISCOVERABLE,
																		 oc_knx_swu_bytes_get_handler, OC_ACL_D, OC_IF_D,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 "urn:knx:dpt.value4UCount",
																		 OC_SIZE_ZERO());
void oc_create_knx_swu_pkg_bytes_resource(int resource_idx, size_t device)
{
	OC_DBG("oc_create_knx_swu_pkgbytes_resource");
	oc_core_populate_resource(resource_idx, device, "/swu/pkgbytes",
														APPLICATION_CBOR,CONTENT_NONE, 
														OC_DISCOVERABLE, oc_knx_swu_bytes_get_handler, 0, 0,
														0, 0);

	oc_core_bind_dpt_resource(resource_idx, device, "urn:knx:dpt.value4UCount");
}

static void oc_knx_swu_pkg_query_url_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}
	oc_rep_begin_root_object();
	oc_rep_i_set_text_string(root, 1, oc_string(swu_device.query_url));
	oc_rep_end_root_object();

	oc_prepare_cbor_response(request, OC_STATUS_OK);
}

static void oc_knx_swu_pkg_query_url_put_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}

	oc_rep_t* rep = request->request_payload;
	if ((rep != NULL) && (rep->type == OC_REP_STRING))
	{
		PRINT("oc_knx_swu_pkg_query_url_put_handler received : %s", oc_string_checked(rep->value.string));
		oc_swu_set_query_url(oc_string_checked(rep->value.string));
		oc_storage_write(KNX_STORAGE_QUERY_URL, (uint8_t*) &swu_device.query_url, oc_string_len(swu_device.query_url));
		oc_prepare_cbor_response(request, OC_STATUS_OK);
		return;
	}

	oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(knx_swu_pkgqurl, knx_swu_pkgnames, 0, "/swu/pkgqurl",
																		 APPLICATION_CBOR, CONTENT_NONE,
																		 OC_DISCOVERABLE,
																		 oc_knx_swu_pkg_query_url_get_handler, OC_ACL_D, OC_IF_D,
																		 oc_knx_swu_pkg_query_url_put_handler, OC_ACL_SWU, OC_IF_SWU,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 "urn:knx:dpt.url", OC_SIZE_ZERO());

void oc_create_knx_swu_pkg_qurl_resource(int resource_idx, size_t device)
{
	OC_DBG("oc_create_knx_swu_pkgqurl_resource");
	oc_core_populate_resource(resource_idx, device, "/swu/pkgqurl",
														APPLICATION_CBOR,CONTENT_NONE, 
														OC_DISCOVERABLE, oc_knx_swu_pkg_query_url_get_handler,
														oc_knx_swu_pkg_query_url_put_handler, 0,
														0, 0);

	oc_core_bind_dpt_resource(resource_idx, device, "urn:knx:dpt.url");
}

static void oc_knx_swu_pkg_name_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}

	if (swu_device.downloaded_once)
	{
		oc_rep_begin_root_object();
		oc_rep_i_set_text_string(root, 1, oc_string(swu_device.pkg_name));
		oc_rep_end_root_object();

		oc_prepare_cbor_response(request, OC_STATUS_OK);
		return;
	}

	oc_prepare_no_format_response_no_payload(request, OC_STATUS_NOT_FOUND);
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(knx_swu_pkgnames, knx_swu, 0, "/swu/pkgname",
																		 APPLICATION_CBOR, CONTENT_NONE, 
																		 OC_DISCOVERABLE,
																		 oc_knx_swu_pkg_name_get_handler, OC_ACL_D, OC_IF_D,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 "urn:knx:dpt.varString8859_1",
																		 OC_SIZE_ZERO());

void oc_create_knx_swu_pkg_names_resource(int resource_idx, size_t device)
{
	OC_DBG("oc_create_knx_swu_pkgnames_resource");
	oc_core_populate_resource(resource_idx, device, "/swu/pkgname",
														APPLICATION_CBOR,CONTENT_NONE, 
														OC_DISCOVERABLE, oc_knx_swu_pkg_name_get_handler, 0, 0, 0, 0);

	oc_core_bind_dpt_resource(resource_idx, device, "urn:knx:dpt.varString8859_1");
}

static void oc_core_knx_swu_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;

	int query_parameter_kvpair_matches = 0; // how many (to this device applicable) query parameter key/value pair matches where found 
	size_t response_length = 0;
	int query_pn = PAGE_NUMBER;
	int query_ps = PAGE_SIZE;

	int first_entry = OC_KNX_SWU_PROTOCOL;  // first entry number of a resource that will be placed on a page
	int last_entry = OC_KNX_SWU;            // last entry number of a resource that will be placed on a page
	int total = last_entry - first_entry;   // total entries of this resource
	bool more_request_needed = false;

	PRINT("oc_core_swu_get_handler - start");

	if (!oc_accept_header_is_ok(request, APPLICATION_LINK_FORMAT))
	{
		return;
	}

	size_t device_index = request->resource->device;

	// handle query parameters l=ps and/or l=total
	if (query_l_was_processed(request, PAGE_SIZE, total))
		return;

	// first entry number of a resource that will be placed on a page
	first_entry += evaluate_query_px(request, &query_pn, &query_ps);

	// check if requested page will carry at least one resource e.g
	// - total=4, pn 5, ps 20, first entry = 100 -> no data on page 5 (all on page 0)
	// - total=4, pn 1, ps 04, first entry = 004 -> no data on page 1 (all on page 0)
	if (first_entry >= last_entry || query_ps == 0)
	{
		oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
		return;
	}

	// entries don't fit in a single page -> more pages are needed to get the full list
	// - total=4, page number 1, page size 02, first entry = 002 -> no more data on next page 
	// - total=4, page number 1, page size 01, first entry = 001 -> more data on next page
	if (last_entry > first_entry + query_ps)
	{
		last_entry = first_entry + query_ps;
		more_request_needed = true;
	}

	for (int i = first_entry; i < last_entry; i++)
	{
		const oc_resource_t* resource = oc_core_get_resource_by_index(i, device_index);
		if (oc_filter_resource(resource, request, device_index, &response_length, &i, i, true))
		{
			query_parameter_kvpair_matches++;
		}
	}

	if (query_parameter_kvpair_matches > 0)
	{
		// add only a page hint if at least one response entry is in
		if (more_request_needed)
		{
			// no page # was in the request (query_p =0) = next page 1 else #+1
			response_length += add_next_page_indicator(oc_string(request->resource->uri), ++query_pn);
		}
		oc_prepare_linkformat_response(request, OC_STATUS_OK, response_length);
	}
	else
	{
		// resources are mandatory, hence this can't be correct here
		oc_prepare_no_format_response_no_payload(request, OC_STATUS_INTERNAL_SERVER_ERROR);
	}
	PRINT("oc_core_swu_get_handler - end");
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(knx_swu, sub, 0, "/swu",
																		 APPLICATION_LINK_FORMAT, CONTENT_NONE,
																		 OC_DISCOVERABLE,
																		 oc_core_knx_swu_get_handler, OC_ACL_P | OC_ACL_D | OC_ACL_C, OC_IF_LI,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_SIZE_MANY(1), "urn:knx:fb.swu");

void oc_create_knx_swu_resource(int resource_idx, size_t device)
{
	OC_DBG("oc_create_knx_swu_resource");
	//
	oc_core_populate_resource(resource_idx, device, "/swu",
														APPLICATION_LINK_FORMAT, CONTENT_NONE,OC_DISCOVERABLE, oc_core_knx_swu_get_handler, 0, 0,
														0, 1, "urn:knx:fb.swu");
}

void oc_create_knx_swu_resources(size_t device_index)
{
	OC_DBG("oc_create_knx_swu_resources");

	// create missing runtime variables 
	oc_swu_set_package_name("");                            // no name for the initial (never downloaded) state
	oc_swu_set_last_update("2020-04-12T23:20:50.52Z"); // EITT test value for manufacturing date 

	if (device_index == 0)
	{
		OC_DBG("device 0: Software update resources created statically");
		return;
	}

	oc_create_knx_swu_protocol_resource(OC_KNX_SWU_PROTOCOL, device_index);
	oc_create_knx_swu_max_defer_resource(OC_KNX_SWU_MAXDEFER, device_index);
	oc_create_knx_swu_method_resource(OC_KNX_SWU_METHOD, device_index);
	oc_create_knx_swu_last_update_resource(OC_KNX_LASTUPDATE, device_index);
	oc_create_knx_swu_result_resource(OC_KNX_SWU_RESULT, device_index);
	oc_create_knx_swu_state_resource(OC_KNX_SWU_STATE, device_index);
	oc_create_knx_swu_update_resource(OC_KNX_SWU_UPDATE, device_index);
	oc_create_knx_swu_pkg_version_resource(OC_KNX_SWU_PKGV, device_index);
	oc_create_knx_swu_a_resource(OC_KNX_SWU_PKGCMD, device_index);
	oc_create_knx_swu_pkg_bytes_resource(OC_KNX_SWU_PKGBYTES, device_index);
	oc_create_knx_swu_pkg_qurl_resource(OC_KNX_SWU_PKGQURL, device_index);
	oc_create_knx_swu_pkg_names_resource(OC_KNX_SWU_PKGNAMES, device_index);
	oc_create_knx_swu_resource(OC_KNX_SWU, device_index);
}

// ----------------------------------------------------------------------------

void oc_swu_set_package_name(const char* name)
{
	oc_free_string(&swu_device.pkg_name);
	oc_new_string(&swu_device.pkg_name, name, strlen(name));
}

void oc_swu_set_last_update(const char* time)
{
	oc_free_string(&swu_device.last_update);
	oc_new_string(&swu_device.last_update, time, strlen(time));
}

void oc_swu_set_package_bytes(const int package_bytes)
{
	swu_device.pkg_bytes = package_bytes;
}

void oc_swu_set_package_version(const int major, const int minor, const int patch)
{
	swu_device.pkg_version.major = major;
	swu_device.pkg_version.minor = minor;
	swu_device.pkg_version.patch = patch;
}

void oc_swu_set_state(const oc_swu_state_t state)
{
	swu_device.state = state;
}

void oc_swu_set_query_url(const char* url)
{
	oc_free_string(&swu_device.query_url);
	oc_new_string(&swu_device.query_url, url, strlen(url));
}

void oc_swu_set_result(const oc_swu_result_t result)
{
	swu_device.result = result;
}