/* 
 * Copyright (c) 2022-2023 Cascoda Ltd
 * Copyright (c) 2024-2026 KNX Association
 *            
 * SPDX-License-Identifier: Apache-2.0
 */

#include "oc_api.h"
#include "oc_core_res.h"
#include "oc_helpers.h"
#include "oc_knx_client.h"
#include "oc_knx_fp.h"
#include "knx_iot_datapoint.h"

void knx_iot_register_datapoint(
        char* resource_path, char* resource_type, char* dpt,
        oc_resource_properties_t properties,
        void* user_data,
        oc_request_callback_t get_handler, oc_acl_mask_t get_handler_acl, oc_interface_mask_t get_handler_interface_mask, 
        oc_request_callback_t put_handler, oc_acl_mask_t put_handler_acl, oc_interface_mask_t put_handler_interface_mask)
{
  knx_iot_register_functional_block_datapoint(
          0, 0, 0,
          resource_path, resource_type, dpt,
          properties,
          user_data,
          get_handler, get_handler_acl, get_handler_interface_mask, 
          put_handler, put_handler_acl, put_handler_interface_mask
  );
}

void knx_iot_register_functional_block_datapoint(
        uint16_t fb_number, uint8_t fb_instance, uint8_t fb_number_of_datapoints,
        char* resource_path, char* resource_type, char* dpt,
        oc_resource_properties_t properties,
        void* user_data,
        oc_request_callback_t get_handler, oc_acl_mask_t get_handler_acl, oc_interface_mask_t get_handler_interface_mask, 
        oc_request_callback_t put_handler, oc_acl_mask_t put_handler_acl, oc_interface_mask_t put_handler_interface_mask)
{
  oc_resource_t* resource = oc_new_resource(resource_path, 1);
  oc_resource_bind_resource_type(resource, resource_type);
  oc_resource_bind_dpt(resource, dpt);
  oc_resource_bind_content_type(resource, APPLICATION_CBOR, CONTENT_NONE);

  if (fb_number_of_datapoints > 0)
  {
      oc_resource_set_functional_block_data(resource, fb_number, fb_instance, fb_number_of_datapoints);
  }

  oc_resource_set_properties(resource, properties);

  if (get_handler != NULL)
  {
      oc_resource_set_request_handler(resource, OC_GET, get_handler, user_data, get_handler_acl, get_handler_interface_mask);
  }

  if (put_handler != NULL)
  {
      oc_resource_set_request_handler(resource, OC_PUT, put_handler, user_data, put_handler_acl, put_handler_interface_mask);
  }

  oc_add_resource(resource);
}

/**
 * @brief add all short interface urn's to the 'root' object with string key 'if'
 *
 * @param resource the resource

 */
static void add_all_interface_short_urns_for_a_resource(const oc_resource_t* resource)
{ 
  // get all if's
  oc_interface_mask_t res_interfaces = OC_IF_NONE;
  oc_resource_get_all_interfaces_for_a_resource(resource, &res_interfaces);
  
  // create interface list, n elements
  const unsigned int nr_entries = oc_count_total_interfaces_in_mask(res_interfaces);
  oc_string_array_t interface_list;
  oc_new_string_array(&interface_list, nr_entries);

  // put all if's to string array
  oc_put_all_interface_short_urns_from_a_mask_in_string_array(res_interfaces, interface_list);

  // add strings by key 'if'
  oc_rep_set_string_array(root, if, interface_list);
    
  // release interface list
  oc_free_string_array(&interface_list);
}   

