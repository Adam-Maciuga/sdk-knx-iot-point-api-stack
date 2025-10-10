/*
 // Copyright (c) 2021,2023 Cascoda Ltd
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

#include "api/oc_knx_fp.h"
#include <stdio.h>
#include "oc_api.h"
#include "oc_core_res.h"
#include "oc_discovery.h"
#include "oc_helpers.h"
#include "oc_knx_helpers.h"
#define __STDC_FORMAT_MACROS // defined to use format specifiers also in C++
#include <inttypes.h>
#include "oc_knx_client.h"
#include "oc_storage.h"

// PUB/RCV/GOT storage data
#define GPT_STORE "dev_knx_pub_entry"       // PUB table base file name
#define GRT_STORE "dev_knx_rcv_entry"       // RCV table base file name
#define GOT_STORE "dev_knx_got_entry"       // GO table base file name
#define FPT_SIZE (sizeof(GPT_STORE) + 6)    // support of '_99999' PUB/RCP/GO FILE entries

// identifier for minimum pub/rcp properties
#define TABLE_ATREF (1 << 0)
#define TABLE_GAS (1 << 1)

// identifier for minimum group object properties
#define GO_HREF (1 << 0)
#define GO_GAS (1 << 1)

// note static variables are initialized with '0' first time
static oc_group_object_table_t g_got[GOT_MAX_ENTRIES];  // go table
static oc_group_table_t g_grt[GRT_MAX_ENTRIES];         // rcp table (to send)

#ifdef OC_PUBLISHER_TABLE
static oc_group_table_t g_gpt[GPT_MAX_ENTRIES];         // pub table (to receive)
#endif

// -externals -

static void oc_print_group_table_entry(int entry, char* store, oc_group_table_t* table);

static void oc_store_group_table_entry(int entry, char* store, const oc_group_table_t* table);

static int oc_delete_group_table_entry(int entry, char* store, oc_group_table_t* table, int max_size);

static int oc_core_find_index_in_table_from_id(int id, oc_group_table_t* table, int max_size);

int find_empty_slot_in_table(int id, oc_group_table_t* table, int max_size);

static uint32_t oc_find_grpid_in_table(oc_group_table_t* table, int max_size, uint32_t group_address);

// ------------

int oc_table_find_id_from_payload(const oc_rep_t* object)
{
  while (object)
  {
    switch (object->type)
    {
    case OC_REP_INT:
    {
      // pub/rcp id (0) is only for type int defined
      if (object->iname == 0)
      {
        int id = (int)object->value.integer;
        PRINT("find id from request: %d ", id);
        return id;
      }
    }
    break;
    default:
      break;
    }
    object = object->next;
  }

  PRINT("no id found (error)");
  return -1;
}

int find_empty_slot_in_group_object_table(void)
{
  for (int i = 0; i < GOT_MAX_ENTRIES; i++)
  {
    if (g_got[i].id == -1)
    { // empty slot is defined as 'not initialized'
      return i;
    }
  }
  return -1;
}

int oc_core_get_group_object_table_total_size(void) { return GOT_MAX_ENTRIES; }

oc_group_object_table_t* oc_core_get_group_object_table_entry(int index)
{
  if (index < 0 || index >= GOT_MAX_ENTRIES)
  {
    return NULL;
  }
  return &g_got[index];
}

int oc_core_find_index_in_group_object_table_from_id(int id)
{
  for (int i = 0; i < GOT_MAX_ENTRIES; i++)
  {
    if (g_got[i].id == id)
    {
      return i;
    }
  }
  return -1;
}

int oc_core_find_first_go_table_index_with_ga(uint32_t group_address)
{
  return oc_core_find_next_go_table_index_with_ga(group_address, -1);
}

int oc_core_find_next_go_table_index_with_ga(uint32_t group_address, int cur_index)
{
  for (int i = cur_index + 1; i < GOT_MAX_ENTRIES; i++)
  {
    if (g_got[i].id > -1)
    {
      for (int j = 0; j < g_got[i].ga_len; j++)
      {
        if (group_address == g_got[i].ga[j])
        {
          return i;
        }
      }
    }
  }
  return -1;
}

int oc_core_find_sending_ga_in_pos_zero_for_href(const char* resource_path, oc_cflag_mask_t* cflags)
{
  // init with number out of upper range defined so far 0..65535 = error
  int32_t lowest_id = INT_MAX;
  uint32_t corresponding_ga = 0;
  if (cflags)
    *cflags = OC_CFLAG_NONE;

  for (int i = 0; i < GOT_MAX_ENTRIES; i++)
  {
    if (g_got[i].id > -1)
    { // table entry present

      if (strlen(resource_path) == oc_string_len(g_got[i].href) && strcmp(resource_path, oc_string(g_got[i].href)) == 0)
      { // resource path matches

        if (g_got[i].id < lowest_id && g_got[i].ga_len > 0)
        { // id lower and ga's are used

          /*
             id is lower with ga's used, position zero (= sending GA), new id candidate, must loop over
             all GO index:
             - index 20: "id" = 12, "href" = abc, ga 200 in pos zero -> a receiving group address (+ flags)
             - index 25: "id" = 10, "href" = abc, ga 100 in pos zero -> the sending group address (+ flags)
          */

          lowest_id = g_got[i].id;
          // only the first GA can be a sending GA
          corresponding_ga = g_got[i].ga[0];
          if (cflags)
            *cflags = g_got[i].cflags;
        }
      }
    }
  }
  // no 'ga in pos zero' found returns -1
  return lowest_id < INT_MAX ? (int)corresponding_ga : -1;
}

oc_string_t oc_core_get_href_from_group_object_table_index(int index)
{
  const oc_string_t error = {0};
  return index < GOT_MAX_ENTRIES ? g_got[index].href : error;
}

oc_cflag_mask_t oc_core_get_cflags_from_group_object_table_index(int index)
{
  if (index < GOT_MAX_ENTRIES)
  {
    return g_got[index].cflags;
  }
  return OC_CFLAG_NONE;
}

int oc_core_get_ga_table_len_from_group_object_table_index(int index)
{
  if (index < GOT_MAX_ENTRIES)
  {
    return g_got[index].ga_len;
  }
  return 0;
}

uint32_t oc_core_get_ga_table_entry_from_group_object_table_index(int index, int entry)
{
  if (index < GOT_MAX_ENTRIES)
  {
    if (entry < g_got[index].ga_len)
    {
      return g_got[index].ga[entry];
    }
  }
  return 0;
}

int oc_core_find_first_group_object_table_index_from_href(const char* resource_path)
{
  for (int i = 0; i < GOT_MAX_ENTRIES; i++)
  {
    if (strlen(resource_path) == oc_string_len(g_got[i].href) && strcmp(resource_path, oc_string(g_got[i].href)) == 0)
    { // href len and content matches
      return i;
    }
  }
  return -1;
}

int oc_core_find_next_group_object_table_index_from_href(const char* resource_path, const int current_index)
{
  if (current_index == -1)
  { // don't iterate if already no index is available
    return -1;
  }

  for (int i = current_index + 1; i < GOT_MAX_ENTRIES; i++)
  {
    if (strlen(resource_path) == oc_string_len(g_got[i].href) && strcmp(resource_path, oc_string(g_got[i].href)) == 0)
    { // href len and content matches
      return i;
    }
  }
  return -1;
}

// TODO use static variable
static int oc_core_items_used_in_go_table(void)
{
  int counter = 0;
  for (int i = 0; i < GOT_MAX_ENTRIES; i++)
  {
    if (g_got[i].id > -1)
    {
      counter++;
    }
  }
  return counter;
}

// TODO use static variable
static int oc_core_items_used_in_rcp_table(void)
{
  int counter = 0;
  for (int i = 0; i < GRT_MAX_ENTRIES; i++)
  {
    if (g_grt[i].id > -1)
    {
      counter++;
    }
  }
  return counter;
}

bool oc_belongs_href_to_resource(oc_string_t href, bool discoverable)
{
  for (const oc_resource_t* resource = oc_ri_get_app_resources(); resource; resource = resource->next)
  {
    if (discoverable)
    {
      if (!(resource->properties & OC_DISCOVERABLE))
      {
        // skip non discoverable resources
        continue;
      }
    }
    if (oc_url_cmp(href, resource->uri) == 0)
    {
      return true;
    }
  }

  return false;
}

static void oc_core_fp_g_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  // query parameter key/value pair matches found
  int query_parameter_kvpair_matches = 0;
  size_t response_length = 0;
  int query_pn = PAGE_NUMBER;
  int query_ps = PAGE_SIZE;

  PRINT("oc_core_fp_g_get_handler - start");

  if (!oc_accept_header_is_ok(request, APPLICATION_LINK_FORMAT))
  {
    return;
  }

  // current resource amount, can never > than max table size
  const int total = oc_core_items_used_in_go_table();

  // handle query parameters l=ps (by using the server default value) and/or l=total
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

  // entries don't fit in a single page -> more pages are needed to get the full list
  // - total=4, page number 1, page size 02, first entry = 002 -> no more data on next page
  // - total=4, page number 1, page size 01, first entry = 001 -> more data on next page
  const bool more_request_needed = total > first_entry + query_ps ? true : false;

  // example </fp/g/1>;ct=60 ; must run through entire table since entries are stored randomly
  for (int i = first_entry; i < GOT_MAX_ENTRIES; i++)
  {
    if (g_got[i].id > -1)
    {
      if (response_length > 0)
      {
        // close previous record to create a new without LF (not found in RFC 6690)
        response_length += oc_rep_add_line_to_buffer(",");
      }

      response_length += oc_rep_add_line_to_buffer("</fp/g/");
      char string[10];
      (void)sprintf(string, "%d>", g_got[i].id);
      response_length += oc_rep_add_line_to_buffer(string);
      response_length += oc_rep_add_line_to_buffer(";ct=60");

      query_parameter_kvpair_matches++;

      if (query_parameter_kvpair_matches >= query_ps)
      {
        // don't add more than page supports
        break;
      }
    }
  }

  if (more_request_needed)
  {
    // add next page; note if no pn was in the request (query_p =0) next page = 0+1 else pn+1
    response_length += add_next_page_indicator(oc_string(request->resource->uri), ++query_pn);
  }
  oc_prepare_linkformat_response(request, OC_STATUS_OK, response_length);

  PRINT("oc_core_fp_g_get_handler - end");
}

