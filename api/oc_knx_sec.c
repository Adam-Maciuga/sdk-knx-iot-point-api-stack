/* 
 * Copyright (c) 2021-2023 Cascoda Ltd
 * Copyright (c) 2024-2026 KNX Association
 *            
 * SPDX-License-Identifier: Apache-2.0
 */

#define __STDC_FORMAT_MACROS // defined to use format specifiers also in C++

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include "api/oc_knx_sec.h"
#include "oc_api.h"
#include "oc_core_res.h"
#include "oc_discovery.h"
#include "security/oc_oscore_context.h"
#include "oc_helpers.h"
#include "oc_knx_helpers.h"
#include "oc_replay.h"
#include "messaging/coap/oscore_constants.h"
#include "port/oc_storage.h"

// forward declarations for the resource chain
extern const oc_resource_t core_resource_knx_auth_o;
extern const oc_resource_t core_resource_knx_auth_o_replwdo;
extern const oc_resource_t core_resource_a_sen;
extern const oc_resource_t core_resource_knx_auth_at;
extern const oc_resource_t core_resource_knx_auth_at_x;
extern const oc_resource_t core_resource_knx_auth;
extern const oc_resource_t core_resource_well_known_core;

// AT storage data
#define AT_STORE "at_store"
#define AT_SIZE (sizeof(AT_STORE) + 6) // support of '_99999' at FILE entries

// static const, maybe changed in a later stack version
static const uint16_t g_oscore_replay_window_size = 32;   // default according to RFC OSCORE

// static RAM variables 
static uint16_t g_oscore_osn_delay_ms = 1000;             // format = dpt.TimePeriodMsec (U16)
static oc_auth_at_t g_at_entries[G_AT_MAX_ENTRIES];       // init all with '0' (also in at table included strings next/ptr/size)

static void oc_store_at_table_entry(oc_auth_at_t* entry);

uint32_t get_oscore_replay_window_size(void) { return g_oscore_replay_window_size;}
uint16_t get_oscore_osn_delay_ms(void) { return g_oscore_osn_delay_ms;}
void set_oscore_osn_delay_ms(uint16_t milliseconds) { g_oscore_osn_delay_ms = milliseconds; }
int32_t get_at_index(const oc_auth_at_t* entry)
{
  if (!entry)
  {
    return -1;
  }
  const int32_t index = (int32_t)(entry - g_at_entries);
  return index >= 0 && index < G_AT_MAX_ENTRIES ? index : -1;
}

/*
  Builds the storage key for an SSN entry as 'ssn_<osc_id_hex>_<contextid_hex>'.
  The caller must pre-initialize storage_name[OSCORE_STORAGE_KEY_LEN] with OSCORE_STORAGE_PREFIX.
  Example: sender id 0x0001, id context 0x200105b7fd8e -> "ssn_0001_200105b7fd8e"
*/
static void oc_build_ssn_storage_name(const oc_auth_at_t* entry, char* storage_name)
{
  const uint8_t* sender_id = (const uint8_t*)oc_string(entry->osc_id);
  const size_t sender_id_len = oc_byte_string_len(entry->osc_id);
  const uint8_t* id_context = (const uint8_t*)oc_string(entry->osc_contextid);
  const size_t id_context_len = oc_byte_string_len(entry->osc_contextid);
  size_t storage_name_len;

  // add 'Sender ID' in hex behind prefix that is something like 'ssn_'
  storage_name_len = OSCORE_STORAGE_KEY_LEN - OSCORE_STORAGE_PREFIX_LEN;
  oc_conv_byte_array_to_hex_string(sender_id, sender_id_len,
                                   storage_name + OSCORE_STORAGE_PREFIX_LEN,
                                   &storage_name_len);

  // append divider ( -1 to overwrite the '\0')
  storage_name[OSCORE_STORAGE_PREFIX_LEN + storage_name_len - 1] = '_';

  // add 'ID Context' behind the 'Sender ID' (+1 to include the foreseen string end '\0' placeholder)
  storage_name_len = OSCORE_STORAGE_KEY_LEN - OSCORE_STORAGE_PREFIX_LEN - OSCORE_STORAGE_DIVIDER_LEN - storage_name_len + 1;
  oc_conv_byte_array_to_hex_string(id_context, id_context_len,
                                   storage_name + (OSCORE_STORAGE_PREFIX_LEN + sender_id_len * 2 + OSCORE_STORAGE_DIVIDER_LEN),
                                   &storage_name_len);
}

/**
 * @brief Read SSN from storage for a given access token entry
 *
 * Constructs storage key as 'ssn_<osc_id_hex>_<contextid_hex>' and reads
 * the stored SSN value. Returns 0 if not found or entry is NULL.
 *
 * @param entry Pointer to the AT table entry
 * @return uint64_t Stored SSN value, or 0 if not found
 */
static uint64_t oc_read_ssn_from_storage(const oc_auth_at_t* entry)
{
  if (!entry)
  {
    return 0;
  }

  char storage_name[OSCORE_STORAGE_KEY_LEN] = OSCORE_STORAGE_PREFIX;
  oc_build_ssn_storage_name(entry, storage_name);

  uint64_t ssn = 0;
  const long bytes_read = oc_storage_read(storage_name, (uint8_t*)&ssn, sizeof(ssn));

  if (bytes_read == sizeof(ssn))
  {
    OC_DBG("Read SSN from storage '%s': %" PRIu64, storage_name, ssn);
    return ssn;
  }
  OC_WRN("Read NO SSN from storage '%s', starting from 0", storage_name);
  return 0;
}

void oc_write_ssn_to_storage(const oc_auth_at_t* entry, uint64_t ssn)
{
  if (!entry)
  {
    return;
  }

  char storage_name[OSCORE_STORAGE_KEY_LEN] = OSCORE_STORAGE_PREFIX;
  oc_build_ssn_storage_name(entry, storage_name);

  const long bytes_written = oc_storage_write(storage_name, (uint8_t*)&ssn, sizeof(ssn));

  if (bytes_written == sizeof(ssn))
  {
    OC_DBG("Wrote SSN to storage '%s': %" PRIu64, storage_name, ssn);
    return;
  }
  OC_WRN("Wrote NO SSN to storage '%s'", storage_name);
}

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

static void oc_core_knx_auth_o_osndelay_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  OC_INF("oc_core_knx_auth_o_osndelay_get_handler");

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  oc_rep_begin_root_object();
  oc_rep_i_set_uint(root, 1, get_oscore_osn_delay_ms());
  oc_rep_end_root_object();

  OC_INF("oc_core_knx_auth_o_osndelay_get_handler - done");
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
  while (rep)
  {
    if (rep->type == OC_REP_INT)
    {
      if (rep->iname == 1)
      {
        OC_DBG("oc_core_knx_auth_o_osndelay_put_handler type: %d value %d", (int)rep->type, (int)rep->value.integer);
        set_oscore_osn_delay_ms((uint16_t)rep->value.integer); 
        oc_prepare_cbor_response(request, OC_STATUS_CHANGED);
        return;
      }
    }

    rep = rep->next;
  }

  oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
}

// resource definition, details/comments see on 'core_resource_well_known_core'
static oc_resource_data_t core_resource_knx_auth_o_osndelay_data;
const oc_resource_t core_resource_knx_auth_o_osndelay = {
  (oc_resource_t*)&core_resource_knx_auth_o,
  {NULL, sizeof("/auth/o/osndelay"), "/auth/o/osndelay"},
  {NULL, 0, NULL},
  {NULL, sizeof("urn:knx:dpt:timePeriodMsec"), "urn:knx:dpt:timePeriodMsec"},
  {APPLICATION_CBOR, CONTENT_NONE},
  OC_DISCOVERABLE,
  {oc_core_knx_auth_o_osndelay_get_handler, NULL, OC_ACL_D, OC_IF_D},
  {oc_core_knx_auth_o_osndelay_put_handler, NULL, OC_ACL_SEC, OC_IF_SEC},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {{NULL}, NULL},
  {{NULL}, NULL},
  0,
  0,
  true,
  &core_resource_knx_auth_o_osndelay_data};

