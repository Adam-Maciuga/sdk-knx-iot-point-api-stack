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
#include "messaging/coap/coap.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef OC_TCP_TLS
#include "security/oc_tls.h"
#endif 
#include "security/oc_oscore.h"
#include "messaging/coap/oscore.h"
#include "oc_buffer.h"
#include "oc_config.h"
#include "oc_events.h"

OC_PROCESS(message_buffer_handler, "OC Message Buffer Handler");

oc_message_t* oc_allocate_message(void)
{
  oc_network_event_handler_mutex_lock();
  oc_message_t* message = calloc(1, sizeof(oc_message_t));
  oc_network_event_handler_mutex_unlock();

  if (message)
  {
    message->data = (uint8_t*)malloc(OC_PDU_SIZE);
    if (!message->data)
    {
      OC_ERR("Out of memory, cannot allocate message data!");
      free(message);
      return NULL;
    }

    // allocated memory is wiped with '0' on allocation, hence do only others on init
    message->ref_count = 1;
    message->endpoint.interface_index = -1;
  }
  else
  {
    OC_WRN("buffer: No free TX/RX buffers!");
  }

  return message;
}

void oc_message_add_ref(oc_message_t* message) 
{
  if (message) 
  {
    message->ref_count++;
    OC_DBG("increase message (%p) ref counter, counter is now %d", (void*)message, message->ref_count);
  }
  
}

void oc_message_unref(oc_message_t* message) 
{
  if (message) 
  {
    message->ref_count--;
    OC_DBG("decrease message (%p) ref counter, counter is now%d", (void*)message,message->ref_count);
    if (message->ref_count == 0)
    {
      if (message->data)
      {
        free(message->data);
      }

      free(message);
    }
  }
}

void oc_receive_message(oc_message_t* message) 
{
  if (oc_process_post(&message_buffer_handler, oc_events[INBOUND_NETWORK_EVENT], message) == OC_PROCESS_ERR_FULL) 
  {
    OC_ERR("oc_receive_message ref_count decrease due to FULL");
    oc_message_unref(message);
  }
}

void oc_send_message(oc_message_t* message) 
{
  // forward message (any type such as plain/ secured, CON/NON request, ... )
	if (oc_process_post(&message_buffer_handler, oc_events[OUTBOUND_NETWORK_EVENT], message) == OC_PROCESS_ERR_FULL) 
  {
		OC_ERR("oc_send_message ref_count decrease due to FULL");
    oc_message_unref(message);
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
        // 3. handle PLAIN unicast messages as a third step, outgoing message are not encrypted (such as an 4-byte CoAP empty ACK)
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
          OC_DBG("Outbound plain discovery (multicast) request, forwarding to IP layer");
          oc_endpoint_print(&message->endpoint);
          oc_send_discovery_request(message);
          oc_message_unref(message);
        }
        else
        {
          OC_DBG("Outbound plain unicast message, forwarding to IP layer");
          
          #ifdef OC_REQUEST_HISTORY
          oc_coap_response_cache_store(message);
          #endif
          
          oc_send_buffer(message);
          oc_message_unref(message);
        }
      }
      else if (ev == oc_events[OUTBOUND_NETWORK_EVENT_ENCRYPTED])
      {
        // 1. handle OSCORE (mc/uc) s-mode messages, outgoing message are encrypted (received from OSCORE layer)
       
        if (message->endpoint.flags & OSCORE)
        {
          OC_DBG("Outbound OSCORE %s message, forwarding to IP layer", message->endpoint.flags & MULTICAST ? "multicast" : "unicast");
          
          #ifdef OC_REQUEST_HISTORY
          oc_coap_response_cache_store(message);
          #endif
          
          oc_send_buffer(message);
          oc_message_unref(message);
        }
      }
    }

  OC_PROCESS_END()
}