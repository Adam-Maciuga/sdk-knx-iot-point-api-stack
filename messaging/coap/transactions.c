/*
// Copyright (c) 2016 Intel Corporation
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
/*
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

#include "transactions.h"
#include "api/oc_main.h"
#include "observe.h"
#include "oc_buffer.h"
#include "util/oc_list.h"
#include "util/oc_memb.h"
#include <string.h>

#ifdef OC_BLOCK_WISE
#include "oc_blockwise.h"
#endif /* OC_BLOCK_WISE */

#ifdef OC_CLIENT
#include "oc_client_state.h"
#endif /* OC_CLIENT */

//#ifdef OC_SECURITY
#ifdef OC_OSCORE
#include "security/oc_tls.h"
#endif

/*---------------------------------------------------------------------------*/
OC_MEMB(transactions_memb, coap_transaction_t, COAP_MAX_OPEN_TRANSACTIONS);
OC_LIST(transactions_list);

static struct oc_process *transaction_handler_process = NULL;

/*---------------------------------------------------------------------------*/
/*- Internal API ------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
void coap_register_as_transaction_handler(void)
{
  transaction_handler_process = OC_PROCESS_CURRENT();
}


coap_transaction_t * coap_new_transaction(uint16_t mid, 
                                          uint8_t *token, uint8_t token_len, 
                                          oc_endpoint_t *endpoint)
{
  coap_transaction_t *t = oc_memb_alloc(&transactions_memb);
  if (t) {
    t->message = oc_internal_allocate_outgoing_message();
    if (t->message) {
      OC_DBG("Created new transaction %u: %p", mid, (void *)t);
      t->mid = mid;
      if (token_len > 0) {
        memcpy(t->token, token, token_len);
        t->token_len = token_len;
      }
      t->retrans_counter = 0;

      /* save client address */
      memcpy(&t->message->endpoint, endpoint, sizeof(oc_endpoint_t));

      oc_list_add(
        transactions_list,
        t); /* list itself makes sure same element is not added twice */
    } else {
      oc_memb_free(&transactions_memb, t);
      t = NULL;
    }
  } else {
    OC_WRN("insufficient memory to create transaction");
  }

  return t;
}

