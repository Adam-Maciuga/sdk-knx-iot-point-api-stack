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

#include "api/oc_knx_sec.h"
#include <stdio.h>
#include "oc_api.h"
#include "oc_core_res.h"
#include "oc_discovery.h"
#define __STDC_FORMAT_MACROS // defined to use format specifiers also in C++
#include <inttypes.h>
#include "security/oc_oscore_context.h"

#include "oc_knx_helpers.h"
#include "oc_storage.h"


// AT storage data
#define AT_STORE "at_store"
#define AT_SIZE (sizeof(AT_STORE) + 6) // support of '_99999' at FILE entries

static uint32_t g_oscore_replaywindow = 32; // default according to RFC OSCORE
static uint32_t g_oscore_osndelay = 1000; // default (ms) defined by iot specification
static oc_auth_at_t g_at_entries[G_AT_MAX_ENTRIES]; // static inits with '0', included strings next/ptr/size are '0' are not valid

// ----------------------------------------------------------------------------

static void oc_store_at_table_entry(int entry);

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
  (void)data;
  (void)iface_mask;

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
  (void)data;
  (void)iface_mask;

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
        PRINT("oc_core_knx_auth_o_osndelay_put_handler type: %d value %d", (int)rep->type, (int)rep->value.integer);
        g_oscore_osndelay = rep->value.integer;
        oc_prepare_cbor_response(request, OC_STATUS_CHANGED);
        return;
      }
    }
    rep = rep->next;
  }

  oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
}

// resource definition, details/comments see on
// 'core_resource_well_known_core_final'
extern const oc_resource_t core_resource_knx_auth_o;
PRAGMA_IN oc_resource_data_t core_resource_knx_auth_o_osndelay_data;
const oc_resource_t core_resource_knx_auth_o_osndelay = {
  (oc_resource_t*)&core_resource_knx_auth_o,
  0,
  {NULL, 0, NULL},
  {NULL, sizeof("/auth/o/osndelay"), "/auth/o/osndelay"},
  {NULL, (size_t)1 * 32, ((char[1][32]){":dpt:timePeriodMsec"})},
  {NULL, 0, NULL},
  {APPLICATION_CBOR, CONTENT_NONE},
  OC_DISCOVERABLE,
  // for non defined PUT/POST/DELETE handler use if.none, to return 4.05 instead of 4.01 (unauthorized)
  {oc_core_knx_auth_o_osndelay_get_handler, NULL, OC_ACL_P, OC_IF_P},
  {oc_core_knx_auth_o_osndelay_put_handler, NULL, OC_ACL_SEC, OC_IF_SEC},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL},
  {NULL, NULL},
  0,
  0,
  true,
  &core_resource_knx_auth_o_osndelay_data};
PRAGMA_OUT

void oc_create_knx_auth_o_osndelay_resource(int resource_idx, size_t device)
{
  OC_DBG("oc_create_knx_auth_o_osndelay_resource");
  //
  oc_core_populate_resource(resource_idx, device, "/auth/o/osndelay", APPLICATION_CBOR, CONTENT_NONE, OC_DISCOVERABLE,
                            oc_core_knx_auth_o_osndelay_get_handler, oc_core_knx_auth_o_osndelay_put_handler, 0, 0, 1,
                            ":dpt:timePeriodMsec");
}

static void oc_core_knx_auth_o_replwdo_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

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

static void oc_core_knx_auth_o_replwdo_put_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

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
        PRINT("oc_core_knx_auth_o_replwdo_put_handler type: %d value %d", rep->type, (int)rep->value.integer);
        g_oscore_replaywindow = rep->value.integer;
        oc_prepare_cbor_response(request, OC_STATUS_CHANGED);
        return;
      }
    }
    rep = rep->next;
  }

  oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
}

// resource definition, details/comments see on
// 'core_resource_well_known_core_final'
PRAGMA_IN oc_resource_data_t core_resource_knx_auth_o_replwdo_data;
const oc_resource_t core_resource_knx_auth_o_replwdo = {
  (oc_resource_t*)&core_resource_knx_auth_o_osndelay,
  0,
  {NULL, 0, NULL},
  {NULL, sizeof("/auth/o/replwdo"), "/auth/o/replwdo"},
  {NULL, (size_t)1 * 32, ((char[1][32]){":dpt.value2UCount"})},
  {NULL, 0, NULL},
  {APPLICATION_CBOR, CONTENT_NONE},
  OC_DISCOVERABLE,
  // for non defined PUT/POST/DELETE handler use if.none, to return 4.05 instead of 4.01 (unauthorized)
  {oc_core_knx_auth_o_replwdo_get_handler, NULL, OC_ACL_P, OC_IF_P},
  {oc_core_knx_auth_o_replwdo_put_handler, NULL, OC_ACL_SEC, OC_IF_SEC},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL},
  {NULL, NULL},
  0,
  0,
  true,
  &core_resource_knx_auth_o_replwdo_data};
PRAGMA_OUT

void oc_create_knx_auth_o_replwdo_resource(int resource_idx, size_t device)
{
  OC_DBG("oc_create_knx_auth_o_replwdo_resource");
  //
  oc_core_populate_resource(resource_idx, device, "/auth/o/replwdo", APPLICATION_CBOR, CONTENT_NONE, OC_DISCOVERABLE,
                            oc_core_knx_auth_o_replwdo_get_handler, oc_core_knx_auth_o_replwdo_put_handler, 0, 0, 1,
                            ":dpt.value2UCount");
}

// ----------------------------------------------------------------------------

static void oc_core_knx_auth_o_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  int query_parameter_kvpair_matches = 0; // how many (to this device applicable) query parameter key/value pair
                                          // matches where found
  size_t response_length = 0;
  int query_pn = PAGE_NUMBER;
  int query_ps = PAGE_SIZE;

  int first_entry = OC_KNX_AUTH_O_REPLWDO; // first entry number of a resource
                                           // that will be placed on a page
  int last_entry = OC_KNX_AUTH_O; // last entry number of a resource that will
                                  // be placed on a page
  int total = last_entry - first_entry; // total entries of this resource
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
  // - total=4, pn 5, ps 20, first entry = 100 -> no data on page 5 (all on page
  // 0)
  // - total=4, pn 1, ps 04, first entry = 004 -> no data on page 1 (all on page
  // 0)
  if (first_entry >= last_entry || query_ps == 0)
  {
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  // entries don't fit in a single page -> more pages are needed to get the full
  // list
  // - total=4, page number 1, page size 02, first entry = 002 -> no more data
  // on next page
  // - total=4, page number 1, page size 01, first entry = 001 -> more data on
  // next page
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

// resource definition, details/comments see on
// 'core_resource_well_known_core_final'
extern const oc_resource_t core_resource_knx_auth_at;
PRAGMA_IN oc_resource_data_t core_resource_knx_auth_o_data;
const oc_resource_t core_resource_knx_auth_o = {
  (oc_resource_t*)&core_resource_knx_auth_at,
  0,
  {NULL, 0, NULL},
  {NULL, sizeof("/auth/o"), "/auth/o"},
  {NULL, 0, NULL},
  {NULL, 0, NULL},
  {APPLICATION_LINK_FORMAT, CONTENT_NONE},
  OC_DISCOVERABLE,
  // for non defined PUT/POST/DELETE handler use if.none, to return 4.05 instead of 4.01 (unauthorized)
  {oc_core_knx_auth_o_get_handler, NULL, OC_ACL_P | OC_ACL_D | OC_ACL_C, OC_IF_LI},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL},
  {NULL, NULL},
  0,
  0,
  true,
  &core_resource_knx_auth_o_data};
