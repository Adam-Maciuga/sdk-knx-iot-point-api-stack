/*
// Copyright (c) 2023 Cascoda Ltd
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
#ifndef OC_REPLAY_H
#define OC_REPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "oc_helpers.h"
#include "oc_buffer.h"

typedef enum replay_state
{
  SYNCED = 0,   // replay window present Y   -> pass msg to AL
  REPLAY = 1,   // replay window present Y   -> 4.01 
  ECHO   = 2,   // replay window present Y+N -> 4.01 + ECHO 
} replay_state_t;


/**
 * @brief Add a synchronised client
 *
 * If a client with the same KID & KID_CTX already exists, it will be
 * reinitialized and marked as in sync (with the new SSN + cleaned window),
 * otherwise a new record will be set up.
 *
 * For a reinitialized record it means also that older SSNs from the old SSN/window 
 * from now on are ignored.    
 *
 * @param rx_ssn Sender Sequence Number of newly received OSCORE request
 * @param rx_kid Key Identifier of received request
 * @param rx_kid_ctx Key ID Context of received request
 */
void oc_replay_add_client(const uint64_t rx_ssn, const oc_string_t rx_kid, const oc_string_t rx_kid_ctx);

/**
 * @brief Check if a client is synchronised
 *
 * If the client is synchronised, this function also updates its entry with the
 * new SSN. Thus, the replay window is updated 'in the background', through the
 * natural usage of this function.
 *
 * @param rx_ssn Sender Sequence Number of newly received OSCORE request
 * @param rx_kid Key Identifier of received request
 * @param rx_kid_ctx Key ID Context of received request
 * @return Either client is synchronised (you may accept the frame with the given SSN)
 * or it is not synchronised (either you challenge the frame or deny it completely)
 */
replay_state_t oc_replay_check_client(uint64_t rx_ssn, oc_string_t rx_kid,
                                       oc_string_t rx_kid_ctx);

/**
 * @brief Mark a message to be retained for retransmission
 *
 * The message is retained using a soft reference - it will not be freed unless
 * the stack runs out of buffers, or after a timeout.
 *
 * If static message buffers are used, this can lead to a constrained client
 * having to drop messages that are otherwise preserved for echo
 * retransmissions, if many requests are being sent out in a short period of
 * time.
 *
 * Messages that need to be retransmitted are identified by the token of 4.01
 * Unauthorised requests with an Echo option which must be included in the
 * retransmitted request.
 *
 * @param msg the message to be retained
 * @param token_len the length of the message's token
 * @param token the token, used for identifying the message
 */
void oc_replay_message_track(struct oc_message_s *msg, uint16_t token_len, const uint8_t *token);

/**
 * @brief Free a message that was previously marked with
 * oc_replay_message_track()
 *
 * @param msg pointer to the message buffer
 */
void oc_replay_message_unref(struct oc_message_s *msg);

/**
 * @brief Find a previously tracked message and mark it as no longer tracked
 *
 * The soft reference will be removed. If this is the last remaining reference
 * to the message, the message will be freed.
 *
 * @param token_len Length of the token
 * @param token Token used to identify the message
 * @return struct oc_message_s*
 */
struct oc_message_s *oc_replay_find_msg_by_token(uint16_t token_len,
                                                 const uint8_t *token);

/**
 * @brief Get the first available (not used) record
 *
 * @return oc_replay_record*
 */
static struct oc_replay_record *get_empty_record(void);

#ifdef __cplusplus
}
#endif

#endif // OC_REPLAY_H