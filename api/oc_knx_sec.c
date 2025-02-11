/*
 // Copyright (c) 2021-2023 Cascoda Ltd
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
#include "api/oc_knx_sec.h"
#include "oc_discovery.h"
#include "oc_core_res.h"
#include <stdio.h>
#define __STDC_FORMAT_MACROS  // defined to use format specifiers also in C++
#include <inttypes.h>
#include "security/oc_oscore_context.h"
#include "oc_knx.h"
#include "oc_knx_helpers.h"
#include "oc_storage.h"

#define AT_STORE "at_store"

 // ---------------------------Variables --------------------------------------

static uint32_t g_oscore_replaywindow = 32;    // default according to RFC OSCORE
static uint32_t g_oscore_osndelay = 1000;      // default (ms) defined by iot specification
static oc_auth_at_t g_at_entries[G_AT_MAX_ENTRIES];

// ----------------------------------------------------------------------------

static void oc_at_dump_entry(size_t device_index, int entry);



char* oc_at_profile_to_string(oc_at_profile_t at_profile)
{
	if (at_profile == OC_PROFILE_COAP_OSCORE)
	{
		return "coap_oscore";
	}
	if (at_profile == OC_PROFILE_COAP_DTLS)
	{
		return "coap_dtls";
	}
	if (at_profile == OC_PROFILE_COAP_TLS)
	{
		return "coap_tls";
	}
	if (at_profile == OC_PROFILE_COAP_PASE)
	{
		return "coap_pase";
	}
	return "";
}

// ----------------------------------------------------------------------------

static void oc_core_knx_auth_o_osndelay_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;

	PRINT("oc_core_knx_auth_o_osndelay_get_handler");

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}

	oc_rep_begin_root_object();
	oc_rep_i_set_uint(root, 1, g_oscore_osndelay);
	oc_rep_end_root_object();

	PRINT("oc_core_knx_auth_o_osndelay_get_handler - done");
	oc_prepare_cbor_response(request, OC_STATUS_OK);
}

static void oc_core_knx_auth_o_osndelay_put_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}

	oc_rep_t* rep = request->request_payload;
	while (rep != NULL)
	{
		if (rep->type == OC_REP_INT)
		{
			if (rep->iname == 1)
			{
				PRINT("oc_core_knx_auth_o_osndelay_put_handler type: %d value %d", (int) rep->type, (int) rep->value.integer);
				g_oscore_osndelay = rep->value.integer;
				oc_prepare_cbor_response(request, OC_STATUS_CHANGED);
				return;
			}
		}
		rep = rep->next;
	}

	oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(knx_auth_o_osndelay, knx_auth_o, 0, "/auth/o/osndelay",
																		 APPLICATION_CBOR, CONTENT_NONE,
																		 OC_DISCOVERABLE,
																		 oc_core_knx_auth_o_osndelay_get_handler, OC_ACL_P, OC_IF_P,
																		 oc_core_knx_auth_o_osndelay_put_handler, OC_ACL_SEC, OC_IF_SEC,
																		 0, OC_ACL_NONE, OC_IF_NONE,
																		 0, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_SIZE_MANY(1), ":dpt:timePeriodMsec");
void
oc_create_knx_auth_o_osndelay_resource(int resource_idx, size_t device)
{
	OC_DBG("oc_create_knx_auth_o_osndelay_resource");
	//
	oc_core_populate_resource(resource_idx, device, "/auth/o/osndelay",
														APPLICATION_CBOR, CONTENT_NONE,
														OC_DISCOVERABLE, oc_core_knx_auth_o_osndelay_get_handler,
														oc_core_knx_auth_o_osndelay_put_handler, 0, 0, 1, ":dpt:timePeriodMsec");
}

static void
oc_core_knx_auth_o_replwdo_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}
	
	oc_rep_begin_root_object();
	oc_rep_i_set_uint(root, 1, g_oscore_replaywindow);
	oc_rep_end_root_object();

	PRINT("oc_core_knx_auth_o_osndelay_get_handler - done");
	oc_prepare_cbor_response(request, OC_STATUS_OK);
}

static void oc_core_knx_auth_o_replwdo_put_handler(oc_request_t* request, oc_interface_mask_t iface_mask,
																									 void* data)
{
	(void) data;
	(void) iface_mask;

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}

	oc_rep_t* rep = request->request_payload;
	while (rep != NULL)
	{
		if (rep->type == OC_REP_INT)
		{
			if (rep->iname == 1)
			{
				PRINT("oc_core_knx_auth_o_replwdo_put_handler type: %d value %d",							rep->type, (int) rep->value.integer);
				g_oscore_replaywindow = rep->value.integer;
				oc_prepare_cbor_response(request, OC_STATUS_CHANGED);
				return;
			}
		}
		rep = rep->next;
	}

	oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(knx_auth_o_replwdo, knx_auth_o_osndelay, 0, "/auth/o/replwdo",
																		 APPLICATION_CBOR, CONTENT_NONE,
																		 OC_DISCOVERABLE,
																		 oc_core_knx_auth_o_replwdo_get_handler, OC_ACL_P, OC_IF_P,
																		 oc_core_knx_auth_o_replwdo_put_handler, OC_ACL_SEC, OC_IF_SEC,
																		 0, OC_ACL_NONE, OC_IF_NONE,
																		 0, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_SIZE_MANY(1), ":dpt.value2UCount");
void
oc_create_knx_auth_o_replwdo_resource(int resource_idx, size_t device)
{
	OC_DBG("oc_create_knx_auth_o_replwdo_resource");
	//
	oc_core_populate_resource(resource_idx, device, "/auth/o/replwdo",
														APPLICATION_CBOR, CONTENT_NONE,
														OC_DISCOVERABLE, oc_core_knx_auth_o_replwdo_get_handler,
														oc_core_knx_auth_o_replwdo_put_handler, 0, 0, 1, ":dpt.value2UCount");
}

// ----------------------------------------------------------------------------

static void oc_core_knx_auth_o_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;

	int query_parameter_kvpair_matches = 0; // how many (to this device applicable) query parameter key/value pair matches where found 
	size_t response_length = 0;
	int query_pn = PAGE_NUMBER;
	int query_ps = PAGE_SIZE;

	int first_entry = OC_KNX_AUTH_O_REPLWDO;// first entry number of a resource that will be placed on a page
	int last_entry = OC_KNX_AUTH_O;         // last entry number of a resource that will be placed on a page
	int total = last_entry - first_entry;   // total entries of this resource
	bool more_request_needed = false;

	PRINT("oc_core_auth_o_get_handler - start");

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
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(knx_auth_o, knx_auth_at, 0, "/auth/o",
																		 APPLICATION_LINK_FORMAT, CONTENT_NONE,
																		 OC_DISCOVERABLE,
																		 oc_core_knx_auth_o_get_handler, OC_ACL_P | OC_ACL_D | OC_ACL_C, OC_IF_LI,
																		 0, OC_ACL_NONE, OC_IF_NONE,
																		 0, OC_ACL_NONE, OC_IF_NONE,
																		 0, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_SIZE_ZERO());
void
oc_create_knx_auth_o_resource(int resource_idx, size_t device_index)
{
	OC_DBG("create /aut/o resources");
	// TODO: what is resource type, none for now
	oc_core_populate_resource(resource_idx, device_index, "/auth/o",
														APPLICATION_LINK_FORMAT, CONTENT_NONE, 
														OC_DISCOVERABLE,
														oc_core_knx_auth_o_get_handler, 0, 0, 0, 0);
}

// ----------------------------------------------------------------------------

#define LDEVID_RENEW 1
#define LDEVID_STOP 2

static int
a_sen_convert_cmd(char* cmd)
{
	if (strncmp(cmd, "renew", strlen("renew")) == 0)
	{
		return LDEVID_RENEW;
	}
	if (strncmp(cmd, "stop", strlen("stop")) == 0)
	{
		return LDEVID_STOP;
	}
	OC_DBG("convert_cmd command not recognized: %s", cmd);
	return 0;
}

static void oc_core_a_sen_post_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;

	int cmd = 0;

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}

	bool changed = false;


	const oc_rep_t* rep = request->request_payload;
	while (rep != NULL)
	{
		PRINT("oc_core_a_sen_post_handler: key: (check) %s ", oc_string_checked(rep->name));
		if (rep->type == OC_REP_STRING)
		{
			if (rep->iname == 2) // 2: "renew"
			{
				cmd = a_sen_convert_cmd(oc_string(rep->value.string));
				changed = true;
				break;
			}
		}
		rep = rep->next;
	}

	// input was set, so create the response
	if (changed == true)
	{
		PRINT("oc_core_a_sen_post_handler cmd %d ", cmd);
		// renew the credentials, note: this is optional for now

		oc_prepare_cbor_response(request, OC_STATUS_CHANGED);
		return;
	}
	oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(knx_a_sen, knx_auth_o_replwdo, 0, "/a/sen",
																		 APPLICATION_CBOR, CONTENT_NONE,
																		 OC_DISCOVERABLE,
																		 0, OC_ACL_NONE, OC_IF_NONE,
																		 0, OC_ACL_NONE, OC_IF_NONE,
																		 oc_core_a_sen_post_handler, OC_ACL_SEC, OC_IF_SEC,
																		 0, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_SIZE_ZERO());
void
oc_create_a_sen_resource(int resource_idx, size_t device)
{
	OC_DBG("oc_create_a_sen_resource");
	// "/a/sen"
	oc_core_populate_resource(resource_idx, device, "/a/sen",
														APPLICATION_CBOR, CONTENT_NONE,  
														OC_DISCOVERABLE, 0, 0,
														oc_core_a_sen_post_handler, 0, 0);
}

// ----------------------------------------------------------------------------

static int
find_empty_at_index(void)
{
	for (int i = 0; i < G_AT_MAX_ENTRIES; i++)
	{
		if (oc_string_len(g_at_entries[i].id) == 0)
		{
			return i;
		}
	}
	return -1;
}

static int
find_index_from_at(oc_string_t* at)
{
	size_t len;
	size_t len_at = oc_string_len(*at);
	for (int i = 0; i < G_AT_MAX_ENTRIES; i++)
	{
		len = oc_string_len(g_at_entries[i].id);
		if (len > 0 && len == len_at &&
				(strncmp(oc_string(*at), oc_string(g_at_entries[i].id), len) == 0))
		{
			return i;
		}
	}
	return -1;
}

static int
find_index_from_at_string(const char* at, int len_at)
{
	int len;
	for (int i = 0; i < G_AT_MAX_ENTRIES; i++)
	{
		len = (int) oc_string_len(g_at_entries[i].id);
		if (len > 0 && len == len_at &&
				(strncmp(at, oc_string(g_at_entries[i].id), len) == 0))
		{
			return i;
		}
	}
	return -1;
}

/* finds 0 ==> id */
static oc_string_t*
find_access_token_from_payload(oc_rep_t* object)
{
	oc_string_t* index = NULL;
	while (object != NULL)
	{
		switch (object->type)
		{
			case OC_REP_BYTE_STRING:
			{
				if (oc_string_len(object->name) == 0 && object->iname == 0)
				{
					index = &object->value.string;
					PRINT("find_access_token_from_payload: %s ", oc_string_checked(*index));
					return index;
				}
			} break;
			case OC_REP_STRING:
			{
				if (oc_string_len(object->name) == 0 && object->iname == 0)
				{
					index = &object->value.string;
					PRINT("find_access_token_from_payload: %s ", oc_string_checked(*index));
					return index;
				}
			} break;
			default:
				break;
		}
		object = object->next;
	}
	PRINT("find_access_token_from_payload Error");
	return index;
}

