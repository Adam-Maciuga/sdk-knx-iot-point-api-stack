/*
// Copyright (c) 2016 Intel Corporation
// Copyright (c) 2022 Cascoda Ltd.
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

#include "messaging/coap/engine.h"
#include "messaging/coap/oc_coap.h"
#include "messaging/coap/separate.h"
#include "oc_api.h"

#ifdef OC_DYNAMIC_ALLOCATION
#include <stdlib.h>
#endif 

#include "oc_core_res.h"

static size_t query_iterator;

int
oc_get_query_value(oc_request_t* request, const char* key, char** value)
{
	if (!request)
		return -1;
	return oc_ri_get_query_value(request->query, request->query_len, key, value);
}

int
oc_query_value_exists(oc_request_t* request, const char* key)
{
	if (!request)
		return -1;
	return oc_ri_query_exists(request->query, request->query_len, key);
}

bool
oc_query_values_available(oc_request_t* request)
{
	if (!request)
		return false;
	if (request->query_len > 0)
	{
		return true;
	}
	return false;
}

void oc_prepare_cbor_response(oc_request_t* request, oc_status_t response_code)
{
	if (request && request->response && request->response->response_buffer)
	{
		const int length = oc_rep_get_encoded_payload_size();
		if (length == 0)
		{
			// same as 'oc_prepare_no_format_response_no_payload'
		  request->response->response_buffer->content_format = CONTENT_NONE;
		}
		else
		{
			request->response->response_buffer->content_format = APPLICATION_CBOR;
		}
		request->response->response_buffer->response_length = length;
		request->response->response_buffer->code = oc_status_code(response_code);
	}
}

void oc_prepare_json_response(oc_request_t* request, oc_status_t response_code)
{
	if (request && request->response && request->response->response_buffer)
	{
		const int length = oc_rep_get_encoded_payload_size();
		if (length == 0)
		{
			// same as 'oc_prepare_no_format_response_no_payload'
			request->response->response_buffer->content_format = CONTENT_NONE;
		}
		else
		{
			request->response->response_buffer->content_format = APPLICATION_JSON;
		}
		request->response->response_buffer->response_length = length;
		request->response->response_buffer->code = oc_status_code(response_code);
	}
}

void oc_prepare_linkformat_response(oc_request_t* request, oc_status_t response_code, size_t response_length)
{
	if (request && request->response && request->response->response_buffer)
	{
		request->response->response_buffer->content_format = APPLICATION_LINK_FORMAT;
		request->response->response_buffer->response_length = response_length;
		request->response->response_buffer->code = oc_status_code(response_code);
	}
}

void oc_prepare_no_format_response_no_payload(oc_request_t* request, oc_status_t response_code)
{
	// note that on a not present response and/or buffer the code below is skipped,
	// such as on an (internal) PUT which does not request a response with a payload
  if (request && request->response && request->response->response_buffer)
	{
		request->response->response_buffer->content_format = CONTENT_NONE;
		request->response->response_buffer->response_length = 0;
		request->response->response_buffer->code = oc_status_code(response_code);
	}
}

void oc_ignore_request(oc_request_t* request)
{
	request->response->response_buffer->code = OC_IGNORE;
}

void oc_set_delayed_callback(void* cb_data, oc_trigger_t callback, uint16_t seconds)
{
	oc_ri_add_timed_event_callback_seconds(cb_data, callback, seconds);
}

void oc_set_delayed_callback_ms(void* cb_data, oc_trigger_t callback, uint16_t miliseconds)
{
	oc_ri_add_timed_event_callback_ticks(cb_data, callback, miliseconds);
}

void oc_remove_delayed_callback(void* cb_data, oc_trigger_t callback)
{
	oc_ri_remove_timed_event_callback(cb_data, callback);
}

void oc_init_query_iterator(void)
{
	query_iterator = 0;
}

int oc_iterate_query(oc_request_t* request, char** key, size_t* key_len, char** value, size_t* value_len)
{
	query_iterator++;
	return oc_ri_get_query_nth_key_value(request->query, request->query_len, key, key_len, value, value_len, query_iterator);
}

bool oc_iterate_query_get_values(oc_request_t* request, const char* key, char** value, int* value_len)
{
	char* current_key = 0;
	size_t key_len = 0, v_len;
	int pos;

	do
	{
		pos = oc_iterate_query(request, &current_key, &key_len, value, &v_len);
		*value_len = (int) v_len;
		if (pos != -1 && strlen(key) == key_len && memcmp(key, current_key, key_len) == 0)
		{
			goto more_or_done;
		}
	}
	while (pos != -1);

	// nothing found, so invalidate len 
	*value_len = -1;

more_or_done:
	if (pos == -1 || (size_t) pos >= request->query_len)
	{
		// no query parameters at all OR scanned up to the last query parameter but no 'hit'
	  return false;
	}
	return true;
}

#ifdef OC_SERVER

oc_resource_t* oc_new_resource(char* resource_path, uint8_t num_resource_types)
{
	oc_resource_t* resource = NULL;

  if (strlen(resource_path) < OC_MAX_URL_LENGTH)
	{
		// allocate resource HEAP, content is cleared
    resource = oc_ri_alloc_resource();
    // allocate resource runtime modifiable data, content is cleared
		oc_resource_data_t* data = oc_ri_alloc_resource_data();

		if (resource && data)
		{
			// uri (href), note that this assigns - with oc_string_t type - an already - by application - allocated resource
			oc_check_uri(resource_path);                        
			resource->uri.next = NULL;
			resource->uri.ptr = resource_path;
			resource->uri.size = strlen(resource_path) + 1; // include null terminator in size

			// types (allocates only the array , types will be assigned later by oc_resource_bind_resource_type)
			oc_new_string_array(&resource->types, num_resource_types);

			// properties
			resource->properties = OC_DISCOVERABLE;

		  #ifdef OC_OSCORE
			// each new (app) resource is secured
			resource->properties |= OC_SECURE;
			#endif 

			// callback handler/ acl scope and interfaces = default
      resource->get_handler.cb = NULL;
			resource->get_handler.acl_scope_mask = OC_ACL_NONE;
			resource->get_handler.interface_mask = OC_IF_NONE;

			resource->put_handler.cb = NULL;
		  resource->put_handler.acl_scope_mask = OC_ACL_NONE;
			resource->put_handler.interface_mask = OC_IF_NONE;

			resource->post_handler.cb = NULL;
		  resource->post_handler.acl_scope_mask = OC_ACL_NONE;
			resource->post_handler.interface_mask = OC_IF_NONE;

			resource->delete_handler.cb = NULL;
		  resource->delete_handler.acl_scope_mask = OC_ACL_NONE;
			resource->delete_handler.interface_mask = OC_IF_NONE;

			/*
			  observe + functional block instance + is_const are '0', cleared by (c)alloc
			*/

			// resource->is_const = false; 
      // resource->observe_period_seconds = 0;
      // resource->fb_data = 0;

			// runtime modifiable data
			resource->runtime_data = data;
			resource->runtime_data->num_observers = 0;
		}
	}
	else
	{
		// returns NULL in release build, needs to be checked/caught by the caller
	  OC_ERR("resource path longer than 30 bytes: %d", (int) strlen(resource_path));
	}

	return resource;
}

