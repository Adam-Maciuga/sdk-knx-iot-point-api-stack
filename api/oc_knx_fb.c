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
#include "api/oc_knx_fb.h"
#include "api/oc_knx_fp.h"
#include "oc_api.h"
#include "oc_knx_helpers.h"

#include <stdio.h>
#include "oc_core_res.h"
#include "oc_discovery.h"

// -----------------------------------------------------------------------------
// TODO this can be optimized.
#define ARRAY_SIZE 50 // up to 50 data points in a functional block
int g_int_array[2][ARRAY_SIZE];
int g_array_size = 0;
static int g_nr_functional_blocks = 0;

int get_fb_number_from_dp(const char* dpt)
{
  // dpa.352.51 or urn:knx:dpa.352.51
  // returns 352 or -1 ('strtol' error returns FB# 0, better than an 'atoi' crash)

  // first '.'
  const char* dot = strchr(dpt, '.');

  // number after '.'
  return dot ? strtol(dot + 1, NULL, 10) : -1;
}

bool is_in_g_array(int value, int instance)
{
  for (int i = 0; i < g_array_size; i++)
  {
    if (value == g_int_array[0][i] && instance == g_int_array[1][i])
    {
      return true;
    }
  }
  return false;
}

void store_in_array(int value, int instance)
{
  if (value == -1)
  {
    return;
  }
  g_int_array[0][g_array_size] = value; // functional block number
  g_int_array[1][g_array_size] = instance; // instance/ occurence of the functional block
  g_array_size++;
}

// count number of datapoint(s) in all application resources with fb number/ fb instance
static int oc_core_count_dp_in_fb(int fb_instance, int fb_number)
{
  int number_of_datapoints_in_fb = 0;

  for (const oc_resource_t* resource = oc_ri_get_app_resources(); resource; resource = resource->next)
  {
    if (resource->properties & OC_DISCOVERABLE)
    {
      for (int i = 0; i < (int)oc_string_array_get_allocated_size(resource->types); i++)
      {
        /*
             framing by functional block numbers & instances, each
             FB resource MUST have 1...n 'dpa' type(s) assigned with a FULL URN
        */

        const char* single_type = oc_string_array_get_item(resource->types, i);
        if (strncmp(single_type, "urn:knx:dpa", 11) == 0)
        {
          // here a -1 is compared to a -1,  but resource instance is never -1
          if (get_fb_number_from_dp(single_type) == fb_number && resource->fb_instance == fb_instance)
          {
            number_of_datapoints_in_fb++;
          }
        }
      }
    }
  }
  return number_of_datapoints_in_fb;
}

