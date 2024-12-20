/*
 // Copyright (c) 2021-2024 Cascoda Ltd
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
#include "api/oc_knx_dev.h"
#include "api/oc_knx_fp.h"
#include "api/oc_knx_gm.h"
#include "api/oc_knx_sec.h"
#include "api/oc_knx_helpers.h"
#include "api/oc_main.h"
#include "port/dns-sd.h"
#include <oc_storage.h> 

#ifdef OC_IOT_ROUTER
#include "api/oc_knx_gm.h"
#endif

#include "oc_core_res.h"
#include "oc_discovery.h"
#include <stdio.h>
#define __STDC_FORMAT_MACROS  // defined to use format specifiers also in C++
#include <inttypes.h>


static void oc_core_dev_sn_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void) data;
  (void) iface_mask;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  size_t device_index = request->resource->device;
  oc_device_info_t* device = oc_core_get_device_info(device_index);
  if (device != NULL)
  {
    // Content-Format: "application/cbor"
    // Payload: "123ABC"
    oc_rep_begin_root_object();
    oc_rep_i_set_text_string(root, 1, oc_string(device->serialnumber));
    oc_rep_end_root_object();

    oc_send_cbor_response(request, OC_STATUS_OK);
    return;
  }

  oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(dev_sn, dev_hwv, 0, "/dev/sn", OC_IF_D,
                                     APPLICATION_CBOR, OC_DISCOVERABLE,
                                     oc_core_dev_sn_get_handler, 0, 0, 0,
                                     "urn:knx:dpt.serNum", OC_SIZE_MANY(1),
                                     "urn:knx:dpa:0.11");

void oc_create_dev_sn_resource(int resource_idx, size_t device)
{
  OC_DBG("oc_create_dev_sn_resource");
  // rt :dpa:0.11
  // rt :dpt.serNum
  oc_core_populate_resource(
    resource_idx, device, "/dev/sn", OC_IF_D, APPLICATION_CBOR, OC_DISCOVERABLE,
    oc_core_dev_sn_get_handler, 0, 0, 0, 1, "urn:knx:dpa:0.11");

  oc_core_bind_dpt_resource(resource_idx, device, "urn:knx:dpt.serNum");
}

// -----------------------------------------------------------------------------

static void oc_core_dev_hwv_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void) data;
  (void) iface_mask;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
    return;
  }
  PRINT("oc_core_dev_hwv_get_handler");

  size_t device_index = request->resource->device;
  oc_device_info_t* device = oc_core_get_device_info(device_index);
  if (device != NULL)
  {
    // cbor with payload: [ 1, 2, 3 ]
    uint64_t array[3];
    array[0] = device->hwv.major;
    array[1] = device->hwv.minor;
    array[2] = device->hwv.patch;
    oc_rep_begin_root_object();
    oc_rep_i_set_int_array(root, 1, array, 3);
    oc_rep_end_root_object();
    oc_send_cbor_response(request, OC_STATUS_OK);
    return;
  }
  oc_send_response_no_format(request, OC_STATUS_INTERNAL_SERVER_ERROR);
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(dev_hwv, dev_fwv, 0, "/dev/hwv", OC_IF_D,
                                     APPLICATION_CBOR, OC_DISCOVERABLE,
                                     oc_core_dev_hwv_get_handler, 0, 0, 0,
                                     "urn:knx:dpt.version", OC_SIZE_ZERO());

void oc_create_dev_hwv_resource(int resource_idx, size_t device)
{
  OC_DBG("oc_create_dev_hwv_resource\n");
  oc_core_populate_resource(resource_idx, device, "/dev/hwv", OC_IF_D,
                            APPLICATION_CBOR, OC_DISCOVERABLE,
                            oc_core_dev_hwv_get_handler, 0, 0, 0, 0);

  oc_core_bind_dpt_resource(resource_idx, device, "urn:knx:dpt.version");
}

// -----------------------------------------------------------------------------

static void oc_core_dev_fwv_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void) data;
  (void) iface_mask;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  PRINT("oc_core_dev_fwv_get_handler - start");

  size_t device_index = request->resource->device;
  oc_device_info_t* device = oc_core_get_device_info(device_index);
  if (device != NULL)
  {
    // Content-Format: "application/cbor"
    // Payload: [ a, b, c ]
    const uint64_t array[3] = { device->fwv.major, device->fwv.minor, device->fwv.patch };
    oc_rep_begin_root_object();
    oc_rep_i_set_int_array(root, 1, array, 3);
    oc_rep_end_root_object();
    oc_send_cbor_response(request, OC_STATUS_OK);
    return;
  }

  oc_send_response_no_format(request, OC_STATUS_INTERNAL_SERVER_ERROR);
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(dev_fwv, dev_hwt, 0, "/dev/fwv", OC_IF_D,
                                     APPLICATION_CBOR, OC_DISCOVERABLE,
                                     oc_core_dev_fwv_get_handler, 0, 0, 0,
                                     "urn:knx:dpt.version", OC_SIZE_MANY(1),
                                     "urn:knx:dpa.0.25");

void oc_create_dev_fwv_resource(int resource_idx, size_t device)
{
  OC_DBG("oc_create_dev_fwv_resource\n");
  oc_core_populate_resource(resource_idx, device, "/dev/fwv", OC_IF_D,
                            APPLICATION_CBOR, OC_DISCOVERABLE,
                            oc_core_dev_fwv_get_handler, 0, 0, 0, 1,
                            "urn:knx:dpa.0.25");

  oc_core_bind_dpt_resource(resource_idx, device, "urn:knx:dpt.version");
}

// -----------------------------------------------------------------------------

static void oc_core_dev_hwt_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void) data;
  (void) iface_mask;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  size_t device_index = request->resource->device;
  oc_device_info_t* device = oc_core_get_device_info(device_index);
  if (device != NULL && oc_string(device->hwt) != NULL)
  {
    oc_rep_begin_root_object();
    oc_rep_i_set_text_string(root, 1, oc_string(device->hwt));
    oc_rep_end_root_object();
    oc_send_cbor_response(request, OC_STATUS_OK);
    return;
  }
  oc_send_response_no_format(request, OC_STATUS_INTERNAL_SERVER_ERROR);
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(dev_hwt, dev_model, 0, "/dev/hwt", OC_IF_D,
                                     APPLICATION_CBOR, OC_DISCOVERABLE,
                                     oc_core_dev_hwt_get_handler, 0, 0, 0,
                                     "urn:knx:dpt.varString8859_1",
                                     OC_SIZE_ZERO());

void oc_create_dev_hwt_resource(int resource_idx, size_t device)
{
  OC_DBG("oc_create_dev_hwt_resource\n");
  // cbor rt :dpt.varString8859_1
  oc_core_populate_resource(resource_idx, device, "/dev/hwt", OC_IF_D,
                            APPLICATION_CBOR, OC_DISCOVERABLE,
                            oc_core_dev_hwt_get_handler, 0, 0, 0, 0);

  oc_core_bind_dpt_resource(resource_idx, device,
                            "urn:knx:dpt.varString8859_1");
}

// -----------------------------------------------------------------------------

static void oc_core_dev_model_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void) data;
  (void) iface_mask;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  size_t device_index = request->resource->device;
  oc_device_info_t* device = oc_core_get_device_info(device_index);
  if (device != NULL && oc_string(device->model) != NULL)
  {
    oc_rep_begin_root_object();
    oc_rep_i_set_text_string(root, 1, oc_string(device->model));
    oc_rep_end_root_object();
    oc_send_cbor_response(request, OC_STATUS_OK);
    return;
  }
  oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(dev_model, dev_hostname, 0, "/dev/model",
                                     OC_IF_D, APPLICATION_CBOR, OC_DISCOVERABLE,
                                     oc_core_dev_model_get_handler, 0, 0, 0,
                                     "urn:knx:dpt.utf8", OC_SIZE_MANY(1),
                                     "urn:knx:dpa.0.15");

void oc_create_dev_model_resource(int resource_idx, size_t device)
{
  OC_DBG("oc_create_dev_model_resource\n");
  oc_core_populate_resource(resource_idx, device, "/dev/model", OC_IF_D,
                            APPLICATION_CBOR, OC_DISCOVERABLE,
                            oc_core_dev_model_get_handler, 0, 0, 0, 1,
                            "urn:knx:dpa.0.15");

  oc_core_bind_dpt_resource(resource_idx, device, "urn:knx:dpt.utf8");
}

// -----------------------------------------------------------------------------

static void oc_core_dev_hostname_put_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void) data;
  (void) iface_mask;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  size_t device_index = request->resource->device;
  oc_rep_t* rep = request->request_payload;
  while (rep != NULL)
  {
    if (rep->type == OC_REP_STRING)
    {
      if (rep->iname == 1) // CBOR value
      {
        PRINT("oc_core_dev_hostname_put_handler received : %s", oc_string_checked(rep->value.string));

        // set hostname for the device 
        oc_core_set_device_hostname(device_index, oc_string(rep->value.string));

        // update storage 
        oc_storage_write(KNX_STORAGE_HOSTNAME, (uint8_t*) oc_string(rep->value.string), oc_string_len(rep->value.string));

        // call host name application callback handler 
        oc_hostname_t* my_hostname = oc_get_hostname_cb();
        if (my_hostname && my_hostname->cb)
        {
          my_hostname->cb(device_index, rep->value.string, my_hostname->data);
        }

        oc_send_response_no_format(request, OC_STATUS_CHANGED);
        return;
      }
    }
    rep = rep->next;
  }

  oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
}

static void oc_core_dev_hostname_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void) data;
  (void) iface_mask;

  PRINT("oc_core_dev_hostname_get_handler - start");

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  size_t device_index = request->resource->device;
  oc_device_info_t* device = oc_core_get_device_info(device_index);

  if (device != NULL)
  {
    oc_rep_begin_root_object();
    oc_rep_i_set_text_string(root, 1, oc_string(device->hostname));
    oc_rep_end_root_object();
    oc_send_cbor_response(request, OC_STATUS_OK);
    return;
  }
  oc_send_cbor_response(request, OC_STATUS_OK);
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(dev_hostname, dev_iid, 0, "/dev/hname",
                                     OC_IF_P, APPLICATION_CBOR, OC_DISCOVERABLE,
                                     oc_core_dev_hostname_get_handler,
                                     oc_core_dev_hostname_put_handler, 0, 0,
                                     "urn:knx:dpt.varString8859_1",
                                     OC_SIZE_ZERO());

void oc_create_dev_hostname_resource(int resource_idx, size_t device)
{
  OC_DBG("oc_create_dev_hostname_resource");
  oc_core_populate_resource(resource_idx, device, "/dev/hname", OC_IF_P,
                            APPLICATION_CBOR, OC_DISCOVERABLE,
                            oc_core_dev_hostname_get_handler,
                            oc_core_dev_hostname_put_handler, 0, 0, 0);

  oc_core_bind_dpt_resource(resource_idx, device, "urn:knx:dpt.varString8859_1");
}

// -----------------------------------------------------------------------------

static void oc_core_dev_iid_put_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void) data;
  (void) iface_mask;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  size_t device_index = request->resource->device;
  oc_rep_t* rep = request->request_payload;
  while (rep != NULL)
  {
    if (rep->type == OC_REP_INT)
    {
      if (rep->iname == 1)
      {
        PRINT("oc_core_dev_iid_put_handler received : %" PRId64 "", rep->value.integer);
        oc_core_set_device_iid(device_index, rep->value.integer);
        // make the value persistent
        oc_storage_write(KNX_STORAGE_IID, (uint8_t*) &rep->value.integer, sizeof(uint64_t));
        oc_send_response_no_format(request, OC_STATUS_CHANGED);

        // do the run time installation
        if (oc_is_device_in_runtime(device_index))
        {
          oc_register_group_multicasts();
          oc_init_datapoints_at_initialization();
          const oc_device_info_t* device = oc_core_get_device_info(device_index);
          knx_publish_service(oc_string(device->serialnumber), device->iid, device->ia, device->pm);
        }
        return;
      }
    }
    rep = rep->next;
  }

  oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
}

static void oc_core_dev_iid_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void) data;
  (void) iface_mask;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  size_t device_index = request->resource->device;
  oc_device_info_t* device = oc_core_get_device_info(device_index);
  if (device != NULL)
  {
    oc_rep_begin_root_object();
    oc_rep_i_set_int(root, 1, device->iid);
    oc_rep_end_root_object();

    oc_send_cbor_response(request, OC_STATUS_OK);
    return;
  }

  oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(dev_iid, dev_pm, 0, "/dev/iid", OC_IF_P,
                                     APPLICATION_CBOR, OC_DISCOVERABLE,
                                     oc_core_dev_iid_get_handler,
                                     oc_core_dev_iid_put_handler, 0, 0,
                                     "urn:knx:dpt.value8Ucount",
                                     OC_SIZE_ZERO());

void oc_create_dev_iid_resource(int resource_idx, size_t device)
{
  OC_DBG("oc_create_dev_iid_resource\n");
  oc_core_populate_resource(resource_idx, device, "/dev/iid", OC_IF_P,
                            APPLICATION_CBOR, OC_DISCOVERABLE,
                            oc_core_dev_iid_get_handler,
                            oc_core_dev_iid_put_handler, 0, 0, 0);

  oc_core_bind_dpt_resource(resource_idx, device, "urn:knx:dpt.value8Ucount");
}

// -----------------------------------------------------------------------------



static void oc_core_dev_ipv6_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void) data;
  (void) iface_mask;

  int first_entry = 0;    // first entry number of a resource that will be  placed on a page
  int total = 0;
  int query_pn;           // page number (page size as request parameter is not used)

  PRINT("oc_core_dev_ipv6_get_handler - start");

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  const size_t device_index = request->resource->device;
  const oc_endpoint_t* my_ep = oc_connectivity_get_endpoints(device_index);

  // calculate total endpoints from the device
  while (my_ep != NULL)
  {
    my_ep = my_ep->next;
    total++;
  }

  // handle query parameters l=ps and/or l=total
  if (query_l_was_processed(request, BATCH_SIZE, total))
    return;

  my_ep = oc_connectivity_get_endpoints(device_index);

  // handle query with page number (pn)
  if (check_if_query_pn_exist(request, &query_pn))
  {
    // update only when pn query parameter was present
    first_entry += query_pn * BATCH_SIZE;

    // check only when pn query parameter was present ...
    // ... that requested page would carry at least one resource e.g; total=10, page=5 -> no data on page 5 
    if (first_entry >= total)
    {
      oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
      return;
    }

    // skip endpoints and return the requested one (for the requested page) 
    for (int i = 0; i < first_entry; i++)
    {
      my_ep = my_ep->next;
    }
  }

  // return the single entry (for the requested page)
  oc_rep_begin_root_object();
  oc_rep_i_set_byte_string(root, 1, my_ep->addr.ipv6.address, sizeof(my_ep->addr.ipv6.address));
  oc_rep_end_root_object();

  oc_send_cbor_response(request, OC_STATUS_OK);

  PRINT("oc_core_dev_ipv6_get_handler - end");
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(dev_ipv6, dev_sa, 0, "/dev/ipv6", OC_IF_P,
                                     APPLICATION_CBOR, OC_DISCOVERABLE,
                                     oc_core_dev_ipv6_get_handler, 0, 0, 0,
                                     "urn:knx:dpt.ipv6", OC_SIZE_ZERO());

void oc_create_dev_ipv6_resource(int resource_idx, size_t device)
{
  OC_DBG("oc_create_dev_ipv6_resource\n");
  oc_core_populate_resource(resource_idx, device, "/dev/ipv6", OC_IF_P,
                            APPLICATION_CBOR, OC_DISCOVERABLE,
                            oc_core_dev_ipv6_get_handler, 0, 0, 0, 0);

  oc_core_bind_dpt_resource(resource_idx, device, "urn:knx:dpt.ipv6");
}

// -----------------------------------------------------------------------------

// internal, can only be used/linked from this file 
static void oc_core_dev_pm_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void) data;
  (void) iface_mask;

  PRINT("calling dev/pm GET handler");

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  size_t device_index = request->resource->device;
  oc_device_info_t* device = oc_core_get_device_info(device_index);

  if (device != NULL)
  {
    // cbor_encode_boolean(&g_encoder, device->pm);
    oc_rep_begin_root_object();
    oc_rep_i_set_boolean(root, 1, device->pm); // knx PRG mode 
    oc_rep_end_root_object();

    oc_send_cbor_response(request, OC_STATUS_OK);
    return;
  }

  oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
}

// internal, can only be used/linked from this file 
static void oc_core_dev_pm_put_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void) data;
  (void) iface_mask;

  PRINT("calling dev/pm PUT handler");

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  size_t device_index = request->resource->device;
  oc_device_info_t* device = oc_core_get_device_info(device_index);
  oc_rep_t* rep = request->request_payload;
  const oc_programming_mode_t* my_cb = oc_get_programming_mode_cb();

  while (rep != NULL)
  {
    if (rep->type == OC_REP_BOOL)
    { // note, type does not reflect a 1:1 meaning of the CBOR major types
      if (rep->iname == 1) // CBOR key
      {
        PRINT("oc_core_dev_pm_put_handler received : %d", (int) rep->value.boolean);

        // application programming mode callback handler, if not present PM it is set directly
        if (my_cb && my_cb->cb)
          my_cb->cb(device_index, rep->value.boolean, my_cb->data);
        else
          device->pm = rep->value.boolean;

        oc_send_response_no_format(request, OC_STATUS_CHANGED);

        knx_publish_service(oc_string(device->serialnumber), device->iid, device->ia, device->pm);
        oc_storage_write(KNX_STORAGE_PM, (uint8_t*) &(rep->value.boolean), 1);
        return;
      }
    }
    rep = rep->next;
  }

  oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(dev_pm, dev_ipv6, 0, "/dev/pm", OC_IF_P,
                                     APPLICATION_CBOR, OC_DISCOVERABLE,
                                     oc_core_dev_pm_get_handler,
                                     oc_core_dev_pm_put_handler, 0, 0,
                                     "urn:knx:dpt.binaryValue", OC_SIZE_MANY(1),
                                     "urn:knx:dpa.0.54");

void oc_create_dev_pm_resource(int resource_idx, size_t device)
{
  OC_DBG("oc_create_dev_pm_resource");
  oc_core_populate_resource(
    resource_idx, device, "/dev/pm", OC_IF_P, APPLICATION_CBOR, OC_DISCOVERABLE,
    oc_core_dev_pm_get_handler, oc_core_dev_pm_put_handler, 0, 0, 1,
    "urn:knx:dpa.0.54");

  oc_core_bind_dpt_resource(resource_idx, device, "urn:knx:dpt.binaryValue");
}

// -----------------------------------------------------------------------------

static void oc_core_dev_dev_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void) data;
  (void) iface_mask;
  size_t response_length = 0;
  int query_parameter_kvpair_matches = 0; // how many (to this device applicable) query parameter key/value pair matches where found 

  int total = OC_DEV - OC_DEV_SN;         // total entries of this resource 
  int first_entry = OC_DEV_SN;            // first entry number of a resource that will be placed on a page
  int last_entry = OC_DEV;                // last entry number of a resource that will be placed on a page     

  int query_pn;                // page number (page size as request parameter is not used)
  bool more_request_needed = false; // if more requests (pages) are needed to get the full list

  PRINT("oc_core_dev_dev_get_handler - start");

  if (!oc_accept_header_is_ok(request, APPLICATION_LINK_FORMAT))
  {
    request->response->response_buffer->code = oc_status_code(OC_STATUS_BAD_REQUEST);
    return;
  }

  const size_t device_index = request->resource->device;

  // handle query parameters l=ps and/or l=total
  if (query_l_was_processed(request, PAGE_SIZE, total))
    return;

  // handle query with page number (pn)
  if (check_if_query_pn_exist(request, &query_pn))
  {
    // update only when pn query parameter was present
    first_entry += query_pn * PAGE_SIZE;

    // check only when pn query parameter was present ...
    // ... that requested page would carry at least one resource e.g; total=10, page=5 -> no data on page 5 
    if (first_entry >= last_entry)
    {
      oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
      return;
    }
  }

  if (last_entry > first_entry + PAGE_SIZE)
  {
    last_entry = first_entry + PAGE_SIZE;
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
    oc_send_linkformat_response(request, OC_STATUS_OK, response_length);
  }
  else
  {
    oc_send_response_no_format(request, OC_STATUS_INTERNAL_SERVER_ERROR);
  }

  PRINT("oc_core_dev_dev_get_handler - end");
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(dev, app, 0, "/dev", OC_IF_LI | OC_IF_D,
                                     APPLICATION_LINK_FORMAT, OC_DISCOVERABLE,
                                     oc_core_dev_dev_get_handler, 0, 0, 0, NULL,
                                     OC_SIZE_MANY(1), "urn:knx:fb.0");

void oc_create_dev_dev_resource(int resource_idx, size_t device)
{
  OC_DBG("oc_create_dev_dev_resource\n");
  // note that this resource is listed in /.well-known/core so it should have
  // the full rt with urn:knx prefix
  oc_core_populate_resource(
    resource_idx, device, "/dev", OC_IF_LI | OC_IF_D, APPLICATION_LINK_FORMAT,
    OC_DISCOVERABLE, oc_core_dev_dev_get_handler, 0, 0, 0, 1, "urn:knx:fb.0");
}

// -----------------------------------------------------------------------------

// 16 bit KNX ia = sa(8)+da(8), example Subnetwork Add. (sa) 0 + (da) Device Add. 1 = 0x0001
static void oc_core_dev_sa_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void) data;
  (void) iface_mask;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  size_t device_index = request->resource->device;
  oc_device_info_t* device = oc_core_get_device_info(device_index);
  if (device != NULL)
  {
    oc_rep_begin_root_object();
    const uint8_t sa = device->ia >> 8;
    oc_rep_i_set_int(root, 1, sa);
    oc_rep_end_root_object();

    oc_send_cbor_response(request, OC_STATUS_OK);
    return;
  }

  oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(dev_sa, dev_da, 0, "/dev/sna", OC_IF_P,
                                     APPLICATION_CBOR, OC_DISCOVERABLE,
                                     oc_core_dev_sa_get_handler, 0, 0, 0,
                                     "urn:knx:dpt.value1Ucount",
                                     OC_SIZE_MANY(1), "urn:knx:dpa.0.57");

static void oc_create_dev_sa_resource(int resource_idx, size_t device)
{
  OC_DBG("oc_create_dev_sa_resource");
  oc_core_populate_resource(resource_idx, device, "/dev/sna", OC_IF_P,
                            APPLICATION_CBOR, OC_DISCOVERABLE,
                            oc_core_dev_sa_get_handler, 0, 0, 0, 1,
                            "urn:knx:dpa.0.57");

  oc_core_bind_dpt_resource(resource_idx, device, "urn:knx:dpt.value1Ucount");
}

// -----------------------------------------------------------------------------

static void oc_core_dev_da_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void) data;
  (void) iface_mask;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  size_t device_index = request->resource->device;
  oc_device_info_t* device = oc_core_get_device_info(device_index);
  if (device != NULL)
  {
    oc_rep_begin_root_object();

    uint8_t da = device->ia;
    oc_rep_i_set_int(root, 1, da);
    oc_rep_end_root_object();
    oc_send_cbor_response(request, OC_STATUS_OK);
    return;
  }

  oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(dev_da, dev_fid, 0, "/dev/da", OC_IF_P,
                                     APPLICATION_CBOR, OC_DISCOVERABLE,
                                     oc_core_dev_da_get_handler, 0, 0, 0,
                                     "urn:knx:dpa.0.58", OC_SIZE_MANY(1),
                                     "urn:knx:dpa.0.58");

static void oc_create_dev_da_resource(int resource_idx, size_t device)
{
  OC_DBG("oc_create_dev_da_resource");
  oc_core_populate_resource(
    resource_idx, device, "/dev/da", OC_IF_P, APPLICATION_CBOR, OC_DISCOVERABLE,
    oc_core_dev_da_get_handler, 0, 0, 0, 1, "urn:knx:dpa.0.58");

  oc_core_bind_dpt_resource(resource_idx, device, "urn:knx:dpt.value1Ucount");
}

// -----------------------------------------------------------------------------

static void oc_core_dev_fid_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void) data;
  (void) iface_mask;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  size_t device_index = request->resource->device;
  const oc_device_info_t* device = oc_core_get_device_info(device_index);
  if (device != NULL)
  {
    oc_rep_begin_root_object();
    oc_rep_i_set_int(root, 1, device->fid);
    oc_rep_end_root_object();

    oc_send_cbor_response(request, OC_STATUS_OK);
    return;
  }

  oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
}

static void oc_core_dev_fid_put_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void) data;
  (void) iface_mask;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  size_t device_index = request->resource->device;
  oc_rep_t* rep = request->request_payload;
  while (rep != NULL)
  {
    if (rep->type == OC_REP_INT)
    {
      if (rep->iname == 1)
      {
        PRINT("oc_core_dev_fid_put_handler received : %" PRId64 "", rep->value.integer);
        oc_core_set_device_fid(device_index, rep->value.integer);
        uint64_t temp = rep->value.integer;
        oc_storage_write(KNX_STORAGE_FID, (uint8_t*) &temp, sizeof(temp));
        oc_send_response_no_format(request, OC_STATUS_CHANGED);
        return;
      }
    }
    rep = rep->next;
  }

  oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(dev_fid, dev_port, 0, "/dev/fid", OC_IF_P,
                                     APPLICATION_CBOR, OC_DISCOVERABLE,
                                     oc_core_dev_fid_get_handler,
                                     oc_core_dev_fid_put_handler, 0, 0,
                                     "urn:knx:dpt.value8Ucount",
                                     OC_SIZE_ZERO());

static void oc_create_dev_fid_resource(int resource_idx, size_t device)
{
  OC_DBG("oc_create_dev_fid_resource\n");
  oc_core_populate_resource(resource_idx, device, "/dev/fid", OC_IF_P,
                            APPLICATION_CBOR, OC_DISCOVERABLE,
                            oc_core_dev_fid_get_handler,
                            oc_core_dev_fid_put_handler, 0, 0, 0);

  oc_core_bind_dpt_resource(resource_idx, device, "urn:knx:dpt.value8Ucount");
}

// -----------------------------------------------------------------------------

static void oc_core_dev_port_get_handler(oc_request_t* request,
                                         oc_interface_mask_t iface_mask, void* data)
{
  (void) data;
  (void) iface_mask;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  size_t device_index = request->resource->device;
  oc_device_info_t* device = oc_core_get_device_info(device_index);
  if (device != NULL)
  {
    oc_rep_begin_root_object();
    oc_rep_i_set_int(root, 1, device->port);
    oc_rep_end_root_object();
    oc_send_cbor_response(request, OC_STATUS_OK);
    return;
  }

  oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(dev_port, dev_mport, 0, "/dev/port",
                                     OC_IF_P, APPLICATION_CBOR, OC_DISCOVERABLE,
                                     oc_core_dev_port_get_handler, 0, 0, 0,
                                     "urn:knx:dpt.value2Ucount",
                                     OC_SIZE_ZERO());

static void oc_create_dev_port_resource(int resource_idx, size_t device)
{
  OC_DBG("oc_create_dev_port_resource\n");
  oc_core_populate_resource(resource_idx, device, "/dev/port", OC_IF_P,
                            APPLICATION_CBOR, OC_DISCOVERABLE,
                            oc_core_dev_port_get_handler, 0, 0, 0, 0);

  oc_core_bind_dpt_resource(resource_idx, device, "urn:knx:dpt.value2Ucount");
}

// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------

static void oc_core_dev_mport_get_handler(oc_request_t* request,
                                          oc_interface_mask_t iface_mask, void* data)
{
  (void) data;
  (void) iface_mask;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  size_t device_index = request->resource->device;
  oc_device_info_t* device = oc_core_get_device_info(device_index);
  if (device != NULL)
  {
    oc_rep_begin_root_object();
    oc_rep_i_set_int(root, 1, device->mport);
    oc_rep_end_root_object();
    oc_send_cbor_response(request, OC_STATUS_OK);
    return;
  }

  oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
}


OC_CORE_CREATE_CONST_RESOURCE_LINKED(dev_mport, dev_mid, 0, "/dev/mport",
                                     OC_IF_P, APPLICATION_CBOR, OC_DISCOVERABLE,
                                     oc_core_dev_mport_get_handler, 0, 0, 0,
                                     "urn:knx:dpt.value2Ucount",
                                     OC_SIZE_ZERO());
void oc_create_dev_mport_resource(int resource_idx, size_t device)
{
  OC_DBG("oc_create_dev_mport_resource\n");
  oc_core_populate_resource(resource_idx, device, "/dev/mport", OC_IF_P,
                            APPLICATION_CBOR, OC_DISCOVERABLE,
                            oc_core_dev_mport_get_handler, 0, 0, 0, 0);

  oc_core_bind_dpt_resource(resource_idx, device, "urn:knx:dpt.value2Ucount");
}

// -----------------------------------------------------------------------------
static int oc_core_dump_ap(size_t device_index)
{
  // KNX_STORAGE_AP
  oc_device_info_t* device = oc_core_get_device_info(device_index);
  if (device != NULL)
  {
    int32_t value = device->ap.major;
    oc_storage_write(KNX_STORAGE_AP_MAJOR, (uint8_t*) &value, sizeof(value));
    value = device->ap.minor;
    oc_storage_write(KNX_STORAGE_AP_MINOR, (uint8_t*) &value, sizeof(value));
    value = device->ap.patch;
    oc_storage_write(KNX_STORAGE_AP_PATCH, (uint8_t*) &value, sizeof(value));
    return 0;
  }
  return -1;
}

static int oc_core_read_ap(size_t device_index)
{
  // KNX_STORAGE_AP
  oc_device_info_t* device = oc_core_get_device_info(device_index);
  if (device != NULL)
  {
    int32_t value;
    long temp_size;

    temp_size =
      oc_storage_read(KNX_STORAGE_AP_MAJOR, (uint8_t*) &value, sizeof(value));
    if (temp_size > 0)
    {
      device->ap.major = value;
    }
    temp_size =
      oc_storage_read(KNX_STORAGE_AP_MINOR, (uint8_t*) &value, sizeof(value));
    if (temp_size > 0)
    {
      device->ap.minor = value;
    }
    temp_size =
      oc_storage_read(KNX_STORAGE_AP_PATCH, (uint8_t*) &value, sizeof(value));
    if (temp_size > 0)
    {
      device->ap.patch = value;
    }
    return 0;
  }
  return -1;
}

static void oc_core_ap_x_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void) data;
  (void) iface_mask;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  size_t device_index = request->resource->device;
  oc_device_info_t* device = oc_core_get_device_info(device_index);
  if (device != NULL)
  {
    // Content-Format: "application/cbor"
    // Payload: [ 1, 2, 3 ]
    uint64_t array[3];
    array[0] = device->ap.major;
    array[1] = device->ap.minor;
    array[2] = device->ap.patch;
    oc_rep_begin_root_object();
    oc_rep_i_set_int_array(root, 1, array, 3);
    oc_rep_end_root_object();
    oc_send_cbor_response(request, OC_STATUS_OK);
    return;
  }

  oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
}

static void oc_core_ap_x_put_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void) data;
  (void) iface_mask;

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  size_t device_index = request->resource->device;
  oc_device_info_t* device = oc_core_get_device_info(device_index);
  oc_rep_t* rep = request->request_payload;

  OC_DBG("oc_core_ap_x_put_handler type: %d", rep ? rep->type : OC_REP_NIL);

  if ((rep != NULL) && (rep->type == OC_REP_INT_ARRAY))
  {
    int64_t* arr = oc_int_array(rep->value.array);
    int array_size = oc_int_array_size(rep->value.array);
    if (array_size != 3)
    {
      oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
      return;
    }
    device->ap.major = (int) arr[0];
    device->ap.minor = (int) arr[1];
    device->ap.patch = (int) arr[2];

    // write to persistent storage
    oc_core_dump_ap(device_index);

    oc_send_response_no_format(request, OC_STATUS_CHANGED);
    return;
  }

  oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(app_x, a_lsm, 0, "/ap/pv", OC_IF_P,
                                     APPLICATION_CBOR, OC_DISCOVERABLE,
                                     oc_core_ap_x_get_handler,
                                     oc_core_ap_x_put_handler, 0, 0,
                                     "urn:knx:dpt.programVersion",
                                     OC_SIZE_MANY(1), "urn:knx:dpa.3.13");

void oc_create_ap_x_resource(int resource_idx, size_t device)
{
  OC_DBG("oc_create_ap_x_resource");
  oc_core_populate_resource(resource_idx, device, "/ap/pv", OC_IF_P,
                            APPLICATION_CBOR, OC_DISCOVERABLE,
                            oc_core_ap_x_get_handler, oc_core_ap_x_put_handler,
                            0, 0, 1, "urn:knx:dpa.3.13");

  oc_core_bind_dpt_resource(resource_idx, device, "urn:knx:dpt.programVersion");
}
// -----------------------------------------------------------------------------

static void oc_core_ap_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void) data;
  (void) iface_mask;

  size_t response_length = 0;
  int query_parameter_kvpair_matches = 0; // how many (to this device applicable) query parameter key/value pair matches where found

  int total = OC_KNX_SPAKE - OC_APP_X;
  int first_entry = OC_APP_X;
  int last_entry = OC_KNX_SPAKE;
  int query_pn;                           // page number (page size as request parameter is not used)
  bool more_request_needed = false;       // If more requests (pages) are needed to get the full list

  PRINT("oc_core_ap_get_handler");

  if (!oc_accept_header_is_ok(request, APPLICATION_LINK_FORMAT))
  {
    oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  size_t device_index = request->resource->device;

  // handle query parameters l=ps and/or l=total
  if (query_l_was_processed(request, PAGE_SIZE, total))
    return;

  // handle query with page number (pn)
  if (check_if_query_pn_exist(request, &query_pn))
  {
    // update only when pn query parameter was present
    first_entry += query_pn * PAGE_SIZE;

    // check only when pn query parameter was present ...
    // ... that requested page would carry at least one resource e.g; total=10, page=5 -> no data on page 5 
    if (first_entry >= last_entry)
    {
      oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
      return;
    }
  }

  if (last_entry > first_entry + PAGE_SIZE)
  {
    last_entry = first_entry + PAGE_SIZE;
    more_request_needed = true;
  }

  for (int i = first_entry; i < last_entry; i++)
  {
    const oc_resource_t* resource =
      oc_core_get_resource_by_index(i, device_index);
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
    oc_send_linkformat_response(request, OC_STATUS_OK, response_length);
  }
  else
  {
    oc_send_response_no_format(request, OC_STATUS_INTERNAL_SERVER_ERROR);
  }

  PRINT("oc_core_ap_get_handler - end");
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(app, app_x, 0, "/ap", OC_IF_P,
                                     APPLICATION_LINK_FORMAT, OC_DISCOVERABLE,
                                     oc_core_ap_get_handler, 0, 0, 0,
                                     "urn:knx:dpt.value2Ucount",
                                     OC_SIZE_MANY(1), "urn:knx:fb.3");

void oc_create_ap_resource(int resource_idx, size_t device)
{
  OC_DBG("oc_create_ap_resource\n");
  oc_core_populate_resource(resource_idx, device, "/ap", OC_IF_P,
                            APPLICATION_LINK_FORMAT, OC_DISCOVERABLE,
                            oc_core_ap_get_handler, 0, 0, 0, 1, "urn:knx:fb.3");

  oc_core_bind_dpt_resource(resource_idx, device, "urn:knx:dpt.value2Ucount");
}

// -----------------------------------------------------------------------------

static void oc_core_dev_mid_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void) data;
  (void) iface_mask;


  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  size_t device_index = request->resource->device;
  oc_device_info_t* device = oc_core_get_device_info(device_index);
  if (device != NULL)
  {
    oc_rep_begin_root_object();
    oc_rep_i_set_int(root, 1, device->mid);
    oc_rep_end_root_object();
    oc_send_cbor_response(request, OC_STATUS_OK);
    return;
  }

  oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(dev_mid, dev, 0, "/dev/mid", OC_IF_P,
                                     APPLICATION_CBOR, OC_DISCOVERABLE,
                                     oc_core_dev_mid_get_handler, 0, 0, 0,
                                     "urn:knx:dpt.value2Ucount",
                                     OC_SIZE_MANY(1), "urn:knx:dpa.0.12");

static void oc_create_dev_mid_resource(int resource_idx, size_t device)
{
  OC_DBG("oc_create_dev_mid_resource\n");
  oc_core_populate_resource(resource_idx, device, "/dev/mid", OC_IF_P,
                            APPLICATION_CBOR, OC_DISCOVERABLE,
                            oc_core_dev_mid_get_handler, 0, 0, 0, 1,
                            "urn:knx:dpa.0.12");

  oc_core_bind_dpt_resource(resource_idx, device, "urn:knx:dpt.value2Ucount");
}

// -----------------------------------------------------------------------------
void oc_knx_device_storage_read(size_t device_index)
{
  PRINT("Loading Device Config from persistent storage");

  if (device_index >= oc_core_get_num_devices())
  {
    PRINT("device_index %d too large", (int) device_index);
    return;
  }

  oc_device_info_t* device = oc_core_get_device_info(device_index);
  if (device == NULL)
  {
    OC_ERR("could not get device %d", (int) device_index);
  }

  // read IA from storage (on error = 0xFFFF)
  uint32_t ia;
  device->ia = oc_storage_read(KNX_STORAGE_IA, (uint8_t*) &ia, sizeof(ia)) > 0 ? ia : 0x0000FFFF;
  PRINT("ia (storage) %u", ia);

  // read host name from storage (on error = 0)
  uint64_t iid;
  device->iid = oc_storage_read(KNX_STORAGE_IID, (uint8_t*) &iid, sizeof(iid)) > 0 ? iid : 0x00000000;
  PRINT("idd (storage) %llu", device->iid);

  // read PRG mode from storage (on error = false)
  bool pm;
  device->pm = oc_storage_read(KNX_STORAGE_PM, (uint8_t*) &pm, sizeof(pm)) > 0 ? pm : false;
  PRINT("pm (storage) %d", pm);

  // read host name from storage (on error = '0')
  char hostname[255] = { 0 };
  int temp_size = oc_storage_read(KNX_STORAGE_HOSTNAME, (uint8_t*) &hostname, 255);
  if (temp_size > 1)
  {
    // '0' terminated host name, used from oc_string 
    oc_core_set_device_hostname(device_index, hostname);
    PRINT("hostname (storage) %s", oc_string_checked(device->hostname));
  }

  oc_core_read_ap(device_index);
}

void oc_knx_device_storage_reset(size_t device_index, int reset_mode)
{

  if (device_index >= oc_core_get_num_devices())
  {
    PRINT("oc_knx_device_storage_reset: device_index %d too large", (int) device_index);
    return;
  }

  oc_device_info_t* device = oc_core_get_device_info(device_index);

  if (device == NULL)
  {
    OC_ERR("oc_knx_device_storage_reset: device is NULL");
    return;
  }

  if (reset_mode == 2)
  {
    // needed as buffer for the call below
    uint32_t u_port = COAP_DEFAULT_PORT;  // unicast communication
    uint32_t m_port = COAP_DEFAULT_PORT;  // multicast communication

    // LSM (first to prevent any runtime messaging in/out)
    oc_a_lsm_set_state(device_index, LSM_S_UNLOADED);

    // set the other data to KNX defaults
    device->pm = false;
    device->ia = 0x0000FFFF;
    device->iid = 0;   // checked in 'runtime test'
    device->fid = 0;
    device->port = u_port;
    device->mport = m_port;

    // set default host name to device '0' SN
    oc_free_string(&device->hostname);
    oc_new_string(&device->hostname, oc_string(device->serialnumber), oc_string_len(device->serialnumber));

    // delete iot device tables 
    oc_delete_group_object_table();
    oc_delete_group_tables();
    oc_delete_at_table(device_index);

  #ifdef OC_IOT_ROUTER
    oc_delete_group_mapping_table();
  #endif

    // writing the empty values
    oc_storage_erase(KNX_STORAGE_IA);
    oc_storage_erase(KNX_STORAGE_IID);
    oc_storage_erase(KNX_STORAGE_FID);
    oc_storage_erase(KNX_STORAGE_PM);

    // writing the default values (default host name = serial number)
    oc_storage_write(KNX_STORAGE_PORT, (uint8_t*) &u_port, sizeof(device->port));
    oc_storage_write(KNX_STORAGE_MPORT, (uint8_t*) &m_port, sizeof(device->mport));
    oc_storage_write(KNX_STORAGE_HOSTNAME, (uint8_t*) oc_string(device->serialnumber), oc_string_len(device->serialnumber));

    return;
  }

  if (reset_mode == 3)
  {
    oc_storage_erase(KNX_STORAGE_IA);

    // set the ia to KNX defaults
    device->pm = false;
    device->ia = 0x0000FFFF;

    return;
  }

  if (reset_mode == 7)
  {
    // LSM (first to prevent any runtime messaging in/out)
    oc_a_lsm_set_state(device_index, LSM_S_UNLOADED);

    // set the ia to KNX defaults
    device->pm = false;

    // delete iot device tables 
    oc_delete_group_object_table();
    oc_delete_group_tables();
    oc_reset_at_table(device_index, reset_mode);

  #ifdef OC_IOT_ROUTER
    oc_delete_group_mapping_table();
  #endif

  }
}

bool oc_knx_device_in_programming_mode(size_t device_index)
{

  if (device_index >= oc_core_get_num_devices())
  {
    PRINT("device_index %d too large", (int) device_index);
    return false;
  }

  oc_device_info_t* device = oc_core_get_device_info(device_index);
  return device->pm;
}

void oc_knx_device_set_programming_mode(size_t device_index, bool programming_mode)
{

  if (device_index >= oc_core_get_num_devices())
  {
    PRINT("device_index %d too large", (int) device_index);
    return;
  }

  oc_device_info_t* device = oc_core_get_device_info(device_index);
  device->pm = programming_mode;
}

void oc_create_knx_device_resources(size_t device_index)
{
  OC_DBG("oc_create_knx_device_resources");

  if (device_index == 0)
  {
    OC_DBG("device 0: KNX device resources created statically");
    return;
  }

  oc_create_dev_sn_resource(OC_DEV_SN, device_index);
  oc_create_dev_hwv_resource(OC_DEV_HWV, device_index);
  oc_create_dev_fwv_resource(OC_DEV_FWV, device_index);
  oc_create_dev_hwt_resource(OC_DEV_HWT, device_index);
  oc_create_dev_model_resource(OC_DEV_MODEL, device_index);
  oc_create_dev_hostname_resource(OC_DEV_HOSTNAME, device_index);
  oc_create_dev_iid_resource(OC_DEV_IID, device_index);
  oc_create_dev_pm_resource(OC_DEV_PM, device_index);
  oc_create_dev_ipv6_resource(OC_DEV_IPV6, device_index);
  oc_create_dev_sa_resource(OC_DEV_SA, device_index);
  oc_create_dev_da_resource(OC_DEV_DA, device_index);
  oc_create_dev_fid_resource(OC_DEV_FID, device_index);
  oc_create_dev_port_resource(OC_DEV_PORT, device_index);
  oc_create_dev_mport_resource(OC_DEV_MPORT, device_index);
  oc_create_dev_mid_resource(OC_DEV_MID, device_index);
  oc_create_ap_resource(OC_APP, device_index);
  oc_create_ap_x_resource(OC_APP_X, device_index);
  // should be last of the dev/xxx resources, it will list those.
  oc_create_dev_dev_resource(OC_DEV, device_index);
}