static void oc_core_knx_auth_o_replwdo_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  oc_rep_begin_root_object();
  oc_rep_i_set_uint(root, 1, g_oscore_replay_window_size); // use direct access
  oc_rep_end_root_object();

  OC_INF("oc_core_knx_auth_o_replwdo_get_handler - done");
  oc_prepare_cbor_response(request, OC_STATUS_OK);
}

// resource definition, details/comments see on 'core_resource_well_known_core'
static oc_resource_data_t core_resource_knx_auth_o_replwdo_data;
const oc_resource_t core_resource_knx_auth_o_replwdo = {
  (oc_resource_t*)&core_resource_knx_auth_o_osndelay,
  {NULL, sizeof("/auth/o/replwdo"), "/auth/o/replwdo"},
  {NULL, 0, NULL},
  {NULL, sizeof("urn:knx:dpt.value2UCount"), "urn:knx:dpt.value2UCount"},
  {APPLICATION_CBOR, CONTENT_NONE},
  OC_DISCOVERABLE,
  {oc_core_knx_auth_o_replwdo_get_handler, NULL, OC_ACL_D, OC_IF_D},
  {NULL, NULL, OC_ACL_NONE, OC_ACL_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {{NULL}, NULL},
  {{NULL}, NULL},
  0,
  0,
  true,
  &core_resource_knx_auth_o_replwdo_data};

static void oc_core_knx_auth_o_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  int query_parameter_kvpair_matches = 0; // query parameter key/value pair matches found
  size_t response_length = 0;
  int query_pn = PAGE_NUMBER;
  int query_ps = PAGE_SIZE;

  int first_entry = OC_KNX_AUTH_O_REPLWDO; // first entry number of a resource that will be placed on a page
  int last_entry = OC_KNX_AUTH_O; // last entry number of a resource that will be placed on a page
  int total = last_entry - first_entry; // total entries of this resource
  bool more_request_needed = false;

  OC_INF("oc_core_auth_o_get_handler - start");

  if (!oc_accept_header_is_ok(request, APPLICATION_LINK_FORMAT))
  {
    return;
  }

  // handle query parameters l=ps and/or l=total
  if (query_l_was_processed(request, PAGE_SIZE, total))
  {
    return;
  }

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

  /*
    entries don't fit in a single page -> more pages are needed to get the full list
    - total=4, page number 1, page size 02, first entry = 002 -> no more data on next page
    - total=4, page number 1, page size 01, first entry = 001 -> more data on next page
  */
  if (last_entry > first_entry + query_ps)
  {
    last_entry = first_entry + query_ps;
    more_request_needed = true;
  }

  for (int i = first_entry; i < last_entry; i++)
  {
    if (oc_check_request_from_index(i, request, &response_length, &i, i, true))
    {
      query_parameter_kvpair_matches++;
    }
  }

  if (query_parameter_kvpair_matches > 0)
  {
    if (more_request_needed)
    {
      /*
      no page # was in the request (query_p =0) = next page 1 else #+1
      */
      response_length += add_next_page_indicator(oc_string(request->resource->uri), ++query_pn);
    }

    oc_prepare_linkformat_response(request, OC_STATUS_OK, response_length);
  }
  else
  {
    /*
    resources are mandatory, hence this can't be correct here
    */
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_INTERNAL_SERVER_ERROR);
  }
}

// resource definition, details/comments see on 'core_resource_well_known_core'
static oc_resource_data_t core_resource_knx_auth_o_data;
const oc_resource_t core_resource_knx_auth_o = {
  (oc_resource_t*)&core_resource_knx_auth_at,
  {NULL, sizeof("/auth/o"), "/auth/o"},
  {NULL, 0, NULL},
  {NULL, 0, NULL},
  {APPLICATION_LINK_FORMAT, CONTENT_NONE},
  OC_DISCOVERABLE,
  {oc_core_knx_auth_o_get_handler, NULL, OC_ACL_P | OC_ACL_D | OC_ACL_C, OC_IF_LI},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {{NULL}, NULL},
  {{NULL}, NULL},
  0,
  0,
  true,
  &core_resource_knx_auth_o_data};

#define LDEVID_RENEW 1
#define LDEVID_STOP  2

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
  while (rep)
  {
    OC_DBG("oc_core_a_sen_post_handler: key: (check) %s ", oc_string_checked(rep->name));
    if (rep->type == OC_REP_STRING)
    {
      if (rep->iname == 2)
      {
        // 2: "renew"
        cmd = a_sen_convert_cmd(oc_string_checked(rep->value.string));
        changed = true;
        break;
      }
    }

    rep = rep->next;
  }

  // input was set, so create the response
  if (changed == true)
  {
    OC_INF("oc_core_a_sen_post_handler cmd %d ", cmd);
    /*
      renew the credentials
      Note: This is optional for now.
    */
    oc_prepare_cbor_response(request, OC_STATUS_CHANGED);
    return;
  }

  oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
}

// resource definition, details/comments see on 'core_resource_well_known_core'
static oc_resource_data_t core_resource_a_sen_data;
const oc_resource_t core_resource_a_sen = {
  (oc_resource_t*)&core_resource_knx_auth_o_replwdo,
  {NULL, sizeof("/a/sen"), "/a/sen"},
  {NULL, 0, NULL},
  {NULL, 0, NULL},
  {APPLICATION_CBOR, CONTENT_NONE},
  OC_DISCOVERABLE,
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {oc_core_a_sen_post_handler, NULL, OC_ACL_SEC, OC_IF_SEC},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {{NULL}, NULL},
  {{NULL}, NULL},
  0,
  0,
  true,
  &core_resource_a_sen_data};

// empty ... when 'id' string is an empty string "" 
static oc_auth_at_t* find_empty_at_entry(void)
{
  
  for (oc_auth_at_t* entry = g_at_entries; entry < g_at_entries + G_AT_MAX_ENTRIES; entry++)
  {
    if (oc_string_len(entry->id) == 0)
    {
      // found an empty 'id' string ...
      return entry;
    }
  }

  return NULL;
}

static oc_auth_at_t* find_entry_from_access_token_string(const char* at, size_t len_at)
{
  for (oc_auth_at_t* entry = g_at_entries; entry < g_at_entries + G_AT_MAX_ENTRIES; entry++)
  {
    const size_t len = oc_string_len(entry->id);
    if (len > 0 && len == len_at && strncmp(at, oc_string(entry->id), len) == 0)
    {
      return entry;
    }
  }

  return NULL;
}

// NULL if none is found in payload
static oc_string_t* find_access_token_id_from_payload(oc_rep_t* object)
{
  while (object)
  {
    switch (object->type)
    {
    case OC_REP_STRING:
      // access token id (0) is only for type text string defined
      if (oc_string_len(object->name) == 0 && object->iname == 0)
      {
        oc_string_t* index = &object->value.string;
        OC_INF("find id from request: %s ", oc_string_checked(*index));
        return index;
      }

      break;
    default:
      break;
    }

    object = object->next;
  }

  OC_ERR("no id found (error)");
  return NULL;
}

// false, if the GA was NOT found in access token GA list
static bool check_access_token_for_group_address(const oc_auth_at_t* at_entry, uint32_t group_address)
{
  if (!at_entry)
  {
    return false;
  }

  /*
    if there is no GA list (len =0), loop does not start
    must be a stack issue that the wrong access token is used
  */

  for (int i = 0; i < at_entry->ga_len; i++)
  {
    if (at_entry->ga[i] == group_address)
    {
      return true;
    }
  }

  return false;
}