static void oc_core_fb_x_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  int query_parameter_kvpair_matches = 0; // query parameter key/value pair matches found
  size_t response_length = 0;
  int query_pn = PAGE_NUMBER;
  int query_ps = PAGE_SIZE;

  PRINT("oc_core_fb_x_get_handler - start");

  if (!oc_accept_header_is_ok(request, APPLICATION_LINK_FORMAT))
  {
    return;
  }

  // fb number such as 417
  const int fb_number = oc_uri_get_wildcard_value_as_int(
    oc_string(request->resource->uri),
    oc_string_len(request->resource->uri),
    request->uri_path,
    request->uri_path_len);

  // fb instance number such as 1 (assumed a fb is defined with 417_1, see code comments)
  const int fb_instance = oc_uri_get_wildcard_value_as_int_after_underscore(
    oc_string(request->resource->uri), 
    oc_string_len(request->resource->uri),
    request->uri_path,
    request->uri_path_len);


  OC_DBG("request url : %.*s", (int)request->uri_path_len, request->uri_path);
  OC_DBG("resource url: %s", oc_string(request->resource->uri));
  OC_DBG("FB value    : %d", fb_number);
  OC_DBG("FB instance : %d", fb_instance);

  // current datapoint amount in the FB (if instance/number not found a '-1' is input here)
  const int total = oc_core_count_dp_in_fb(fb_instance, fb_number);

  // handle query parameters l=ps and/or l=total
  if (query_l_was_processed(request, PAGE_SIZE, total))
    return;

  // first entry number of a resource that will be placed on a page
  const int first_entry = evaluate_query_px(request, &query_pn, &query_ps);

  // check if requested page will carry at least one resource e.g
  // - total=4, pn 5, ps 20, first entry = 100 -> no data on page 5 (all on page 0)
  // - total=4, pn 1, ps 04, first entry = 004 -> no data on page 1 (all on page 0) page 5
  if (first_entry >= total || query_ps == 0)
  {
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  // entries don't fit in a single page -> more pages are needed to get the full list
  // - total=4, page number 1, page size 02, first entry = 002 -> no more data on next page
  // - total=4, page number 1, page size 01, first entry = 001 -> more data on next page
  const bool more_request_needed = total > first_entry + query_ps ? true : false;

  int skipped_before_first_entry = 0;

  // create data points per functional block instance
  for (const oc_resource_t* resource = oc_ri_get_app_resources(); resource; resource = resource->next)
  {
    if (resource->properties & OC_DISCOVERABLE)
    {
      // is there something we can add to response 
      bool frame_resource = false;

      for (int i = 0; i < (int)oc_string_array_get_allocated_size(resource->types); i++)
      {
        /*
             framing by functional block numbers & instances, each
             FB resource MUST have 1...n 'dpa' type(s) assigned with a FULL URN
        */

        const char* single_type = oc_string_array_get_item(resource->types, i);
        if (strncmp(single_type, "urn:knx:dpa", 11) == 0)
        {
          if (get_fb_number_from_dp(single_type) == fb_number && resource->fb_instance == fb_instance)
          {
            frame_resource = true;
          }
        }
      }

      if (frame_resource)
      { // hit, will be added to response 

        if (skipped_before_first_entry < first_entry)
        { // do not add, is lower than needed 
          skipped_before_first_entry++;
        }
        else
        {// add

          // called from GET /fb/x handler so always truncate resources URN's
          oc_add_resource_to_response_payload(resource, &response_length, true);
          query_parameter_kvpair_matches++;

          if (query_parameter_kvpair_matches >= query_ps)
          {
            // page is full
            break;
          }
        }
      }
    }
  }

  if (query_parameter_kvpair_matches > 0)
  { // at least one found
    if (more_request_needed)
    {
      // no page # was in the request (query_p =0) = next page 1 else #+1
      response_length += add_next_page_indicator(oc_string(request->resource->uri), ++query_pn);
    }
    oc_prepare_linkformat_response(request, OC_STATUS_OK, response_length);
  }
  else
  {
    
    // an application FB resource without any (visible) datapoint can't be correct here
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_INTERNAL_SERVER_ERROR);
  }

  PRINT("oc_core_fb_x_get_handler - end");
}

