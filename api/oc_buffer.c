/*
 * Copyright (c) 2016 Intel Corporation
 * Copyright (c) 2022 Cascoda Ltd
 * Copyright (c) 2024-2026 KNX Association
 *            
 * SPDX-License-Identifier: Apache-2.0
 */

#include "messaging/coap/engine.h"
#include "oc_signal_event_loop.h"
#include "port/oc_network_events_mutex.h"
#include "util/oc_memb.h"
#include "messaging/coap/coap.h"
#include <stdint.h>
#include <stdio.h>
#ifdef OC_DYNAMIC_ALLOCATION
#include <stdlib.h>
#endif 

#ifdef OC_TCP_TLS
#include "security/oc_tls.h"
#endif 
#include "security/oc_oscore.h"
#include "messaging/coap/oscore.h"
#include "oc_buffer.h"
#include "oc_config.h"
#include "oc_events.h"

OC_PROCESS(message_buffer_handler, "OC Message Buffer Handler");
#ifdef OC_INOUT_BUFFER_POOL
OC_MEMB_STATIC(oc_incoming_buffers, oc_message_t, OC_INOUT_BUFFER_POOL);
OC_MEMB_STATIC(oc_outgoing_buffers, oc_message_t, OC_INOUT_BUFFER_POOL);
#else  
OC_MEMB(oc_incoming_buffers, oc_message_t, OC_MAX_NUM_CONCURRENT_REQUESTS);
OC_MEMB(oc_outgoing_buffers, oc_message_t, OC_MAX_NUM_CONCURRENT_REQUESTS);
#endif 

static oc_message_t* allocate_message(struct oc_memb* pool) {
  oc_network_event_handler_mutex_lock();
  oc_message_t* message = (oc_message_t*)oc_memb_alloc(pool);
  oc_network_event_handler_mutex_unlock();
  if (message) {
#if defined(OC_DYNAMIC_ALLOCATION) && !defined(OC_INOUT_BUFFER_SIZE)
    message->data = (uint8_t*)malloc(OC_PDU_SIZE);
    if (!message->data) {
      OC_ERR("Out of memory, cannot allocate message");
      oc_memb_free(pool, message);
      return NULL;
    }
#endif 

    // allocated memory is wiped with '0' on allocation, hence do only others on init
    message->pool = pool;
    message->ref_count = 1;
    message->endpoint.interface_index = -1;
    message->endpoint.auth_at_index_from_former_inbound_request = -1;
    
#if !defined(OC_DYNAMIC_ALLOCATION) || defined(OC_INOUT_BUFFER_SIZE)
    OC_DBG("buffer: Allocated TX/RX buffer; num free: %d", oc_memb_numfree(pool));
#endif 
  } 
  else 
  {
    
    /*
     No unused buffers, so go through buffers with soft references and
     free one (with the lowest ref count 1). Said buffer can no longer be
     used for e.g. retransmitting requests when challenged with an Echo option.

     - However, freeing up one of these means that it can no longer be used for
       its original purpose.
     - Additionally an auto release method must be defined for the message itself.
    
    */

    for (int i = 0; i < pool->num; i++) {
      const int offset = pool->size * i;
      message = (oc_message_t*) ((uint8_t*) pool->mem + offset);

			if (message->ref_count == 1 && message->soft_ref_cb)
			{
				// call auto 'release' method to release a message
			  message->soft_ref_cb(message);

        // was the last reference (=1), so now we can allocate a new message successfully
        return allocate_message(pool);
      }
    }

    OC_WRN("buffer: No free TX/RX buffers!");
    message = NULL;
  }

  return message;
}

void oc_set_buffers_avail_cb(oc_memb_buffers_avail_callback_t cb) 
{
  oc_memb_set_buffers_avail_cb(&oc_incoming_buffers, cb);
}

oc_message_t* oc_allocate_message(void) 
{
  return allocate_message(&oc_incoming_buffers);
}

oc_message_t* oc_internal_allocate_outgoing_message(void) 
{
  return allocate_message(&oc_outgoing_buffers);
}

void oc_message_add_ref(oc_message_t* message) 
{
  if (message) 
  {
    message->ref_count++;
  }
}

