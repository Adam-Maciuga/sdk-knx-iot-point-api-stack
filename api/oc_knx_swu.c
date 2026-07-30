/*
 * Copyright (c) 2021 Cascoda Ltd
 * Copyright (c) 2026 KNX Association
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "oc_knx_swu.h"
#include "oc_helpers.h"
#include "oc_ri.h"
#include "oc_api.h"
#include "oc_core_res.h"
#include "oc_discovery.h"
#include "oc_knx_helpers.h"
#include "oc_main.h"
#include "port/oc_storage.h"

// forward declarations for the resource chain
extern const oc_resource_t core_resource_knx_swu_hwref;
extern const oc_resource_t core_resource_knx_swu_method;
extern const oc_resource_t core_resource_knx_swu_lastupdate;
extern const oc_resource_t core_resource_knx_swu_result;
extern const oc_resource_t core_resource_knx_swu_state;
extern const oc_resource_t core_resource_knx_swu_update;
extern const oc_resource_t core_resource_knx_swu_pkgv;
extern const oc_resource_t core_resource_knx_swu_pkgcmd;
extern const oc_resource_t core_resource_knx_swu_pkgbytes;
extern const oc_resource_t core_resource_knx_swu_pkgnames;
extern const oc_resource_t core_resource_knx_swu;
extern const oc_resource_t core_resource_sub;

/*
  - static, but not const, since values are changed at runtime 
  - all members not listed here are zero initialized (static storage duration),
    e.g.; state = OC_SWU_STATE_IDLE, result = OC_SWU_RESULT_INIT, protocol = CoAP,
    pkg_bytes/current_defer = 0, pkg_version = 0.0.0 and all strings are empty
*/
static oc_device_swu_t swu_device = {.update_method = PUSH}; // fixed, stack does not support PULL

/*
 * Software Update (SWU) Implementation
 * 
 * This file implements the KNX IoT Point API software update protocol (Section 4.2).
 * 
 * Architecture:
 * - Stack Layer (this file): Protocol state machine, download tracking, callback notification
 * - Application Layer: Device-specific upgrade logic, package metadata, version management
 * 
 * State Machine Flow:
 * 1. IDLE → DOWNLOADING: On first block received at /a/swu
 * 2. DOWNLOADING → DOWNLOADED: When all bytes received (pkg_bytes >= expected_package_size)
 * 3. DOWNLOADED → UPDATING: On PUT to /swu/update
 * 4. UPDATING → IDLE: After application completes upgrade (via callback)
 * 
 * Download Process:
 * - Blocks arrive at /a/swu with query parameters: pkgs (total size), po (offset), ps (size)
 * - Stack tracks progress via expected_package_size and pkg_bytes counter
 * - Application receives each block via swu_cb() callback
 * - Application detects completion and sets package metadata
 * 
 * Upgrade Process:
 * - PUT to /swu/update triggers upgrade
 * - Stack transitions to UPDATING state
 * - Stack notifies application via swu_upgrade_cb() callback
 * - Application performs upgrade, updates firmware version, sets result
 * - Application transitions back to IDLE when complete
 */

// Track expected total package size (set from first block's pkgs parameter)
static int expected_package_size = 0;

// Forward declarations
typedef struct oc_swu_upgrade_t oc_swu_upgrade_t;
static const oc_swu_upgrade_t* oc_get_swu_upgrade_cb(void);

static void oc_knx_swu_protocol_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

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
  (void)data;
  (void)iface_mask;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  const oc_rep_t* rep = request->request_payload;

  if (rep && rep->type == OC_REP_INT)
  {
    if (rep->value.integer == CoAP)
    {
      // this stack supports only CoAP (note, value is set on startup, will never be changed, no overwrite here)
      OC_DBG("swu protocol received : %d", swu_device.protocol);

      oc_prepare_cbor_response(request, OC_STATUS_CHANGED);
      return;
    }
  }

  oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
}

