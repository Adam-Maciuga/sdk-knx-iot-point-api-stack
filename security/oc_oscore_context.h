/*
 * Copyright (c) 2020 Intel Corporation
 * Copyright (c) 2022-2023 Cascoda Ltd.
 * Copyright (c) 2024-2026 KNX Association
 *            
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OC_OSCORE_CONTEXT_H
#define OC_OSCORE_CONTEXT_H

#include "messaging/coap/oscore_constants.h"
#include "api/oc_knx_sec.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 *
 * @brief Oscore context information as data for the encryption/decryption,
 *        created from an auth/at entry. The structure has a dual use for sender
 *        context and recipient context, details see oscore engine - SECURITY DETAILS. 
 *
 */
typedef struct oc_oscore_context_t
{
  struct oc_oscore_context_t *next;                 /**< pointer to the next, NULL if there is not any */
  const oc_auth_at_t* auth_at;                      /**< pointer to the access token entry from AT table, that was used to decrypt a received message */
  uint8_t master_secret[OSCORE_MASTER_SECRET_LEN];  /**< OSCORE master secret */
  
  uint8_t sender_id[OSCORE_SENDER_ID_LEN];          /**< 'Sender ID' (in OSCORE) */
  uint8_t sender_id_len;                            /**< 'Sender ID' (in OSCORE) length */

  uint8_t recipient_id[OSCORE_SENDER_ID_LEN];       /**< 'Recipient ID' (in OSCORE) */
  uint8_t recipient_id_len;                         /**< 'Recipient ID' (in OSCORE) length */

  uint8_t id_context[OSCORE_ID_CONTEXT_LEN];        /**< 'ID Context' (in OSCORE) */
  uint8_t id_context_len;                           /**< 'ID Context' (in OSCORE) length */

  uint64_t ssn;                                     /**< sender sequence number */
  oc_clock_time_t last_used;                        /**< time of last use, for runtime caching of recipient contexts */

  // derived parameters
  uint8_t sender_key[OSCORE_KEY_LEN];               /**< 128-bit sender key */
  uint8_t recipient_key[OSCORE_KEY_LEN];            /**< 128-bit recipient key */
  uint8_t common_iv[OSCORE_COMMON_IV_LEN];          /**< Common IV */
  
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
int oc_oscore_context_derive_param(
        const uint8_t *id, uint8_t id_len,
        const uint8_t *id_ctx, uint8_t id_ctx_len, 
        const char *type, 
        const uint8_t *secret, uint8_t secret_len, 
        const uint8_t *salt, uint8_t salt_len, 
        const uint8_t *param, uint8_t param_len);

void oc_oscore_free_context(oc_oscore_context_t *ctx);

/**
 * @brief free all OSCORE sender and recipient contexts
 *
 */
void oc_oscore_free_all_contexts(void);

/**
 * @brief free all Request/Response Sender Contexts
 *
 * @note releases any context if it is not used as any "Recipient Context"
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
 * @param auth_at_entry the access token entry
 */
void oc_oscore_free_contexts_at_id(const oc_auth_at_t* auth_at_entry);

/**
 * @brief Parameters for creating an OSCORE context.
 *        Used with oc_oscore_add_context, oc_oscore_add_sender_context and oc_oscore_add_recipient_context.
 */
typedef struct oc_oscore_context_params_t {
  const uint8_t* sender_id;     /**< Sender ID (NULL for a recipient-only context) */
  const uint8_t* recipient_id;  /**< Recipient ID (NULL for a sender-only context) */
  uint64_t ssn;                 /**< Sender sequence number */
  const uint8_t* id_context;    /**< ID Context */
  const oc_auth_at_t* auth_at;  /**< Pointer to the access token entry in the AT table */
  uint8_t id_context_size;      /**< Length of ID Context */
  uint8_t sender_id_size;       /**< Length of sender_id */
  uint8_t recipient_id_size;    /**< Length of recipient_id */
  bool read_ssn_from_storage;   /**< If true, an offset is added to the SSN, otherwise not */
} oc_oscore_context_params_t;

/**
 * @brief creates an OSCORE context (e.g. the internal structure for encoding/decoding)
 *
 * @param params pointer to the context parameters struct
 * @return != NULL context can be used for encryption/decryption, else not
 */
oc_oscore_context_t* oc_oscore_add_context(const oc_oscore_context_params_t* params);

/**
 * @brief creates an OSCORE recipient context by filling 'oc_oscore_add_context' with 'Recipient ID' ('Sender ID' is NULL/0)
 *
 **/
oc_oscore_context_t* oc_oscore_add_recipient_context(oc_oscore_context_params_t* params);

/**
 * @brief creates an OSCORE sender context by filling 'oc_oscore_add_context' with 'Sender ID' ('Recipient ID' is NULL/0)
 *
 **/
oc_oscore_context_t* oc_oscore_add_sender_context(oc_oscore_context_params_t* params);

/**
 * @brief Free the least recently used recipient context
 *
 * The use times are updated when the contexts are created or found using the find_context_by_* functions
 *
 */
void oc_oscore_free_lru_recipient_context(void);

oc_oscore_context_t *oc_oscore_find_context_by_group_address(uint32_t group_address);

// inputs are checked against the own Recipient Contexts (RID)
oc_oscore_context_t *oc_oscore_find_context_by_kid_and_kid_context(uint8_t *kid, uint8_t kid_len, uint8_t *kid_ctx, uint8_t kid_ctx_len);

// searches a context by token/ mid of inbound response, from a former (OBSERVE) request by a client (inputs are checked against the contexts INBOUND access token)
oc_oscore_context_t* oc_oscore_find_context_by_token_mid(const coap_packet_t* pkt, uint8_t** request_piv, uint8_t* request_piv_len, bool tcp);

#ifdef __cplusplus
}
#endif

#endif
