/*
// Copyright (c) 2016 Intel Corporation
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

#include "oc_client_state.h"
#include "messaging/coap/oc_coap.h"
#include "oc_api.h"
#include "oc_discovery.h"
#include "oc_knx_fb.h"
#include "oc_knx_fp.h"
#include "oc_core_res.h"
#include "oc_endpoint.h"
#include "oc_knx_helpers.h"

#ifdef OC_OSCORE
// #include "security/oc_pstat.h" ... former used by OS_SECURITY
// #include "security/oc_sdi.h"   ... former used by OS_SECURITY
#include "security/oc_tls.h"
#endif
#include <inttypes.h>
#include "oc_knx_dev.h"

/*
* - below resources must be in the unicast response for well-known/core certification tests,
*   other resources may be in real products, tests demands only those one
* - use int type to satisfy directly function call by using the array
*/
int basic_resources[] =
{
	OC_DEV, OC_KNX_K, OC_KNX_SWU, OC_KNX_AUTH
};

// (size of all)/(size of one) : 5 x int (4) / 4 = 20/4 = 5 
#define OC_NUM_MANDATORY_CORE_RESOURCES_PER_WK (int)( sizeof(basic_resources) / sizeof(basic_resources[0]) )

bool oc_add_resource_to_response_payload(const oc_resource_t* resource, oc_request_t* request, size_t* response_length, const bool truncate)
{

	if (resource == NULL || oc_string_len(resource->uri) == 0)
	{
		return false;
	}

	// close previous record to create a new record without a LF (not found in RFC 6690, also on JSON removed)
	if (*response_length > 0)
	{
		*response_length += oc_rep_add_line_to_buffer(",");
	}

	// <
	*response_length += oc_rep_add_line_to_buffer("<");

	// uri
	*response_length += oc_rep_add_line_to_buffer(oc_string(resource->uri));

	// >
	*response_length += oc_rep_add_line_to_buffer(">;");

	// rt's
	const int number_of_resource_types = oc_string_array_get_allocated_size(resource->types);

	if (number_of_resource_types > 0)
	{
		// open rt's
	  *response_length += oc_rep_add_line_to_buffer("rt=\"");

		for (int i = 0; i < number_of_resource_types; i++)
		{
			// rt's with FULL urn:knx
		  const int size = oc_string_array_get_item_size(resource->types, i);
			const char* t = oc_string_array_get_item(resource->types, i);
			if (size > 0)
			{
				if (i > 0)
				{ // not for the first rt ...

					// white space as separator between the rt values
					*response_length += oc_rep_add_line_to_buffer(" ");
				}

				if (!truncate)
				{ // rt's must be in the response with urn:knx (requester was not using urn:knx)

					// frame 1:1 (assumption urn:knx is always present in a resource)
					*response_length += oc_rep_add_line_size_to_buffer(t, size);
				}
				else
				{ // rt's must be in the response without urn:knx (7)

				  if (strncmp(t, "urn:knx", 7) == 0)
					{
						// take it only if urn:knx is in and frame a chunk with offset '7' after 'urn:knx'
						*response_length += oc_rep_add_line_size_to_buffer(&t[7], (int) size - 7);
					}
					else
					{
						// does not start with urn:knx, so frame what you have (e.g; vendor namespaces rt's such as urn:abb)
						*response_length += oc_rep_add_line_size_to_buffer(t, size);
					}
				}
			}
		}

		// close rt's 
		*response_length += oc_rep_add_line_to_buffer("\";");
	}

	// if's, if present
	oc_interface_mask_t interfaces = OC_IF_NONE;

	if (oc_resource_get_all_interfaces_for_a_resource(resource, &interfaces))
	{
		// open if's
	  *response_length += oc_rep_add_line_to_buffer("if=\"");
		*response_length += oc_frame_interfaces_mask_in_response(interfaces, truncate);
		// close if's
		*response_length += oc_rep_add_line_to_buffer("\";");
	}

	// ct, if defined first
	if (resource->content_type[0] != CONTENT_NONE)
	{
		// space for one type only (max 5 digits up to number of CONTENT_NONE)
		char my_ct_value[5];
		*response_length += oc_rep_add_line_to_buffer("ct=");

		// ct, if defined second
		if (resource->content_type[1] != CONTENT_NONE)
		{// 2 types, ct="60 40"

			// "
			*response_length += oc_rep_add_line_to_buffer("\"");

			// number
			(void) sprintf(my_ct_value, "%d", resource->content_type[0]);
			*response_length += oc_rep_add_line_to_buffer(my_ct_value);

			// space
			*response_length += oc_rep_add_line_to_buffer(" ");

			// number
			(void) sprintf(my_ct_value, "%d", resource->content_type[1]);
			*response_length += oc_rep_add_line_to_buffer(my_ct_value);

			// "
			*response_length += oc_rep_add_line_to_buffer("\"");

		}
		else
		{// 1 type, ct=60

			// number 
			(void) sprintf(my_ct_value, "%d", resource->content_type[0]);
			*response_length += oc_rep_add_line_to_buffer(my_ct_value);
		}
	}

	return true;
}