// resource definition, details/comments see on 'core_resource_well_known_core'
static oc_resource_data_t core_resource_knx_swu_protocol_data;
const oc_resource_t core_resource_knx_swu_protocol = {&core_resource_knx_swu_hwref,
                                                      {NULL, sizeof("/swu/protocol"), "/swu/protocol"},
                                                      {NULL, 0, NULL},
                                                      {NULL, sizeof("urn:knx:dpt.protocols"), "urn:knx:dpt.protocols"},
                                                      {APPLICATION_CBOR, CONTENT_NONE},
                                                      OC_DISCOVERABLE,
                                                      {oc_knx_swu_protocol_get_handler, NULL, OC_ACL_D, OC_IF_D},
                                                      {oc_knx_swu_protocol_put_handler, NULL, OC_ACL_SWU, OC_IF_SWU},
                                                      {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                      {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                      {{NULL}, NULL},
                                                      {{NULL}, NULL},
                                                      0,
                                                      0,
                                                      true,
                                                      &core_resource_knx_swu_protocol_data};

static void oc_knx_swu_hwref_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  oc_rep_begin_root_object();
  oc_rep_i_set_text_string(root, 1, oc_string(swu_device.hwref));
  oc_rep_end_root_object();

  oc_prepare_cbor_response(request, OC_STATUS_OK);
}

// resource definition, details/comments see on 'core_resource_well_known_core'
static oc_resource_data_t core_resource_knx_swu_hwref_data;
const oc_resource_t core_resource_knx_swu_hwref = {
  &core_resource_knx_swu_method,
  {NULL, sizeof("/swu/hwref"), "/swu/hwref"},
  {NULL, 0, NULL},
  {NULL, sizeof("urn:knx:dpt.varString8559_1"), "urn:knx:dpt.varString8559_1"},
  {APPLICATION_CBOR, CONTENT_NONE},
  OC_DISCOVERABLE,
  {oc_knx_swu_hwref_get_handler, NULL, OC_ACL_D, OC_IF_D},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {{NULL}, NULL},
  {{NULL}, NULL},
  0,
  0,
  true,
  &core_resource_knx_swu_hwref_data};

static void oc_knx_swu_method_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

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
  (void)data;
  (void)iface_mask;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  const oc_rep_t* rep = request->request_payload;

  if (rep && rep->type == OC_REP_INT)
  {
    if (rep->value.integer == PUSH)
    {
      // this stack supports only PUSH (note, value is set on startup, will never be changed, no overwrite here)
      OC_DBG("swu update method received : %d", swu_device.update_method);

      oc_prepare_cbor_response(request, OC_STATUS_CHANGED);
      return;
    }
    
  }

  oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
}

// resource definition, details/comments see on
// 'core_resource_well_known_core'
static oc_resource_data_t core_resource_knx_swu_method_data;
const oc_resource_t core_resource_knx_swu_method = {
  &core_resource_knx_swu_lastupdate,
  {NULL, sizeof("/swu/method"), "/swu/method"},
  {NULL, 0, NULL},
  {NULL, sizeof("urn:knx:dpt.transferMethod"), "urn:knx:dpt.transferMethod"},
  {APPLICATION_CBOR, CONTENT_NONE},
  OC_DISCOVERABLE,
  {oc_knx_swu_method_get_handler, NULL, OC_ACL_D, OC_IF_D},
  {oc_knx_swu_method_put_handler, NULL, OC_ACL_SWU, OC_IF_SWU},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {{NULL}, NULL},
  {{NULL}, NULL},
  0,
  0,
  true,
  &core_resource_knx_swu_method_data};

static void oc_knx_swu_last_update_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  oc_rep_begin_root_object();

  /* 
    KNX Specification
    - initial value -> date of manufacturing
    - if device HAS RTC    : after a software update = shows actual timestamp of last software update (RFC 3339)
    - if device has NO RTC : after a software update = shows "osv:true" 

   Note, before a software update the date of manufacturing has to be returned, regardless of any clock availability. 

   The KNX IoT stack needs an RTC such as to handle CoAP (CON) timeouts or brute force spake attempts, hence the logic 
   here is to return always the stored timestamp (it is either date of manufacturing or the last software update time, 
   set on application init).
  */

  oc_rep_i_set_text_string(root, 1, oc_string(swu_device.last_update));
  oc_rep_end_root_object();

  oc_prepare_cbor_response(request, OC_STATUS_OK);
}

