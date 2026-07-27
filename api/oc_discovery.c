/*
 * Copyright (c) 2016 Intel Corporation
 * Copyright (c) 2021-2023 Cascoda Ltd
 * Copyright (c) 2026 KNX Association
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include "oc_client_state.h"
#include "oc_api.h"
#include "oc_discovery.h"
#include "oc_knx_fb.h"
#include "oc_knx_fp.h"
#include "oc_core_res.h"
#include "oc_endpoint.h"
#include "oc_knx_helpers.h"
#include "oc_knx_dev.h"

/*
 * - below resources must be in the uc/mc response for well-known/core,
 *   other resources may be added in real products, certification tests demands only the mandatory
 * - use int type to satisfy directly function call by using the array
 */
const int core_resources_for_well_known[] = 
{
  OC_DEV,					// mandatory
  OC_KNX_K,				// mandatory
  OC_KNX_SWU,			// mandatory
  OC_KNX_AUTH,		// mandatory
  OC_APP					// optional
};

// number of core resources to be considered for well-known requests  
#define RESOURCES_FOR_WELL_KNOWN (5)

#define EP_STR_LEN_DOT_SN (9)

static bool oc_discovery_sn_matches(char* ep_request, int ep_len, const char* serialnumber)
{
	/*
	 * SN pos is fixed after first '.', '*' pos may follow somewhere after this (distance = max sn)
	 * - max sn len is -> ep_len >= 9 - EP_STR_LEN_DOT_SN(9) = 0..12, but may be > 12 (attack/error)
	 * - so limit to max 12
	 * - 0 will end up in NOT FOUND on full SN no size match
	 *
	 * (a) sn.*                         fits always (to get all KNX devices in an IP network, note other system uses also well-known EP)
	 * (b) sn.00fa12345678              fits to the SN entirely (useful on mc)
	 * (c) sn.0* ... sn.00fa1234567*   fits to the SN partly (clause 2.6.1.3.4)
	 */
	char* ep_serialnumber_start_pos = ep_request + EP_STR_LEN_DOT_SN;
	const int ep_sn_len = ep_len - EP_STR_LEN_DOT_SN;
	const int wildcard_scan_len = ep_sn_len < SN_STR_LEN_MAX ? ep_sn_len : SN_STR_LEN_MAX;
	const char* ep_wildcard_pos = oc_strnchr(ep_serialnumber_start_pos, '*', wildcard_scan_len);

	if (ep_wildcard_pos == ep_serialnumber_start_pos)
	{
		// a ('*' is next char after '.')
		return true;
	}

	if (ep_wildcard_pos)
	{
		// partial compare somewhere in SN string, SN can be of 1..11 chars
		const size_t ep_partial_sn_len = ep_wildcard_pos - ep_serialnumber_start_pos;
		// compare SN prefix size 1..11
		return strncmp(serialnumber, ep_serialnumber_start_pos, ep_partial_sn_len) == 0;
	}

	// full compare (device has a string, ep_request has a stream without /0, inbound SN != 12 is never a match)
	return ep_sn_len == SN_STR_LEN_MAX
				 && strncmp(serialnumber, ep_serialnumber_start_pos, SN_STR_LEN_MAX) == 0;
}

// multicast ignore, unicast respond with 4.04 Not Found (Len 0)
static void oc_discovery_prepare_no_match_response(oc_request_t* request)
{
  if (request->origin && request->origin->flags & MULTICAST)
	{
    OC_DBG("discovery multicast request, no match -> ignore it");
    oc_ignore_request(request);
	}
	else
	{
    OC_DBG("discovery unicast request, no match -> send unicast response with length = 0");
	  oc_prepare_no_format_response_no_payload(request, OC_STATUS_NOT_FOUND);
	}
}