bool oc_check_resource_by_request(const oc_resource_t* resource, oc_request_t* request,
                        size_t* response_length, int* skipped,
                        const int first_entry, bool truncate)
{
	// note, matches also when 'rt' key is not part of request query parameter
  if (!oc_check_resource_by_rt(resource, request))
	{
		// key 'rt' is part of query, but value was not found,
		// leave since any chain of additional query parameters will never match (and-ed!)
	  return false;
	}

	// note, matches also when 'if' key is not part of request query parameter
  if (!oc_check_resource_by_if(resource, request))
	{
    // key 'if' is part of query, but value was not found,
    // leave since any chain of additional query parameters will never match (and-ed!)
	  return false;
	}

	if (!(resource->properties & OC_DISCOVERABLE))
	{
		return false;
	}

	if (*skipped < first_entry)
	{
		(*skipped)++;
		return false;
	}

	// 'urn:knx' truncation expected? | 'urn:knx' part is present in request query ?  | 'urn:knx' will be cut out?
	// y                              | don't care                                    | y  (request belongs to a KNX EP, cut out always)
	// n (used only on wk request)    | y                                             | y  (request was using urn:knx, cut out)
	// n (used only on wk request)    | n                                             | n  (request was NOT using urn:knx, put in)

	if (!truncate)
	{
		// only on a wk request it is false, 
	  truncate = oc_check_request_query_value_on_urn_knx(request);
		// result see above, last column
	}

	return oc_add_resource_to_response_payload(resource, request, response_length, truncate);
}

// filter for application resources
static bool oc_process_application_resources(oc_request_t* request,
                                             size_t* response_length, int* query_parameter_kvpair_matches, int* skipped,
                                             const int first_entry, const int last_entry)
{
	for (const oc_resource_t* resource = oc_ri_get_app_resources(); resource; resource = resource->next)
	{
		// skip non visible application resources
	  if (!(resource->properties & OC_DISCOVERABLE))
			continue;

		if (oc_check_resource_by_request(resource, request, response_length, skipped, first_entry, false))
		{
			(*query_parameter_kvpair_matches)++;
			if (first_entry + (*query_parameter_kvpair_matches) >= last_entry)
			{
				// first page entry + current amount of matches exceeds page size
				return true;
			}
		}
	}
	return false;
}

// filter for basic device resources
static bool oc_process_basic_resources(oc_request_t* request, const size_t device_index,
																			 size_t* response_length, int* query_parameter_kvpair_matches, int* skipped,
																			 const int first_entry, const int last_entry)
{
	for (int i = 0; i < OC_NUM_MANDATORY_CORE_RESOURCES_PER_WK; i++)
	{
		if (oc_check_resource_by_request(oc_core_get_resource_by_index(basic_resources[i], device_index), request, response_length, skipped, first_entry, false))
		{
			(*query_parameter_kvpair_matches)++;
			if (first_entry + (*query_parameter_kvpair_matches) >= last_entry)
			{
				// first page entry + current amount of matches exceeds page size
				return true;
			}
		}
	}
	return false;
}

