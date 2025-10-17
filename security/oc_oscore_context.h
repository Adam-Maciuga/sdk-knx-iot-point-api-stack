/*
// Copyright (c) 2020 Intel Corporation
// Copyright (c) 2022-2023 Cascoda Ltd
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

#ifndef OC_OSCORE_CONTEXT_H
#define OC_OSCORE_CONTEXT_H

#include "messaging/coap/oscore_constants.h"
#include "oc_helpers.h"
#include "port/oc_clock.h"
#include "oc_uuid.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Replay window type definition
 *
 */
typedef struct oc_rwin_t
{
  /**
   * @brief Sender Sequence Number
   */
  uint64_t ssn;
  /**
   * @brief Sender Address, usually the IPv6 source address of the sending
   * device
   */
  uint8_t sender_address[16];
  /**
   * @brief  Destination Address, usually an S-mode multicast address
   */
  uint8_t destination_address[16];
} oc_rwin_t;

/**
  @brief Oscore context information as data for the encryption/decryption, created from an auth/at entry.
 
  @note An oscore context shares the client and server side context
   - Message  | Client            | Server            | Derived Key 
   - Request  | Sender Context    | Recipient Context | Request Key
   - Response | Recipient Context | Sender Context    | Response Key

  The structure has a dual use, as sender context and recipient context. 

 */
typedef struct oc_oscore_context_t
{
  struct oc_oscore_context_t *next;                 // pointer to the next, NULL if there is not any
  int auth_at_index;                                // access token index from AT table, that was used to decrypt a received message 
  uint8_t master_secret[OSCORE_MASTER_SECRET_LEN];  // OSCORE master secret
  
  uint8_t sender_id[OSCORE_SENDER_ID_LEN];          // OSCORE Sender ID
  uint8_t sender_id_len;                            // length
  uint8_t recipient_id[OSCORE_SENDER_ID_LEN];       // OSCORE Recipient ID
  uint8_t recipient_id_len;                         // length
  uint64_t ssn;                                     // sender sequence number
  uint8_t id_context[OSCORE_ID_CONTEXT_LEN];        // OSCORE ID Context
  uint8_t id_context_len;                           // length

  // derived parameters
  uint8_t sender_key[OSCORE_KEY_LEN];               // 128-bit sender key 
  uint8_t recipient_key[OSCORE_KEY_LEN];            // 128-bit recipient key
  uint8_t common_iv[OSCORE_COMMON_IV_LEN];          // Common IV
  oc_clock_time_t last_used;                        // time of last use, for runtime caching of recipient contexts
} oc_oscore_context_t;

/**
 * @brief creates an OSCORE data
 *
 * @param id the OSCORE identifier
 * @param id_len the length of the OSCORE identifier
 * @param id_ctx the OSCORE context identifier
 * @param id_ctx_len the length of the OSCORE context identifier
 * @param type  the type of context
 *
 * @param secret the OSCORE master secret
 * @param secret_len the length of the OSCORE master secret
 * @param salt the salt to be used
 * @param salt_len the length of the salt
 * @param param the parameters
 * @param param_len the length of the parameters
 *
 * @return true parameters derived (installed, e.g. can be used for
 * encryption/decryption)
 * @return false parameters NOT derived (NOT installed)
 */
int oc_oscore_context_derive_param(const uint8_t *id, uint8_t id_len,
                                   uint8_t *id_ctx, uint8_t id_ctx_len,
                                   const char *type, uint8_t *secret,
                                   uint8_t secret_len, uint8_t *salt,
                                   uint8_t salt_len, uint8_t *param,
                                   uint8_t param_len);

void oc_oscore_free_context(oc_oscore_context_t *ctx);

/**
 * @brief free all OSCORE sender and recipient contexts
 *
 */
void oc_oscore_free_all_contexts(void);

/**
 * @brief free all OSCORE sender contexts
 *
 * @note sender context is released only if recipient context is not used
 *
 */
void oc_oscore_free_sender_contexts(void);

/**
 * @brief Free OSCORE context information with a given auth_at index
 *        and removes the entry from the linked list of OSCORE context
 *        information entries.
 *
 * @note deletes all security context that are referring to the given at token
 *
 * @param auth_at_index the index
 */
void oc_oscore_free_contexts_at_id(int auth_at_index);

/**
 * @brief creates an OSCORE context (e.g. the internal structure for encoding/decoding
 *
 * Note: OSCORE context is also a field.
 *
 *
 * @param sender_id the Sender ID (SID)
 * @param sender_id_size the length of Sender ID
 * @param recipient_id the Recipient ID (RID)
 * @param recipient_id_size the length of Recipient ID
 * @param ssn  the sender sequence number

 * @param mastersecret the OSCORE master secret
 * @param mastersecret_size the length of the OSCORE master secret
 * @param salt the salt
 * @param salt_size the length of the salt
 * @param id_context the ID Context
 * @param id_context_size the length of the ID Context
 * @param auth_at_index index in the auth at table -1.
 * @param read_ssn_from_storage initialize ssn with an offset (details see code comments) from storage
 *
 * @return != NULL context can be used for encryption/decryption, else not
 */
oc_oscore_context_t* oc_oscore_add_context(
  const char *sender_id, int sender_id_size,
  const char* recipient_id, int recipient_id_size,
  uint64_t ssn,
  const char *mastersecret, int mastersecret_size, 
  const char *salt, int salt_size, 
  const char* id_context, int id_context_size, 
  int auth_at_index,
  bool read_ssn_from_storage);

/**
 * @brief Free the least recently used recipient context
 *
 * The use times are updated when the contexts are created or found using the
 * find_context_by_* functions
 *
 */
void oc_oscore_free_lru_recipient_context(void);

oc_oscore_context_t *oc_oscore_find_context_by_group_address(uint32_t group_address);

oc_oscore_context_t *oc_oscore_find_context_by_kid(
  uint8_t *kid,
  uint8_t kid_len);

oc_oscore_context_t *oc_oscore_find_context_by_kid_and_kid_context(
  uint8_t *kid, uint8_t kid_len,
  uint8_t *kid_ctx, uint8_t kid_ctx_len);

oc_oscore_context_t *oc_oscore_find_context_by_token_mid(
  uint8_t *token, uint8_t token_len, uint16_t mid,
  uint8_t **request_piv, uint8_t *request_piv_len, bool tcp);

oc_oscore_context_t *oc_oscore_find_context_by_oscore_id(char *oscore_id,
                                                         size_t oscore_id_len);
#ifdef __cplusplus
}
#endif

#endif /* OC_OSCORE_CONTEXT_H */
