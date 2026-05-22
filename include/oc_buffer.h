/*
// Copyright (c) 2016 Intel Corporation
// Copyright (c) 2024-2026 KNX Association
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
/**
  @brief CoAP message buffer implementation, e.g. for the payloads being transferred
  @file
*/
#ifndef OC_BUFFER_H
#define OC_BUFFER_H

#include "port/oc_connectivity.h"
#include "util/oc_process.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

OC_PROCESS_NAME(message_buffer_handler);

/**
 * @brief function to allocate a message
 *
 * @return oc_message_t* the allocated message
 */
oc_message_t *oc_allocate_message(void);

/**
 * @brief add (increase) reference (for tracking in use)
 *
 * @param message the message
 */
void oc_message_add_ref(oc_message_t *message);

/**
 * @brief decrease reference count for tracking in use, in case of reference count 
 *        is '0' the ->data ptr (payload stream) + message (from pool) are memory wise released
 *
 * @param message the message
 */
void oc_message_unref(oc_message_t *message);

/**
 * @brief receive (CoAP) message
 *
 * @param message the received message
 */
void oc_receive_message(oc_message_t *message);

/**
 * @brief send (CoAP) message by forwarding it to lower (OSCORE) layers,
 *        after finally sending the (plain/encrypted) message the message
 *        (if not tracked) will be 'released' for the ->data pointer (payload stream) and the message as such (pool)
 *
 * @param message the CoAP message
 */
void oc_send_message(oc_message_t *message);

/**
 * @brief close all tls sessions
 *
 */
void oc_close_all_tls_sessions(void);

#ifdef __cplusplus
}
#endif

#endif