bool oc_add_resource_to_response_payload(const oc_resource_t* resource, size_t* response_length, bool truncate)
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
		// ct, if defined second
		if (resource->content_type[1] != CONTENT_NONE)
		{// 2 types, ct="60 40"

			// space for two types (max 5 digits up to number of CONTENT_NONE = 99999)
      #define MAX_CT_LEN_DOUBLE (3 + 1 + 5 + 1 + 5 + 1)

      char double_my_ct_value[MAX_CT_LEN_DOUBLE];


			// ct= + " + number + ' ' + number + "
      (void)snprintf(double_my_ct_value, MAX_CT_LEN_DOUBLE, "ct=\"%d %d\"", 
										 resource->content_type[0],
                     resource->content_type[1]);
      *response_length += oc_rep_add_line_to_buffer(double_my_ct_value);

		}
		else
		{// 1 type, ct=60

			// space for one type (max 5 digits up to number of CONTENT_NONE = 99999)
		  #define MAX_CT_LEN_SINGLE (3 + 5)

      char single_my_ct_value[MAX_CT_LEN_SINGLE];

		  // ct= + number 
			(void)snprintf(single_my_ct_value, MAX_CT_LEN_SINGLE, "ct=%d", 
										 resource->content_type[0]);
      *response_length += oc_rep_add_line_to_buffer(single_my_ct_value);
		}
	}

	return true;
}

bool oc_check_request_from_index(int index, oc_request_t* request, size_t* response_length, int* skipped, int first_entry, bool truncate)
{
  const oc_resource_t* indexed_resource = oc_core_get_core_resource_by_index(index);
  return oc_check_request_from_resource(indexed_resource, request,response_length, skipped, first_entry, truncate);
}