PRAGMA_OUT

void oc_create_knx_auth_o_resource(int resource_idx, size_t device_index)
{
  OC_DBG("create /aut/o resources");
  // TODO: what is resource type, none for now
  oc_core_populate_resource(resource_idx, device_index, "/auth/o", APPLICATION_LINK_FORMAT, CONTENT_NONE, OC_DISCOVERABLE,
                            oc_core_knx_auth_o_get_handler, 0, 0, 0, 0);
}

// ----------------------------------------------------------------------------

#define LDEVID_RENEW 1
#define LDEVID_STOP 2

static int a_sen_convert_cmd(char* cmd)
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
  (void)data;
  (void)iface_mask;

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

// resource definition, details/comments see on
// 'core_resource_well_known_core_final'
PRAGMA_IN oc_resource_data_t core_resource_knx_a_sen_data;
const oc_resource_t core_resource_knx_a_sen = {
  (oc_resource_t*)&core_resource_knx_auth_o_replwdo,
  0,
  {NULL, 0, NULL},
  {NULL, sizeof("/a/sen"), "/a/sen"},
  {NULL, 0, NULL},
  {NULL, 0, NULL},
  {APPLICATION_CBOR, CONTENT_NONE},
  OC_DISCOVERABLE,
  // for non defined PUT/POST/DELETE handler use if.none, to return 4.05 instead of 4.01 (unauthorized)
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {oc_core_a_sen_post_handler, NULL, OC_ACL_SEC, OC_IF_SEC},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL},
  {NULL, NULL},
  0,
  0,
  true,
  &core_resource_knx_a_sen_data};
PRAGMA_OUT

void oc_create_a_sen_resource(int resource_idx, size_t device)
{
  OC_DBG("oc_create_a_sen_resource");

  oc_core_populate_resource(resource_idx, device, "/a/sen", APPLICATION_CBOR, CONTENT_NONE, OC_DISCOVERABLE, 0, 0,
                            oc_core_a_sen_post_handler, 0, 0);
}

// ----------------------------------------------------------------------------

// empty ... when 'id' string is ""
static int find_empty_at_index(void)
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

static int find_index_from_at_table_from_id(const oc_string_t* at)
{
  const int len_at = oc_string_len(*at);

  for (int i = 0; i < G_AT_MAX_ENTRIES; i++)
  {
    const int len = oc_string_len(g_at_entries[i].id);
    if (len > 0 && len == len_at && strncmp(oc_string(*at), oc_string(g_at_entries[i].id), len) == 0)
    {
      return i;
    }
  }
  return -1;
}

static int find_index_from_at_string(const char* at, const int len_at)
{
  for (int i = 0; i < G_AT_MAX_ENTRIES; i++)
  {
    const int len = oc_string_len(g_at_entries[i].id);
    if (len > 0 && len == len_at && (strncmp(at, oc_string(g_at_entries[i].id), len) == 0))
    {
      return i;
    }
  }
  return -1;
}

// NULL if none is found in payload
static oc_string_t* find_access_token_id_from_payload(oc_rep_t* object)
{
  while (object)
  {
    switch (object->type)
    {
    case OC_REP_STRING:
    {
      // id is only for type text string defined
      if (oc_string_len(object->name) == 0 && object->iname == 0)
      {
        oc_string_t* index = &object->value.string;
        PRINT("find id from request: %s ", oc_string_checked(*index));
        return index;
      }
    }
    break;
    default:
      break;
    }
    object = object->next;
  }
  PRINT("no id found (error)");
  return NULL;
}