int oc_core_get_at_table_size(void)
{
	return G_AT_MAX_ENTRIES;
}

// TODO use static variable
int oc_core_items_used_in_auth_at_table(void)
{
	int counter = 0;
	for (int i = 0; i < G_AT_MAX_ENTRIES; i++)
	{
		if (oc_string_len(g_at_entries[i].id) > 0)
		{
			counter++;
		}
	}
	return counter;
}

// ----------------------------------------------------------------------------

static void oc_core_auth_at_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;

	int query_parameter_kvpair_matches = 0;  // how many (to this device applicable) query parameter key/value pair matches where found 
	size_t response_length = 0;
	int query_pn = PAGE_NUMBER;
	int query_ps = PAGE_SIZE;

	PRINT("oc_core_auth_at_get_handler - start");

	if (!oc_accept_header_is_ok(request, APPLICATION_LINK_FORMAT))
	{
		return;
	}

	// current resource amount
	const int total = oc_core_items_used_in_auth_at_table();

	// handle query parameters l=ps and/or l=total
	if (query_l_was_processed(request, PAGE_SIZE, total))
		return;

	// empty table returns an empty link-response
	if (total == 0)
	{
		oc_prepare_linkformat_response(request, OC_STATUS_OK, 0);
		return;
	}

	// first entry number of a resource that will be placed on a page
	const int first_entry = evaluate_query_px(request, &query_pn, &query_ps);

	// check if requested page will carry at least one resource e.g
	// - total=4, pn 5, ps 20, first entry = 100 -> no data on page 5 (all on page 0)
	// - total=4, pn 1, ps 04, first entry = 004 -> no data on page 1 (all on page 0)
	if (first_entry >= total || query_ps == 0)
	{
		oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
		return;
	}

	/// entries don't fit in a single page -> more pages are needed to get the full list
	// - total=4, page number 1, page size 02, first entry = 002 -> no more data on next page 
	// - total=4, page number 1, page size 01, first entry = 001 -> more data on next page
	const bool more_request_needed = total > first_entry + query_ps ? true : false;

	// example </auth/at/token-id>;ct=60 ; must run through entire table since entries are stored randomly 
	for (int i = first_entry; i < G_AT_MAX_ENTRIES; i++)
	{
		if (oc_string_len(g_at_entries[i].id) > 0)
		{
			if (response_length > 0)
			{
				// close previous record to create a new without LF (not found in RFC 6690)
				response_length += oc_rep_add_line_to_buffer(",");
			}

			response_length += oc_rep_add_line_to_buffer("</auth/at/");
			response_length += oc_rep_add_line_to_buffer(oc_string(g_at_entries[i].id));
			response_length += oc_rep_add_line_to_buffer(">;ct=60");

			query_parameter_kvpair_matches++;

			if (query_parameter_kvpair_matches >= query_ps)
			{ // don't add more than page supports
				break;
			}
		}
	}

	if (more_request_needed)
	{
		// no page # was in the request (query_p =0) = next page 1 else #+1
		response_length += add_next_page_indicator(oc_string(request->resource->uri), ++query_pn);
	}
	oc_prepare_linkformat_response(request, OC_STATUS_OK, response_length);

	PRINT("oc_core_auth_at_get_handler - end");
}