void knx_iot_get_handler(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data, 
        const oc_rep_value_type_t value_type, volatile void* value, 
        volatile app_datapoint_handler_flags_t* flags, char* description)
{
  (void)interfaces;
  bool error_state = true;

  PRINT("-- Begin GET at %s ", oc_string(request->resource->uri));

  if (!oc_accept_header_is_ok(request, APPLICATION_CBOR))
  {
    return;
  }

  // TODO FIXME that section was missing for the 'test_parameter', but that is not a problem, right!?
  // handle different caller sources, here included as an example to distinguish
  // the caller source (e.g.; called by '/p' or '/k')
  if (oc_is_redirected_request_from(request) == 1)
  {
    PRINT("redirected_request %.*s", (int)request->uri_path_len, request->uri_path);
  }

  const oc_device_info_t* const device = oc_core_get_device_info();

  // open CBOR
  oc_rep_begin_root_object();

  if (oc_query_value_exists(request, "m") != -1)
  {
    // ... query parameter 'm' is present, check the various values
    char* m_value;
    char* m_key;
    size_t m_value_len = oc_get_query_value(request, "m", &m_value);
    size_t m_key_len;

    const bool wildcard = strncmp(m_value, "*", m_value_len) == 0;

    PRINT("Query Parameter: %.*s", (int)m_value_len, m_value);

    oc_init_query_iterator();

    // check (0..n) query parameter
    while (oc_iterate_query(request, &m_key, &m_key_len, &m_value, &m_value_len) != -1)
    {
      // id
      if (strncmp(m_value, "id", m_value_len) == 0 || wildcard)
      {
        // knx://sn: + max len SN + uri path + \0 = ~ 65
        char serial_number[65];
        (void)snprintf(serial_number, 65, "knx://sn:%s%s", 
                       oc_string(device->serialnumber),
                       oc_string(request->resource->uri));
        oc_rep_i_set_text_string(root, 0, serial_number);
        error_state = false;
      }

      // value
      if (strncmp(m_value, "value", m_value_len) == 0 || wildcard)
      {
        switch (value_type)
        {
          case OC_REP_BOOL:
            //oc_rep_i_set_boolean(root, 1, *((bool*)value));	
            oc_rep_text_set_boolean(root, value, *((bool*)value));  // TODO FIXME why was only the bool type encoded with key as text? This should be consistent over all types.
            error_state = false;
            PRINT("get %s as %d", description ? description : "unknown", *((bool*)value));
            break;

          case OC_REP_INT:
            // see 'Callback Notes'
            oc_rep_i_set_int(root, 1, *((int*)value));
            error_state = false;
            PRINT("get %s as %d", description ? description : "unknown", *((int*)value));
            break;

          case OC_REP_FLOAT:
            oc_rep_i_set_float(root, 1, *((float*)value));
            error_state = false;
            PRINT("get %s as %f", description ? description : "unknown", *((float*)value));
            break;
         
/* TODO implement this
          case OC_REP_STRING:
            // Note:
            // Caller needs to provided a char* pointer with enough space.
            if (rep->value.string)
            {
              strcpy((char*)value, rep->value.string);
              error_state = false;
              PRINT("get %s as \"%s\"", description ? description : "unknown", (char*)value);
            }
            break;
*/
          default:
            /* unknown type, error_state remains true */
            // TODO add missing valid types
            break;
        }
      }

      // resource types (rt)
      if (strncmp(m_value, "rt", m_value_len) == 0 || wildcard)
      {
        // use the first type, skip urn:knx (=7), if more types are used
        // - add a next in the data structure
        // - add an extra line
        const char* first_type = oc_string_array_get_item(request->resource->types, 0);
        oc_rep_text_set_text_string(root, rt, first_type + 7);
        error_state = false;
      }

      // interface (if)
      // Note:
      // Array of text strings.
      if (strncmp(m_value, "if", m_value_len) == 0 || wildcard)
      {
        add_all_interface_short_urns_for_a_resource(request->resource);
        error_state = false;
      }

      // datapoint type (dpt)
      if (strncmp(m_value, "dpt", m_value_len) == 0 || wildcard)
      {
        oc_rep_text_set_text_string(root, dpt, oc_string(request->resource->dpt));
        error_state = false;
      }

      // group address (ga)
      if (strncmp(m_value, "ga", m_value_len) == 0 || wildcard)
      {
        const int index = oc_core_find_first_group_object_table_index_from_href(oc_string(request->resource->uri));
        if (index > -1)
        {
          oc_group_object_table_t* got_table_entry = oc_core_get_group_object_table_entry(index);
          if (got_table_entry)
          {
            oc_rep_set_int_array(root, ga, got_table_entry->ga, got_table_entry->ga_len);
          }
        }

        error_state = false;
      }

      // href (MAY omit in the response, here not for a '*')
      if (strncmp(m_value, "href", m_value_len) == 0 || wildcard)
      {
        oc_rep_text_set_text_string(root, href, oc_string(request->resource->uri));
        error_state = false;
      }
    }
  }
  else
  { 
    // ... no query parameter 'm' present at all, set value for the GET

    // see 'Callback Notes'
    switch (value_type)
    {
      case OC_REP_BOOL:
        oc_rep_i_set_boolean(root, 1, *((bool*)value));
        error_state = false;
        PRINT("get %s as %d", description ? description : "unknown", *((bool*)value));
        break;

      case OC_REP_INT:
        // see 'Callback Notes'
        oc_rep_i_set_int(root, 1, *((int*)value));
        error_state = false;
        PRINT("get %s as %d", description ? description : "unknown", *((int*)value));
        break;

      case OC_REP_FLOAT:
        oc_rep_i_set_float(root, 1, *((float*)value));
        error_state = false;
        PRINT("get %s as %f", description ? description : "unknown", *((float*)value));
        break;
     
/* TODO implement this
      case OC_REP_STRING:
        // Note:
        // Caller needs to provided a char* pointer with enough space.
        if (rep->value.string)
        {
          strcpy((char*)value, rep->value.string);
          error_state = false;
          PRINT("get %s as \"%s\"", description ? description : "unknown", (char*)value);
        }
        break;
*/
      default:
        /* unknown type, error_state remains true */
        // TODO add missing valid types
        break;
    }
  }

  // close CBOR
  oc_rep_end_root_object();

  // check CBOR encoding errors
  if (g_err != CborNoError)
  {
    error_state = true;
  }

  PRINT("CBOR encoder size %d", oc_rep_get_encoded_payload_size());

  // set datapoint flags
  if (flags)
  {
    *flags = (error_state ? DPH_ERROR : DPH_NO_ERROR) + DPH_GET + DPH_NEW_EVENT;
  }

  if (error_state)
  {
    // wrong device, CBOR error or unknown 'm' query parameter key values
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_NOT_FOUND);
  }
  else
  {
    oc_prepare_cbor_response(request, OC_STATUS_OK);
  }

  PRINT("-- End GET at %s ", oc_string(request->resource->uri));
}

