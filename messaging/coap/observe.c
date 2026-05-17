/*    
 * Copyright (c) 2016 Intel Corporation
 * Copyright (c) 2024-2026 KNX Association
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
 */

#include "oc_config.h"
#include "separate.h"
#include "conf.h"

#ifdef OC_SERVER
#include "observe.h"
#include "util/oc_memb.h"
#include <stdio.h>
#include <string.h>
#include "oc_buffer.h"
#include "port/oc_clock.h"
#include <stdlib.h>

#ifdef OC_BLOCK_WISE
#include "oc_blockwise.h"
#include <errno.h>
#endif
#include "oc_coap.h"
#include "oc_endpoint.h"
#include "oc_rep.h"
#include "oc_ri.h"

// RFC 7641 Section 3.4: Observe option is at most 3 bytes (0 to 2^24-1)
#define OBSERVE_COUNTER_MASK 0x00FFFFFF

/* 
  - used to seed each new (individual) observer with its notification response with a unique starting sequence number
  - see RFC 7641 Section 3.4 for details on the observe option value and sequence number wrap-around handling
  - starts with 3 , see 
*/
static uint32_t observe_counter = 3;
uint32_t get_observe_counter(void) { return observe_counter; }

OC_LIST(observers_list);
OC_MEMB(observers_memb, coap_observer_t, COAP_MAX_OBSERVERS);

static void coap_remove_observer(coap_observer_t* o);
static void coap_remove_expired_observers(void);

// removes ONE existing observer for the given client endpoint and resource URI, if any, and returns 1 if found and removed, 0 if not found
static int coap_remove_observer_handle_by_uri(const oc_endpoint_t* endpoint, const char* uri, int uri_len)
{
  coap_observer_t* obs = (coap_observer_t*)oc_list_head(observers_list);

  while (obs)
  {
    coap_observer_t* next = obs->next;

    if (oc_endpoint_compare(&obs->endpoint, endpoint) == 0
        && oc_string_len(obs->url) == (size_t)uri_len
        && memcmp(oc_string(obs->url), uri, uri_len) == 0)
    {
      coap_remove_observer(obs);
      OC_DBG("removed 1 observer");
      return 1;
    }

    obs = next;
  }

  OC_DBG("removed 0 observer");
  return 0;
}


#ifdef OC_BLOCK_WISE
static int add_observer(const oc_resource_t* resource, uint16_t block2_size,
                        const oc_endpoint_t* endpoint, const uint8_t* token, size_t token_len,
                        const char* uri, size_t uri_len)
#else
static int add_observer(const oc_resource_t* resource, oc_endpoint_t* endpoint,
                        const uint8_t* token, size_t token_len, const char* uri,
                        size_t uri_len)
#endif
{
  const int duplicate = coap_remove_observer_handle_by_uri(endpoint, uri, (int)uri_len);

  coap_observer_t* o = (coap_observer_t*)oc_memb_alloc(&observers_memb);

  if (o)
  {
    oc_new_string(&o->url, uri, uri_len);
    memcpy(&o->endpoint, endpoint, sizeof(oc_endpoint_t));
    o->token_len = (uint8_t)token_len;
    memcpy(o->token, token, token_len);
    o->last_mid = 0;
    
    // pre inc and init counter for THIS observation from global counter
    o->obs_counter = ++observe_counter & OBSERVE_COUNTER_MASK;
    o->resource = resource;

    #ifdef OC_BLOCK_WISE
    o->block2_size = block2_size;
    #endif
    
    // knx iot defaults, overridden after observe registration
    o->lifetime = 0;                          // not set, will be updated by oc_ri after registration
    o->created = oc_clock_time();             // time of observer creation, used for lifetime expiry check
    o->use_con = true;                        // default in KNX IoT specification, can be overridden by "non=true" query parameter
    o->first_sent = false;                    // first notification NOT send out 
    resource->runtime_data->num_observers++;  // increase number of current observers for this resource

    #ifdef OC_DYNAMIC_ALLOCATION
    OC_DBG("adding observer (%i) for /%s [0x%02X%02X]", oc_list_length(observers_list) + 1, oc_string_checked(o->url), o->token[0], o->token[1]);
    #else
    OC_DBG("adding observer (%i/%i) for /%s [0x%02X%02X]",
           oc_list_length(observers_list) + 1, COAP_MAX_OBSERVERS, oc_string_checked(o->url), o->token[0], o->token[1]);
    #endif
    
    oc_list_add(observers_list, o);
    return duplicate;
  }

  OC_WRN("insufficient memory to add new observer");
  return -1;
}

