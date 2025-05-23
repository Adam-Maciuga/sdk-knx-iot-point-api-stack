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
#define FPT_SIZE (sizeof(GPT_STORE) + 6)    // support of '_99999' pub/rcp/go FILE entries

// note static variables are initialized with '0' first time
static oc_group_object_table_t g_got[GOT_MAX_ENTRIES];  // go table
static oc_group_table_t g_grt[GRT_MAX_ENTRIES];         // rcp table (to send)

#ifdef OC_PUBLISHER_TABLE
static oc_group_table_t g_gpt[GPT_MAX_ENTRIES];         // pub table (to receive)
#endif

// -externals -

static void oc_print_reduced_group_table_entry(int entry, char* store, oc_group_table_t* table, int max_size);

static void oc_print_group_table_entry(int entry, char* store, oc_group_table_t* table);

static void oc_store_group_table_entry(int entry, char* store, const oc_group_table_t* table);

static int oc_delete_group_table_entry(int entry, char* store, oc_group_table_t* table, int max_size);

static int oc_core_find_index_in_table_from_id(int id, oc_group_table_t* table, int max_size);

int find_empty_slot_in_table(int id, oc_group_table_t* table, int max_size);

static int oc_core_add_entry(int index, oc_group_table_t* table, int table_size, oc_group_table_t entry);

static uint32_t oc_find_grpid_in_table(oc_group_table_t* table, int max_size, uint32_t group_address);

// ------------

int oc_print_reduced_group_publisher_table(void)
{
#ifdef OC_PUBLISHER_TABLE
  for (int i = 0; i < GPT_MAX_ENTRIES; i++)
  {
    oc_print_reduced_group_table_entry(i, GPT_STORE, g_gpt, GPT_MAX_ENTRIES);
  }
#endif
  return 0;
}

int oc_print_reduced_group_recipient_table(void)
{
  for (int i = 0; i < GRT_MAX_ENTRIES; i++)
  {
    oc_print_reduced_group_table_entry(i, GRT_STORE, g_grt, GRT_MAX_ENTRIES);
  }
  return 0;
}

int oc_table_find_id_from_rep(const oc_rep_t* object)
{
  // use copy to keep original parameter
  const oc_rep_t* tmpObject = object;

  while (tmpObject != NULL)
  {
    switch (tmpObject->type)
    {
    case OC_REP_INT:
    {
      // CBOR key(iname) for 'id' is '0'
      if (oc_string_len(tmpObject->name) == 0 && tmpObject->iname == 0)
      {
        const int id = (int)tmpObject->value.integer;
        PRINT("find id from request: %d ", id);
        return id;
      }
    }
    break;
    default:
      break;
    }
    tmpObject = tmpObject->next;
  }

  PRINT("oc_table_find_id_from_rep id=-1 (error)");
  return -1;
}

int find_empty_slot_in_group_object_table(const int id)
{
  if (id < 0)
  {
    // should be a positive number
    return -1;
  }

  for (int i = 0; i < GOT_MAX_ENTRIES; i++)
  {
    if (g_got[i].id == -1)
    { // empty slot
      return i;
    }
  }
  return -1;
}

int oc_core_set_group_object_table(int index, oc_group_object_table_t entry)
{
  if (index >= GOT_MAX_ENTRIES)
  {
    OC_ERR("index too large index:%d %d", index, GOT_MAX_ENTRIES);
  }
  g_got[index].cflags = entry.cflags;
  g_got[index].id = entry.id;

  oc_free_string(&g_got[index].href);
  oc_new_string(&g_got[index].href, oc_string(entry.href), oc_string_len(entry.href));
  /* copy the ga array */
  g_got[index].ga_len = 0;
  uint32_t* new_array = (uint32_t*)malloc(entry.ga_len * sizeof(uint32_t));

  if (new_array != NULL && entry.id > -1)
  {
    for (int i = 0; i < entry.ga_len; i++)
    {
#pragma warning(suppress : 6386)
      new_array[i] = entry.ga[i];
    }
    if (g_got[index].ga != 0)
    {
      free(g_got[index].ga);
    }
    g_got[index].ga_len = entry.ga_len;
    g_got[index].ga = new_array;
  }
  return 0;
}

int oc_core_get_group_object_table_total_size(void) { return GOT_MAX_ENTRIES; }

