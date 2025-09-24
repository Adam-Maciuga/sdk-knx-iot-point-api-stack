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

#include "api/oc_knx_p.h"
#include <stdio.h>
#include "api/oc_knx_fp.h"
#include "api/oc_knx_helpers.h"
#include "oc_api.h"
#include "oc_core_res.h"
#include "oc_discovery.h"

// add application datapoint's to the response and return true if at least one was added
static bool oc_was_adding_data_points_to_response(oc_request_t* request, const oc_resource_t* resource,
                                                  size_t* response_length, const int page_size)
{
  int matches = 0;

  for (; resource && matches < page_size; resource = resource->next)
  {
    if (resource->device != 0)
    {
      continue;
    }

    // called from GET /p handler so always truncate resources URN's
    oc_add_resource_to_response_payload(resource, response_length, true);
    matches++;
  }

  return matches > 0 ? true : false;
}

static void oc_core_p_get_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  size_t response_length = 0;
  int total = 0; // total entries of this resource
  int query_pn = PAGE_NUMBER;
  int query_ps = PAGE_SIZE;

  PRINT("oc_core_p_get_handler - start");

  if (!oc_accept_header_is_ok(request, APPLICATION_LINK_FORMAT))
  {
    return;
  }

  // calculate total properties
  const oc_resource_t* my_p = oc_ri_get_app_resources();
  for (; my_p; my_p = my_p->next)
  {
    if (my_p->device != 0)
    {
      continue;
    }
    if (oc_string(my_p->uri) != NULL)
    {
      total++;
    }
  }

  // handle query parameters l=ps and/or l=total
  if (query_l_was_processed(request, PAGE_SIZE, total))
    return;

  // first entry number of a resource that will be placed on a page
  const int first_entry = evaluate_query_px(request, &query_pn, &query_ps);

  // check if requested page will carry at least one resource e.g
  // - total=4, pn 5, ps 20, first entry = 100 -> no data on page 5 (all on page 0)
  // - total=4, pn 1, ps 04, first entry = 004 -> no data on page 1 (all on page 0)n page 5
  if (first_entry >= total || query_ps == 0)
  {
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
    return;
  }

  // calculate first property for the requested page
  my_p = oc_ri_get_app_resources();
  for (int i = 0; i < first_entry; i++)
  {
    my_p = my_p->next; // TODO check why correct device is not considered here (fails if > 0 device)
  }

  // entries don't fit in a single page -> more pages are needed to get the full list
  // - total=4, page number 1, page size 02, first entry = 002 -> no more data on next page
  // - total=4, page number 1, page size 01, first entry = 001 -> more data on next page
  const bool more_request_needed = total > first_entry + query_ps ? true : false;

  // add ONLY application datapoint's to response
  if (oc_was_adding_data_points_to_response(request, my_p, &response_length, query_ps))
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
    // (> 0 application) resources are mandatory, hence this can't be correct here
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_INTERNAL_SERVER_ERROR);
  }

  PRINT("oc_core_p_get_handler - end");
}