static int frame_sn(const char* serial_number, const uint64_t iid, const uint16_t ia)
{

	int framed_bytes = oc_rep_add_line_to_buffer("<>;ep=\"knx://sn.");
	int response_length = framed_bytes;

	framed_bytes = oc_rep_add_line_to_buffer(serial_number);
	response_length += framed_bytes;

	framed_bytes = oc_rep_add_line_to_buffer(" knx://ia.");
	response_length += framed_bytes;

	char text_hex[20];
	oc_conv_uint64_to_hex_string(text_hex, iid);
	framed_bytes = oc_rep_add_line_to_buffer(text_hex);
	response_length += framed_bytes;

	(void) snprintf(text_hex, 19, ".%x", ia);
	framed_bytes = oc_rep_add_line_to_buffer(text_hex);
	response_length += framed_bytes;

	framed_bytes = oc_rep_add_line_to_buffer("\"");
	response_length += framed_bytes;

	return response_length;
}

void oc_wkcore_discovery_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) iface_mask;
	(void) data;

	char* key;             // one key pointer for a key=value 'pair' 
	size_t key_len;

	char* value;           // one value pointer for a key=value 'pair' 
	size_t value_len;

	char* rt_request = 0;   // 'rt'
	int rt_len = 0;

	char* ep_request = 0;   // 'ep' 
	int ep_len = 0;

	char* if_request = 0;   // 'if'
	int if_len = 0;

	char* d_request = 0;    // 'd'
	int d_len = 0;


	int query_parameter_key_value_pair_matches = 0; // how many query parameter key/value pair matches where found 
	size_t response_length = 0;
	int skipped = 0;
	int query_pn = PAGE_NUMBER;
	int query_ps = PAGE_SIZE;

	bool current_page_is_full = false;      // true if response page is full, no more resources can be added
	bool query_parameter_key_match = false; // true if at least one query parameter KEY was found

	if (!oc_accept_header_is_ok(request, APPLICATION_LINK_FORMAT))
	{
		return;
	}

	oc_init_query_iterator();
	while (oc_iterate_query(request, &key, &key_len, &value, &value_len) > 0)
	{ // each KEY (+ value) is stored one time per request (last wins)

		if (strncmp(key, "rt", key_len) == 0)
		{
			rt_request = value;
			rt_len = (int) value_len;
			query_parameter_key_match = true;
		}
		if (strncmp(key, "ep", key_len) == 0)
		{
			ep_request = value;
			ep_len = (int) value_len;
			query_parameter_key_match = true;
		}
		if (strncmp(key, "if", key_len) == 0)
		{
			if_request = value;
			if_len = (int) value_len;
			query_parameter_key_match = true;
		}
		if (strncmp(key, "d", key_len) == 0)
		{
			d_request = value;
			d_len = (int) value_len;
			query_parameter_key_match = true;
		}
	}

	// get from the request the addressed device as index
	const size_t device_index = request->resource->device;
	const oc_device_info_t* device = oc_core_get_device_info(device_index);

	// --- multicast w/wo query parameter OR unicast w/wo query parameter ---

	if (request->query_len == 0 && request->origin && request->origin->flags & MULTICAST)
	{
    // multicast /wo query parameter, frame sn + iid + ia
	  response_length = frame_sn(oc_string(device->serialnumber), device->iid, device->ia);
		oc_prepare_linkformat_response(request, OC_STATUS_OK, response_length);
		return;
	}

	// --- multicast w/ query parameter OR unicast w/wo query parameter ---

	// current resource amount
	int total = OC_NUM_MANDATORY_CORE_RESOURCES_PER_WK;

	// count all and add all 'visible' application resources in case of query parameters rt/if are present
	if (rt_len > 0 || if_len > 0)
	{
		for (const oc_resource_t* my_resource = oc_ri_get_app_resources(); my_resource; my_resource = my_resource->next)
		{
			// skip other devices and not "public" resources 
			if (my_resource->device != device_index || !(my_resource->properties & OC_DISCOVERABLE))
			{
				continue;
			}
			if (oc_string(my_resource->uri) != NULL)
			{ // local URI must be present for a resource 
				total++;
			}
		}
	}

	// add FBs
	total += oc_count_functional_blocks();

	// handle query parameters l=ps and/or l=total
	if (query_l_was_processed(request, PAGE_SIZE, total))
		return;

	// first entry number of a resource that will be placed on a page
	const int first_entry = evaluate_query_px(request, &query_pn, &query_ps);

	// check if requested page will carry at least one resource e.g
	// - total=4, pn 5, ps 20, first entry = 100 -> no data on page 5 (all on page 0)
	// - total=4, pn 1, ps 04, first entry = 004 -> no data on page 1 (all on page 0)no data on page 5 
	if (first_entry >= total || query_ps == 0)
	{
		oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
		return;
	}

	// entries don't fit in a single page -> more pages are needed to get the full list
	// - total=4, page number 1, page size 02, first entry = 002 -> no more data on next page 
	// - total=4, page number 1, page size 01, first entry = 001 -> more data on next page
	const bool more_request_needed = total > first_entry + query_ps ? true : false;

	// on any query parameter present but no query parameter KEY match 
	if (request->query_len > 0 && !query_parameter_key_match)
	{
		if (request->origin && request->origin->flags & MULTICAST)
		{
      // multicast: query parameter key NOT found = ignore request (response suppression)
      oc_ignore_request(request);
		}
		else
		{
      // unicast: query parameter key NOT found
      oc_prepare_no_format_response_no_payload(request, OC_STATUS_NOT_FOUND);
		}
		return;
	}

	// handle sector, if device belongs to a GA ?d=urn:knx:g.s.[ga] list the data points to which the GA applies to
	if (d_len > 12 && strncmp(d_request, "urn:knx:g.s.", 12) == 0)
	{
		const int group_address = atoi(&d_request[12]);
		PRINT("group address: %d", group_address);

		// if not in 'runtime' just return
		if (!oc_is_device_in_runtime(device_index))
		{
			// handle bad request, note below layer ignores this message if it is a multicast request
			PRINT("device not at 'runtime'");
			oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
			return;
		}

		if (strncmp(d_request, "urn:knx:g.s.*", 13) == 0)
		{
			// quote from EITT test 5.1.1.8: "Must fail since the response would likely be excessively large"
			oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
			return;
		}

		// create the response
		bool const at_least_one_added = oc_add_points_from_group_object_table_to_response(request, device_index, group_address, &response_length);

		if (at_least_one_added)
		{
			// unicast or multicast request w/ query parameter and hit
			oc_prepare_linkformat_response(request, OC_STATUS_OK, response_length);
		}
		else
		{
			if (request->origin && request->origin->flags & MULTICAST)
			{
        // on multicast request w/ query parameter and NO hit
        oc_ignore_request(request);
			}
			else
			{ 
        // on unicast request w/ query parameter and NO hit
        oc_prepare_no_format_response_no_payload(request, OC_STATUS_NOT_FOUND);
			}
		}
		return;
	}

	// handle programming mode
	if (if_len == 13 && strncmp(if_request, "urn:knx:if.pm", 13) == 0)
	{
		if (oc_knx_device_in_programming_mode(device_index))
		{ // PRG mode on
			/*
				 - add only '<>; ep="knx://sn.<serial-number> knx://ia.<ia>"' when the interface
					 is if.pm && device is in programming mode, return immediately (do not process any other query params)

				 - the ep=knx://sn.* and if=urn:knx:if.pm concatenation is ignored since HERE only
					 needs to respond when the device is in programming mode
			*/

			PRINT("oc_wkcore_discovery_handler PM HANDLING: PRG mode on");

			if (ep_request != 0 && ep_len > 9 && strncmp(ep_request, "knx://sn.", 9) == 0)
			{ // query parameter if=urn:knx:if.pm AND ep=knx://sn. AND some extra xx data present

				// get sn from request, fix position
				const char* ep_serialnumber = ep_request + 9;

				if (strncmp(oc_string(device->serialnumber), ep_serialnumber, strlen(oc_string(device->serialnumber))) != 0)
				{ // SN does NOT match, xx data can be anything

					PRINT("oc_wkcore_discovery_handler PM HANDLING: PRG mode on, SN no direct match");

					if (request->origin && (request->origin->flags & MULTICAST) == 0)
					{
						// on unicast request w/ query parameter and NO hit
						oc_prepare_no_format_response_no_payload(request, OC_STATUS_NOT_FOUND);
					}
					else
					{
						// on multicast request w/ query parameter and NO hit 
						oc_ignore_request(request);
					}
					return;
				}
				PRINT("oc_wkcore_discovery_handler PM HANDLING: PRG mode on, SN 1:1 match");
				// SN does match 1:1, leaves here and continues on 'handle serial number' (with double code)
			}
			else
			{ // query parameter if=urn:knx:if.pm AND some extra xx (nothing up to wildcard)  

				if (skipped < first_entry)
				{
					// ???
					skipped++;
				}
				else
				{
					// add sn to response for unicast/multicast (but don't send now)
					response_length = frame_sn(oc_string(device->serialnumber), device->iid, device->ia);
					query_parameter_key_value_pair_matches++;
				}

				PRINT("oc_wkcore_discovery_handler PM HANDLING: PRG mode on, SN MAY match");
				// SN may match, leaves here and continues on 'handle serial number' (with double code)
			}
		}
		else
		{ // PRG mode off

			PRINT("oc_wkcore_discovery_handler PM HANDLING: PRG mode off");

			if (request->origin && request->origin->flags & MULTICAST)
			{
        // multicast request w/ query parameter and NO PRG mode set
        oc_ignore_request(request);
			}
			else
			{
        // unicast request w/ query parameter and NO PRG mode set
        oc_prepare_no_format_response_no_payload(request, OC_STATUS_NOT_FOUND);
			}
			return;
		}
	}

	// handle individual address 
	if (ep_request != 0 && ep_len > 9 && strncmp(ep_request, "knx://ia.", 9) == 0)
	{
		/* request with IA = ...ia.IID.IA -> knx://ia.d773e094b6.1101
			 the IA is NOT always at a fixed pos; IID = 40 BIT = 5 byte = 10 char, leading zeros are omitted
		*/

		#define EP_STR_LEN_DOT_IA  (9)  // knx://ia.
		#define IID_STR_LEN_MAX    (10) // max IID length
		#define IA_STR_LEN_MAX     (4)  // max IA length

		// IID pos is fixed after first '.', IA pos follows after second '.' (distance = max iid + 1)
		char* ep_iid_start_pos = ep_request + EP_STR_LEN_DOT_IA;
		char* ep_ia_end_pos = ep_request + ep_len - 1;
		char* ep_ia_dot_pos = oc_strnchr(ep_iid_start_pos, '.', IID_STR_LEN_MAX + 1);
		char* ep_ia_start_pos = ep_ia_dot_pos + 1; // on error = NULL + 1 = 1

		// max len IA +\0
		char ia_str[IA_STR_LEN_MAX + 1] = "";

		if (ep_ia_dot_pos)
		{
			//copy actual IA size
			strncpy(ia_str, ep_ia_start_pos, ep_ia_end_pos - ep_ia_dot_pos);
		}

		// string is hex formatted, on conversion error = 0
		const uint16_t ia = strtoul(ia_str, NULL, 16);

		if (ia == device->ia)
		{
			// max len IID + \0
			char iid_str[IID_STR_LEN_MAX + 1] = "";
			strncpy(iid_str, ep_iid_start_pos, ep_ia_dot_pos - ep_iid_start_pos);
			// string is hex formatted, on conversion error = 0 (performance ...)
			const uint64_t iid = strtoull(iid_str, NULL, 16);

			if (iid == device->iid)
			{
				response_length = frame_sn(oc_string(device->serialnumber), device->iid, device->ia);
				oc_prepare_linkformat_response(request, OC_STATUS_OK, response_length);
				return;
			}
		}

		// on unicast/multicast request w/ query parameter and NO hit
		// TODO topic will be decided by iot group (#14 clarification list)
		oc_ignore_request(request);
		return;
	}

	// handle serial number
	if (ep_request != 0 && ep_len > 9 && strncmp(ep_request, "knx://sn.", 9) == 0)
	{

		#define EP_STR_LEN_DOT_SN  (9)  // knx://sn.
		#define SN_STR_LEN_MAX    (12)  // max SN length

		// SN pos is fixed after first '.', '*' pos may follow somewhere after this (distance = max sn size)
		char* ep_serialnumber_start_pos = ep_request + EP_STR_LEN_DOT_SN;
		char* ep_star_pos = oc_strnchr(ep_serialnumber_start_pos, '*', SN_STR_LEN_MAX);

		// max len SN + \0
		char sn_substr[SN_STR_LEN_MAX + 1] = "";

		if (ep_star_pos)
		{
			// copy part SN size 
			strncpy(sn_substr, ep_serialnumber_start_pos, ep_star_pos - ep_serialnumber_start_pos);
		}

		// - sn.*        fits always
		// - sn.00fa...  fits to the sn entirely (useful on mc)
		// - sn.00fa*    fits to the sn part (clause 2.6.1.3.4)
		if (strncmp(ep_serialnumber_start_pos, "*", 1) == 0 ||
				strncmp(oc_string(device->serialnumber), ep_serialnumber_start_pos, strlen(oc_string(device->serialnumber))) == 0 ||
				strstr(oc_string(device->serialnumber), sn_substr) != NULL)
		{
			response_length = frame_sn(oc_string(device->serialnumber), device->iid, device->ia);
			oc_prepare_linkformat_response(request, OC_STATUS_OK, response_length);
		}
		else
		{
			// on unicast/multicast request w/ query parameter and NO hit
			// TODO topic will be decided by iot group (#14 clarification list)
			oc_ignore_request(request);
		}
		return;
	}

	// handle rt/if query parameters 
	if (rt_len > 0 || if_len > 0)
	{
		PRINT("oc_wkcore_discovery_handler rt='%.*s'", rt_len, rt_request); // first (len) value defines precision 
		PRINT("oc_wkcore_discovery_handler if='%.*s'", if_len, if_request); // first (len) value defines precision 

		// process application resources (and add on a 'hit' to response) 
		current_page_is_full = oc_process_application_resources(request, &response_length, &query_parameter_key_value_pair_matches, &skipped, first_entry, first_entry + query_ps);
	}

	if (!current_page_is_full)
	{ // page not full, things can still be added

		// add basic device resources
		current_page_is_full = oc_process_basic_resources(request, device_index, &response_length, &query_parameter_key_value_pair_matches, &skipped, first_entry, first_entry + query_ps);

		PRINT("oc_wkcore_discovery_handler add common resources on a unicast request ...");
	}

	if (!current_page_is_full && request->origin && (request->origin->flags & MULTICAST) == 0)
	{ // unicast: page not full, things can still be added

		// add FBs in case of .../core discovery request contains matching query parameters,
		// such as query parameter rt=fb 
		if (oc_check_if_functional_blocks_need_to_add(request))
		{
			oc_was_adding_function_blocks_to_response(request, false, &response_length, &query_parameter_key_value_pair_matches, &skipped, first_entry, first_entry + query_ps);

			PRINT("oc_wkcore_discovery_handler added present FB resources on a unicast request ...");
		}
	}

	if (query_parameter_key_value_pair_matches > 0 && response_length > 0)
	{
		// unicast or multicast request
		// matches/response_length >0/>0
		// m>0;l>0 : -- > at least one query parameter KV pair match was found for this device

		// add only a page hint if at least one response entry is in
		if (more_request_needed)
		{
			// no page # was in the request (query_p =0) = next page 1 else #+1
			response_length += add_next_page_indicator(oc_string(request->resource->uri), ++query_pn);
		}

		PRINT("oc_wkcore_discovery_handler send matching response with length = %d", (int) response_length);
		oc_prepare_linkformat_response(request, OC_STATUS_OK, response_length);
	}
	else
	{
		// matches/response_length ?/?
		// m>0;l=0 : -- > at least one query parameter KEY/VALUE pair found but no hit for this device
		// m=0;l>0 : -- > n/a (no query parameter KEY/VALUE pair but a hit ....)
		// m=0;l=0 : -- > NO query parameter KEY/VALUE pair was found AND (hence this) no hit for this device

		if (request->origin && request->origin->flags & MULTICAST)
		{ // multicast request
      PRINT("oc_wkcore_discovery_handler multicast request, no match -> ignore it");
      oc_ignore_request(request);
		}
		else
		{ // unicast request
      PRINT("oc_wkcore_discovery_handler unicast request, no match -> send unicast response with length = 0");
      oc_prepare_no_format_response_no_payload(request, OC_STATUS_NOT_FOUND);
		}
	}
}


