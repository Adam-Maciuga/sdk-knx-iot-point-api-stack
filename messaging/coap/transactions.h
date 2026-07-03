/*
// Copyright (c) 2016 Intel Corporation
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

#ifndef TRANSACTIONS_H
#define TRANSACTIONS_H

#include "coap.h"
#include "util/oc_etimer.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RFC 7252 §4.2 https://www.rfc-editor.org/rfc/rfc7252#section-4.2
 * Initial retransmit timeout = random(TIMEOUT, TIMEOUT * RANDOM_FACTOR).
 * BACKOFF_MASK is the modulo range (+1 for inclusive upper bound):
 *   interval = COAP_RESPONSE_TIMEOUT_TICKS + oc_random_value() % BACKOFF_MASK
 * yields a uniform random timeout in [TIMEOUT, TIMEOUT * RANDOM_FACTOR] seconds.
 * The +0.5f rounds to nearest integer before the (long) truncation.
 * float arithmetic avoids software-emulated 64-bit double on 32-bit targets.
 */
#define COAP_RESPONSE_TIMEOUT_TICKS (OC_CLOCK_SECOND * COAP_RESPONSE_TIMEOUT)
#define COAP_RESPONSE_TIMEOUT_BACKOFF_MASK (((COAP_RESPONSE_TIMEOUT_TICKS * \
           ((float)COAP_RESPONSE_RANDOM_FACTOR - 1.0f)) +  0.5f) + 1)

/**
   @brief Container for transactions with message buffer and retransmission info

   @note
   - a transaction is an individual CON/NON request/response cycle (example read/response cycle)
   - messages without token uses the MID,  such as an empty ACK response on a CON message, see CoAP RFC, Figure 20
   - messages with token
 */
typedef struct coap_transaction
{
  struct coap_transaction* next; 

  oc_message_t* message;
  uint8_t token[COAP_TOKEN_LEN];      // coap AL level: a client matches a request with a response 
  struct oc_etimer retransmit_timer; 
  struct oc_group_table_t* recipient; // optional recipient pointer for outbound s-mode unicast messages (oc_group_table_t*)
  uint8_t token_len;
  uint8_t retransmit_counter;         // 0 = initial message, no retransmission started
  uint16_t mid;                       // coap TL level: a client relates an out CON msg with an in ACK msg, a receiver ignores already received msg

} coap_transaction_t, transaction_t;

void coap_register_as_transaction_handler(void);

// creates a CoAP transaction + DOES NOT copy message data
coap_transaction_t* coap_new_transaction(uint16_t mid, const uint8_t *token, uint8_t token_len, oc_endpoint_t *endpoint);

// creates a CoAP transaction + DOES copy message data 1:1 (copies also type NON (uc/mc)/ CON (uc))
coap_transaction_t* coap_new_transaction_with_data(uint16_t mid, const uint8_t* token, uint8_t token_len, oc_message_t* s_mode_message);

void coap_send_transaction(coap_transaction_t *t);
void coap_clear_transaction(coap_transaction_t *t);

// returns ONLY coap CON transactions by mid
coap_transaction_t *coap_get_transaction_by_mid(uint16_t mid);
  
// returns ONLY coap CON transactions by token, note, returns also a non-null transaction if both token len's are '0';
coap_transaction_t *coap_get_transaction_by_token(const coap_packet_t* pkt);

// returns ANY transaction (CON/NON, w/wo s-mode), checks first mid then token, note, returns also a non-null transaction if both token len's are '0';
coap_transaction_t* get_any_transaction_by_token_or_mid(uint16_t mid, const uint8_t* token, uint8_t token_len);

void coap_check_transactions(void);
void coap_free_all_transactions(void);
void coap_free_transactions_by_endpoint(const oc_endpoint_t *endpoint);

#ifdef __cplusplus
}
#endif

#endif 