// resource definition, details/comments see on 'core_resource_well_known_core'
extern const oc_resource_t core_resource_knx_swu_protocol;
PRAGMA_IN oc_resource_data_t core_resource_knx_f_x_data;
const oc_resource_t core_resource_knx_f_x = {(oc_resource_t*)&core_resource_knx_swu_protocol,
                                             {NULL, sizeof("/f/*"), "/f/*"},
                                             {NULL, 0, NULL},
                                             {NULL, 0, NULL},
                                             {APPLICATION_LINK_FORMAT, CONTENT_NONE},
                                             OC_UNDISCOVERABLE,
                                             {oc_core_fb_x_get_handler, NULL, OC_ACL_P | OC_ACL_D | OC_ACL_C, OC_IF_LI},
                                             {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                             {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                             {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                             {NULL, NULL},
                                             {NULL, NULL},
                                             0,
                                             0,
                                             1,
                                             &core_resource_knx_f_x_data};
PRAGMA_OUT

// -----------------------------------------------------------------------------

int oc_count_functional_blocks_from_application(void)
{
  int number_of_fbs = 0;

  if (g_nr_functional_blocks > 0)
  { // cached..., if calculated once, return value instead of compute it again
    return g_nr_functional_blocks;
  }

  // scan all application resources
  const oc_resource_t* resource = oc_ri_get_app_resources();
  for (; resource; resource = resource->next)
  {
    // skip non discoverable resources
    if (!(resource->properties & OC_DISCOVERABLE))
    { 
      continue;
    }

    // get rt type from array
    const oc_string_array_t types = resource->types;

    for (int i = 0; i < (int)oc_string_array_get_allocated_size(types); i++)
    {
      // get single type
      const char* t = oc_string_array_get_item(types, i);

        /*
           framing by functional block numbers & instances, each
           FB resource MUST have 1...n 'dpa' type(s) assigned with a FULL URN
        */
        if (strncmp(t, "urn:knx:dpa", 11) == 0)
        {
          int fb_number = get_fb_number_from_dp(t); 
          int fb_instance = resource->fb_instance;

          if (fb_number > 0 && !is_in_g_array(fb_number, fb_instance))
          {
            // add FB and/or its instance if FB number present and not yet count
            store_in_array(fb_number, fb_instance);
            number_of_fbs++;
          }
      }
    }
  }
  g_nr_functional_blocks = number_of_fbs;
  return g_nr_functional_blocks;
}

// check if an FB may be added according to the request and its query parameters (see below)
bool oc_check_if_functional_blocks_need_to_add(oc_request_t* request)
{
  char* value = NULL;
  size_t value_len;
  char* key;
  size_t key_len;
  char* rt_request = 0;
  int rt_len = 0;
  char* if_request = 0;
  int if_len = 0;
  char* wildcard = NULL;

  value_len = -1;
  oc_init_query_iterator();
  while (oc_iterate_query(request, &key, &key_len, &value, &value_len) > 0)
  {
    if (strncmp(key, "rt", key_len) == 0)
    {
      rt_request = value;
      rt_len = (int)value_len;
    }
    if (strncmp(key, "if", key_len) == 0)
    {
      if_request = value;
      if_len = (int)value_len;
    }
  }


  if (rt_len == 0 && if_len == 0)
  {
    // no 'rt' and no 'if' query parameter present --> add FBs
    return true;
  }
  if (rt_len > 0)
  {
    wildcard = memchr(rt_request, '*', rt_len);
    if (wildcard != NULL)
    {
      // wildcard query parameter --> add FBs
      return true;
    }
    if (strstr(rt_request, "fb") != NULL)
    {
      // 'fb' query parameter --> add FBs
      return true;
    }
  }
  if (if_len > 0)
  {
    wildcard = memchr(if_request, '*', if_len);
    if (wildcard != NULL)
    {
      // wildcard query parameter --> add FBs
      return true;
    }
    if (strstr(if_request, "ll") != NULL)
    {
      // 'll' query parameter --> add FBs
      return true;
    }
  }
  return false;
}

bool oc_was_adding_function_blocks_to_response(oc_request_t* request, bool short_urn_form, size_t* response_length, int* matches,
                                               int* skipped, int first_entry, int last_entry)
{
  (void)request;

  int original_matches = *matches;
  int counter = 0;

  const oc_resource_t* resource = oc_ri_get_app_resources();

  // scan all DPA resources to derive from it to the number of used FBs
  for (; resource; resource = resource->next)
  {
    // skip non discoverable resources
    if (!(resource->properties & OC_DISCOVERABLE))
    {
      continue;
    }

    // get rt type
    oc_string_array_t types = resource->types;

    for (int i = 0; i < (int)oc_string_array_get_allocated_size(types); i++)
    {
      // get single type
      const char* t = oc_string_array_get_item(types, i);

      /*
           regular functional block, framing by functional block numbers & instances
           note each FB resource has a 'dpa' type assigned
           (well-know EP with urn:..., knx specific EP without urn:...)
           note that a possible present iot router FB is also counted ones
      */
      if (strncmp(t, ":dpa", 4) == 0 || strncmp(t, "urn:knx:dpa", 11) == 0)
      {
        const int fp_int = get_fb_number_from_dp(t);
        const int instance = resource->fb_instance;

        // if FB and/or its instance not already counted ...
        if (fp_int > 0 && !is_in_g_array(fp_int, instance))
        {
          // add FB and/or its instance
          store_in_array(fp_int, instance);
          counter++;
        }
      }
    }
  }

  for (int i = 0; i < g_array_size; i++)
  {
    if (*skipped < first_entry)
    {
      (*skipped)++;
    }
    else if (first_entry + *matches >= last_entry)
    {
      return matches;
    }
    else
    {
      // the FB number with possible instance
      char fb_number[24];

      if (*response_length > 0)
      {
        // close previous record to create a new without LF (not found in RFC 6690)
        *response_length += oc_rep_add_line_to_buffer(",");
      }

      // FB URI (fix)
      *response_length += oc_rep_add_line_to_buffer("</f/");

      // > 1 of the same FB number ?
      if (g_int_array[1][i] > 0)
      {
        // functional block with instance by adding 2 instance numbers, e.g. <functional block>_<instance> -> example: 417_01
        (void)snprintf(fb_number, 23, "%d_%02d", g_int_array[0][i], g_int_array[1][i]);
      }
      else
      {
        // functional block with no instance, e.g. <functional block>_<instance> -> example: 417
        (void)snprintf(fb_number, 5, "%d", g_int_array[0][i]);
      }


      (*matches)++;

      // number
      *response_length += oc_rep_add_line_to_buffer(fb_number);

      // text (in relation on request)
      if (short_urn_form)
        *response_length += oc_rep_add_line_to_buffer(">;rt=\":fb.");
      else
        *response_length += oc_rep_add_line_to_buffer(">;rt=\"urn:knx:fb.");

      // FB instance (e.g. max functional block is 5 digits such as 12345)
      (void)snprintf(fb_number, 6, "%d", g_int_array[0][i]);
      *response_length += oc_rep_add_line_to_buffer(fb_number);

      // FB if and ct (fix)
      *response_length += oc_rep_add_line_to_buffer("\";if=\":if.ll\";ct=40");
    }
  }

  if (*matches > original_matches)
  {
    // update global counter
    if (g_nr_functional_blocks == 0)
    {
      // store the number of counted FBs so that we only have to do this once
      g_nr_functional_blocks = counter;
    }
    return true;
  }

  return false;
}

/*
 * return list of function blocks
 */
static void oc_core_fb_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  int query_parameter_kvpair_matches = 0; // query parameter key/value pair matches found
  size_t response_length = 0;
  int query_pn = PAGE_NUMBER;
  int query_ps = PAGE_SIZE;
  int skipped = 0;

  PRINT("oc_core_fb_get_handler");

  if (!oc_accept_header_is_ok(request, APPLICATION_LINK_FORMAT))
  {
    return;
  }

  // number of application FBs
  const int total = oc_count_functional_blocks_from_application();

  // handle query parameters l=ps and/or l=total
  if (query_l_was_processed(request, PAGE_SIZE, total))
    return;

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

  // entries don't fit in a single page -> more pages are needed to get the full list
  // - total=4, page number 1, page size 02, first entry = 002 -> no more data on next page
  // - total=4, page number 1, page size 01, first entry = 001 -> more data on next page
  const bool more_request_needed = total > first_entry + query_ps ? true : false;

  if (oc_was_adding_function_blocks_to_response(request, true, &response_length, &query_parameter_kvpair_matches,
                                                &skipped, first_entry, total))
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

  PRINT("oc_core_fb_get_handler - end");
}

// resource definition, details/comments see on 'core_resource_well_known_core'
PRAGMA_IN oc_resource_data_t core_resource_knx_f_data;
const oc_resource_t core_resource_knx_f = {(oc_resource_t*)&core_resource_knx_f_x,
                                           {NULL, sizeof("/f"), "/f"},
                                           {NULL, 0, NULL},
                                           {NULL, 0, NULL},
                                           {APPLICATION_LINK_FORMAT, CONTENT_NONE},
                                           OC_UNDISCOVERABLE,
                                           {oc_core_fb_get_handler, NULL, OC_ACL_P | OC_ACL_D | OC_ACL_C, OC_IF_LI},
                                           {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                           {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                           {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                           {NULL, NULL},
                                           {NULL, NULL},
                                           0,
                                           0,
                                           1,
                                           &core_resource_knx_f_data};
PRAGMA_OUT