static void oc_core_auth_at_post_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;
	oc_rep_t* rep = NULL;
	oc_rep_t* object = NULL;
	oc_rep_t* subobject = NULL;
	oc_rep_t* oscobject = NULL;
	oc_status_t return_status = OC_STATUS_BAD_REQUEST;
	bool scope_updated = false;
	bool other_updated = false;
	int index = -1;
	PRINT("oc_core_auth_at_post_handler");

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}
	size_t device_index = request->resource->device;

	/* debugging info */
	oc_print_rep_as_json(request->request_payload, true);

	rep = request->request_payload;
	while (rep != NULL)
	{
		if (rep->type == OC_REP_OBJECT)
		{
			// Check if payload valid
			object = rep->value.object;
			while (object != NULL)
			{
				if (object->type == OC_REP_MIXED_ARRAY)
				{
					PRINT("mixed array as scope is not allowed!");
					oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
					return;
				}
				object = object->next;
			}

			object = rep->value.object;
			oc_string_t* at = find_access_token_from_payload(object);
			if (at == NULL)
			{
				PRINT("access token not found!");
				oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
				return;
			}
			index = find_index_from_at(at);
			if (index != -1)
			{
				PRINT("entry already exist!");
				return_status = OC_STATUS_CHANGED;
			}
			else
			{
				index = find_empty_at_index();
				return_status = OC_STATUS_CREATED;
				if (index == -1)
				{
					PRINT("no space left!");
					oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
					return;
				}
			}
			oc_free_string(&(g_at_entries[index].id));
			oc_new_string(&g_at_entries[index].id, oc_string(*at),
										oc_string_len(*at));

			bool id_only = true;
			object = rep->value.object;
			while (object != NULL)
			{
				if (object->type == OC_REP_STRING_ARRAY)
				{
					id_only = false;
					// scope
					if (object->iname == 9)
					{
						// scope: array of interfaces as string
						oc_string_array_t str_array;
						size_t str_array_size = 0;
						oc_interface_mask_t interfaces = OC_IF_NONE;
						oc_rep_i_get_string_array(object, 9, &str_array, &str_array_size);
						for (size_t i = 0; i < str_array_size; i++)
						{
							char* if_str = oc_string_array_get_item(str_array, i);
							oc_interface_mask_t if_mask = oc_ri_get_interface_mask(if_str, strlen(if_str));
							if (if_mask == OC_IF_LI)
							{
								OC_ERR("   if.ll is not a valid access scope!");
								oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
								return;
							}
							interfaces = interfaces + if_mask;
						}
						g_at_entries[index].scope = interfaces;
						scope_updated = true;
					}
				}
				else if (object->type == OC_REP_INT_ARRAY)
				{
					id_only = false;
					// scope
					if (object->iname == 9)
					{
						g_at_entries[index].scope = OC_IF_NONE;
						int64_t* array = 0;
						size_t array_size = 0;
						// not making a deep copy
						oc_rep_i_get_int_array(object, 9, &array, &array_size);
						if (array_size > 0)
						{
							// make the deep copy
							if ((g_at_entries[index].ga_len > 0) &&
									(&g_at_entries[index].ga != NULL))
							{
								int64_t* cur_arr = g_at_entries[index].ga;
								if (cur_arr)
								{
									free(cur_arr);
								}
								g_at_entries[index].ga = NULL;
							}
							g_at_entries[index].ga_len = (int) array_size;
							// always set the group address scope, if there is 1 or more ga entries
							g_at_entries[index].scope = OC_IF_G;
							int64_t* new_array =
								(int64_t*) malloc(array_size * sizeof(uint64_t));
							if (new_array)
							{
								for (size_t i = 0; i < array_size; i++)
								{
									new_array[i] = array[i];
								}
								g_at_entries[index].ga = new_array;
							}
							else
							{
								OC_ERR("out of memory");
							}
						}
					}
				}
				else if (object->type == OC_REP_STRING)
				{
					if (object->iname != 0)
					{
						id_only = false;
					}
					if (object->iname == 2)
					{
						// sub
						oc_free_string(&(g_at_entries[index].sub));
						oc_new_string(&g_at_entries[index].sub,
													oc_string(object->value.string),
													oc_string_len(object->value.string));
					}
					// if (object->iname == 3) {
					//  aud
					//  oc_free_string(&(g_at_entries[index].aud));
					//  oc_new_string(&g_at_entries[index].aud,
					//                oc_string(object->value.string),
					//                oc_string_len(object->value.string));
					//}
				}
				else if (object->type == OC_REP_INT)
				{
					id_only = false;
					if (object->iname == 38)
					{
						// profile (38 ("coap_dtls" ==1 or "coap_oscore" == 2))
						PRINT("profile %d", (int) object->value.integer);
						g_at_entries[index].profile = (int) object->value.integer;
					}
				}
				else if (object->type == OC_REP_OBJECT)
				{
					id_only = false;
					// level of cnf or sub.
					subobject = object->value.object;
					int subobject_nr = object->iname;
					PRINT("subobject_nr %d", subobject_nr);
					while (subobject)
					{
						if (subobject->type == OC_REP_STRING)
						{
							if (subobject->iname == 3 && subobject_nr == 8)
							{
								// cnf::kid (8::3)
								oc_free_string(&(g_at_entries[index].kid));
								oc_new_string(&g_at_entries[index].kid,
															oc_string(subobject->value.string),
															oc_string_len(subobject->value.string));
							}
						}
						else if (subobject->type == OC_REP_OBJECT)
						{
							oscobject = subobject->value.object;
							int oscobject_nr = subobject->iname;
							while (oscobject)
							{
								if (oscobject->type == OC_REP_INT)
								{
									if (oscobject->iname == 4 && subobject_nr == 8 &&
											oscobject_nr == 4)
									{
										// not storing it: we only support value 10 at the moment.
										// g_at_entries[index].osc_alg = (int)object->value.integer;
										if ((int) object->value.integer != 10)
										{
											OC_ERR("algorithm is not 10 : %d",
														 (int) object->value.integer);
											return_status = OC_STATUS_BAD_REQUEST;
										}
										other_updated = true;
									}
								}
								if (oscobject->type == OC_REP_BYTE_STRING)
								{
									if (oscobject->iname == 2 && subobject_nr == 8 &&
											oscobject_nr == 4)
									{
										// cnf::osc::ms
										oc_free_string(&(g_at_entries[index].osc_ms));
										oc_new_byte_string(&g_at_entries[index].osc_ms,
																			 oc_string(oscobject->value.string),
																			 oc_string_len(oscobject->value.string));
										other_updated = true;
									}
									if (oscobject->iname == 6 && subobject_nr == 8 &&
											oscobject_nr == 4)
									{
										// cnf::osc::contextId
										oc_free_string(&(g_at_entries[index].osc_contextid));
										oc_new_byte_string(&g_at_entries[index].osc_contextid,
																			 oc_string(oscobject->value.string),
																			 oc_string_len(oscobject->value.string));
										other_updated = true;
									}
									// if (oscobject->iname == 7 && subobject_nr == 8 &&
									//     oscobject_nr == 4) {
									//   // cnf::osc::rid
									//   oc_free_string(&(g_at_entries[index].osc_rid));
									//   oc_new_byte_string(&g_at_entries[index].osc_rid,
									//                      oc_string(oscobject->value.string),
									//                      oc_string_len(oscobject->value.string));
									//   other_updated = true;
									// }
									if (oscobject->iname == 0 && subobject_nr == 8 &&
											oscobject_nr == 4)
									{
										// cnf::osc::id
										oc_free_string(&(g_at_entries[index].osc_id));
										oc_new_byte_string(&g_at_entries[index].osc_id,
																			 oc_string(oscobject->value.string),
																			 oc_string_len(oscobject->value.string));
										other_updated = true;
									}
									if (oscobject->iname == 5 && subobject_nr == 8 &&
											oscobject_nr == 4)
									{
										// cnf::osc::salt
										oc_free_string(&(g_at_entries[index].osc_salt));
										oc_new_byte_string(&g_at_entries[index].osc_salt,
																			 oc_string(oscobject->value.string),
																			 oc_string_len(oscobject->value.string));
										other_updated = true;
									}
								} /* type */

								oscobject = oscobject->next;
							}
						}
						subobject = subobject->next;
					}
				}
				object = object->next;
			} // while (inner object)
			if (id_only)
			{
				PRINT("only found id in request, deleting entry at index: %d", index);
				oc_at_delete_entry(device_index, index);
			}
			else
			{
				PRINT("storage index: %d (%s) ", index, oc_string_checked(*at));
				// show the entry on screen
				oc_print_auth_at_entry(device_index, index);

				// dump the entry to persistent storage
				oc_at_dump_entry(device_index, index);
			}
		} // if type == object
		rep = rep->next;
	} // while (rep)

	PRINT("oc_core_auth_at_post_handler - activating oscore context");
	// add the oscore contexts by reinitializing all used oscore keys.
	// do not update the oscore when:
	// - update of the scope contents only
	if ((return_status == OC_STATUS_CHANGED) && (other_updated == false) &&
			(scope_updated == true))
	{
		OC_WRN("update scope only");
	}
	else
	{
		// update the oscore context
		oc_init_oscore_from_storage(device_index, false);
	}

	oc_prepare_no_format_response_no_payload(request, return_status);
	PRINT("oc_core_auth_at_post_handler - end");
}