int oc_core_get_at_table_size(void) { return G_AT_MAX_ENTRIES; }

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
  (void)data;
  (void)iface_mask;

  int query_parameter_kvpair_matches = 0; // how many (to this device applicable) query parameter key/value pair
                                          // matches where found
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
  // - total=4, pn 5, ps 20, first entry = 100 -> no data on page 5 (all on page
  // 0)
  // - total=4, pn 1, ps 04, first entry = 004 -> no data on page 1 (all on page
  // 0)
  if (first_entry >= total || query_ps == 0)
  {
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  /// entries don't fit in a single page -> more pages are needed to get the
  /// full list
  // - total=4, page number 1, page size 02, first entry = 002 -> no more data
  // on next page
  // - total=4, page number 1, page size 01, first entry = 001 -> more data on
  // next page
  const bool more_request_needed = total > first_entry + query_ps ? true : false;

  // example </auth/at/token-id>;ct=60 ; must run through entire table since
  // entries are stored randomly
  for (int i = first_entry; i < G_AT_MAX_ENTRIES; i++)
  {
    if (oc_string_len(g_at_entries[i].id) > 0)
    {
      if (response_length > 0)
      {
        // close previous record to create a new without LF (not found in RFC
        // 6690)
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
  (void)data;
  (void)iface_mask;
  oc_rep_t* rep = NULL;

  oc_rep_t* sub_object = NULL;
  oc_rep_t* oscore_object = NULL;

  // default assumption
  oc_status_t return_status = OC_STATUS_BAD_REQUEST;

  bool scope_updated = false;
  bool other_updated = false;

  int index = -1;
  PRINT("oc_core_auth_at_post_handler");

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  // debugging
  oc_print_rep_as_json(request->request_payload, true);

  // set ptr to collection of 1...n ATs in payload
  rep = request->request_payload;
  oc_rep_t* object = NULL;

  while (rep != NULL)
  {
    if (rep->type == OC_REP_OBJECT)
    {
      // check for valid payload (EITT Test 5.3.8.2)
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

      // reassign ...
      object = rep->value.object;
      oc_string_t* at = find_access_token_id_from_payload(object);

      if (at == NULL)
      {
        OC_ERR("access token not found!");
        oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
        return;
      }
      index = find_index_from_at_table_from_id(at);
      if (index != -1)
      {
        OC_INF("entry already exist!");
        return_status = OC_STATUS_CHANGED;
      }
      else
      {
        return_status = OC_STATUS_CREATED;
        index = find_empty_at_index();
        if (index == -1)
        {
          OC_ERR("AT table has no empty slot to add a new entry");
          oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
          return;
        }
      }

      // set AT entry id
      oc_free_string(&(g_at_entries[index].id));
      oc_new_string(&g_at_entries[index].id, oc_string(*at), oc_string_len(*at));

      bool id_only = true; // used to delete the AT table entry

      while (object != NULL)
      {
        if (object->type == OC_REP_STRING_ARRAY)
        {
          // any extra element - even if not valid - causes a "not an id only"
          id_only = false;

          // scope with ACL scope string array from MaC such as ["if.sec", "if.swu"]
          if (object->iname == 9)
          {
            // array of scopes as (32 byte) strings (no useful macro found)
            // - char ptr must iterate each 32 bytes
            // - length must iterate over length / 32
            char* array = object->value.array.ptr;
            const int new_array_size = (int)object->value.array.size / STRING_ARRAY_ITEM_MAX_LEN;

            // set default scope
            oc_acl_mask_t acl_scopes = OC_ACL_NONE;

            // in case of scopes the GA/GA len property is not used
            g_at_entries[index].ga = NULL;
            g_at_entries[index].ga_len = 0;

            for (int i = 0; i < new_array_size; i++)
            {
              // address each string
              char* acl_string = (char*)array + i * STRING_ARRAY_ITEM_MAX_LEN;

              // get bit enum
              oc_acl_mask_t acl_mask = oc_ri_get_scope_mask(acl_string, strlen(acl_string));

              // compact the strings as acl bit mask definitions
              acl_scopes += acl_mask;
            }

            // no scope or a bad scope was set such as if.ll
            if (acl_scopes == OC_ACL_NONE)
            {
              OC_ERR("no valid access scope was set");
              oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
              return;
            }

            // scopes (compacted bits)
            g_at_entries[index].scope = acl_scopes;
            scope_updated = true;
          }
        }
        else if (object->type == OC_REP_INT_ARRAY)
        {
          // any extra element - even if not valid - causes a "not an id only"
          id_only = false;

          // scope with GA integer array from MaC such as [200, 201]
          if (object->iname == 9)
          {
            // in case of GAs (incl size 0) the scope property is not used
            g_at_entries[index].scope = OC_ACL_NONE;

            const int64_t* array = oc_int_array(object->value.array);
            const int new_array_size = oc_int_array_size(object->value.array);

            // malloc of 'zero' byte return pointer is undefined
            uint32_t* new_array = malloc(new_array_size * sizeof(uint32_t));
            if (new_array && new_array_size > 0)
            {
              for (int i = 0; i < new_array_size; i++)
              {
                new_array[i] = (uint32_t)array[i];
              }

              PRINT("ga size %d", new_array_size);

              // release a possible ga array, it will be overwritten,
              // no selective adding (note it releases the org ptr)
              // free ignores NULL ptr
              free(g_at_entries[index].ga);

              g_at_entries[index].ga_len = new_array_size;
              g_at_entries[index].ga = new_array;

              // TODO when observe is implemented for /k check access scope properly
              // check where used...
              g_at_entries[index].scope = OC_ACL_G;
            }
            else
            {
              OC_ERR("out of stack memory");

              oc_prepare_no_format_response_no_payload(request, OC_STATUS_INTERNAL_SERVER_ERROR);
              return;
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
            oc_new_string(&g_at_entries[index].sub, oc_string(object->value.string), oc_string_len(object->value.string));
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
            // profile
            PRINT("profile %d", (int)object->value.integer);
            g_at_entries[index].profile = (int)object->value.integer;
          }
        }
        else if (object->type == OC_REP_OBJECT)
        {
          id_only = false;

          // level of cnf or sub.
          sub_object = object->value.object;
          int sub_object_nr = object->iname;

          PRINT("sub object nr %d", sub_object_nr);
          while (sub_object)
          {
            if (sub_object->type == OC_REP_STRING)
            {
              if (sub_object_nr == 8 && sub_object->iname == 3)
              {
                // cnf:kid (8:3)
                oc_free_string(&(g_at_entries[index].kid));
                oc_new_string(&g_at_entries[index].kid, oc_string(sub_object->value.string),
                              oc_string_len(sub_object->value.string));
              }
            }
            else if (sub_object->type == OC_REP_OBJECT)
            {
              oscore_object = sub_object->value.object;
              int oscore_object_nr = sub_object->iname;
              while (oscore_object)
              {
                if (oscore_object->type == OC_REP_INT)
                {
                  if (sub_object_nr == 8 && oscore_object_nr == 4 && oscore_object->iname == 4)
                  {
                    // only support value 10 at the moment, EITT test 5.3.19.3
                    if ((int)object->value.integer != 10)
                    {
                      OC_ERR("algorithm is not 10 : %d", (int)object->value.integer);
                      return_status = OC_STATUS_BAD_REQUEST;
                    }
                    other_updated = true;
                  }
                }
                if (oscore_object->type == OC_REP_BYTE_STRING)
                {
                  if (sub_object_nr == 8 && oscore_object_nr == 4 && oscore_object->iname == 2)
                  {
                    // cnf:osc:ms
                    oc_free_string(&(g_at_entries[index].osc_ms));
                    oc_new_byte_string(&g_at_entries[index].osc_ms, oc_string(oscore_object->value.string),
                                       oc_string_len(oscore_object->value.string));
                    other_updated = true;
                  }
                  if (sub_object_nr == 8 && oscore_object_nr == 4 && oscore_object->iname == 6)
                  {
                    // cnf:osc:contextId
                    oc_free_string(&(g_at_entries[index].osc_contextid));
                    oc_new_byte_string(&g_at_entries[index].osc_contextid, oc_string(oscore_object->value.string),
                                       oc_string_len(oscore_object->value.string));
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
                  if (sub_object_nr == 8 && oscore_object_nr == 4 && oscore_object->iname == 0)
                  {
                    // cnf:osc:id
                    oc_free_string(&(g_at_entries[index].osc_id));
                    oc_new_byte_string(&g_at_entries[index].osc_id, oc_string(oscore_object->value.string),
                                       oc_string_len(oscore_object->value.string));
                    other_updated = true;
                  }
                  if (sub_object_nr == 8 && oscore_object_nr == 4 && oscore_object->iname == 5)
                  {
                    // cnf:osc:salt
                    oc_free_string(&(g_at_entries[index].osc_salt));
                    oc_new_byte_string(&g_at_entries[index].osc_salt, oc_string(oscore_object->value.string),
                                       oc_string_len(oscore_object->value.string));
                    other_updated = true;
                  }
                }

                oscore_object = oscore_object->next;
              }
            }
            sub_object = sub_object->next;
          }
        }
        object = object->next;
      }

      if (id_only)
      {
        PRINT("only found id in request, deleting entry at index: %d", index);
        oc_delete_at_table_entry(index);
      }
      else
      {
        PRINT("storage index: %d (%s) ", index, oc_string_checked(*at));
        oc_print_auth_at_entry(index);
        oc_store_at_table_entry(index);
      }
    }
    rep = rep->next;
  }

  PRINT("activating oscore context");
  // add the oscore contexts by reinitializing all used oscore keys.
  // do not update the oscore when update of the scope contents only
  if (return_status == OC_STATUS_CHANGED && other_updated == false && scope_updated == true)
  {
    OC_WRN("update scope only");
  }
  else
  {
    // update the oscore context
    oc_init_oscore_from_storage(false);
  }

  // the last return status from a collection with 'n' POST elements is responded (CREATED/CHANGED)
  oc_prepare_no_format_response_no_payload(request, return_status);
  PRINT("oc_core_auth_at_post_handler - end");
}

static void oc_core_auth_at_delete_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;
  PRINT("oc_core_auth_at_delete_handler - start");

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  oc_delete_at_table();

  oc_prepare_no_format_response_no_payload(request, OC_STATUS_DELETED);
  PRINT("oc_core_auth_at_delete_handler - end");
}

// resource definition, details/comments see on
// 'core_resource_well_known_core_final'
extern const oc_resource_t core_resource_knx_auth_at_x;
PRAGMA_IN oc_resource_data_t core_resource_knx_auth_at_data;
const oc_resource_t core_resource_knx_auth_at = {
  (oc_resource_t*)&core_resource_knx_auth_at_x,
  0,
  {NULL, 0, NULL},
  {NULL, sizeof("/auth/at"), "/auth/at"},
  {NULL, (size_t)1 * 32, ((char[1][32]){"urn:knx:fb.at"})},
  {NULL, 0, NULL},
  {APPLICATION_LINK_FORMAT, CONTENT_NONE},
  OC_DISCOVERABLE,
  // for non defined PUT/POST/DELETE handler use if.none, to return 4.05 instead of 4.01 (unauthorized)
  {oc_core_auth_at_get_handler, NULL, OC_ACL_P | OC_ACL_D | OC_ACL_C, OC_IF_LI},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {oc_core_auth_at_post_handler, NULL, OC_ACL_SEC, OC_IF_SEC},
  {oc_core_auth_at_delete_handler, NULL, OC_ACL_SEC, OC_IF_SEC},
  {NULL, NULL},
  {NULL, NULL},
  0,
  0,
  true,
  &core_resource_knx_auth_at_data};
PRAGMA_OUT

void oc_create_auth_at_resource(int resource_idx, size_t device)
{
  oc_core_populate_resource(resource_idx, device, "/auth/at", APPLICATION_LINK_FORMAT, CONTENT_NONE, OC_DISCOVERABLE,
                            oc_core_auth_at_get_handler, 0, oc_core_auth_at_post_handler, oc_core_auth_at_delete_handler, 1,
                            "urn:knx:fb.at");
}

// ----------------------------------------------------------------------------

static void oc_core_auth_at_x_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;
  const char* value;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }
  PRINT("oc_core_auth_at_x_get_handler - start");

  // find the id part from the invoked URL auth/at/xyz...123
  int value_len =
    oc_uri_get_wildcard_value_as_string(
      oc_string(request->resource->uri), oc_string_len(request->resource->uri),
      request->uri_path, request->uri_path_len, 
      &value);

  // no access token string found
  if (value_len <= 0)
  {
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
    PRINT("AT string not found");
    return;
  }

  PRINT("id = %.*s", value_len, value);

  // get the AT index from access string token 
  int index = find_index_from_at_string(value, value_len);
  if (index < 0)
  {
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
    PRINT("AT index not found");
    return;
  }

  // debugging
  oc_print_auth_at_entry(index);

  oc_rep_begin_root_object();

  // profile : 38
  oc_rep_i_set_int(root, 38, g_at_entries[index].profile);

  // id : 0
  oc_rep_i_set_text_string(root, 0, oc_string(g_at_entries[index].id));

  // scope : 9 (either list of GAS is used or acl scopes)
  if (g_at_entries[index].ga_len > 0)
  {
    // list of GAs is used
    oc_rep_i_set_int_array(root, 9, g_at_entries[index].ga, g_at_entries[index].ga_len);
  }
  else
  {
    // compacted list of scopes is used (specification says "interfaces" but scopes are meant)
    const unsigned int nr_entries = oc_count_total_scopes_in_mask(g_at_entries[index].scope);
    if (nr_entries > 0)
    {
      PRINT("%u scope entries", nr_entries);

      // scope list
      oc_string_array_t scopes;

      oc_new_string_array(&scopes, nr_entries);
      oc_put_scopes_from_mask_in_string_array(g_at_entries[index].scope, scopes);
      oc_rep_i_set_string_array(root, 9, scopes);

      oc_free_string_array(&scopes);
    }
  }

  if (g_at_entries[index].profile == OC_PROFILE_COAP_OSCORE || g_at_entries[index].profile == OC_PROFILE_COAP_PASE)
  {
    // create cnf map (8)
    oc_rep_i_set_key(&root_map, 8) CborEncoder cnf_map;
    cbor_encoder_create_map(&root_map, &cnf_map, CborIndefiniteLength);

    // create osc map (4)
    oc_rep_i_set_key(&cnf_map, 4) CborEncoder osc_map;
    cbor_encoder_create_map(&cnf_map, &osc_map, CborIndefiniteLength);

    if (oc_string_len(g_at_entries[index].osc_ms) > 0)
    {
      // root::cnf::osc::ms
      oc_rep_i_set_byte_string(osc, 2, oc_byte_string(g_at_entries[index].osc_ms),
                               oc_byte_string_len(g_at_entries[index].osc_ms)); 
    }

    if (oc_string_len(g_at_entries[index].osc_contextid) > 0)
    {
      // root::cnf::osc::contextid
      oc_rep_i_set_byte_string(osc, 6, oc_byte_string(g_at_entries[index].osc_contextid),
                               oc_byte_string_len(g_at_entries[index].osc_contextid)); 
    }

    if (oc_string_len(g_at_entries[index].osc_id) > 0)
    {
      // root::cnf::osc::osc_id
      oc_rep_i_set_byte_string(osc, 0, oc_byte_string(g_at_entries[index].osc_id),
                               oc_byte_string_len(g_at_entries[index].osc_id)); 
    }

    cbor_encoder_close_container_checked(&cnf_map, &osc_map);
    cbor_encoder_close_container_checked(&root_map, &cnf_map);
  }

  oc_rep_end_root_object();

  oc_prepare_cbor_response(request, OC_STATUS_OK);
  PRINT("oc_core_auth_at_x_get_handler - end");
}

static void oc_core_auth_at_x_delete_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;
  const char* value;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }
  PRINT("oc_core_auth_at_x_delete_handler - start");

  // find the id part from the invoked URL auth/at/xyz...123
  int value_len = oc_uri_get_wildcard_value_as_string(
    oc_string(request->resource->uri), oc_string_len(request->resource->uri),
    request->uri_path, request->uri_path_len, 
    &value);

  // no access token string found
  if (value_len <= 0)
  {
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
    PRINT("AT string not found");
    return;
  }
  PRINT("id = %.*s", value_len, value);

  // get the AT index from access string token 
  int index = find_index_from_at_string(value, value_len);

  if (index < 0)
  {
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
    PRINT("AT index not found");
    return;
  }

  PRINT("delete AT table index");
  oc_delete_at_table_entry(index);

#ifdef OC_OSCORE
  // delete the related oscore contexts
  oc_oscore_free_contexts_at_id(index);
#endif

  PRINT("oc_core_auth_at_x_delete_handler - done");
  oc_prepare_no_format_response_no_payload(request, OC_STATUS_DELETED);
}