/**
* Creates a const CORE resource that is linked to a next resource.
* All resources together defines a linked (resource) list, the last 
* resource of the list uses a link that points to NULL (... this one here)
*
* - resource fields, see 'oc_resource_t'
* - resources are statically defined directly as part of the c-code, 
*   expanded from macros => different macro definitions for the different 
    (cross) - compilers are difficult when extending a macro   
* - compiler pragmas remain as macros
* 
*/

oc_resource_dummy_t core_resource_well_known_core_final = { NULL, -1 };					 		// next resource (here NULL)
PRAGMA_IN																																										 		// compiler specific
oc_resource_data_t core_resource_well_known_core_data;																			 		// user runtime data
const oc_resource_t core_resource_well_known_core =																					 		// the actual resource definition 
{
	(oc_resource_t*) &core_resource_well_known_core_final,																		 		// ptr to next resource
	0,																																												 		// device instance
	{ NULL, 0, NULL },																						 								// resource name
	{ NULL, sizeof("/.well-known/core"), "/.well-known/core" },							 		// Endpoint URI
	{ NULL, (size_t)1 * 32, (char[1][32]){	"well-known-type"} },					        // types (0...n), if 0 => 3 x NULL
	{ NULL, 0, NULL },																						 								// DPT
	{ APPLICATION_LINK_FORMAT, CONTENT_NONE },																							 		// content formats (max 2)
	OC_DISCOVERABLE,																																					 		// resource properties
	{ oc_wkcore_discovery_handler, NULL, OC_ACL_NONE, OC_IF_NONE },		// get callback
	{ NULL, NULL, OC_ACL_NONE, OC_IF_NONE },											 		// put callback
	{ NULL, NULL, OC_ACL_NONE, OC_IF_NONE },											 		// post callback
	{ NULL, NULL, OC_ACL_NONE, OC_IF_NONE },											 		// delete callback
	{ NULL, NULL },																										 		// property get callback
	{ NULL, NULL },																										 		// property set callback 
	0,																																												 		// observe period
	0,																																												 		// FB instance
	true,																																											 		// is static precompiled resource
	&core_resource_well_known_core_data																												 		// ptr to user runtime data					
};
PRAGMA_OUT																																									 		// compiler specific

void oc_create_discovery_resource(const int resource_idx, const size_t device_index)
{
	OC_DBG("create /.well-known/core resources");

	if (device_index == 0)
	{
		OC_DBG("device 0: KNX device resources created statically");
		return;
	}

	oc_core_populate_resource(resource_idx, device_index, "/.well-known/core",
														APPLICATION_LINK_FORMAT, CONTENT_NONE,
														OC_DISCOVERABLE, oc_wkcore_discovery_handler, 0,
														0, 0, 1, "well-known-type");
}

oc_discovery_flags_t
oc_ri_process_discovery_payload(const uint8_t* payload, const int len,
																const oc_client_handler_t client_handler,
																oc_endpoint_t* endpoint,
																oc_content_format_t content, void* user_data)
{
	const oc_discovery_all_handler_t all_handler = client_handler.discovery_all;
	const oc_discovery_flags_t ret = OC_CONTINUE_DISCOVERY;

	if (content == APPLICATION_LINK_FORMAT)
	{

		PRINT("oc_ri_process_discovery_payload: calling handler all");
		if (all_handler)
		{
			all_handler((const char*) payload, len, endpoint, user_data);
		}
	}

	return ret;
}