static void coap_remove_observer(coap_observer_t* o)
{
  OC_DBG("removing observer for /%s [0x%02X%02X]", oc_string_checked(o->url), o->token[0], o->token[1]);

  #ifdef OC_BLOCK_WISE
  oc_blockwise_state_t* response_state = oc_blockwise_find_response_buffer(
    oc_string(o->resource->uri) + 1, oc_string_len(o->resource->uri) - 1,
    &o->endpoint, COAP_GET, NULL, 0, OC_BLOCKWISE_SERVER);
  if (response_state)
  {
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
  coap_observer_t *obs = (coap_observer_t*)oc_list_head(observers_list);

  while (obs)
  {
    coap_observer_t* next = obs->next;
    coap_remove_observer(obs);
    obs = next;
  }
}

// removes ALL observers for the given client endpoint, and returns the amount of removed observers
int coap_remove_observer_by_client(const oc_endpoint_t* endpoint)
{
  int removed = 0;
  coap_observer_t *obs = (coap_observer_t*)oc_list_head(observers_list);

  OC_DBG("Unregistering observers for client at: ");
  PRINTipaddr(*endpoint);

  while (obs)
  {
    coap_observer_t* next = obs->next;
    if (oc_endpoint_compare(&obs->endpoint, endpoint) == 0)
    {
      coap_remove_observer(obs);
      removed++;
    }
    obs = next;
  }

  OC_DBG("Removed %d observers", removed);
  return removed;
}

// removes ONE observer for the given client endpoint and request token, and returns 1 if found and removed, 0 if not found
int coap_remove_observer_by_token(const oc_endpoint_t* endpoint, const uint8_t* token, size_t token_len)
{
  OC_DBG("unregistering observer for request token 0x%02X%02X ...", token[0], token[1]);

  for (coap_observer_t* obs = (coap_observer_t*)oc_list_head(observers_list); obs; obs = obs->next)
  {
    if (oc_endpoint_compare(&obs->endpoint, endpoint) == 0 
        && obs->token_len == token_len 
        && memcmp(obs->token, token, token_len) == 0)
    {
      coap_remove_observer(obs);
      OC_DBG("removed 1 observer");
      return 1;
    }
  }

  OC_DBG("removed 0 observer");
  return 0;
}

// removes ONE observer for the given client endpoint and request MID, and returns 1 if found and removed, 0 if not found
int coap_remove_observer_by_mid(const oc_endpoint_t* endpoint, uint16_t mid)
{
  OC_DBG("unregistering observers for request MID %u", mid);

  for (coap_observer_t* obs = (coap_observer_t*)oc_list_head(observers_list); obs; obs = obs->next)
  {
    if (oc_endpoint_compare(&obs->endpoint, endpoint) == 0 
        && obs->last_mid == mid)
    {
      coap_remove_observer(obs);
      OC_DBG("removed 1 observer");
      return 1;
    }
  }

  OC_DBG("removed 0 observer");
  return 0;
}

// removes ALL observers for the given resource, and returns the amount of removed observers
int coap_remove_observer_by_resource(const oc_resource_t* rsc)
{
  int removed = 0;
  coap_observer_t *obs = (coap_observer_t*)oc_list_head(observers_list);

  while (obs)
  {
    // save tmp copy of next pointer, since obs might be removed in the if block below
    coap_observer_t* next = obs->next;
    
    if (obs->resource == rsc 
        && oc_string(rsc->uri)
        && oc_string_len(obs->url) == oc_string_len(rsc->uri) - 1 
        && memcmp(oc_string(obs->url), oc_string(rsc->uri) + 1, oc_string_len(rsc->uri) - 1) == 0)
    {
      coap_remove_observer(obs);
      removed++;
    }

    // restore to next observer in list
    obs = next;
  }

  return removed;
}

#ifdef OC_SECURITY	// TODO 12 FIXME this is NOT TCP only!
int coap_remove_observers_on_dos_change(bool reset)
{
  // Iterate over observers.
  coap_observer_t* obs = (coap_observer_t*)oc_list_head(observers_list);
  while (obs != NULL)
  {
    if (reset || !oc_sec_check_acl(COAP_GET, obs->resource, &obs->endpoint))
    {
      coap_observer_t* o = obs;
      coap_packet_t notification[1];
      const uint16_t mid = coap_get_next_mid();
#ifdef OC_TCP
if (obs->endpoint.flags& TCP) {
        coap_tcp_init_message(notification, SERVICE_UNAVAILABLE_5_03);
      } else
#endif
{
              coap_udp_init_message(notification, COAP_TYPE_NON,
                        SERVICE_UNAVAILABLE_5_03, mid);
              }

        coap_set_token(notification, obs->token, obs->token_len);
        coap_transaction_t* transaction = coap_new_transaction(mid, obs->token, obs->token_len, &obs->endpoint);
        if (transaction)
        {
          transaction->message->length = coap_serialize_message(notification, transaction->message->data);
          if (transaction->message->length > 0)
          {
            coap_send_transaction(transaction);
          }
          else
          {
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

int coap_notify_observers(const oc_resource_t* resource, oc_response_buffer_t* response_buf, const oc_endpoint_t* endpoint)
{
  if (!resource)
  {
    OC_WRN("no resource passed; returning");
    return 0;
  }

  #ifdef OC_SECURITY	// TODO 12 FIXME this is NOT TCP only!
  oc_sec_pstat_t *ps = oc_sec_get_pstat();
  if (ps->s != OC_DOS_RFNOP) 
  {
    OC_WRN("device not in RFNOP; skipping notification");
    return 0;
  }
  #endif

  if (resource->runtime_data->num_observers == 0)
  {
    OC_INF("no observers present");
    return 0;
  }

  // remove expired observers before sending notifications (stale notifications are only deleted here, no cyclic check)
  coap_remove_expired_observers();

  #ifdef OC_BLOCK_WISE
  oc_blockwise_state_t* response_state = NULL;
  #endif

  // .buffer is init with 0, on free no problem
  oc_response_buffer_t response_buffer = {.buffer_size = OC_MAX_OBSERVE_SIZE};

  // .separate_response may be overwritten in GET handler, but response buffer is only used if separate_response is NULL, so it's ok to set it here already
  oc_response_t response = {.response_buffer = &response_buffer}; 
  
  if (!response_buf)
  { /* 
       no response buffer passed (resource is NOT NULL), so we will create a temporary response buffer and populate
       it by calling the resource's GET handler, and then use this buffer for notifications to observers
    */
    
    OC_DBG("issue GET request to resource %s", oc_string_checked(resource->uri));

    #ifndef OC_DYNAMIC_ALLOCATION
    uint8_t stack_buffer[OC_MAX_OBSERVE_SIZE];
    response_buffer.buffer = stack_buffer;
    #else
    response_buffer.buffer = (uint8_t*)malloc(OC_MAX_OBSERVE_SIZE);
    if (!response_buffer.buffer)
    {
      OC_WRN("out of memory allocating buffer");
      return resource->runtime_data->num_observers;
    }
    #endif

    // init 
    oc_request_t request = {.resource = resource, .response = &response};

    // init CBOR data stream
    oc_rep_new(response_buffer.buffer, (int)response_buffer.buffer_size);

    // call handler, request is empty so no interface mask by request
    resource->get_handler.cb(&request, resource->get_handler.interface_mask, resource->get_handler.user_data);

    if (response_buffer.code == OC_IGNORE)
    {
      OC_DBG("resource ignored request");
      #ifdef OC_DYNAMIC_ALLOCATION
      free(response_buffer.buffer);
      #endif
      return resource->runtime_data->num_observers;
    }

    // set since it was NULL before, but needed later on notification (see below)
    response_buf = &response_buffer;
  }

  // iterate over observers
  coap_observer_t*  obs = (coap_observer_t*)oc_list_head(observers_list);
  while (obs)
  {
    if (obs->resource != resource || (endpoint && oc_endpoint_compare(&obs->endpoint, endpoint) != 0))
    {
      obs = obs->next;
      continue;
    }

    /*
      RFC 7641 3.1: the registration GET response itself serves as the first notification.

      For if.o resources (/p/X) the piggybacked registration GET response already contains the current
      datapoint value, e.g.:
        { 1: true }   -- current value of the datapoint (boolean 'on'), prepared by get_handler.cb

      Skipping here prevents an immediate duplicate notification right after registration (the newly created registration is then part of this list).
      Subsequent notifications will call get_handler.cb again to read the latest value.
    */
    if (!obs->first_sent)
    {
      obs->first_sent = true;
      obs = obs->next;
      continue;
    }

    // it will be a separate CON response (CAN be overwritten from NULL ONLY in GET handler)
    if (response.separate_response)
    {
      coap_packet_t req[1];

      #ifdef OC_TCP
      if (obs->endpoint.flags & TCP)
      {
        coap_tcp_init_message(req, COAP_GET);
      }
      else
      #endif
      {
        coap_udp_init_message(req, COAP_TYPE_NON, COAP_GET, 0);
      }
      memcpy(req->token, obs->token, obs->token_len);
      req->token_len = obs->token_len;

      coap_set_header_uri_path(req, oc_string(resource->uri), oc_string_len(resource->uri));

      OC_DBG("creating separate response for notification");

      #ifdef OC_BLOCK_WISE
      if (coap_separate_accept(req, response.separate_response, &obs->endpoint, obs->obs_counter, obs->block2_size) == 1)
      {
      #else
      if (coap_separate_accept(req, response.separate_response, &obs->endpoint, obs->obs_counter) == 1) {
      #endif
        response.separate_response->active = true;
      }
    } // separate response
    else
    { // response_buf is always non-NULL here: either passed by caller or set above after calling GET handler
      OC_DBG("notifying observer");

      // build notification with the raw inbound CBOR payload
      coap_packet_t notification[1];
      const coap_message_type_t msg_type = obs->use_con ? COAP_TYPE_CON : COAP_TYPE_NON;
      const uint16_t mid = coap_get_next_mid();

      #ifdef OC_TCP
      if (obs->endpoint.flags & TCP)
      {
        coap_tcp_init_message(notification, CONTENT_2_05);
      }
      else
      #endif
      {
        // KNX spec 2.5.9.4: use CON by default; NON when observer registered with "non=true"
        coap_udp_init_message(notification, msg_type, CONTENT_2_05, mid);
      }

      #ifdef OC_BLOCK_WISE
      #ifdef OC_TCP
      if (!(obs->endpoint.flags & TCP) && response_buf->response_length > obs->block2_size) 
      {
      #else
      if (response_buf->response_length > obs->block2_size)
      {
        #endif
        notification->type = COAP_TYPE_CON;
        response_state = oc_blockwise_find_response_buffer(
          oc_string(obs->resource->uri) + 1,
          oc_string_len(obs->resource->uri) - 1,
          &obs->endpoint, COAP_GET,
          NULL, 0, OC_BLOCKWISE_SERVER);
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
            continue;
          }
        }

        response_state = oc_blockwise_alloc_response_buffer(
          oc_string(obs->resource->uri) + 1,
          oc_string_len(obs->resource->uri) - 1,
          &obs->endpoint, COAP_GET,
          OC_BLOCKWISE_SERVER);

        if (!response_state)
        {
          break;
        }

        memcpy(response_state->buffer, response_buf->buffer, response_buf->response_length);
        response_state->payload_size = (uint32_t)response_buf->response_length;
        uint32_t payload_size = 0;
        const uint8_t* payload = oc_blockwise_dispatch_block(response_state, 0, obs->block2_size, &payload_size);
        if (payload)
        {
          coap_set_payload(notification, payload, payload_size);
          coap_set_header_block2(notification, 0, 1, obs->block2_size);
          coap_set_header_size2(notification, response_state->payload_size);
          oc_blockwise_response_state_t* bwt_res_state =
            (oc_blockwise_response_state_t*)response_state;
          coap_set_header_etag(notification, bwt_res_state->etag, COAP_ETAG_LEN);
        }
      }
      else
      #endif
      { // payload fits

        #ifdef OC_TCP
        if (!(obs->endpoint.flags & TCP) && obs->use_con && obs->obs_counter % COAP_OBSERVE_REFRESH_INTERVAL == 0)
        {
        #else
        if (obs->use_con && obs->obs_counter % COAP_OBSERVE_REFRESH_INTERVAL == 0)
        {
        #endif
          
          // RFC 7641 4.5: periodic CON to verify client is still alive; skip for NON observers
          OC_DBG("coap_observe_notify: forcing CON notification to check for client liveness");
          notification->type = COAP_TYPE_CON;
        }

        coap_set_payload(notification, response_buf->buffer, response_buf->response_length);
      }

      // final code from GET handler, may be changed by separate response handler if separate_response is set above
      coap_set_status_code(notification, response_buf->code);

      // set only if a valid value is present, otherwise leave it out when '0' e.g; not initialized in GET handler 
      if (response_buf->content_format > 0)
      {
        coap_set_header_content_format(notification, response_buf->content_format);
      }

      // KNX spec 2.6.10.2: indicate the heartbeat interval via Max-Age so the subscriber can detect stale values
      if (obs->resource->observe_period_seconds > 0)
      {
        coap_set_header_max_age(notification, obs->resource->observe_period_seconds);
      }

      coap_set_token(notification, obs->token, obs->token_len);

      coap_transaction_t* transaction = coap_new_transaction(mid, obs->token, obs->token_len, &obs->endpoint);
      if (transaction)
      {
        if (notification->code < BAD_REQUEST_4_00 && obs->resource->runtime_data->num_observers)
        {
          // use current seq. number
          coap_set_header_observe(notification, obs->obs_counter);

          // increment seq. number, each observation relationship has its own counter (was init from global counter)
          obs->obs_counter++;
          obs->obs_counter &= OBSERVE_COUNTER_MASK;
        }
        else
        {
          /*
             notification with error response code or no more observers for this resource,
             do not set Observe option to avoid client confusion and unnecessary retransmissions
             -> de-register
          */
          coap_set_header_observe(notification, OC_OBSERVE_DEREGISTER);
        }

        obs->last_mid = mid;

        transaction->message->length = coap_serialize_message(notification, transaction->message->data);
        if (transaction->message->length > 0)
        {
          coap_send_transaction(transaction);
        }
        else
        {
          coap_clear_transaction(transaction);
        }
      }
      
    }
    
    obs = obs->next;
  }

  #ifdef OC_DYNAMIC_ALLOCATION
  free(response_buffer.buffer);
  #endif

  return resource->runtime_data->num_observers;
}

#ifdef OC_BLOCK_WISE
int coap_observe_handler(void* request, void* response, const oc_resource_t* resource, uint16_t block2_size, oc_endpoint_t* endpoint)
#else
int coap_observe_handler(void* request, void* response, const oc_resource_t* resource, oc_endpoint_t* endpoint)
#endif
{
  coap_packet_t* const coap_req = (coap_packet_t*)request;
  coap_packet_t* const coap_res = (coap_packet_t*)response;

  // registration/ de-registration  result
  int result = -1;

  if (coap_req->code == COAP_GET && coap_res->code < BAD_REQUEST_4_00)
  {
    // a GET with a positive response
    if (IS_OPTION(coap_req, COAP_OPTION_OBSERVE))
    {
      if (coap_req->observe == OC_OBSERVE_REGISTER)
      { // register

        #ifdef OC_BLOCK_WISE
        result = add_observer(
           resource, block2_size, endpoint, coap_req->token, coap_req->token_len, 
           coap_req->uri_path, coap_req->uri_path_len);

        #else
        result = add_observer(resource, endpoint, coap_req->token, coap_req->token_len, coap_req->uri_path, coap_req->uri_path_len);
        #endif

        if (result >= 0)
        { // added new or refreshed observe relationship
          coap_observer_t* obs = (coap_observer_t*)oc_list_tail(observers_list);
          if (obs)
          {
            /*
              /k, clause 2.5.9.3: "lt" SHALL + "non" MAY query parameters apply to KNX IoT CoAP Observe registrations. 
              "lt" is REQUIRED; reject without it.

              /p/ clause 2.5.11.6: "lt" SHALL + "non" MAY (= assumed, no specification hint) query parameters apply KNX IoT CoAP Observe registrations. 
              "lt" is REQUIRED; reject without it.
            */

            // parse "lt" (lifetime) -- KNX spec 2.5.11.6: REQUIRED for all observe registrations
            const char* lt_value = NULL;
            const int lt_len = coap_get_query_variable(request, "lt", &lt_value);

            if (lt_len <= 0)
            {
              OC_WRN("observe register rejected: missing 'lt' query parameter");
              coap_remove_observer(obs);
              return -2;
            }

            errno = 0;
            const uint32_t lifetime = (uint32_t)strtoul(lt_value, NULL, 10);
            /*
              KNX spec 2.5.11.6: device SHALL support a minimum lifetime of 86400s (24h).
              This means any value up to 86400 is valid; reject values above the supported maximum.
            */
            if (errno || lifetime == 0 || lifetime > 86400)
            {
              OC_WRN("observe register rejected: unsupported 'lt' value %u", (unsigned)lifetime);
              coap_remove_observer(obs);
              return -2;
            }

            obs->lifetime = lifetime;

            // parse "non" (non-confirmable) -- KNX spec 2.5.11.6
            const char* non_value = NULL;
            const int non_len = coap_get_query_variable(request, "non", &non_value);

            // default is CON (use_con = true); only set NON when "non=true" (4 chars)
            obs->use_con = !(non_len == 4 && strncmp(non_value, "true", 4) == 0);
          }
        }
      }
      else if (coap_req->observe == OC_OBSERVE_DEREGISTER)
      { // deregister
        result = coap_remove_observer_by_token(endpoint, coap_req->token, coap_req->token_len);
      }
    }
  }
  return result;
}

static void coap_remove_expired_observers(void)
{
  const oc_clock_time_t now = oc_clock_time();
  coap_observer_t* obs = (coap_observer_t*)oc_list_head(observers_list);

  while (obs)
  {
    coap_observer_t* next = obs->next;
    if (obs->lifetime > 0)
    {
      const oc_clock_time_t elapsed = now - obs->created;
      if (elapsed > (oc_clock_time_t)obs->lifetime * OC_CLOCK_SECOND)
      {
        OC_DBG("removing expired observer for /%s", oc_string_checked(obs->url));
        coap_remove_observer(obs);
      }
    }
    obs = next;
  }
}

void coap_notify_k_observers(const oc_resource_t* resource, const uint8_t* payload, size_t payload_len)
{
  if (!resource || !payload || payload_len == 0)
  {
    return;
  }

  // remove expired observers before sending notifications (stale notifications are only deleted here, no cyclic check)
  coap_remove_expired_observers();

  coap_observer_t* obs = (coap_observer_t*)oc_list_head(observers_list);
  while (obs)
  {
    coap_observer_t* next = obs->next;

    if (obs->resource != resource)
    {
      obs = next;
      continue;
    }

    if (!obs->first_sent)
    {
      /*
        RFC 7641 3.1: the registration GET response itself serves as the first notification.

        For /k the piggybacked registration GET response contains only the device individual address:
          { 4: 1234 }   -- where 4 = 'ia' key, 1234 = individual address of this device

        The actual s-mode payload (sia + s:{st,ga,value}) is only forwarded in subsequent notifications.
        Skip and mark as sent so the next s-mode frame triggers the first real payload notification.
      */
      obs->first_sent = true;
      obs = next;
      continue;
    }

    // build notification with the raw inbound CBOR payload
    coap_packet_t notification[1];
    const coap_message_type_t msg_type = obs->use_con ? COAP_TYPE_CON : COAP_TYPE_NON;
    const uint16_t mid = coap_get_next_mid();

    #ifdef OC_TCP
    if (obs->endpoint.flags & TCP)
    {
      coap_tcp_init_message(notification, CONTENT_2_05);
    }
    else
    #endif
    {
      // KNX spec 2.5.9.4: use CON by default; NON when observer registered with "non=true"
      coap_udp_init_message(notification, msg_type, CONTENT_2_05, mid);
    }

    coap_set_header_content_format(notification, APPLICATION_CBOR);
    coap_set_token(notification, obs->token, obs->token_len);
    coap_set_payload(notification, payload, payload_len);

    coap_transaction_t* transaction = coap_new_transaction(mid, obs->token, obs->token_len, &obs->endpoint);
    if (transaction)
    {
      // use current seq. number
      coap_set_header_observe(notification, obs->obs_counter);
      
      // increment seq. number, each observation relationship has its own counter (was init from global counter)
      obs->obs_counter++;
      obs->obs_counter &= OBSERVE_COUNTER_MASK;

      obs->last_mid = mid;
      
      transaction->message->length = coap_serialize_message(notification, transaction->message->data);
      if (transaction->message->length > 0)
      {
        coap_send_transaction(transaction);
      }
      else
      {
        coap_clear_transaction(transaction);
      }
    }

    obs = next;
  }
}

#endif