static void oc_core_fp_g_post_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  // default assumption
  oc_status_t return_status = OC_STATUS_BAD_REQUEST;

  PRINT("oc_core_fp_g_post_handler - start");

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  if (oc_knx_get_lsm() != LSM_S_LOADING)
  {
    OC_ERR("not in loading state");
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_METHOD_NOT_ALLOWED);
    return;
  }

  // debugging info
  oc_print_rep_as_json(request->request_payload, true);

  /*
    A GO collection is checked per entry, the last return code wins (created/changed).
    Note that on a 4.00 / 5.00 the previously written entries - from
    a collection - are not restored (currently to complicate). This is for sure not
    a problem if a MaC writes only one entry per request.
  */

  // set ptr to collection of 1...n GOs in payload
  const oc_rep_t* rep = request->request_payload;
  oc_rep_t* object = NULL;

  // no payload -> 4.00
  while (rep)
  {
    switch (rep->type)
    {
    // a possible collection of GOs
    case OC_REP_OBJECT:

      // treat request payload value as one GO (that itself defines a chain of objects for id, href,...)
      object = rep->value.object;

      // find GO id in request
      const int id = oc_table_find_id_from_payload(object);
      if (id == -1)
      {
        OC_ERR("GO table id not found in request, but is a mandatory part");
        oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
        return;
      }

      // find index in GO table
      int array_index = oc_core_find_index_in_group_object_table_from_id(id);
      if (array_index != -1)
      {
        // GO id (array index) already in use, so it will be changed
        return_status = OC_STATUS_CHANGED;
      }
      else
      {
        // no GO id (array index) in use, so we will create one
        return_status = OC_STATUS_CREATED;

        // returns a valid index, if not -1
        array_index = find_empty_slot_in_group_object_table();
        if (array_index == -1)
        {
          OC_ERR("GO table has no empty slot to add a new GO entry");
          oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
          return;
        }
      }

#define MANDATORY_GO_PROPERTIES (4) // id, ga, cflags and href must be present
      bool id_only = true; // used to delete the GO table entry

      uint8_t allocator = 0; // identify which "stack" memory resource are allocated during the post
      oc_group_object_table_t tmp_go_entry = g_got[array_index]; // fill with live GO (from a present entry or from an empty entry)

      // set GO id
      tmp_go_entry.id = id;
      int current_go_properties = 1;

      while (object)
      {
        switch (object->type)
        {

        case OC_REP_INT:

          if (object->iname != 0)
          {
            // NOT id (0), for sure from now on a not 'ID only' case,
            // note that id (0) was already scanned/assigned
            id_only = false;
          }

          // cflags (8)
          if (object->iname == 8)
          {
            // set flags in tmp copy
            tmp_go_entry.cflags = (int)object->value.integer;
            current_go_properties++;
          }

          break;
        case OC_REP_STRING:

          // any extra element - even if not valid - causes a "not an id only"
          id_only = false;

          // href (11)
          if (object->iname == 11)
          {
            // set (new) href in tmp copy (org ptr still valid)
            oc_new_string(&tmp_go_entry.href, oc_string(object->value.string), oc_string_len(object->value.string));

            current_go_properties++;
            allocator |= GO_HREF;
          }

          break;
        case OC_REP_INT_ARRAY:

          // any extra element - even if not valid - causes a "not an id only"
          id_only = false;

          // ga array (7)
          if (object->iname == 7)
          {
            const int64_t* array = oc_int_array(object->value.array);
            const int new_array_size = oc_int_array_size(object->value.array);

            // malloc of 'zero' byte return pointer is undefined, ga size shall be 32 bit
            uint32_t* new_array = malloc(new_array_size * sizeof(uint32_t));
            if (new_array && new_array_size > 0)
            {
              for (int i = 0; i < new_array_size; i++)
              {
                new_array[i] = (uint32_t)array[i];
              }

              PRINT("ga size %d", new_array_size);

              // assign GA array in tmp copy (org ptr still valid)
              tmp_go_entry.ga_len = new_array_size;
              tmp_go_entry.ga = new_array;

              current_go_properties++;
              allocator |= GO_GAS;
            }
            else
            {
              OC_ERR("out of stack memory");

              // on error: free GO tmp entry (all heap allocations)
              oc_free_allocated_go_table_elements(&tmp_go_entry, allocator);

              oc_prepare_no_format_response_no_payload(request, OC_STATUS_INTERNAL_SERVER_ERROR);
              return;
            }
          }

          break;
        default:

          // any other invalid type returns a 4.00
          // note that an empty ga array (7: [] = EITT test) is coded in current CBOR with "OC_REP_NIL"
          OC_ERR("invalid object type detected");

          // on error: free GO tmp entry (all heap allocations)
          oc_free_allocated_go_table_elements(&tmp_go_entry, allocator);

          oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
          return;
        }

        object = object->next;
      }

      /*
        a: created +  id only/< 3 elements  = ERROR (to few elements)
        b: created +  4 elements            = OK (create)
        c: changed +  id only               = OK (delete)
        d: changed +  1..3 elements         = OK (update)

      */

      if (return_status == OC_STATUS_CHANGED && id_only)
      { // c

        // no tmp elements allocated ...
        PRINT("only found id in request, deleting entry at index: %d", array_index);
        oc_delete_group_object_table_entry(array_index);
      }
      else
      {
        if (return_status == OC_STATUS_CREATED && current_go_properties < MANDATORY_GO_PROPERTIES)
        { // a

          // id + ga (filled or empty) AND at least one of ia, grpid or url must be present
          PRINT("mandatory items missing, no entry created at index: %d", array_index);

          // on error: free PUB tmp entry (all heap allocations)
          oc_free_allocated_go_table_elements(&tmp_go_entry, allocator);

          oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
          return;
        }

        // b + d

        bool do_save = true;
        // check entry with some additional sanity checks
        // a "bad" status will stop processing and return a 4.00

        // note that an empty array ( 7: []) does not end up in setting the GA len, it ends in 4.00
        if (tmp_go_entry.ga_len == 0)
        {
          do_save = false;
          OC_ERR("no ga's %d", tmp_go_entry.ga_len);
        }
        if (tmp_go_entry.cflags == 0)
        {
          do_save = false;
          OC_ERR("no cflags set %d", tmp_go_entry.cflags);
        }
        if (oc_string_len(tmp_go_entry.href) > OC_MAX_URL_LENGTH)
        {
          do_save = false;
          OC_ERR("href is longer than %d", OC_MAX_URL_LENGTH);
        }
        if (!oc_belongs_href_to_resource(tmp_go_entry.href, true))
        {
          do_save = false;
          OC_ERR("href '%s' does not belong to device", oc_string_checked(tmp_go_entry.href));
        }

        if (!do_save)
        {
          // on error: free GO tmp entry (all heap allocations)
          oc_free_allocated_go_table_elements(&tmp_go_entry, allocator);

          oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
          return;
        }

        // here all ok, set new GO entry
        PRINT("storing GO table entry at %d", array_index);

        /*
          Free - on stack allocated -  live GO table entry elements that will be overwritten next.
          - created : 2 elements (GAs, HREF)
          - changed : 1..2 elements (GAs, HREF) - 0 not possible, would be to delete a GO entry
        */
        oc_free_allocated_go_table_elements(&g_got[array_index], allocator);

        // assign GO table entry with tmp GO (all elements)
        g_got[array_index] = tmp_go_entry;

        // debugging
        oc_print_group_object_table_entry(array_index);

        // store (includes here an overwrite)
        oc_store_group_object_table_entry(array_index);
      }

      break;
    default:
      break;
    }

    // next GO entry
    rep = rep->next;
  }

  // create/update a GO --> update
  oc_knx_increase_fingerprint();

  // the last return status from a collection with 'n' POST elements is responded (CREATED/CHANGED)
  oc_prepare_no_format_response_no_payload(request, return_status);


  PRINT("oc_core_fp_g_post_handler - end");
}

// resource definition, details/comments see on 'core_resource_well_known_core'
extern const oc_resource_t core_resource_knx_fp_g_x;
PRAGMA_IN oc_resource_data_t core_resource_knx_fp_g_data;
const oc_resource_t core_resource_knx_fp_g = {(oc_resource_t*)&core_resource_knx_fp_g_x,
                                              {NULL, sizeof("/fp/g"), "/fp/g"},
                                              {NULL, 0, NULL},
                                              {NULL, 0, NULL},
                                              {APPLICATION_CBOR, CONTENT_NONE},
                                              OC_DISCOVERABLE,
                                              {oc_core_fp_g_get_handler, NULL, OC_ACL_P | OC_ACL_D | OC_ACL_C, OC_IF_LI},
                                              {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                              {oc_core_fp_g_post_handler, NULL, OC_ACL_C, OC_IF_C | OC_IF_B},
                                              {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                              {NULL, NULL},
                                              {NULL, NULL},
                                              0,
                                              0,
                                              1,
                                              &core_resource_knx_fp_g_data};
PRAGMA_OUT

static void oc_core_fp_g_x_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;
  PRINT("oc_core_fp_g_x_get_handler - start");

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  const int id = oc_uri_get_wildcard_int_value_as_int(
    oc_string(request->resource->uri), 
    oc_string_len(request->resource->uri),
    request->uri_path,
    request->uri_path_len);

  // find GO index in GO table
  int index = oc_core_find_index_in_group_object_table_from_id(id);
  PRINT("id=%d index = %d", id, index);
  if (index == -1)
  {
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_NOT_FOUND);
    return;
  }

  if (g_got[index].id == -1)
  {
    // GO table entry is not used
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_INTERNAL_SERVER_ERROR);
    return;
  }

  oc_rep_begin_root_object();

  // id - 0
  oc_rep_i_set_int(root, 0, g_got[index].id);
  // href - 11
  oc_rep_i_set_text_string(root, 11, oc_string(g_got[index].href));
  // ga - 7
  oc_rep_i_set_int_array(root, 7, g_got[index].ga, g_got[index].ga_len);
  // cflags - 8
  oc_rep_i_set_int(root, 8, g_got[index].cflags);

  oc_rep_end_root_object();
  oc_prepare_cbor_response(request, OC_STATUS_OK);

  PRINT("oc_core_fp_g_x_get_handler - end");
}

static void oc_core_fp_g_x_del_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;
  PRINT("oc_core_fp_g_x_del_handler - start");


  if (oc_knx_get_lsm() != LSM_S_LOADING)
  {
    OC_ERR("not in loading state");
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  int id = oc_uri_get_wildcard_int_value_as_int(
    oc_string(request->resource->uri),
    oc_string_len(request->resource->uri),
    request->uri_path,
    request->uri_path_len);

  int index = oc_core_find_index_in_group_object_table_from_id(id);

  if (index == -1)
  {
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_NOT_FOUND);
    return;
  }

  oc_delete_group_object_table_entry(index);

  // delete fp/g --> update
  oc_knx_increase_fingerprint();

  oc_prepare_no_format_response_no_payload(request, OC_STATUS_DELETED);

  PRINT("oc_core_fp_g_x_del_handler - end");
}