static void oc_core_auth_at_delete_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;
	PRINT("oc_core_auth_at_delete_handler - start");

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}

	size_t device_index = request->resource->device;
	oc_delete_at_table(device_index);

	oc_prepare_no_format_response_no_payload(request, OC_STATUS_DELETED);
	PRINT("oc_core_auth_at_delete_handler - end");
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(knx_auth_at, knx_auth_at_x, 0, "/auth/at",
																		 APPLICATION_LINK_FORMAT, APPLICATION_CBOR,		// second ct (CBOR)  is wrong, it is used from post (not get) but demanded by certification test ...
																		 OC_DISCOVERABLE,
																		 oc_core_auth_at_get_handler, OC_ACL_P | OC_ACL_D | OC_ACL_C | OC_ACL_SEC, OC_IF_LI,
																		 0, OC_ACL_NONE, OC_IF_NONE,
																		 oc_core_auth_at_post_handler, OC_ACL_SEC, OC_IF_SEC,
																		 oc_core_auth_at_delete_handler, OC_ACL_SEC, OC_IF_SEC,
																		 NULL, OC_SIZE_MANY(1), "urn:knx:fb.at");

void oc_create_auth_at_resource(int resource_idx, size_t device)
{
	oc_core_populate_resource(resource_idx, device, "/auth/at",
														APPLICATION_LINK_FORMAT, CONTENT_NONE, OC_DISCOVERABLE, oc_core_auth_at_get_handler, 0,
														oc_core_auth_at_post_handler, oc_core_auth_at_delete_handler, 1,
														"urn:knx:fb.at");
}

// ----------------------------------------------------------------------------

static void oc_core_auth_at_x_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}
	PRINT("oc_core_auth_at_x_get_handler - start");

	// - find the id from the URL
	const char* value;
	int value_len = oc_uri_get_wildcard_value_as_string(
		oc_string(request->resource->uri), oc_string_len(request->resource->uri),
		request->uri_path, request->uri_path_len, &value);
	// - delete the index.
	if (value_len <= 0)
	{
		oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
		PRINT("index (at) not found");
		return;
	}
	PRINT("id = %.*s", value_len, value);
	// get the index
	int index = find_index_from_at_string(value, value_len);
	// - delete the index.
	if (index < 0)
	{
		oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
		PRINT("index in structure not found");
		return;
	}
	oc_print_auth_at_entry(0, index);

	// return the data
	oc_rep_begin_root_object();
	// profile : 38
	oc_rep_i_set_int(root, 38, g_at_entries[index].profile);
	// id : 0
	oc_rep_i_set_text_string(root, 0, oc_string(g_at_entries[index].id));
	// audience : 3
	// if (oc_string_len(g_at_entries[index].aud) > 0) {
	//  oc_rep_i_set_text_string(root, 3, oc_string(g_at_entries[index].aud));
	//}
	if (g_at_entries[index].ga_len > 0)
	{
		// group object list
		// taking input of int64 array
		oc_rep_i_set_int_array(root, 9, g_at_entries[index].ga, g_at_entries[index].ga_len);
	}
	else
	{
		// the scope as list of cflags or group object table entries
		const unsigned int nr_entries = oc_count_total_interfaces_in_mask(g_at_entries[index].scope);
		if (nr_entries > 0)
		{
			// interface list
			oc_string_array_t cflags_entries;
			oc_new_string_array(&cflags_entries, nr_entries);
			oc_put_interfaces_in_a_mask_in_string_array(g_at_entries[index].scope, cflags_entries);
			PRINT("%u entries in cflags", nr_entries);
			oc_rep_i_set_string_array(root, 9, cflags_entries);
			oc_free_string_array(&cflags_entries);
		}
	}
	if (g_at_entries[index].profile == OC_PROFILE_COAP_DTLS)
	{
		if (oc_string_len(g_at_entries[index].sub) > 0)
		{
			PRINT("sub    : %s", oc_string_checked(g_at_entries[index].sub));
		}
		if (oc_string_len(g_at_entries[index].kid) > 0)
		{
			PRINT("kid    : %s", oc_string_checked(g_at_entries[index].kid));
		}
	}
	if (g_at_entries[index].profile == OC_PROFILE_COAP_OSCORE ||
			g_at_entries[index].profile == OC_PROFILE_COAP_PASE)
	{
		// create cnf 98)
		oc_rep_i_set_key(&root_map, 8);
		CborEncoder cnf_map;
		cbor_encoder_create_map(&root_map, &cnf_map, CborIndefiniteLength);
		// create osc (4)
		oc_rep_i_set_key(&cnf_map, 4);
		CborEncoder osc_map;
		cbor_encoder_create_map(&cnf_map, &osc_map, CborIndefiniteLength);
		if (oc_string_len(g_at_entries[index].osc_ms) > 0)
		{
			oc_rep_i_set_byte_string(
				osc, 2, oc_string(g_at_entries[index].osc_ms),
				oc_byte_string_len(g_at_entries[index].osc_ms)); // root::cnf::osc::ms
		}
		// if (oc_string_len(g_at_entries[index].osc_alg) > 0) {
		//   oc_rep_i_set_text_string(
		//     osc, 4, oc_string(g_at_entries[index].osc_alg)); //
		//     root::cnf::osc::alg
		// }
		if (oc_string_len(g_at_entries[index].osc_contextid) > 0)
		{
			oc_rep_i_set_byte_string(
				osc, 6, oc_string(g_at_entries[index].osc_contextid),
				oc_byte_string_len(
				g_at_entries[index].osc_contextid)); // root::cnf::osc::contextid
		}
		// if (oc_string_len(g_at_entries[index].osc_rid) > 0) {
		//   oc_rep_i_set_byte_string(
		//     osc, 7, oc_string(g_at_entries[index].osc_rid),
		//     oc_byte_string_len(
		//       g_at_entries[index].osc_rid)); // root::cnf::osc::osc_rid
		// }
		if (oc_string_len(g_at_entries[index].osc_id) > 0)
		{
			oc_rep_i_set_byte_string(
				osc, 0, oc_string(g_at_entries[index].osc_id),
				oc_byte_string_len(
				g_at_entries[index].osc_id)); // root::cnf::osc::osc_id
		}
		cbor_encoder_close_container_checked(&cnf_map, &osc_map);
		cbor_encoder_close_container_checked(&root_map, &cnf_map);
	}

	oc_rep_end_root_object();

	oc_prepare_cbor_response(request, OC_STATUS_OK);
	PRINT("oc_core_auth_at_x_get_handler - end");
}

// probably no post handler needed
// partial update?
// how does that look like?
void oc_core_auth_at_x_post_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;
	oc_rep_t* rep = NULL;
	int cmd = 0;

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}
	PRINT("oc_core_auth_at_x_post_handler - start");
	bool changed = false;
	/* loop over the request document to check if all inputs are ok */
	rep = request->request_payload;
	while (rep != NULL)
	{
		PRINT("key: (check) %s ", oc_string_checked(rep->name));
		if (rep->type == OC_REP_STRING)
		{
			if (rep->iname == 2)
			{
				cmd = a_sen_convert_cmd(oc_string(rep->value.string));
				changed = true;
				break;
			}
		}
		rep = rep->next;
	}
	/* input was set, so create the response*/
	if (changed == true)
	{
		PRINT("cmd %d", cmd);
		oc_prepare_cbor_response(request, OC_STATUS_CHANGED);
		return;
	}
	oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
}