// resource definition, details/comments see on
// 'core_resource_well_known_core'
static oc_resource_data_t core_resource_knx_swu_lastupdate_data;
const oc_resource_t core_resource_knx_swu_lastupdate = {
  &core_resource_knx_swu_result,
  {NULL, sizeof("/swu/lastupdate"), "/swu/lastupdate"},
  {NULL, 0, NULL},
  {NULL, sizeof("urn:knx:dpt.varString8859_1"), "urn:knx:dpt.varString8859_1"},
  {APPLICATION_CBOR, CONTENT_NONE},
  OC_DISCOVERABLE,
  {oc_knx_swu_last_update_get_handler, NULL, OC_ACL_D, OC_IF_D},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {{NULL}, NULL},
  {{NULL}, NULL},
  0,
  0,
  true,
  &core_resource_knx_swu_lastupdate_data};

static void oc_knx_swu_result_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  oc_rep_begin_root_object();
  oc_rep_i_set_int(root, 1, swu_device.result);
  oc_rep_end_root_object();

  oc_prepare_cbor_response(request, OC_STATUS_OK);
}

// resource definition, details/comments see on
// 'core_resource_well_known_core'
static oc_resource_data_t core_resource_knx_swu_result_data;
const oc_resource_t core_resource_knx_swu_result = {&core_resource_knx_swu_state,
                                                    {NULL, sizeof("/swu/result"), "/swu/result"},
                                                    {NULL, 0, NULL},
                                                    {NULL, sizeof("urn:knx:dpt.updateResult"), "urn:knx:dpt.updateResult"},
                                                    {APPLICATION_CBOR, CONTENT_NONE},
                                                    OC_DISCOVERABLE,
                                                    {oc_knx_swu_result_get_handler, NULL, OC_ACL_D, OC_IF_D},
                                                    {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                    {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                    {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                    {{NULL}, NULL},
                                                    {{NULL}, NULL},
                                                    0,
                                                    0,
                                                    true,
                                                    &core_resource_knx_swu_result_data};

static void oc_knx_swu_state_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  oc_rep_begin_root_object();
  oc_rep_i_set_int(root, 1, swu_device.state);
  oc_rep_end_root_object();

  oc_prepare_cbor_response(request, OC_STATUS_OK);
}

// resource definition, details/comments see on
// 'core_resource_well_known_core'
static oc_resource_data_t core_resource_knx_swu_state_data;
const oc_resource_t core_resource_knx_swu_state = {&core_resource_knx_swu_update,
                                                   {NULL, sizeof("/swu/state"), "/swu/state"},
                                                   {NULL, 0, NULL},
                                                   {NULL, sizeof("urn:knx:dpt.dldState"), "urn:knx:dpt.dldState"},
                                                   {APPLICATION_CBOR, CONTENT_NONE},
                                                   OC_DISCOVERABLE,
                                                   {oc_knx_swu_state_get_handler, NULL, OC_ACL_D, OC_IF_D},
                                                   {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                   {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                   {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                   {{NULL}, NULL},
                                                   {{NULL}, NULL},
                                                   0,
                                                   0,
                                                   true,
                                                   &core_resource_knx_swu_state_data};

// callback infrastructure for upgrade trigger, allows application to implement device-specific upgrade logic
typedef struct oc_swu_upgrade_t 
{
  oc_swu_upgrade_cb_t cb;   // application callback for upgrade trigger
  void* data;               // user context passed to callback
} oc_swu_upgrade_t;

static oc_swu_upgrade_t g_swu_upgrade_handler = {.cb = NULL, .data = NULL};

// Application registers upgrade callback before oc_main_init()
void oc_set_swu_upgrade_cb(oc_swu_upgrade_cb_t cb, void* data)
{
  g_swu_upgrade_handler.cb = cb;
  g_swu_upgrade_handler.data = data;
}

// Internal getter for callback (used by PUT /swu/update handler)
static const oc_swu_upgrade_t* oc_get_swu_upgrade_cb(void)
{
  return &g_swu_upgrade_handler;
}

// PUT handler for /swu/update - triggers the upgrade process
// Called after package is fully downloaded (state == DOWNLOADED)
static void oc_knx_swu_update_put_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  // Only accessible in DOWNLOADED state (per KNX IoT specification)
  // Client must wait for download to complete before triggering upgrade
  if (swu_device.state == OC_SWU_STATE_DOWNLOADED)
  {
    oc_rep_t* rep = request->request_payload;

    if (rep && rep->type == OC_REP_INT)
    {
      // payload contains defer time in seconds from MaC, device can delay upgrade to finish current operations
      swu_device.current_defer = rep->value.integer >= 0 ? (uint16_t)rep->value.integer : 0;
      
      // state transition: DOWNLOADED → UPDATING, stack manages this transition, application manages UPDATING → IDLE
      swu_device.state = OC_SWU_STATE_UPDATING;
      OC_DBG("SWU state: DOWNLOADED -> UPDATING");
      
      // Notify application to perform device-specific upgrade
      // Application responsibilities:
      // 1. Respect defer_time (delay before starting upgrade)
      // 2. Apply firmware from downloaded package
      // 3. Verify firmware integrity
      // 4. Update device firmware version: oc_core_set_and_store_device_fwv()
      // 5. Set result: oc_swu_set_result(OC_SWU_RESULT_SUCCESS/failure)
      // 6. Complete transition: oc_swu_set_state(OC_SWU_STATE_IDLE)
      
      const oc_swu_upgrade_t* swu_upgrade_cb = oc_get_swu_upgrade_cb();
      if (swu_upgrade_cb && swu_upgrade_cb->cb) 
      {
        swu_upgrade_cb->cb(swu_device.current_defer, swu_upgrade_cb->data);
      }
      
      // Send immediate response (upgrade happens asynchronously in application)
      oc_prepare_no_format_response_no_payload(request, OC_STATUS_CHANGED);
      return;
    }
  }

  // Return BAD_REQUEST if:
  // - Not in DOWNLOADED state
  // - Missing or invalid defer time parameter
  oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
}

// get after trigger the FWU package the remaining time to FWU will start
static void oc_knx_swu_update_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  /*
    KNX IOT Specification

    - in DOWNLOADED state the remaining time until the new firmware package
      will be applied is returned, while an ongoing download is answered with '4.00 Bad Request'
      
    - we assume here also in idle/updating state the remaining time is not relevant, 
      hence also '4.00 Bad Request' is returned

  */

  if (swu_device.state == OC_SWU_STATE_DOWNLOADED)
  {
    oc_rep_begin_root_object();
    oc_rep_i_set_uint(root, 1, swu_device.current_defer);
    oc_rep_end_root_object();
    oc_prepare_cbor_response(request, OC_STATUS_OK);
  }
  else
  {
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
  }
}

// resource definition, details/comments see on
// 'core_resource_well_known_core'
static oc_resource_data_t core_resource_knx_swu_update_data;
const oc_resource_t core_resource_knx_swu_update = {
  &core_resource_knx_swu_pkgv,
  {NULL, sizeof("/swu/update"), "/swu/update"},
  {NULL, 0, NULL},
  {NULL, sizeof("urn:knx:dpt.timePeriodSecZ"), "urn:knx:dpt.timePeriodSecZ"},
  {APPLICATION_CBOR, CONTENT_NONE},
  OC_DISCOVERABLE,
  {oc_knx_swu_update_get_handler, NULL, OC_ACL_D, OC_IF_D},
  {oc_knx_swu_update_put_handler, NULL, OC_ACL_SWU, OC_IF_SWU},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {{NULL}, NULL},
  {{NULL}, NULL},
  0,
  0,
  true,
  &core_resource_knx_swu_update_data};

static void oc_knx_swu_pkg_version_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  // Per KNX spec: "The version of the newly available software package
  // if the State resource is in state downloaded.
  // Otherwise, returns the response code '4.04 Not Found'."
  if (swu_device.state == OC_SWU_STATE_DOWNLOADED)
  {
    oc_rep_begin_root_object();
    const int64_t pkg_ver[3] = {swu_device.pkg_version.major, swu_device.pkg_version.minor, swu_device.pkg_version.patch};
    oc_rep_i_set_int_array(root, 1, pkg_ver, 3);
    oc_rep_end_root_object();

    oc_prepare_cbor_response(request, OC_STATUS_OK);
    return;
  }

  oc_prepare_no_format_response_no_payload(request, OC_STATUS_NOT_FOUND);
}

// resource definition, details/comments see on
// 'core_resource_well_known_core'
static oc_resource_data_t core_resource_knx_swu_pkgv_data;
const oc_resource_t core_resource_knx_swu_pkgv = {&core_resource_knx_swu_pkgcmd,
                                                  {NULL, sizeof("/swu/pkgv"), "/swu/pkgv"},
                                                  {NULL, 0, NULL},
                                                  {NULL, sizeof("urn:knx:dpt.version"), "urn:knx:dpt.version"},
                                                  {APPLICATION_CBOR, CONTENT_NONE},
                                                  OC_DISCOVERABLE,
                                                  {oc_knx_swu_pkg_version_get_handler, NULL, OC_ACL_D, OC_IF_D},
                                                  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                  {{NULL}, NULL},
                                                  {{NULL}, NULL},
                                                  0,
                                                  0,
                                                  true,
                                                  &core_resource_knx_swu_pkgv_data};

// a linked list for THE delayed response message for a (single) swu request (only one pending response is allowed)
static oc_separate_response_t delayed_separate_response_for_a_swu_request;

static void oc_knx_swu_a_put_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  char* key = NULL;
  char* value = NULL;
  size_t key_len = 0;
  size_t value_len = 0;

  int pkgs_package_size = 0;  // in bytes 
  int ps_block_size = 0;      // page size 
  int po_block_offset = 0;    // page offset, bytes to skip, default =0 if query parameter 'po' is missing

  

  OC_DBG("oc_knx_swu_a_put_handler - start");

  if (!oc_accept_header_is_ok(request, APPLICATION_OCTET_STREAM))
  {
    return;
  }

  oc_init_query_iterator();

  // scan all query parameter, object by object (note po/ps/pkgs may appear at the same time)
  while (oc_iterate_query(request, &key, &key_len, &value, &value_len) > 0)
  {
    if (strncmp(key, "po", key_len) == 0)
    {
      // validate: conversion error, invalid chars (only decimal digits allowed), out of range
      if (!oc_parse_sint32_from_base10_string(value, (int)value_len, &po_block_offset))
      {
        OC_ERR("invalid 'po' parameter");
        oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
        return;
      }
    }
    else if (strncmp(key, "ps", key_len) == 0)
    {
      // validate: conversion error, invalid chars (only decimal digits allowed), out of range, a block size of 0 is not allowed
      if (!oc_parse_sint32_from_base10_string(value, (int)value_len, &ps_block_size) || ps_block_size == 0)
      {
        OC_ERR("invalid 'ps' parameter");
        oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
        return;
      }
    }
    else if (strncmp(key, "pkgs", key_len) == 0)
    {
      /*
        first PUT SHALL contain the package size (in bytes), from second request it SHALL be ignored (if present),
        validate: conversion error, invalid chars (only decimal digits allowed), out of range, a package size of 0 is not allowed
      */
      if (!oc_parse_sint32_from_base10_string(value, (int)value_len, &pkgs_package_size) || pkgs_package_size == 0)
      {
        OC_ERR("invalid 'pkgs' parameter");
        oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
        return;
      }
    }
  }

  OC_DBG("binary package byte size: %d", pkgs_package_size);
  OC_DBG("block size: %d", ps_block_size);
  OC_DBG("block offset: %d", po_block_offset);

  // ===== State Machine: IDLE → DOWNLOADING =====
  // Transition occurs when first block with data arrives
  // - Resets byte counter to 0
  // - Resets result to INIT (clear any previous download status)
  // - Resets expected package size (cleanup from previous download)
  // - Clears package metadata (name, version) from previous download
  if (swu_device.state == OC_SWU_STATE_IDLE && request->_payload_len > 0)
  {
    swu_device.state = OC_SWU_STATE_DOWNLOADING;
    swu_device.pkg_bytes = 0;
    swu_device.result = OC_SWU_RESULT_INIT;
    expected_package_size = 0;  // Reset for new download (cleanup stale state)
    
    // Clear package metadata from previous download
    oc_swu_set_package_name("");
    oc_swu_set_package_version(0, 0, 0);
    
    OC_DBG("SWU state: IDLE -> DOWNLOADING (starting fresh download, metadata cleared)");
  }

  // Store total package size from first block
  // The 'pkgs' query parameter contains total size (appears only in first request)
  // Used later to detect download completion
  if (pkgs_package_size > 0 && expected_package_size == 0)
  {
    expected_package_size = pkgs_package_size;
    OC_DBG("Expected package size set to: %d", expected_package_size);
  }

  // Update download progress counter
  // Stack tracks total bytes received across all blocks
  swu_device.pkg_bytes += request->_payload_len;
  OC_DBG("Received bytes: %d / %d", swu_device.pkg_bytes, expected_package_size);

  // ===== Application Callback for Block Processing =====
  // Each block is forwarded to application for device-specific handling
  // (e.g., write to flash, file system, or buffer)
  const oc_swu_t* application_swu_cb = oc_get_swu_cb();

  if (application_swu_cb && application_swu_cb->cb)
  {
    // Prepare separate response infrastructure
    // Does NOT send ACK yet - allows application to choose response type
    oc_separate_response_t* sep_response = &delayed_separate_response_for_a_swu_request;
    memset(sep_response, 0, sizeof(oc_separate_response_t));
    
    // Forward block to application callback
    // Application receives:
    // - binary_size: Total package size (from pkgs, 0 for subsequent blocks)
    // - block_offset: Offset in package (from po query parameter)
    // - block_data: Actual data payload
    // - block_len: Length of this block
    // 
    // Application can:
    // 1. Fast path: Write to storage and return immediately
    //    → Stack sends piggybacked ACK 2.04
    // 2. Slow path: Call oc_set_delayed_callback() for async operation
    //    → Stack defers response, application sends separate CON later
    application_swu_cb->cb(sep_response, 
                           pkgs_package_size, 
                           po_block_offset, 
                           request->_payload,
                           request->_payload_len,
                           application_swu_cb->data);
    
    // ===== Response Handling =====
    // Check if application initiated delayed/separate response
    // (by calling oc_set_delayed_callback() or similar)
    if (!sep_response->active)
    {
      // Fast path: Application returned immediately
      // Send piggybacked ACK 2.04 Changed (CoAP spec compliant for fast operations)
      oc_prepare_no_format_response_no_payload(request, OC_STATUS_CHANGED);
      OC_DBG("oc_knx_swu_a_put_handler: sending piggybacked response");
    }
    else
    {
      // Slow path: Application initiated delayed response
      // Application is responsible for sending separate CON response later
      OC_DBG("oc_knx_swu_a_put_handler: callback initiated delayed separate response");
    }
  }
  else
  {
    // No application callback registered
    OC_ERR("SWU callback not registered - cannot process firmware blocks");
    // Send 5.01 Not Implemented if no handler is available
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_NOT_IMPLEMENTED);
  }
  
  // ===== State Machine: DOWNLOADING → DOWNLOADED =====
  // Transition occurs when all bytes have been received
  // Stack detects completion by comparing pkg_bytes with expected_package_size
  if (expected_package_size > 0 && swu_device.pkg_bytes >= expected_package_size && 
      swu_device.state == OC_SWU_STATE_DOWNLOADING)
  {
    // Mark download as complete
    swu_device.state = OC_SWU_STATE_DOWNLOADED;
    
    OC_DBG("SWU state: DOWNLOADING -> DOWNLOADED (received %d bytes)", swu_device.pkg_bytes);
    
    // ===== Application Responsibility: Package Metadata =====
    // Application should detect completion in parallel and set package metadata:
    // 
    // Detection in swu_cb():
    //   static size_t total_received = 0;
    //   total_received += block_len;
    //   if (total_received >= binary_size) {
    //     // Download complete!
    //   }
    // 
    // Set metadata:
    //   oc_swu_set_package_version(major, minor, patch);  // From package header or known version
    //   oc_swu_set_package_name("firmware.bin");          // Package filename
    // 
    // Note: Version should ideally be extracted from package header, for testing, can use placeholder values
  }

  OC_DBG("oc_knx_swu_a_put_handler - end");
}