#ifdef OC_PUBLISHER_TABLE

// resource definition, details/comments see on 'core_resource_well_known_core'
extern const oc_resource_t core_resource_knx_fp_p;
PRAGMA_IN oc_resource_data_t core_resource_knx_fp_g_x_data;
const oc_resource_t core_resource_knx_fp_g_x = {(oc_resource_t*)&core_resource_knx_fp_p,
                                                {NULL, sizeof("/fp/g/*"), "/fp/g/*"},
                                                {NULL, 0, NULL},
                                                {NULL, 0, NULL},
                                                {APPLICATION_CBOR, CONTENT_NONE},
                                                OC_DISCOVERABLE,
                                                {oc_core_fp_g_x_get_handler, NULL, OC_ACL_D, OC_IF_D},
                                                {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                {oc_core_fp_g_x_del_handler, NULL, OC_ACL_C, OC_IF_C},
                                                {NULL, NULL},
                                                {NULL, NULL},
                                                0,
                                                0,
                                                1,
                                                &core_resource_knx_fp_g_x_data};
PRAGMA_OUT
#else

// resource definition, details/comments see on 'core_resource_well_known_core'
extern const oc_resource_t core_resource_knx_fp_r;
PRAGMA_IN oc_resource_data_t core_resource_knx_fp_g_x_data;
const oc_resource_t core_resource_knx_fp_g_x = {(oc_resource_t*)&core_resource_knx_fp_r,
                                                {NULL, sizeof("/fp/g/*"), "/fp/g/*"},
                                                {NULL, 0, NULL},
                                                {NULL, 0, NULL},
                                                {APPLICATION_CBOR, CONTENT_NONE},
                                                OC_DISCOVERABLE,
                                                {oc_core_fp_g_x_get_handler, NULL, OC_ACL_P, OC_IF_P},
                                                {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                {oc_core_fp_g_x_del_handler, NULL, OC_ACL_C, OC_IF_C},
                                                {NULL, NULL},
                                                {NULL, NULL},
                                                0,
                                                0,
                                                1,
                                                &core_resource_knx_fp_g_x_data};
PRAGMA_OUT
#endif

// -PUBLISHER-

#ifdef OC_PUBLISHER_TABLE

int oc_core_find_index_in_publisher_table_from_id(int id)
{
  return oc_core_find_index_in_table_from_id(id, g_gpt, GPT_MAX_ENTRIES);
}

uint32_t oc_find_grpid_in_publisher_table(uint32_t group_address)
{
  return oc_find_grpid_in_table(g_gpt, GPT_MAX_ENTRIES, group_address);
}

// TODO use static variable
static int oc_core_items_used_in_pub_table(void)
{
  int counter = 0;
  for (int i = 0; i < GPT_MAX_ENTRIES; i++)
  {
    if (g_gpt[i].id > -1)
    {
      counter++;
    }
  }
  return counter;
}

static void oc_core_fp_p_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  int query_parameter_kvpair_matches = 0; // query parameter key/value pair matches found
  size_t response_length = 0;
  int query_pn = PAGE_NUMBER;
  int query_ps = PAGE_SIZE;

  PRINT("oc_core_fp_p_get_handler - start");

  if (!oc_accept_header_is_ok(request, APPLICATION_LINK_FORMAT))
  {
    return;
  }

  // current resource amount
  const int total = oc_core_items_used_in_pub_table();

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

  // entries don't fit in a single page -> more pages are needed to get the full list
  // - total=4, page number 1, page size 02, first entry = 002 -> no more data on next page
  // - total=4, page number 1, page size 01, first entry = 001 -> more data on next page
  const bool more_request_needed = total > first_entry + query_ps ? true : false;

  // example </fp/p/1>;ct=60 ; must run through entire table since entries are stored randomly
  for (int i = first_entry; i < GPT_MAX_ENTRIES; i++)
  {
    if (g_gpt[i].id > -1)
    {
      if (response_length > 0)
      {
        // close previous record to create a new without LF (not found in RFC 6690)
        response_length += oc_rep_add_line_to_buffer(",");
      }

      response_length += oc_rep_add_line_to_buffer("</fp/p/");
      char string[10];
      (void)sprintf((char*)&string, "%d>", g_gpt[i].id);
      response_length += oc_rep_add_line_to_buffer(string);
      response_length += oc_rep_add_line_to_buffer(";ct=60");

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

  PRINT("oc_core_fp_p_get_handler - end");
}

static void oc_core_fp_p_post_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  // default assumption
  oc_status_t return_status = OC_STATUS_BAD_REQUEST;

  PRINT("oc_core_fp_p_post_handler - start");

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  if (oc_knx_get_lsm() != LSM_S_LOADING)
  {
    OC_ERR("not in loading state");
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_METHOD_NOT_ALLOWED);
    return;
  }

  // debugging info
  oc_print_rep_as_json(request->request_payload, true);

  /*
    A PUB collection is checked per entry, the last return code wins (created/changed).
    Note that on a 4.00 / 5.00 the previously written entries - from
    a collection - are not restored (currently to complicate). This is for sure not
    a problem if a MaC writes only one entry per request.
  */

  // set ptr to collection of 1...n PUB entries in payload
  const oc_rep_t* rep = request->request_payload;
  const oc_rep_t* object = NULL;

  // no payload -> 4.00
  while (rep)
  {
    switch (rep->type)
    {
    // a possible collection of PUB entries
    case OC_REP_OBJECT:

      // treat request payload value as one entry (that itself defines a chain of objects for id, ia,...)
      object = rep->value.object;

      // find PUB id in request
      const int id = oc_table_find_id_from_payload(object);
      if (id == -1)
      {
        OC_ERR("PUB table id not found in request, but is a mandatory part");
        oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
        return;
      }

      // find index in PUB table
      int array_index = oc_core_find_index_in_table_from_id(id, g_gpt, GPT_MAX_ENTRIES);
      if (array_index != -1)
      {
        // index already in use, so it will be changed
        return_status = OC_STATUS_CHANGED;
      }
      else
      {
        // no index, so we will create one
        return_status = OC_STATUS_CREATED;

        // no index, so we will create one (default)
        array_index = find_empty_slot_in_table(id, g_gpt, GPT_MAX_ENTRIES);
        if (array_index == -1)
        {
          OC_ERR("PUB table has no empty slot to add a new entry");
          oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
          return;
        }
      }

// id + ga array (filled or empty) + grpid OR id + ia + at must be present
#define MANDATORY_GPT_PROPERTIES (3)

      // to delete a PUB table entry
      bool id_only = true;

      // identify which "stack" memory resource are allocated during the post
      uint8_t allocator = 0;

      // fill with live PUB entry (from a present/empty entry)
      oc_group_table_t tmp_gpt_entry = g_gpt[array_index];

      // set PUB id
      tmp_gpt_entry.id = id;
      int current_gpt_properties = 1;

      while (object)
      {
        switch (object->type)
        {
        case OC_REP_INT:

          if (object->iname != 0)
          {
            // NOT an id (0), for sure from now on a not 'ID only' case,
            // note that id (0) was already scanned/assigned
            id_only = false;
          }

          // ia (12) - used on unicast
          if (object->iname == 12)
          {
            tmp_gpt_entry.ia = (int)object->value.integer;
            current_gpt_properties++;
          }
          // grpid (13) - used on multicast
          else if (object->iname == 13)
          {
            tmp_gpt_entry.grpid = (uint32_t)object->value.integer;
            current_gpt_properties++;
          }
          // iid (26) - used on multicast
          else if (object->iname == 26)
          {
            tmp_gpt_entry.iid = object->value.integer;
          }
          // fid (25) - used on unicast
          else if (object->iname == 25)
          {
            tmp_gpt_entry.fid = object->value.integer;
          }

          break;
        case OC_REP_STRING:

          // any extra element - even if not valid - causes a "not an id only"
          id_only = false;

          // at (14) - used on unicast
          if (object->iname == 14)
          {
            // set (new) at in tmp copy (org ptr still valid)
            oc_new_string(&tmp_gpt_entry.at, oc_string(object->value.string), oc_string_len(object->value.string));

            allocator |= TABLE_ATREF;
          }

          break;
        case OC_REP_INT_ARRAY:

          // any extra element - even if not valid - causes a "not an id only"
          id_only = false;

          // ga array (7) - used on multicast
          if (object->iname == 7) // resource 'ga array'
          {
            const int64_t* array = oc_int_array(object->value.array);
            const int array_size = oc_int_array_size(object->value.array);

            // malloc of 'zero' byte return pointer is undefined
            // ga size shall be 32 bit
            uint32_t* new_array = malloc(array_size * sizeof(uint32_t));
            if (new_array && array_size > 0)
            {
              for (int i = 0; i < array_size; i++)
              {
                new_array[i] = (uint32_t)array[i];
              }

              PRINT("ga size %d", array_size);

              tmp_gpt_entry.ga_len = array_size;
              tmp_gpt_entry.ga = new_array;

              current_gpt_properties++;
              allocator |= TABLE_GAS;
            }
            else
            {
              OC_ERR("out of stack memory");

              // on error: free PUB tmp entry (all heap allocations)
              oc_free_allocated_table_elements(&tmp_gpt_entry, allocator);

              oc_prepare_no_format_response_no_payload(request, OC_STATUS_INTERNAL_SERVER_ERROR);
              return;
            }
          }

          break;
        case OC_REP_NIL:

          // any extra element - even if not valid - causes a "not an id only"
          id_only = false;

          // ga array (7) - used on multicast, resource 'ga array' = empty (specification request)
          if (object->iname == 7)
          {
            tmp_gpt_entry.ga_len = 0;
            tmp_gpt_entry.ga = NULL;

            current_gpt_properties++; // also on empty ga array satisfies the items number
            allocator |= TABLE_GAS; // free() ignores NULL ptr
          }

          break;
        default:

          // any other invalid type returns a 4.00
          OC_ERR("invalid object type detected");

          // on error: free PUB tmp entry (all heap allocations)
          oc_free_allocated_table_elements(&tmp_gpt_entry, allocator);

          oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
          return;
        }
        object = object->next;
      }

      /*
        Options
        -------

        a: created +  id only/< 3 elements  = ERROR (to few elements)
        b: created +  3 elements            = OK (create)
        c: changed +  id only               = OK (delete)
        d: changed +  1/2 elements          = OK (update)

      */

      if (return_status == OC_STATUS_CHANGED && id_only)
      { // c

        // no tmp elements will be allocated ...
        PRINT("only found id in request, deleting entry at index: %d", array_index);
        oc_delete_group_table_entry(array_index, GPT_STORE, g_gpt, GPT_MAX_ENTRIES);
      }
      else
      {
        if (return_status == OC_STATUS_CREATED && current_gpt_properties < MANDATORY_GPT_PROPERTIES)
        { // a

          // see details on constant
          PRINT("mandatory items missing, no entry created at index: %d", array_index);

          // on error: free PUB tmp entry (all heap allocations)
          oc_free_allocated_table_elements(&tmp_gpt_entry, allocator);

          oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
          return;
        }

        // b + d, here all ok, set new PUB entry
        PRINT("storing PUB table at %d", array_index);

        /*
          Free - on stack allocated -  live PUB table entry elements that will be overwritten next.
          - created : 2 elements (GAs, AT)
          - changed : 1..2 elements (GAs, AT) - 0 not possible, would be to delete a PUB entry
        */
        oc_free_allocated_table_elements(&g_gpt[array_index], allocator);

        // assign PUB table entry with tmp PUB (all elements)
        g_gpt[array_index] = tmp_gpt_entry;

        // debugging
        oc_print_group_table_entry(array_index, GPT_STORE, g_gpt);

        // store (includes here an overwrite)
        oc_store_group_table_entry(array_index, GPT_STORE, g_gpt);
      }

      break;
    default:
      break;
    }

    // next PUB entry
    rep = rep->next;
  }

  // create/update a PUB entry
  oc_knx_increase_fingerprint();

  // the last (positive) return status from a collection with 'n' POST elements is responded (CREATED/CHANGED)
  oc_prepare_no_format_response_no_payload(request, return_status);

  PRINT("oc_core_fp_p_post_handler - end");
}

// resource definition, details/comments see on 'core_resource_well_known_core'
extern const oc_resource_t core_resource_knx_fp_p_x;
PRAGMA_IN oc_resource_data_t core_resource_knx_fp_p_data;
const oc_resource_t core_resource_knx_fp_p = {(oc_resource_t*)&core_resource_knx_fp_p_x,
                                              {NULL, sizeof("/fp/p"), "/fp/p"},
                                              {NULL, 0, NULL},
                                              {NULL, 0, NULL},
                                              {APPLICATION_CBOR, CONTENT_NONE},
                                              OC_DISCOVERABLE,
                                              {oc_core_fp_p_get_handler, NULL, OC_ACL_P | OC_ACL_D | OC_ACL_C, OC_IF_LI},
                                              {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                              {oc_core_fp_p_post_handler, NULL, OC_ACL_C, OC_IF_C | OC_IF_B},
                                              {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                              {NULL, NULL},
                                              {NULL, NULL},
                                              0,
                                              0,
                                              1,
                                              &core_resource_knx_fp_p_data};
PRAGMA_OUT

static void oc_core_fp_p_x_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;
  PRINT("oc_core_fp_p_x_get_handler - start");

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  const int id = oc_uri_get_wildcard_int_value_as_int(
    oc_string(request->resource->uri),
    oc_string_len(request->resource->uri),
    request->uri_path,
    request->uri_path_len);

  const int index = oc_core_find_index_in_table_from_id(id, g_gpt, GPT_MAX_ENTRIES);

  PRINT("id:%d index = %d", id, index);

  if (index == -1)
  {
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_NOT_FOUND);
    return;
  }

  if (g_gpt[index].id == -1)
  {
    // index not present in PUB table
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_INTERNAL_SERVER_ERROR);
    return;
  }

  oc_rep_begin_root_object();

  // id - 0
  oc_rep_i_set_int(root, 0, g_gpt[index].id);

  // ia - 12
  if (g_gpt[index].ia > -1)
  {
    oc_rep_i_set_int(root, 12, g_gpt[index].ia);
  }

  // grpid - 13
  if (g_gpt[index].grpid > 0)
  {
    oc_rep_i_set_int(root, 13, g_gpt[index].grpid);
  }

  // fid - 25
  if (g_gpt[index].fid > -1)
  {
    oc_rep_i_set_int(root, 25, g_gpt[index].fid); // id 25
  }

  // iid - 26
  if (g_gpt[index].iid > -1)
  {
    oc_rep_i_set_int(root, 26, g_gpt[index].iid);
  }

  // at - 14
  if (oc_string_len(g_gpt[index].at) > 0)
  {
    oc_rep_i_set_text_string(root, 14, oc_string(g_gpt[index].at));
  }

  // ga - 7
  oc_rep_i_set_int_array(root, 7, g_gpt[index].ga, g_gpt[index].ga_len);

  oc_rep_end_root_object();

  oc_prepare_cbor_response(request, OC_STATUS_OK);

  PRINT("oc_core_fp_p_x_get_handler - end");
}