void knx_iot_put_handler(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data, 
        const oc_rep_value_type_t value_type, volatile void* value, knx_iot_put_callback_t callback, 
        volatile app_datapoint_handler_flags_t* flags, const char* description)
{
  bool error_state = true;

  // get interfaces of the resource PUT method ...
  bool is_input_datapoint = interfaces & OC_IF_I;
  bool is_parameter = interfaces & OC_IF_P;

  // sets the pointer to the (/k or /p) handed over 'value' object, note it may be also NULL
  const oc_rep_t* rep = request->request_payload;

  PRINT("-- Begin PUT at %s ", oc_string(request->resource->uri));

  // TODO that section was missing for the 'test_parameter', but that is not a problem, right!?
  // handle different caller sources, here included as an example to distinguish
  // the caller source (e.g.; called by '/p' or '/k' s-mode message EP)
  if (oc_is_redirected_request_from(request) == 1)
  {
    // caller /p --> always allow to write (see 'Callback Notes' above)
    is_input_datapoint = true;
    PRINT("redirected_request %.*s", (int)request->uri_path_len, request->uri_path);
  }

  // loop over object
  while (rep)
  {
    // Note:
    // A faulty construct will not be handled.
    // e.g.:
    // '{..., 1: true, 1: false}' or
    // '{..., 1: 2, 1: 5}'
    if (rep->iname == 1 && rep->type == value_type)
    {
      if (! (is_input_datapoint || is_parameter))
      {
        // see 'Callback Notes'
        oc_prepare_no_format_response_no_payload(request, OC_STATUS_METHOD_NOT_ALLOWED);
        return;
      }

      switch (value_type)
      {
        case OC_REP_BOOL:
          *((bool*)value) = rep->value.boolean;
          error_state = false;
          PRINT("set %s to %d", description ? description : "unknown", *((bool*)value));
          break;

        case OC_REP_INT:
          // see 'Callback Notes'
          *((int*)value) = (int)rep->value.integer;
          error_state = false;
          PRINT("set %s to %d", description ? description : "unknown", *((int*)value));
          break;

        case OC_REP_FLOAT:
          *((float*)value) = rep->value.float_p;
          error_state = false;
          PRINT("set %s to %f", description ? description : "unknown", *((float*)value));
          break;
/* TODO FIXME
       case OC_REP_STRING:
          // Note:
          // Caller needs to provided a char* pointer with enough space.
          if (rep->value.string)
          {
            strcpy((char*)value, rep->value.string);
            error_state = false;
            PRINT("set %s to \"%s\"", description ? description : "unknown", (char*)value);
          }
          break;
*/      
        default:
          /* unknown type, error_state remains true */
          // TODO add missing valid types
          break;
      }

      break;
    }

    rep = rep->next;
  }

  // set datapoint flags
  if (flags)
  {
    *flags = (error_state ? DPH_ERROR : DPH_NO_ERROR) + DPH_PUT + DPH_NEW_EVENT;
  }

  // call the application defined callback if set and no error
  if (!error_state && callback)
  { 
    callback(user_data, value_type, value, description);
  }

  // inform the stack on status
  oc_prepare_no_format_response_no_payload(request, error_state ? OC_STATUS_BAD_REQUEST : OC_STATUS_CHANGED);

  PRINT("-- End PUT at %s ", oc_string(request->resource->uri));
}
