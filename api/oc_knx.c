/*
 // Copyright (c) 2021-2022 Cascoda Ltd
 // Copyright (c) 2024-2025 KNX Association
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

#include <oc_storage.h>
#include "oc_knx.h"
#include "api/oc_knx_helpers.h"
#include "oc_api.h"
#include "oc_core_res.h"
#include "oc_knx_client.h"
#include "oc_knx_dev.h"
#include "oc_knx_fp.h"
#include "oc_knx_sec.h"
#include "oc_main.h"
#include "oc_oscore_context.h"
#include "oc_rep.h"
#include "port/dns-sd.h"

#define __STDC_FORMAT_MACROS // defined to use format specifiers also in C++

#ifdef OC_SPAKE
#include "security/oc_spake2plus.h"
#endif

// ---------------------------Variables --------------------------------------

static uint64_t g_fingerprint = 0;  // covers GO/PUB/SUB table and 'P' parameters
static oc_pase_t g_pase;            // holds the negotiated pase parameter (IMPORTANT consider the notes on oc_pase_t type definition)
static oc_string_t g_idevid;
static oc_string_t g_ldevid;
static int pase_step = 0;           // covers the current running pase step 

// ----------------------------------------------------------------------------

enum SpakeKeys
{
  SPAKE_ID = 0,
  SPAKE_SALT = 5,
  SPAKE_PW = 8, // for device handover, not implemented yet
  SPAKE_PA_SHARE_P = 10,
  SPAKE_PB_SHARE_V = 11,
  SPAKE_PBKDF2 = 12,
  SPAKE_CB_CONFIRM_V = 13,
  SPAKE_CA_CONFIRM_P = 14,
  SPAKE_RND = 15,
  SPAKE_IT = 16,
};

static int convert_cmd(char* cmd)
{
#define RESTART_DEVICE 2
#define RESET_DEVICE 1

  if (strncmp(cmd, "reset", strlen("reset")) == 0)
  {
    return RESET_DEVICE;
  }
  if (strncmp(cmd, "restart", strlen("restart")) == 0)
  {
    return RESTART_DEVICE;
  }

  OC_DBG("convert_cmd command not recognized: %s", cmd);
  return 0;
}

/*
  payload example:
  {
    "api": {
      "version" : "1.0.0",
      "base": "/"
    }
  }
  note that base path and version cannot be set from outside, hence below used as fixed constants
*/
static void oc_core_knx_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  // this EP MUST support JSON in addition (KNX IoT specification clause 5.1.3)
  if (!oc_accept_header_is_ok(request, APPLICATION_JSON) && !oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  if (request->accept == APPLICATION_JSON)
  {
    // no begin/end object is needed, it raps only the raw content
    oc_rep_add_line_to_buffer("{\"api\": {\"base\": \"/\", \"version\": \"1.0.0\" }}");  // TODO will be 1.1.0
    oc_prepare_json_response(request, OC_STATUS_OK);
  }
  else
  {
    oc_rep_begin_root_object();
    oc_rep_set_object(root, api);
    oc_rep_text_set_text_string(api, version, "1.0.0"); // TODO will be 1.1.0
    oc_rep_text_set_text_string(api, base, "/");
    oc_rep_close_object(root, api);
    oc_rep_end_root_object();

    oc_prepare_cbor_response(request, OC_STATUS_OK);
  }
}

// cache reset value, original values may be void when callbacks are executed (due to clean up resources)
static int cached_erase_code_value;

static oc_event_callback_retval_t reset(void* context)
{
  PRINT("reset device: %d", cached_erase_code_value);

  /* Specification demands
     - reset a possible PRG mode
     - terminate a possible PASE token (removes all, even that only one should be present)
   
   Erase code

   2 (Factory Reset) :
      - individual address (ia)
      - host name (hname)
      - Installation ID (iid)
      - programming mode (pm)
      - device address (da)
      - sub address (sa)
      - group object table
      - recipient table
      - publisher table
      - PASE token (with below deletion)
      - all access tokens 
   
   7 (Factory Reset without IA):
      - group object table
      - recipient table
      - publisher table
      - PASE token (explicitly)
      - all access tokens that do not contain 'if.sec'
   
    @note Before the actual reset actions the factory preset callback handler is called ,
          after the actions the reset callback handler

  */

  // application factory preset callback handler
  const oc_factory_presets_t* my_preset_cb = oc_get_factory_presets_cb();
  if (my_preset_cb && my_preset_cb->cb)
  {
    PRINT("Factory PRESET callback handler is called");
    my_preset_cb->cb(my_preset_cb->data);
  }

  // delete data
  oc_knx_device_storage_reset(cached_erase_code_value);

  // application reset callback handler
  const oc_reset_t* my_reset_cb = oc_get_reset_cb();
  if (my_reset_cb && my_reset_cb->cb)
  {
    PRINT("Factory RESET callback handler is called");
    my_reset_cb->cb(cached_erase_code_value, my_reset_cb->data);
  }

  PRINT("Re-register mDNS with new data of ia, iid , pm mode (values are usually changed after a reset)");
  const oc_device_info_t* device = oc_core_get_device_info();
  knx_publish_service(oc_string(device->serialnumber), device->iid, device->ia, device->pm);

  return OC_EVENT_DONE;
}

static oc_event_callback_retval_t restart(void* context)
{
  PRINT("restart device");

  /* Specification demands
     - reset a possible PRG mode
     - terminate a possible PASE token (removes all, even that only one should be present)
     - apply (changed) configuration parameters latest after 30s

  */

  oc_device_info_t* device = oc_core_get_device_info();
  device->pm = false;

  // delete PASE token
  oc_core_find_and_remove_pase_token_in_at_table();

  // check and send on i-flags
  oc_init_datapoints_at_initialization();

  // application restart callback handler
  const oc_restart_t* my_restart = oc_get_restart_cb();
  if (my_restart && my_restart->cb)
  {
    my_restart->cb(my_restart->data);
  }

  return OC_EVENT_DONE;
}

/*

  JSON      CBOR
  value =   1       Unsigned    // erase codes for 'reset'
  cmd =     2       String      // 'restart', 'reset'
  status =  3       Unsigned
  code =    "code"  Unsigned
  time =    "time"  Unsigned

  CBOR payload example:
  { 2: "restart" }
  { 2: "reset", 1: <erase code> }
  <erase code>:
  - 2=m (delete all security parameters + network parameters)
  - 3=o (delete IA)
  - 7=m (delete all security parameters except with if.sec, don't delete network parameters)

*/
static void oc_core_knx_post_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  int erase_code_value = -1; // JSON key
  int cmd = -1; // JSON key

  // all values init to '0', 200 byte size is sufficient for request data
  char buffer[200] = {0};
  oc_rep_to_json(request->request_payload, buffer, 200, false);

  PRINT("oc_core_knx_post_handler with data %s", buffer);

  oc_rep_t* rep = request->request_payload;
  while (rep)
  {
    switch (rep->type)
    { // note, type does not reflect a 1:1 meaning of the CBOR major types

    case OC_REP_STRING:
    {
      // command (2)
      if (rep->iname == 2) 
      {
        cmd = convert_cmd(oc_string(rep->value.string));
      }
    }
    break;
    case OC_REP_INT:
    {
      // value (1)
      if (rep->iname == 1)
      {
        erase_code_value = (int)rep->value.integer;
      }
    }
    break;
    default:
      break;
    }
    rep = rep->next;
  }

  PRINT("cmd: %d value: %d", cmd, erase_code_value);

  if (cmd == RESTART_DEVICE)
  {
    // safe '-1' 'erase code' value (restart don't use a value)
    cached_erase_code_value = erase_code_value;

    // restart callback 
    oc_set_delayed_callback_ms(NULL, restart, 100);

    // send NO response
    PRINT("oc_core_knx_post_handler - end, restart");
    return;
  }
  if (cmd == RESET_DEVICE)
  {
    // safe 'erase code' value (reset uses a value)
    cached_erase_code_value = erase_code_value;

    // init reset callback with 2 seconds  
    oc_set_delayed_callback_ms(NULL, reset, 2000);

    // Before executing the reset function, the KNX IoT device MUST return a
    // response with CoAP response code 2.04 CHANGED and with payload containing
    // Error Code and Process Time in seconds as defined for the Response
    // to a Master Reset Request for KNX Classic devices, see [09].

    // check erase code value for response error (0:no error, 2:unsupported erase code, others not used here)
    const unsigned int response_code =
      erase_code_value == RESET_TO_DEFAULT_STATE || 
      erase_code_value == RESET_TO_DEFAULT_WO_IA ? RESET_NO_ERROR : RESET_UNSUPPORTED_ERASE_CODE;

    // response time (fixed value, need to be set in relation of the used hardware)
    const unsigned int response_time = 2;

    oc_rep_begin_root_object();
    oc_rep_text_set_int(root, code, response_code);
    oc_rep_text_set_int(root, time, response_time);
    oc_rep_end_root_object();

    // send response
    oc_prepare_cbor_response(request, OC_STATUS_CHANGED);
    PRINT("oc_core_knx_post_handler - end, reset");
    return;
  }

  PRINT("invalid command");
  oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
}