void oc_resource_bind_resource_type(oc_resource_t* resource, const char* type)
{
	if (resource == NULL)
	{
		OC_ERR("resource is NULL");
		return;
	}
	if (resource->is_const)
	{
		OC_ERR("resource data is const");
		return;
	}
	oc_string_array_add_item(resource->types, type);
}

void oc_resource_bind_dpt(oc_resource_t* resource, const char* dpt)
{
	if (resource == NULL)
	{
		OC_ERR("resource is NULL");
		return;
	}
	if (resource->is_const)
	{
		OC_ERR("resource data is const");
		return;
	}
	oc_free_string(&resource->dpt);
	
	if (dpt)
	{
		oc_new_string(&resource->dpt, dpt, strlen(dpt));
	}
}

void oc_resource_bind_content_type(oc_resource_t* resource, 
																	 oc_content_format_t content_type_man, 
																	 oc_content_format_t content_type_opt)
{
	if (resource == NULL)
	{
		OC_ERR("resource is NULL");
		return;
	}
	if (resource->is_const)
	{
    OC_DBG("resource data is const");
		return;
	}
	resource->content_type[0] = content_type_man;
	resource->content_type[1] = content_type_opt;

}

#ifdef OC_SECURITY
void
oc_resource_make_public(oc_resource_t* resource)
{
	resource->properties &= ~OC_SECURE;
}
#endif 