oc_group_object_table_t* oc_core_get_group_object_table_entry(int index)
{
  if (index < 0)
  {
    return NULL;
  }
  if (index >= GOT_MAX_ENTRIES)
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

int oc_core_find_first_group_object_table_index(uint32_t group_address)
{
  return oc_core_find_next_group_object_table_index(group_address, -1);
}

int oc_core_find_next_group_object_table_index(uint32_t group_address, int cur_index)
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

oc_string_t oc_core_get_href_from_group_object_table_index(int index)
{
  const oc_string_t error = {0};
  return index < GOT_MAX_ENTRIES ? g_got[index].href : error;
}

oc_cflag_mask_t oc_core_group_object_table_cflag_entries(int index)
{
  if (index < GOT_MAX_ENTRIES)
  {
    return g_got[index].cflags;
  }
  return 0;
}

int oc_core_find_group_object_table_number_group_entries(int index)
{
  if (index < GOT_MAX_ENTRIES)
  {
    return g_got[index].ga_len;
  }
  return 0;
}

int oc_core_find_group_object_table_group_entry(int index, int entry)
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

int oc_core_find_group_object_table_url(const char* url)
{
  for (int i = 0; i < GOT_MAX_ENTRIES; i++)
  {
    if (strlen(url) == oc_string_len(g_got[i].href) && strcmp(url, oc_string(g_got[i].href)) == 0)
    { // href len and content matches
      return i;
    }
  }
  return -1;
}

int oc_core_find_next_group_object_table_url(const char* url, const int cur_index)
{
  if (cur_index == -1)
  { // don't iterate if already no index available
    return -1;
  }

  for (int i = cur_index + 1; i < GOT_MAX_ENTRIES; i++)
  {
    if (strlen(url) == oc_string_len(g_got[i].href) && strcmp(url, oc_string(g_got[i].href)) == 0)
    {
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

bool oc_belongs_href_to_resource(oc_string_t href, bool discoverable, size_t device_index)
{
  for (const oc_resource_t* resource = oc_ri_get_app_resources(); resource; resource = resource->next)
  {
    if (discoverable)
    {
      if (resource->device != device_index || !(resource->properties & OC_DISCOVERABLE))
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

  int query_parameter_kvpair_matches =
    0; // how many (to this device applicable) query parameter key/value pair matches where found
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

  const size_t device_index = request->resource->device;
  if (oc_a_lsm_state(device_index) != LSM_S_LOADING)
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
  const oc_rep_t* object = NULL;

  // no payload -> 4.00
  while (rep != NULL)
  {
    switch (rep->type)
    {
    // a possible collection of GOs
    case OC_REP_OBJECT:

      // treat request payload value as one GO (that itself defines a chain of objects for id, href,...)
      object = rep->value.object;

      // find 'id' key in request
      const int id = oc_table_find_id_from_rep(object);
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
        // index already in use, so it will be changed
        return_status = OC_STATUS_CHANGED;
      }
      else
      {
        // no index, so we will create one
        return_status = OC_STATUS_CREATED;

        // returns a valid index if not -1
        array_index = find_empty_slot_in_group_object_table(id);
        if (array_index == -1)
        {
          OC_ERR("GO table has no empty slot to add a new entry");
          oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
          return;
        }
      }

      #define GO_HREF (1 << 0)
      #define GO_GAS (1 << 1)

      uint8_t allocator = 0; // identify which "stack" memory resource are allocated during the post
      oc_group_object_table_t tmp_got_entry = g_got[array_index]; // fill with live GO (from a present entry or from an empty entry)
      bool id_only = true; // used to delete the GO table entry
      int mandatory_items = 0; // needs to be 4 for creating new entry, i.e. id, ga, cflags & href

      while (object != NULL)
      {
        switch (object->type)
        {
        
        case OC_REP_INT:

          // id (0)
          if (object->iname == 0)
          {
            // set id in tmp copy 
            tmp_got_entry.id = (int)object->value.integer;

            // valid item + possibly an 'id only case'
            mandatory_items++;
          }

          // cflags (8)
          if (object->iname == 8)
          {
            // set flags in tmp copy
            tmp_got_entry.cflags = (int)object->value.integer;

            // valid item + for sure no 'id only case'
            id_only = false;
            mandatory_items++;
          }

          break;
        case OC_REP_STRING:

          // any extra element - even if not valid - causes a "not an id only"
          id_only = false;

          // href (11)
          if (object->iname == 11)
          {
            // set (new) href in tmp copy (org ptr still valid)
            oc_new_string(&tmp_got_entry.href, oc_string(object->value.string), oc_string_len(object->value.string));

            mandatory_items++;
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

              // assign GA array in tmp copy (org ptr still valid)
              tmp_got_entry.ga_len = array_size;
              tmp_got_entry.ga = new_array;

              mandatory_items++;
              allocator |= GO_GAS;
            }
            else
            {
              OC_ERR("out of stack memory");

              // on error: free GO tmp entry (all heap allocations)
              oc_free_allocated_go_elements(&tmp_got_entry, allocator);

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
          oc_free_allocated_go_elements(&tmp_got_entry, allocator);

          oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
          return;
        }

        object = object->next;
      }

      if (id_only)
      {
        // no tmp elements allocated ...
        PRINT("only found id in request, deleting entry at index: %d", array_index);
        oc_delete_group_object_table_entry(array_index);
      }
      else
      {
        if (return_status == OC_STATUS_CREATED && mandatory_items != 4)
        {
          PRINT("mandatory items missing, no entry created at index: %d", array_index);

          // on error: free GO tmp entry (all heap allocations)
          oc_free_allocated_go_elements(&tmp_got_entry, allocator);

          oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
          return;
        }
        // created + 4 items
        // or
        // changed + 1..3 items (id cannot be reassigned)

        bool do_save = true;
        // check entry with some additional sanity checks
        // a "bad" status will stop processing and return a 4.00

        // note that an empty array ( 7: []) does not end up in setting the GA len, it ends in 4.00
        if (tmp_got_entry.ga_len == 0)
        {
          do_save = false;
          OC_ERR("no groups %d", tmp_got_entry.ga_len);
        }
        if (tmp_got_entry.cflags == 0)
        {
          do_save = false;
          OC_ERR("no cflags set %d", tmp_got_entry.cflags);
        }
        if (oc_string_len(tmp_got_entry.href) > OC_MAX_URL_LENGTH)
        {
          do_save = false;
          OC_ERR("href is longer than %d", OC_MAX_URL_LENGTH);
        }
        if (!oc_belongs_href_to_resource(tmp_got_entry.href, true, 0))
        {
          do_save = false;
          OC_ERR("href '%s' does not belong to device", oc_string_checked(tmp_got_entry.href));
        }

        if (!do_save)
        {
          // on error: free GO tmp entry (all heap allocations)
          oc_free_allocated_go_elements(&tmp_got_entry, allocator);

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
        oc_free_allocated_go_elements(&g_got[array_index], allocator);

        // assign GO table entry with tmp GO (all elements)
        g_got[array_index] = tmp_got_entry;

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

// resource definition, details/comments see on 'core_resource_well_known_core_final'
extern const oc_resource_t core_resource_knx_fp_g_x;
PRAGMA_IN oc_resource_data_t core_resource_knx_fp_g_data;
const oc_resource_t core_resource_knx_fp_g = {(oc_resource_t*)&core_resource_knx_fp_g_x,
                                              0,
                                              {NULL, 0, NULL},
                                              {NULL, sizeof("/fp/g"), "/fp/g"},
                                              {NULL, (size_t)1 * 32, ((char[1][32]){"urn:knx:if.c"})},
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

void oc_create_fp_g_resource(int resource_idx, size_t device)
{
  OC_DBG("oc_create_fp_g_resource");
  oc_core_populate_resource(resource_idx, device, "/fp/g", APPLICATION_CBOR, CONTENT_NONE, OC_DISCOVERABLE,
                            oc_core_fp_g_get_handler, 0, oc_core_fp_g_post_handler, 0, 1, "urn:knx:if.c");
}

static void oc_core_fp_g_x_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;
  PRINT("oc_core_fp_g_x_get_handler - start");

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  const int id = oc_uri_get_wildcard_value_as_int(oc_string(request->resource->uri), oc_string_len(request->resource->uri),
                                                  request->uri_path, request->uri_path_len);

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

  size_t device_index = request->resource->device;
  if (oc_a_lsm_state(device_index) != LSM_S_LOADING)
  {
    OC_ERR("not in loading state");
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  int id = oc_uri_get_wildcard_value_as_int(oc_string(request->resource->uri), oc_string_len(request->resource->uri),
                                            request->uri_path, request->uri_path_len);
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

// resource definition, details/comments see on 'core_resource_well_known_core_final'
extern const oc_resource_t core_resource_knx_fp_p;
PRAGMA_IN oc_resource_data_t core_resource_knx_fp_g_x_data;
const oc_resource_t core_resource_knx_fp_g_x = {(oc_resource_t*)&core_resource_knx_fp_p,
                                                0,
                                                {NULL, 0, NULL},
                                                {NULL, sizeof("/fp/g/*"), "/fp/g/*"},
                                                {NULL, (size_t)1 * 32, ((char[1][32]){"urn:knx:if.c"})},
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
#else

// resource definition, details/comments see on 'core_resource_well_known_core_final'
extern const oc_resource_t core_resource_knx_fp_r;
PRAGMA_IN oc_resource_data_t core_resource_knx_fp_g_x_data;
const oc_resource_t core_resource_knx_fp_g_x = {(oc_resource_t*)&core_resource_knx_fp_r,
                                                0,
                                                {NULL, 0, NULL},
                                                {NULL, sizeof("/fp/g/*"), "/fp/g/*"},
                                                {NULL, (size_t)1 * 32, ((char[1][32]){"urn:knx:if.c"})},
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

void oc_create_fp_g_x_resource(int resource_idx, size_t device)
{
  OC_DBG("oc_create_fp_g_x_resource");
  oc_core_populate_resource(resource_idx, device, "/fp/g/*", APPLICATION_CBOR, CONTENT_NONE, OC_DISCOVERABLE,
                            oc_core_fp_g_x_get_handler, 0, 0, oc_core_fp_g_x_del_handler, 1, "urn:knx:if.c");
}

// -PUBLISHER-

#ifdef OC_PUBLISHER_TABLE

int oc_core_find_empty_slot_in_publisher_table(int id) { return find_empty_slot_in_table(id, g_gpt, GPT_MAX_ENTRIES); }

int oc_core_find_index_in_publisher_table_from_id(int id)
{
  return oc_core_find_index_in_table_from_id(id, g_gpt, GPT_MAX_ENTRIES);
}

int oc_core_add_publisher_entry(int index, oc_group_table_t entry)
{
  return oc_core_add_entry(index, g_gpt, GPT_MAX_ENTRIES, entry);
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

int oc_core_find_publisher_table_index(uint32_t group_address)
{
  int i, j;
  for (i = 0; i < GPT_MAX_ENTRIES; i++)
  {

    if (g_gpt[i].id > -1)
    {
      for (j = 0; j < g_gpt[i].ga_len; j++)
      {
        if (group_address == g_gpt[i].ga[j])
        {
          return i;
        }
      }
    }
  }
  return -1;
}

oc_string_t oc_core_find_publisher_table_url_from_index(int index) { return g_gpt[index].url; }

static void oc_core_fp_p_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  int query_parameter_kvpair_matches =
    0; // how many (to this device applicable) query parameter key/value pair matches where found
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

  const size_t device_index = request->resource->device;
  if (oc_a_lsm_state(device_index) != LSM_S_LOADING)
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
  while (rep != NULL)
  {
    switch (rep->type)
    {
    // a possible collection of PUB entries
    case OC_REP_OBJECT:

      // treat request payload value as one entry (that itself defines a chain of objects for id, ia,...)
      object = rep->value.object;

      // find PUB id in request
      const int id = oc_table_find_id_from_rep(object);
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

      #define TABLE_ATREF (1 << 0)
      #define TABLE_GAS (1 << 1)
      #define TABLE_URL (1 << 2)

      uint8_t allocator = 0; // identify which "stack" memory resource are allocated during the post
      oc_group_table_t tmp_pub_entry = g_gpt[array_index]; // fill with live PUB entry (from a present/empty entry)
      bool id_only = true; // to delete a publisher table entry
      int mandatory_items = 0; // needs to be 3 for creating entry, see below

      while (object != NULL)
      {
        switch (object->type)
        {
        case OC_REP_INT:

          // id(0)
          if (object->iname == 0) 
          {
            // set id in tmp copy
            tmp_pub_entry.id = (int)object->value.integer;

            // valid item + possibly an 'id only case'
            mandatory_items++;
          }

          // ia (12)
          if (object->iname == 12) 
          {
            tmp_pub_entry.ia = (int)object->value.integer;

            // valid item + for sure no 'id only case'
            mandatory_items++;
            id_only = false;
           
          }

          // grpid (13)
          if (object->iname == 13) 
          {
            tmp_pub_entry.grpid = (uint32_t)object->value.integer;

            // valid item + for sure no 'id only case'
            mandatory_items++;
            id_only = false;
           
          }

          // iid (26)
          if (object->iname == 26) 
          {
            tmp_pub_entry.iid = object->value.integer;

            // valid item + for sure no 'id only case'
            id_only = false;
          }

          // fid (25)
          if (object->iname == 25) 
          {
            tmp_pub_entry.fid = object->value.integer;

            // valid item + for sure no 'id only case'
            id_only = false;
          }

          break;
        case OC_REP_STRING:

          // any extra element - even if not valid - causes a "not an id only"
          id_only = false;

          // url (10)
          if (object->iname == 10) 
          {
            // set (new) url in tmp copy (org ptr still valid)
            oc_new_string(&tmp_pub_entry.url, oc_string(object->value.string), oc_string_len(object->value.string));

            mandatory_items++;
            allocator |= TABLE_URL;
          }

          // at (14))
          if (object->iname == 14)
          {
            // set (new) at in tmp copy (org ptr still valid)
            oc_new_string(&tmp_pub_entry.at, oc_string(object->value.string), oc_string_len(object->value.string));

            allocator |= TABLE_ATREF;
          }

          break;
        case OC_REP_INT_ARRAY:

          // any extra element - even if not valid - causes a "not an id only"
          id_only = false;

          // ga array (7) 
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

              tmp_pub_entry.ga_len = array_size;
              tmp_pub_entry.ga = new_array;

              mandatory_items++;
              allocator |= TABLE_GAS;
            }
            else
            {
              OC_ERR("out of stack memory");

              // on error: free PUB tmp entry (all heap allocations)
              oc_free_allocated_table_elements(&tmp_pub_entry, allocator);

              oc_prepare_no_format_response_no_payload(request, OC_STATUS_INTERNAL_SERVER_ERROR);
              return;
            }
          }

          break;
        case OC_REP_NIL:

          // any extra element - even if not valid - causes a "not an id only"
          id_only = false;

          // ga (7) = empty (specification request)
          if (object->iname == 7) 
          {
            tmp_pub_entry.ga_len = 0;
            tmp_pub_entry.ga = NULL;

            mandatory_items++; // also on empty ga array the items# are satisfied
            allocator |= TABLE_GAS; // free() ignores NULL ptr
          }

          break;
        default:

          // any other invalid type returns a 4.00
          OC_ERR("invalid object type detected");

          // on error: free PUB tmp entry (all heap allocations)
          oc_free_allocated_table_elements(&tmp_pub_entry, allocator);

          oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
          return;
          
        }
        object = object->next;
      }

      if (id_only)
      {
        // no tmp elements allocated ...
        PRINT("only found id in request, deleting entry at index: %d", array_index);
        oc_delete_group_table_entry(array_index, GPT_STORE, g_gpt, GPT_MAX_ENTRIES);
      }
      else
      {
        if (return_status == OC_STATUS_CREATED && mandatory_items < 3)
        {
          // id + ga (filled or empty) AND at least one of ia, grpid or url must be present
          PRINT("mandatory items missing, no entry created at index: %d", array_index);

          // on error: free PUB tmp entry (all heap allocations)
          oc_free_allocated_table_elements(&tmp_pub_entry, allocator);

          oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
          return;
        }
        // created + 3 items
        // or
        // changed + 1 ... 2 items (id cannot be reassigned)

        bool do_save = true;
        // check entry with some additional sanity checks
        // a "bad" status will stop processing and return a 4.00

        if (oc_string_len(tmp_pub_entry.url) > OC_MAX_URL_LENGTH)
        {
          do_save = false;
          OC_ERR("url is longer than %d ", OC_MAX_URL_LENGTH);
        }

        if (!do_save)
        {
          // on error: free PUB tmp entry (all heap allocations)
          oc_free_allocated_table_elements(&tmp_pub_entry, allocator);

          oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
          return;
        }

        // here all ok, set new PUB entry
        PRINT("storing PUB table at %d", array_index);

        /*
          Free - on stack allocated -  live PUB table entry elements that will be overwritten next.
          - created : 2 elements (GAs, URL, AT)
          - changed : 1..2 elements (GAs, URL, AT) - 0 not possible, would be to delete a PUB entry
        */
        oc_free_allocated_table_elements(&g_gpt[array_index], allocator);

        // assign PUB table entry with tmp PUB (all elements)
        g_gpt[array_index] = tmp_pub_entry;

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

  // the last return status from a collection with 'n' POST elements is responded (CREATED/CHANGED)
  oc_prepare_no_format_response_no_payload(request, return_status);

  PRINT("oc_core_fp_p_post_handler - end");
}

// resource definition, details/comments see on 'core_resource_well_known_core_final'
extern const oc_resource_t core_resource_knx_fp_p_x;
PRAGMA_IN oc_resource_data_t core_resource_knx_fp_p_data;
const oc_resource_t core_resource_knx_fp_p = {(oc_resource_t*)&core_resource_knx_fp_p_x,
                                              0,
                                              {NULL, 0, NULL},
                                              {NULL, sizeof("/fp/p"), "/fp/p"},
                                              {NULL, (size_t)1 * 32, ((char[1][32]){"urn:knx:if.c"})},
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

void oc_create_fp_p_resource(int resource_idx, size_t device)
{
  OC_DBG("oc_create_fp_p_resource");
  oc_core_populate_resource(resource_idx, device, "/fp/p", APPLICATION_CBOR, CONTENT_NONE, OC_DISCOVERABLE,
                            oc_core_fp_p_get_handler, 0, oc_core_fp_p_post_handler, 0, 1, "urn:knx:if.c");
}

static void oc_core_fp_p_x_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;
  PRINT("oc_core_fp_p_x_get_handler - start");

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  const int id = oc_uri_get_wildcard_value_as_int(oc_string(request->resource->uri), oc_string_len(request->resource->uri),
                                                  request->uri_path, request->uri_path_len);

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

  // url- 10
  oc_rep_i_set_text_string(root, 10, oc_string(g_gpt[index].url));

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

  size_t device_index = request->resource->device;
  if (oc_a_lsm_state(device_index) != LSM_S_LOADING)
  {
    OC_ERR("not in loading state");
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  int id = oc_uri_get_wildcard_value_as_int(oc_string(request->resource->uri), oc_string_len(request->resource->uri),
                                            request->uri_path, request->uri_path_len);
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

// resource definition, details/comments see on 'core_resource_well_known_core_final'
extern const oc_resource_t core_resource_knx_fp_r;
PRAGMA_IN oc_resource_data_t core_resource_knx_fp_p_x_data;
const oc_resource_t core_resource_knx_fp_p_x = {(oc_resource_t*)&core_resource_knx_fp_r,
                                                0,
                                                {NULL, 0, NULL},
                                                {NULL, sizeof("/fp/p/*"), "/fp/p/*"},
                                                {NULL, (size_t)1 * 32, ((char[1][32]){"urn:knx:if.c"})},
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

void oc_create_fp_p_x_resource(int resource_idx, size_t device)
{
  OC_DBG("oc_create_fp_p_x_resource");
  oc_core_populate_resource(resource_idx, device, "/fp/p/*", APPLICATION_CBOR, CONTENT_NONE, OC_DISCOVERABLE,
                            oc_core_fp_p_x_get_handler, 0, 0, oc_core_fp_p_x_del_handler, 0, 1, "urn:knx:if.c");
}

#endif

// -RECIPIENT-

static void oc_core_fp_r_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  int query_parameter_kvpair_matches =
    0; // how many (to this device applicable) query parameter key/value pair matches where found
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

  const size_t device_index = request->resource->device;
  if (oc_a_lsm_state(device_index) != LSM_S_LOADING)
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
  while (rep != NULL)
  {
    switch (rep->type)
    {
      // a possible collection of RCP entries
    case OC_REP_OBJECT:

      // treat request payload value as one entry (that itself defines a chain of objects for id, ia,...)
      object = rep->value.object;

      // find RCP id in request
      const int id = oc_table_find_id_from_rep(object);
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

      uint8_t allocator = 0; // identify which "stack" memory resource are allocated during the post
      oc_group_table_t tmp_rcp_entry = g_grt[array_index]; // fill with live RCP entry (from a present/empty entry)
      bool id_only = true; // used to delete the RCP table entry
      int mandatory_items = 0; // needs to be 3 for creating entry, see below

      while (object != NULL)
      {
        switch (object->type)
        {
        case OC_REP_INT:

          // id(0)
          if (object->iname == 0) 
          {

            // set id in tmp copy
            tmp_rcp_entry.id = (int)object->value.integer;

            // valid item + possibly an 'id only case'
            mandatory_items++;
          }

          // ia (12)
          if (object->iname == 12) 
          {
            
            tmp_rcp_entry.ia = (int)object->value.integer;

            // valid item + for sure no 'id only case'
            mandatory_items++;
            id_only = false;
          }

          // grpid (13)
          if (object->iname == 13) 
          {
            
            tmp_rcp_entry.grpid = (uint32_t)object->value.integer;

            // valid item + for sure no 'id only case'
            mandatory_items++;
            id_only = false;
          }

          // iid (26)
          if (object->iname == 26) 
          {
            tmp_rcp_entry.iid = object->value.integer;

            // valid item + for sure no 'id only case'
            id_only = false;
          }

          // fid (25)
          if (object->iname == 25) 
          {
            tmp_rcp_entry.fid = object->value.integer;

            // valid item + for sure no 'id only case'
            id_only = false;
          }

          break;
        case OC_REP_STRING:

          // any extra element - even if not valid - causes a "not an id only"
          id_only = false;

          // url (10)
          if (object->iname == 10) 
          {
            // set (new) url in tmp copy (org ptr still valid)
            oc_new_string(&tmp_rcp_entry.url, oc_string(object->value.string), oc_string_len(object->value.string));

            mandatory_items++;
            allocator |= TABLE_URL;
          }

          // at (14))
          if (object->iname == 14)
          {
            // set (new) at in tmp copy (org ptr still valid)
            oc_new_string(&tmp_rcp_entry.at, oc_string(object->value.string), oc_string_len(object->value.string));

            allocator |= TABLE_ATREF;
          }

          break;
        case OC_REP_INT_ARRAY:

          // any extra element - even if not valid - causes a "not an id only"
          id_only = false;

          // ga array (7)
          if (object->iname == 7)
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

              tmp_rcp_entry.ga_len = array_size;
              tmp_rcp_entry.ga = new_array;

              mandatory_items++;
              allocator |= TABLE_GAS;
            }
            else
            {
              OC_ERR("out of stack memory");

              // on error: free PUB tmp entry (all heap allocations)
              oc_free_allocated_table_elements(&tmp_rcp_entry, allocator);

              oc_prepare_no_format_response_no_payload(request, OC_STATUS_INTERNAL_SERVER_ERROR);
              return;
            }
          }

          break;
        
        case OC_REP_NIL:

          // any extra element - even if not valid - causes a "not an id only"
          id_only = false;

          if (object->iname == 7) // resource 'ga array' = empty (specification request)
          {
            tmp_rcp_entry.ga_len = 0;
            tmp_rcp_entry.ga = NULL;

            mandatory_items++; // also on empty ga array the items# are satisfied
            allocator |= TABLE_GAS; // free() ignores NULL ptr
          }

          break;
        case OC_REP_BOOL:

          // any extra element - even if not valid - causes a "not an id only"
          id_only = false;

          // resource 'non' (CBOR/JSON = 'non'/'non')
          if (oc_string_len(object->name) > 0 && strncmp(oc_string(object->name), "non", 3) == 0)
          {
            tmp_rcp_entry.non = object->value.boolean;
          }

          break;
        default:

          // any other invalid type returns a 4.00
          OC_ERR("invalid object type detected");

          // on error: free RCP tmp entry (all heap allocations)
          oc_free_allocated_table_elements(&tmp_rcp_entry, allocator);

          oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
          break;
        }
        object = object->next;
      }

      if (id_only)
      {
        // no tmp elements allocated ...
        PRINT("only found id in request, deleting entry at index: %d", array_index);
        oc_delete_group_table_entry(array_index, GRT_STORE, g_grt, GRT_MAX_ENTRIES);
      }
      else
      {
        if (return_status == OC_STATUS_CREATED && mandatory_items < 3) 
        { // id + ga (filled or empty) AND at least one of ia, grpid or url must be present
          PRINT("mandatory items missing, no entry created at index: %d", array_index);

          // on error: free RCP tmp entry (all heap allocations)
          oc_free_allocated_table_elements(&tmp_rcp_entry, allocator);
          
          oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
          return;
        }
        // created + 3 items
        // or
        // changed + 1 ... 2 items (id cannot be reassigned)

        bool do_save = true;
        // check entry with some additional sanity checks
        // a "bad" status will stop processing and return a 4.00

        if (oc_string_len(tmp_rcp_entry.url) > OC_MAX_URL_LENGTH)
        {
          do_save = false;
          OC_ERR("url is longer than %d ", OC_MAX_URL_LENGTH);
        }

        if (!do_save)
        {

          // on error: free RCP tmp entry (all heap allocations)
          oc_free_allocated_table_elements(&tmp_rcp_entry, allocator);

          oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
          return;
        }

        // here all ok, set new RCP entry
        PRINT("storing RCV table at %d", array_index);

        /*
          Free - on stack allocated -  live RCP table entry elements that will be overwritten next.
          - created : 2 elements (GAs, URL, AT)
          - changed : 1..2 elements (GAs, URL, AT) - 0 not possible, would be to delete a PUB entry
        */
        oc_free_allocated_table_elements(&g_grt[array_index], allocator);

        // assign RCP table entry with tmp RCP (all elements)
        g_grt[array_index] = tmp_rcp_entry;

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

  // the last return status from a collection with 'n' POST elements is responded (CREATED/CHANGED)
  oc_prepare_no_format_response_no_payload(request, return_status);

  PRINT("oc_core_fp_r_post_handler - end");
}

// resource definition, details/comments see on 'core_resource_well_known_core_final'
extern const oc_resource_t core_resource_knx_fp_r_x;
PRAGMA_IN oc_resource_data_t core_resource_knx_fp_r_data;
const oc_resource_t core_resource_knx_fp_r = {(oc_resource_t*)&core_resource_knx_fp_r_x,
                                              0,
                                              {NULL, 0, NULL},
                                              {NULL, sizeof("/fp/r"), "/fp/r"},
                                              {NULL, (size_t)1 * 32, ((char[1][32]){"urn:knx:if.c"})},
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

void oc_create_fp_r_resource(int resource_idx, size_t device)
{
  OC_DBG("oc_create_fp_r_resource");
  oc_core_populate_resource(resource_idx, device, "/fp/r", APPLICATION_CBOR, CONTENT_NONE, OC_DISCOVERABLE,
                            oc_core_fp_r_get_handler, 0, oc_core_fp_r_post_handler, 0, 1, "urn:knx:if.c");
}

static void oc_core_fp_r_x_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  PRINT("oc_core_fp_r_x_get_handler - start");

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  const int id = oc_uri_get_wildcard_value_as_int(oc_string(request->resource->uri), oc_string_len(request->resource->uri),
                                                  request->uri_path, request->uri_path_len);

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

  // url- 10
  oc_rep_i_set_text_string(root, 10, oc_string(g_grt[index].url));

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

  size_t device_index = request->resource->device;
  if (oc_a_lsm_state(device_index) != LSM_S_LOADING)
  {
    OC_ERR("not in loading state");
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  int id = oc_uri_get_wildcard_value_as_int(oc_string(request->resource->uri), oc_string_len(request->resource->uri),
                                            request->uri_path, request->uri_path_len);
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

// resource definition, details/comments see on 'core_resource_well_known_core_final'
extern const oc_resource_t core_resource_knx_p;
PRAGMA_IN oc_resource_data_t core_resource_knx_fp_r_x_data;
const oc_resource_t core_resource_knx_fp_r_x = {(oc_resource_t*)&core_resource_knx_p,
                                                0,
                                                {NULL, 0, NULL},
                                                {NULL, sizeof("/fp/r/*"), "/fp/r/*"},
                                                {NULL, (size_t)1 * 32, ((char[1][32]){"urn:knx:if.c"})},
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

void oc_create_fp_r_x_resource(int resource_idx, size_t device)
{
  OC_DBG("oc_create_fp_r_x_resource");
  oc_core_populate_resource(resource_idx, device, "/fp/r/*", APPLICATION_CBOR, CONTENT_NONE, OC_DISCOVERABLE,
                            oc_core_fp_r_x_get_handler, 0, 0, oc_core_fp_r_x_del_handler, 1, "urn:knx:if.c");
}

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

char* oc_core_get_recipient_index_url(int index)
{
  if (index >= GRT_MAX_ENTRIES)
  {
    return NULL;
  }

  // ia = -1 = init value; ia == 0 is reserved in KNX, so only send when ia > 0
  if (g_grt[index].ia > 0)
  {
    PRINT("oc_core_get_recipient_index_url: ia %d", g_grt[index].ia);

    if (oc_string_len(g_grt[index].url) > 0)
    {
      // use url only in case ia is also present
      PRINT("oc_core_get_recipient_index_url: url %s", oc_string_checked(g_grt[index].url));
      return oc_string(g_grt[index].url);
    }

    PRINT("oc_core_get_recipient_index_url: (default) k");
    return "k";
  }

  return NULL;
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
  PRINT("no go storage");
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

              // assign GA array in GO table directly
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
  if (init == false)
  {
    oc_free_string(&g_got[entry].href);
    free(g_got[entry].ga); // NULL ptr is handled
  }

  g_got[entry].ga = NULL;
  g_got[entry].ga_len = 0;
  g_got[entry].cflags = 0;
}

void oc_free_allocated_go_elements(oc_group_object_table_t* entry, const uint8_t allocator)
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

void oc_free_allocated_table_elements(oc_group_table_t* entry, const uint8_t allocator)
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

  if (allocator & TABLE_URL)
  {
    oc_free_string(&entry->url);
  }
}

int oc_delete_group_object_table_entry(int entry)
{
  if (entry < 0 || entry > GOT_MAX_ENTRIES - 1)
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

void oc_free_group_object_table(void)
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
  PRINT("iid (26)   : %" PRIu64 "", table[entry].iid);
  PRINT("fid (25)   : %" PRIu64 "", table[entry].fid);
  PRINT("grpid (13) : %u", table[entry].grpid);

  if (oc_string_len(table[entry].url) > 0)
  {
    PRINT("url (10)   : '%s'", oc_string_checked(table[entry].url));
  }
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

static void oc_print_reduced_group_table_entry(int entry, char* store, oc_group_table_t* table, int max_size)
{
  (void)max_size;
  if (table[entry].id == -1)
  {
    return;
  }
  printf("  %s [%d] --> [%d]", store, entry, table[entry].ga_len);
  printf("    id (0)     : %d", table[entry].id);

  printf("    iid (26)   : ");
  oc_print_uint64_t(table[entry].iid, DEC_REPRESENTATION);
  printf("");

  printf("    grpid (13) : %u", table[entry].grpid);

  printf("    ga (7)     : [");
  for (int i = 0; i < table[entry].ga_len; i++)
  {
    printf(" %u", table[entry].ga[i]);
  }
  printf(" ]");
}

// store RCP/PUB table data in CBOR (hex stream data)
static void oc_store_group_table_entry(int entry, char* store, const oc_group_table_t* table)
{
#ifndef OC_USE_STORAGE
  (void)entry;
  PRINT("no go storage");
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

void oc_load_group_table_entry(int entry, char* Store, oc_group_table_t* rp_table)
{
  char filename[FPT_SIZE];
  (void)snprintf(filename, FPT_SIZE, "%s_%d", Store, entry);

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

    int err = oc_parse_rep(buf, bytes_to_read, &rep);
    oc_rep_t* head = rep;
    if (err == 0)
    {
      while (rep != NULL)
      {
        switch (rep->type)
        {

        case OC_REP_INT:
          if (rep->iname == 0)
          {
            rp_table[entry].id = (int)rep->value.integer;
          }
          if (rep->iname == 12)
          {
            rp_table[entry].ia = (int)rep->value.integer;
          }
          if (rep->iname == 13)
          {
            rp_table[entry].grpid = (uint32_t)rep->value.integer;
          }
          if (rep->iname == 25)
          {
            rp_table[entry].fid = rep->value.integer;
          }
          if (rep->iname == 26)
          {
            rp_table[entry].iid = rep->value.integer;
          }
          break;
        case OC_REP_STRING:
          if (rep->iname == 10)
          {
            oc_free_string(&rp_table[entry].url);
            oc_new_string(&rp_table[entry].url, oc_string(rep->value.string), oc_string_len(rep->value.string));
          }
          if (rep->iname == 14)
          {
            oc_free_string(&rp_table[entry].at);
            oc_new_string(&rp_table[entry].at, oc_string(rep->value.string), oc_string_len(rep->value.string));
          }
          break;
        case OC_REP_INT_ARRAY:
          if (rep->iname == 7)
          {
            int64_t* arr = oc_int_array(rep->value.array);
            int array_size = (int)oc_int_array_size(rep->value.array);
            uint32_t* new_array = (uint32_t*)malloc(array_size * sizeof(uint32_t));
            if (new_array != NULL && array_size > 0)
            {
              for (int i = 0; i < array_size; i++)
              {
#pragma warning(suppress : 6386)
                new_array[i] = (uint32_t)arr[i];
              }
              // assign only when the new array is allocated correctly
              rp_table[entry].ga_len = array_size;
            }
            if (rp_table[entry].ga != 0)
            {
              free(rp_table[entry].ga);
            }
            PRINT("ga size %d", array_size);
            // if (rp_table[entry].ga) {
            //   free(rp_table[entry].ga);
            // }
            rp_table[entry].ga = new_array;
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

void oc_load_object_table(void)
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
  if (init == false)
  {
    oc_free_string(&table[entry].url);
    oc_free_string(&table[entry].at);
    free(table[entry].ga);
  }

  table[entry].ga = NULL;
  table[entry].ga_len = 0;
}

static int oc_delete_group_table_entry(int entry, char* store, oc_group_table_t* table, int max_size)
{

  // use either GPT or GRT table size
  if (entry < 0 || entry > max_size - 1)
    return -1;

  // delete GPT/GRT entry (note, one file per entry)
  char filename[20];
  (void)snprintf(filename, 20, "%s_%d", store, entry);
  oc_storage_erase(filename);

  oc_free_group_table_entry(entry, table, false);
  return 0;
}

void oc_delete_group_tables(void)
{
  PRINT("Deleting Recipient Table from Persistent storage");
  for (int i = 0; i < GRT_MAX_ENTRIES; i++)
  {
    oc_delete_group_table_entry(i, GRT_STORE, g_grt, GRT_MAX_ENTRIES);
    oc_print_group_table_entry(i, GRT_STORE, g_grt);
  }

#ifdef OC_PUBLISHER_TABLE
  PRINT("Deleting Publisher Table from Persistent storage");
  for (int i = 0; i < GPT_MAX_ENTRIES; i++)
  {
    oc_delete_group_table_entry(i, GPT_STORE, g_gpt, GPT_MAX_ENTRIES);
    oc_print_group_table_entry(i, GPT_STORE, g_gpt);
  }
#endif
}

static void oc_free_group_tables(void)
{
  PRINT("Free RCP table from persistent storage");
  for (int i = 0; i < GRT_MAX_ENTRIES; i++)
  {
    oc_free_group_table_entry(i, g_grt, false);
  }

#ifdef OC_PUBLISHER_TABLE
  PRINT("Free PUB table from persistent storage");
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

int oc_core_add_entry(int index, oc_group_table_t* table, int table_size, oc_group_table_t entry)
{
  if (index >= table_size)
  {
    OC_ERR("recipient table index is too large: index(%d) max_size(%d)", index, table_size);
  }

  // Store entries
  table[index].id = entry.id;
  table[index].iid = entry.iid;
  table[index].fid = entry.fid;
  table[index].grpid = entry.grpid;

  // Copy group addresses
  table[index].ga_len = 0;
  uint32_t* new_array = (uint32_t*)malloc(entry.ga_len * sizeof(uint32_t));
  if (new_array != NULL && entry.id > -1)
  {
    for (int i = 0; i < entry.ga_len; i++)
    {
#pragma warning(suppress : 6386)
      new_array[i] = entry.ga[i];
    }
    // copy only when the allocation was done correctly
    table[index].ga_len = entry.ga_len;
    if (table[index].ga != 0)
    {
      free(table[index].ga);
    }
    table[index].ga = new_array;
  }

  return 0;
}

int oc_core_add_recipient_entry(int index, oc_group_table_t entry)
{
  return oc_core_add_entry(index, g_grt, GRT_MAX_ENTRIES, entry);
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

int oc_core_find_empty_slot_in_recipient_table(int id) { return find_empty_slot_in_table(id, g_grt, GRT_MAX_ENTRIES); }

int oc_core_find_index_in_recipient_table_from_id(int id)
{
  return oc_core_find_index_in_table_from_id(id, g_grt, GRT_MAX_ENTRIES);
}

static void oc_init_tables(void)
{
#ifdef OC_PUBLISHER_TABLE

  for (int i = 0; i < GPT_MAX_ENTRIES; i++)
  {
    oc_free_group_table_entry(i, g_gpt, true);
  }
#endif

  for (int i = 0; i < GRT_MAX_ENTRIES; i++)
  {
    oc_free_group_table_entry(i, g_grt, true);
  }

  for (int i = 0; i < GOT_MAX_ENTRIES; i++)
  {
    oc_free_group_object_table_entry(i, true);
  }
}

void oc_create_knx_fp_resources(size_t device_index)
{
  OC_DBG("oc_create_knx_fp_resources");

  if (device_index == 0)
  {
    OC_DBG("device 0: KNX function point resources created statically");
  }
  else
  {
    oc_create_fp_g_resource(OC_KNX_FP_G, device_index);
    oc_create_fp_g_x_resource(OC_KNX_FP_G_X, device_index);

#ifdef OC_PUBLISHER_TABLE
    oc_create_fp_p_resource(OC_KNX_FP_P, device_index);
    oc_create_fp_p_x_resource(OC_KNX_FP_P_X, device_index);
#endif

    oc_create_fp_r_resource(OC_KNX_FP_R, device_index);
    oc_create_fp_r_x_resource(OC_KNX_FP_R_X, device_index);
  }
  oc_init_tables();
  oc_load_group_object_table();
  oc_load_object_table();
}

void oc_free_knx_fp_resources(size_t device_index)
{
  oc_free_group_tables();
  oc_free_group_object_table();
}

bool is_in_array(uint32_t value, uint32_t* array, int array_size)
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

bool oc_add_points_in_group_object_table_to_response(oc_request_t* request, size_t device_index, uint32_t group_address,
                                                     size_t* response_length)
{

  bool return_value = false;

  PRINT("oc_add_points_in_group_object_table_to_response %u", group_address);

  for (int index = 0; index < GOT_MAX_ENTRIES; index++)
  {
    if (g_got[index].id > -1)
    {
      if (is_in_array(group_address, g_got[index].ga, g_got[index].ga_len))
      {
        // add the resource to response, note, it is not checked if the resource is already there...
        PRINT("oc_add_points_in_group_object_table_to_response [%d] %s", index, oc_string_checked(g_got[index].href));

        oc_add_resource_to_response_payload(
          oc_ri_get_app_resource_by_uri(oc_string(g_got[index].href), oc_string_len(g_got[index].href), device_index),
          request, device_index, response_length, true);
        return_value = true;
      }
    }
  }
  return return_value;
}

oc_endpoint_t oc_create_multicast_group_address_with_port(oc_endpoint_t in, uint32_t group_nr, uint64_t iid, int scope,
                                                          uint32_t port)
{
  // create the multicast address from group and scope
  // FF3_:FD__:____:____:(8-f)___:____
  // FF35:30:<ULA-routing-prefix>::<group id>
  //    | 5 == scope
  //    | 3 == scope
  // Multicast prefix: FF35:0030:  [4 bytes]
  // ULA routing prefix: FD11:2222:3333::  [6 bytes + 2 empty bytes]
  // Group Identifier: 8000 : 0068 [4 bytes ]

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

  int my_transport_flags = 0;
  my_transport_flags += IPV6;
  my_transport_flags += MULTICAST;
  my_transport_flags += DISCOVERY;
#ifdef OC_OSCORE
  my_transport_flags += OSCORE;
#endif

  oc_make_ipv6_endpoint(group_mcast, my_transport_flags, port, 0xff, 0x30 + scope, 0, 0x30, //  FF35::30:
                        0xfd, ula_5, ula_4, ula_3, ula_2,
                        ula_1, // FD11 : 2222 : 3333
                        0, 0, // ::
                        byte_4, byte_3, byte_2, byte_1);
  PRINT("oc_create_multicast_group_address_with_port S=%d iid=%" PRIu64 " G=%u B4=%d "
        "B3=%d B2=%d "
        "B1=%d",
        scope, iid, group_nr, byte_4, byte_3, byte_2, byte_1);
  PRINT("");
  PRINTipaddr(group_mcast);

  group_mcast.group_address = group_nr;
  memcpy(&in, &group_mcast, sizeof(oc_endpoint_t));

  return in;
}

oc_endpoint_t oc_create_multicast_group_address(oc_endpoint_t in, uint32_t group_nr, uint64_t iid, int scope)
{
  return oc_create_multicast_group_address_with_port(in, group_nr, iid, scope, 5683);
}

void subscribe_group_to_multicast_with_port(uint32_t group_nr, uint64_t iid, int scope, uint32_t port)
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
  // create the multi cast address from group and scope
  oc_endpoint_t group_mcast;
  memset(&group_mcast, 0, sizeof(group_mcast));

  group_mcast = oc_create_multicast_group_address(group_mcast, group_nr, iid, scope);

  // subscribe
  oc_connectivity_subscribe_mcast_ipv6(&group_mcast);
}

void unsubscribe_group_to_multicast_with_port(uint32_t group_nr, uint64_t iid, int scope, uint32_t port)
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

  group_mcast = oc_create_multicast_group_address(group_mcast, group_nr, iid, scope);

  // un subscribe
  oc_connectivity_unsubscribe_mcast_ipv6(&group_mcast);
}

uint32_t oc_find_grpid_in_table(oc_group_table_t* table, int max_size, const uint32_t group_address)
{
  for (int index = 0; index < max_size; index++)
  {
    uint32_t* array = table[index].ga;
    int array_size = table[index].ga_len;
    bool found = is_in_array(group_address, array, array_size);
    if (found)
    {
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
  oc_device_info_t* device = oc_core_get_device_info(0);
  if (!device)
  {
    PRINT("oc_register_group_multicasts: no device info");
    return;
  }
  const uint64_t installation_id = device->iid;
  const uint32_t mport = device->mport;

  PRINT("oc_register_group_multicasts: mport %u", mport);

  bool pub_table_grpid_entry_present = false;

  for (int index = 0; index < GPT_MAX_ENTRIES; index++)
  {
    uint32_t grpid = g_gpt[index].grpid;
    if (grpid > 0)
    {
      pub_table_grpid_entry_present = true;
      break;
    }
  }
  if (pub_table_grpid_entry_present)
  {
    // register via grpid
    for (int index = 0; index < GPT_MAX_ENTRIES; index++)
    {
      const int nr_entries = g_got[index].ga_len;
      const oc_cflag_mask_t cflags = g_got[index].cflags;

      // check if the GA is used for receiving, e.g. one of r/w/u
      if (cflags & OC_CFLAG_WRITE + OC_CFLAG_UPDATE + OC_CFLAG_READ)
      {
        for (int i = 0; i < nr_entries; i++)
        {
          // check if a 'receiving' GA as part of a GO is in the publisher table (device can receive)
          const uint32_t grpid = oc_find_grpid_in_table(g_gpt, GPT_MAX_ENTRIES, g_got[index].ga[i]);

          PRINT("oc_register_group_multicasts index=%d i=%d grpid: %u group_address: %d cflags=", index, i, grpid,
                g_got[index].ga[i]);
          oc_print_cflags(cflags);

          if (grpid > 0)
          { // found
            subscribe_group_to_multicast_with_port(grpid, installation_id, 2, mport);
            subscribe_group_to_multicast_with_port(grpid, installation_id, 5, mport);
          }
        }
      }
    }
  }
  else
  {
    // register via group object
    for (int index = 0; index < GOT_MAX_ENTRIES; index++)
    {
      const int nr_entries = g_got[index].ga_len;
      const oc_cflag_mask_t cflags = g_got[index].cflags;

      // check if the GA is used for receiving, e.g. one of r/w/u
      if (cflags & OC_CFLAG_WRITE + OC_CFLAG_UPDATE + OC_CFLAG_READ)
      {
        // register all GA's of this GO
        for (int i = 0; i < nr_entries; i++)
        {
          PRINT("oc_register_group_multicasts index=%d i=%d group: %u  cflags=", index, i, g_got[index].ga[i]);
          oc_print_cflags(cflags); // debugging
          subscribe_group_to_multicast_with_port(g_got[index].ga[i], installation_id, 2, mport);
          subscribe_group_to_multicast_with_port(g_got[index].ga[i], installation_id, 5, mport);
        }
      }
    }
  }
#endif
}

void oc_init_datapoints_at_initialization(void)
{
  PRINT("Scan datapoints for possible initialization...");

  for (int index = 0; index < GOT_MAX_ENTRIES; index++)
  {
    // at least one GA is assigned (is an array)
    if (g_got[index].ga_len > 0)
    {
      if (g_got[index].cflags & OC_CFLAG_INIT)
      {
        // read on init cflags is set, fire (after device restart)
        // via the sending association(first assigned ga == sending ga)
        PRINT("Init_datapoint, index: %d issue read on group address %u", index, g_got[index].ga[0]);
        oc_do_s_mode_read(g_got[index].ga[0]);
      }
    }
  }
}