// resource definition, details/comments see on
// 'core_resource_well_known_core_final'
extern const oc_resource_t core_resource_knx_fp_g;
PRAGMA_IN oc_resource_data_t core_resource_knx_data;
const oc_resource_t core_resource_knx = {(oc_resource_t*)&core_resource_knx_fp_g,
                                         0,
                                         {NULL, sizeof("/.well-known/knx"), "/.well-known/knx"},
                                         {NULL, 0, NULL},
                                         {NULL, 0, NULL},
                                         {APPLICATION_LINK_FORMAT, CONTENT_NONE},
                                         OC_DISCOVERABLE,
                                         {oc_core_knx_get_handler, NULL, OC_ACL_NONE, OC_IF_NONE},
                                         {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                         {oc_core_knx_post_handler, NULL, OC_ACL_C | OC_ACL_SEC, OC_IF_C | OC_IF_SEC},
                                         {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                         {NULL, NULL},
                                         {NULL, NULL},
                                         0,
                                         0,
                                         true,
                                         &core_resource_knx_data};
PRAGMA_OUT

static void oc_create_knx_resource(int resource_idx)
{
  OC_DBG("create /knx resources");
  oc_core_populate_resource(resource_idx, "/.well-known/knx", APPLICATION_LINK_FORMAT, CONTENT_NONE, OC_DISCOVERABLE,
                            oc_core_knx_get_handler, 0, oc_core_knx_post_handler, 0, 0);
}


oc_lsm_state_t oc_knx_get_lsm()
{
  oc_device_info_t* device = oc_core_get_device_info();
  if (device == NULL)
  {
    OC_ERR("device not found");
    return LSM_S_UNLOADED;
  }

  return device->lsm_s;
}

int oc_knx_set_and_store_lsm(oc_lsm_state_t new_state)
{
  oc_device_info_t* device = oc_core_get_device_info();
  if (device == NULL)
  {
    OC_ERR("device not found");
    return -1;
  }

  // set state for device (RAM) and file storage (tests on LSM uses device property) 
  device->lsm_s = new_state;
  oc_storage_write(KNX_STORAGE_LSM, (uint8_t*)&new_state, sizeof(new_state));

  return 0;
}

const char* oc_core_get_lsm_state_as_string(oc_lsm_state_t lsm)
{
  // states
  if (lsm == LSM_S_UNLOADED)
  {
    return "unloaded";
  }
  if (lsm == LSM_S_LOADED)
  {
    return "loaded";
  }
  if (lsm == LSM_S_LOADING)
  {
    return "loading";
  }
  if (lsm == LSM_S_UNLOADING)
  {
    return "unloading";
  }
  if (lsm == LSM_S_LOADCOMPLETING)
  {
    return "load completing";
  }

  return "";
}

const char* oc_core_get_lsm_event_as_string(oc_lsm_event_t lsm)
{
  // commands
  if (lsm == LSM_E_NOP)
  {
    return "nop";
  }
  if (lsm == LSM_E_STARTLOADING)
  {
    return "startLoading";
  }
  if (lsm == LSM_E_LOADCOMPLETE)
  {
    return "loadComplete";
  }
  if (lsm == LSM_E_UNLOAD)
  {
    return "unload";
  }

  return "";
}


// LSM handler, stores the new state and returns if event was OK (true)
static bool oc_lsm_event_to_state(oc_lsm_event_t lsm_e)
{
  if (lsm_e == LSM_E_NOP)
  {
    // do nothing
    return true;
  }
  if (lsm_e == LSM_E_STARTLOADING)
  {
    oc_knx_set_and_store_lsm(LSM_S_LOADING);
    return true;
  }
  if (lsm_e == LSM_E_LOADCOMPLETE)
  {
    oc_knx_set_and_store_lsm(LSM_S_LOADED);
    return true;
  }
  if (lsm_e == LSM_E_UNLOAD)
  {
    // LSM (first to prevent any runtime messaging in/out)
    oc_knx_set_and_store_lsm(LSM_S_UNLOADED);

    // do a reset like erase code 2 but not the AT table, ia, iid, fid -> EITT test
    oc_delete_group_tables();
    oc_delete_group_object_table();

    return true;
  }
  return false;
}

static void oc_core_a_lsm_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  PRINT("oc_core_a_lsm_get_handler - start");

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  // get from the request the addressed device as index

  oc_device_info_t* device = oc_core_get_device_info();

  if (device == NULL)
  {
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  oc_lsm_state_t lsm = oc_knx_get_lsm();

  oc_rep_begin_root_object();
  oc_rep_i_set_int(root, 3, lsm);
  oc_rep_end_root_object();

  oc_prepare_cbor_response(request, OC_STATUS_OK);

  PRINT("oc_core_a_lsm_get_handler - end");
}

static void oc_core_a_lsm_post_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  PRINT("oc_core_lsm_post_handler - start");

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  // get from the request the addressed device as index

  oc_device_info_t* device = oc_core_get_device_info();

  if (device == NULL)
  {
    PRINT("oc_core_lsm_post_handler - end");
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  // default setting if nothing will be found
  int event = LSM_E_NOP;

  const oc_rep_t*  rep = request->request_payload;
  while (rep)
  {
    if (rep->type == OC_REP_INT)
    {
      // cmd = CBOR KEY 2, status = int
      if (rep->iname == 2)
      {
        event = (int)rep->value.integer;
        break;
      }
    }
    rep = rep->next;
  }

  PRINT("load event %d [%s]", event, oc_core_get_lsm_event_as_string(event));

  // LSM state changed correctly ?
  if (oc_lsm_event_to_state(event))
  {
    const oc_loadstate_t* my_cb = oc_get_lsm_change_cb();

    if (my_cb && my_cb->cb)
    { // application callback handler for LSM present ...
      my_cb->cb(oc_knx_get_lsm(), my_cb->data);
    }

    // if LSM is loaded , e.g; application is running ...
    if (oc_is_device_in_runtime())
    {
      oc_register_group_multicasts();
      oc_init_datapoints_at_initialization();
      knx_publish_service(oc_string(device->serialnumber), device->iid, device->ia, device->pm);
    }

    // create response
    oc_rep_new(request->response->response_buffer->buffer, (int)request->response->response_buffer->buffer_size);
    oc_rep_begin_root_object();
    oc_rep_i_set_int(root, 3, oc_knx_get_lsm());
    oc_rep_end_root_object();

    // note that also on event 'NOP' a 'changed' is returned
    oc_prepare_cbor_response(request, OC_STATUS_CHANGED);
    return;
  }
  // invalid event
  oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
}

// resource definition, details/comments see on 'core_resource_well_known_core_final'
extern const oc_resource_t core_resource_knx_spake;
PRAGMA_IN oc_resource_data_t core_resource_a_lsm_data;
const oc_resource_t core_resource_a_lsm = {(oc_resource_t*)&core_resource_knx_spake,
                                           0,
                                           {NULL, sizeof("/a/lsm"), "/a/lsm"},
                                           {NULL, 0, NULL},
                                           {NULL, 0, NULL},
                                           {APPLICATION_CBOR, CONTENT_NONE},
                                           OC_DISCOVERABLE,
                                           {oc_core_a_lsm_get_handler, NULL, OC_ACL_C, OC_IF_C},
                                           {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                           {oc_core_a_lsm_post_handler, NULL, OC_ACL_C, OC_IF_C},
                                           {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                           {NULL, NULL},
                                           {NULL, NULL},
                                           0,
                                           0,
                                           true,
                                           &core_resource_a_lsm_data};
PRAGMA_OUT

static void oc_create_a_lsm_resource(int resource_idx)
{
  OC_DBG("create /a/lsm resources");

  oc_core_populate_resource(resource_idx, "/a/lsm", APPLICATION_CBOR, CONTENT_NONE, OC_DISCOVERABLE,
                            oc_core_a_lsm_get_handler, 0, oc_core_a_lsm_post_handler, 0, 0);
}

static void oc_core_knx_k_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  PRINT("oc_core_knx_k_get_handler");

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  oc_device_info_t* device = oc_core_get_device_info();
  if (device == NULL)
  {
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  // TODO OBSERVE is not implemented for (1) 'lt' and 'non' metadata (2.5.9.3/4) and (2) SECOND get request -> response payload (2.5.9.1)

  // only ia of device, no payload for first GET request
  oc_rep_begin_root_object();
  oc_rep_i_set_int(root, 4, device->ia);
  oc_rep_end_root_object();

  oc_prepare_cbor_response(request, OC_STATUS_OK);

  PRINT("oc_core_knx_k_get_handler - done");
}


static void oc_core_knx_k_post_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)iface_mask;
  (void)data;
  char ip_address[100];

  // define summary callback handler status as specified for no error
  oc_status_t summary_handler_status = OC_STATUS_CHANGED;

  // { sia: 5678, s: {st: write, ga: 1, value: 100 }}
  // -> value can be anything incl. a string
  // -> define and clear a temporary object notification, can be:
  // - sia + w/a + ga + value  : write/update ga with value (see example above)
  // - sia                     : sync message
  // - sia + r + + ga          : read ga
  oc_group_object_notification_t received_notification = {0};

  // pointer to possible value object, may be not present (GET = OK),
  // PUT (= not OK), it is handed then over as default NULL to the AL callback handlers
  oc_rep_t* received_notification_value_object = NULL;

  PRINT("oc_core_knx_k_post_handler - start");

  // debugging
  PRINT("decoded payload size : %d", (int)request->_payload_len);
  oc_print_rep_as_json(request->request_payload, true);

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  oc_device_info_t* device = oc_core_get_device_info();
  if (device == NULL)
  {
    oc_prepare_no_format_response_no_payload(request, OC_IGNORE);
    return;
  }

  // scan received payload for sia/st/ga/value
  oc_rep_t* rep = request->request_payload;

  while (rep)
  {
    switch (rep->type)
    {
      case OC_REP_INT:
      {
        // sia (4), mandatory
        if (rep->iname == 4)
        {
          received_notification.sia = (uint32_t)rep->value.integer;
        }
        break;
      }
      case OC_REP_OBJECT:
      {
        // two objects are defined:
        // - (5) s-map object with defined types for st/ga
        // - (1) value object with several types for the value (bool, string, ...), subject to application handlers
        oc_rep_t* object = rep->value.object;

        while (object)
        {

          // value (1), object type don't care here, optional
          if (object->iname == 1)
          {
            // picks the object that contains the value
            received_notification_value_object = object;
          }

          switch (object->type)
          {
            case OC_REP_STRING:
            {
              // st (6), optional
              if (object->iname == 6)
              {
                // frees any already assigned 'st' (would be an error in request payload)
                oc_free_string(&received_notification.st);
                oc_new_string(&received_notification.st, oc_string(object->value.string), oc_string_len(object->value.string));
              }
              break;
            }
            case OC_REP_INT:
            {
              // ga (7), optional
              if (object->iname == 7)
              {
                received_notification.ga = (uint32_t)object->value.integer;
              }
              break;
            }
           
            default:
              break;
          }
          object = object->next;
        }
        break;
      }
     
      default:
        break;
    }
    rep = rep->next;
  }

  if (oc_is_device_in_runtime() == false)
  {
    PRINT("device not in runtime state:%d - ignore message", device->lsm_s);
    oc_prepare_no_format_response_no_payload(request, OC_IGNORE);
    return;
  }

  // debugging ...
  SNPRINTFipaddr(ip_address, 100 - 1, *request->origin);
  // handle the request loop over the group addresses of the /fp/r (recipient table)
  PRINT("/k : sia: %u ga: %u st: %s origin: %s", 
        received_notification.sia, 
        received_notification.ga,
        oc_string_checked(received_notification.st), 
        ip_address);

  // set default request-flags, only one out of a/w/r is possible
  oc_cflag_mask_t service_type_from_request = OC_CFLAG_NONE;

  if (strcmp(oc_string_checked(received_notification.st), "w") == 0)
  {
    // write, any ga => cflags = w -> overwrite object value
    service_type_from_request = OC_CFLAG_WRITE;
  }
  else if (strcmp(oc_string_checked(received_notification.st), "a") == 0)
  {
    // update, any ga => cflags = w -> overwrite object value
    service_type_from_request = OC_CFLAG_UPDATE;
  }
  else if (strcmp(oc_string_checked(received_notification.st), "r") == 0)
  {
    /// read, any ga => cflags = r -> read object value (group speaker principle, one 'r' flag should be set ...)
    service_type_from_request = OC_CFLAG_READ;
  }

  // GO array INDEX with the GA included (out of 0...max GO table entries)
  // - process all GOs in the table with this GA included,
  // - position of GA in GA array don't care 
  int go_table_index_where_ga_is_used = oc_core_find_first_go_table_index_with_ga(received_notification.ga);

  PRINT("/k : index %d", go_table_index_where_ga_is_used);

  if (go_table_index_where_ga_is_used != -1)
  { // index found

    // each application callback handler gets a new copy of the original request + new response buffer
    oc_request_t new_request; // filled completely later on
    oc_response_buffer_t response_buffer = {0};
    oc_response_t response_obj; // filled completely later on

    /*
      Internal Callback Handler, Examples and Handling

      Group object table LSAB
      -----------------------

      GO0 [ id: 0, href: "/p/lsab/0/soo", cflags: w  , ga_len: 10, GA: [1..10] ] // actuator input
      GO1 [ id: 8, href: "/p/lsab/0/ioo", cflags: r+t, ga_len: 1,  GA: [11,12] ] // actuator status
      GO2 [ id: 2, href: "/p/lsab/0/ext", cflags: w,   ga_len: 2,  GA: [15,11] ] // updates value ALSO on GA 11
      GO3 [ id: 3, href: "/p/lsab/0/cut", cflags: w,   ga_len: 20, GA: [16..36]] // a split entry with 20 GAs (16 = sending GA)
      GO4 [ id: 4, href: "/p/lsab/0/cut", cflags: w,   ga_len: 2,  GA: [37,38] ] // a split entry with the remainder GAs (all receiving GAs)
      GO5 [ id: 5, href: "/p/lsab/0/a00", cflags: w,   ga_len: 2,  GA: [39]    ] // add on, to update value on GA 39 
      GO6 [ id: 6, href: "/p/lsab/0/a01", cflags: w,   ga_len: 2,  GA: [39]    ] // add on, to update value on GA 39 
      GO7 [ id: 7, href: "/p/lsab/0/as1", cflags: w,   ga_len: 2,  GA: [11,40] ] // actuator status 1, same ga and no r-flag
      GO8 [ id: 1, href: "/p/lsab/0/as2", cflags: r+w, ga_len: 2,  GA: [41,11] ] // actuator status 2, same ga and r-flag

      Application Resources
      ---------------------

      AR0 : /p/lsab/0/soo, GET, PUT, if.i
      AR1 : /p/lsab/0/ioo, GET,      if.o, true   // PUT is optional for outputs
      AR2 :              , GET, PUT, if.i         // product problem , no resource path defined
      AR3 : /p/lsab/0/a01, GET, PUT, if.i
      AR4 : /p/lsab/0/ext, GET, PUT, if.i
      AR5 : /p/lsab/0/as1, GET,      if.o         // PUT is optional for outputs
      AR6 : /p/lsab/0/as2, GET,      if.o, false  // PUT is optional for outputs

      (1) Write/Update 
      ----------------

        Updates all GOs in table with the GA included
        - w-cflag must be enabled,
        - affects also GOs where the GA is in position zero, sending GA
          (bidirectional w/r- cflags settings on a resource are no common use in KNX s-mode)

        { sia: 5678, s: {st: write, ga: 1, value: 100 }}

        1. scan for first GO index where GA = 1 is used = GO0
           - scan application resources for href = AR0
           - check cflags GO0 (write = enabled)
           - call PUT handler for this application resource = AR0
        2. scan for next GO where GA = 1 is used = NONE
        3. DONE

        --> All GOs are updated

      (2) Read
      --------

        Sends for all GOs in table where the GA is included a (mc/uc) read response
        - r-cflag must be enabled, t-cflag will be ignored (considered only on self triggered requests)
        - use the sending GA (position 0) on lowest 'id' per 'href'
        - 3/7/2 AL does not mandate to send only one response, if needed MaC user needs to remove the r-flag

        { sia: 5678, s: {st: read, ga: 11 }}

        1. find first GO table index where GA = 11 is used = GO1
           - check application resources for href = AR1
           - check cflags GO1 (read = enabled)
           - call GET handler for application resource = AR1
           - issue read response with sending GA from GO1 = 11
        2. find next GO table index where GA = 11 is used = GO2
           - check application resources for href = AR4
           - check cflags GO2 (read = disabled)
           - break
        3. scan for next GO where GA = 11 is used = GO7
           - scan application resources for href = AR5
           - check cflags GO7 (read = disabled)
           - break
        4. scan for next GO where GA = 11 is used = GO8
           - check application resources for href = AR6
           - check cflags GO8 (read = enabled)
           - call GET handler for application resource = AR6
           - issue read response with sending GA from GO8 = 41
        5. DONE

        --> NO GOs are internally updated
           (the read response does not update internally all GOs with u-flag)

        DETAIL
        ------
        How does the read behave?
        (don't care if this MaC configuration above may be 'not correct or useless')

        Option 1:
        -  {ga: 41, value: false, st:a}
        -  search for GO with lowest id where GA is linked and send on the sending GA = GO8
        -> not used

        Option 2:
        -  {ga: 11, value: false, st:a}
        -  search for GO with lowest id where GA is linked and send on same GA = GO8
        -> not used

        Option 3:
        -  {ga: 11, value: true, st:a}
        -  search for GOs where GA is a sending GA = GO1
        -> not used

        Option 4:
        -  {ga: 41, value: false, st:a}, {ga: 11, value: true, st:a}
        -  search for GOs where GA is linked and send on sending GA = GO8 + GO1
        -> used

        Option 5:
        -  {ga: 11, value: false, st:a}, {ga: 11, value: true, st:a}
        -  search for GOs where GA is linked and send on same GA = GO8 + GO1
        -> not used

    */
    while (go_table_index_where_ga_is_used != -1)
    {
      // get href from the GO table index
      oc_string_t go_href = oc_core_get_href_from_group_object_table_index(go_table_index_where_ga_is_used);

      PRINT("/k : resource path %s", oc_string_checked(go_href));

      // device EP present (sanity check, GO without href is usually a product problem or MAC configuration error)
      if (oc_string_len(go_href) > 0)
      {
        // get the application resource with the HREF from the GO, to perform on the forward call
        const oc_resource_t* application_resource_with_href_match =
          oc_ri_get_app_resource_by_resource_path(oc_string(go_href), oc_string_len(go_href));

        if (!application_resource_with_href_match)
        {
          /*
             - group object table and application resource definition see above
             - POST /k with write on GA 39
             - if the first GO href entry does not have a matching application resource
               option 1 :
               1. first GO is GO5 -> no application resource
               2. stop and return
               option 2 (used):
               1. first GO is GO5 -> no application resource
               2. search GO table for next href with GA included -> GO6 -> AR3
               3. update AR3
               4. return

           */

          // get NEXT GO array index (NOT GO table id) with the GA included (out of last...max GO table entries)
          go_table_index_where_ga_is_used =
            oc_core_find_next_go_table_index_with_ga(received_notification.ga, go_table_index_where_ga_is_used);
          continue;
        }

        /*
          here we have
          - a GO with the GA included
          - a GO with a resource path (href) and an application resource with the SAME resource path (href)
        */

        // get GO c-flags
        const oc_cflag_mask_t cflags = oc_core_get_cflags_from_group_object_table_index(go_table_index_where_ga_is_used);

        // if corresponding c-flag and the (only one possible) original service type from request 'a/w/r' are set,
        // write request and w-flag set = write possible, use copy hence cflags may be different for each GO (same for other options)
        oc_cflag_mask_t service = service_type_from_request & cflags;

        if (service & OC_CFLAG_WRITE && application_resource_with_href_match->put_handler.cb)
        {
          PRINT("/k : write with GOT index %d handled due to write flag enabled %d", go_table_index_where_ga_is_used, cflags);

          /*
            here we have a GO with a resource path (href) and application resource with the SAME resource path (href)
            and an application resource PUT handler with a write flag enabled, call application PUT handler
            - for /k only a POST is defined, application handler needs to end up in one (PUT) handler for /k and /p
          */

          // copy all data from request to new request (performance consuming)
          // - do that for every GO, the last callback from previous GO may have manipulated the data
          // - includes also the originally called resource, maybe used in callback handler to access interfaces or acl scopes
          oc_ri_new_request_from_request(&new_request, request, &response_buffer, &response_obj);

          // sets the payload pointer to the 'value' OBJECT --> MUST BE IN (otherwise NULL is assigned)
          // used by /p and /k that calls the same application callback handlers
          new_request.request_payload = received_notification_value_object;

          // set src to /k (POST /k with payload) ; for a redirect check in application callback handles
          new_request.uri_path = "/k";
          new_request.uri_path_len = 2; // exclude for uri path len string null termination

          // use new request (not the received one with POST), user data are possible
          // call application handler with own interface/ user data
          // (it makes no sense to call it with the original /k interface mask, this is a fix value)
          application_resource_with_href_match->put_handler.cb(
            &new_request, application_resource_with_href_match->put_handler.interface_mask,
            application_resource_with_href_match->put_handler.user_data);

          // collect the max 'bad' status code, usually overwritten by the callback
          collect_and_rank_status(new_request.response->response_buffer->code, &summary_handler_status);
        }
        if (service & OC_CFLAG_UPDATE && application_resource_with_href_match->put_handler.cb)
        {
          PRINT("/k : response with GOT index %d handled due to update on response flag enabled %d", go_table_index_where_ga_is_used, cflags);

          /*
            here we have a GO with a resource path (href) and application resource with the SAME resource path (href)
            and an application resource PUT handler with a write flag enabled, call application PUT handler
            - for /k only a POST is defined, application handler needs to end up in one (PUT) handler for /k and /p
          */

          // copy all data from request to new request (performance consuming)
          // - do that for every GO, the last callback from previous GO may have manipulated the data
          // - includes also the originally called resource, maybe used in callback handler to access interfaces or acl scopes
          oc_ri_new_request_from_request(&new_request, request, &response_buffer, &response_obj);

          // sets the payload pointer to the 'value' OBJECT --> MUST BE IN (otherwise NULL is assigned)
          // used by /p and /k that calls the same application callback handlers
          new_request.request_payload = received_notification_value_object;

          // set src to /k (POST /k with payload) ; for a redirect check in application callback handles
          new_request.uri_path = "/k";
          new_request.uri_path_len = 2; // exclude for uri path len string null termination

          // use new request (not the received one with POST), user data are possible
          // call application handler with own interface/ user data
          // (it makes no sense to call it with the original /k interface mask, this is a fix value)
          application_resource_with_href_match->put_handler.cb(
            &new_request, application_resource_with_href_match->put_handler.interface_mask,
            application_resource_with_href_match->put_handler.user_data);

          // collect the max 'bad' status code, usually overwritten by the callback
          collect_and_rank_status(new_request.response->response_buffer->code, &summary_handler_status);
        }
        if (service & OC_CFLAG_READ && application_resource_with_href_match->get_handler.cb)
        {
          PRINT("/k : read with GOT index %d handled due to read flags enabled %d", go_table_index_where_ga_is_used, cflags);

          /*
          here we have
          - a GO with the GA included
          - a GO with a resource path (href) and an application resource with the SAME resource path (href)
          - an application resource GET handler with a read flag enabled

          for /k only a POST is defined, application handler needs to end up in one (GET) handler for /k and /p
          */

          // copy all data from request to new request (performance consuming)
          // - do that for every GO, the last callback from previous GO may have manipulated the data
          // - includes also the originally called resource, maybe used in callback handler to access interfaces or acl scopes
          oc_ri_new_request_from_request(&new_request, request, &response_buffer, &response_obj);

          // set src to /k (POST /k with payload) ; for a redirect check in application callback handles
          new_request.uri_path = "/k";
          new_request.uri_path_len = 2; // exclude for uri path len string null termination

          // use new request (not the received one with POST), user data are possible
          // call application handler with own interface/ user data
          // (it makes no sense to call it with the original /k interface mask, this is a fix value)
          application_resource_with_href_match->get_handler.cb(&new_request, 
                                                               application_resource_with_href_match->get_handler.interface_mask,
                                                               application_resource_with_href_match->get_handler.user_data);

          // #2 - send read response
          {
            // Option 4, get sending GA for the current resource path href (out of 0...max GO table entries)
            const int sending_ga = oc_core_find_sending_ga_in_pos_zero_for_href(oc_string(go_href), NULL);

            if (sending_ga != -1)
            { // we have a sending GA, now we can send the read response, rest was checked before

              // grpid
              uint32_t grpid = oc_find_grpid_in_recipient_table(sending_ga);
              if (grpid > 0)
              { // grpid is set in case of multicast in RCP table (configured by MaC)

              #ifdef OC_USE_MULTICAST_SCOPE_2
                oc_issue_s_mode_mc(2, device->ia, grpid, sending_ga, device->iid, "a",
                                new_request.response->response_buffer->buffer,
                                (int)new_request.response->response_buffer->response_length);

              #endif
                oc_issue_s_mode_mc(5, device->ia, grpid, sending_ga, device->iid, "a",
                                new_request.response->response_buffer->buffer,
                                (int)new_request.response->response_buffer->response_length);
              }
              else
              {
                // TODO resolve IP unicast to send via unicast...
                // discover unicast IPv6 for IA via mDNS
                // send message with unicast IPv6
                PRINT("grpid =0");
              }
            }

            // collect the max 'bad' status code, usually overwritten by the callback
            collect_and_rank_status(new_request.response->response_buffer->code, &summary_handler_status);
          }
        }
      }

      // get NEXT GO array index (NOT GO table id) with the GA included (out of last...max GO table entries)
      go_table_index_where_ga_is_used = oc_core_find_next_go_table_index_with_ga(received_notification.ga, go_table_index_where_ga_is_used);
    }
  }
  else
  { // index not found
    
    // - ga is not part of GO table
    // - sia ONLY (see above)
    summary_handler_status = OC_STATUS_NOT_FOUND;
  }

  if (request->origin && request->origin->flags & MULTICAST)
  { // multicast request: don't send anything ELSE back
    // if configured in PUB table a read was answered with multicast beforehand

    PRINT("multicast - not sending response");
    oc_prepare_no_format_response_no_payload(request, OC_IGNORE);
  }
  else
  { // unicast request: send status back
    // if configured in PUB table a read was answered with multicast (GRIP ID > 0) and/or unicast beforehand

    PRINT("unicast - sending response");
    oc_prepare_no_format_response_no_payload(request, summary_handler_status);
  }

  PRINT("oc_core_knx_k_post_handler - end");
}

// resource definition, details/comments see on
// 'core_resource_well_known_core_final'
extern const oc_resource_t core_resource_knx_fingerprint;
PRAGMA_IN oc_resource_data_t core_resource_knx_k_data;
const oc_resource_t core_resource_knx_k = {(oc_resource_t*)&core_resource_knx_fingerprint,
                                           0,
                                           {NULL, sizeof("/k"), "/k"},
                                           {NULL, (size_t)1 * 32, ((char[1][32]){"urn:knx:g.s"})},
                                           {NULL, 0, NULL},
                                           {APPLICATION_CBOR, CONTENT_NONE},
                                           OC_DISCOVERABLE,
                                           {oc_core_knx_k_get_handler, NULL, OC_ACL_G | OC_ACL_GA, OC_IF_G},
                                           {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                           {oc_core_knx_k_post_handler, NULL, OC_ACL_G | OC_ACL_GA, OC_IF_G},
                                           {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                           {NULL, NULL},
                                           {NULL, NULL},
                                           0,
                                           0,
                                           true,
                                           &core_resource_knx_k_data};


static void oc_create_knx_k_resource(int resource_idx)
{
  OC_DBG("create /k resources");
  oc_core_populate_resource(resource_idx, "/k", APPLICATION_CBOR, CONTENT_NONE, OC_DISCOVERABLE,
                            oc_core_knx_k_get_handler, 0, oc_core_knx_k_post_handler, 0, 1, "urn:knx:g.s");
}

static void oc_core_knx_fingerprint_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;
  PRINT("oc_core_knx_fingerprint_get_handler");

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  // check if the state is loaded

  if (oc_knx_get_lsm() != LSM_S_LOADED)
  {
    OC_ERR("not in loaded state");
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_SERVICE_UNAVAILABLE);
    return;
  }

  oc_rep_begin_root_object();
  oc_rep_i_set_int(root, 1, g_fingerprint);
  oc_rep_end_root_object();

  PRINT("oc_core_knx_fingerprint_get_handler - done");
  oc_prepare_cbor_response(request, OC_STATUS_OK);
}

// resource definition, details/comments see on
// 'core_resource_well_known_core_final'
extern const oc_resource_t core_resource_knx_ia;
PRAGMA_IN oc_resource_data_t core_resource_knx_fingerprint_data;
const oc_resource_t core_resource_knx_fingerprint = {(oc_resource_t*)&core_resource_knx_ia,
                                                     0,
                                                     {NULL, sizeof("/.well-known/knx/f"), "/.well-known/knx/f"},
                                                     {NULL, 0, NULL},
                                                     {NULL, 0, NULL},
                                                     {APPLICATION_CBOR, CONTENT_NONE},
                                                     OC_DISCOVERABLE,
                                                     {oc_core_knx_fingerprint_get_handler, NULL, OC_ACL_C, OC_IF_C},
                                                     {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                     {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                     {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                     {NULL, NULL},
                                                     {NULL, NULL},
                                                     0,
                                                     0,
                                                     true,
                                                     &core_resource_knx_fingerprint_data};
PRAGMA_OUT

static void oc_create_knx_fingerprint_resource(int resource_idx)
{
  OC_DBG("create /k/f resources");
  oc_core_populate_resource(resource_idx, "/.well-known/knx/f", APPLICATION_CBOR, CONTENT_NONE, OC_DISCOVERABLE,
                            oc_core_knx_fingerprint_get_handler, 0, 0, 0, 0);
}

// ----------------------------------------------------------------------------

static void oc_core_knx_ia_post_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  bool ia_set = false;
  bool iid_set = false;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  oc_rep_t* rep = request->request_payload;

  while (rep)
  {
    if (rep->type == OC_REP_INT)
    {
      if (rep->iname == 12)
      {
        PRINT("received 12 (ia) : %d", (int)rep->value.integer);
        oc_core_set_and_store_device_ia((uint16_t)rep->value.integer);
        ia_set = true;
      }
      else if (rep->iname == 25)
      {
        PRINT("received 25 (fid): %llu", (uint64_t)rep->value.integer);
        oc_core_set_and_store_device_fid(rep->value.integer);
      }
      else if (rep->iname == 26)
      {
        PRINT("received 26 (iid): %llu", (uint64_t)rep->value.integer);
        oc_core_set_and_store_device_iid(rep->value.integer);
        iid_set = true;
      }
    }
    rep = rep->next;
  }

  // iid/ia are mandatory
  if (iid_set && ia_set)
  {
    if (oc_is_device_in_runtime())
    {
      oc_register_group_multicasts();
      oc_init_datapoints_at_initialization();
      oc_device_info_t* device = oc_core_get_device_info();
      knx_publish_service(oc_string(device->serialnumber), device->iid, device->ia, device->pm);
    }
    oc_prepare_cbor_response(request, OC_STATUS_CHANGED);
  }
  else
  {
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
  }
}

// resource definition, details/comments see on
// 'core_resource_well_known_core_final'
PRAGMA_IN oc_resource_data_t core_resource_knx_ia_data;
const oc_resource_t core_resource_knx_ia = {(oc_resource_t*)&core_resource_knx,
                                            0,
                                            {NULL, sizeof("/.well-known/knx/ia"), "/.well-known/knx/ia"},
                                            {NULL, 0, NULL},
                                            {NULL, 0, NULL},
                                            {APPLICATION_CBOR, CONTENT_NONE},
                                            OC_DISCOVERABLE,
                                            {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                            {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                            {oc_core_knx_ia_post_handler, NULL, OC_ACL_C | OC_ACL_SEC, OC_IF_C | OC_IF_SEC},
                                            {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                            {NULL, NULL},
                                            {NULL, NULL},
                                            0,
                                            0,
                                            true,
                                            &core_resource_knx_ia_data};
PRAGMA_OUT

static void oc_create_knx_ia(int resource_idx)
{
  OC_DBG("create /knx/ia resources");
  oc_core_populate_resource(resource_idx, "/.well-known/knx/ia", APPLICATION_CBOR, CONTENT_NONE, OC_DISCOVERABLE,
                            NULL, 0, oc_core_knx_ia_post_handler, 0, 0);
}


static void oc_core_knx_ldevid_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;
  size_t response_length = 0;

  PRINT("oc_core_knx_ldevid_get_handler");

  if (!oc_accept_header_is_ok(request, APPLICATION_PKCS7_CMC_REQUEST))
  {
    return;
  }
  response_length = oc_string_len(g_ldevid);
  oc_rep_encode_raw((const uint8_t*)oc_string(g_ldevid), (size_t)response_length);

  request->response->response_buffer->content_format = APPLICATION_PKCS7_CMC_RESPONSE;
  request->response->response_buffer->code = oc_status_code(OC_STATUS_OK);
  request->response->response_buffer->response_length = response_length;

  PRINT("oc_core_knx_ldevid_get_handler- done");
}

// resource definition, details/comments see on
// 'core_resource_well_known_core_final'
PRAGMA_IN oc_resource_data_t core_resource_knx_ldevid_data;
const oc_resource_t core_resource_knx_ldevid = {(oc_resource_t*)&core_resource_knx_k,
                                                0,
                                                {NULL, sizeof("/.well-known/knx/ldevid"), "/.well-known/knx/ldevid"},
                                                {NULL, (size_t)1 * 32, ((char[1][32]){":dpt.a[n]"})},
                                                {NULL, 0, NULL},
                                                {APPLICATION_PKCS7_CMC_REQUEST, CONTENT_NONE},
                                                OC_DISCOVERABLE,
                                                {oc_core_knx_ldevid_get_handler, NULL, OC_ACL_D, OC_IF_D},
                                                {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                {NULL, NULL},
                                                {NULL, NULL},
                                                0,
                                                0,
                                                true,
                                                &core_resource_knx_ldevid_data};
PRAGMA_OUT

static void oc_create_knx_ldevid_resource(int resource_idx)
{
  OC_DBG("oc_create_knx_ldevid_resource");
  oc_core_populate_resource(resource_idx, "/.well-known/knx/ldevid", APPLICATION_PKCS7_CMC_REQUEST, CONTENT_NONE,
                            OC_DISCOVERABLE, oc_core_knx_ldevid_get_handler, 0, 0, 0, 1, ":dpt.a[n]");
}


static void oc_core_knx_idevid_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;
  size_t response_length = 0;

  PRINT("oc_core_knx_idevid_get_handler");

  if (!oc_accept_header_is_ok(request, APPLICATION_PKCS7_CMC_REQUEST))
  {
    return;
  }
  response_length = oc_string_len(g_idevid);
  oc_rep_encode_raw((const uint8_t*)oc_string(g_idevid), (size_t)response_length);

  request->response->response_buffer->content_format = APPLICATION_PKCS7_CMC_RESPONSE;
  request->response->response_buffer->code = oc_status_code(OC_STATUS_OK);
  request->response->response_buffer->response_length = response_length;

  PRINT("oc_core_knx_idevid_get_handler- done");
}

// resource definition, details/comments see on
// 'core_resource_well_known_core_final'
PRAGMA_IN oc_resource_data_t core_resource_knx_idevid_data;
const oc_resource_t core_resource_knx_idevid = {(oc_resource_t*)&core_resource_knx_ldevid,
                                                0,
                                                {NULL, sizeof("/.well-known/knx/idevid"), "/.well-known/knx/idevid"},
                                                {NULL, (size_t)1 * 32, ((char[1][32]){":dpt.a[n]"})},
                                                {NULL, 0, NULL},
                                                {APPLICATION_PKCS7_CMC_REQUEST, CONTENT_NONE},
                                                OC_DISCOVERABLE,
                                                {oc_core_knx_idevid_get_handler, NULL, OC_ACL_D, OC_IF_D},
                                                {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                                {NULL, NULL},
                                                {NULL, NULL},
                                                0,
                                                0,
                                                true,
                                                &core_resource_knx_idevid_data};
PRAGMA_OUT

void oc_create_knx_idevid_resource(int resource_idx)
{
  OC_DBG("oc_create_knx_idevid_resource");
  oc_core_populate_resource(resource_idx, "/.well-known/knx/idevid", APPLICATION_PKCS7_CMC_REQUEST, CONTENT_NONE,
                            OC_DISCOVERABLE, oc_core_knx_idevid_get_handler, 0, 0, 0, 1, ":dpt.a[n]");
}


#ifdef OC_SPAKE
static spake_data_t spake_data = {0};
static int failed_handshake_count = 0;

static bool is_blocking = false;

static oc_event_callback_retval_t decrement_counter(void* data)
{
  if (failed_handshake_count > 0)
  {
    --failed_handshake_count;
  }

  if (is_blocking && failed_handshake_count == 0)
  {
    is_blocking = false;
  }
  return OC_EVENT_CONTINUE;
}

static void increment_counter(void) { ++failed_handshake_count; }

// prevent from brute force handshake attempts
static bool is_handshake_blocked(void)
{
  if (is_blocking)
  {
    return true;
  }

  // after 10 failed attempts per minute, block the client for the
  // next minute
  if (failed_handshake_count > 10)
  {
    is_blocking = true;
    return true;
  }

  return false;
}

#endif

static oc_separate_response_t spake_separate_rsp;
static oc_event_callback_retval_t oc_core_knx_spake_separate_post_handler(void* req_p);

/*
  - handles the MaC PASE requests from perspective of (server) device 
  - no PASE workflow to issue a server based PASE enrolment is implemented 
 */
static void oc_core_knx_spake_post_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  PRINT("oc_core_knx_spake_post_handler - start");

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  /* SPAKE2+ is only allowed if a  device is in the "default cfg" state
   - use unloaded LSM -> security problem

     If MaC resets the device (LSM = unloaded) and waits n seconds (as the device said ...)
     an attacker can set an own PASE token to read out all data the MaC will write
     later on (including that an attacker can do a reconfiguration)

   - use empty AT table as criteria

     On a reset code 7, PASE token is removed, all tokens are removed expect entries with 'if.sec' scope
     -> this results in a nonempty AT table, which is NOT the default cfg state (see security leak above)

     On a reset code 2, all tokens are removed (including PASE token)
     -> this results FOR SURE in an empty AT table, which is the default cfg state

     On a restart, PASE token is removed, all other token remains
     -> this results in a nonempty AT table which is NOT the default cfg state (see security leak above)
 
  */

  // check if the AT table is empty (see above)
  if (oc_core_items_used_in_auth_at_table() > 0)
  {
    OC_ERR("device is not in the 'default configuration state'");
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
    return;
  }

#ifdef OC_SPAKE
  if (is_handshake_blocked())
  {
    request->response->response_buffer->code = oc_status_code(OC_STATUS_SERVICE_UNAVAILABLE);
    request->response->response_buffer->max_age = failed_handshake_count * 10;
    return;
  }
#endif

  // set ptr
  oc_rep_t* rep = request->request_payload;

  pase_step = 0;
  uint8_t members_step_1 = 0;
  uint8_t members_step_2 = 0;
  uint8_t members_step_3 = 0;

  // check input to classify step 1..3 (no state machine is implemented)
  // - step 1: 2 - id + rnd 
  // - step 2: 1 - shareP   
  // - step 3: 1 - confirmP 
  // no check if there are multiple byte strings in the request payload (first wins)
  while (rep)
  {
    switch (rep->type)
    {
      // check identifiers for byte strings (salt, ...))
      case OC_REP_BYTE_STRING:
      {
        if (rep->iname == SPAKE_PA_SHARE_P)
        {
          // pase credential request (step 2)
          pase_step = SPAKE_PA_SHARE_P;
          members_step_2++;
        }
        else if (rep->iname == SPAKE_CA_CONFIRM_P)
        {
          // pase credential verification request (step 3) 
          pase_step = SPAKE_CA_CONFIRM_P;
          members_step_3++;
        }
        else if (rep->iname == SPAKE_RND)
        {
          // pase parameter request (step 1) 
          pase_step = SPAKE_RND;
          members_step_1++;
        }
      }
    break;
      // check identifiers for text strings
      case OC_REP_STRING:
      {
        if (rep->iname == SPAKE_ID)
        {
          // pase parameter request (step 1) 
          pase_step = SPAKE_RND;
          members_step_1++;
        }
      }
      break;
    default:
      break;
    }
    rep = rep->next;
  }

  bool s1 = members_step_1 == 2 && pase_step == SPAKE_RND;
  bool s2 = members_step_2 == 1 && pase_step == SPAKE_PA_SHARE_P;
  bool s3 = members_step_3 == 1 && pase_step == SPAKE_CA_CONFIRM_P;

  // check if one out of step 1..3 is part of request and contains valid data
  if (!s1 && !s2 && !s3)
  {
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  // reset ptr
  rep = request->request_payload;

  // handle input
  while (rep)
  {
    switch (rep->type)
    {
      case OC_REP_BYTE_STRING:
      {
        if (rep->iname == SPAKE_CA_CONFIRM_P)
        {
          // real string size (excluding '\') from request must match 
          if (oc_string_len(rep->value.string) != sizeof(g_pase.confirmP))
          {
            oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
            return;
          }
          memcpy(g_pase.confirmP, oc_cast(rep->value.string, uint8_t), sizeof(g_pase.confirmP));
        }
        if (rep->iname == SPAKE_PA_SHARE_P)
        {
          // real string size (excluding '\') from request must match   
          if (oc_string_len(rep->value.string) != sizeof(g_pase.shareP))
          {
            oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
            return;
          }
          memcpy(g_pase.shareP, oc_cast(rep->value.string, uint8_t), sizeof(g_pase.shareP));
        }
        if (rep->iname == SPAKE_RND)
        {
          // real string size (excluding '\') from request must match 
          if (oc_string_len(rep->value.string) != sizeof(g_pase.rnd))
          {
            oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
            return;
          }
          memcpy(g_pase.rnd, oc_cast(rep->value.string, uint8_t), sizeof(g_pase.rnd));
        }
        
      }
      break;
      case OC_REP_STRING:
        {
          if (rep->iname == SPAKE_ID)
          {
            // no empty string id is allowed and no string id larger than 7
            if (oc_string_len(rep->value.string) == 0 || oc_string_len(rep->value.string) > 7)
            {
              oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
              return;
            }

            // free possible old spake token id
            oc_free_string(&g_pase.id);
            oc_new_byte_string(&g_pase.id, oc_string(rep->value.string), oc_string_len(rep->value.string));
            PRINT("==> CLIENT RECEIVES %d", (int)oc_byte_string_len(rep->value.string));
          }
        }
      break;
    default:
      break;
    }
    rep = rep->next;
  }

  PRINT("pase_step: %d", pase_step);

  oc_indicate_separate_response(request, &spake_separate_rsp);
  oc_set_delayed_callback(NULL, &oc_core_knx_spake_separate_post_handler, 0);
}

// handles the device PASE requests (responses to a MaC PASE request)
static oc_event_callback_retval_t oc_core_knx_spake_separate_post_handler(void* req_p)
{
  (void)req_p;
  PRINT("oc_core_knx_spake_separate_post_handler - start");

  // previous device response is fired and no longer active ...
  if (!spake_separate_rsp.active)
  {
    return OC_EVENT_DONE;
  }

  // assign
  oc_set_separate_response_buffer(&spake_separate_rsp);

  // step 1
  if (pase_step == SPAKE_RND)
  {
    // return 2.04 changed, frame rnd, salt, it , ...

    #ifdef OC_SPAKE

    /*
      PASE parameter exchange (step 1)

      - get random numbers for rnd and salt when starting a new PASE session
      - set fixed compile time value for number of iterations (IMPORTANT consider the notes on oc_pase_t type definition)

    */
    g_pase.it = OC_SPAKE_IT;
    oc_spake_parameter_exchange(g_pase.rnd, g_pase.salt);

    OC_DBG_SPAKE("Rnd:");  OC_LOGbytes_SPAKE(g_pase.rnd, sizeof(g_pase.rnd));
    OC_DBG_SPAKE("Salt:"); OC_LOGbytes_SPAKE(g_pase.salt, sizeof(g_pase.salt));
    OC_DBG_SPAKE("Iterations: %u", g_pase.it);

    #endif 

    oc_rep_begin_root_object();

    // rnd (15)
    oc_rep_i_set_byte_string(root, SPAKE_RND, g_pase.rnd, 32);
    // pbkdf2
    oc_rep_i_set_key(&root_map, SPAKE_PBKDF2);
    oc_rep_begin_object(&root_map, pbkdf2);
    // it (16)
    oc_rep_i_set_uint(pbkdf2, SPAKE_IT, g_pase.it);
    // salt (5)
    oc_rep_i_set_byte_string(pbkdf2, SPAKE_SALT, g_pase.salt, 32);
    oc_rep_end_object(&root_map, pbkdf2);

    oc_rep_end_root_object();

    oc_send_separate_response(&spake_separate_rsp, OC_STATUS_CHANGED);
    return OC_EVENT_DONE;
  }

  #ifdef OC_SPAKE
  // step 2
  if (pase_step == SPAKE_PA_SHARE_P)
  {
    // return 2.04 changed, frame shareV, confirmV

    mbedtls_mpi_free(&spake_data.w0);
    mbedtls_ecp_point_free(&spake_data.L);
    mbedtls_mpi_free(&spake_data.y);
    mbedtls_ecp_point_free(&spake_data.pub_y);

    mbedtls_mpi_init(&spake_data.w0);
    mbedtls_ecp_point_init(&spake_data.L);
    mbedtls_mpi_init(&spake_data.y);
    mbedtls_ecp_point_init(&spake_data.pub_y);

    int ret = oc_spake_get_w0_L_params(sizeof(g_pase.salt), g_pase.salt, g_pase.it, &spake_data.w0, &spake_data.L);
    if (ret != 0)
    {
      OC_ERR("oc_spake_get_w0_L_params failed with code %d", ret);
      goto error;
    }

    ret = oc_spake_gen_keypair(&spake_data.y, &spake_data.pub_y);
    if (ret != 0)
    {
      OC_ERR("oc_spake_gen_keypair failed with code %d", ret);
      goto error;
    }

    // next step: calculate pB, encode it into the struct
    mbedtls_ecp_point pB;
    mbedtls_ecp_point_init(&pB);
    ret = oc_spake_calc_shareV(&pB, &spake_data.pub_y, &spake_data.w0);
    if (ret != 0)
    {
      OC_ERR("oc_spake_calc_pB failed with code %d", ret);
      mbedtls_ecp_point_free(&pB);
      goto error;
    }

    ret = oc_spake_encode_pubkey(&pB, g_pase.shareV);
    if (ret != 0)
    {
      OC_ERR("oc_spake_encode_pubkey failed with code %d", ret);
      mbedtls_ecp_point_free(&pB);
      goto error;
    }
    ret = oc_spake_calc_transcript_responder(&spake_data, g_pase.shareP, &pB);
    if (ret != 0)
    {
      OC_ERR("oc_spake_calc_transcript_responder failed with code %d", ret);
      mbedtls_ecp_point_free(&pB);
      goto error;
    }

    oc_spake_calc_confirmV(spake_data.K_main, g_pase.confirmV, g_pase.shareP);
    mbedtls_ecp_point_free(&pB);

    // return 2.04 changed, frame shareV (11) & confirmV (13)

    oc_rep_begin_root_object();

    // shareV (11)
    oc_rep_i_set_byte_string(root, SPAKE_PB_SHARE_V, g_pase.shareV, sizeof(g_pase.shareV));
    // confirmV (13)
    oc_rep_i_set_byte_string(root, SPAKE_CB_CONFIRM_V, g_pase.confirmV, sizeof(g_pase.confirmV));

    oc_rep_end_root_object();

    oc_send_separate_response(&spake_separate_rsp, OC_STATUS_CHANGED);
    return OC_EVENT_DONE;
  }

  // step 3
  if (pase_step == SPAKE_CA_CONFIRM_P)
  {
    // return 2.04 changed, empty payload

    // calculate expected cA
    uint8_t expected_ca[32];

    OC_DBG_SPAKE("KaKe & pB Bytes");
    OC_LOGbytes_OSCORE(spake_data.K_main, 32);
    OC_LOGbytes_OSCORE(g_pase.shareV, sizeof(g_pase.shareV));
    oc_spake_calc_confirmP(spake_data.K_main, expected_ca, g_pase.shareV);
    OC_DBG_SPAKE("cA:");
    OC_LOGbytes_OSCORE(expected_ca, 32);

    if (memcmp(expected_ca, g_pase.confirmP, sizeof(g_pase.confirmP)) != 0)
    {
      OC_ERR("oc_spake_calc_confirmP failed");
      goto error;
    }

    // shared_key is 16-byte array - NOT NULL TERMINATED
    uint8_t shared_key[16] = {0};
    oc_spake_calc_K_shared(spake_data.K_main, shared_key);

    // set the /auth/at entry with the calculated shared key
    // knx does not have multiple devices per instance (for now), so hardcode the use of the first device

    // update pase token in AT table
    OC_DBG_SPAKE("update PASE token for (server) device after successful negotiation with MaC");

    // debugging
    PRINT("set id : (%llu) ", oc_byte_string_len(g_pase.id));
    oc_char_println_hex(oc_string(g_pase.id), oc_byte_string_len(g_pase.id));
    PRINT("set ms : (%llu) ", sizeof(shared_key));
    oc_char_println_hex(shared_key, sizeof(shared_key));

    // - create the token & store in at table (usually at position 0)
    // - note there should be no entries, if there is an entry then overwrite it
    // - it is a by MaC freely chosen id
    oc_oscore_set_auth_shared(oc_string(g_pase.id), oc_byte_string_len(g_pase.id), shared_key, sizeof(shared_key));

    // empty payload
    oc_send_empty_separate_response(&spake_separate_rsp, OC_STATUS_CHANGED);

    // handshake completed successfully - clear state
    memset(spake_data.K_main, 0, sizeof(spake_data.K_main));
    mbedtls_ecp_point_free(&spake_data.L);
    mbedtls_ecp_point_free(&spake_data.pub_y);
    mbedtls_mpi_free(&spake_data.w0);
    mbedtls_mpi_free(&spake_data.y);

    mbedtls_ecp_point_init(&spake_data.L);
    mbedtls_ecp_point_init(&spake_data.pub_y);
    mbedtls_mpi_init(&spake_data.w0);
    mbedtls_mpi_init(&spake_data.y);

    // reset pase object, except id (it holds an allocated oc_string stack memory)
    memset(g_pase.shareP, 0, sizeof(g_pase.shareP));
    memset(g_pase.shareV, 0, sizeof(g_pase.shareV));
    memset(g_pase.confirmP, 0, sizeof(g_pase.confirmP));
    memset(g_pase.confirmV, 0, sizeof(g_pase.confirmV));
    memset(g_pase.rnd, 0, sizeof(g_pase.rnd));
    memset(g_pase.salt, 0, sizeof(g_pase.salt));

    g_pase.it = OC_SPAKE_IT;

    return OC_EVENT_DONE;
  }

  error:

  PRINT("oc_core_knx_spake_separate_post_handler - error");

  // be paranoid: wipe all global data after an error
  memset(spake_data.K_main, 0, sizeof(spake_data.K_main));
  mbedtls_ecp_point_free(&spake_data.L);
  mbedtls_ecp_point_free(&spake_data.pub_y);
  mbedtls_mpi_free(&spake_data.w0);
  mbedtls_mpi_free(&spake_data.y);

  mbedtls_ecp_point_init(&spake_data.L);
  mbedtls_ecp_point_init(&spake_data.pub_y);
  mbedtls_mpi_init(&spake_data.w0);
  mbedtls_mpi_init(&spake_data.y);
  #endif 

  // reset pase object, except id (it holds an allocated oc_string stack memory)
  memset(g_pase.shareP, 0, sizeof(g_pase.shareP));
  memset(g_pase.shareV, 0, sizeof(g_pase.shareV));
  memset(g_pase.confirmP, 0, sizeof(g_pase.confirmP));
  memset(g_pase.confirmV, 0, sizeof(g_pase.confirmV));
  memset(g_pase.rnd, 0, sizeof(g_pase.rnd));
  memset(g_pase.salt, 0, sizeof(g_pase.salt));


  #ifdef OC_SPAKE
  g_pase.it = OC_SPAKE_IT;
  increment_counter();
  #endif

  oc_send_separate_response(&spake_separate_rsp, OC_STATUS_BAD_REQUEST);
  return OC_EVENT_DONE;
}

// resource definition, details/comments see on
// 'core_resource_well_known_core_final'
PRAGMA_IN oc_resource_data_t core_resource_knx_spake_data;
const oc_resource_t core_resource_knx_spake = {(oc_resource_t*)&core_resource_knx_idevid,
                                               0,
                                               {NULL, sizeof("/.well-known/knx/spake"), "/.well-known/knx/spake"},
                                               {NULL, 0, NULL},
                                               {NULL, 0, NULL},
                                               {APPLICATION_CBOR, CONTENT_NONE},
                                               OC_DISCOVERABLE,
                                               {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                               {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                               {oc_core_knx_spake_post_handler, NULL, OC_ACL_NONE, OC_IF_NONE},
                                               {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                               {NULL, NULL},
                                               {NULL, NULL},
                                               0,
                                               0,
                                               true,
                                               &core_resource_knx_spake_data};
PRAGMA_OUT

static void oc_create_knx_spake_resource(int resource_idx)
{
  OC_DBG("oc_create_knx_spake_resource");
  oc_core_populate_resource(resource_idx, "/.well-known/knx/spake", APPLICATION_CBOR, CONTENT_NONE, OC_DISCOVERABLE,
                            0, 0, oc_core_knx_spake_post_handler, 0, 0);
}

#ifdef OC_SPAKE
int oc_initialise_spake_data(void)
{
  // can fail if initialization of the RNG does not work (return == 0)
  if(oc_spake_init() != 0) 
    return -1;

  mbedtls_mpi_init(&spake_data.w0);
  mbedtls_ecp_point_init(&spake_data.L);
  mbedtls_mpi_init(&spake_data.y);
  mbedtls_ecp_point_init(&spake_data.pub_y);

  // start SPAKE brute force protection timer
  oc_set_delayed_callback(NULL, decrement_counter, 10);

  return 0;
}
#endif 

void oc_knx_set_idevid(const char* idevid, int len)
{
  oc_free_string(&g_idevid);
  oc_new_string(&g_idevid, idevid, len);
}

void oc_knx_set_ldevid(char* ldevid, int len)
{
  oc_free_string(&g_ldevid);
  oc_new_string(&g_ldevid, ldevid, len);
}

void oc_knx_load_fingerprint(void)
{
  g_fingerprint = 0; // set to zero for reading error cases
  oc_storage_read(FINGERPRINT_STORE, (uint8_t*)&g_fingerprint, sizeof(g_fingerprint));
}

void oc_knx_increase_fingerprint(void)
{
  g_fingerprint++; // must be only different
  oc_storage_write(FINGERPRINT_STORE, (uint8_t*)&g_fingerprint, sizeof(g_fingerprint));
}

bool oc_is_device_in_runtime()
{
  oc_device_info_t* device = oc_core_get_device_info();

  if (device->iid == 0)
  {
    // EITT test this for a reset with code 2
    return false;
  }

  if (device->lsm_s != LSM_S_LOADED)
  {

    return false;
  }

  return true;
}