static void oc_core_auth_at_x_delete_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;
	const char* value;
	int value_len = -1;

	if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
	{
		return;
	}
	PRINT("oc_core_auth_at_x_delete_handler - start");
	size_t device_index = request->resource->device;

	// - find the id from the URL
	value_len = oc_uri_get_wildcard_value_as_string(
		oc_string(request->resource->uri), oc_string_len(request->resource->uri),
		request->uri_path, request->uri_path_len, &value);
	// - delete the index.
	if (value_len <= 0)
	{
		oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
		PRINT("index (at) not found");
		return;
	}
	PRINT("id = %.*s", value_len, value);
	// get the index
	int index = find_index_from_at_string(value, value_len);
	// - delete the index.
	if (index < 0)
	{
		oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
		PRINT("oc_core_auth_at_x_delete_handler: index in structure not found");
		return;
	}

	// actual delete of the context id so that this entry is seen as empty
	oc_at_delete_entry(device_index, index);
	// do the persistent storage
	oc_at_dump_entry(device_index, index);
	// delete the related oscore contexts
#ifdef OC_OSCORE
	oc_oscore_free_contexts_at_id(index);
#endif

	PRINT("oc_core_auth_at_x_delete_handler - done");
	oc_prepare_no_format_response_no_payload(request, OC_STATUS_DELETED);
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(knx_auth_at_x, knx_auth, 0, "/auth/at/*",
																		 APPLICATION_CBOR, CONTENT_NONE,
																		 OC_DISCOVERABLE,
																		 oc_core_auth_at_x_get_handler, OC_ACL_SEC, OC_IF_SEC,
																		 0, OC_ACL_NONE, OC_IF_NONE,
																		 0, OC_ACL_NONE, OC_IF_NONE,
																		 oc_core_auth_at_x_delete_handler, OC_ACL_SEC, OC_IF_SEC,
																		 NULL, OC_SIZE_MANY(1), "dpt.a[n]");

void
oc_create_auth_at_x_resource(int resource_idx, size_t device)
{
	OC_DBG("oc_create_auth_at_x_resource");

	oc_core_populate_resource(resource_idx, device, "/auth/at/*",
														APPLICATION_CBOR, CONTENT_NONE, OC_DISCOVERABLE,
														oc_core_auth_at_x_get_handler, 0, 0,
														oc_core_auth_at_x_delete_handler, 1, "dpt.a[n]");
}

// ----------------------------------------------------------------------------

static void
oc_core_knx_auth_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) data;
	(void) iface_mask;

	int query_parameter_kvpair_matches = 0; // how many (to this device applicable) query parameter key/value pair matches where found 
	size_t response_length = 0;
	int query_pn = PAGE_NUMBER;
	int query_ps = PAGE_SIZE;

	int first_entry = OC_KNX_AUTH_O;        // first entry number of a resource that will be placed on a page
	int last_entry = OC_KNX_AUTH_AT_X;      // last entry number of a resource that will be placed on a page
	int total = last_entry - first_entry;   // total entries of this resource
	bool more_request_needed = false;

	PRINT("oc_core_knx_auth_get_handler - start");

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
		if (more_request_needed)
		{
			// no page # was in the request (query_p =0) = next page 1 else #+1
			response_length += add_next_page_indicator(oc_string(request->resource->uri), ++query_pn);
		}
		oc_prepare_linkformat_response(request, OC_STATUS_OK, response_length);
	}
	else
	{
		// some resources are mandatory, hence this can't be correct here
		oc_prepare_no_format_response_no_payload(request, OC_STATUS_INTERNAL_SERVER_ERROR);
	}

	PRINT("oc_core_knx_auth_get_handler - end");
}

#ifdef OC_IOT_ROUTER
OC_CORE_CREATE_CONST_RESOURCE_LINKED(knx_auth, knx_fp_gm, 0, "/auth",
																		 OC_IF_LI | OC_IF_D,
																		 APPLICATION_LINK_FORMAT, OC_DISCOVERABLE,
																		 oc_core_knx_auth_get_handler, 0, 0, 0,
																		 NULL, OC_SIZE_ZERO());
#else
OC_CORE_CREATE_CONST_RESOURCE_LINKED(knx_auth, well_known_core, 0, "/auth",
																		 APPLICATION_LINK_FORMAT, CONTENT_NONE,
																		 OC_DISCOVERABLE,
																		 oc_core_knx_auth_get_handler, OC_ACL_P | OC_ACL_D | OC_ACL_C, OC_IF_LI,
																		 0, OC_ACL_NONE, OC_IF_NONE,
																		 0, OC_ACL_NONE, OC_IF_NONE,
																		 0, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_SIZE_ZERO());
#endif
void
oc_create_knx_auth_resource(int resource_idx, size_t device)
{
	OC_DBG("oc_create_knx_auth_resource");
	oc_core_populate_resource(resource_idx, device, "/auth",
														APPLICATION_LINK_FORMAT, CONTENT_NONE, 
														OC_DISCOVERABLE,
														oc_core_knx_auth_get_handler, 0, 0, 0, 0);
}

void
oc_print_auth_at_entry(size_t device_index, int index)
{
	(void) device_index;
#ifdef OC_PRINT

	if (index > -1)
	{
		if (oc_string_len(g_at_entries[index].id) > 0)
		{

			PRINT("at index      : %d", index);
			PRINT("id (0)        : %s", oc_string_checked(g_at_entries[index].id));
			PRINT("scope (9)     : %d", g_at_entries[index].scope);
			PRINT("profile (38)  : %d (%s)", g_at_entries[index].profile, oc_at_profile_to_string(g_at_entries[index].profile));
			if (g_at_entries[index].profile == OC_PROFILE_COAP_DTLS)
			{

				if (oc_string_len(g_at_entries[index].sub) > 0)
				{
					PRINT("sub           : %s", oc_string_checked(g_at_entries[index].sub));
				}

				if (oc_string_len(g_at_entries[index].kid) > 0)
				{
					PRINT("kid           : %s", oc_string_checked(g_at_entries[index].kid));
				}
			}
			if (g_at_entries[index].profile == OC_PROFILE_COAP_OSCORE ||
					g_at_entries[index].profile == OC_PROFILE_COAP_PASE)
			{
				if (oc_string_len(g_at_entries[index].osc_ms) > 0)
				{
					PRINT("osc:ms (h)    : (%d) ",
								(int) oc_byte_string_len(g_at_entries[index].osc_ms));
					oc_string_println_hex(g_at_entries[index].osc_ms);
				}
				if (oc_string_len(g_at_entries[index].osc_salt) > 0)
				{
					PRINT("osc:salt (h)  : (%d) ",
								(int) oc_byte_string_len(g_at_entries[index].osc_salt));
					oc_string_println_hex(g_at_entries[index].osc_salt);
				}
				if (oc_string_len(g_at_entries[index].osc_contextid) > 0)
				{
					PRINT("osc:ctx_id (h): (%d) ",
								(int) oc_byte_string_len(g_at_entries[index].osc_contextid));
					oc_string_println_hex(g_at_entries[index].osc_contextid);
				}
				if (oc_string_len(g_at_entries[index].osc_id) > 0)
				{
					PRINT("osc:id (h)    : (%d) ",
								(int) oc_byte_string_len(g_at_entries[index].osc_id));
					oc_string_println_hex(g_at_entries[index].osc_id);
				}
				if (oc_string_len(g_at_entries[index].osc_rid) > 0)
				{
					PRINT("osc:rid (h)   : (%d) ",
								(int) oc_byte_string_len(g_at_entries[index].osc_rid));
					oc_string_println_hex(g_at_entries[index].osc_rid);
				}
				if (g_at_entries[index].ga_len > 0)
				{
					PRINT("osc:ga        : [");
					for (int i = 0; i < g_at_entries[index].ga_len; i++)
					{
						PRINTF("%" PRIu64 "", (uint64_t) g_at_entries[index].ga[i]);
					}
					PRINTF("]");
				}
			}
		}
	}

#endif
}

oc_acl_mask_t oc_at_get_scope_mask(int index)
{
	if (index < 0)
	{
		return OC_ACL_NONE;
	}
	if (index > G_AT_MAX_ENTRIES - 1)
	{
		return OC_ACL_NONE;
	}
	return g_at_entries[index].scope;
}