int oc_core_get_at_table_size(void)
{
  return G_AT_MAX_ENTRIES;
}

/*
  Count items on demand is easier than inc/dec on creation/deletion of individual entries.
  Reason:
  Functions to create/delete an entry are also used to delete entire tables w/o having present entries,
  but it is a bit more time-consuming (battery devices).
*/
int oc_core_items_used_in_auth_at_table(void)
{
  int counter = 0;
  for (oc_auth_at_t* entry = g_at_entries; entry < g_at_entries + G_AT_MAX_ENTRIES; entry++)
  {
    if (oc_string_len(entry->id) > 0)
    {
      counter++;
    }
  }

  return counter;
}

static void oc_core_auth_at_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  // query parameter key/value pair matches found
  int query_parameter_kvpair_matches = 0; 
  size_t response_length = 0;
  int query_pn = PAGE_NUMBER;
  int query_ps = PAGE_SIZE;

  OC_INF("oc_core_auth_at_get_handler - start"); 

  if (!oc_accept_header_is_ok(request, APPLICATION_LINK_FORMAT))
  {
    return;
  }

  // current resource amount
  const int total = oc_core_items_used_in_auth_at_table();

  // handle query parameters l=ps and/or l=total
  if (query_l_was_processed(request, PAGE_SIZE, total))
  {
    return;
  }

  // empty table returns an empty link-response
  if (total == 0)
  {
    oc_prepare_linkformat_response(request, OC_STATUS_OK, 0);
    return;
  }

  // first entry number of a resource that will be placed on a page
  const int first_entry = evaluate_query_px(request, &query_pn, &query_ps);

  /*
    check if requested page will carry at least one resource e.g
    - total=4, pn 5, ps 20, first entry = 100 -> no data on page 5
      (all on page 0)
    - total=4, pn 1, ps 04, first entry = 004 -> no data on page 1
      (all on page 0)
  */
  if (first_entry >= total || query_ps == 0)
  {
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  /*
    entries don't fit in a single page -> more pages are needed to get the full list
    - total=4, page number 1, page size 02, first entry = 002
      -> no more data on next page
    - total=4, page number 1, page size 01, first entry = 001
      -> more data on next page
  */
  const bool more_request_needed = total > first_entry + query_ps ? true : false;

  /*
    Example </auth/at/token-id>;ct=60 ; must run through entire table since entries are stored randomly.
  */
  for (oc_auth_at_t* entry = g_at_entries + first_entry; entry < g_at_entries + G_AT_MAX_ENTRIES; entry++)
  {
    if (oc_string_len(entry->id) > 0)
    {
      if (response_length > 0)
      {
        // close previous record to create a new without LF (not found in RFC 6690)
        response_length += oc_rep_add_line_to_buffer(",");
      }

      response_length += oc_rep_add_line_to_buffer("</auth/at/");
      response_length += oc_rep_add_line_to_buffer(oc_string(entry->id));
      response_length += oc_rep_add_line_to_buffer(">;ct=60");

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

  OC_INF("oc_core_auth_at_get_handler - end");
}

static void oc_core_auth_at_post_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  oc_rep_t* rep = NULL;
  oc_rep_t* cnf_object = NULL;
  oc_rep_t* oscore_object = NULL;

  // default assumption
  oc_status_t return_status = OC_STATUS_BAD_REQUEST;

  bool scope_updated = false;
  bool other_updated = false;

  oc_auth_at_t* at_entry = NULL;
  OC_INF("oc_core_auth_at_post_handler");

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  // debugging
  oc_print_rep_as_json(request->request_payload, true);

  // set ptr to collection of 1...n access token entries in payload
  rep = request->request_payload;
  oc_rep_t* object = NULL;

  while (rep)
  {
    if (rep->type == OC_REP_OBJECT)
    {
      // check for valid payload (EITT Test 5.3.8.2)
      object = rep->value.object;

      // scan all objects for a mixed array first 
      while (object)
      {
        if (object->type == OC_REP_MIXED_ARRAY)
        {
          OC_WRN("mixed array as scope is not allowed!");
          oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
          return;
        }

        object = object->next;
      }

      /*
        treat request payload value as one entry
        (that itself defines a chain of objects for id, scopes,...)
      */
      object = rep->value.object;

      // find access token id in request
      oc_string_t* access_token_id = find_access_token_id_from_payload(object);

      if (access_token_id == NULL)
      {
        OC_ERR("Access token ID in payload not found!");
        oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
        return;
      }

      // find index from access token table
      at_entry = find_entry_from_access_token_string(oc_string(*access_token_id), oc_string_len(*access_token_id));
      if (at_entry)
      {
        // index already in use, so it will be changed
        return_status = OC_STATUS_CHANGED;
      }
      else
      {
        // no index, so we will create one
        return_status = OC_STATUS_CREATED;

        // no index, so we will create one (default)
        at_entry = find_empty_at_entry();
        if (!at_entry)
        {
          OC_ERR("AT table has no empty slot to add a new entry!");
          oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
          return;
        }
      }

      #define MANDATORY_AT_PROPERTIES (5) // id, profile, scope, cnf:osc:id/ms
      bool id_only = true; // used to delete the AT table entry

      /*
        Set access token id, note that an access token is (too) complex to use
        a shadow copy as for POST on pub/rcp/go.
      */
      oc_free_string(&at_entry->id);
      oc_new_string(&at_entry->id, oc_string(*access_token_id), oc_string_len(*access_token_id));

      // access token id already set
      int current_at_properties = 1;

      while (object)
      {
        if (object->type == OC_REP_STRING_ARRAY)
        {
          // any extra element - even if not valid - causes a "not an id only"
          id_only = false;

          // scope with ACL scope string array from MaC such as ["if.sec", "if.swu"]
          if (object->iname == 9)
          {
            /*
              array of scopes as (32 byte) strings (no useful macro found)
              - char ptr must iterate each 32 bytes
              - length must iterate over length / 32
            */
            char* array = (char*)object->value.array.ptr;
            const int new_array_size = (int)object->value.array.size / STRING_ARRAY_ITEM_MAX_LEN;

            // set default scope
            oc_acl_mask_t acl_scopes = OC_ACL_NONE;

            // release a possible ga array (same id was used with GA list, now without), free ignores NULL ptr
            free(at_entry->ga);

            // in case of scopes the GA/GA len property is not used
            at_entry->ga = NULL;
            at_entry->ga_len = 0;
            for (int i = 0; i < new_array_size; i++)
            {
              // address each string
              char* acl_string = (char*)array + i * STRING_ARRAY_ITEM_MAX_LEN;

              // compact the input strings as acl bit mask definitions
              acl_scopes += oc_ri_get_scope_mask(acl_string, strlen(acl_string));
            }

            // no scope (an empty array) or a bad scope was set (such as if.ll)
            if (acl_scopes == OC_ACL_NONE)
            {
              OC_ERR("No valid access scope was set!");
              oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
              return;
            }

            scope_updated = true;

            // scopes (compacted bits)
            at_entry->scope = acl_scopes;
            current_at_properties++;
          }
        }
        else if (object->type == OC_REP_INT_ARRAY)
        {
          // any extra element - even if not valid - causes a "not an id only"
          id_only = false;

          // scope with GA integer array from MaC such as [200, 201]
          if (object->iname == 9)
          {
            // default, will be overwritten if GA array was correctly assigned
            at_entry->scope = OC_ACL_NONE;

            // a post request does NOT append items to an (existing) array, it overwrites them  
            const int64_t* array = oc_int_array(object->value.array);
            const int new_array_size = oc_int_array_size(object->value.array);

            // malloc of 'zero' byte return pointer is undefined
            uint32_t* new_array = (uint32_t*)malloc(new_array_size * sizeof(uint32_t));
            if (new_array)
            {
              if (new_array_size > 0)
              {
                for (int i = 0; i < new_array_size; i++)
                {
                  new_array[i] = (uint32_t)array[i];
                }

                OC_DBG("ga size %d", new_array_size);

                // release a possible ga array, it will be overwritten, free ignores NULL ptr
                free(at_entry->ga);

                at_entry->ga_len = new_array_size;
                at_entry->ga = new_array;

                /*
                  define THIS auth at token below with <ga> scope 's-mode messaging',
                  - is defined only here in the code
                  - is used only on /k resource (see also oc_knx_sec_check_acl)
                  - is used only in combination with a present ga array / array len (see above)
                */
                at_entry->scope = OC_ACL_GA;
                current_at_properties++;
              }
              else
              {
                OC_ERR("No valid access GA scope was set!");
                oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
                return;
              }
            }
            else
            {
              OC_ERR("Out of stack memory!");
              oc_prepare_no_format_response_no_payload(request, OC_STATUS_INTERNAL_SERVER_ERROR);
              return;
            }
          }
        }
        else if (object->type == OC_REP_STRING)
        {
          if (object->iname != 0)
          {
            /*
              NOT id (0), for sure from now on a not 'ID only' case
              Note: id (0) was already scanned/assigned
            */
            id_only = false;
          }
        }
        else if (object->type == OC_REP_INT)
        {
          // any extra element - even if not valid - causes a "not an id only"
          id_only = false;

          // profile (38)
          if (object->iname == 38)
          {
            at_entry->profile = (oc_at_profile_t)object->value.integer;
            current_at_properties++;
          }
        }
        else if (object->type == OC_REP_OBJECT)
        {
          // any extra element - even if not valid - causes a "not an id only"
          id_only = false;

          // level of cnf:xxx
          cnf_object = object->value.object;
          int cnf_object_nr = object->iname;

          OC_DBG("cnf object nr %d", cnf_object_nr);
          while (cnf_object)
          {
            if (cnf_object->type == OC_REP_OBJECT)
            {
              oscore_object = cnf_object->value.object;
              int oscore_object_nr = cnf_object->iname;
              while (oscore_object)
              {
                // cnf:osc(8:4)
                if (oscore_object->type == OC_REP_INT)
                {
                  if (cnf_object_nr == 8 && oscore_object_nr == 4 && oscore_object->iname == 4)
                  {
                    // cnf:osc:ms (8:4:4)

                    /* algorithm (10) only supports value 10 at the moment, EITT test 5.3.19.3 */
                    if (object->value.integer != 10)
                    {
                      OC_ERR("algorithm is not 10 : %d", (int)object->value.integer);
                      return_status = OC_STATUS_BAD_REQUEST;
                    }

                    other_updated = true;
                  }
                }
                else if (oscore_object->type == OC_REP_BYTE_STRING)
                {
                  if (cnf_object_nr == 8 && oscore_object_nr == 4 && oscore_object->iname == 2)
                  {
                    // cnf:osc:ms (8:4:2)

                    uint8_t mastersecret_size = oc_string_len(oscore_object->value.string);
                    char* mastersecret_ptr = oc_string(oscore_object->value.string);

                    // size does not fit
                    if (mastersecret_size < OSCORE_KEY_LEN || mastersecret_size > OSCORE_MASTER_SECRET_LEN)
                    {
                      OC_ERR("master secret size must be in range 16 ... 32: %u", mastersecret_size);
                      oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
                      return;
                    }

                    // all '0'
                    if (oc_check_string_on_zero_content(mastersecret_ptr, mastersecret_size))
                    {

                      OC_ERR("master secret with len %u content cannot be zero : ", mastersecret_size);
                      oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
                      return;
                    }

                    oc_free_string(&at_entry->osc_ms);
                    oc_new_byte_string(&at_entry->osc_ms, mastersecret_ptr, mastersecret_size);

                    other_updated = true;
                    current_at_properties++;
                  }
                  else if (cnf_object_nr == 8 && oscore_object_nr == 4 && oscore_object->iname == 6)
                  {
                    // cnf:osc:contextId (8:4:6)

                    size_t id_context_size = oc_string_len(oscore_object->value.string);

                    // size does not fit
                    if (id_context_size > OSCORE_ID_CONTEXT_LEN)
                    {
                      OC_ERR("id context size to big : %zu", id_context_size);
                      oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
                      return;
                    }

                    oc_free_string(&at_entry->osc_contextid);
                    oc_new_byte_string(&at_entry->osc_contextid, oc_string(oscore_object->value.string), id_context_size);
                    other_updated = true;
                  }
                  else if (cnf_object_nr == 8 && oscore_object_nr == 4 && oscore_object->iname == 0)
                  {
                    // cnf:osc:id (8:4:0)

                    size_t sender_id_size = oc_string_len(oscore_object->value.string);

                    // size does not fit
                    if (sender_id_size > OSCORE_SENDER_ID_LEN)
                    {
                      OC_ERR("sender id size to big : %zu", sender_id_size);
                      oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
                      return;
                    }

                    oc_free_string(&at_entry->osc_id);
                    oc_new_byte_string(&at_entry->osc_id, oc_string(oscore_object->value.string), sender_id_size);
                    other_updated = true;
                    current_at_properties++;
                  }
                  else if (cnf_object_nr == 8 && oscore_object_nr == 4 && oscore_object->iname == 5)
                  {
                    // cnf:osc:salt (8:4:5), RFC 9003 max 255 bytes on JSON ....

                    oc_free_string(&at_entry->osc_salt);
                    oc_new_byte_string(&at_entry->osc_salt, oc_string(oscore_object->value.string), oc_string_len(oscore_object->value.string));
                    other_updated = true;
                  }
                }

                oscore_object = oscore_object->next;
              }
            }

            cnf_object = cnf_object->next;
          }
        }

        object = object->next;
      }

      /*
        a: created +  id only/< min elements = ERROR (to few elements)
        b: created +  5 elements             = OK (create)
        c: changed +  id only                = OK (delete)
        d: changed +  1...n elements         = OK (update)
      */
      if (return_status == OC_STATUS_CHANGED && id_only)
      {
        // c
        OC_DBG("only found id in request, deleting entry...");
        oc_delete_at_table_entry(at_entry);
      }
      else
      {
        if (return_status == OC_STATUS_CREATED && current_at_properties < MANDATORY_AT_PROPERTIES)
        {
          // a
          OC_WRN("mandatory items missing, no entry created");
          oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
          return;
        }

        // b + d
        OC_DBG("storage index: %d (%s) ", (int)(at_entry - g_at_entries), oc_string_checked(*access_token_id));

        // debugging
        oc_print_auth_at_entry(at_entry);
        oc_store_at_table_entry(at_entry);
      }
    }

    rep = rep->next;
  }

  OC_INF("activating oscore context");

  if (return_status == OC_STATUS_CHANGED && other_updated == false && scope_updated == true)
  {
    // do not update the oscore when update only scope content
    OC_WRN("updated scopes only, NO reinitializing of all used oscore keys ");
  }
  else
  {
    // add the oscore contexts by reinitializing all used oscore keys
    oc_init_oscore_from_storage(false);
  }

  // the last return status from a collection with 'n' POST elements is responded (CREATED/CHANGED)
  oc_prepare_no_format_response_no_payload(request, return_status);
  OC_INF("oc_core_auth_at_post_handler - end"); 
}

static void oc_core_auth_at_delete_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;
  OC_INF("oc_core_auth_at_delete_handler - start");

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  oc_delete_at_table();

  oc_prepare_no_format_response_no_payload(request, OC_STATUS_DELETED);
  OC_INF("oc_core_auth_at_delete_handler - end"); 
}

