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
 *
 * This file is part of the Contiki operating system.
 */

#ifndef OBSERVE_H
#define OBSERVE_H

#include "coap.h"
#include "oc_ri.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
	Observer entry -- fields ordered by descending alignment to minimize padding.
	Used by the CoAP observe mechanism (RFC 7641) and KNX IoT subscriptions (clause 2.5.9).
*/
typedef struct coap_observer_t
{
	struct coap_observer_t* next;    // linked list pointer (for LIST)

	// --- 8-byte aligned: pointers, oc_string_t, ... ---
	const oc_resource_t* resource;
	oc_string_t url;               // observed resource URI
	oc_endpoint_t endpoint;        // client endpoint (IP + port)
	oc_clock_time_t created;       // timestamp when observer was created (for lifetime expiry)

	// --- 4-byte aligned: uint32_t, int32_t, enums ---
	uint32_t obs_counter;          // RFC 7641: 24-bit sequence number for notifications 
	uint32_t lifetime;             // "lt" query parameter in seconds, 0 = not set

	// --- 2-byte aligned: uint16_t ---
	uint16_t last_mid;
	#ifdef OC_BLOCK_WISE
	uint16_t block2_size;
	#endif 

	// --- 1-byte: uint8_t, bool ---
	uint8_t token_len;
	uint8_t token[COAP_TOKEN_LEN];
	bool use_con;                  // true = CON (default per KNX), false = NON (when "non=true" is present in registration request)
} coap_observer_t;

	int coap_remove_observer_by_client(const oc_endpoint_t* endpoint);
	int coap_remove_observer_by_token(const oc_endpoint_t* endpoint, const uint8_t* token, size_t token_len);
	int coap_remove_observer_by_mid(const oc_endpoint_t* endpoint, uint16_t mid);
	int coap_remove_observer_by_resource(const oc_resource_t* rsc);
	void coap_free_all_observers(void);
	

#ifdef OC_BLOCK_WISE
int coap_observe_handler(void* request, void* response, const oc_resource_t* resource, uint16_t block2_size, oc_endpoint_t* endpoint);
#else
int coap_observe_handler(void* request, void* response, const oc_resource_t* resource, oc_endpoint_t* endpoint);
#endif

int coap_remove_observers_on_dos_change(bool reset);

/**
 * @brief Notify /k observers (KNX clause 2.5.9.1)
 *        Each observer gets the original CBOR payload forwarded as a notification.
 *        Expired observers are removed before sending.
 *
 * @param payload inbound CBOR payload bytes
 * @param payload_len length of payload
 *

  @note  
   - notifies on /k observers on self-triggered outbound application s-mode POST 'w' request 
   - does not notify on /k for inbound POST s-mode request
   - does not notify on /k for inbound POST/PUT request
*/
void coap_notify_k_observers(const uint8_t* payload, size_t payload_len);


/**
 * @brief Notify /p and 'core res.' observers (KNX clause 2.5.9.1).
 *        Each observer gets the fetched resource original raw CBOR payload forwarded as a notification.
 *        Expired observers are removed before sending.
 *
 * @param resource the notification resource
 * @param response_buf inbound CBOR payload bytes in the response buffer for the notification, 
 *                     if NULL an internal buffer will be used/filled by calling the GET handler of the resource, 
 *                     so the notification will contain the current state of the resource
 * @param endpoint the client endpoint to notify, if NULL , all observers of the resource will be notified
 *

  @note
   - notifies on /p observers on self-triggered outbound application s-mode PUT/POST 'w' request
   - notifies on /p on inbound PUT/POST request
   - does not notify on /p on inbound POST s-mode request

*/
int coap_notify_observers(const oc_resource_t* resource, oc_response_buffer_t* response_buf, const oc_endpoint_t* endpoint);

// get current observe counter value (will be incremented on each new observer and each notification)
uint32_t get_observe_counter(void);

#ifdef __cplusplus
}
#endif

#endif 