void oc_resource_set_discoverable(oc_resource_t* resource, bool state)
{
	if (resource == NULL)
	{
		OC_ERR("resource is NULL");
		return;
	}
	if (resource->is_const)
	{
		OC_ERR("resource data is const");
		return;
	}

	if (state)
		resource->properties |= OC_DISCOVERABLE;
	else
		resource->properties &=  ~OC_DISCOVERABLE;
}

void oc_resource_set_observable(oc_resource_t* resource, bool state)
{
	if (resource == NULL)
	{
		OC_ERR("resource is NULL");
		return;
	}
	if (resource->is_const)
	{
		OC_ERR("resource data is const");
		return;
	}

	if (state)
		resource->properties |= OC_OBSERVABLE;
	else
		resource->properties &= ~(OC_OBSERVABLE | OC_PERIODIC);
}

void oc_resource_set_periodic_observable(oc_resource_t* resource, uint16_t seconds)
{
	if (resource == NULL)
	{
		OC_ERR("oc_resource_set_periodic_observable: resource is NULL");
		return;
	}
	if (resource->is_const)
	{
		OC_ERR("oc_resource_set_periodic_observable: resource data is const");
		return;
	}

	resource->properties |= OC_OBSERVABLE | OC_PERIODIC;
	resource->observe_period_seconds = seconds;
}

void oc_resource_set_functional_block_data(oc_resource_t* resource, uint16_t fb_number, uint8_t fb_instance, uint8_t fb_number_datapoints)
{
	if (resource == NULL)
	{
		OC_ERR("oc_resource_set_function_block_instance: resource is NULL");
		return;
	}
	if (resource->is_const)
	{
		OC_ERR("oc_resource_set_function_block_instance: resource data is const");
		return;
	}
  resource->fb_data = (fb_number << 16) + (fb_instance << 8) + fb_number_datapoints;
}

void oc_resource_set_properties_cbs(oc_resource_t* resource,
																		oc_get_properties_cb_t get_properties,
																		void* get_props_user_data,
																		oc_set_properties_cb_t set_properties,
																		void* set_props_user_data)
{
	if (resource == NULL)
	{
		OC_ERR("oc_resource_set_properties_cbs: resource is NULL");
		return;
	}
	if (resource->is_const)
	{
		OC_ERR("oc_resource_set_properties_cbs: resource data is const");
		return;
	}

	resource->get_properties.cb.get_props = get_properties;
	resource->get_properties.user_data = get_props_user_data;
	resource->set_properties.cb.set_props = set_properties;
	resource->set_properties.user_data = set_props_user_data;
}

void oc_resource_set_request_handler(oc_resource_t* resource,
																		 oc_method_t method,
																		 oc_request_callback_t callback,
																		 void* user_data,
																		 oc_acl_mask_t scopes,
																		 oc_interface_mask_t interfaces)
{
	// used to create a copy of the resource pointer 
	oc_request_handler_t* handler = NULL;

	if (resource == NULL)
	{
		OC_ERR("oc_resource_set_request_handler: resource is NULL");
		return;
	}
	if (resource->is_const)
	{
		OC_ERR("oc_resource_set_request_handler: resource data is const");
		return;
	}

	switch (method)
	{
		case OC_GET:
			handler = &resource->get_handler;
			break;
		case OC_POST:
			handler = &resource->post_handler;
			break;
		case OC_PUT:
			handler = &resource->put_handler;
			break;
		case OC_DELETE:
			handler = &resource->delete_handler;
			break;
		default:  // skip FETCH method for now 
			break;
	}

	if (handler)
	{
		handler->cb = callback;
		handler->user_data = user_data;
		handler->acl_scope_mask |= scopes;
		handler->interface_mask |= interfaces;
	}
}

