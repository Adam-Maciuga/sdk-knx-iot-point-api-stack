/*    
 * Copyright (c) 2016 Intel Corporation
 * Copyright (c) 2024-2025 KNX Association
 *  
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2013, Institute for Pervasive Computing, ETH Zurich
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 */

#include "oc_config.h"
#include "separate.h"

#ifdef OC_SERVER

#include "observe.h"
#include "util/oc_memb.h"
#include <stdio.h>
#include <string.h>

#include "oc_buffer.h"
//#ifdef OC_SECURITY	// TODO FIXME, what do we need when of those?
//#include "security/oc_acl_internal.h"
//#include "security/oc_pstat.h"
//#endif

#ifdef OC_BLOCK_WISE
#include "oc_blockwise.h"
#endif

#include "oc_coap.h"
#include "oc_endpoint.h"
#include "oc_rep.h"
#include "oc_ri.h"
/*-------------------*/
int32_t observe_counter = 3;
/*---------------------------------------------------------------------------*/
OC_LIST(observers_list);
OC_MEMB(observers_memb, coap_observer_t, COAP_MAX_OBSERVERS);

/*---------------------------------------------------------------------------*/
/*- Internal API ------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
static int coap_remove_observer_handle_by_uri(oc_endpoint_t *endpoint, const char *uri,
        int uri_len, oc_interface_mask_t iface_mask)
{
  int removed = 0;
  coap_observer_t *obs = (coap_observer_t *)oc_list_head(observers_list), *next;

  while (obs) {
    next = obs->next;
    if (((oc_endpoint_compare(&obs->endpoint, endpoint) == 0)) &&
        (oc_string_len(obs->url) == (size_t)uri_len &&
         memcmp(oc_string(obs->url), uri, uri_len) == 0) &&
        obs->iface_mask == iface_mask) {
      coap_remove_observer(obs);
      removed++;
      break;
    }

    obs = next;
  }

  return removed;
}
/*---------------------------------------------------------------------------*/

#ifdef OC_BLOCK_WISE
static intadd_observer(const oc_resource_t *resource, uint16_t block2_size,
             oc_endpoint_t *endpoint, const uint8_t *token, size_t token_len,
             const char *uri, size_t uri_len, oc_interface_mask_t iface_mask)
#else
static intadd_observer(const oc_resource_t *resource, oc_endpoint_t *endpoint,
             const uint8_t *token, size_t token_len, const char *uri,
             size_t uri_len, oc_interface_mask_t iface_mask)