// resource definition, details/comments see on 'core_resource_well_known_core'
static oc_resource_data_t core_resource_knx_auth_at_data;
const oc_resource_t core_resource_knx_auth_at = {
  (oc_resource_t*)&core_resource_knx_auth_at_x,
  {NULL, sizeof("/auth/at"), "/auth/at"},
  {NULL, 0, NULL},
  {NULL, 0, NULL},
  {APPLICATION_LINK_FORMAT, CONTENT_NONE},
  OC_DISCOVERABLE,
  {oc_core_auth_at_get_handler, NULL, OC_ACL_P | OC_ACL_D | OC_ACL_C, OC_IF_LI},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {oc_core_auth_at_post_handler, NULL, OC_ACL_SEC, OC_IF_SEC},
  {oc_core_auth_at_delete_handler, NULL, OC_ACL_SEC, OC_IF_SEC},
  {{NULL}, NULL},
  {{NULL}, NULL},
  0,
  0,
  true,
  &core_resource_knx_auth_at_data};

static void oc_core_auth_at_x_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;
  const char* value;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }
  OC_INF("oc_core_auth_at_x_get_handler - start");

  // find the id part from the invoked URL auth/at/xyz...123
  const int value_len = oc_uri_get_wildcard_value_as_string(
    oc_string(request->resource->uri),
    oc_string_len(request->resource->uri),
    request->uri_path, request->uri_path_len, &value);

  // no access token string found
  if (value_len <= 0)
  {
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
    OC_WRN("AT string not found");
    return;
  }

  OC_DBG("id = %.*s", value_len, value); 

  // get the AT entry from access string token
  oc_auth_at_t* at_entry = find_entry_from_access_token_string(value, value_len);
  if (!at_entry)
  {
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
    OC_WRN("AT entry not found");
    return;
  }

  // debugging
  oc_print_auth_at_entry(at_entry);

  oc_rep_begin_root_object();

  // profile : 38
  oc_rep_i_set_int(root, 38, at_entry->profile);

  // id : 0
  oc_rep_i_set_text_string(root, 0, oc_string(at_entry->id));

  // scope : 9 (either list of GAS is used or acl scopes)
  if (at_entry->scope == OC_ACL_GA)
  {
    // list of GAs is used (the internal <ga> scope OC_ACL_GA is never returned as 'scope')
    oc_rep_i_set_int_array(root, 9, at_entry->ga, at_entry->ga_len);
  }
  else
  {
    // number of access scopes (all scopes except <ga> scope)
    const unsigned int nr_entries = oc_count_total_scopes_in_mask(at_entry->scope);
    if (nr_entries > 0)
    {
      OC_DBG("%u scope entries", nr_entries);

      // access scope list
      oc_string_array_t scopes;

      oc_new_string_array(&scopes, nr_entries);

      // compacted list of access scopes is used (specification says "interfaces" but access scopes are meant)
      oc_put_all_access_scope_names_from_a_mask_in_string_array(at_entry->scope, scopes);
      oc_rep_i_set_string_array(root, 9, scopes);

      oc_free_string_array(&scopes);
    }
  }

  if (at_entry->profile == OC_PROFILE_COAP_OSCORE || at_entry->profile == OC_PROFILE_COAP_PASE)
  {
    // create cnf map (8)
    oc_rep_i_set_key(&root_map, 8);

    CborEncoder cnf_map;
    cbor_encoder_create_map(&root_map, &cnf_map, CborIndefiniteLength);

    // create osc map (4)
    oc_rep_i_set_key(&cnf_map, 4);

    CborEncoder osc_map;
    cbor_encoder_create_map(&cnf_map, &osc_map, CborIndefiniteLength);

    if (oc_string_len(at_entry->osc_ms) > 0)
    {
      // root::cnf::osc::ms
      oc_rep_i_set_byte_string(osc, 2, oc_byte_string(at_entry->osc_ms),
                               oc_byte_string_len(at_entry->osc_ms));
    }

    if (oc_string_len(at_entry->osc_contextid) > 0)
    {
      // root::cnf::osc::contextid
      oc_rep_i_set_byte_string(osc, 6,
                               oc_byte_string(at_entry->osc_contextid),
                               oc_byte_string_len(at_entry->osc_contextid));
    }

    if (oc_string_len(at_entry->osc_id) > 0)
    {
      // root::cnf::osc::osc_id
      oc_rep_i_set_byte_string(osc, 0,
                               oc_byte_string(at_entry->osc_id),
                               oc_byte_string_len(at_entry->osc_id));
    }

    cbor_encoder_close_container_checked(&cnf_map, &osc_map);
    cbor_encoder_close_container_checked(&root_map, &cnf_map);
  }

  oc_rep_end_root_object();

  oc_prepare_cbor_response(request, OC_STATUS_OK);
  OC_INF("oc_core_auth_at_x_get_handler - end");
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

  OC_INF("oc_core_auth_at_x_delete_handler - start");

  // find the id part from the invoked URL auth/at/xyz...123
  const int value_len = oc_uri_get_wildcard_value_as_string(
    oc_string(request->resource->uri), oc_string_len(request->resource->uri),
    request->uri_path, request->uri_path_len, &value);

  // no access token string found
  if (value_len <= 0)
  {
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
    OC_WRN("AT string not found");
    return;
  }

  OC_DBG("id = %.*s", value_len, value); 

  // get the AT entry from access string token
  oc_auth_at_t* at_entry = find_entry_from_access_token_string(value, value_len);

  if (!at_entry)
  {
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
    OC_WRN("AT entry not found");
    return;
  }

  OC_DBG("delete AT table entry");
  oc_delete_at_table_entry(at_entry);

  // delete the related oscore contexts
  oc_oscore_free_contexts_at_id(at_entry);

  OC_INF("oc_core_auth_at_x_delete_handler - done");
  oc_prepare_no_format_response_no_payload(request, OC_STATUS_DELETED);
}