bool oc_resource_get_all_interfaces_for_a_resource(const oc_resource_t* resource, oc_interface_mask_t* interfaces)
{
	bool at_least_one_handler_defined = false;

	if (resource == NULL)
	{
		OC_ERR("get acl scope from resource: resource is NULL");
		return false;
	}

	// GET defined
	if (resource->get_handler.cb)
	{
    at_least_one_handler_defined = true;
	  *interfaces|= resource->get_handler.interface_mask;
	}

	// PUT defined
  if (resource->put_handler.cb)
  {
    at_least_one_handler_defined = true;
    *interfaces |= resource->put_handler.interface_mask;
  }

	// POST defined
  if (resource->post_handler.cb)
  {
    at_least_one_handler_defined = true;
    *interfaces |= resource->post_handler.interface_mask;
  }

	// DELETE defined
  if (resource->delete_handler.cb)
  {
    at_least_one_handler_defined = true;
    *interfaces |= resource->delete_handler.interface_mask;
  }

	if (at_least_one_handler_defined)
	{
    OC_INF("at least one resource handler defined for this resource");
	  return true;
	}

	OC_INF("no resource handler defined for this resource");
	return false;
}

bool oc_resource_get_acl_for_method(const oc_resource_t* resource, oc_method_t method, oc_acl_mask_t* scopes)
{
  // used to create a copy of the resource pointer
  const oc_request_handler_t* handler = NULL;

  if (resource == NULL)
  {
    OC_ERR("get acl scope from resource: resource is NULL");
    return false;
  }

  switch (method)
  {
  case OC_GET:
    handler = &resource->get_handler;
    break;

  case OC_POST:
    handler = &resource->post_handler;
    break;

    case OC_PUT:
    handler = &resource->put_handler;
    break;

    case OC_DELETE:
    handler = &resource->delete_handler;
    break;

		// skip FETCH method for now
    default: 
    break;
  }

  if (handler)
  {
    *scopes = handler->acl_scope_mask;
    return true;
  }

  OC_INF("resource handler not defined for this method");
  return false;
}

bool oc_add_resource(oc_resource_t* resource)
{
	return oc_ri_add_resource(resource);
}

static oc_event_callback_retval_t oc_delayed_delete_resource_cb(void* data)
{
	oc_resource_t* resource = data;
  oc_ri_delete_resource(resource);
	return OC_EVENT_DONE;
}

void oc_delayed_delete_resource(oc_resource_t* resource)
{
	oc_set_delayed_callback(resource, oc_delayed_delete_resource_cb, 0);
}

void oc_indicate_separate_response(oc_request_t* request, oc_separate_response_t* response)
{
	request->response->separate_response = response;
	oc_prepare_cbor_response(request, OC_STATUS_OK);
}

void oc_set_separate_response_buffer(oc_separate_response_t* handle)
{
	coap_separate_t* cur = oc_list_head(handle->requests);
	handle->response_state = oc_blockwise_alloc_response_buffer(
		oc_string(cur->uri), oc_string_len(cur->uri), &cur->endpoint, cur->method,
		OC_BLOCKWISE_SERVER);
	#ifdef OC_BLOCK_WISE
	oc_rep_new(handle->response_state->buffer, OC_MAX_APP_DATA_SIZE);
	#else  
	oc_rep_new(handle->buffer, OC_BLOCK_SIZE);
	#endif 
}

