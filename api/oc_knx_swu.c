/*
 // Copyright (c) 2021 Cascoda Ltd
 // Copyright (c) 2025 KNX Association
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

#include "oc_knx_swu.h"
#include "include/oc_helpers.h"
#include "include/oc_ri.h"
#include "oc_api.h"
#include "oc_core_res.h"
#include "oc_discovery.h"
#include "oc_knx_helpers.h"
#include "oc_main.h"
#include "port/oc_storage.h"

// only static since values are changed at runtime
static oc_device_swu_t swu_device = {
  0,     0,
  PUSH, {NULL, 0, NULL},
  {NULL, 0, NULL}, 0,
  {0, 0, 0}, OC_SWU_STATE_IDLE,
  {NULL, 0, NULL}, OC_SWU_RESULT_INIT,
  false,
  CoAP,
  {NULL, 0, NULL}
};

// below data can be set (PUT) all other can only be read
#define KNX_STORAGE_SWU_MAX_DEFER "swu_knx_max_defer"
#define KNX_STORAGE_SWU_METHOD "swu_knx_method"
#define KNX_STORAGE_SWU_PROTOCOL "swu_knx_protocol"
#define KNX_STORAGE_QUERY_URL "swu_knx_query_url"

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
    OC_DBG("oc_knx_swu_protocol_put_handler received : %d", (int)rep->value.integer);

    if (rep->value.integer == CoAP)
    {
      // allow only CoAP to be written, otherwise bad request
      // value is already set by init ... but store it again and save to storage

      swu_device.protocol = CoAP;
      oc_storage_write(KNX_STORAGE_SWU_PROTOCOL, (uint8_t*)&swu_device.protocol, sizeof(swu_device.protocol));

      oc_prepare_cbor_response(request, OC_STATUS_CHANGED);
      return;
    }
  }

  oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
}

// resource definition, details/comments see on
// 'core_resource_well_known_core_final'
extern const oc_resource_t core_resource_knx_swu_maxdefer;
PRAGMA_IN oc_resource_data_t core_resource_knx_swu_protocol_data;
const oc_resource_t core_resource_knx_swu_protocol = {(oc_resource_t*)&core_resource_knx_swu_maxdefer,
                                                      {NULL, sizeof("/swu/protocol"), "/swu/protocol"},
                                                      {NULL, 0, NULL},
                                                      {NULL, sizeof("urn:knx:dpt.protocols"), "urn:knx:dpt.protocols"},
                                                      {APPLICATION_CBOR, CONTENT_NONE},
                                                      OC_DISCOVERABLE,
                                                      {oc_knx_swu_protocol_get_handler, NULL, OC_ACL_D, OC_IF_D},
                                                      {oc_knx_swu_protocol_put_handler, NULL, OC_ACL_SWU, OC_IF_SWU},
                                                      {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                      {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                      {NULL, NULL},
                                                      {NULL, NULL},
                                                      0,
                                                      0,
                                                      true,
                                                      &core_resource_knx_swu_protocol_data};
PRAGMA_OUT

static void oc_knx_swu_max_defer_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

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
  (void)data;
  (void)iface_mask;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  oc_rep_t* rep = request->request_payload;

  if (rep && rep->type == OC_REP_INT)
  {
    OC_DBG("oc_knx_swu_max_defer_put_handler received : %d", (int)rep->value.integer);
    swu_device.max_defer = (int)rep->value.integer;
    oc_storage_write(KNX_STORAGE_SWU_MAX_DEFER, (uint8_t*)&swu_device.max_defer, sizeof(swu_device.max_defer));
    oc_prepare_cbor_response(request, OC_STATUS_OK);
    return;
  }

  oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
}

// resource definition, details/comments see on
// 'core_resource_well_known_core_final'
extern const oc_resource_t core_resource_knx_swu_hwref;
PRAGMA_IN oc_resource_data_t core_resource_knx_swu_maxdefer_data;
const oc_resource_t core_resource_knx_swu_maxdefer = {
  (oc_resource_t*)&core_resource_knx_swu_hwref,
  {NULL, sizeof("/swu/maxdefer"), "/swu/maxdefer"},
  {NULL, 0, NULL},
  {NULL, sizeof("urn:knx:dpt.timePeriodSec"), "urn:knx:dpt.timePeriodSec"},
  {APPLICATION_CBOR, CONTENT_NONE},
  OC_DISCOVERABLE,
  {oc_knx_swu_max_defer_get_handler, NULL, OC_ACL_D, OC_IF_D},
  {oc_knx_swu_max_defer_put_handler, NULL, OC_ACL_SWU, OC_IF_SWU},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL},
  {NULL, NULL},
  0,
  0,
  true,
  &core_resource_knx_swu_maxdefer_data};
PRAGMA_OUT

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

// resource definition, details/comments see on
// 'core_resource_well_known_core_final'
extern const oc_resource_t core_resource_knx_swu_method;
PRAGMA_IN oc_resource_data_t core_resource_knx_swu_hwref_data;
const oc_resource_t core_resource_knx_swu_hwref = {
  (oc_resource_t*)&core_resource_knx_swu_method,
  {NULL, sizeof("/swu/hwref"), "/swu/hwref"},
  {NULL, 0, NULL},
  
  {NULL, sizeof("urn:knx:dpt.varString8559_1"), "urn:knx:dpt.varString8559_1"},
  {APPLICATION_CBOR, CONTENT_NONE},
  OC_DISCOVERABLE,
  {oc_knx_swu_hwref_get_handler, NULL, OC_ACL_D, OC_IF_D},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL},
  {NULL, NULL},
  0,
  0,
  true,
  &core_resource_knx_swu_hwref_data};
PRAGMA_OUT

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

  oc_rep_t* rep = request->request_payload;

  if (rep && rep->type == OC_REP_INT)
  {
    OC_DBG("oc_knx_swu_method_put_handler received : %d", (int)rep->value.integer);

    if (rep->value.integer == PUSH)
    {
      // allow only PUSH method for this stack
      swu_device.update_method = PUSH;
      oc_storage_write(KNX_STORAGE_SWU_METHOD, (uint8_t*)&swu_device.update_method, sizeof(swu_device.update_method));
      oc_prepare_cbor_response(request, OC_STATUS_OK);
      return;
    }
  }

  oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
}

// resource definition, details/comments see on
// 'core_resource_well_known_core_final'
extern const oc_resource_t core_resource_knx_lastupdate;
PRAGMA_IN oc_resource_data_t core_resource_knx_swu_method_data;
const oc_resource_t core_resource_knx_swu_method = {
  (oc_resource_t*)&core_resource_knx_lastupdate,
  {NULL, sizeof("/swu/method"), "/swu/method"},
  {NULL, 0, NULL},
  {NULL, sizeof("urn:knx:dpt.transferMethod"), "urn:knx:dpt.transferMethod"},
  {APPLICATION_CBOR, CONTENT_NONE},
  OC_DISCOVERABLE,
  {oc_knx_swu_method_get_handler, NULL, OC_ACL_D, OC_IF_D},
  {oc_knx_swu_method_put_handler, NULL, OC_ACL_SWU, OC_IF_SWU},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL},
  {NULL, NULL},
  0,
  0,
  true,
  &core_resource_knx_swu_method_data};
PRAGMA_OUT

static void oc_knx_swu_last_update_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

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

// resource definition, details/comments see on
// 'core_resource_well_known_core_final'
extern const oc_resource_t core_resource_knx_swu_result;
PRAGMA_IN oc_resource_data_t core_resource_knx_lastupdate_data;
const oc_resource_t core_resource_knx_lastupdate = {
  (oc_resource_t*)&core_resource_knx_swu_result,
  {NULL, sizeof("/swu/lastupdate"), "/swu/lastupdate"},
  {NULL, 0, NULL},
  {NULL, sizeof("urn:knx:dpt.varString8859_1"), "urn:knx:dpt.varString8859_1"},
  {APPLICATION_CBOR, CONTENT_NONE},
  OC_DISCOVERABLE,
  {oc_knx_swu_last_update_get_handler, NULL, OC_ACL_D, OC_IF_D},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL},
  {NULL, NULL},
  0,
  0,
  true,
  &core_resource_knx_lastupdate_data};
PRAGMA_OUT

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
// 'core_resource_well_known_core_final'
extern const oc_resource_t core_resource_knx_swu_state;
PRAGMA_IN oc_resource_data_t core_resource_knx_swu_result_data;
const oc_resource_t core_resource_knx_swu_result = {(oc_resource_t*)&core_resource_knx_swu_state,
                                                    {NULL, sizeof("/swu/result"), "/swu/result"},
                                                    {NULL, 0, NULL},
                                                    {NULL, sizeof("urn:knx:dpt.updateResult"), "urn:knx:dpt.updateResult"},
                                                    {APPLICATION_CBOR, CONTENT_NONE},
                                                    OC_DISCOVERABLE,
                                                    {oc_knx_swu_result_get_handler, NULL, OC_ACL_D, OC_IF_D},
                                                    {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                    {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                    {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                    {NULL, NULL},
                                                    {NULL, NULL},
                                                    0,
                                                    0,
                                                    true,
                                                    &core_resource_knx_swu_result_data};
PRAGMA_OUT

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
// 'core_resource_well_known_core_final'
extern const oc_resource_t core_resource_knx_swu_update;
PRAGMA_IN oc_resource_data_t core_resource_knx_swu_state_data;
const oc_resource_t core_resource_knx_swu_state = {(oc_resource_t*)&core_resource_knx_swu_update,
                                                   {NULL, sizeof("/swu/state"), "/swu/state"},
                                                   {NULL, 0, NULL},
                                                   {NULL, sizeof("urn:knx:dpt.dldState"), "urn:knx:dpt.dldState"},
                                                   {APPLICATION_CBOR, CONTENT_NONE},
                                                   OC_DISCOVERABLE,
                                                   {oc_knx_swu_state_get_handler, NULL, OC_ACL_D, OC_IF_D},
                                                   {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                   {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                   {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                   {NULL, NULL},
                                                   {NULL, NULL},
                                                   0,
                                                   0,
                                                   true,
                                                   &core_resource_knx_swu_state_data};
PRAGMA_OUT

// trigger for upgrading the FWU package after a download
static void oc_knx_swu_update_put_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  // only accessible in this state (see specification)
  if (swu_device.state == OC_SWU_STATE_DOWNLOADED)
  {
    oc_rep_t* rep = request->request_payload;

    if (rep && rep->type == OC_REP_INT)
    {
      // value of current defer time in sec
      // TODO timer to start FWU not implemented
      swu_device.current_defer = (int)rep->value.integer;
      oc_prepare_cbor_response(request, OC_STATUS_OK);
      return;
    }
  }

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

  // only accessible in this state (see specification)
  if (swu_device.state == OC_SWU_STATE_DOWNLOADED)
  {
    oc_rep_begin_root_object();
    oc_rep_i_set_int(root, 1, swu_device.current_defer);
    oc_rep_end_root_object();
  }

  oc_prepare_cbor_response(request, OC_STATUS_OK);
}

// resource definition, details/comments see on
// 'core_resource_well_known_core_final'
extern const oc_resource_t core_resource_knx_swu_pkgv;
PRAGMA_IN oc_resource_data_t core_resource_knx_swu_update_data;
const oc_resource_t core_resource_knx_swu_update = {
  (oc_resource_t*)&core_resource_knx_swu_pkgv,
  {NULL, sizeof("/swu/update"), "/swu/update"},
  {NULL, 0, NULL},
  {NULL, sizeof("urn:knx:dpt.timePeriodSecZ"), "urn:knx:dpt.timePeriodSecZ"},
  {APPLICATION_CBOR, CONTENT_NONE},
  OC_DISCOVERABLE,
  {oc_knx_swu_update_get_handler, NULL, OC_ACL_D, OC_IF_D},
  {oc_knx_swu_update_put_handler, NULL, OC_ACL_SWU, OC_IF_SWU},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL},
  {NULL, NULL},
  0,
  0,
  true,
  &core_resource_knx_swu_update_data};
PRAGMA_OUT

static void oc_knx_swu_pkg_version_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  if (swu_device.downloaded_once)
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
// 'core_resource_well_known_core_final'
extern const oc_resource_t core_resource_knx_swu_pkgcmd;
PRAGMA_IN oc_resource_data_t core_resource_knx_swu_pkgv_data;
const oc_resource_t core_resource_knx_swu_pkgv = {(oc_resource_t*)&core_resource_knx_swu_pkgcmd,
                                                  {NULL, sizeof("/swu/pkgv"), "/swu/pkgv"},
                                                  {NULL, 0, NULL},
                                                  {NULL, sizeof("urn:knx:dpt.version"), "urn:knx:dpt.version"},
                                                  {APPLICATION_CBOR, CONTENT_NONE},
                                                  OC_DISCOVERABLE,
                                                  {oc_knx_swu_pkg_version_get_handler, NULL, OC_ACL_D, OC_IF_D},
                                                  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                  {NULL, NULL},
                                                  {NULL, NULL},
                                                  0,
                                                  0,
                                                  true,
                                                  &core_resource_knx_swu_pkgv_data};
PRAGMA_OUT

// a fix delayed response message for the swu methods...
static oc_separate_response_t s_delayed_response_swu;

static void oc_knx_swu_a_put_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  char* key = NULL;
  char* value = NULL;
  size_t key_len = 0;
  size_t value_len = 0;

  int binary_size = 0;
  int block_size = 0;
  int block_offset = 0; // bytes to skip, default if query parameter 'po' is missing

  const uint8_t* payload_ptr = NULL;
  size_t payload_size = 0;

  OC_DBG("oc_knx_swu_a_put_handler - start");

  if (!oc_accept_header_is_ok(request, APPLICATION_OCTET_STREAM))
  {
    return;
  }

  oc_init_query_iterator();

  // scan all query parameter
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
      // first PUT contains the size, ignore from second (if present)
      binary_size = atoi(value);
    }
  }

  OC_DBG("binary size: %d", binary_size);
  OC_DBG("block size: %d", block_size);
  OC_DBG("block offset: %d", block_offset);

  // if swu blob data are present ...
  if (request->_payload && request->_payload_len > 0)
  {
    payload_ptr = request->_payload;
    payload_size = request->_payload_len;
  }

  // get application FWU handler
  // - is usually device hardware and application specific and needs some processing time
  const oc_swu_t* application_swu_cb = oc_get_swu_cb();

  if (application_swu_cb && application_swu_cb->cb)
  {
    oc_indicate_separate_response(request, &s_delayed_response_swu);
    // call application handler including user data (can be NULL)
    application_swu_cb->cb(&s_delayed_response_swu, binary_size, block_offset, payload_ptr, payload_size,
                           application_swu_cb->data);
  }
  else
  {
    oc_prepare_cbor_response(request, OC_STATUS_OK);
  }

  OC_DBG("oc_knx_swu_a_put_handler - end");
}

// resource definition, details/comments see on
// 'core_resource_well_known_core_final'
extern const oc_resource_t core_resource_knx_swu_pkgbytes;
PRAGMA_IN oc_resource_data_t core_resource_knx_swu_pkgcmd_data;
const oc_resource_t core_resource_knx_swu_pkgcmd = {(oc_resource_t*)&core_resource_knx_swu_pkgbytes,
                                                    {NULL, sizeof("/a/swu"), "/a/swu"},
                                                    {NULL, 0, NULL},
                                                    {NULL, sizeof("urn:knx:dpt.file"), "urn:knx:dpt.file"},
                                                    {APPLICATION_OCTET_STREAM, CONTENT_NONE},
                                                    OC_DISCOVERABLE,
                                                    {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                    {oc_knx_swu_a_put_handler, NULL, OC_ACL_SWU, OC_IF_SWU},
                                                    {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                    {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                    {NULL, NULL},
                                                    {NULL, NULL},
                                                    0,
                                                    0,
                                                    true,
                                                    &core_resource_knx_swu_pkgcmd_data};
PRAGMA_OUT

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
// 'core_resource_well_known_core_final'
extern const oc_resource_t core_resource_knx_swu_pkgqurl;
PRAGMA_IN oc_resource_data_t core_resource_knx_swu_pkgbytes_data;
const oc_resource_t core_resource_knx_swu_pkgbytes = {(oc_resource_t*)&core_resource_knx_swu_pkgqurl,
                                                      {NULL, sizeof("/swu/pkgbytes"), "/swu/pkgbytes"},
                                                      {NULL, 0, NULL},
                                                      {NULL, sizeof("urn:knx:dpt.value4UCount"), "urn:knx:dpt.value4UCount"},
                                                      {APPLICATION_CBOR, CONTENT_NONE},
                                                      OC_DISCOVERABLE,
                                                      {oc_knx_swu_bytes_get_handler, NULL, OC_ACL_D, OC_IF_D},
                                                      {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                      {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                      {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                      {NULL, NULL},
                                                      {NULL, NULL},
                                                      0,
                                                      0,
                                                      true,
                                                      &core_resource_knx_swu_pkgbytes_data};
PRAGMA_OUT

static void oc_knx_swu_pkg_query_url_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

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
  (void)data;
  (void)iface_mask;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  oc_rep_t* rep = request->request_payload;

  if (rep && rep->type == OC_REP_STRING)
  {
    OC_DBG("oc_knx_swu_pkg_query_url_put_handler received : %s", oc_string_checked(rep->value.string));

    oc_swu_set_query_url(oc_string_checked(rep->value.string));
    oc_storage_write(KNX_STORAGE_QUERY_URL, (uint8_t*)&swu_device.query_url, oc_string_len(swu_device.query_url));
    oc_prepare_cbor_response(request, OC_STATUS_OK);
    return;
  }

  oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
}

// resource definition, details/comments see on
// 'core_resource_well_known_core_final'
extern const oc_resource_t core_resource_knx_swu_pkgnames;
PRAGMA_IN oc_resource_data_t core_resource_knx_swu_pkgqurl_data;
const oc_resource_t core_resource_knx_swu_pkgqurl = {(oc_resource_t*)&core_resource_knx_swu_pkgnames,
                                                     {NULL, sizeof("/swu/pkgqurl"), "/swu/pkgqurl"},
                                                     {NULL, 0, NULL},
                                                     {NULL, sizeof("urn:knx:dpt.url"), "urn:knx:dpt.url"},
                                                     {APPLICATION_CBOR, CONTENT_NONE},
                                                     OC_DISCOVERABLE,
                                                     {oc_knx_swu_pkg_query_url_get_handler, NULL, OC_ACL_D, OC_IF_D},
                                                     {oc_knx_swu_pkg_query_url_put_handler, NULL, OC_ACL_SWU, OC_IF_SWU},
                                                     {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                     {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                     {NULL, NULL},
                                                     {NULL, NULL},
                                                     0,
                                                     0,
                                                     true,
                                                     &core_resource_knx_swu_pkgqurl_data};
PRAGMA_OUT

static void oc_knx_swu_pkg_name_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

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

// resource definition, details/comments see on
// 'core_resource_well_known_core_final'
extern const oc_resource_t core_resource_knx_swu;
PRAGMA_IN oc_resource_data_t core_resource_knx_swu_pkgnames_data;
const oc_resource_t core_resource_knx_swu_pkgnames = {
  (oc_resource_t*)&core_resource_knx_swu,
  {NULL, sizeof("/swu/pkgname"), "/swu/pkgname"},
  {NULL, 0, NULL},
  {NULL, sizeof("urn:knx:dpt.varString8859_1"), "urn:knx:dpt.varString8859_1"},
  {APPLICATION_CBOR, CONTENT_NONE},
  OC_DISCOVERABLE,
  {oc_knx_swu_pkg_name_get_handler, NULL, OC_ACL_D, OC_IF_D},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
  {NULL, NULL},
  {NULL, NULL},
  0,
  0,
  true,
  &core_resource_knx_swu_pkgnames_data};
PRAGMA_OUT

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

  // check if requested page will carry at least one resource e.g
  // - total=4, pn 5, ps 20, first entry = 100 -> no data on page 5 (all on page 0)
  // - total=4, pn 1, ps 04, first entry = 004 -> no data on page 1 (all on page 0)
  if (first_entry >= last_entry || query_ps == 0)
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
    const oc_resource_t* resource = oc_core_get_resource_by_index(i);
    if (oc_check_resource_by_request(resource, request, &response_length, &i, i, true))
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
// 'core_resource_well_known_core_final'
extern const oc_resource_t core_resource_knx_a_sen;
PRAGMA_IN oc_resource_data_t core_resource_knx_swu_data;
const oc_resource_t core_resource_knx_swu = {(oc_resource_t*)&core_resource_knx_a_sen,
                                             {NULL, sizeof("/swu"), "/swu"},
                                             {NULL, (size_t)1 * 32, ((char[1][32]){"urn:knx:fb.swu"})},
                                             {NULL, 0, NULL},
                                             {APPLICATION_LINK_FORMAT, CONTENT_NONE},
                                             OC_DISCOVERABLE,
                                             {oc_core_knx_swu_get_handler, NULL, OC_ACL_P | OC_ACL_D | OC_ACL_C, OC_IF_LI},
                                             {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                             {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                             {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                             {NULL, NULL},
                                             {NULL, NULL},
                                             0,
                                             0,
                                             true,
                                             &core_resource_knx_swu_data};
PRAGMA_OUT

void oc_create_knx_swu_resources(void)
{
  OC_DBG("oc_create_knx_swu_resources");

  // create missing runtime variables for device 0
  // - no SWU name for the package (never downloaded)
  // - manufacturing date (artificial, value used from EITT KNX certification tests)
  // - hw reference (artificial, value used from EITT KNX certification tests)
  oc_swu_set_package_name("");
  oc_swu_set_last_update("2020-04-12T23:20:50.52Z");
  oc_swu_set_hwref("0102030405ABCDEF");

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

void oc_swu_set_query_url(const char* url)
{
  oc_free_string(&swu_device.query_url);
  oc_new_string(&swu_device.query_url, url, strlen(url));
}

void oc_swu_set_result(const oc_swu_result_t result) { swu_device.result = result; }