static void oc_core_p_post_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{
  (void)data;
  (void)iface_mask;

  bool error = false;

  PRINT("oc_core_p_post_handler - start");

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  // get from the request the addressed device as index


  // check first if the url is implemented on the device (performance)
  oc_rep_t* rep = request->request_payload;
  while (rep)
  {
    if (rep->type == OC_REP_OBJECT)
    {
      // scan for objects, a post may contain in the collection many items, each with many attributes
      const oc_rep_t* entry_object = rep->value.object;

      // check if 'href' is present
      while (entry_object)
      {
        // href = CBOR KEY 11, value = string = MANDATORY according to specification
        if (entry_object->iname == 11 && entry_object->type == OC_REP_STRING)
        {
          if (!oc_belongs_href_to_resource(entry_object->value.string, false))
          {
            // there is no href in all application resources that fits to the request href
            error = true;
            OC_ERR("href '%.*s' does not belong to device", 
                   (int)oc_string_len(entry_object->value.string), 
                   oc_string_checked(entry_object->value.string));
          }
        }
        entry_object = entry_object->next;
      }
    }
    rep = rep->next;
  }

  if (error)
  {
    PRINT("oc_core_p_post_handler - end");

    // no bad request since /p was ok, but not a single collection 'href'
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_INTERNAL_SERVER_ERROR);
    return;
  }

  // each application callback handler gets a new copy of the original request + new response buffer
  oc_request_t new_request = {0};
  oc_response_buffer_t response_buffer = {0};
  oc_response_t response_obj; // filled complete later on 

  // define summary callback handler status
  oc_status_t summary_handler_status = OC_STATUS_OK;

  // get back source payload start
  rep = request->request_payload;

  while (rep)
  {
    if (rep->type == OC_REP_OBJECT)
    {
      // scan for objects, a post may contain in the collection many items, each with many attributes (value, metadata)
      oc_rep_t* entry_object = rep->value.object;

      const oc_string_t* entry_url = NULL;
      oc_rep_t* entry_value = NULL;


      while (entry_object)
      {
        // href = MANDATORY
        if (entry_object->iname == 11 && entry_object->type == OC_REP_STRING)
        {
          entry_url = &entry_object->value.string;
        }

        // value
        if (entry_object->iname == 1)
        {
          entry_value = entry_object;
        }

        /*
          Do the post only if (at least) a href and value is in the request, reason: 

          - WRITING of metadata (per POST here, or PUT) is optional in the specification; this is NOT supported from the stack, hence to
            write a value in this stack version at least href + value must be present. 
          - Note, READING of metadata (per GET) is mandatory in the specification for specific types (if, rt, ...),
            this is supported from the stack (application callback handler).
        */
        if (entry_value && entry_url)
        {
          // copy all data from request to new request (performance consuming)
          // - includes also the originally called resource, maybe used in PUT handler to access interfaces or acl scopes 
          oc_ri_new_request_from_request(&new_request, request, &response_buffer, &response_obj);

          // sets the payload pointer to the collection 'item' OBJECT that includes the value
          // used by /p and /k that calls the same application callback handlers
          new_request.request_payload = rep->value.object;

          // set src to POST p with payload; for a redirect check in application callback handles
          // 'p/' and not '/p' because the PUT for p/{property-path} starts with segment 'p/'
          new_request.uri_path = "p/";
          new_request.uri_path_len = 2; // exclude for uri path len string null termination 

          const oc_resource_t* my_resource =
            oc_ri_get_app_resource_by_resource_path(oc_string(*entry_url), oc_string_len(*entry_url));

          if (my_resource && my_resource->put_handler.cb)
          {
            // call application PUT handler
            // for /k only a POST is defined, application callback needs to end up in one (PUT) handler for /k and /p

            // user data can be NULL if not defined by application request handler
            // call application handler with own interface/ user data
            // (it makes no sense to call it with the original /p interface mask, this is a fix vale)
            my_resource->put_handler.cb(&new_request, 
                                        my_resource->put_handler.interface_mask,
                                        my_resource->put_handler.user_data);

            // collect the max 'bad' status code, usually overwritten by the callback
            collect_and_rank_status(new_request.response->response_buffer->code, &summary_handler_status);

            // create /p --> update
            oc_knx_increase_fingerprint();
          }
        }
        entry_object = entry_object->next;
      }
    }
    rep = rep->next;
  }

  oc_prepare_no_format_response_no_payload(request, summary_handler_status);
  PRINT("oc_core_p_post_handler - end");
}

// resource definition, details/comments see on 'core_resource_well_known_core_final'
extern const oc_resource_t core_resource_knx_f;
PRAGMA_IN oc_resource_data_t core_resource_knx_p_data;
const oc_resource_t core_resource_knx_p = {(oc_resource_t*)&core_resource_knx_f,
                                           0,
                                           {NULL, sizeof("/p"), "/p"},
                                           {NULL, (size_t)1 * 32, ((char[1][32]){"urn:knx:fb.0"})},
                                           {NULL, 0, NULL},
                                           {APPLICATION_LINK_FORMAT, CONTENT_NONE},
                                           OC_UNDISCOVERABLE,
                                           {oc_core_p_get_handler, NULL, OC_ACL_P | OC_ACL_D | OC_ACL_C, OC_IF_LI},
                                           {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                           {oc_core_p_post_handler, NULL, OC_ACL_C, OC_IF_C | OC_IF_B},
                                           {NULL, NULL, OC_ACL_NONE, OC_IF_NONE},
                                           {NULL, NULL},
                                           {NULL, NULL},
                                           0,
                                           0,
                                           1,
                                           &core_resource_knx_p_data};
PRAGMA_OUT

void oc_create_p_resource(int resource_idx)
{
  OC_DBG("oc_create_p_resource");
  // note that this resource is listed in /.well-known/core so it should have
  // the full rt with urn:knx prefix
  oc_core_populate_resource(resource_idx, "/p", APPLICATION_LINK_FORMAT, CONTENT_NONE, OC_DISCOVERABLE,
                            oc_core_p_get_handler, 0, oc_core_p_post_handler, 0, 1, "urn:knx:fb.0");
}