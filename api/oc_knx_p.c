/*
 // Copyright (c) 2022-2023 Cascoda Ltd
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
#include "api/oc_knx_p.h"
#include "api/oc_knx_fp.h"
#include "api/oc_knx_helpers.h"

#include "oc_core_res.h"
#include "oc_discovery.h"
#include <stdio.h>


 // add datapoint to response and return true if at least one was added
static bool oc_add_data_points_to_response(oc_request_t* request, const oc_resource_t* resource,
                                           size_t device_index, size_t* response_length,
                                           const int page_size)
{
  (void) request;
  int matches = 0;

  for (; resource && matches < page_size; resource = resource->next)
  {
    if (resource->device != device_index)
    {
      continue;
    }
    oc_add_resource_to_wk(resource, request, device_index, response_length, true);
    matches++;
  }

  return matches > 0 ? true : false;
}

/*
 * return list of parameter handlers
 */
static void oc_core_p_get_handler(oc_request_t* request, const oc_interface_mask_t iface_mask, const void* data)
{
  (void) data;
  (void) iface_mask;

  size_t response_length = 0;             // response payload len
  bool more_request_needed = false;       // if more requests (pages) are needed to get the full list
  bool ps_exists;                         // will be initialized in function in anx case
  bool total_exists;                      // will be initialized in function in anx case
  int total = 0;                          // total resources found that matches the request pattern (maybe cut if it does not fit to a page)
  int first_entry = 0;                    // first entry number of a resource that will be placed on a page
  int query_pn = -1;                      // page number (page size as request parameter is not used)

  PRINT("oc_core_p_get_handler - start");

  if (!oc_accept_header_is_ok(request, APPLICATION_LINK_FORMAT) )
  {
    request->response->response_buffer->code = oc_status_code(OC_STATUS_BAD_REQUEST);
    return;
  }

  // get from the request the addressed device as index
  const size_t device_index = request->resource->device;
  const oc_resource_t* my_p = oc_ri_get_app_resources();

  // calculate total properties
  for (; my_p; my_p = my_p->next)
  {
    if (my_p->device != device_index)
    {
      continue;
    }
    if (oc_string(my_p->uri) != NULL)
    {
      total++;
    }
  }

  // handle query parameters l=ps and/or l=total
  const int l_exist = check_if_query_l_exist(request, &ps_exists, &total_exists);
  if (l_exist == 1)
  {
    response_length = oc_frame_query_l(oc_string(request->resource->uri), ps_exists, PAGE_SIZE, total_exists, total);
    oc_send_linkformat_response(request, OC_STATUS_OK, response_length);
    return;
  }
  if (l_exist == -1)
  {
    oc_send_response_no_format(request, OC_STATUS_NOT_FOUND);
    return;
  }
  if (l_exist == -2)
  {
    oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  my_p = oc_ri_get_app_resources();
  // handle query with page number (pn)
  if (check_if_query_pn_exist(request, &query_pn, NULL))
  {
    first_entry = query_pn * PAGE_SIZE;

    // check if the requested page would carry at least one resource
    // e.g; total=10, page=5 -> no data on page 5 
    if (first_entry >= total)
    {
      oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
      return;
    }

    // skip all endpoints 'before' the requested page# and return the next one
    for (int i = 0; i < first_entry; i++)
    {
      my_p = my_p->next;
    }
  }

  // check if found entries fits in a single page of n ... n+page size, cut data to page 'n'
  // e.g; first=0, total=300 -> 'more response needed' marker = true
  if (total > first_entry + PAGE_SIZE)
  {
    more_request_needed = true;
  }

  const bool at_least_one_added = oc_add_data_points_to_response(request, my_p, device_index, &response_length, PAGE_SIZE);
  if (at_least_one_added)
  {
    // add only a page hint if at least one response entry is in
    if (more_request_needed)
    {
      // no page # was in the request next page is 1 or #+1
      const int next_page_num = query_pn > -1 ? query_pn + 1 : 1;
      response_length += add_next_page_indicator(oc_string(request->resource->uri), next_page_num);
    }
    oc_send_linkformat_response(request, OC_STATUS_OK, response_length);
  }
  else
  {
    // no reference found for the request pattern
    oc_send_response_no_format(request, OC_STATUS_INTERNAL_SERVER_ERROR);
  }

  PRINT("oc_core_p_get_handler - end");
}

/*
 * handles the /p put command, e.g. list of parameters
 */
static void oc_core_p_post_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void) data;
  bool error = false;

  PRINT("oc_core_p_post_handler - start");

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR) )
  {
    request->response->response_buffer->code = oc_status_code(OC_STATUS_BAD_REQUEST);
    return;
  }

  // get from the request the addressed device as index
  size_t device_index = request->resource->device;

  // check if the url is implemented on the device
  oc_rep_t* rep = request->request_payload;
  while (rep != NULL)
  {
    if (rep->type == OC_REP_OBJECT)
    {

      const oc_rep_t* entry = rep->value.object;
      while (entry != NULL)
      {
        // href = CBOR KEY 11, value = string 
        if ((entry->iname == 11) && (entry->type == OC_REP_STRING))
        {
          if (oc_belongs_href_to_resource(entry->value.string, false, device_index) == false)
          {
            error = true;
            OC_ERR("href '%.*s' does not belong to device",
                   (int) oc_string_len(entry->value.string),
                   oc_string_checked(entry->value.string));
          }
        }
        entry = entry->next;
      }
    }
    rep = rep->next;
  }

  if (error)
  {
    PRINT("oc_core_p_post_handler - end");
    oc_send_response_no_format(request, OC_STATUS_INTERNAL_SERVER_ERROR);
    return;
  }

  rep = request->request_payload;

  while (rep != NULL)
  {
    if (rep->type == OC_REP_OBJECT)
    {
      oc_rep_t* entry = rep->value.object;
      const oc_string_t* myurl = NULL;
      oc_rep_t* value = NULL;

      while (entry != NULL)
      {
        // href
        if ((entry->iname == 11) && (entry->type == OC_REP_STRING))
        {
          myurl = &entry->value.string;
        }

        // value
        if (entry->iname == 1)
        {
          value = entry;
        }

        if (value && myurl)
        {
          // do the post

          oc_request_t new_request = { 0 };
          oc_response_buffer_t response_buffer = { 0 };
          oc_response_t response_obj = { 0 };

          oc_ri_new_request_from_request(&new_request, request, &response_buffer, &response_obj);

          new_request.request_payload = value;
          new_request.uri_path = oc_string(*myurl);
          new_request.uri_path_len = oc_string_len(*myurl);

          const oc_resource_t* my_resource = oc_ri_get_app_resource_by_uri(oc_string(*myurl), oc_string_len(*myurl), device_index);

          if (my_resource)
          {
            // changing from POST to PUT; POST is not mandatory for parameters & datapoints
            if (my_resource->put_handler.cb)
            {
              // use new request (not received one with POST)
              // user data are not possible for /p EP 
              my_resource->put_handler.cb(&new_request, iface_mask, NULL);
            }
          }
        }
        entry = entry->next;
      }
    }
    rep = rep->next;
  }
  oc_knx_increase_fingerprint();
  oc_send_response_no_format(request, OC_STATUS_CHANGED);
  PRINT("oc_core_p_post_handler - end");
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(knx_p, knx_f, 0, "/p",
                                     OC_IF_LI | OC_IF_D | OC_IF_C | OC_IF_B,
                                     APPLICATION_LINK_FORMAT, OC_UNDISCOVERABLE,
                                     oc_core_p_get_handler, 0,
                                     oc_core_p_post_handler, 0, NULL,
                                     OC_SIZE_MANY(1), "urn:knx:fb.0");

void oc_create_p_resource(int resource_idx, size_t device)
{
  OC_DBG("oc_create_p_resource");
  // note that this resource is listed in /.well-known/core so it should have
  // the full rt with urn:knx prefix
  oc_core_populate_resource(resource_idx, device, "/p",
                            OC_IF_LI | OC_IF_D | OC_IF_C | OC_IF_B,
                            APPLICATION_LINK_FORMAT, 0, oc_core_p_get_handler,
                            0, oc_core_p_post_handler, 0, 1, "urn:knx:fb.0");
}

void oc_create_knx_p_resources(size_t device_index)
{
  OC_DBG("oc_create_knx_p_resources");

  if (device_index == 0)
  {
    OC_DBG("device 0: KNX parameter resources created statically");
    return;
  }

  oc_create_p_resource(OC_KNX_P, device_index);
}