void oc_message_unref(oc_message_t* message) 
{
  if (message) 
  {
    message->ref_count--;
    if (message->ref_count == 0) {
      #if defined(OC_DYNAMIC_ALLOCATION) && !defined(OC_INOUT_BUFFER_SIZE)
      if (message->data) 
      {
        free(message->data);
      }
      #endif 

      struct oc_memb* pool = message->pool;
      if (pool) 
      {
        oc_memb_free(pool, message);
      }
    }
  }
}

void oc_receive_message(oc_message_t* message) 
{
  if (oc_process_post(&message_buffer_handler, oc_events[INBOUND_NETWORK_EVENT],
          message) == OC_PROCESS_ERR_FULL) 
  {
    oc_message_unref(message);
  }
}

void oc_send_message(oc_message_t* message) 
{
  // forward message (any type such as plain/ secured, CON/NON request, ... )
	if (oc_process_post(&message_buffer_handler, oc_events[OUTBOUND_NETWORK_EVENT], message) == OC_PROCESS_ERR_FULL) 
  {
		OC_ERR("oc_send_message ref_count decrease due to FULL");
		message->ref_count--;
	}

  _oc_signal_event_loop();
}

#ifdef KNX_TCP_TLS
void oc_close_all_tls_sessions(void)
{
  oc_process_post(&message_buffer_handler, oc_events[TLS_CLOSE_ALL_SESSIONS], NULL);
  _oc_signal_event_loop();
}
#endif 

OC_PROCESS_THREAD(message_buffer_handler, ev, data)
{
  OC_PROCESS_BEGIN();
    while (1)
    {
      OC_PROCESS_YIELD();

      oc_message_t* message = (oc_message_t*)data;

      if (ev == oc_events[INBOUND_NETWORK_EVENT])
      {
        // inbound
        if (oscore_is_oscore_message(message))
        {
          // here a plain message is checked for OSCORE header and in case of it is sent to the OSCORE layer
          OC_DBG("Inbound OSCORE message, forwarding to OSCORE layer");
          oc_process_post(&oc_oscore_handler, oc_events[INBOUND_OSCORE_EVENT], data);
        }
        else
        {
          OC_DBG("Inbound plain message, forwarding to CoAP layer");
          oc_process_post(&coap_engine, oc_events[INBOUND_RI_EVENT], data);
        }
      }
      else if (ev == oc_events[OUTBOUND_NETWORK_EVENT])
      {
        // 1. handle OSCORE (mc/uc) s-mode messages first, encrypt the outgoing message before sending it (pass to OSCORE layer)
        // 2. handle PLAIN (multicast) discovery messages as a second step
        if (message->endpoint.flags & OSCORE)
        {
          if (message->endpoint.flags & MULTICAST)
          {
            // multicast
            OC_DBG("Outbound plain multicast message, forwarding to OSCORE layer");
            oc_process_post(&oc_oscore_handler, oc_events[OUTBOUND_MC_OSCORE_EVENT], data);
          }
          else
          {
            // unicast
            OC_DBG("Outbound plain unicast message, forwarding to OSCORE layer");
            oc_process_post(&oc_oscore_handler, oc_events[OUTBOUND_UC_OSCORE_EVENT], data);
          }
        }
        else if (message->endpoint.flags & DISCOVERY)
        {
          OC_DBG("Outbound plain discovery request, forwarding to IP layer");
          oc_endpoint_print(&message->endpoint);
          oc_send_discovery_request(message);
          oc_message_unref(message);
        }
        else
        {
          OC_DBG("Outbound plain unicast message, forwarding to IP layer");
          oc_send_buffer(message);
          oc_message_unref(message);
        }
      }
      else if (ev == oc_events[OUTBOUND_NETWORK_EVENT_ENCRYPTED])
      {
        // 1. handle OSCORE (mc/uc) s-mode messages first, outgoing message are encrypted (received from OSCORE layer)
       
        if (message->endpoint.flags & OSCORE)
        {
          if (message->endpoint.flags & MULTICAST)
          {
            OC_DBG("Outbound OSCORE multicast message, forwarding to IP layer");
            oc_send_discovery_request(message);
            oc_message_unref(message);
          }
          else
          {
            OC_DBG("Outbound OSCORE unicast message, forwarding to IP layer");
            oc_send_buffer(message);
            oc_message_unref(message);
          }
        }
      }
    }

  OC_PROCESS_END()
}