// resource definition, details/comments see on 'core_resource_well_known_core'
static oc_resource_data_t core_resource_knx_swu_pkgcmd_data;
const oc_resource_t core_resource_knx_swu_pkgcmd = {&core_resource_knx_swu_pkgbytes,
                                                    {NULL, sizeof("/a/swu"), "/a/swu"},
                                                    {NULL, 0, NULL},
                                                    {NULL, sizeof("urn:knx:dpt.file"), "urn:knx:dpt.file"},
                                                    {APPLICATION_OCTET_STREAM, CONTENT_NONE},
                                                    OC_DISCOVERABLE,
                                                    {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                    {oc_knx_swu_a_put_handler, NULL, OC_ACL_SWU, OC_IF_SWU},
                                                    {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                    {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                    {{NULL}, NULL},
                                                    {{NULL}, NULL},
                                                    0,
                                                    0,
                                                    true,
                                                    &core_resource_knx_swu_pkgcmd_data};

static void oc_knx_swu_bytes_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  oc_rep_begin_root_object();
  oc_rep_i_set_int(root, 1, swu_device.pkg_bytes);
  oc_rep_end_root_object();

  oc_prepare_cbor_response(request, OC_STATUS_OK);
}

// resource definition, details/comments see on
// 'core_resource_well_known_core'
static oc_resource_data_t core_resource_knx_swu_pkgbytes_data;
const oc_resource_t core_resource_knx_swu_pkgbytes = {&core_resource_knx_swu_pkgnames,
                                                      {NULL, sizeof("/swu/pkgbytes"), "/swu/pkgbytes"},
                                                      {NULL, 0, NULL},
                                                      {NULL, sizeof("urn:knx:dpt.value4UCount"), "urn:knx:dpt.value4UCount"},
                                                      {APPLICATION_CBOR, CONTENT_NONE},
                                                      OC_DISCOVERABLE,
                                                      {oc_knx_swu_bytes_get_handler, NULL, OC_ACL_D, OC_IF_D},
                                                      {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                      {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                      {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                      {{NULL}, NULL},
                                                      {{NULL}, NULL},
                                                      0,
                                                      0,
                                                      true,
                                                      &core_resource_knx_swu_pkgbytes_data};

static void oc_knx_swu_pkg_name_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  // Per KNX spec: "The package name of the non-active software package.
  // Returns response code '4.04 Not Found' if no non-active package is available."
  // Non-active package is available only in DOWNLOADED state
  if (swu_device.state == OC_SWU_STATE_DOWNLOADED)
  {
    oc_rep_begin_root_object();
    oc_rep_i_set_text_string(root, 1, oc_string(swu_device.pkg_name));
    oc_rep_end_root_object();

    oc_prepare_cbor_response(request, OC_STATUS_OK);
    return;
  }

  oc_prepare_no_format_response_no_payload(request, OC_STATUS_NOT_FOUND);
}

// resource definition, details/comments see on
// 'core_resource_well_known_core'
static oc_resource_data_t core_resource_knx_swu_pkgnames_data;
const oc_resource_t core_resource_knx_swu_pkgnames = {
  &core_resource_knx_swu,
  {NULL, sizeof("/swu/pkgname"), "/swu/pkgname"},
  {NULL, 0, NULL},
  {NULL, sizeof("urn:knx:dpt.varString8859_1"), "urn:knx:dpt.varString8859_1"},
  {APPLICATION_CBOR, CONTENT_NONE},
  OC_DISCOVERABLE,
  {oc_knx_swu_pkg_name_get_handler, NULL, OC_ACL_D, OC_IF_D},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {{NULL}, NULL},
  {{NULL}, NULL},
  0,
  0,
  true,
  &core_resource_knx_swu_pkgnames_data};

static void oc_core_knx_swu_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  int query_parameter_kvpair_matches = 0; // how many query parameter key/value pair matches where found
  size_t response_length = 0;
  int query_pn = PAGE_NUMBER;
  int query_ps = PAGE_SIZE;

  int first_entry = OC_KNX_SWU_PROTOCOL; // first entry number of a resource that will be placed on a page
  int last_entry = OC_KNX_SWU; // last entry number of a resource that will be placed on a page
  int total = last_entry - first_entry; // total entries of this resource
  bool more_request_needed = false;

  OC_DBG("oc_core_swu_get_handler - start");

  if (!oc_accept_header_is_ok(request, APPLICATION_LINK_FORMAT))
  {
    return;
  }

  // handle query parameters l=ps and/or l=total
  if (query_l_was_processed(request, PAGE_SIZE, total))
    return;

  // first entry number of a resource that will be placed on a page
  first_entry += evaluate_query_px(request, &query_pn, &query_ps);

  /* check if requested page will carry at least one resource e.g
     - total=4, pn 5, ps 20, first entry = 100 -> no data on page 5 (all on page 0)
     - total=4, pn 1, ps 04, first entry = 004 -> no data on page 1 (all on page 0)
     - ps < 1 is invalid
     - pn < 0 is invalid

  */
  if (first_entry >= last_entry || query_ps < 1 || query_pn < 0)
  {
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  // entries don't fit in a single page -> more pages are needed to get the full
  // list
  // - total=4, page number 1, page size 02, first entry = 002 -> no more data on next page
  // - total=4, page number 1, page size 01, first entry = 001 -> more data on next page
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
  OC_DBG("oc_core_swu_get_handler - end");
}

// resource definition, details/comments see on
// 'core_resource_well_known_core'
static oc_resource_data_t core_resource_knx_swu_data;
const oc_resource_t core_resource_knx_swu = {&core_resource_sub,
                                             {NULL, sizeof("/swu"), "/swu"},
                                             {NULL, (size_t)1 * 32, (char[1][32]){"urn:knx:fb.swu"}},
                                             {NULL, 0, NULL},
                                             {APPLICATION_LINK_FORMAT, CONTENT_NONE},
                                             OC_DISCOVERABLE,
                                             {oc_core_knx_swu_get_handler, NULL, OC_ACL_P | OC_ACL_D | OC_ACL_C, OC_IF_LI},
                                             {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                             {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                             {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                             {{NULL}, NULL},
                                             {{NULL}, NULL},
                                             0,
                                             0,
                                             true,
                                             &core_resource_knx_swu_data};

void oc_create_knx_swu_resources(void)
{
  OC_DBG("oc_create_knx_swu_resources");

  // set default values
  oc_swu_set_package_name("");
  oc_swu_set_hwref("0102030405ABCDEF");

  // free by default before set storage or manufacturing data
  oc_free_string(&swu_device.last_update);

  char last_update_buf[64] = "";
  const long ret = oc_storage_read(KNX_STORAGE_SWU_LAST_UPDATE, (uint8_t*)last_update_buf, sizeof(last_update_buf) - 1);
  if (ret > 0) 
  { // try to load last_update from storage (persists across reboots)
    oc_new_string(&swu_device.last_update, last_update_buf, ret);
  }
  else
  { // set device manufacturing date as default for last update (see Application Init/Startup)
    const oc_device_info_t* device = oc_core_get_device_info();
    oc_new_string(&swu_device.last_update, device->mfg_date, strlen(device->mfg_date));
  }

  OC_DBG("Using %s date for last_update : %s", ret > 0 ? "manufacturing" : "storage", oc_string(swu_device.last_update));
}

void oc_swu_set_package_name(const char* name)
{
  oc_free_string(&swu_device.pkg_name);
  oc_new_string(&swu_device.pkg_name, name, strlen(name));
}

void oc_swu_set_last_update(const char* time)
{
  oc_free_string(&swu_device.last_update);
  oc_new_string(&swu_device.last_update, time, strlen(time));
  
  // Persist to storage so timestamp survives reboot
  oc_storage_write(KNX_STORAGE_SWU_LAST_UPDATE, (uint8_t*)oc_string(swu_device.last_update), oc_string_len(swu_device.last_update));
}

void oc_swu_set_hwref(const char* hwref)
{
  oc_free_string(&swu_device.hwref);
  oc_new_string(&swu_device.hwref, hwref, strlen(hwref));
}

void oc_swu_set_package_bytes(const int package_bytes) { swu_device.pkg_bytes = package_bytes; }

void oc_swu_set_package_version(const int major, const int minor, const int patch)
{
  swu_device.pkg_version.major = major;
  swu_device.pkg_version.minor = minor;
  swu_device.pkg_version.patch = patch;
}

void oc_swu_set_state(const oc_swu_state_t state) { swu_device.state = state; }

void oc_swu_set_result(const oc_swu_result_t result) { swu_device.result = result; }