static void oc_core_fp_p_x_del_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;
  PRINT("oc_core_fp_p_x_del_handler - start");


  if (oc_knx_get_lsm() != LSM_S_LOADING)
  {
    OC_ERR("not in loading state");
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  int id = oc_uri_get_wildcard_int_value_as_int(
    oc_string(request->resource->uri),
    oc_string_len(request->resource->uri),
    request->uri_path,
    request->uri_path_len);

  int index = oc_core_find_index_in_table_from_id(id, g_gpt, GPT_MAX_ENTRIES);

  if (index == -1)
  {
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_NOT_FOUND);
    return;
  }

  oc_delete_group_table_entry(index, GPT_STORE, g_gpt, GPT_MAX_ENTRIES);

  // delete fp/p --> update
  oc_knx_increase_fingerprint();

  oc_prepare_no_format_response_no_payload(request, OC_STATUS_DELETED);

  PRINT("oc_core_fp_p_x_del_handler - end");
}

// resource definition, details/comments see on 'core_resource_well_known_core'
extern const oc_resource_t core_resource_knx_fp_r;
PRAGMA_IN oc_resource_data_t core_resource_knx_fp_p_x_data;
const oc_resource_t core_resource_knx_fp_p_x = {(oc_resource_t*)&core_resource_knx_fp_r,
                                                {NULL, sizeof("/fp/p/*"), "/fp/p/*"},
                                                {NULL, 0, NULL},
                                                {NULL, 0, NULL},
                                                {APPLICATION_CBOR, CONTENT_NONE},
                                                OC_DISCOVERABLE,
                                                {oc_core_fp_p_x_get_handler, NULL, OC_ACL_D, OC_IF_D},
                                                {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                {oc_core_fp_p_x_del_handler, NULL, OC_ACL_C, OC_IF_C},
                                                {NULL, NULL},
                                                {NULL, NULL},
                                                0,
                                                0,
                                                1,
                                                &core_resource_knx_fp_p_x_data};
PRAGMA_OUT
#endif

// -RECIPIENT-

static void oc_core_fp_r_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  int query_parameter_kvpair_matches = 0; // query parameter key/value pair matches found
  size_t response_length = 0;
  int query_pn = PAGE_NUMBER;
  int query_ps = PAGE_SIZE;

  PRINT("oc_core_fp_r_get_handler - start");

  if (!oc_accept_header_is_ok(request, APPLICATION_LINK_FORMAT))
  {
    return;
  }

  // current resource amount
  const int total = oc_core_items_used_in_rcp_table();

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

  // entries don't fit in a single page -> more pages are needed to get the full list
  // - total=4, page number 1, page size 02, first entry = 002 -> no more data on next page
  // - total=4, page number 1, page size 01, first entry = 001 -> more data on next page
  const bool more_request_needed = total > first_entry + query_ps ? true : false;

  // example </fp/r/1>;ct=60 ; must run through entire table since entries are stored randomly
  for (int i = first_entry; i < GRT_MAX_ENTRIES; i++)
  {
    if (g_grt[i].id > -1)
    {
      if (response_length > 0)
      {
        // close previous record to create a new without LF (not found in RFC 6690)
        response_length += oc_rep_add_line_to_buffer(",");
      }

      response_length += oc_rep_add_line_to_buffer("</fp/r/");
      char string[10];
      (void)sprintf(string, "%d>", g_grt[i].id);
      response_length += oc_rep_add_line_to_buffer(string);
      response_length += oc_rep_add_line_to_buffer(";ct=60");

      query_parameter_kvpair_matches++;

      if (query_parameter_kvpair_matches >= query_ps)
      {
        // don't add more than page supports
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

  PRINT("oc_core_fp_r_get_handler - end");
}

static void oc_core_fp_r_post_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  // default assumption
  oc_status_t return_status = OC_STATUS_BAD_REQUEST;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  if (oc_knx_get_lsm() != LSM_S_LOADING)
  {
    OC_ERR("not in loading state");
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_METHOD_NOT_ALLOWED);
    return;
  }

  // debugging info
  oc_print_rep_as_json(request->request_payload, true);

  /*
    An RCP collection is checked per entry, the last return code wins (created/changed).
    Note that on a 4.00 / 5.00 the previously written entries - from
    a collection - are not restored (currently to complicate). This is for sure not
    a problem if a MaC writes only one entry per request.
  */

  // set ptr to collection of 1...n RCP entries in payload
  const oc_rep_t* rep = request->request_payload;
  const oc_rep_t* object = NULL;

  // no payload -> 4.00
  while (rep)
  {
    switch (rep->type)
    {
      // a possible collection of RCP entries
    case OC_REP_OBJECT:

      // treat request payload value as one entry (that itself defines a chain of objects for id, ia,...)
      object = rep->value.object;

      // find RCP id in request
      const int id = oc_table_find_id_from_payload(object);
      if (id == -1)
      {
        OC_ERR("RCP table id not found in request, but is a mandatory part");
        oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
        return;
      }

      // find index in RCP table
      int array_index = oc_core_find_index_in_table_from_id(id, g_grt, GRT_MAX_ENTRIES);
      if (array_index != -1)
      {
        // index already in use, so it will be changed
        return_status = OC_STATUS_CHANGED;
      }
      else
      {
        // no index, so we will create one
        return_status = OC_STATUS_CREATED;

        // no index, so we will create one
        array_index = find_empty_slot_in_table(id, g_grt, GRT_MAX_ENTRIES);
        if (array_index == -1)
        {
          OC_ERR("RCP table has no empty slot to add a new entry");
          oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
          return;
        }

        // non-confirmable flag for a new entry is init to false ONLY once on creation (not on a possible 'changed' update)
      }

// id + ga array (filled or empty) + grpid OR id + ia + at must be present
#define MANDATORY_GRT_PROPERTIES (3)

      // used to delete the RCP table entry
      bool id_only = true;

      // identify which "stack" memory resource are allocated during the post
      uint8_t allocator = 0;

      // fill with live RCP entry (from a present/empty entry)
      oc_group_table_t tmp_grt_entry = g_grt[array_index];

      // set RCP id
      tmp_grt_entry.id = id;
      int current_grt_properties = 1;

      while (object)
      {
        switch (object->type)
        {
        case OC_REP_INT:

          if (object->iname != 0)
          {
            // NOT an id (0), for sure from now on a not 'ID only' case,
            // note that id (0) was already scanned/assigned
            id_only = false;
          }

          // ia (12) - used on unicast
          if (object->iname == 12)
          {
            tmp_grt_entry.ia = (int)object->value.integer;
            current_grt_properties++;
          }
          // grpid (13) - used on multicast
          else if (object->iname == 13)
          {
            tmp_grt_entry.grpid = (uint32_t)object->value.integer;
            current_grt_properties++;
          }
          // iid (26) - used on multicast
          else if (object->iname == 26)
          {
            tmp_grt_entry.iid = object->value.integer;
          }
          // fid (25) - used on unicast
          else if (object->iname == 25)
          {
            tmp_grt_entry.fid = object->value.integer;
          }

          break;
        case OC_REP_STRING:

          // any extra element - even if not valid - causes a "not an id only"
          id_only = false;

          // at (14) - used on unicast, see IMPORTANT notes below (in Options)
          if (object->iname == 14)
          {
            // set (new) at in tmp copy (org ptr still valid)
            oc_new_string(&tmp_grt_entry.at, oc_string(object->value.string), oc_string_len(object->value.string));

            allocator |= TABLE_ATREF;
          }

          break;
        case OC_REP_INT_ARRAY:

          // any extra element - even if not valid - causes a "not an id only"
          id_only = false;

          // ga array (7) - used on multicast
          if (object->iname == 7)
          {
            const int64_t* array = oc_int_array(object->value.array);
            const int new_array_size = oc_int_array_size(object->value.array);

            // malloc of 'zero' byte return pointer is undefined
            // ga size shall be 32 bit
            uint32_t* new_array = malloc(new_array_size * sizeof(uint32_t));
            if (new_array && new_array_size > 0)
            {
              for (int i = 0; i < new_array_size; i++)
              {
                new_array[i] = (uint32_t)array[i];
              }

              PRINT("ga size %d", new_array_size);

              tmp_grt_entry.ga_len = new_array_size;
              tmp_grt_entry.ga = new_array;

              current_grt_properties++;
              allocator |= TABLE_GAS;
            }
            else
            {
              OC_ERR("out of stack memory");

              // on error: free PUB tmp entry (all heap allocations)
              oc_free_allocated_table_elements(&tmp_grt_entry, allocator);

              oc_prepare_no_format_response_no_payload(request, OC_STATUS_INTERNAL_SERVER_ERROR);
              return;
            }
          }

          break;

        case OC_REP_NIL:

          // any extra element - even if not valid - causes a "not an id only"
          id_only = false;

          // ga array (7) - used on multicast, resource 'ga array' = empty (specification request)
          if (object->iname == 7)
          {
            tmp_grt_entry.ga_len = 0;
            tmp_grt_entry.ga = NULL;

            current_grt_properties++; // also an empty ga array satisfies the items number
            allocator |= TABLE_GAS; // free() ignores NULL ptr
          }

          break;
        case OC_REP_BOOL:

          // any extra element - even if not valid - causes a "not an id only"
          id_only = false;

          // resource 'non' (CBOR/JSON = 'non'/'non') - used on unicast, multicast are always NON messages (=true)
          if (oc_string_len(object->name) > 0 && strncmp(oc_string(object->name), "non", 3) == 0)
          {
            tmp_grt_entry.non = object->value.boolean;
          }

          break;
        default:

          // any other invalid type returns a 4.00
          OC_ERR("invalid object type detected");

          // on error: free RCP tmp entry (all heap allocations)
          oc_free_allocated_table_elements(&tmp_grt_entry, allocator);

          oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
          break;
        }
        object = object->next;
      }

      /*
        Options
        -------

        a: created +  id only/< 3 elements  = ERROR (to few elements)
        b: created +  3 elements            = OK (create)
        c: changed +  id only               = OK (delete)
        d: changed +  1/2 elements          = OK (update)


        Notes
        -----
        A MaC shall not configure the same access token for different IA recipients (unicast) in the
        RCP table, e.g.;
        - RCP entry 1: (ia) 1234, (at) = 'token1'
        - RCP entry 2: (ia) 1235, (at) = 'token1'

        A sender would use the same access token (key material) from 'token1' to encrypt a message
        for ia 1234 and 1235.

        Since there is a replay window with #n entries to the left, an attacker may record the unlock message
        and resend it later again.

        Sender
        SSN10  UNLOCK -> (ia) 1234
        SSN11  UNLOCK -> (ia) 1235
        :
        :
        SSN12  LOCK   -> (ia) 1234
        SSN13  LOCK   -> (ia) 1235
        :
        :
        Attacker
        SSN11  UNLOCK -> (ia) 1234
          -> SSN11 is in left side of replay window from Receiver with ia 1234,
             seen from its last received valid SSN12
          -> the destination ia 1234 is not part of the msg, an attacker needs only the
             IPv6 address of device with ia 1234

      */

      if (return_status == OC_STATUS_CHANGED && id_only)
      { // c

        // no tmp elements will be allocated ...
        PRINT("only found id in request, deleting entry at index: %d", array_index);
        oc_delete_group_table_entry(array_index, GRT_STORE, g_grt, GRT_MAX_ENTRIES);
      }
      else
      {
        if (return_status == OC_STATUS_CREATED && current_grt_properties < MANDATORY_GRT_PROPERTIES)
        { // a

          // see details on constant
          PRINT("mandatory items missing, no entry created at index: %d", array_index);

          // on error: free PUB tmp entry (all heap allocations)
          oc_free_allocated_table_elements(&tmp_grt_entry, allocator);

          oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
          return;
        }

        // b + d, here all ok, set new PUB entry
        PRINT("storing RCP table at %d", array_index);

        /*
          Free - on stack allocated -  live RCP table entry elements that will be overwritten next.
          - created : 2 elements (GAs, AT)
          - changed : 1..2 elements (GAs, AT) - 0 not possible, would be to delete an RCP entry
        */
        oc_free_allocated_table_elements(&g_grt[array_index], allocator);

        // assign RCP table entry with tmp RCP (all elements)
        g_grt[array_index] = tmp_grt_entry;

        // debugging
        oc_print_group_table_entry(array_index, GRT_STORE, g_grt);

        // store (includes here an overwrite)
        oc_store_group_table_entry(array_index, GRT_STORE, g_grt);
      }

      break;
    default:
      break;
    }

    // next RCP entry
    rep = rep->next;
  }

  // create/update a RCP entry
  oc_knx_increase_fingerprint();

  // the last (positive) return status from a collection with 'n' POST elements is responded (CREATED/CHANGED)
  oc_prepare_no_format_response_no_payload(request, return_status);

  PRINT("oc_core_fp_r_post_handler - end");
}

// resource definition, details/comments see on 'core_resource_well_known_core'
extern const oc_resource_t core_resource_knx_fp_r_x;
PRAGMA_IN oc_resource_data_t core_resource_knx_fp_r_data;
const oc_resource_t core_resource_knx_fp_r = {(oc_resource_t*)&core_resource_knx_fp_r_x,
                                              {NULL, sizeof("/fp/r"), "/fp/r"},
                                              {NULL, 0, NULL},
                                              {NULL, 0, NULL},
                                              {APPLICATION_CBOR, CONTENT_NONE},
                                              OC_DISCOVERABLE,
                                              {oc_core_fp_r_get_handler, NULL, OC_ACL_P | OC_ACL_D | OC_ACL_C, OC_IF_LI},
                                              {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                              {oc_core_fp_r_post_handler, NULL, OC_ACL_C, OC_IF_C | OC_IF_B},
                                              {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                              {NULL, NULL},
                                              {NULL, NULL},
                                              0,
                                              0,
                                              1,
                                              &core_resource_knx_fp_r_data};
PRAGMA_OUT

static void oc_core_fp_r_x_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  PRINT("oc_core_fp_r_x_get_handler - start");

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  const int id = oc_uri_get_wildcard_int_value_as_int(
    oc_string(request->resource->uri),
    oc_string_len(request->resource->uri),
    request->uri_path,
    request->uri_path_len);

  const int index = oc_core_find_index_in_table_from_id(id, g_grt, GRT_MAX_ENTRIES);

  PRINT("id:%d index = %d", id, index);

  if (index == -1)
  {
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_NOT_FOUND);
    return;
  }

  if (g_grt[index].id == -1)
  {
    // index not present in RCP table
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_INTERNAL_SERVER_ERROR);
    return;
  }

  oc_rep_begin_root_object();

  // id - 0
  oc_rep_i_set_int(root, 0, g_grt[index].id);

  // ia - 12
  if (g_grt[index].ia > -1)
  {
    oc_rep_i_set_int(root, 12, g_grt[index].ia);
  }

  // grpid - 13
  if (g_grt[index].grpid > 0)
  {
    oc_rep_i_set_int(root, 13, g_grt[index].grpid);
  }

  // fid - 25
  if (g_grt[index].fid > -1)
  {
    oc_rep_i_set_int(root, 25, g_grt[index].fid);
  }

  // iid - 26
  if (g_grt[index].iid > -1)
  {
    oc_rep_i_set_int(root, 26, g_grt[index].iid);
  }

  // at - 14
  if (oc_string_len(g_grt[index].at) > 0)
  {
    oc_rep_i_set_text_string(root, 14, oc_string(g_grt[index].at));
  }

  // ga - 7
  oc_rep_i_set_int_array(root, 7, g_grt[index].ga, g_grt[index].ga_len);

  oc_rep_end_root_object();

  oc_prepare_cbor_response(request, OC_STATUS_OK);

  PRINT("oc_core_fp_r_x_get_handler - end");
}