// (re)sends a message by 'transaction' and
// - NON-confirmable clears the transaction afterward
// - CON-confirmable MAY clear afterward the transaction (all reps done)
void coap_send_transaction(coap_transaction_t *t)
{
  if (!oc_main_initialized()) 
    return;

  #ifdef OC_DEBUG

  OC_DBG("sending transaction (len: %llu , mid %u)", t->message->length, t->mid);

  if (t == NULL) {
    OC_ERR("transaction == NULL");
  }
  if (t->message == NULL) {
    OC_ERR("message in transaction == NULL");
  }
  if (t->message->data == NULL) {
    OC_ERR("data in message in transaction == NULL");
  }

  #endif

  bool confirmable = COAP_TYPE_CON == (COAP_HEADER_TYPE_MASK & t->message->data[0]) >> COAP_HEADER_TYPE_POSITION ? true : false;

  #ifdef OC_TCP
  if (!(t->message->endpoint.flags & TCP) && confirmable) {
  #else 
  if (confirmable) 
  {
  #endif

    OC_DBG("send_transaction - CON message");

    if (t->retrans_counter < COAP_MAX_RETRANSMIT) 
    {
      
      OC_DBG("not timed out, keeping transaction %u: %p", t->mid, (void *)t);

      if (t->retrans_counter == 0) 
      {
        t->retrans_timer.timer.interval = COAP_RESPONSE_TIMEOUT_TICKS + oc_random_value() % (oc_clock_time_t)COAP_RESPONSE_TIMEOUT_BACKOFF_MASK;
        OC_DBG("interval initialized %d", (int)t->retrans_timer.timer.interval);
      }
      else 
      {
        t->retrans_timer.timer.interval <<= 1;
        OC_DBG("interval doubled %d", (int)t->retrans_timer.timer.interval);
      }

      OC_PROCESS_CONTEXT_BEGIN(transaction_handler_process);
      oc_etimer_restart(&t->retrans_timer); // interval updated above
      OC_PROCESS_CONTEXT_END(transaction_handler_process);

      oc_message_add_ref(t->message);
      coap_send_message(t->message);
    }
    else 
    {
      OC_WRN("removing transaction (timed out) with mid %u", t->mid);
      #ifdef OC_SERVER
      coap_remove_observer_by_client(&t->message->endpoint);
      #endif

      #ifdef OC_CLIENT
      oc_ri_free_client_cbs_by_mid(t->mid);
      #endif 

      #ifdef OC_BLOCK_WISE
      oc_blockwise_scrub_buffers(false);
      #endif
      #ifdef OC_SECURITY
      if (t->message->endpoint.flags & SECURED) {
        oc_tls_close_connection(&t->message->endpoint);
      } else
      #endif 
      {
        coap_clear_transaction(t);
      }
    }
  }
  else 
  {
    OC_DBG("send_transaction - NON message");

    // add ref
    oc_message_add_ref(t->message);

    coap_send_message(t->message);

    // removes also the ref 
    coap_clear_transaction(t);
  }
}

void coap_clear_transaction(coap_transaction_t *t)
{
  if (t) 
  {
    OC_DBG("freeing transaction for MID %u", t->mid);

    oc_etimer_stop(&t->retrans_timer);
    oc_message_unref(t->message);
    oc_list_remove(transactions_list, t);
    oc_memb_free(&transactions_memb, t);
  }
}

coap_transaction_t * coap_get_transaction_by_mid(uint16_t mid)
{
  for (coap_transaction_t* t = oc_list_head(transactions_list); t; t = t->next)
  {
    if (t->mid == mid) 
    {
      OC_DBG("found transaction for MID %u", t->mid);
      return t;
    }
  }
  return NULL;
}

coap_transaction_t * coap_get_transaction_by_token(uint8_t *token, uint8_t token_len)
{
  for (coap_transaction_t* t = oc_list_head(transactions_list); t; t = t->next) 
  {
    if (t->token_len == token_len && memcmp(t->token, token, token_len) == 0) 
    {
      OC_DBG("found transaction by token %p", (void *)t);
      return t;
    }
  }
  return NULL;
}

void coap_check_transactions(void)
{
  coap_transaction_t *t = oc_list_head(transactions_list);
  while (t) 
  {
    // save next
    coap_transaction_t* next = t->next;

    if (oc_etimer_expired(&t->retrans_timer)) 
    { // expired

      // increase attempts 
      ++(t->retrans_counter);

      OC_DBG("retransmitting MID %u with attempt (%u)", t->mid, t->retrans_counter);
      coap_send_transaction(t);

      const int removed = oc_list_length(transactions_list);

      if (removed - oc_list_length(transactions_list) > 1) 
      {

        // list is not empty maybe there is transaction needs to be resent, restart the list 
        t = (coap_transaction_t *)oc_list_head(transactions_list);
        continue;
      }
    }
    // restore next
    t = next;
  }
}

void coap_free_all_transactions(void)
{
  coap_transaction_t *t = oc_list_head(transactions_list);
  while (t) 
  {
    // get next 
    coap_transaction_t* next = t->next;
    // clear current 
    coap_clear_transaction(t);
    // restore next 
    t = next;
  }
}

void coap_free_transactions_by_endpoint(oc_endpoint_t *endpoint)
{
  coap_transaction_t *t = oc_list_head(transactions_list);

  while (t) 
  {
    // save next
    coap_transaction_t* next = t->next;
    if (oc_endpoint_compare(&t->message->endpoint, endpoint) == 0) 
    {
      const int removed = oc_list_length(transactions_list);

      #ifdef OC_CLIENT

      // remove the client callback tied to this transaction
      oc_ri_free_client_cbs_by_mid(t->mid);

      #endif 

      if (removed - oc_list_length(transactions_list) > 0) 
      {
        // list is not empty maybe there is another transaction for the same endpoint, restart the list 
        t = (coap_transaction_t *)oc_list_head(transactions_list);
        continue;
      }
      // clear found transaction 
      coap_clear_transaction(t);
    }
    // restore next
    t = next;
  }
}