// resource definition, details/comments see on 'core_resource_well_known_core'
static oc_resource_data_t core_resource_knx_auth_at_x_data;
const oc_resource_t core_resource_knx_auth_at_x = {
  (oc_resource_t*)&core_resource_knx_auth,
  {NULL, sizeof("/auth/at/*"), "/auth/at/*"},
  {NULL, 0, NULL},
  {NULL, 0, NULL},
  {APPLICATION_CBOR, CONTENT_NONE},
  OC_DISCOVERABLE,
  {oc_core_auth_at_x_get_handler, NULL, OC_ACL_SEC, OC_IF_SEC},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {oc_core_auth_at_x_delete_handler, NULL, OC_ACL_SEC, OC_IF_SEC},
  {{NULL}, NULL},
  {{NULL}, NULL},
  0,
  0,
  true,
  &core_resource_knx_auth_at_x_data};

static void oc_core_knx_auth_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  int query_parameter_kvpair_matches = 0; // query parameter key/value pair matches found
  size_t response_length = 0;
  int query_pn = PAGE_NUMBER;
  int query_ps = PAGE_SIZE;

  int first_entry = OC_KNX_AUTH_O; // first entry number of a resource that will
  // be placed on a page
  int last_entry = OC_KNX_AUTH_AT_X; // last entry number of a resource that
  // will be placed on a page
  int total = last_entry - first_entry; // total entries of this resource
  bool more_request_needed = false;

  OC_INF("oc_core_knx_auth_get_handler - start");

  if (!oc_accept_header_is_ok(request, APPLICATION_LINK_FORMAT))
  {
    return;
  }

  // handle query parameters l=ps and/or l=total
  if (query_l_was_processed(request, PAGE_SIZE, total))
  {
    return;
  }

  // first entry number of a resource that will be placed on a page
  first_entry += evaluate_query_px(request, &query_pn, &query_ps);

  // check if requested page will carry at least one resource e.g
  // - total=4, pn 5, ps 20, first entry = 100 -> no data on page 5
  //   (all on page 0)
  // - total=4, pn 1, ps 04, first entry = 004 -> no data on page 1
  //   (all on page 0)
  if (first_entry >= last_entry || query_ps == 0)
  {
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  // entries don't fit in a single page -> more pages are needed to get the full list
  // - total=4, page number 1, page size 02, first entry = 002
  //   -> no more data on next page
  // - total=4, page number 1, page size 01, first entry = 001
  //   -> more data on next page
  if (last_entry > first_entry + query_ps)
  {
    last_entry = first_entry + query_ps;
    more_request_needed = true;
  }

  for (int i = first_entry; i < last_entry; i++)
  {
    if (oc_check_request_from_index(i, request, &response_length, &i, i, true))
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

  OC_INF("oc_core_knx_auth_get_handler - end");
}

// resource definition, details/comments see on 'core_resource_well_known_core'
static oc_resource_data_t core_resource_knx_auth_data;
const oc_resource_t core_resource_knx_auth = {
  (oc_resource_t*)&core_resource_well_known_core,
  {NULL, sizeof("/auth"), "/auth"},
  {NULL, (size_t)1 * 32, (char[1][32]){"urn:knx:fb.auth"}},
  {NULL, 0, NULL},
  {APPLICATION_LINK_FORMAT, CONTENT_NONE},
  OC_DISCOVERABLE,
  {oc_core_knx_auth_get_handler, NULL, OC_ACL_P | OC_ACL_D | OC_ACL_C, OC_IF_LI},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {{NULL}, NULL},
  {{NULL}, NULL},
  0,
  0,
  true,
  &core_resource_knx_auth_data};

void oc_print_auth_at_entry(const oc_auth_at_t* entry)
{
  #ifdef OC_PRINT
  if (entry && oc_string_len(entry->id) > 0)
  {
    OC_DBG("at index      : %d", (int)(entry - g_at_entries));
    OC_DBG("id (0)        : %s", oc_string_checked(entry->id));
    OC_DBG("scope (9)     : %d", entry->scope);
    OC_DBG("profile (38)  : %d (%s)", entry->profile, oc_at_profile_to_string(entry->profile));

    if (entry->profile == OC_PROFILE_COAP_OSCORE || entry->profile == OC_PROFILE_COAP_PASE)
    {
      if (oc_string_len(entry->osc_ms) > 0)
      {
        OC_DBG("osc:ms (h)    : (%d) ", (int)oc_byte_string_len(entry->osc_ms));
        oc_string_println_hex(entry->osc_ms);
      }

      if (oc_string_len(entry->osc_salt) > 0)
      {
        OC_DBG("osc:salt (h)  : (%d) ", (int)oc_byte_string_len(entry->osc_salt));
        oc_string_println_hex(entry->osc_salt);
      }

      if (oc_string_len(entry->osc_contextid) > 0)
      {
        OC_DBG("osc:ctx_id (h): (%d) ", (int)oc_byte_string_len(entry->osc_contextid));
        oc_string_println_hex(entry->osc_contextid);
      }

      if (oc_string_len(entry->osc_id) > 0)
      {
        OC_DBG("osc:id (h)    : (%d) ", (int)oc_byte_string_len(entry->osc_id));
        oc_string_println_hex(entry->osc_id);
      }

      if (entry->scope == OC_ACL_GA)
      {
        // print entire array in one line
        OC_DBG("osc:ga        : [");
        for (int i = 0; i < entry->ga_len; i++)
        {
          PRINTF("%" PRIu64 " ", (uint64_t)entry->ga[i]);
        }

        PRINTF("]");
      }
    }
  }
#endif
}

static oc_acl_mask_t oc_at_get_scope_mask(const oc_auth_at_t* entry)
{
  return entry ? entry->scope : OC_ACL_NONE;
}

int oc_delete_at_table_entry(oc_auth_at_t* entry)
{
  // results in a valid case in an index between 0 ... G_AT_MAX_ENTRIES
  const int index = get_at_index(entry);

  if (index != -1)
  {
    // AT file entry
    char filename[AT_SIZE];
    (void)snprintf(filename, AT_SIZE, "%s_%d", AT_STORE, index);
    oc_storage_erase(filename);

    // id, scope, profile
    oc_free_string(&entry->id);
    oc_new_string(&entry->id, "", 0);
    entry->scope = OC_ACL_NONE;
    entry->profile = OC_PROFILE_UNKNOWN;

    // oscore object
    oc_free_string(&entry->osc_ms);
    oc_new_byte_string(&entry->osc_ms, "", 0);

    oc_free_string(&entry->osc_salt);
    oc_new_byte_string(&entry->osc_salt, "", 0);

    oc_free_string(&entry->osc_contextid);
    oc_new_byte_string(&entry->osc_contextid, "", 0);

    oc_free_string(&entry->osc_id);
    oc_new_byte_string(&entry->osc_id, "", 0);

    // release a possible ga array, free ignores NULL ptr
    free(entry->ga);

    entry->ga = NULL;
    entry->ga_len = 0;
    return 0;
  }

  return -1;
}

/*
  Store AT table data in CBOR (hex stream data).
  Storage of the fields is done via the cbor keys in their hierarchy.
  Example tag cnf:osc:ms = 8:4:2 = 842
*/
static void oc_store_at_table_entry(oc_auth_at_t* entry)
{
  // results in a valid case in an index between 0 ... G_AT_MAX_ENTRIES
  const int index = get_at_index(entry);
  
  if (index != -1)
  {
    char filename[AT_SIZE];
    (void)snprintf(filename, AT_SIZE, "%s_%d", AT_STORE, index);

    uint8_t* buf = (uint8_t*)malloc(OC_MAX_APP_DATA_SIZE);
    if (!buf)
    {
      return;
    }

    oc_rep_new(buf, OC_MAX_APP_DATA_SIZE);

    // write the data
    oc_rep_begin_root_object();

    // 0: id
    oc_rep_i_set_text_string(root, 0, oc_string(entry->id));

    // 9: acl scope (compacted bits)
    oc_rep_i_set_int(root, 9, entry->scope);

    // 38: profile
    oc_rep_i_set_int(root, 38, entry->profile);

    // 84x: cnf:osc:xx map
    oc_rep_i_set_byte_string(root, 840, oc_byte_string(entry->osc_id), oc_byte_string_len(entry->osc_id));
    oc_rep_i_set_byte_string(root, 842, oc_byte_string(entry->osc_ms), oc_byte_string_len(entry->osc_ms));
    oc_rep_i_set_byte_string(root, 845, oc_byte_string(entry->osc_salt), oc_byte_string_len(entry->osc_salt));
    oc_rep_i_set_byte_string(root, 846, oc_byte_string(entry->osc_contextid), oc_byte_string_len(entry->osc_contextid));

    /*
      777: ga's
      Note:
      Here also an empty array (777: []) might be written in case of present
      scopes such as a binary 'if.sec').
    */
    oc_rep_i_set_int_array(root, 777, entry->ga, entry->ga_len);

    oc_rep_end_root_object();

    const int size = oc_rep_get_encoded_payload_size();
    if (size > 0)
    {
      OC_DBG("stored current state [%s] [%d]: size %d", filename, index, size);

      const long written_size = oc_storage_write(filename, buf, size);
      if (written_size != (long)size)
      {
        OC_WRN("entry: [%s] written %d != %d (to write)", filename, (int)written_size, size);
      }
    }

    free(buf);
  }
}

static void oc_load_at_table_entry(oc_auth_at_t* entry)
{
  // results in a valid case in an index between 0 ... G_AT_MAX_ENTRIES
  const int index = get_at_index(entry);
  
  if (index !=-1)
  {
    char filename[AT_SIZE];
    (void)snprintf(filename, AT_SIZE, "%s_%d", AT_STORE, index);

    oc_rep_t* rep;

    uint8_t* buf = (uint8_t*)malloc(OC_MAX_APP_DATA_SIZE);
    if (!buf)
    {
      return;
    }

    const long bytes_to_read = oc_storage_read(filename, buf, OC_MAX_APP_DATA_SIZE);
    if (bytes_to_read > 0)
    {
      OC_INF("Reading from %s , bytes : %ld", filename, bytes_to_read);

      // oc_rep_t nodes are allocated via calloc inside oc_parse_rep.
      const int err = oc_parse_rep(buf, bytes_to_read, &rep);
      oc_rep_t* head = rep;

      if (err == 0)
      {
        while (rep)
        {
          switch (rep->type)
          {
          case OC_REP_INT:
            if (rep->iname == 9)
            {
              // 9: scope (compacted bits)
              entry->scope = (oc_acl_mask_t)rep->value.integer;
            }
            else if (rep->iname == 38)
            {
              // 38: profile
              entry->profile = (oc_at_profile_t)rep->value.integer;
            }

            break;
          case OC_REP_STRING:
            if (rep->iname == 0)
            {
              // 0: id
              oc_free_string(&entry->id);
              oc_new_string(&entry->id, oc_string(rep->value.string), oc_string_len(rep->value.string));
            }

            break;
          case OC_REP_BYTE_STRING:
            /*
              reading back the strings should be done with oc_string_len, not with oc_byte_string_len
              string_len = len - 1 to exclude the '\0' from rep object; byte string len includes it
            */
            if (rep->iname == 840)
            {
              oc_free_string(&entry->osc_id);
              oc_new_byte_string(&entry->osc_id, oc_string(rep->value.string), oc_string_len(rep->value.string));
            }
            else if (rep->iname == 842)
            {
              oc_free_string(&entry->osc_ms);
              oc_new_byte_string(&entry->osc_ms, oc_string(rep->value.string), oc_string_len(rep->value.string));
            }
            else if (rep->iname == 845)
            {
              oc_free_string(&entry->osc_salt);
              oc_new_byte_string(&entry->osc_salt, oc_string(rep->value.string), oc_string_len(rep->value.string));
            }
            else if (rep->iname == 846)
            {
              oc_free_string(&entry->osc_contextid);
              oc_new_byte_string(&entry->osc_contextid, oc_string(rep->value.string), oc_string_len(rep->value.string));
            }

            break;
          case OC_REP_INT_ARRAY:
            // ga array with GAs from MaC
            if (rep->iname == 777)
            {
              // a load command does NOT append items to an (existing) array, it overwrites them
              const int64_t* array = oc_int_array(rep->value.array);
              const int new_array_size = oc_int_array_size(rep->value.array);

              // malloc of 'zero' byte return pointer is undefined
              uint32_t* new_array = (uint32_t*)malloc(new_array_size * sizeof(uint32_t));
              if (new_array && new_array_size > 0)
              {
                for (int i = 0; i < new_array_size; i++)
                {
                  new_array[i] = (uint32_t)array[i];
                }

                // release an already assigned ga array on the org ptr, free ignores NULL ptr
                free(entry->ga);

                // assign only when the new array is allocated correctly
                entry->ga_len = new_array_size;
                entry->ga = new_array;

                OC_DBG("at table entry with NON empty ga array 777: [...] loaded from storage, size %d", new_array_size);
              }
            }

            break;
          case OC_REP_NIL:

            // ga array (777), note that an empty ga array is coded in current CBOR with "OC_REP_NIL"
            if (rep->iname == 777)
            {
              // release an already assigned ga array on the org ptr, free ignores NULL ptr
              free(entry->ga);

              entry->ga_len = 0;
              entry->ga = NULL;

              OC_DBG("at table entry with empty ga array 777: [] loaded from storage, size 0");
            }

            break;
          default:
            OC_ERR("invalid object type detected");
            break;
          }

          rep = rep->next;
        }
      }

      oc_free_rep(head);
    }

    free(buf);
  }
}

void oc_core_find_and_remove_pase_token_in_at_table(void)
{
  for (oc_auth_at_t* entry = g_at_entries; entry < g_at_entries + G_AT_MAX_ENTRIES; entry++)
  {
    if (entry->profile == OC_PROFILE_COAP_PASE)
    {
      oc_delete_at_table_entry(entry); // delete entry from AT table
      oc_oscore_free_contexts_at_id(entry); // removes possible references
      OC_DBG("PASE key found, invalidated...");
    }
  }
}

oc_auth_at_t* oc_core_find_at_entry_by_osc_id(uint8_t* osc_id, size_t osc_id_len)
{
  for (oc_auth_at_t* entry = g_at_entries; entry < g_at_entries + G_AT_MAX_ENTRIES; entry++)
  {
    if (oc_byte_string_len(entry->osc_id) == osc_id_len && memcmp(oc_string(entry->osc_id), osc_id, osc_id_len) == 0)
    {
      return entry;
    }
  }

  return NULL;
}

static void oc_load_at_table(void)
{
  OC_INF("Loading AT Table into RAM from persistent storage");
  
  for (oc_auth_at_t* entry = g_at_entries; entry < g_at_entries + G_AT_MAX_ENTRIES; entry++)
  {
    // debugging 
    oc_load_at_table_entry(entry);
    oc_print_auth_at_entry(entry);
  }

  // create the oscore contexts
  oc_init_oscore_from_storage(true);
}

void oc_delete_at_table(void)
{
  OC_INF("Deleting AT from RAM and persistent storage");

  for (oc_auth_at_t* entry = g_at_entries; entry < g_at_entries + G_AT_MAX_ENTRIES; entry++)
  {
    // debugging 
    oc_print_auth_at_entry(entry);
    oc_delete_at_table_entry(entry);
  }

  oc_oscore_free_all_contexts();
  oc_oscore_free_all_replay_records();
}

void oc_delete_at_table_except_sec_scope_entries(void)
{
  OC_INF("deleting 'non if.sec' access token table entries from RAM and storage");

  for (oc_auth_at_t* entry = g_at_entries; entry < g_at_entries + G_AT_MAX_ENTRIES; entry++)
  {
    const oc_acl_mask_t scope = oc_at_get_scope_mask(entry);

    if (!(scope & OC_ACL_SEC))
    {
      // delete the entries that are not including "if.sec"
      oc_print_auth_at_entry(entry);
      oc_delete_at_table_entry(entry);
    }
  }

  // (re)create the oscore contexts in the table that still remains
  oc_init_oscore_from_storage(true);
}

void oc_oscore_set_auth_shared(const char* client_sender_id, int client_sender_id_size, uint8_t* shared_key, int shared_key_size)
{
  oc_auth_at_t* at_entry = find_entry_from_access_token_string(client_sender_id, client_sender_id_size);

  if (!at_entry)
  {
    // no pase token present in AT table (normal case)
    at_entry = find_empty_at_entry();
  }

  if (!at_entry)
  {
    OC_ERR("no space left in AT table");
  }
  else
  {
    /*
      write (OR overwrite if entry was already occupied) the above defined pase entry
      to AT table (RAM) and file storage
    */

    // id
    oc_free_string(&at_entry->id);
    oc_new_string(&at_entry->id, client_sender_id, client_sender_id_size);

    // pase token owns only if.sec scope
    at_entry->scope = OC_ACL_SEC;
    at_entry->profile = OC_PROFILE_COAP_PASE;

    // secret
    oc_free_string(&at_entry->osc_ms);
    oc_new_byte_string(&at_entry->osc_ms, (char*)shared_key, shared_key_size);

    // no 'kid_context'
    oc_free_string(&at_entry->osc_contextid);
    oc_new_byte_string(&at_entry->osc_contextid, "", 0);

    // 'kid' (on the wire it was a byte string, so we have to store the byte string)
    oc_free_string(&at_entry->osc_id);
    oc_new_byte_string(&at_entry->osc_id, client_sender_id, client_sender_id_size);

    // release a possible ga array, it will be overwritten, free ignores NULL ptr
    free(at_entry->ga);

    // no ga array as scope for PASE token
    at_entry->ga_len = 0;
    at_entry->ga = NULL;

    // store
    oc_store_at_table_entry(at_entry);

    // init
    oc_init_oscore_from_storage(false);
  }
}

oc_auth_at_t* oc_get_auth_at_entry(int index)
{
  return index >= 0 && index < G_AT_MAX_ENTRIES ? &g_at_entries[index] : NULL;
}

void oc_create_knx_sec_resources(void)
{
  OC_DBG("oc_create_knx_sec_resources");
  oc_load_at_table();
}

void oc_init_oscore_from_storage(const bool read_ssn_from_storage)
{
  oc_oscore_free_sender_contexts();

  for (oc_auth_at_t* at_entry = g_at_entries; at_entry < g_at_entries + G_AT_MAX_ENTRIES; at_entry++)
  {
    if (oc_string_len(at_entry->id) > 0)
    {
      // debugging 
      oc_print_auth_at_entry(at_entry);

      if (at_entry->profile == OC_PROFILE_COAP_OSCORE || at_entry->profile == OC_PROFILE_COAP_PASE)
      {

        /*
          'Client' Side (details see method 'oc_oscore_receive_message' header)
          - create oscore REQUEST sender context = kid + kid_context + ms + salt from token
          - SSN initialized = read from storage, context is already present

          'Client' Side (details see method 'oc_oscore_receive_message' header), create:
          Request Sender Context
          - kid
          - kid_context
          - ms + salt
          - ssn = read from storage, context is already present
        */

        // always read stored SSN from storage to maintain continuity
        const uint64_t stored_ssn = oc_read_ssn_from_storage(at_entry);

        OC_DBG("loaded SSN from storage: %" PRIu64 " (padding=%s)", stored_ssn, read_ssn_from_storage ? "yes" : "no");

        /*
               'Client' Side (details see method 'oc_oscore_receive_message' header), create:
                Request Sender Context (from access token)
               - kid (oc_oscore_add_sender_context')
               - kid_context (here)
               - ms + salt from token (inside 'oc_oscore_add_context')
               - ssn = from storage
        */
        oc_oscore_context_params_t oscore_params = 
        {
          .ssn = stored_ssn,
          .id_context = (const uint8_t*)oc_string(at_entry->osc_contextid),
          .id_context_size = oc_byte_string_len(at_entry->osc_contextid),
          .auth_at = at_entry, // at entry cannot be out of range because of loop above
          .read_ssn_from_storage = read_ssn_from_storage // offset is added to SSN when reading from storage
        };
        const oc_oscore_context_t* oscore_ctx = oc_oscore_add_sender_context(&oscore_params);

        if (!oscore_ctx)
        {
          OC_ERR("failed to add a context entry for AT table entry (on device startup this is usually because of too less context buffers");
        }
      }
      else
      {
        OC_WRN("no oscore context was initialized, no PASE/OSCORE context available");
      }
    }
  }
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

bool oc_knx_sec_check_acl(coap_method_t method, const oc_resource_t* resource, oc_endpoint_t* endpoint, oc_rep_t* value_object)
{
  //  scope of called resource, init with default
  oc_acl_mask_t called_res_scope = OC_ACL_NONE;

  // check for scope, considering of CoAP INNER method (GET, PUT, ...)
  if (!oc_resource_get_acl_for_method(resource, method, &called_res_scope))
  {
    // resource or handler for method does not exist, no access
    return false;
  }

  // resource and handler for method exists...
  // uri len of resource versus uri len of caller endpoint was checked before
  if (called_res_scope == OC_ACL_NONE)
  {
    // not a secure resource, access allowed, see table in clause 5.1.3 of specification,
    // all resources with methods that do not have any scope uses in stack 'OC_ACL_NONE'
    return true;
  }

  // debugging
  OC_DBG("method allowed flags : ");
  PRINTipaddr_flags(*endpoint);

  if ((endpoint->flags & (OSCORE | OSCORE_DECRYPTED)) != (OSCORE | OSCORE_DECRYPTED))
  {
    // not a OSCORE message that was able to decrypt with given security context (CCM, MAC)
    OC_DBG_OSCORE("access denied for: %s with flags: %d",
                  oc_string_checked(resource->uri), endpoint->flags);
    return false;
  }

  /*
    Example for auth/o sub resource

    The stack compares caller scope with called scope,
    at least one bit match = ok, otherwise 4.00 (bad request) response.

    caller scope
    ------------
    The acl scope from auth/at table entry that was used to decrypt the message,
    written by a MaC (by using the tool key) such as
    if.p/d/c/sec/swu (never an interface like if.ll or if.b)

    called scope
    ------------
    The device resource + method 'precompiled' ACL scope
    (see resource definitions, e.g.; auth/o GET = if.p/d/c for linked-list)
  */

  const oc_acl_mask_t caller_acl_scope = oc_at_get_scope_mask(endpoint->auth_at_of_inbound_msg);

  // bitwise 'and' -> at least one scope from access token and resource must match
  if (caller_acl_scope & called_res_scope)
  {
    /*
      Here the caller/called scopes are matching

      O0 common endpoint (auth/at) call with caller/called scope hosting at least 'if.sec'
         -> OK

      O1 s-mode endpoint (/k) call with caller/called scope hosting at least 'if.g.s'
         -> OK
         = all ga's are allowed for 'if.g.s'

      O2 s-mode endpoint (/k) call with caller/called scope hosting at least a '<ga>'
         -> NOT ENOUGH
         = some ga's are allowed for <ga>
         - ga from request must be also part of ga list in access token (list MUST BE NOT empty)
         - no other EP than /k uses '<ga>' as resource scope (see resource definition)
         - <ga> scope is set only in one place (create access token with non-empty list)
    */

    if (caller_acl_scope == OC_ACL_GA)
    {
      /*
        O2 - a call with <ga> access token
        Here this is only possible if /k resource was addressed.
        (caller_acl_scope & called_res_scope ->
        OC_ACL_GA & (OC_ACL_GA + OC_ACL_G) = true for /k resource definition)
      */

      // scan received payload for request ga (may also have 32-bit value 0xFFFFFFFF) 
      const oc_rep_t* rep = value_object;
      bool group_address_match = false;

      while (rep)
      {
        if (rep->type == OC_REP_OBJECT)
        {
          // s map with st/ga/value (if present on w=write/a=update)
          const oc_rep_t* s_map = rep->value.object;

          while (s_map)
          {
            if (s_map->type == OC_REP_INT && s_map->iname == 7)
            {
              // only GA is of interest

              // found a GA, check it, and break ...
              group_address_match = check_access_token_for_group_address(endpoint->auth_at_of_inbound_msg, (uint32_t)s_map->value.integer);
              break;
            }

            s_map = s_map->next;
          }
        }

        rep = rep->next;
      }

      // a: no ga found in request payload at all = request payload error (no access) -> false 
      // b: acl scopes + method OK + ga found in request payload, was checked
      //    - b1 : true -> found in an access token
      //    - b2 : false -> not found in any access token

      return group_address_match;
    }

    // acl scopes + method OK
    return true;
  }

  #ifdef OC_DEBUG
  OC_DBG_OSCORE("access to %s unauthorized: request scope=%d ; resource scope=%d :", oc_string(resource->uri), caller_acl_scope, called_res_scope);

  oc_print_acl_scopes(caller_acl_scope);
  oc_print_acl_scopes(called_res_scope);

  #endif

  return false;
}