// resource definition, details/comments see on
// 'core_resource_well_known_core_final'
extern const oc_resource_t core_resource_knx_auth;
PRAGMA_IN oc_resource_data_t core_resource_knx_auth_at_x_data;
const oc_resource_t core_resource_knx_auth_at_x = {
  (oc_resource_t*)&core_resource_knx_auth,
  0,
  {NULL, 0, NULL},
  {NULL, sizeof("/auth/at/*"), "/auth/at/*"},
  {NULL, (size_t)1 * 32, ((char[1][32]){"dpt.a[n]"})},
  {NULL, 0, NULL},
  {APPLICATION_CBOR, CONTENT_NONE},
  OC_DISCOVERABLE,
  // for non defined PUT/POST/DELETE handler use if.none, to return 4.05 instead of 4.01 (unauthorized)
  {oc_core_auth_at_x_get_handler, NULL, OC_ACL_SEC, OC_IF_SEC},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {oc_core_auth_at_x_delete_handler, NULL, OC_ACL_SEC, OC_IF_SEC},
  {NULL, NULL},
  {NULL, NULL},
  0,
  0,
  true,
  &core_resource_knx_auth_at_x_data};
PRAGMA_OUT

void oc_create_auth_at_x_resource(int resource_idx, size_t device)
{
  OC_DBG("oc_create_auth_at_x_resource");

  oc_core_populate_resource(resource_idx, device, "/auth/at/*", APPLICATION_CBOR, CONTENT_NONE, OC_DISCOVERABLE,
                            oc_core_auth_at_x_get_handler, 0, 0, oc_core_auth_at_x_delete_handler, 1, "dpt.a[n]");
}