int oc_at_delete_entry(size_t device_index, int index)
{
	(void) device_index;
	if (index < 0)
	{
		return -1;
	}
	if (index > G_AT_MAX_ENTRIES - 1)
	{
		return -1;
	}

	// generic
	oc_free_string(&g_at_entries[index].id);
	oc_new_string(&g_at_entries[index].id, "", 0);
	g_at_entries[index].scope = OC_IF_NONE;
	g_at_entries[index].profile = OC_PROFILE_UNKNOWN;
	// oscore object
	oc_free_string(&g_at_entries[index].osc_ms);
	oc_new_byte_string(&g_at_entries[index].osc_ms, "", 0);
	oc_free_string(&g_at_entries[index].osc_salt);
	oc_new_byte_string(&g_at_entries[index].osc_salt, "", 0);
	oc_free_string(&g_at_entries[index].osc_contextid);
	oc_new_byte_string(&g_at_entries[index].osc_contextid, "", 0);
	oc_free_string(&g_at_entries[index].osc_rid);
	oc_new_byte_string(&g_at_entries[index].osc_rid, "", 0);
	oc_free_string(&g_at_entries[index].osc_id);
	oc_new_byte_string(&g_at_entries[index].osc_id, "", 0);
	// dtls object
	oc_free_string(&g_at_entries[index].sub);
	oc_new_string(&g_at_entries[index].sub, "", 0);
	oc_free_string(&g_at_entries[index].kid);
	oc_new_string(&g_at_entries[index].kid, "", 0);

	if (g_at_entries[index].ga_len > 0)
	{
		uint64_t* cur_arr = g_at_entries[index].ga;
		if (cur_arr)
		{
			free(cur_arr);
		}
		g_at_entries[index].ga_len = 0;
	}

	char filename[20];
	snprintf(filename, 20, "%s_%d", AT_STORE, index);
	oc_storage_erase(filename);

	return 0;
}

// Note: storage of the fields is done via the cbor keys in the hierarchy
// so tag : 842  8.4.2 ==> "cnf":"osc":"ms"
static void
oc_at_dump_entry(size_t device_index, int entry)
{
	(void) device_index;
#ifndef OC_USE_STORAGE
	(void)entry;
	PRINT("no auth/at storage");
#else
	char filename[20];

	snprintf(filename, 20, "%s_%d", AT_STORE, entry);
	uint8_t* buf = malloc(OC_MAX_APP_DATA_SIZE);
	if (!buf)
		return;

	oc_rep_new(buf, OC_MAX_APP_DATA_SIZE);
	// write the data
	oc_rep_begin_root_object();
	// id 0
	oc_rep_i_set_text_string(root, 0, oc_string(g_at_entries[entry].id));
	// interface 9 /// this is different than the response on the wire
	oc_rep_i_set_int(root, 9, g_at_entries[entry].scope);
	oc_rep_i_set_int(root, 38, g_at_entries[entry].profile);
	oc_rep_i_set_byte_string(root, 842, oc_string(g_at_entries[entry].osc_ms),
													 oc_byte_string_len(g_at_entries[entry].osc_ms));
	oc_rep_i_set_byte_string(
		root, 846, oc_string(g_at_entries[entry].osc_contextid),
		oc_byte_string_len(g_at_entries[entry].osc_contextid));
	oc_rep_i_set_byte_string(root, 847, oc_string(g_at_entries[entry].osc_rid),
													 oc_byte_string_len(g_at_entries[entry].osc_rid));
	oc_rep_i_set_byte_string(root, 840, oc_string(g_at_entries[entry].osc_id),
													 oc_byte_string_len(g_at_entries[entry].osc_id));
	oc_rep_i_set_text_string(root, 82, oc_string(g_at_entries[entry].sub));
	oc_rep_i_set_text_string(root, 81, oc_string(g_at_entries[entry].kid));
	oc_rep_i_set_int_array(root, 777, g_at_entries[entry].ga,
												 g_at_entries[entry].ga_len);

	oc_rep_end_root_object();

	int size = oc_rep_get_encoded_payload_size();
	if (size > 0)
	{
		OC_DBG("oc_at_dump_entry: dumped current state [%s] [%d]: "
					 "size %d",
					 filename, entry, size);
		long written_size = oc_storage_write(filename, buf, size);
		if (written_size != (long) size)
		{
			PRINT("oc_at_dump_entry: [%s] written %d != %d (towrite)", filename,
						(int) written_size, size);
		}
	}
	free(buf);
#endif /* OC_USE_STORAGE */
}

static void
oc_at_load_entry(int entry)
{
	int ret;
	char filename[20];
	oc_rep_t* rep, * head;
	snprintf(filename, 20, "%s_%d", AT_STORE, entry);
	uint8_t* buf = malloc(OC_MAX_APP_DATA_SIZE);
	if (!buf)
		return;

	ret = oc_storage_read(filename, buf, OC_MAX_APP_DATA_SIZE);
	if (ret > 0)
	{
		struct oc_memb rep_objects = { sizeof(oc_rep_t), 0, 0, 0, 0 };
		oc_rep_set_pool(&rep_objects);
		int err = oc_parse_rep(buf, ret, &rep);
		head = rep;
		if (err == 0)
		{
			while (rep != NULL)
			{
				switch (rep->type)
				{

					case OC_REP_INT:
						if (rep->iname == 9)
						{
							g_at_entries[entry].scope = (int) rep->value.integer;
						}
						if (rep->iname == 38)
						{
							g_at_entries[entry].profile = (int) rep->value.integer;
						}
						break;
					case OC_REP_STRING:
						if (rep->iname == 0)
						{
							oc_free_string(&g_at_entries[entry].id);
							oc_new_string(&g_at_entries[entry].id, oc_string(rep->value.string),
														oc_string_len(rep->value.string));
						}
						if (rep->iname == 82)
						{
							oc_free_string(&g_at_entries[entry].sub);
							oc_new_string(&g_at_entries[entry].sub,
														oc_string(rep->value.string),
														oc_string_len(rep->value.string));
						}
						if (rep->iname == 81)
						{
							oc_free_string(&g_at_entries[entry].kid);
							oc_new_string(&g_at_entries[entry].kid,
														oc_string(rep->value.string),
														oc_string_len(rep->value.string));
						}
						break;
					case OC_REP_BYTE_STRING:
						// note: reading back the strings should be done with oc_string_len
						// not with oc_byte_string_len
						if (rep->iname == 840)
						{
							oc_free_string(&g_at_entries[entry].osc_id);
							oc_new_byte_string(&g_at_entries[entry].osc_id,
																 oc_string(rep->value.string),
																 oc_string_len(rep->value.string));
						}
						if (rep->iname == 842)
						{
							oc_free_string(&g_at_entries[entry].osc_ms);
							oc_new_byte_string(&g_at_entries[entry].osc_ms,
																 oc_string(rep->value.string),
																 oc_string_len(rep->value.string));
						}
						if (rep->iname == 846)
						{
							oc_free_string(&g_at_entries[entry].osc_contextid);
							oc_new_byte_string(&g_at_entries[entry].osc_contextid,
																 oc_string(rep->value.string),
																 oc_string_len(rep->value.string));
						}

						if (rep->iname == 847)
						{
							oc_free_string(&g_at_entries[entry].osc_rid);
							oc_new_byte_string(&g_at_entries[entry].osc_rid,
																 oc_string(rep->value.string),
																 oc_string_len(rep->value.string));
						}
						break;
					case OC_REP_INT_ARRAY:
						if (rep->iname == 777)
						{
							int64_t* array = oc_int_array(rep->value.array);
							int array_size = (int) oc_int_array_size(rep->value.array);

							if (array_size > 0)
							{
								// make the deep copy
								if (g_at_entries[entry].ga_len > 0)
								{
									uint64_t* cur_arr = g_at_entries[entry].ga;
									if (cur_arr)
									{
										free(cur_arr);
									}
									g_at_entries[entry].ga = NULL;
									// free(&g_at_entries[entry].ga);
								}
								g_at_entries[entry].ga_len = array_size;
								int64_t* new_array =
									(int64_t*) malloc(array_size * sizeof(uint64_t));

								if (new_array)
								{
									for (int i = 0; i < array_size; i++)
									{
										new_array[i] = array[i];
									}
									g_at_entries[entry].ga = new_array;
								}
								else
								{
									OC_ERR("out of memory");
								}
							}
						}
						break;
					default:
						break;
				}
				rep = rep->next;
			}
		}
		oc_free_rep(head);
	}
	free(buf);
}