static void oc_core_fp_r_x_del_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;
  PRINT("oc_core_fp_r_x_del_handler");


  if (oc_knx_get_lsm() != LSM_S_LOADING)
  {
    OC_ERR("not in loading state");
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  int id = oc_uri_get_wildcard_int_value_as_int(
    oc_string(request->resource->uri),
    oc_string_len(request->resource->uri),
    request->uri_path, 
    request->uri_path_len);

  int index = oc_core_find_index_in_table_from_id(id, g_grt, GRT_MAX_ENTRIES);

  if (index == -1)
  {
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_NOT_FOUND);
    return;
  }

  oc_delete_group_table_entry(index, GRT_STORE, g_grt, GRT_MAX_ENTRIES);

  // delete fp/r --> update
  oc_knx_increase_fingerprint();

  oc_prepare_no_format_response_no_payload(request, OC_STATUS_DELETED);

  PRINT("oc_core_fp_r_x_del_handler - end");
}

// resource definition, details/comments see on 'core_resource_well_known_core'
extern const oc_resource_t core_resource_knx_p;
PRAGMA_IN oc_resource_data_t core_resource_knx_fp_r_x_data;
const oc_resource_t core_resource_knx_fp_r_x = {(oc_resource_t*)&core_resource_knx_p,
                                                {NULL, sizeof("/fp/r/*"), "/fp/r/*"},
                                                {NULL, 0, NULL},
                                                {NULL, 0, NULL},
                                                {APPLICATION_CBOR, CONTENT_NONE},
                                                OC_DISCOVERABLE,
                                                {oc_core_fp_r_x_get_handler, NULL, OC_ACL_D, OC_IF_D},
                                                {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                {oc_core_fp_r_x_del_handler, NULL, OC_ACL_C, OC_IF_C},
                                                {NULL, NULL},
                                                {NULL, NULL},
                                                0,
                                                0,
                                                1,
                                                &core_resource_knx_fp_r_x_data};
PRAGMA_OUT

bool oc_core_check_recipient_index_on_group_address(int index, uint32_t group_address)
{
  if (index >= GRT_MAX_ENTRIES)
  {
    return -1;
  }
  if (group_address <= 0)
  {
    return -1;
  }
  for (int i = 0; g_grt[index].ga_len; i++)
  {
    if (g_grt[index].ga[i] == group_address)
    {
      return true;
    }
  }
  return false;
}

uint16_t oc_core_get_recipient_ia(int index)
{
  if (index >= GRT_MAX_ENTRIES)
  {
    return 0;
  }

  return (uint16_t)g_grt[index].ia;
}

// -utilities -

void oc_cflags_as_string(char* buffer, oc_cflag_mask_t cflags)
{

  if (cflags & OC_CFLAG_READ)
  {
    strcat(buffer, "r");
  }
  else
  {
    strcat(buffer, ".");
  }
  if (cflags & OC_CFLAG_WRITE)
  {
    strcat(buffer, "w");
  }
  else
  {
    strcat(buffer, ".");
  }
  if (cflags & OC_CFLAG_INIT)
  {
    strcat(buffer, "i");
  }
  else
  {
    strcat(buffer, ".");
  }
  if (cflags & OC_CFLAG_TRANSMISSION)
  {
    strcat(buffer, "t");
  }
  else
  {
    strcat(buffer, ".");
  }
  if (cflags & OC_CFLAG_UPDATE)
  {
    strcat(buffer, "u");
  }
  else
  {
    strcat(buffer, ".");
  }
}

void oc_print_cflags(const oc_cflag_mask_t cflags)
{
#ifdef OC_PRINT

  if (cflags & OC_CFLAG_READ)
  {
    PRINTF("r");
  }
  if (cflags & OC_CFLAG_WRITE)
  {
    PRINTF("w");
  }
  if (cflags & OC_CFLAG_INIT)
  {
    PRINTF("i");
  }
  if (cflags & OC_CFLAG_TRANSMISSION)
  {
    PRINTF("t");
  }
  if (cflags & OC_CFLAG_UPDATE)
  {
    PRINTF("u");
  }

#endif
}

void oc_print_group_object_table_entry(int entry)
{
#ifdef OC_PRINT

  if (g_got[entry].id == -1)
  {
    return;
  }

  PRINT("id (0)     : %d", g_got[entry].id);
  PRINT("href (11)  : %s", oc_string_checked(g_got[entry].href));
  PRINT("cflags (8) : %d string: ", g_got[entry].cflags);
  oc_print_cflags(g_got[entry].cflags);
  PRINT("ga (7)     : [");
  for (int i = 0; i < g_got[entry].ga_len; i++)
  {
    PRINTF("%u", g_got[entry].ga[i]);
  }
  PRINTF("]");

#endif
}

void oc_store_group_object_table_entry(int entry)
{
#ifndef OC_USE_STORAGE
  (void)entry;
  PRINT("no storage for the GO table enabled");
#else

  char filename[FPT_SIZE];
  (void)snprintf(filename, FPT_SIZE, "%s_%d", GOT_STORE, entry);

  uint8_t* buf = malloc(OC_MAX_APP_DATA_SIZE);
  if (!buf)
    return;

  oc_rep_new(buf, OC_MAX_APP_DATA_SIZE);

  // write the data in CBOR format
  oc_rep_begin_root_object();

  // id - 0
  oc_rep_i_set_int(root, 0, g_got[entry].id);
  // href - 11
  oc_rep_i_set_text_string(root, 11, oc_string(g_got[entry].href));
  // ga - 7
  oc_rep_i_set_int_array(root, 7, g_got[entry].ga, g_got[entry].ga_len);
  // cflags - 8 (note this is a binary value and different as the response on the wire with r/w/u/t/i)
  oc_rep_i_set_int(root, 8, g_got[entry].cflags);

  oc_rep_end_root_object();

  const int size = oc_rep_get_encoded_payload_size();
  if (size > 0)
  {
    OC_DBG("oc_dump_GO_table_entry: [%s] [%d]: size %d", filename, entry, size);
    long written_size = oc_storage_write(filename, buf, size);
    if (written_size != (long)size)
    {
      PRINT("written %d != %d (to be written)", (int)written_size, size);
    }
  }
  free(buf);
#endif
}

void oc_load_group_object_table_entry(int entry)
{
  char filename[FPT_SIZE];
  (void)snprintf(filename, FPT_SIZE, "%s_%d", GOT_STORE, entry);

  oc_rep_t* rep;

  uint8_t* buf = malloc(OC_MAX_APP_DATA_SIZE);
  if (!buf)
    return;

  const long bytes_to_read = oc_storage_read(filename, buf, OC_MAX_APP_DATA_SIZE);
  PRINTF(" ... bytes: %ld", bytes_to_read < 0 ? 0 : bytes_to_read);
  if (bytes_to_read > 0)
  {
    struct oc_memb rep_objects = {sizeof(oc_rep_t), 0, 0, 0, 0};
    oc_rep_set_pool(&rep_objects);

    int err = oc_parse_rep(buf, bytes_to_read, &rep);
    oc_rep_t* head = rep;
    if (err == 0)
    {
      while (rep != NULL)
      {
        switch (rep->type)
        {

        case OC_REP_INT:

          // id (0)
          if (rep->iname == 0)
          {
            g_got[entry].id = (int)rep->value.integer;
          }

          // cflags (8)
          if (rep->iname == 8)
          {
            g_got[entry].cflags = (int)rep->value.integer;
          }

          break;
        case OC_REP_STRING:

          // href (11)
          if (rep->iname == 11)
          {
            oc_free_string(&g_got[entry].href);
            oc_new_string(&g_got[entry].href, oc_string(rep->value.string), oc_string_len(rep->value.string));
          }

          break;
        case OC_REP_INT_ARRAY:

          // ga array (7)
          if (rep->iname == 7)
          {
            // temp ptr to address the CBOR array
            const int64_t* array = oc_int_array(rep->value.array);
            const int new_array_size = oc_int_array_size(rep->value.array);

            // malloc of 'zero' byte return pointer is undefined
            uint32_t* new_array = malloc(new_array_size * sizeof(uint32_t));
            if (new_array && new_array_size > 0)
            {
              for (int i = 0; i < new_array_size; i++)
              {
                new_array[i] = (uint32_t)array[i];
              }

              // release a possible ga array, it will be overwritten,
              // no selective adding (note it releases the org ptr)
              // free ignores NULL ptr
              free(g_got[entry].ga);

              PRINT("ga size %d", new_array_size);

              // assign only when the new array is allocated correctly
              g_got[entry].ga_len = new_array_size;
              g_got[entry].ga = new_array;
            }
          }

          break;
        default:
          // any other invalid type prints ...
          // note that an empty ga array (7: [] = EITT test) is coded in current CBOR with "OC_REP_NIL"
          PRINT("invalid object type detected");
          break;
        }
        rep = rep->next;
      }
    }
    oc_free_rep(head);
  }
  free(buf);
}

void oc_load_group_object_table(void)
{
  PRINT("Loading Group Object Table from persistent storage");
  for (int i = 0; i < GOT_MAX_ENTRIES; i++)
  {
    oc_load_group_object_table_entry(i);
    oc_print_group_object_table_entry(i);
  }
}

void oc_free_group_object_table_entry(int entry, bool init)
{
  g_got[entry].id = -1;

  // free "string" data memory only if already initialized
  // assumes in table uninitialized/random string data - don't release it ...
  if (init == false)
  {
    oc_free_string(&g_got[entry].href);
    free(g_got[entry].ga); // NULL ptr is handled
  }

  // calling with init = true AND allocated GA array keeps memory leak open ...
  // so careful on use of init flag ...
  g_got[entry].ga = NULL;
  g_got[entry].ga_len = 0;
  g_got[entry].cflags = 0;
}

void oc_free_allocated_go_table_elements(oc_group_object_table_t* entry, uint8_t allocator)
{
  if (allocator & GO_HREF)
  {
    oc_free_string(&entry->href);
  }

  if (allocator & GO_GAS)
  {
    free(entry->ga);
    entry->ga_len = 0;
    entry->ga = NULL;
  }
}

void oc_free_allocated_table_elements(oc_group_table_t* entry, uint8_t allocator)
{
  if (allocator & TABLE_ATREF)
  {
    oc_free_string(&entry->at);
  }

  if (allocator & TABLE_GAS)
  {
    free(entry->ga);
    entry->ga_len = 0;
    entry->ga = NULL;
  }
}

int oc_delete_group_object_table_entry(int entry)
{
  if (entry < 0 || entry >= GOT_MAX_ENTRIES)
    return -1;

  // delete GO entry (note, one file per entry)
  char filename[FPT_SIZE];
  (void)snprintf(filename, FPT_SIZE, "%s_%d", GOT_STORE, entry);
  oc_storage_erase(filename);

  oc_free_group_object_table_entry(entry, false);
  return 0;
}

void oc_delete_group_object_table(void)
{
  PRINT("Deleting Group Object Table from Persistent storage");
  for (int i = 0; i < GOT_MAX_ENTRIES; i++)
  {
    oc_delete_group_object_table_entry(i);
    oc_print_group_object_table_entry(i);
  }
}

static void oc_free_group_object_table(void)
{
  PRINT("Free GO Table");
  for (int i = 0; i < GOT_MAX_ENTRIES; i++)
  {
    oc_free_group_object_table_entry(i, false);
  }
}

int oc_core_find_index_in_table_from_id(int id, oc_group_table_t* table, int max_size)
{
  for (int i = 0; i < max_size; i++)
  {
    if (table[i].id == id)
    {
      return i;
    }
  }
  return -1;
}

static void oc_print_group_table_entry(int entry, char* store, oc_group_table_t* table)
{
#ifdef OC_PRINT

  if (table[entry].id == -1)
  {
    return;
  }
  PRINT("%s [%d] --> [%d]", store, entry, table[entry].ga_len);
  PRINT("id (0)     : %d", table[entry].id);
  PRINT("ia (12)    : %d", table[entry].ia);
  PRINT("iid (26)   : %" PRIi64 "", table[entry].iid);
  PRINT("fid (25)   : %" PRIi64 "", table[entry].fid);
  PRINT("grpid (13) : %u", table[entry].grpid);

  if (oc_string_len(table[entry].at) > 0)
  {
    PRINT("at (14) : %s", oc_string_checked(table[entry].at));
  }
  PRINT("ga (7)     : [");
  for (int i = 0; i < table[entry].ga_len; i++)
  {
    PRINTF("%u", table[entry].ga[i]);
  }
  PRINTF("]");

#endif
}

// store RCP/PUB table data in CBOR (hex stream data)
static void oc_store_group_table_entry(int entry, char* store, const oc_group_table_t* table)
{
#ifndef OC_USE_STORAGE
  (void)entry;
  PRINT("no storage for the RCP/PUB table enabled");
#else

  char filename[FPT_SIZE];
  (void)snprintf(filename, FPT_SIZE, "%s_%d", store, entry);

  uint8_t* buf = malloc(OC_MAX_APP_DATA_SIZE);
  if (!buf)
    return;

  oc_rep_new(buf, OC_MAX_APP_DATA_SIZE);

  // write the data in CBOR format
  oc_rep_begin_root_object();

  // id - 0
  oc_rep_i_set_int(root, 0, table[entry].id);
  // ia - 12
  oc_rep_i_set_int(root, 12, table[entry].ia);
  // iid - 26
  oc_rep_i_set_int(root, 26, table[entry].iid);
  // fid - 25
  oc_rep_i_set_int(root, 25, table[entry].fid);
  // grpid - 13
  oc_rep_i_set_int(root, 13, table[entry].grpid);
  // at - 14
  oc_rep_i_set_text_string(root, 14, oc_string(table[entry].at));
  // ga - 7
  oc_rep_i_set_int_array(root, 7, table[entry].ga, table[entry].ga_len);
  // non - 'non'
  oc_rep_text_set_text_string(root, non, oc_string(table[entry].at));

  oc_rep_end_root_object();

  const int size = oc_rep_get_encoded_payload_size();
  if (size > 0)
  {
    OC_DBG("oc_dump_PUB/RCV_table_entry: [%s] [%s] [%d]: size %d", filename, store, entry, size);
    long written_size = oc_storage_write(filename, buf, size);
    if (written_size != (long)size)
    {
      PRINT("written %d != %d (to be written)", (int)written_size, size);
    }
  }

  free(buf);
#endif
}

static void oc_load_group_table_entry(int entry, char* store, oc_group_table_t* table)
{
  char filename[FPT_SIZE];
  (void)snprintf(filename, FPT_SIZE, "%s_%d", store, entry);

  oc_rep_t* rep;

  uint8_t* buf = malloc(OC_MAX_APP_DATA_SIZE);
  if (!buf)
  {
    return;
  }

  const long bytes_to_read = oc_storage_read(filename, buf, OC_MAX_APP_DATA_SIZE);
  PRINTF(" ... bytes: %ld", bytes_to_read < 0 ? 0 : bytes_to_read);
  if (bytes_to_read > 0)
  {
    struct oc_memb rep_objects = {sizeof(oc_rep_t), 0, 0, 0, 0};
    oc_rep_set_pool(&rep_objects);

    const int err = oc_parse_rep(buf, bytes_to_read, &rep);
    oc_rep_t* head = rep;

    if (err == 0)
    {
      while (rep)
      {
        switch (rep->type)
        {
        case OC_REP_INT:
          if (rep->iname == 0)
          {
            table[entry].id = (int32_t)rep->value.integer;
          }
          if (rep->iname == 12)
          {
            table[entry].ia = (uint32_t)rep->value.integer;
          }
          if (rep->iname == 13)
          {
            table[entry].grpid = (uint32_t)rep->value.integer;
          }
          if (rep->iname == 25)
          {
            table[entry].fid = rep->value.integer;
          }
          if (rep->iname == 26)
          {
            table[entry].iid = rep->value.integer;
          }
          break;
        case OC_REP_STRING:

          // at (14)
          if (rep->iname == 14)
          {
            oc_free_string(&table[entry].at);
            oc_new_string(&table[entry].at, oc_string(rep->value.string), oc_string_len(rep->value.string));
          }
          break;
        case OC_REP_INT_ARRAY:

          // ga array (7)
          if (rep->iname == 7)
          {
            // temp ptr to address the CBOR array
            const int64_t* array = oc_int_array(rep->value.array);
            const int new_array_size = (int)oc_int_array_size(rep->value.array);

            // malloc of 'zero' byte return pointer is undefined
            uint32_t* new_array = malloc(new_array_size * sizeof(uint32_t));
            if (new_array && new_array_size > 0)
            {
              for (int i = 0; i < new_array_size; i++)
              {
                new_array[i] = (uint32_t)array[i];
              }

              // release a possible ga array, it will be overwritten,
              // no selective adding (note it releases the org ptr)
              // free ignores NULL ptr
              free(table[entry].ga);

              PRINT("ga size %d", new_array_size);

              // assign only when the new array is allocated correctly
              table[entry].ga_len = new_array_size;
              table[entry].ga = new_array;
            }
          }
          break;
        default:
          // any other invalid type prints ...
          // note that an empty ga array (7: [] = EITT test) is coded in current CBOR with "OC_REP_NIL"
          PRINT("invalid object type detected");
          break;
        }
        rep = rep->next;
      }
    }
    oc_free_rep(head);
  }
  free(buf);
}

static void oc_load_object_table(void)
{
  PRINT("Loading Group Recipient Table from persistent storage");
  for (int i = 0; i < GRT_MAX_ENTRIES; i++)
  {
    oc_load_group_table_entry(i, GRT_STORE, g_grt);
    oc_print_group_table_entry(i, GRT_STORE, g_grt);
  }

#ifdef OC_PUBLISHER_TABLE
  PRINT("Loading Group Publisher Table from persistent storage");
  for (int i = 0; i < GPT_MAX_ENTRIES; i++)
  {
    oc_load_group_table_entry(i, GPT_STORE, g_gpt);
    oc_print_group_table_entry(i, GPT_STORE, g_gpt);
  }
#endif
}

static void oc_free_group_table_entry(const int entry, oc_group_table_t* table, const bool init)
{
  table[entry].id = -1; // init value, used also in code to check on its validity
  table[entry].ia = -1; // init value, used also in code to check on its validity
  table[entry].iid = -1; // init value, used also in code to check on its validity
  table[entry].fid = -1; // init value, used also in code to check on its validity
  table[entry].grpid = 0; // init value, used also in code to check on its validity

  // free "string" data memory only if already initialized
  // assumes in table uninitialized/random string data - don't release it ...
  if (init == false)
  {
    oc_free_string(&table[entry].at);
    free(table[entry].ga);
  }

  // calling with init = true AND allocated GA array keeps memory leak open ...
  // so careful on use of init flag ...
  table[entry].ga = NULL;
  table[entry].ga_len = 0;
}

/**
 * @brief delete entry of the Group Table,
 * - the GO table entry in RAM is invalidated
 * - the GO table entry on storage disappears
 *
 * @param entry the index of the entry in the Group Table
 * @param store store name (PUB/RCP table)
 * @param table PUB/RCP table pointer
 * @param max_size the size of the table
 */
static int oc_delete_group_table_entry(int entry, char* store, oc_group_table_t* table, int max_size)
{
  // use either GPT or GRT table size
  if (entry < 0 || entry >= max_size)
    return -1;

  // delete GPT/GRT entry (note, one file per entry)
  char filename[FPT_SIZE];
  (void)snprintf(filename, 20, "%s_%d", store, entry);
  oc_storage_erase(filename);

  oc_free_group_table_entry(entry, table, false);
  return 0;
}

void oc_delete_group_tables(void)
{
  PRINT("Deleting Recipient Table from RAM and storage (file system)");
  for (int i = 0; i < GRT_MAX_ENTRIES; i++)
  {
    oc_delete_group_table_entry(i, GRT_STORE, g_grt, GRT_MAX_ENTRIES);
    oc_print_group_table_entry(i, GRT_STORE, g_grt);
  }

#ifdef OC_PUBLISHER_TABLE
  PRINT("Deleting Publisher Table from RAM and storage (file system)");
  for (int i = 0; i < GPT_MAX_ENTRIES; i++)
  {
    oc_delete_group_table_entry(i, GPT_STORE, g_gpt, GPT_MAX_ENTRIES);
    oc_print_group_table_entry(i, GPT_STORE, g_gpt);
  }
#endif
}

static void oc_free_group_tables(void)
{
  PRINT("Free RCP table from RAM");
  for (int i = 0; i < GRT_MAX_ENTRIES; i++)
  {
    oc_free_group_table_entry(i, g_grt, false);
  }

#ifdef OC_PUBLISHER_TABLE
  PRINT("Free PUB table from RAM");
  for (int i = 0; i < GPT_MAX_ENTRIES; i++)
  {
    oc_free_group_table_entry(i, g_gpt, false);
  }
#endif
}

int find_empty_slot_in_table(int id, oc_group_table_t* table, int max_size)
{
  if (id < 0)
  {
    // shall be 0...65535
    return -1;
  }

  for (int i = 0; i < max_size; i++)
  {
    if (table[i].id == -1)
    { // empty slot
      return i;
    }
  }
  return -1;
}

int oc_core_get_recipient_table_size(void) { return GRT_MAX_ENTRIES; }

oc_group_table_t* oc_core_get_recipient_table_entry(int index)
{
  return index < 0 ? NULL : index >= GRT_MAX_ENTRIES ? NULL : &g_grt[index];
}

int oc_core_get_publisher_table_size(void)
{
#ifdef OC_PUBLISHER_TABLE
  return GPT_MAX_ENTRIES;
#else
  return 0;
#endif
}

oc_group_table_t* oc_core_get_publisher_table_entry(int index)
{

#ifdef OC_PUBLISHER_TABLE
  return index < 0 ? NULL : index >= GPT_MAX_ENTRIES ? NULL : &g_gpt[index];
#else
  return NULL;
#endif
}

int oc_core_find_index_in_recipient_table_from_id(int id)
{
  return oc_core_find_index_in_table_from_id(id, g_grt, GRT_MAX_ENTRIES);
}

static void oc_init_tables(void)
{
#ifdef OC_PUBLISHER_TABLE

  for (int i = 0; i < GPT_MAX_ENTRIES; i++)
  {
    // init GPT table, assumes in PUB table uninitialized/random string data - don't release it ...
    oc_free_group_table_entry(i, g_gpt, true);
  }
#endif

  for (int i = 0; i < GRT_MAX_ENTRIES; i++)
  {
    // init GRT table,assumes in RCP table uninitialized/random string data - don't release it ...
    oc_free_group_table_entry(i, g_grt, true);
  }

  for (int i = 0; i < GOT_MAX_ENTRIES; i++)
  {
    // init GO table, assumes in GO table uninitialized/random string data - don't release it ...
    oc_free_group_object_table_entry(i, true);
  }
}

void oc_create_knx_fp_resources(void)
{
  OC_DBG("oc_create_knx_fp_resources");

  oc_init_tables();
  oc_load_group_object_table();
  oc_load_object_table();
}

void oc_free_knx_table_resources(void)
{
  oc_free_group_tables();
  oc_free_group_object_table();
}

static bool is_in_array(uint32_t value, uint32_t* array, int array_size)
{
  if (array_size <= 0)
  {
    return false;
  }
  if (array == NULL)
  {
    return false;
  }
  for (int i = 0; i < array_size; i++)
  {
    if (array[i] == value)
    {
      return true;
    }
  }
  return false;
}

bool oc_add_points_from_group_object_table_to_response(oc_request_t* request, uint32_t group_address, size_t* response_length)
{
  bool return_value = false;

  PRINT("oc_add_points_from_group_object_table_to_response %u", group_address);

  for (int index = 0; index < GOT_MAX_ENTRIES; index++)
  {
    if (g_got[index].id > -1)
    {
      if (is_in_array(group_address, g_got[index].ga, g_got[index].ga_len))
      {
        // add the resource to response, note, it is not checked if the resource is already there...
        PRINT("oc_add_points_from_group_object_table_to_response [%d] %s", index, oc_string_checked(g_got[index].href));

        // called from GET /p handler so always truncate resources URN's
        oc_add_resource_to_response_payload(
          oc_ri_get_app_resource_by_resource_path(oc_string_checked(g_got[index].href), oc_string_len(g_got[index].href)),
          response_length, true);
        return_value = true;
      }
    }
  }
  return return_value;
}

oc_endpoint_t oc_create_multicast_group_address_with_port(oc_endpoint_t in, uint32_t group_nr, uint64_t iid, int scope, uint16_t port)
{
  // create the multicast address from group and scope
  // FF3_:FD__:____:____:(8-f)___:____
  // FF35:30:<ULA-routing-prefix>::<group id>
  //    | 5 == scope
  //    | 3 == scope
  // Multicast prefix: FF35:0030:          [4 bytes]
  // ULA routing prefix: FD11:2222:33a3::  [6 bytes + 2 empty bytes]
  // Group Identifier: 8000 : 0068         [4 bytes ]

  // group number to the various bytes
  uint8_t byte_1 = (uint8_t)group_nr;
  uint8_t byte_2 = (uint8_t)(group_nr >> 8);
  uint8_t byte_3 = (uint8_t)(group_nr >> 16);
  uint8_t byte_4 = (uint8_t)(group_nr >> 24);

  // iid as  ula prefix to various bytes
  uint8_t ula_1 = (uint8_t)iid;
  uint8_t ula_2 = (uint8_t)(iid >> 8);
  uint8_t ula_3 = (uint8_t)(iid >> 16);
  uint8_t ula_4 = (uint8_t)(iid >> 24);
  uint8_t ula_5 = (uint8_t)(iid >> 32);

  // flags
  int my_transport_flags = IPV6 + MULTICAST;

#ifdef OC_OSCORE
  my_transport_flags |= OSCORE;
#endif

  oc_make_ipv6_endpoint(group_mcast, my_transport_flags, 
                        port, 0xff, 0x30 + scope, 0, 0x30,        // FF35::30:
                        0xfd, ula_5, ula_4, ula_3, ula_2, ula_1,  // FD11 : 2222 : 3333
                        0, 0,                                     // ::
                        byte_4, byte_3, byte_2, byte_1);          // group id

  PRINT("oc_create_multicast_group_address_with_port S=%d iid=%" PRIu64 " G=%u B4=%d B3=%d B2=%d B1=%d :",
        scope, iid, group_nr, byte_4, byte_3, byte_2, byte_1);
  PRINTipaddr(group_mcast);

  group_mcast.group_address = group_nr;

  // copy all from local data to (return) pointer
  memcpy(&in, &group_mcast, sizeof(oc_endpoint_t));

  return in;
}

void subscribe_group_to_multicast_with_port(uint32_t group_nr, uint64_t iid, int scope, uint16_t port)
{
  // create the multicast address from group and scope and port
  oc_endpoint_t group_mcast = {0};

  group_mcast = oc_create_multicast_group_address_with_port(group_mcast, group_nr, iid, scope, port);

  // subscribe
  oc_connectivity_subscribe_mcast_ipv6(&group_mcast);
}

void subscribe_group_to_multicast(uint32_t group_nr, uint64_t iid, int scope)
{
  // FF35::30: <ULA-routing-prefix>::<group id>
  //
  // create the multicast address from group and scope
  oc_endpoint_t group_mcast = {0};

  group_mcast = oc_create_multicast_group_address_with_port(group_mcast, group_nr, iid, scope, COAP_DEFAULT_PORT);

  // subscribe
  oc_connectivity_subscribe_mcast_ipv6(&group_mcast);
}

void unsubscribe_group_to_multicast_with_port(uint32_t group_nr, uint64_t iid, int scope, uint16_t port)
{
  // create the multicast address from group and scope
  oc_endpoint_t group_mcast = {0};

  group_mcast = oc_create_multicast_group_address_with_port(group_mcast, group_nr, iid, scope, port);

  // un subscribe
  oc_connectivity_unsubscribe_mcast_ipv6(&group_mcast);
}

void unsubscribe_group_to_multicast(uint32_t group_nr, uint64_t iid, int scope)
{
  // FF35::30: <ULA-routing-prefix>::<group id>
  //
  // create the multi cast address from group and scope
  oc_endpoint_t group_mcast;
  memset(&group_mcast, 0, sizeof(group_mcast));

  group_mcast = oc_create_multicast_group_address_with_port(group_mcast, group_nr, iid, scope, COAP_DEFAULT_PORT);

  // un subscribe
  oc_connectivity_unsubscribe_mcast_ipv6(&group_mcast);
}

uint32_t oc_find_grpid_in_table(oc_group_table_t* table, int max_size, const uint32_t group_address)
{
  for (int index = 0; index < max_size; index++)
  {
    if (is_in_array(group_address, table[index].ga, table[index].ga_len))
    {
      // break immediately
      return table[index].grpid;
    }
  }
  // not found
  return 0;
}

uint32_t oc_find_grpid_in_recipient_table(const uint32_t group_address)
{
  return oc_find_grpid_in_table(g_grt, GRT_MAX_ENTRIES, group_address);
}

void oc_register_group_multicasts(void)
{
  #ifdef OC_PUBLISHER_TABLE

  // register only if publisher is active, installation id will be used as ULA prefix
  const oc_device_info_t* const  device = oc_core_get_device_info();

  PRINT("multicast port %i", COAP_DEFAULT_PORT);

  // register via grpid
  for (int index = 0; index < GPT_MAX_ENTRIES; index++)
  {
    const oc_cflag_mask_t cflags = g_got[index].cflags;

    // check if the GA is used for any receiving action, e.g. one of r/w/u
    if (cflags & OC_CFLAG_WRITE + OC_CFLAG_UPDATE + OC_CFLAG_READ)
    {
      for (int i = 0; i < g_got[index].ga_len; i++)
      {
        // check if the 'receiving' GA from GO table entry is in the publisher table (device wants to receive it)
        const uint32_t grpid = oc_find_grpid_in_table(g_gpt, GPT_MAX_ENTRIES, g_got[index].ga[i]);

        PRINT("oc_register_group_multicasts index=%d i=%d grpid: %u group_address: %u cflags=", index, i, grpid, g_got[index].ga[i]);
        oc_print_cflags(cflags);

        if (grpid > 0)
        { // found
          subscribe_group_to_multicast_with_port(grpid, device->iid, 2, COAP_DEFAULT_PORT);
          subscribe_group_to_multicast_with_port(grpid, device->iid, 5, COAP_DEFAULT_PORT);
        }
      }
    }
  }

  #endif
}

void oc_init_datapoints_at_initialization(void)
{
  PRINT("scan datapoints for possible i-cflag initialization ...");

  for (int i = 0; i < GOT_MAX_ENTRIES; i++)
  {
    if (g_got[i].id > -1)
    {
      // at least one GA is assigned (is an array)
      if (g_got[i].ga_len > 0)
      {
        if (g_got[i].cflags & OC_CFLAG_INIT)
        {
          // read on init cflags is set, fire (after device restart)
          // no check on -1, there MUST be at least one sending GA
          const uint32_t sending_group_address = oc_core_find_sending_ga_in_pos_zero_for_href(oc_string(g_got[i].href), NULL);

          OC_INF("init datapoint, index: %d issue read on group address %u", i, sending_group_address);

          const oc_device_info_t* const  device = oc_core_get_device_info();
          uint16_t sia_value = device->ia;
          uint64_t iid = device->iid;

          OC_INF("oc_do_s_mode_read : ga=%u ia=%d, iid=%" PRIu64 "", sending_group_address, sia_value, iid);

          // find the (mc) grpid that belongs to the group address
          const uint32_t grpid = oc_find_grpid_in_recipient_table(sending_group_address);
          if (grpid > 0)
          { // grpid is set in case of multicast in RCP table (configured by MaC)

          #ifdef OC_USE_MULTICAST_SCOPE_2
            oc_issue_s_mode_mc(2, sia_value, grpid, sending_group_address, iid, "r", 0, 0);
          #endif
            oc_issue_s_mode_mc(5, sia_value, grpid, sending_group_address, iid, "r", 0, 0);
          }
          else
          {
            // TODO resolve IP unicast to send via unicast...
            // discover unicast IPv6 for IA via mDNS
            // send message with unicast IPv6
            PRINT("grpid =0");
          }
        }
      }
    }
  }
}