// ----------------------------------------------------------------------------

static void oc_core_knx_auth_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  int query_parameter_kvpair_matches = 0; // how many (to this device applicable) query parameter key/value pair
                                          // matches where found
  size_t response_length = 0;
  int query_pn = PAGE_NUMBER;
  int query_ps = PAGE_SIZE;

  int first_entry = OC_KNX_AUTH_O; // first entry number of a resource that will
                                   // be placed on a page
  int last_entry = OC_KNX_AUTH_AT_X; // last entry number of a resource that
                                     // will be placed on a page
  int total = last_entry - first_entry; // total entries of this resource
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
  // - total=4, pn 5, ps 20, first entry = 100 -> no data on page 5 (all on page
  // 0)
  // - total=4, pn 1, ps 04, first entry = 004 -> no data on page 1 (all on page
  // 0)
  if (first_entry >= last_entry || query_ps == 0)
  {
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  // entries don't fit in a single page -> more pages are needed to get the full
  // list
  // - total=4, page number 1, page size 02, first entry = 002 -> no more data
  // on next page
  // - total=4, page number 1, page size 01, first entry = 001 -> more data on
  // next page
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

// resource definition, details/comments see on
// 'core_resource_well_known_core_final'
extern const oc_resource_t core_resource_knx_fp_gm;
PRAGMA_IN oc_resource_data_t core_resource_knx_auth_data;
const oc_resource_t core_resource_knx_auth = {
  (oc_resource_t*)&core_resource_knx_fp_gm,
  0,
  {NULL, 0, NULL},
  {NULL, sizeof("/auth"), "/auth"},
  {NULL, 0, NULL},
  {NULL, 0, NULL},
  {APPLICATION_LINK_FORMAT, CONTENT_NONE},
  OC_DISCOVERABLE,
  // for non defined PUT/POST/DELETE handler use if.none, to return 4.05 instead of 4.01 (unauthorized)
  {oc_core_knx_auth_get_handler, NULL, OC_ACL_P | OC_ACL_D | OC_ACL_C, OC_IF_LI},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL},
  {NULL, NULL},
  0,
  0,
  true,
  &core_resource_knx_auth_data};
PRAGMA_OUT
#else

// resource definition, details/comments see on
// 'core_resource_well_known_core_final'
extern const oc_resource_t core_resource_well_known_core;
PRAGMA_IN oc_resource_data_t core_resource_knx_auth_data;
const oc_resource_t core_resource_knx_auth = {
  (oc_resource_t*)&core_resource_well_known_core,
  0,
  {NULL, 0, NULL},
  {NULL, sizeof("/auth"), "/auth"},
  {NULL, 0, NULL},
  {NULL, 0, NULL},
  {APPLICATION_LINK_FORMAT, CONTENT_NONE},
  OC_DISCOVERABLE,
  // for non defined PUT/POST/DELETE handler use if.none, to return 4.05 instead of 4.01 (unauthorized)
  {oc_core_knx_auth_get_handler, NULL, OC_ACL_P | OC_ACL_D | OC_ACL_C, OC_IF_LI},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL},
  {NULL, NULL},
  0,
  0,
  true,
  &core_resource_knx_auth_data};
PRAGMA_OUT
#endif

void oc_create_knx_auth_resource(int resource_idx, size_t device)
{
  OC_DBG("oc_create_knx_auth_resource");
  oc_core_populate_resource(resource_idx, device, "/auth", APPLICATION_LINK_FORMAT, CONTENT_NONE, OC_DISCOVERABLE,
                            oc_core_knx_auth_get_handler, 0, 0, 0, 0);
}

