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

#ifndef ENGINE_H
#define ENGINE_H

#include "coap.h"
#include "observe.h"
#include "separate.h"
#include "transactions.h"

#ifdef __cplusplus
extern "C" {
#endif

OC_PROCESS_NAME(coap_engine);

void coap_init_engine(void);

/**
 * @brief Check if a coap inbound replayed message is pending, if not the inbound message is registered
 *        as a new message in the history buffer and passed through to the 'upper' layers. 
 *        Outdated entries are wiped out from history with OC_REQUEST_HISTORY_TIMEOUT.  
 *        More details read the 'note'
 *
 * @note A replay is an inbound UDP coap telegram, addressing the same endpoint (IPv6 address,
 *       port and MID). A server receiving the same message again with this MID does echo back a cached response. 
 *       
 *       CON scenario
 *       - client -> CON -> server ->  (piggybacked) ACK -> x -> client = CON message repeated by client with SAME MID due to not received (piggybacked) ACK
 *
 *       NON scenario
 *       - client -> NON -> server ->  network duplication -> NON -> server = rejected
 *
*/
bool oc_coap_check_if_duplicate_and_if_not_add_to_history(const coap_packet_t* coap, const oc_endpoint_t* endpoint);

// clear the request history buffer (ONLY on device reset with erase code 2)
void oc_coap_clear_request_history(void);

/**
 * @brief Check if the inbound message is a 1:1 mirrored (loopback) message send by myself, 
 *        by checking the endpoint IPv6 address and port (yes : return true, no : return false). 
 *      
 * @note 
 *       - Loopback messages may pop up here on a used loopback/localhost adapter. 
 *         The total amount depends on how many endpoints (IP addresses) are registered by the device.
 *
 *       - When sending an uc/mc 'write' request, the internal update of other linked GO's is NOT done with
 *         the 1:1 replayed IPv6 message. The updates are performed internally on the write method, see 
 *         'oc_send_s_mode_mc_or_uc_message'
 *
 */
bool oc_coap_check_if_loopback_message(const oc_message_t* msg);

/**
 * @brief Send a coap 4 byte ACK + EMPTY_0_00 response.
 *
 * @note
 * - incoming CON msg -> outgoing ACK msg with code 'EMPTY_0_00' , incoming mid
 *
 * @param mid message id (mid)
 * @param endpoint addressed inbound endpoint
 *
 * @return true if the ACK was sent, false if not (e.g. because of a no memory)
 *
 */
bool coap_send_response_with_empty_ack(uint16_t mid, const oc_endpoint_t* endpoint);

/**
 * @brief Store an outbound ACK response in the response cache for CON retransmission handling.
 *
 * When a client retransmits a CON request (because the ACK response was lost),
 * the cached response is re-sent instead of silently dropping the duplicate.
 * Only ACK responses (CoAP type 2) are cached. (Piggybacked responses are cached, but no empty ACKs, neither Separate responses.)
 *
 * The message is kept alive via oc_message_add_ref(); no data copy is made.
 *
 * @param message the outgoing message (wire-ready bytes + endpoint)
 */
void oc_coap_response_cache_store(oc_message_t* message);

#ifdef __cplusplus
}
#endif

#endif 