#endif
{
  // Remove existing observe relationship, if any.
  int dup =
    coap_remove_observer_handle_by_uri(endpoint, uri, (int)uri_len, iface_mask);

  coap_observer_t *o = oc_memb_alloc(&observers_memb);

  if (o) {
    oc_new_string(&o->url, uri, uri_len);
    memcpy(&o->endpoint, endpoint, sizeof(oc_endpoint_t));
    o->token_len = (uint8_t)token_len;
    memcpy(o->token, token, token_len);
    o->last_mid = 0;
    o->iface_mask = iface_mask;
    o->obs_counter = observe_counter;
    o->resource = resource;
#ifdef OC_BLOCK_WISE
    o->block2_size = block2_size;
#endif
    resource->runtime_data->num_observers++;
#ifdef OC_DYNAMIC_ALLOCATION
    OC_DBG("Adding observer (%u) for /%s [0x%02X%02X]",
            oc_list_length(observers_list) + 1, oc_string_checked(o->url),
            o->token[0], o->token[1]);
#else
    OC_DBG("Adding observer (%u/%u) for /%s [0x%02X%02X]",
           oc_list_length(observers_list) + 1, COAP_MAX_OBSERVERS,
           oc_string_checked(o->url), o->token[0], o->token[1]);
#endif
    oc_list_add(observers_list, o);
    return dup;
  }

  OC_WRN("insufficient memory to add new observer");
  return -1;
}
/*---------------------------------------------------------------------------*/
/*- Removal -----------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
static const char *
get_iface_query(oc_interface_mask_t iface_mask)
{
  (void)iface_mask;

  return NULL;
}

void coap_remove_observer(coap_observer_t *o)
{
  OC_DBG("Removing observer for /%s [0x%02X%02X]", oc_string_checked(o->url),
          o->token[0], o->token[1]);

#ifdef OC_BLOCK_WISE
  const char *query = get_iface_query(o->iface_mask);
  oc_blockwise_state_t *response_state = oc_blockwise_find_response_buffer(
    oc_string(o->resource->uri) + 1, oc_string_len(o->resource->uri) - 1,
  &o->endpoint, OC_GET, query, (query) ? strlen(query) : 0,
    OC_BLOCKWISE_SERVER);
  if (response_state) {
    response_state->ref_count = 0;
  }
#endif
  o->resource->runtime_data->num_observers--;
  oc_free_string(&o->url);
  oc_list_remove(observers_list, o);
  oc_memb_free(&observers_memb, o);
}

void coap_free_all_observers(void)
{
  coap_observer_t *obs = (coap_observer_t *)oc_list_head(observers_list), *next;

  while (obs) {
    next = obs->next;
    coap_remove_observer(obs);
    obs = next;
  }
}

int coap_remove_observer_by_client(oc_endpoint_t *endpoint)
{
  int removed = 0;
  coap_observer_t *obs = (coap_observer_t *)oc_list_head(observers_list), *next;

  OC_DBG("Unregistering observers for client at: ");
  PRINTipaddr(*endpoint);

  while (obs) {
    next = obs->next;
    if (oc_endpoint_compare(&obs->endpoint, endpoint) == 0) {
      coap_remove_observer(obs);
      removed++;
    }
    obs = next;
  }

  OC_DBG("Removed %d observers", removed);
  return removed;
}

int coap_remove_observer_by_token(oc_endpoint_t *endpoint, uint8_t *token,
        size_t token_len)
{
  int removed = 0;
  coap_observer_t *obs = (coap_observer_t *)oc_list_head(observers_list);
  OC_DBG("Unregistering observers for request token 0x%02X%02X", token[0],
         token[1]);
  while (obs) {
    if (oc_endpoint_compare(&obs->endpoint, endpoint) == 0 &&
        obs->token_len == token_len &&
        memcmp(obs->token, token, token_len) == 0) {
      coap_remove_observer(obs);
      removed++;
      break;
    }

    obs = obs->next;
  }

  OC_DBG("Removed %d observers", removed);
  return removed;
}

int coap_remove_observer_by_mid(oc_endpoint_t *endpoint, uint16_t mid)
{
  int removed = 0;
  coap_observer_t *obs = NULL;
  OC_DBG("Unregistering observers for request MID %u", mid);

  for (obs = (coap_observer_t *)oc_list_head(observers_list); obs != NULL;
       obs = obs->next) {
    if (oc_endpoint_compare(&obs->endpoint, endpoint) == 0 &&
        obs->last_mid == mid) {
      coap_remove_observer(obs);
      removed++;
      break;
    }
  }

  OC_DBG("Removed %d observers", removed);
  return removed;
}

int coap_remove_observer_by_resource(const oc_resource_t *rsc)
{
  int removed = 0;
  coap_observer_t *obs = (coap_observer_t *)oc_list_head(observers_list), *next;

  while (obs) {
    next = obs->next;
    if ((obs->resource == rsc) &&
        (oc_string(rsc->uri) &&
         oc_string_len(obs->url) == (oc_string_len(rsc->uri) - 1) &&
         memcmp(oc_string(obs->url), oc_string(rsc->uri) + 1,
                oc_string_len(rsc->uri) - 1) == 0)) {
      coap_remove_observer(obs);
      removed++;
    }

    obs = next;
  }

  return removed;
}

/*---------------------------------------------------------------------------*/
/*- Notification ------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
#ifdef OC_SECURITY	// TODO FIXME this is NOT TCP only!
int coap_remove_observers_on_dos_change(bool reset)
{
  // Iterate over observers.
  coap_observer_t *obs = (coap_observer_t *)oc_list_head(observers_list);
  while (obs != NULL) {
    if (reset || !oc_sec_check_acl(OC_GET, obs->resource, &obs->endpoint)) {
      coap_observer_t *o = obs;
      coap_packet_t notification[1];
#ifdef OC_TCP
      if (obs->endpoint.flags & TCP) {
        coap_tcp_init_message(notification, SERVICE_UNAVAILABLE_5_03);
      } else
#endif
      {
        coap_udp_init_message(notification, COAP_TYPE_NON,
                SERVICE_UNAVAILABLE_5_03, 0);
      }

      coap_set_token(notification, obs->token, obs->token_len);
      coap_transaction_t *transaction = coap_new_transaction(
              coap_get_next_mid(), obs->token, obs->token_len, &obs->endpoint);
      if (transaction) {
        notification->mid = transaction->mid;
        transaction->message->length =
          coap_serialize_message(notification, transaction->message->data);
        if (transaction->message->length > 0) {
          coap_send_transaction(transaction);
        } else {
          coap_clear_transaction(transaction);
        }
      }

      obs = obs->next;
      coap_remove_observer(o);
      continue;
    }
    obs = obs->next;
  }
  return 0;
}
#endif

int coap_notify_observers(const oc_resource_t *resource, 
        oc_response_buffer_t *response_buf, oc_endpoint_t *endpoint)
{
  if (!resource) {
    OC_WRN("coap_notify_observers: no resource passed; returning");
    return 0;
  }

#ifdef OC_SECURITY	// TODO FIXME this is NOT TCP only!
  oc_sec_pstat_t *ps = oc_sec_get_pstat();
  if (ps->s != OC_DOS_RFNOP) {
    OC_WRN("coap_notify_observers: device not in RFNOP; skipping notification");
    return 0;
  }
#endif

  // bool resource_is_collection = false;
  coap_observer_t *obs = NULL;
  if (resource->runtime_data->num_observers > 0) {
#ifdef OC_BLOCK_WISE
    oc_blockwise_state_t *response_state = NULL;
#endif

#ifndef OC_DYNAMIC_ALLOCATION
    uint8_t buffer[OC_MAX_OBSERVE_SIZE];
#else
    uint8_t *buffer = malloc(OC_MAX_OBSERVE_SIZE);
    if (!buffer) {
      OC_WRN("coap_notify_observers: out of memory allocating buffer");
      goto leave_notify_observers;
    }
#endif

    oc_request_t request = { 0 };
    oc_response_t response;
    response.separate_response = 0;
    oc_response_buffer_t response_buffer;
    if (!response_buf && resource) {
      OC_DBG("coap_notify_observers: Issue GET request to resource %s", oc_string_checked(resource->uri));
      response_buffer.buffer = buffer;
      response_buffer.buffer_size = OC_MAX_OBSERVE_SIZE;
      response.response_buffer = &response_buffer;
      request.resource = resource;
      request.response = &response;
      request.request_payload = NULL;
      oc_rep_new(response_buffer.buffer, (int)response_buffer.buffer_size);

      resource->get_handler.cb(&request, resource->get_handler.interface_mask, 
              resource->get_handler.user_data);
      
      response_buf = &response_buffer;
      if (response_buf->code == OC_IGNORE) {
        OC_DBG("coap_notify_observers: Resource ignored request");
        goto leave_notify_observers;
      }
    }

    // Iterate over observers.
    obs = (coap_observer_t *)oc_list_head(observers_list);
    while (obs != NULL) {
      if ((obs->resource != resource) ||
              (endpoint && oc_endpoint_compare(&obs->endpoint, endpoint) != 0)) {
        obs = obs->next;
        continue;
      } // obs->resource != resource || endpoint != obs->endpoint
      // if (resource_is_collection && obs->iface_mask != OC_IF_BASELINE) {
      //  obs = obs->next;
      //  continue;
      //}
      // if (obs->iface_mask == OC_IF_STARTUP) {
      //  OC_DBG("coap_notify_observers: Skipping startup established observe");
      //  obs = obs->next;
      //  continue;
      //}
      if (response.separate_response != NULL) {
        coap_packet_t req[1];
#ifdef OC_TCP
        if (obs->endpoint.flags & TCP) {
          coap_tcp_init_message(req, COAP_GET);
        } else
#endif
        {
          coap_udp_init_message(req, COAP_TYPE_NON, COAP_GET, 0);
        }
        memcpy(req->token, obs->token, obs->token_len);
        req->token_len = obs->token_len;

        coap_set_header_uri_path(req, oc_string(resource->uri),
                oc_string_len(resource->uri));

        OC_DBG("creating separate response for notification");
#ifdef OC_BLOCK_WISE
        if (coap_separate_accept(req, response.separate_response,
                &obs->endpoint, obs->obs_counter, obs->block2_size) == 1) {
#else
        if (coap_separate_accept(req, response.separate_response,
                &obs->endpoint, obs->obs_counter) == 1) {
#endif
          response.separate_response->active = 1;
        }
      } // separate response
      else {
        OC_DBG("coap_notify_observers: notifying observer");
        coap_transaction_t *transaction = NULL;
        if (response_buf) {
          coap_packet_t notification[1];
          bool is_revert = false;
          uint8_t status_code = CONTENT_2_05;
          // if (obs->iface_mask == OC_IF_STARTUP_REVERT) {
          //  OC_DBG("coap_notify_observers: Setting Valid response for a REVERT
          //  "
          //         "notification");
          //  status_code = VALID_2_03;
          //  response_buf->code = VALID_2_03;
          //  is_revert = !is_revert;
          //}
#ifdef OC_TCP
          if (obs->endpoint.flags & TCP) {
            coap_tcp_init_message(notification, status_code);
          } else
#endif
          {
            coap_udp_init_message(notification, COAP_TYPE_NON, status_code, 0);
          }

          if (!is_revert) {
#ifdef OC_BLOCK_WISE
#ifdef OC_TCP
            if (!(obs->endpoint.flags & TCP) &&
                response_buf->response_length > obs->block2_size) {
#else
            if (response_buf->response_length > obs->block2_size) {
#endif
              notification->type = COAP_TYPE_CON;
              response_state = oc_blockwise_find_response_buffer(
                      oc_string(obs->resource->uri) + 1, 
                      oc_string_len(obs->resource->uri) - 1, 
                      &obs->endpoint, OC_GET,
                      NULL, 0, OC_BLOCKWISE_SERVER);
              if (response_state) {
                if (response_state->payload_size ==
                    response_state->next_block_offset) {
                  oc_blockwise_free_response_buffer(response_state);
                  response_state = NULL;
                } else {
                  continue;
                }
              }

              response_state = oc_blockwise_alloc_response_buffer(
                      oc_string(obs->resource->uri) + 1,
                      oc_string_len(obs->resource->uri) - 1, 
                      &obs->endpoint, OC_GET,
                      OC_BLOCKWISE_SERVER);

              if (!response_state) {
                goto leave_notify_observers;
              }

              memcpy(response_state->buffer, response_buf->buffer,
                      response_buf->response_length);
              response_state->payload_size =
                      (uint32_t)response_buf->response_length;
              uint32_t payload_size = 0;
              const uint8_t* payload = oc_blockwise_dispatch_block(
                response_state, 0, obs->block2_size, &payload_size);
              if (payload) {
                coap_set_payload(notification, payload, payload_size);
                coap_set_header_block2(notification, 0, 1, obs->block2_size);
                coap_set_header_size2(notification,
                                      response_state->payload_size);
                oc_blockwise_response_state_t *bwt_res_state =
                  (oc_blockwise_response_state_t *)response_state;
                coap_set_header_etag(notification, bwt_res_state->etag,
                                     COAP_ETAG_LEN);
              }
            } // blockwise transfer
            else
#endif
            {
#ifdef OC_TCP
              if (!(obs->endpoint.flags & TCP) &&
                  obs->obs_counter % COAP_OBSERVE_REFRESH_INTERVAL == 0) {
#else
              if (obs->obs_counter % COAP_OBSERVE_REFRESH_INTERVAL == 0) {
#endif
                OC_DBG("coap_observe_notify: forcing CON notification to check "
                       "for "
                       "client liveness");
                notification->type = COAP_TYPE_CON;
              }

              coap_set_payload(notification, response_buf->buffer,
                      response_buf->response_length);
            } //! blockwise transfer
          } // !is_revert

          coap_set_status_code(notification, response_buf->code);
          if (notification->code < BAD_REQUEST_4_00 &&
                  obs->resource->runtime_data->num_observers) {
            coap_set_header_observe(notification, (obs->obs_counter)++);
            observe_counter++;
          } else {
            coap_set_header_observe(notification, 1);
          }

          if (response_buf->content_format > 0) {
            coap_set_header_content_format(notification,
                    response_buf->content_format);
          }

          coap_set_token(notification, obs->token, obs->token_len);
          transaction = coap_new_transaction(coap_get_next_mid(), obs->token,
                  obs->token_len, &obs->endpoint);
          if (transaction) {
            // transaction
            obs->last_mid = transaction->mid;
            notification->mid = transaction->mid;
            transaction->message->length =
              coap_serialize_message(notification, transaction->message->data);
            if (transaction->message->length > 0) {
              coap_send_transaction(transaction);
            } else {
              coap_clear_transaction(transaction);
            }
          }
        } // response_buf != NULL
      }  //! separate response
      obs = obs->next;
    } // iterate over observers
  leave_notify_observers:;
#ifdef OC_DYNAMIC_ALLOCATION
    if (buffer) {
      free(buffer);
    }
#endif
  } // num_observers > 0
  else {
    OC_WRN("coap_notify_observers: no observers");
  }

  return resource->runtime_data->num_observers;
}

void notify_resource_defaults_observer(const oc_resource_t *resource,
        oc_interface_mask_t iface_mask, oc_response_buffer_t *response_buf)
{
#ifdef OC_BLOCK_WISE
  oc_blockwise_state_t *response_state = NULL;
#endif

#ifndef OC_DYNAMIC_ALLOCATION
  uint8_t buffer[OC_MAX_APP_DATA_SIZE];
#else  
  uint8_t* buffer = (uint8_t*)malloc(OC_MAX_APP_DATA_SIZE);
  if (!buffer) {
    OC_WRN("coap_notify_observers: out of memory allocating buffer");
    goto leave_notify_observers;
  } 
#endif 

  coap_observer_t *obs = NULL;
  oc_request_t request = { 0 };
  oc_response_t response = { 0 };
  response.separate_response = 0;
  oc_response_buffer_t response_buffer;
  OC_DBG("coap_notify_observers: Issue GET request to resource %s\n", oc_string_checked(resource->uri));
  
  response_buffer.buffer = buffer;
  response_buffer.buffer_size = OC_MAX_APP_DATA_SIZE;
  response.response_buffer = &response_buffer;
  request.resource = resource;
  request.response = &response;
  request.request_payload = NULL;
  oc_rep_new(response_buffer.buffer, (int)response_buffer.buffer_size);
  resource->get_handler.cb(&request, iface_mask, resource->get_handler.user_data);
  response_buf = &response_buffer;
  if (response_buf->code == OC_IGNORE) 
  {
    OC_DBG("coap_notify_observers: Resource ignored request");
    goto leave_notify_observers;
  } 

  // Iterate over observers.
  obs = (coap_observer_t *)oc_list_head(observers_list);
  while (obs) 
  {
    if (obs->resource != resource) 
    {
      obs = obs->next;
      continue;
    } // obs->resource != resource || endpoint != obs->endpoint
    if (obs->iface_mask != iface_mask) 
    {
      obs = obs->next;
      continue;
    }
    if (response.separate_response) 
    {
      coap_packet_t req[1];
      #ifdef OC_TCP
      if (obs->endpoint.flags & TCP) {
        coap_tcp_init_message(req, COAP_GET);
      } else
      #endif 
      {
        coap_udp_init_message(req, COAP_TYPE_NON, COAP_GET, 0);
      }
      memcpy(req->token, obs->token, obs->token_len);
      req->token_len = obs->token_len;

      coap_set_header_uri_path(req, oc_string(resource->uri),
                               oc_string_len(resource->uri));

      OC_DBG("coap_notify_observers: Creating separate response for "
             "notification");
      #ifdef OC_BLOCK_WISE
      if (coap_separate_accept(req, response.separate_response, &obs->endpoint, obs->obs_counter, obs->block2_size) == 1)
      #else 
      if (coap_separate_accept(req, response.separate_response, &obs->endpoint,
                               obs->obs_counter) == 1)
      #endif 
        response.separate_response->active = 1;
    } 
    else
    {
      OC_DBG("coap_notify_observers: notifying observer");
      coap_transaction_t *transaction = NULL;
      if (response_buf) 
      {
        coap_packet_t notification[1];
        uint8_t status_code = CONTENT_2_05;

        #ifdef OC_TCP
        if (obs->endpoint.flags & TCP) {
          coap_tcp_init_message(notification, status_code);
        } else
        #endif 
        {
          coap_udp_init_message(notification, COAP_TYPE_NON, status_code, 0);
        }

        #ifdef OC_BLOCK_WISE
        #ifdef OC_TCP
        if (!(obs->endpoint.flags & TCP) &&
            response_buf->response_length > obs->block2_size) {
        #else 
        if (response_buf->response_length > obs->block2_size) {
        #endif 
          notification->type = COAP_TYPE_CON;
          response_state = oc_blockwise_find_response_buffer(
            oc_string(obs->resource->uri) + 1,
            oc_string_len(obs->resource->uri) - 1, &obs->endpoint, OC_GET, NULL,
            0, OC_BLOCKWISE_SERVER);
          if (response_state) {
            if (response_state->payload_size ==
                response_state->next_block_offset) {
              oc_blockwise_free_response_buffer(response_state);
              response_state = NULL;
            } else {
              return;
            }
          }
          response_state = oc_blockwise_alloc_response_buffer(
            oc_string(obs->resource->uri) + 1,
            oc_string_len(obs->resource->uri) - 1, &obs->endpoint, OC_GET,
            OC_BLOCKWISE_SERVER);
          if (!response_state) {
            goto leave_notify_observers;
          }
          memcpy(response_state->buffer, response_buf->buffer,
                 response_buf->response_length);
          response_state->payload_size =
            (uint32_t)response_buf->response_length;
          uint32_t payload_size = 0;
          const uint8_t* payload = oc_blockwise_dispatch_block(
            response_state, 0, obs->block2_size, &payload_size);
          if (payload) {
            coap_set_payload(notification, payload, payload_size);
            coap_set_header_block2(notification, 0, 1, obs->block2_size);
            coap_set_header_size2(notification, response_state->payload_size);
            oc_blockwise_response_state_t *bwt_res_state =
              (oc_blockwise_response_state_t *)response_state;
            coap_set_header_etag(notification, bwt_res_state->etag,
                                 COAP_ETAG_LEN);
          }
        }
        else
        #endif
        {
        #ifdef OC_TCP
          if (!(obs->endpoint.flags & TCP) &&
              obs->obs_counter % COAP_OBSERVE_REFRESH_INTERVAL == 0) {
        #else 
          if (obs->obs_counter % COAP_OBSERVE_REFRESH_INTERVAL == 0) {
        #endif 
            OC_DBG("coap_observe_notify: forcing CON notification to check for client liveness");
            notification->type = COAP_TYPE_CON;
          }
          coap_set_payload(notification, 
                           response_buf->buffer,
                           response_buf->response_length);
        } 
        coap_set_status_code(notification, response_buf->code);
        if (notification->code < BAD_REQUEST_4_00 &&
            obs->resource->runtime_data->num_observers) {
          coap_set_header_observe(notification, (obs->obs_counter)++);
          observe_counter++;
        } else {
          coap_set_header_observe(notification, 1);
        }
        if (response_buf->content_format > 0) {
          coap_set_header_content_format(notification,
                                         response_buf->content_format);
        }
        coap_set_token(notification, obs->token, obs->token_len);
        transaction = coap_new_transaction(coap_get_next_mid(), obs->token,
                                           obs->token_len, &obs->endpoint);
        if (transaction) {
          obs->last_mid = transaction->mid;
          notification->mid = transaction->mid;
          transaction->message->length =
            coap_serialize_message(notification, transaction->message->data);
          if (transaction->message->length > 0) {
            coap_send_transaction(transaction);
          } else {
            coap_clear_transaction(transaction);
          }
        } 
      }
    }
    obs = obs->next;
  }
leave_notify_observers:;
#ifdef OC_DYNAMIC_ALLOCATION
  if (buffer) {
    free(buffer);
  }
#endif
}

#ifdef OC_BLOCK_WISE
int coap_observe_handler(void *request, void *response, const oc_resource_t *resource, uint16_t block2_size, 
                         oc_endpoint_t *endpoint, oc_interface_mask_t iface_mask)
#else 
int coap_observe_handler(void *request, void *response, const oc_resource_t *resource, 
                         oc_endpoint_t *endpoint, oc_interface_mask_t iface_mask)
#endif 
{
  coap_packet_t *const coap_req = request;
  coap_packet_t *const coap_res = response;

  int dup = -1;

  if (coap_req->code == COAP_GET && coap_res->code < BAD_REQUEST_4_00) 
  {
    // A GET without a positive response
    if (IS_OPTION(coap_req, COAP_OPTION_OBSERVE)) {
      if (coap_req->observe == 0) 
      { // register
        
#ifdef OC_BLOCK_WISE
        dup = add_observer(resource, block2_size, endpoint, coap_req->token, coap_req->token_len, 
                       coap_req->uri_path, coap_req->uri_path_len, iface_mask);
#else  
        dup = add_observer(resource, endpoint, coap_req->token, coap_req->token_len,
                       coap_req->uri_path, coap_req->uri_path_len, iface_mask);
#endif 
      }
      else if (coap_req->observe == 1) 
      { // deregister
        dup = coap_remove_observer_by_token(endpoint, coap_req->token, coap_req->token_len);
      }
    }
  }
  return dup;
}

#endif