void oc_print_auth_at_entry(int index)
{
#ifdef OC_PRINT

  if (index > -1)
  {
    if (oc_string_len(g_at_entries[index].id) > 0)
    {
      PRINT("at index      : %d", index);
      PRINT("id (0)        : %s", oc_string_checked(g_at_entries[index].id));
      PRINT("scope (9)     : %d", g_at_entries[index].scope);
      PRINT("profile (38)  : %d (%s)", g_at_entries[index].profile, oc_at_profile_to_string(g_at_entries[index].profile));
      
      if (g_at_entries[index].profile == OC_PROFILE_COAP_OSCORE || g_at_entries[index].profile == OC_PROFILE_COAP_PASE)
      {
        if (oc_string_len(g_at_entries[index].osc_ms) > 0)
        {
          PRINT("osc:ms (h)    : (%d) ", (int)oc_byte_string_len(g_at_entries[index].osc_ms));
          oc_string_println_hex(g_at_entries[index].osc_ms);
        }
        if (oc_string_len(g_at_entries[index].osc_salt) > 0)
        {
          PRINT("osc:salt (h)  : (%d) ", (int)oc_byte_string_len(g_at_entries[index].osc_salt));
          oc_string_println_hex(g_at_entries[index].osc_salt);
        }
        if (oc_string_len(g_at_entries[index].osc_contextid) > 0)
        {
          PRINT("osc:ctx_id (h): (%d) ", (int)oc_byte_string_len(g_at_entries[index].osc_contextid));
          oc_string_println_hex(g_at_entries[index].osc_contextid);
        }
        if (oc_string_len(g_at_entries[index].osc_id) > 0)
        {
          PRINT("osc:id (h)    : (%d) ", (int)oc_byte_string_len(g_at_entries[index].osc_id));
          oc_string_println_hex(g_at_entries[index].osc_id);
        }
        if (g_at_entries[index].ga_len > 0)
        {
          PRINT("osc:ga        : [");
          for (int i = 0; i < g_at_entries[index].ga_len; i++)
          {
            PRINTF("%" PRIu64 "", (uint64_t)g_at_entries[index].ga[i]);
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
  return index < 0 || index > G_AT_MAX_ENTRIES - 1 ? OC_ACL_NONE : g_at_entries[index].scope;
}

int oc_delete_at_table_entry(int entry)
{
  if (entry < 0 || entry > G_AT_MAX_ENTRIES - 1)
    return -1;

  // AT file entry
  char filename[AT_SIZE];
  (void)snprintf(filename, AT_SIZE, "%s_%d", AT_STORE, entry);
  oc_storage_erase(filename);

  // generic data
  oc_free_string(&g_at_entries[entry].id);
  oc_new_string(&g_at_entries[entry].id, "", 0);
  g_at_entries[entry].scope = OC_ACL_NONE;
  g_at_entries[entry].profile = OC_PROFILE_UNKNOWN;

  // oscore object
  oc_free_string(&g_at_entries[entry].osc_ms);
  oc_new_byte_string(&g_at_entries[entry].osc_ms, "", 0);

  oc_free_string(&g_at_entries[entry].osc_salt);
  oc_new_byte_string(&g_at_entries[entry].osc_salt, "", 0);

  oc_free_string(&g_at_entries[entry].osc_contextid);
  oc_new_byte_string(&g_at_entries[entry].osc_contextid, "", 0);

  oc_free_string(&g_at_entries[entry].osc_id);
  oc_new_byte_string(&g_at_entries[entry].osc_id, "", 0);

  // TLS object
  oc_free_string(&g_at_entries[entry].sub);
  oc_new_string(&g_at_entries[entry].sub, "", 0);
  oc_free_string(&g_at_entries[entry].kid);
  oc_new_string(&g_at_entries[entry].kid, "", 0);

  // release a possible ga array
  // free ignores NULL ptr
  free(g_at_entries[entry].ga);

  g_at_entries[entry].ga = NULL;
  g_at_entries[entry].ga_len = 0;

  return 0;
}

/*
  store AT table data in CBOR (hex stream data), storage of the fields is done
  via the cbor keys in their hierarchy, example tag cnf:osc:ms = 8:4:2 = 842
*/
static void oc_store_at_table_entry(int entry)
{
#ifndef OC_USE_STORAGE
  (void)entry;
  PRINT("no storage for the AT table enabled");
#else

  char filename[AT_SIZE];
  (void)snprintf(filename, AT_SIZE, "%s_%d", AT_STORE, entry);

  uint8_t* buf = malloc(OC_MAX_APP_DATA_SIZE);
  if (!buf)
    return;

  oc_rep_new(buf, OC_MAX_APP_DATA_SIZE);

  // write the data
  oc_rep_begin_root_object();

  // 0: id
  oc_rep_i_set_text_string(root, 0, oc_string(g_at_entries[entry].id));

  // 2: sub
  oc_rep_i_set_text_string(root, 2, oc_string(g_at_entries[entry].sub));

  // 9: acl scope (compacted bits)
  oc_rep_i_set_int(root, 9, g_at_entries[entry].scope);

  // 38: profile
  oc_rep_i_set_int(root, 38, g_at_entries[entry].profile);

  // 83: kid
  oc_rep_i_set_text_string(root, 83, oc_string(g_at_entries[entry].kid));

  // 84x: cnf:osc:xx map
  oc_rep_i_set_byte_string(root, 840, oc_string(g_at_entries[entry].osc_id), oc_byte_string_len(g_at_entries[entry].osc_id));
  oc_rep_i_set_byte_string(root, 842, oc_string(g_at_entries[entry].osc_ms), oc_byte_string_len(g_at_entries[entry].osc_ms));
  oc_rep_i_set_byte_string(root, 845, oc_string(g_at_entries[entry].osc_salt),
                           oc_byte_string_len(g_at_entries[entry].osc_salt));
  oc_rep_i_set_byte_string(root, 846, oc_string(g_at_entries[entry].osc_contextid),
                           oc_byte_string_len(g_at_entries[entry].osc_contextid));

  // 777: ga's
  oc_rep_i_set_int_array(root, 777, g_at_entries[entry].ga, g_at_entries[entry].ga_len);

  oc_rep_end_root_object();

  const int size = oc_rep_get_encoded_payload_size();
  if (size > 0)
  {
    OC_DBG("stored current state [%s] [%d]: size %d", filename, entry, size);

    long written_size = oc_storage_write(filename, buf, size);
    if (written_size != (long)size)
    {
      PRINT("entry: [%s] written %d != %d (to write)", filename, (int)written_size, size);
    }
  }
  free(buf);
#endif
}

static void oc_load_at_table_entry(int entry)
{
  char filename[AT_SIZE];
  (void)snprintf(filename, AT_SIZE, "%s_%d", AT_STORE, entry);

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

          // 9 - scope (compacted bits)
          if (rep->iname == 9)
          {
            g_at_entries[entry].scope = (int)rep->value.integer;
          }

          // 38 - profile
          if (rep->iname == 38)
          {
            g_at_entries[entry].profile = (int)rep->value.integer;
          }
          break;
        case OC_REP_STRING:

          // 0 - id
          if (rep->iname == 0)
          {
            oc_free_string(&g_at_entries[entry].id);
            oc_new_string(&g_at_entries[entry].id, oc_string(rep->value.string), oc_string_len(rep->value.string));
          }

          // 2 - sub
          if (rep->iname == 2)
          {
            oc_free_string(&g_at_entries[entry].sub);
            oc_new_string(&g_at_entries[entry].sub, oc_string(rep->value.string), oc_string_len(rep->value.string));
          }

          // 83 - kid
          if (rep->iname == 83)
          {
            oc_free_string(&g_at_entries[entry].kid);
            oc_new_string(&g_at_entries[entry].kid, oc_string(rep->value.string), oc_string_len(rep->value.string));
          }
          break;
        case OC_REP_BYTE_STRING:

          // reading back the strings should be done with oc_string_len, not with oc_byte_string_len
          // string_len = len - 1 to exclude the '\0' from rep object; byte string len includes it

          if (rep->iname == 840)
          {
            oc_free_string(&g_at_entries[entry].osc_id);
            oc_new_byte_string(&g_at_entries[entry].osc_id, oc_string(rep->value.string), oc_string_len(rep->value.string));
          }
          if (rep->iname == 842)
          {
            oc_free_string(&g_at_entries[entry].osc_ms);
            oc_new_byte_string(&g_at_entries[entry].osc_ms, oc_string(rep->value.string), oc_string_len(rep->value.string));
          }

          if (rep->iname == 845)
          {
            oc_free_string(&g_at_entries[entry].osc_salt);
            oc_new_byte_string(&g_at_entries[entry].osc_salt, oc_string(rep->value.string),
                               oc_string_len(rep->value.string));
          }

          if (rep->iname == 846)
          {
            oc_free_string(&g_at_entries[entry].osc_contextid);
            oc_new_byte_string(&g_at_entries[entry].osc_contextid, oc_string(rep->value.string),
                               oc_string_len(rep->value.string));
          }
          break;
        case OC_REP_INT_ARRAY:

          // ga array with GAs from MaC
          if (rep->iname == 777)
          {
            // storage array + len
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
              free(g_at_entries[entry].ga);

              PRINT("ga size %d", new_array_size);

              // assign only when the new array is allocated correctly
              g_at_entries[entry].ga_len = new_array_size;
              g_at_entries[entry].ga = new_array;
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

int oc_core_find_at_entry_with_id(char* id)
{
  for (int i = 0; i < G_AT_MAX_ENTRIES; i++)
  {
    if ((oc_string_len(g_at_entries[i].id) > 0) && (strncmp(oc_string(g_at_entries[i].id), id, strlen(id)) == 0))
    {
      return i;
    }
  }
  return -1;
}

void oc_core_find_and_remove_pase_token_in_at_table(void)
{
  for (int i = 0; i < G_AT_MAX_ENTRIES; i++)
  {
    if (g_at_entries[i].profile == OC_PROFILE_COAP_PASE)
    {
      oc_delete_at_table_entry(i); // delete entry from AT table
      oc_oscore_free_contexts_at_id(i); // removes possible references
      PRINT("PASE key found, invalidated...");
      return;
    }
  }
  PRINT("PASE key NOT found, hence NOT invalidated...");
}

int oc_core_find_at_entry_with_osc_id(uint8_t* osc_id, size_t osc_id_len)
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

int oc_core_find_at_entry_empty_slot(void)
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

void oc_load_at_table(void)
{
  PRINT("Loading AT Table from persistent storage");
  for (int i = 0; i < G_AT_MAX_ENTRIES; i++)
  {
    oc_load_at_table_entry(i);
    oc_print_auth_at_entry(i);
  }
  // create the oscore contexts
  oc_init_oscore_from_storage(true);
}

void oc_delete_at_table(void)
{
  PRINT("Deleting 'all' AT Object Table entries from RAM and storage");

  for (int i = 0; i < G_AT_MAX_ENTRIES; i++)
  {
    oc_print_auth_at_entry(i);
    oc_delete_at_table_entry(i);
  }
#ifdef OC_OSCORE
  oc_oscore_free_all_contexts();
#endif
}

void oc_delete_at_table_except_sec_scope_entries(void)
{
  PRINT("Deleting 'non if.sec' AT Object Table entries from RAM and storage");

  // reset the entries that are not "if.sec"
  for (int i = 0; i < G_AT_MAX_ENTRIES; i++)
  {
    const oc_acl_mask_t scope = oc_at_get_scope_mask(i);

    if (!(scope & OC_ACL_SEC))
    {
      // delete the entries that are not including "if.sec"
      oc_print_auth_at_entry(i);
      oc_delete_at_table_entry(i);
    }
  }
#ifdef OC_OSCORE
  // (re)create the oscore contexts in the table that still remains
  oc_init_oscore_from_storage(true);
#endif
}

// ----------------------------------------------------------------------------

void oc_oscore_set_auth_shared(char* client_senderid, int client_senderid_size,
                               uint8_t* shared_key, int shared_key_size)
{
  // local tmp token id
  oc_string_t pase_token_id = {NULL,0,NULL};

  // id
  oc_new_string(&pase_token_id, client_senderid, client_senderid_size);

  int index = oc_core_find_at_entry_with_id(oc_string(pase_token_id));

  if (index == -1)
  {
    // no pase token present in AT table (normal case)
    index = oc_core_find_at_entry_empty_slot();
  }

  if (index == -1)
  {
    OC_ERR("no space left in AT table");
  }
  else
  {
    // write (OR overwrite) the above defined pase entry to AT table (RAM) and file storage
    // here the 'index' cannot be >= G_AT_MAX_ENTRIES 

    // id (use local temp id)
    oc_free_string(&g_at_entries[index].id);
    oc_new_string(&g_at_entries[index].id, oc_string(pase_token_id), oc_string_len(pase_token_id));

    // pase token owns only if.sec scope 
    g_at_entries[index].scope = OC_ACL_SEC;
    g_at_entries[index].profile = OC_PROFILE_COAP_PASE;

    // no sub
    oc_free_string(&g_at_entries[index].sub);
    oc_new_string(&g_at_entries[index].sub,"",0);

    // no kid
    oc_free_string(&g_at_entries[index].kid);
    oc_new_string(&g_at_entries[index].kid, "" ,0);

    // secret
    oc_free_string(&g_at_entries[index].osc_ms);
    oc_new_byte_string(&g_at_entries[index].osc_ms, (char*)shared_key, shared_key_size);

    // no kid context
    oc_free_string(&g_at_entries[index].osc_contextid);
    oc_new_byte_string(&g_at_entries[index].osc_contextid, "",0);

    // kid (on the wire it was a byte string, so we have to store the byte string)
    oc_free_string(&g_at_entries[index].osc_id);
    oc_new_byte_string(&g_at_entries[index].osc_id, client_senderid, client_senderid_size);

    // release a possible ga array, it will be overwritten
    // free ignores NULL ptr
    free(g_at_entries[index].ga);
    g_at_entries[index].ga_len = 0;
    g_at_entries[index].ga = NULL;

    // store
    oc_store_at_table_entry(index);

    // init
    oc_init_oscore_from_storage(false);
  }

  // free allocated memory from local pase token id
  oc_free_string(&pase_token_id);
}

void oc_oscore_set_auth_device(char* client_senderid, int client_senderid_size,
                               uint8_t* shared_key, int shared_key_size)
{
  // - create the token & store in at table (usually at position 0)
  // - note there should be no entries, if there is an entry then overwrite it
  // - it is a by MaC freely chosen id 
  PRINT("set id : (%2d) ", client_senderid_size);
  oc_char_println_hex(client_senderid, client_senderid_size);

  PRINT("set ms : (%2d) ", shared_key_size);
  oc_char_println_hex(shared_key, shared_key_size);

  oc_oscore_set_auth_shared(client_senderid, client_senderid_size, shared_key, shared_key_size);
}

oc_auth_at_t* oc_get_auth_at_entry(int index)
{
  return index < 0 || index >= G_AT_MAX_ENTRIES ? NULL : &g_at_entries[index];
}

void oc_create_knx_sec_resources(size_t device_index)
{
  OC_DBG("oc_create_knx_sec_resources");

  oc_load_at_table();

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

void oc_init_oscore_from_storage(const bool read_ssn_from_storage)
{
#ifdef OC_OSCORE

  OC_DBG_OSCORE("... activating OSCORE credentials");
  OC_DBG_OSCORE("... removing all present OSCORE sender contexts");

  oc_oscore_free_sender_contexts();

  OC_DBG_OSCORE("... adding OSCORE contexts from AT table");
  for (int i = 0; i < G_AT_MAX_ENTRIES; i++)
  {
    if (oc_string_len(g_at_entries[i].id) > 0)
    {
      oc_print_auth_at_entry(i);

      if (g_at_entries[i].profile == OC_PROFILE_COAP_OSCORE || g_at_entries[i].profile == OC_PROFILE_COAP_PASE)
      {

        // TODO read ssn from storage and pass to add ctx below 

        // REQUEST CLIENT SIDE
        // create oscore REQUEST sender context
        // create oscore RESPONSE recipient context
        OC_DBG_OSCORE("... client for outgoing request: adding oscore REQUEST sender context + RESPONSE recipient context with Sender ID : ");
        oc_char_println_hex(oc_string(g_at_entries[i].osc_id), oc_byte_string_len(g_at_entries[i].osc_id));

        oc_oscore_context_t* ctx = oc_oscore_add_context(
          oc_string(g_at_entries[i].osc_id), oc_byte_string_len(g_at_entries[i].osc_id),
          "", 0, 
          0, 
          oc_string(g_at_entries[i].osc_ms), oc_byte_string_len(g_at_entries[i].osc_ms), 
          oc_string(g_at_entries[i].osc_salt), oc_byte_string_len(g_at_entries[i].osc_salt),
          oc_string(g_at_entries[i].osc_contextid), oc_byte_string_len(g_at_entries[i].osc_contextid),
          i,
          read_ssn_from_storage);

        if (ctx == NULL)
        {
          OC_ERR("failed to add a context entry for AT table entry = %d", i);
        }

        // - contexts for sending have populated sender id and null receive id
        // - the spake key, however, is for receiving only, and the osc_id is already inside receiver_id.

        // posts to auth/at set id into the sender id, so the created contexts
        // are only usable for sending. recipient contexts are created
        // dynamically with the key from the access token matching the key ID,
        // and with the context id within the received request
      }
      else
      {
        OC_DBG_OSCORE("no oscore context was initialized");
      }
    }
  }
#endif
}

// used for API test only
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
  // uri len of resource versus uri len of caller endpoint was checked before
  if (called_res_scope == OC_ACL_NONE)
  {
    // not a secure resource, access allowed
    // see table 5.1.3, all resources with methods that do not have any scope (=
    // 'none')
    return true;
  }

  PRINT("method allowed flags:");
  PRINTipaddr_flags(*endpoint);

#ifdef OC_OSCORE

  if ((endpoint->flags & OSCORE + OSCORE_DECRYPTED) != OSCORE + OSCORE_DECRYPTED)
  {
    // not a OSCORE message that was able to decrypt with given security context (CCM, MAC)
    OC_DBG_OSCORE("access denied for: %s [%s] with flags: %d", get_method_name(method), oc_string_checked(resource->uri),
                  endpoint->flags);
    return false;
  }

  // Example for auth/o sub resource
  // MAC writes - on tool key - if.p/d/c/sec/swu scopes in e.g.;
  //     aut/at/2 acl table entry (never an interface like if.ll or if.b)
  // DEV owns for each method AND resource a 'precompiled' acl and
  //     interface definition, e.g.; EP auth/o GET = acl : if.p/d/c, interface = if.ll
  //
  // The stack compares for the given method the auth/at acl scope with the
  // resource acl scope, at least one bit match = ok, otherwise 4.00 (bad
  // request) response.

  // caller scope, e.g. of the auth/at table entry that was used to decrypt the
  // message on OC_OSCORE defined the 'auth_at_index' is always incremented by
  // + 1 , so no extra sanity check here is needed
  const oc_acl_mask_t caller_acl_scope = oc_at_get_scope_mask(endpoint->auth_at_index - 1);

  // bitwise 'and' -> at least one scope must match
  if (!(caller_acl_scope & called_res_scope))
  {
    PRINT("access to %s unauthorized: request scope=%d; resource scope=%d :", oc_string(resource->uri), caller_acl_scope,
          called_res_scope);

    oc_print_acl_scopes(caller_acl_scope);
    oc_print_acl_scopes(called_res_scope);

    OC_WRN("resource call denied");

    return false;
  }

#endif

  // acl scopes + method checked or OC_OSCORE is off
  return true;
}