int
oc_core_set_at_table(size_t device_index, int index, oc_auth_at_t entry,
										 bool store)
{
	(void) device_index;
	if (index < G_AT_MAX_ENTRIES)
	{

		oc_free_string(&g_at_entries[index].id);
		oc_new_string(&g_at_entries[index].id, oc_string(entry.id),
									oc_string_len(entry.id));
		g_at_entries[index].scope = entry.scope;
		g_at_entries[index].profile = entry.profile;
		oc_free_string(&g_at_entries[index].sub);
		oc_new_string(&g_at_entries[index].sub, oc_string(entry.sub),
									oc_string_len(entry.sub));
		oc_free_string(&g_at_entries[index].kid);
		oc_new_string(&g_at_entries[index].kid, oc_string(entry.kid),
									oc_string_len(entry.kid));
		oc_free_string(&g_at_entries[index].osc_ms);
		oc_new_byte_string(&g_at_entries[index].osc_ms, oc_string(entry.osc_ms),
											 oc_byte_string_len(entry.osc_ms));
		// oc_free_string(&g_at_entries[index].osc_alg);
		// oc_new_string(&g_at_entries[index].osc_alg, oc_string(entry.osc_alg),
		//               oc_string_len(entry.osc_alg));
		oc_free_string(&g_at_entries[index].osc_contextid);
		oc_new_byte_string(&g_at_entries[index].osc_contextid,
											 oc_string(entry.osc_contextid),
											 oc_byte_string_len(entry.osc_contextid));
		oc_free_string(&g_at_entries[index].osc_rid);
		oc_new_byte_string(&g_at_entries[index].osc_rid, oc_string(entry.osc_rid),
											 oc_byte_string_len(entry.osc_rid));
		oc_free_string(&g_at_entries[index].osc_id);
		oc_new_byte_string(&g_at_entries[index].osc_id, oc_string(entry.osc_id),
											 oc_byte_string_len(entry.osc_id));
		// clean up existing entry
		if (g_at_entries[index].ga_len > 0)
		{
			int64_t* cur_arr = g_at_entries[index].ga;
			if (cur_arr)
			{
				free(cur_arr);
			}
		}
		// copy initial data
		g_at_entries[index].ga_len = entry.ga_len;
		g_at_entries[index].ga = NULL;
		// copy the array
		if (g_at_entries[index].ga_len > 0)
		{
			int array_size = g_at_entries[index].ga_len;
			g_at_entries[index].ga_len = (int) array_size;
			int64_t* new_array = (int64_t*) malloc(array_size * sizeof(uint64_t));
			if (new_array)
			{
				for (size_t i = 0; i < array_size; i++)
				{
					new_array[i] = entry.ga[i];
				}
				g_at_entries[index].ga = new_array;
			}
			else
			{
				OC_ERR("out of memory");
				return -1;
			}
		}

		if (store)
		{
			oc_at_dump_entry(device_index, index);
		}
	}
	// activate the credentials
	OC_DBG_OSCORE("oc_core_set_at_table: activating OSCORE credentials");
	oc_init_oscore_from_storage(device_index, false);

	return 0;
}

int
oc_core_find_at_entry_with_id(size_t device_index, char* id)
{
	for (int i = 0; i < G_AT_MAX_ENTRIES; i++)
	{
		if ((oc_string_len(g_at_entries[i].id) > 0) &&
				(strncmp(oc_string(g_at_entries[i].id), id, strlen(id)) == 0))
		{
			return i;
		}
	}
	return -1;
}

int oc_core_find_pase_entry(size_t device_index)
{
	for (int i = 0; i < G_AT_MAX_ENTRIES; i++)
	{
		if (g_at_entries[i].profile == OC_PROFILE_COAP_PASE)
			return i;
	}
	return -1;
}

int
oc_core_find_at_entry_with_osc_id(size_t device_index, uint8_t* osc_id,
																	size_t osc_id_len)
{
	for (int i = 0; i < G_AT_MAX_ENTRIES; i++)
	{
		if (oc_byte_string_len(g_at_entries[i].osc_id) == osc_id_len &&
				memcmp(oc_string(g_at_entries[i].osc_id), osc_id, osc_id_len) == 0)
		{
			return i;
		}
	}
	return -1;
}

int
oc_core_find_at_entry_empty_slot(size_t device_index)
{
	for (int i = 0; i < G_AT_MAX_ENTRIES; i++)
	{
		if (oc_string_len(g_at_entries[i].id) == 0)
		{
			return i;
		}
	}
	return -1;
}

void
oc_load_at_table(size_t device_index)
{
	PRINT("Loading AT Table from Persistent storage");
	for (int i = 0; i < G_AT_MAX_ENTRIES; i++)
	{
		oc_at_load_entry(i);
		if (oc_string_len(g_at_entries[i].id) > 0)
		{
			oc_print_auth_at_entry(device_index, i);
		}
	}
	// create the oscore contexts
	oc_init_oscore_from_storage(device_index, true);
}

void
oc_delete_at_table(size_t device_index)
{
	PRINT("Deleting AT Object Table from Persistent storage");
	for (int i = 0; i < G_AT_MAX_ENTRIES; i++)
	{
		oc_at_delete_entry(device_index, i);
		oc_print_auth_at_entry(device_index, i);
	}
#ifdef OC_OSCORE
	oc_oscore_free_all_contexts();
#endif
}

void
oc_reset_at_table(size_t device_index, int erase_code)
{
	PRINT("Reset AT Object Table: %d", erase_code);

	if (erase_code == 2)
	{
		oc_delete_at_table(device_index);
	}
	else if (erase_code == 7)
	{
		// reset the entries that are not "if.sec"
		oc_interface_mask_t scope = OC_IF_NONE;
		for (int i = 0; i < G_AT_MAX_ENTRIES; i++)
		{
			scope = oc_at_get_scope_mask(i);
			// OC_IF_SEC flag not set
			if ((scope & OC_IF_SEC) == 0)
			{
				// reset the entries that are not "if.sec"
				oc_at_delete_entry(device_index, i);
				oc_print_auth_at_entry(device_index, i);
			}
		}
	#ifdef OC_OSCORE
		// create the oscore contexts that still remain
		oc_init_oscore_from_storage(device_index, true);
	#endif
	}
}

// ----------------------------------------------------------------------------

void
oc_oscore_set_auth_shared(char* client_senderid, int client_senderid_size,
													char* client_recipientid, int client_recipientid_size,
													uint8_t* shared_key, int shared_key_size)
{
	oc_auth_at_t spake_entry;
	memset(&spake_entry, 0, sizeof(spake_entry));
	// this is the index in the table, so it is the full string
	oc_new_string(&spake_entry.id, client_senderid, client_senderid_size);
	spake_entry.ga_len = 0;
	spake_entry.profile = OC_PROFILE_COAP_PASE;
	spake_entry.scope = OC_ACL_SEC;
	oc_new_byte_string(&spake_entry.osc_ms, (char*) shared_key, shared_key_size);
	// no context id
	oc_new_byte_string(&spake_entry.osc_rid, client_recipientid,
										 client_recipientid_size);
	// note that HEX was NOT on the wire, but the byte string.
	// so we have to store the byte string
	oc_new_byte_string(&spake_entry.osc_id, client_senderid,
										 client_senderid_size);

	int index = oc_core_find_at_entry_with_id(0, oc_string(spake_entry.id));
	if (index == -1)
	{
		index = oc_core_find_at_entry_empty_slot(0);
	}
	if (index == -1)
	{
		OC_ERR("no space left in auth/at");
	}
	else
	{
		oc_core_set_at_table((size_t) 0, index, spake_entry, true);
		oc_at_dump_entry((size_t) 0, index);
		// add the oscore context...
		oc_init_oscore(0);
	}
}