static void oc_send_separate_response_with_length(oc_separate_response_t* handle, oc_status_t response_code, size_t length)
{
	oc_response_buffer_t response_buffer;

	response_buffer.buffer = handle->response_state->buffer;
	response_buffer.response_length = length;
	response_buffer.code = oc_status_code(response_code);
	response_buffer.content_format = length > 0 ? APPLICATION_CBOR : CONTENT_NONE;

	coap_separate_t* cur = oc_list_head(handle->requests);

	while (cur)
	{
		// get next
	  coap_separate_t* next = cur->next;

	  if (cur->observe < 3)
		{
			// not more than 3 observers per endpoint at a time
	    coap_transaction_t* t = coap_new_transaction(coap_get_next_mid(), cur->token, cur->token_len, &cur->endpoint);
			if (t)
			{
				coap_packet_t response[1];
				coap_separate_resume(response, cur, (uint8_t) oc_status_code(response_code), t->mid);
				coap_set_header_content_format(response, response_buffer.content_format);

				#ifdef OC_BLOCK_WISE
				oc_blockwise_state_t* response_state = NULL;
				  #ifdef OC_TCP
				if (!(cur->endpoint.flags & TCP) &&
						response_buffer.response_length > cur->block2_size)
				{
					#else  
				if (response_buffer.response_length > cur->block2_size)
				{
					#endif 
					response_state = oc_blockwise_find_response_buffer(
						oc_string(cur->uri), oc_string_len(cur->uri), &cur->endpoint,
						cur->method, NULL, 0, OC_BLOCKWISE_SERVER);
					if (response_state)
					{
						if (response_state->payload_size ==
								response_state->next_block_offset)
						{
							oc_blockwise_free_response_buffer(response_state);
							response_state = NULL;
						}
						else
						{
							goto next_separate_request;
						}
					}
					response_state = oc_blockwise_alloc_response_buffer(
						oc_string(cur->uri), oc_string_len(cur->uri), &cur->endpoint,
						cur->method, OC_BLOCKWISE_SERVER);
					if (!response_state)
					{
						goto next_separate_request;
					}

					memcpy(response_state->buffer, response_buffer.buffer,
								 response_buffer.response_length);
					response_state->payload_size =
						(uint32_t) response_buffer.response_length;

					uint32_t payload_size = 0;
					const void* payload = oc_blockwise_dispatch_block(
						response_state, 0, cur->block2_size, &payload_size);
					if (payload)
					{
						coap_set_payload(response, payload, payload_size);
						coap_set_header_block2(response, 0, 1, cur->block2_size);
						coap_set_header_size2(response, response_state->payload_size);
						oc_blockwise_response_state_t* bwt_res_state =
							(oc_blockwise_response_state_t*) response_state;
						coap_set_header_etag(response, bwt_res_state->etag, COAP_ETAG_LEN);
					}
				}
				else
					#endif 
					if (response_buffer.response_length > 0)
					{
						coap_set_payload(response, handle->response_state->buffer,
														 response_buffer.response_length);
					}
				coap_set_status_code(response, response_buffer.code);
				t->message->length = coap_serialize_message(response, t->message->data);
				if (t->message->length > 0)
				{
					coap_send_transaction(t);
				}
				else
				{
					coap_clear_transaction(t);
				}
			}
		}
		else
		{
			const oc_resource_t* resource = oc_ri_get_app_resource_by_resource_path(
        oc_string(cur->uri), oc_string_len(cur->uri));
			if (resource)
			{
				coap_notify_observers(resource, &response_buffer, &cur->endpoint);
			}
		}
		#ifdef OC_BLOCK_WISE
		next_separate_request :
		#endif 
		coap_separate_clear(handle, cur);

		// restore next
	  cur = next;
	}
	handle->active = 0;
	oc_blockwise_free_response_buffer(handle->response_state);
}

void oc_send_separate_response(oc_separate_response_t * handle, oc_status_t response_code)
{
	size_t length;
	if (handle->response_state->payload_size != 0)
		length = handle->response_state->payload_size;
	else
		length = oc_rep_get_encoded_payload_size();

	oc_send_separate_response_with_length(handle, response_code, length);
}

void oc_send_empty_separate_response(oc_separate_response_t * handle, oc_status_t response_code)
{
	oc_send_separate_response_with_length(handle, response_code, 0);
}

int oc_notify_observers(const oc_resource_t * resource)
{
	return coap_notify_observers(resource, NULL, NULL);
}
#endif 