bool oc_check_request_from_resource(const oc_resource_t* resource, oc_request_t* request,	size_t* response_length, int* skipped, int first_entry, bool truncate)
{
  if (!resource)
  {
    return false;
  }

  // fast check first 
	if (!(resource->properties & OC_DISCOVERABLE))
  {
    return false;
  }

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

	if (*skipped < first_entry)
	{
		/*
		 * - ignore resources that do not fit to the requested page,
		 *   such as for page 5 (ps=20) 80 items will be skipped
		 *   (applicable only when calling  
    */
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

	return oc_add_resource_to_response_payload(resource, response_length, truncate);
}

// filter for application resources (not all of them may be able to discover)
static bool oc_process_application_resources(oc_request_t* request,
                                             size_t* response_length, int* query_parameter_kvpair_matches, 
																						 int* skipped,
                                             const int first_entry, 
																						 const int last_entry)
{
	for (const oc_resource_t* resource = oc_ri_get_app_resources(); resource; resource = resource->next)
	{
	    if (oc_check_request_from_resource(resource, request, response_length, skipped, first_entry, false))
      {
        (*query_parameter_kvpair_matches)++;
        if (first_entry + *query_parameter_kvpair_matches >= last_entry)
        {
          // first page entry + current amount of matches exceeds page size
          return true;
        }
      }
	}
	return false;
}

// filter for core resources (all of them are able to discover)
static bool oc_process_core_resources(oc_request_t* request,
                                      size_t* response_length, int* query_parameter_kvpair_matches, 
																			int* skipped,
																			int first_entry, 
																			int last_entry)
{

  for (int i = 0; i < RESOURCES_FOR_WELL_KNOWN; i++)
	{
    const int index = core_resources_for_well_known[i];

	  if (oc_check_request_from_index(index, request, response_length, skipped, first_entry, false))
		{
			(*query_parameter_kvpair_matches)++;
			if (first_entry + *query_parameter_kvpair_matches >= last_entry)
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

	// add a space to not concatenate sn  with ia 
	framed_bytes = oc_rep_add_line_to_buffer(" knx://ia.");
	response_length += framed_bytes;

	// max 16 nibble hex iid chars, max 4 nibble hex ia chars
	char text_hex[20];
	oc_conv_uint64_to_hex_string(text_hex, iid);
	framed_bytes = oc_rep_add_line_to_buffer(text_hex);
	response_length += framed_bytes;

	// reuse buffer, '.' + max 5 digits for the decimal coded 16-bit ia
	(void) snprintf(text_hex, 1 + 5, ".%x", ia);
	framed_bytes = oc_rep_add_line_to_buffer(text_hex);
	response_length += framed_bytes;

	framed_bytes = oc_rep_add_line_to_buffer("\"");
	response_length += framed_bytes;

	return response_length;
}

void oc_well_known_core_discovery_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
	(void) iface_mask;
	(void) data;

	char* key;								// one key pointer for a key=value 'pair' 
	size_t key_len;

	char* value;							// one value pointer for a key=value 'pair' 
	size_t value_len;

	char* rt_request = NULL;	// 'rt'
	int rt_len = 0;

	char* ep_request = NULL;	// 'ep' 
	int ep_len = 0;

	char* if_request = NULL;	// 'if'
	int if_len = 0;

	char* d_request = NULL;		// 'd'
	int d_len = 0;

	#define EP_STR_LEN_DOT_SN (9) // knx://sn.
  
  int query_parameter_key_value_pair_matches = 0; // how many query parameter key/value pair matches where found AND added to the response
	size_t response_length = 0;
  int skipped = 0;	// ignore resources that do not fit to the requested page
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

	// get device 
	const oc_device_info_t* const device = oc_core_get_device_info();

  /*
   * multicast
   * 
   * without ANY query parameter
   * - (m0) empty link with sn + ia such as <>;ep="knx://sn.00fa10020800 knx://ia.0.ffff"
   *
   * unicast
   *
   * without rt/if query parameter
   * - (a0) core resources (/dev, /k, ...) -> mandatory/ optional defined ones  
   * - (b0) application functional blocks (FB) -> at least one resource type must be defined (such 'fb.0')
   *
   * with rt/if query parameter
   * - (a1) core resources (/dev, /k, ...) -> for those the rt/if filter fits 
   * - (b0) see above
   * - (c1) all 'visible' application resources -> at least one resource path must be defined (such '/p/1')
   */

	// --- multicast w/wo query parameter OR unicast w/wo query parameter ---

	// (m0)
	if (request->query_len == 0 && request->origin && request->origin->flags & MULTICAST)
	{
		response_length = frame_sn(device->serialnumber, device->iid, device->ia);
		oc_prepare_linkformat_response(request, OC_STATUS_OK, response_length);
		return;
	}

	// --- multicast w/ query parameter OR unicast w/wo query parameter ---

	// (a0) (a1)  
  int total = RESOURCES_FOR_WELL_KNOWN;
  // (b0)
  total += oc_count_functional_blocks_from_application();

  // (c1) 
	if (rt_len > 0 || if_len > 0)
	{
		for (const oc_resource_t* my_resource = oc_ri_get_app_resources(); my_resource; my_resource = my_resource->next)
		{
			// skip non "public" resources 
			if (my_resource->properties & OC_DISCOVERABLE && oc_string(my_resource->uri))
			{
        // able to discover + resource path must be defined with a NON-NULL resource path 
			  total++;
			}
		}
	}

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

	// on any query parameter present but no query parameter KEY match 
	if (request->query_len > 0 && !query_parameter_key_match)
	{
		oc_discovery_prepare_no_match_response(request);
		return;
	}

	#define LEN_DOT_SECTOR (12) // urn:knx:g.s.
  #define GA_LEN_MAX (5)     // max GA length in decimal coded digits (16 bit max, 65535 = 5 digits)

	// handle sector, if device belongs to a GA ?d=urn:knx:g.s.[ga] list the data points to which the GA applies to
  if (d_len >= LEN_DOT_SECTOR && strncmp(d_request, "urn:knx:g.s.", LEN_DOT_SECTOR) == 0)
	{
		const int ga_len = d_len - LEN_DOT_SECTOR;

		if (ga_len == 0 || ga_len > GA_LEN_MAX)
    {
      // no GA value present or GA value too long
		  oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
      return;
    }

		for (int i = 0; i < ga_len; i++)
		{
			if (d_request[LEN_DOT_SECTOR + i] < '0' || d_request[LEN_DOT_SECTOR + i] > '9')
			{
        // not a decimal coded GA value
			  oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
				return;
			}
		}

		// GA must be decimal coded, 1..5 digits long, and within the 16-bit range
	  errno = 0;
    const uint32_t group_address = strtol(&d_request[LEN_DOT_SECTOR], NULL, 10);
    OC_DBG("group address: %04x", group_address);

		// if not in 'runtime' or a conversion error just return
    if (!oc_is_device_in_runtime() || errno || group_address > 0xFFFF)
		{
			// handle bad request, note below layer ignores this message if it is a multicast request
			OC_ERR("device not at 'runtime' or ga conversion error");
			oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
			return;
		}

		// create the response
		bool const at_least_one_added = oc_add_points_from_group_object_table_to_response(request, group_address, &response_length);

		if (at_least_one_added)
		{
			// unicast or multicast request w/ query parameter and hit
			oc_prepare_linkformat_response(request, OC_STATUS_OK, response_length);
		}
		else
		{
			oc_discovery_prepare_no_match_response(request);
		}
		return;
	}

	// handle programming mode
	if (if_len == 13 && strncmp(if_request, "urn:knx:if.pm", 13) == 0)
	{
		if (oc_knx_device_in_programming_mode())
		{ 
			/*
				 device PRG mode on + if=urn:knx:if.pm 

         (a) sn.*												fits always (to get all KNX devices in an IP network, note other system uses also well-known EP)
         (b) sn.00fa12345678						fits to the sn entirely (useful on mc)
         (c) sn.0* ... sn.00fa1234567*  fits to the sn partly (clause 2.6.1.3.4)

         (a/b/c) 
         - add '<>; ep="knx://sn.<serial-number> knx://ia.<ia>"' 
         - return immediately (do not process any other (possible) query params after if.pm and sn.xyz EVEN if they would invalidate the query)
			
			*/

			if (ep_request && ep_len >= EP_STR_LEN_DOT_SN && strncmp(ep_request, "knx://sn.", EP_STR_LEN_DOT_SN) == 0)
      { // (a/b/c)
				if (oc_discovery_sn_matches(ep_request, ep_len, device->serialnumber))
        {
          OC_INF("oc_well_known_core_discovery_handler PM HANDLING: PRG mode on, SN match");
				  response_length = frame_sn(device->serialnumber, device->iid, device->ia);
          oc_prepare_linkformat_response(request, OC_STATUS_OK, response_length);
        }
        else
				{ 
					OC_INF("oc_well_known_core_discovery_handler PM HANDLING: PRG mode on, SN no match");
					oc_discovery_prepare_no_match_response(request);
				}
        return;
			}
			
			// query parameter is if=urn:knx:if.pm AND something extra, but no 'knx://sn.'  
			if (skipped < first_entry)
			{ // do not add, is lower than needed for this page
				skipped++;
			}
			else
			{
				// add sn to response for unicast/multicast (but don't send now)
				response_length = frame_sn(device->serialnumber, device->iid, device->ia);
				query_parameter_key_value_pair_matches++;
			}

			OC_INF("oc_well_known_core_discovery_handler PM HANDLING: PRG mode on, but no ep=knx://sn.xyz");
			// leaves here and continues on check other query key/values 
		}
		else
    { // device PRG mode off - return immediately

			OC_INF("oc_well_known_core_discovery_handler PM HANDLING: PRG mode off");
			oc_discovery_prepare_no_match_response(request);
			return;
		}
	}

	#define LEN_DOT_IA (9)       // knx://ia.
  #define IID_STR_LEN_MAX (10) // max IID length in hex coded ASCII if no leading zeros are omitted (5 octets = 40 bit)
  #define IA_STR_LEN_MAX (4)   // max IA length in hex coded ASCII (2 octets = 16 bit)
	
  // handle individual address 
	if (ep_request && ep_len >= LEN_DOT_IA && strncmp(ep_request, "knx://ia.", LEN_DOT_IA) == 0)
	{
		/* 
		   same principle as on outbound discovery for unicast s-mode messages
		   
		   request with knx://ia.IID.IA -> knx://ia.d773e094b6.1101 (no leading IID zeros in this example)
			 the ia is NOT always at a fixed pos; IID = 40 BIT = 5 byte = 10 char, leading zeros are omitted
		*/

		// IID pos is fixed after first '.', IA pos follows after second '.' (max size to be searched for second '.' = max iid + 1)
    char* ep_iid_start_pos = ep_request + LEN_DOT_IA;
    const int ep_ia_len = ep_len - LEN_DOT_IA;
    const int dot_scan_len = ep_ia_len < IID_STR_LEN_MAX + 1 ? ep_ia_len : IID_STR_LEN_MAX + 1;
    const char* ep_ia_dot_pos = oc_strnchr(ep_iid_start_pos, '.', dot_scan_len);

		if (ep_ia_dot_pos)
    { // convert only if a '.' was found 

			// 1..10 
		  const size_t ep_iid_len = ep_ia_dot_pos - ep_iid_start_pos;

			if (ep_iid_len == 0 || ep_iid_len > IID_STR_LEN_MAX)
			{
				oc_discovery_prepare_no_match_response(request);
				return;
			}

			// IA can be of 1..4 chars; 0 or > 4 is invalid
			const char* ep_ia_start_pos = ep_ia_dot_pos + 1;

			// 1..4
			const size_t ep_ia_part_len = ep_request + ep_len - ep_ia_start_pos;

			if (ep_ia_part_len == 0 || ep_ia_part_len > IA_STR_LEN_MAX)
			{
				oc_discovery_prepare_no_match_response(request);
				return;
			}

			// empty max len IA + string termination '\0'
      char ia_str[IA_STR_LEN_MAX + 1] = "";

			// copy valid IA size 1..4
			strncpy(ia_str, ep_ia_start_pos, ep_ia_part_len);

			errno = 0; 
      // string is hex formatted
      const uint16_t ia = (uint16_t)strtoul(ia_str, NULL, 16);

			// test IA first since many devices will have the same IID 
			// converted ia = 0 is accepted, but usually the device will not have 0 assigned
      if (errno == 0 && ia == device->ia)
      {
        // empty max len IID + string termination '\0'
        char iid_str[IID_STR_LEN_MAX + 1] = "";

				// copy valid IID size 1..10
				strncpy(iid_str, ep_iid_start_pos, ep_iid_len);

				errno = 0; 
        // string is hex formatted, on conversion error = 0 device will not have IID = 0 -> ignores request
        const uint64_t iid = strtoull(iid_str, NULL, 16);

				// converted iid = 0 is accepted, but usually the device will not have 0 assigned
        if (errno == 0 && iid == device->iid)
        {
          response_length = frame_sn(device->serialnumber, device->iid, device->ia);
          oc_prepare_linkformat_response(request, OC_STATUS_OK, response_length);
          return;
        }
      }
    }

		oc_discovery_prepare_no_match_response(request);
    return;
	}

	// handle serial number
	if (ep_request && ep_len >= EP_STR_LEN_DOT_SN && strncmp(ep_request, "knx://sn.", EP_STR_LEN_DOT_SN) == 0)
	{
		if (oc_discovery_sn_matches(ep_request, ep_len, device->serialnumber))
		{
      OC_INF("oc_well_known_core_discovery_handler SN HANDLING: SN match");
		  response_length = frame_sn(device->serialnumber, device->iid, device->ia);
			oc_prepare_linkformat_response(request, OC_STATUS_OK, response_length);
		}
		else
		{
      OC_INF("oc_well_known_core_discovery_handler SN HANDLING: SN no match");
			oc_discovery_prepare_no_match_response(request);
		}
		return;
	}

	/*
	 * - process FIRST core resources and SECOND application resources
	 * - add on a 'hit' the resource to the response
	 */

	OC_INF("oc_well_known_core_discovery_handler add core resources ...");

  // core
  current_page_is_full = oc_process_core_resources(request, &response_length, &query_parameter_key_value_pair_matches,
                                                    &skipped, first_entry, first_entry + query_ps);

  // add application resources only if query parameters are present
	if (rt_len > 0 || if_len > 0)
  {
    OC_DBG("oc_well_known_core_discovery_handler rt='%.*s' if='%.*s' ", rt_len, rt_request, if_len, if_request); // first (len) value defines precision for %.

    // application
    if (!current_page_is_full)
    {
      OC_INF("oc_well_known_core_discovery_handler add application resources ...");

      current_page_is_full = oc_process_application_resources(request, &response_length, &query_parameter_key_value_pair_matches, &skipped, first_entry, first_entry + query_ps);
    }
  }

	if (!current_page_is_full && request->origin && !(request->origin->flags & MULTICAST))
	{ // unicast: page not full, things can still be added

		// add FBs in case of well-known discovery request contains matching query parameters:
		// - not present at all
		// - rt=*, rt=*fb* -> urn:knx:fb.321
		// - if=*, if=*ll* -> urn:knx:id.ll
		if (oc_check_if_functional_blocks_need_to_add(request))
		{
			OC_INF("oc_well_known_core_discovery_handler add functional block resources ...");

			// note that this function is reusing the number of counted FBs
		  oc_add_functional_blocks_from_application_to_response(request, false, &response_length, &query_parameter_key_value_pair_matches, &skipped, first_entry, first_entry + query_ps);
		}
	}

	// decide the next-page hint from the entries actually added to this page, after
	// all steps (core + application + functional blocks) have run. Each step stops
	// once query_ps entries are placed (query_ps > 0 is guaranteed above), so a full
	// page is the signal that matching resources may be left over. current_page_is_full
	// cannot be used here: the functional-block step does not feed its result back into
	// it, so an unfiltered request that overflows on FBs would drop the hint.
	const bool more_request_needed = query_parameter_key_value_pair_matches >= query_ps;

	if (query_parameter_key_value_pair_matches > 0 && response_length > 0)
	{
		// unicast or multicast request
		// matches/response_length >0/>0
		// m>0;l>0 : -- > at least one query parameter KV pair match was found for this device AND added to the response

		// add a page hint only when the page filled up, hence matching resources
		// are left over for the next page
		if (more_request_needed)
		{
			// no page # was in the request (query_p =0) = next page 1 else #+1
			response_length += add_next_page_indicator(oc_string(request->resource->uri), ++query_pn);
		}

		OC_DBG("oc_well_known_core_discovery_handler send matching response with length = %d", (int) response_length);
		oc_prepare_linkformat_response(request, OC_STATUS_OK, response_length);
	}
	else
	{
		// matches/response_length ?/?
		// m>0;l=0 : -- > at least one query parameter KEY/VALUE pair found but no hit for this device (nothing added to response)
		// m=0;l>0 : -- > n/a (no query parameter KEY/VALUE pair but a hit ....)
		// m=0;l=0 : -- > NO query parameter KEY/VALUE pair was found AND (hence this) no hit for this device

		oc_discovery_prepare_no_match_response(request);
	}
}


/**
* Creates a const CORE resource that is linked to a next resource.
* All resources together defines a linked (resource) list, the last 
* resource of the list uses a link that points to NULL (... this one here)
*
* - resources are statically defined directly as part of the c-code, 
*   expanded from macros => different macro definitions for the different 
    (cross) - compilers are difficult when extending a macro

* - compiler pragmas remain as macros
*
*	- resource fields
*	  for common infos, see 'oc_resource_t' below
*
*		- a resource type (FB/DPA) is defined as in KNX IoT specification (if defined),
*		  a datapoint type (DPT) type is defined as in KNX IoT specification (if defined)
*		  - 1...n for core + application resources  -> see also well-known handler
*			- 0 -> NULL,0,NULL
*			
*/


static oc_resource_data_t core_resource_well_known_core_data;
const oc_resource_t core_resource_well_known_core =																					 		// the actual resource definition
{
	NULL,																		 																		                  // ptr to next resource -> well-known is the last resource
	{ NULL, sizeof("/.well-known/core"), "/.well-known/core" },							 		// Endpoint URI
	{ NULL, 0, NULL },																														// resource types, see comment above
	{ NULL, 0, NULL },																						 								// datapoint type, see comment above - if none => 3 x NULL
	{ APPLICATION_LINK_FORMAT, CONTENT_NONE },																							 		// content formats (max 2)
	OC_DISCOVERABLE,																																					 		// resource properties
	{ oc_well_known_core_discovery_handler, NULL, OC_ACL_NONE, OC_IF_NONE },		// get callback
	{ NULL, NULL, OC_ACL_NONE, OC_IF_NONE },											 		// put callback, if not defined use if.none, to return 4.05 instead of 4.01
	{ NULL, NULL, OC_ACL_NONE, OC_IF_NONE },											 		// post callback, if not defined use if.none, to return 4.05 instead of 4.01
	{ NULL, NULL, OC_ACL_NONE, OC_IF_NONE },											 		// delete callback, if not defined use if.none, to return 4.05 instead of 4.01
	{{NULL}, NULL},																										 	// property get callback
	{{NULL}, NULL},																										 	// property set callback
	0,																																												 		// observe period
	0,																																												 		// FB instance
	true,																																											 		// is static precompiled resource
	&core_resource_well_known_core_data																												 		// ptr to user runtime data
};