void
oc_oscore_set_auth_mac(char* client_senderid, int client_senderid_size,
											 char* client_recipientid, int client_recipientid_size,
											 uint8_t* shared_key, int shared_key_size)
{
	// create the token & store in at tables at position 0
	// note there should be no entries.. if there is an entry then overwrite
	// it..
	PRINT("oc_oscore_set_auth_mac sn       : %s", client_senderid);
	PRINT("oc_oscore_set_auth_mac rid [%d] : ", client_recipientid_size);
	oc_char_println_hex(client_recipientid, client_recipientid_size);
	PRINT("oc_oscore_set_auth_mac ms  [%d] : ", shared_key_size);
	oc_char_println_hex(shared_key, shared_key_size);

	oc_oscore_set_auth_shared(client_senderid, client_senderid_size,
														client_recipientid, client_recipientid_size,
														shared_key, shared_key_size);
}

void
oc_oscore_set_auth_device(char* client_senderid, int client_senderid_size,
													char* client_recipientid, int client_recipientid_size,
													uint8_t* shared_key, int shared_key_size)
{
	PRINT("oc_oscore_set_auth_device sn :%s", client_senderid);
	PRINT("oc_oscore_set_auth_device rid : (%d) ", client_recipientid_size);
	oc_char_println_hex(client_recipientid, client_recipientid_size);
	PRINT("oc_oscore_set_auth_device ms : (%d) ", shared_key_size);
	oc_char_println_hex(shared_key, shared_key_size);

	oc_oscore_set_auth_shared(client_senderid, client_senderid_size,
														client_recipientid, client_recipientid_size,
														shared_key, shared_key_size);
}

oc_auth_at_t*
oc_get_auth_at_entry(size_t device_index, int index)
{
	(void) device_index;

	if (index < 0)
	{
		return NULL;
	}
	if (index >= G_AT_MAX_ENTRIES)
	{
		return NULL;
	}
	return &g_at_entries[index];
}

void oc_create_knx_sec_resources(size_t device_index)
{
	OC_DBG("oc_create_knx_sec_resources");

	oc_load_at_table(device_index);

	if (device_index == 0)
	{
		OC_DBG("device 0: KNX security resources created statically");
		return;
	}

	oc_create_knx_auth_o_replwdo_resource(OC_KNX_AUTH_O_REPLWDO, device_index);
	oc_create_knx_auth_o_osndelay_resource(OC_KNX_AUTH_O_OSNDELAY, device_index);
	oc_create_knx_auth_o_resource(OC_KNX_AUTH_O, device_index);
	oc_create_a_sen_resource(OC_KNX_A_SEN, device_index);

	oc_create_auth_at_resource(OC_KNX_AUTH_AT, device_index);
	oc_create_auth_at_x_resource(OC_KNX_AUTH_AT_X, device_index);
	oc_create_knx_auth_resource(OC_KNX_AUTH, device_index);
}

void oc_init_oscore(size_t device_index)
{
	oc_init_oscore_from_storage(device_index, false);
}

void oc_init_oscore_from_storage(size_t device_index, const bool from_storage)
{
#ifndef OC_OSCORE
	(void)device_index;
#else 

	OC_DBG_OSCORE("oc_init_oscore deleting old sender contexts!!");
	oc_oscore_free_sender_contexts();

	OC_DBG_OSCORE("oc_init_oscore adding OSCORE context...");
	for (int i = 0; i < G_AT_MAX_ENTRIES; i++)
	{

		if (oc_string_len(g_at_entries[i].id) > 0)
		{
			oc_print_auth_at_entry(device_index, i);

			if (g_at_entries[i].profile == OC_PROFILE_COAP_OSCORE ||
					g_at_entries[i].profile == OC_PROFILE_COAP_PASE)
			{
				uint64_t ssn = 0;
				oc_oscore_context_t* ctx = oc_oscore_add_context(
					device_index, oc_string(g_at_entries[i].osc_id),
					oc_byte_string_len(g_at_entries[i].osc_id),
					oc_string(g_at_entries[i].osc_rid),
					oc_byte_string_len(g_at_entries[i].osc_rid), ssn, "desc",
					oc_string(g_at_entries[i].osc_ms),
					oc_byte_string_len(g_at_entries[i].osc_ms),
					oc_string(g_at_entries[i].osc_salt),
					oc_byte_string_len(g_at_entries[i].osc_salt),
					oc_string(g_at_entries[i].osc_contextid),
					oc_byte_string_len(g_at_entries[i].osc_contextid), i, from_storage);
				if (ctx == NULL)
				{
					OC_ERR("  failed to load index= %d", i);
				}

				// contexts for sending have populated sender id and null receive id
				// the spake key, however, is for receiving only, and the osc_id is
				// already inside receiver_id.

				// posts to auth/at set id into the sender id, so the created contexts
				// are only usable for sending. recipient contexts are created
				// dynamically with the key from the access token matching the key ID,
				// and with the context id within the received request
			}
			else
			{
				OC_DBG_OSCORE("oc_init_oscore: no oscore context");
			}
		}
	}
#endif
}

bool oc_knx_contains_interface(oc_interface_mask_t caller_scope, oc_interface_mask_t called_scope)
{
	if (caller_scope & called_scope)
	{
		// one of the entries is matching (bitset of 'a and b')
		return true;
	}
	return false;
}

bool oc_knx_sec_check_acl(oc_method_t method, oc_resource_t* resource, oc_endpoint_t* endpoint)
{
	// called resource scope, init with default
	oc_acl_mask_t called_res_scope = OC_ACL_NONE;

	// check for scope, considering of CoAP method (GET, ...)
	if (!oc_resource_get_acl_and_interface_mask(resource, method, &called_res_scope, NULL))
	{
		// resource or handler for method does not exist, no access
		return false;
	}

	// resource and handler for method exists...
	// uri len of resource versus uri len of caller endpoint was checked before (oc_ri.c)
	if (called_res_scope == OC_ACL_NONE)
	{
		// not a secure resource, access allowed
		// see table 5.1.3, all resources with methods that do not have any scope (= 'none')
		return true;
	}

	PRINT("method allowed flags:");
	PRINTipaddr_flags(*endpoint);

#ifdef OC_OSCORE

	if ((endpoint->flags & OSCORE + OSCORE_DECRYPTED) != OSCORE + OSCORE_DECRYPTED)
	{
		// not a OSCORE message that was able to decrypt with given security context (CCM, MAC) 
		OC_DBG_OSCORE("access denied for: %s [%s] with flags: %d", get_method_name(method), oc_string_checked(resource->uri), endpoint->flags);
		return false;
	}

	// Example for auth/o sub resource 
	// MAC writes - on tool key - if.p/d/c/sec/swu scopes in
	//     aut/at/o acl table entry (never an interface like if.ll or if.b)
	// DEV owns for each method AND resource a 'precompiled' acl and
	//     interface definition, auth/o GET: acl = if.p/d/c, interface = if.ll 
	//
	// The stack compares for the given method the auth/at acl scope with the
	// resource acl scope, at least one bit match = ok, otherwise 4.00 (bad request) response. 

	// caller scope, e.g. of the auth/at table entry that was used to decrypt the message
	// on OC_OSCORE defined the 'auth_at_index' is always incremented by  + 1 , so no extra sanity check here is needed
	const oc_acl_mask_t caller_acl_scope = oc_at_get_scope_mask(endpoint->auth_at_index - 1);

	// bitwise 'and' -> at least one scope must match
	if (!(caller_acl_scope & called_res_scope))
	{
		PRINT("caller scope vs resource %s scope = unauthorized: request %d vs resource %d :", oc_string(resource->uri), caller_acl_scope, called_res_scope);
		oc_print_acl_scopes(caller_acl_scope);
		oc_print_acl_scopes(called_res_scope);

		OC_WRN("resource call denied");

		return false;
	}

#endif

	// acl scopes + method checked or OC_OSCORE is off
	return true;
}