/* 
 * Copyright (c) 2020 Intel Corporation
 * Copyright (c) 2024-2026 KNX Association
 *            
 * SPDX-License-Identifier: Apache-2.0
 */
#include <inttypes.h>
#include "api/oc_events.h"
#include "api/oc_knx_sec.h"
#include "messaging/coap/engine.h"
#include "messaging/coap/transactions.h"
#include "port/oc_storage.h"
#include "oc_client_state.h"
#include "oc_oscore_context.h"
#include "oc_oscore_crypto.h"
#ifdef KNX_TCP_TLS
#include "oc_tls.h"
#endif
#include "util/oc_process.h"

OC_PROCESS(oc_oscore_handler, "OSCORE Process");

// parses (and assign if found) the oscore message outer options, checks if it is an OSCORE message (packet is wiped with '0' inside method)
static coap_status_t oscore_parse_outer_message(oc_message_t* msg, coap_packet_t* packet)
{
  // init with '0'
  memset(packet, 0, sizeof(coap_packet_t));

  // set coap pointer to message data pointer
  packet->buffer = msg->data;
  uint8_t* current_option = NULL;

#ifdef OC_TCP
  if (msg->endpoint.flags & TCP)
  {
    coap_pkt->transport_type = COAP_TRANSPORT_TCP;
    // parse header fields
    size_t message_length = 0;
    uint8_t num_extended_length_bytes = 0;
    coap_tcp_parse_message_length(msg->data, &message_length, &num_extended_length_bytes);

    coap_pkt->type = COAP_TYPE_NON;
    coap_pkt->mid = 0;
    coap_pkt->code = coap_pkt->buffer[1 + num_extended_length_bytes];

    current_option = msg->data + COAP_TCP_DEFAULT_HEADER_LEN + num_extended_length_bytes;
  }
  else
#endif
  {
    packet->transport_type = COAP_TRANSPORT_UDP;
    packet->version = (COAP_HEADER_VERSION_MASK & packet->buffer[0]) >> COAP_HEADER_VERSION_POSITION;
    packet->type = (coap_message_type_t)((COAP_HEADER_TYPE_MASK & packet->buffer[0]) >> COAP_HEADER_TYPE_POSITION);
    packet->mid = (uint16_t)(packet->buffer[2] << 8 | packet->buffer[3]);
    packet->code = packet->buffer[1];
    packet->token_len = (COAP_HEADER_TOKEN_LEN_MASK & packet->buffer[0]) >> COAP_HEADER_TOKEN_LEN_POSITION;

    current_option = msg->data + COAP_HEADER_LEN;
  }

#ifdef OC_DEBUG

  print_coap_service(packet->code, "outer coap code");

#endif

  const bool is_ack = packet->type == COAP_TYPE_ACK;
  const bool is_ack_with_empty_payload = is_ack && packet->code == EMPTY_0_00;


  if (is_ack_with_empty_payload)
  {
    OC_WRN("CoAP EMPTY ACK with 'zero' payload can't be a valid OSCORE message");
    return BAD_REQUEST_4_00;
  }

  if (packet->version != 1)
  {
    OC_WRN("CoAP version must be 1");
    return BAD_REQUEST_4_00;
  }

  // token
  if (packet->token_len > COAP_TOKEN_LEN)
  {
    OC_WRN("Token Length must not be more than 8");
    return BAD_REQUEST_4_00;
  }

  memcpy(packet->token, current_option, packet->token_len);
  OC_DBG_OSCORE("Token len %u : ", packet->token_len);
  OC_LOGbytes(packet->token, packet->token_len);

  current_option += packet->token_len;

  // parse outer options of 'decrypted' message, any present - not allowed - outer option causes a 4.02
  const coap_status_t ret = coap_oscore_parse_options(packet, msg->data, (uint32_t)msg->length, current_option, false, true, true);

  OC_INF("coap parse oscore outer options : %s", ret == COAP_NO_ERROR ? "ok" : "failed");
  return ret;
}

// parses (and assign if found) the oscore message inner options and inner coap code (packet is NOT wiped with '0' inside method)
static coap_status_t oscore_parse_inner_message(uint8_t* data, size_t data_len, coap_packet_t* packet)
{
  /*
    (a) don't init entire coap packet with '0', a present coap package is NOW used for parsing the inner message,
        so that already parsed (outer) options (e.g. kid, kid_context,...) are kept

    (b) set payload len back to zero, since previous payload len was calculated based on the outer message, and inner message may have different payload
        len (e.g. due to different options, or even payload len = 0 if no application payload is present in inner message) 
  */

  // set coap pointer to message payload data (inner options, payload)
  packet->buffer = data;
  
  // (b)
  packet->payload = NULL;
  packet->payload_len = 0;

  // inner coap code + inner option from (inner) plaintext message, see https://www.rfc-editor.org/rfc/rfc8613#section-5.3
  packet->code = data[0];
  uint8_t* inner_options = &data[1];

  #ifdef OC_DEBUG

  print_coap_service(packet->code, "inner coap code");

  #endif

  // parse inner options of 'decrypted' message, any present - not allowed - inner option causes a 4.02
  const coap_status_t ret = coap_oscore_parse_options(packet, data, (uint32_t)data_len, inner_options, true, false, false);

  OC_INF("coap parse oscore inner options : %s", ret == COAP_NO_ERROR ? "ok" : "failed");
  return ret;
}

// ssn++ and save to storage each 32 - value (lays within replay window ...)
static void increment_ssn_in_context(oc_oscore_context_t* ctx)
{
  ctx->ssn++;

  if (ctx->ssn % OSCORE_SSN_WRITE_FREQ_K == 0)
  {

    // TODO Recipient ID + ID Context also saved ? (to not always on startup issue an echo challenge)

    /*

      store current SSN with frequency OSCORE_WRITE_FREQ_K,
      based on recommendations in RFC 8613, appendix B.1. to prevent SSN reuse

      save ssn per (hex) sender id and (hex) id context as storage name 'ssn' + sender id + id context
      - sender id  -> with max 14 ascii chars (hex coded)
      - id context -> with max 32 ascii chars (hex coded) - may be empty

      example: for sender id 0001 and id context 200105b7fd8e = 'ssn_0001_200105b7fd8e'
     */
    char storage_name[OSCORE_STORAGE_KEY_LEN] = {OSCORE_STORAGE_PREFIX};
    size_t storage_name_len;

    // claim that first buffer is big enough, is used in method below
    storage_name_len = sizeof(storage_name) - OSCORE_STORAGE_PREFIX_LEN;

    // add 'id' behind prefix that is something like 'ssn_'
    oc_conv_byte_array_to_hex_string(ctx->sender_id, ctx->sender_id_len,
                                     // buffer start ptr, to paste in the hex coded sender id
                                     storage_name + OSCORE_STORAGE_PREFIX_LEN, &storage_name_len);

    // append divider ( -1 to overwrite the '\0')
    storage_name[OSCORE_STORAGE_PREFIX_LEN + storage_name_len - 1] = '_';

    // claim that buffer is big enough, is used in method below ( +1 to include the foreseen sting end '\0' placeholder)
    storage_name_len = sizeof(storage_name) - OSCORE_STORAGE_PREFIX_LEN - OSCORE_STORAGE_DIVIDER_LEN - storage_name_len + 1;

    // add 'context' behind the 'id'
    oc_conv_byte_array_to_hex_string(
      ctx->id_context, ctx->id_context_len,
      // buffer start ptr, to paste in the hex coded id context ( + 1 to skip divider)
      storage_name + (OSCORE_STORAGE_PREFIX_LEN + ctx->sender_id_len * 2 + OSCORE_STORAGE_DIVIDER_LEN), &storage_name_len);

    oc_storage_write(storage_name, (uint8_t*)&ctx->ssn, sizeof(ctx->ssn));
  }
}

/*
  SECURITY DETAILS
  ================

  GENERAL RULE
  ============
  For an incoming secure request or response, if I can decode it successfully,
  my answer is always secure.

  MESSAGE
  =======
  An oscore context composes Client (1) A1/B1 contexts or Server (2) A2/B2 contexts.

  +-----------------------+   +-----------------------+
  |  A1 Sender Context    | = | A2 Recipient Context  |
  +-----------------------+   +-----------------------+
  |  B1 Recipient Context | = | B2 Sender Context     |
  '-----------------------'   '-----------------------'

  # | Client (1)                                     |    Message      | Server (2)
  A | (8.1) Sender Context (sender id + s-key)       | -> Request  ->  | (8.2) Recipient Context (recipient id + r-key)
  B | (8.4) Recipient Context (recipient id + r-key) | <- Response <-  | (8.3) Sender Context (sender id + s-key)

  Sender Contexts
  - have a MaC populated Sender ID and NO Receiver ID
  - posts to auth/at set the Sender ID + ID Context, so the created contexts are only usable for sending

  Recipient Contexts
  - are dynamically created with the 'osc:contextid' key, from the access token matching with 'kid' and 'kid_context' from received request
  - the 'pase' key, however, is for receiving only, and the 'osc:contextid' is already inside receiver_id.

  A1: client composing a request
      - OSCORE 8.1 https://www.rfc-editor.org/rfc/rfc8613.html#section-8.1
      - 'oc_oscore_send_unicast_message',
      - 'oc_oscore_send_multicast_message'

  A2: server receiving a request
      - OSCORE 8.2 https://www.rfc-editor.org/rfc/rfc8613.html#section-8.2
      - 'oc_oscore_receive_message'

  B2: server composing a response (from A2)
      - OSCORE 8.3 https://www.rfc-editor.org/rfc/rfc8613.html#section-8.3
      - 'oc_oscore_receive_message'

        from 8.2 step 8     -> decryption successful, process message further in CoAP layer,
                               respond later on secured
        from 8.2 step 2/3/6 -> decryption failed, respond with 4.x

        4.01 -  unsecured, kid not present
        4.02 -  unsecured, oscore coap option (9) wrong (outer)
        4.02 -  coap option from RFC 5.4.1 and critical (odd) and unknown and
                inner = secured   (usually all other options)
                outer = unsecured (usually ONLY the OSCORE option, optionally proxy, max age)

        4.02    unsecured, Recipient Context present but decryption failed

        ignore - coap option from RFC 5.4.1 and elective (even) and unknown


  B1: client processing a received response
      - OSCORE 8.4 https://www.rfc-editor.org/rfc/rfc8613.html#section-8.4
      - 'oc_oscore_receive_message'

        -> step 1     ignore outer class E options
        -> step 2..5  decrypt
        -> step 6     reconstruct org. message
        -> step 7     decryption successful, process message further in CoAP layer,
                      no own response for a received response
        -> step 8     decryption failed, stop processing and ignore

  STEPS
  =====

  Send a message
  =================

  Sender -> Message -> Receiver
  =============================

    Sender (IA 2.0.1)
    -----------------
      Access Token (configured by MaC)
      - osc.id <0001> (usually the sending GA (mc) or device SN (uc))
      - osc.ms <ms_1>
      - osc.salt <salt_1>
      - osc.contextId 2001140921 (IA + time stamp (seconds))

      Sender Context
      - Sender ID, 0001
      - ID Context, 2001140921
      > AES Key, by device generated

    Message (unicast or multicast)
    ------------------------------
    - kid 0001
    - kid_context 2001140921

    Receiver (IA 2.0.2)
    -------------------
      Access Token (configured by MaC)
      - osc.id <0001> (usually the sending GA (mc) or device SN (uc))
      - osc.ms <ms_1>
      - osc.salt <salt_1>
      - osc.contextId 2002140922 (IA + time stamp (seconds))

      Recipient Context
      - Recipient ID, ?
      - ID Context, ?
      > AES Key, ?

      1. Step: try find (kid/ kid_context) in list of Recipient Contexts
               (usually saved to avoid a resynchronization after a device restart)
      2. Step: if nothing found in step 1, create a new Recipient Context

               Step A: look at kid in <Message> to get Recipient ID => 0001
               Step B: find access token with kid => 0001 to get corresponding master secret <ms_1>
               Step C: look at kid_context in <Message> to get ID Context => 2001140921

      3. Recipient Context (created - see 2 - or already present)
         - Recipient ID 0001
         - ID Context 2001140921 (from <Message>)
         - Master secret <ms_1> (from Receiver - Access Token)
         - Replay window <UNINITIALIZED>
         > AES Key, by device generated
*/

/**
  @brief receive a OSCORE message

  @param msg the message, pushed to queue INBOUND_OSCORE_EVENT since the previous
             oscore header check was ok

  @note See details in description on top of this file

*/
static int oc_oscore_receive_message(oc_message_t* msg)
{
  OC_DBG("####### OSCORE BEGIN #######");
  OC_DBG_OSCORE("process inbound OSCORE message");

  /*
    here we know (and set as default) it is an OSCORE message (it host the OSCORE option header)
    - not necessarily a message for us, we are able to decrypt
    - not necessarily a message without errors
    - IPv6 flag MUST already be set by lower layer (ip adapter)

  */
  msg->endpoint.flags |= OSCORE;

  // defaults
  oc_oscore_context_t* oscore_ctx = NULL;
  bool s_mode_echo_re_request = false;

  // names are from RFC OSCORE option
  uint8_t aad[OSCORE_AAD_MAX_LEN], aad_len = 0, nonce[OSCORE_AEAD_NONCE_LEN];

  /*
    msg was filled before from an inbound message, consider also:
    - COAP_TYPE_RST, treat reset with code > EMPTY_0_00 the same is as RST with EMPTY_0_00 (not a response)
    - COAP_TYPE_ACK

  */
  bool is_inbound_request;  // a coap GET (1) ... FETCH (5) --> in OSCORE only a POST
  bool is_inbound_response; // a coap CHANGED_2_04 (68) ... --> in OSCORE only a 2.04
  bool is_reset;
  bool is_con;
  bool is_non;

  // check loop back first before process any message, removes also unnecessary decryption and a throw later in coap layer
  if (oc_coap_check_if_loopback_message(msg))
  {
    // ignore duplicate request
    oc_message_unref(msg);
    return -1;
  }

  // create local CoAP packet
  coap_packet_t coap_pkt[1];

  if (oscore_parse_outer_message(msg, coap_pkt) != COAP_NO_ERROR)
  {
    /*
     Here we are still scan normal COAP message content (not yet in the OSCORE part)

     - (x) response + outer option problem = 8.4, step NA  = stop processing (also on empty ack or reset message)
     - (y) request + outer option problem  = 8.2, step 6   = unsecured 4.02
    */

    is_con = coap_pkt->type == COAP_TYPE_CON;
    is_non = coap_pkt->type == COAP_TYPE_NON;
    is_inbound_request = (is_con || is_non) && coap_pkt->code >= OC_GET && coap_pkt->code <= OC_FETCH;

    if (is_inbound_request)
    { // (y)

      OC_ERR("parse OUTER OSCORE message : error, return unsecured 4.02");
      oscore_send_error(coap_pkt, BAD_OPTION_4_02, &msg->endpoint, false);
    }

    // (x) + (y)
    oc_message_unref(msg);
    return -1;
  }

  // assign first on no error
  is_reset = coap_pkt->type == COAP_TYPE_RST;
  is_con = coap_pkt->type == COAP_TYPE_CON;
  is_non = coap_pkt->type == COAP_TYPE_NON;

  is_inbound_request = (is_con || is_non) && coap_pkt->code >= OC_GET && coap_pkt->code <= OC_FETCH;
  is_inbound_response = !is_reset && coap_pkt->code > OC_FETCH; // NON response or CON|ACK response 

  OC_DBG("parse OUTER OSCORE message : ok");

  /*
             kid > 0, piv > 0                                    kid > 0, piv = 0     kid = 0, piv = ?
    req[in]  8.2: AAD(RID/piv(msg)) + AEAD(RID/piv(msg))         4.02                 4.02
    res[in]  8:4: AAD(SID/piv(req)) + AEAD(RID/piv(msg))         4.02                 AAD(SID/piv(req)) + AEAD(SID/piv(req))


    req[in]
    - new/ fresh inbound request message -> usually no context is available
    -  an s-mode inbound request 'unicast echo response' message (see specification, figure 26, (2)) -> no context is
    available

    res[in]
    - a CHANGED_2_04 (68) = OK
    - a RESET with any code = ignore

   */

  uint8_t *request_piv = NULL, *request_kid = NULL, *nonce_piv = NULL, *nonce_kid = NULL;
  uint8_t request_piv_len = 0, request_kid_len = 0, nonce_piv_len = 0, nonce_kid_len;

  #ifdef OC_REQUEST_HISTORY
  // a check here removes unnecessary decryption and a throw later in coap layer
  if (oc_coap_check_if_duplicate_and_if_not_add_to_history(coap_pkt, &msg->endpoint))
  {
    // ignore duplicate request
    oc_message_unref(msg);
    return -1;
  }
  #endif

  if (is_inbound_request)
  { // 8.2

    if (coap_pkt->kid_len > 0)
    { // kid > 0

      OC_DBG("searching OSCORE context from incoming request message by 'kid_context' + 'kid' (len %d) : ", coap_pkt->kid_len);
      OC_LOGbytes(coap_pkt->kid, coap_pkt->kid_len);

      // find context from inbound request
      oscore_ctx = oc_oscore_find_context_by_kid_and_kid_context(coap_pkt->kid, coap_pkt->kid_len, coap_pkt->kid_ctx, coap_pkt->kid_ctx_len);

      if (!oscore_ctx)
      { // no beforehand cached context available, make one (usually on a fresh req[in])

        // find auth/at entry with corresponding 'kid' from inbound message
        const int idx = oc_core_find_at_entry_with_osc_id(coap_pkt->kid, coap_pkt->kid_len);
        if (idx == -1)
        {
          /*
            'kid' from 'request' not found as part of my own contexts,
             the inbound sender is not known to the server
             - an inbound GA as 'kid' that does not match to the server's AT table but using a (by server) registered
               multicast address, mc later ignored, standard case (a MULTICAST group value write where the GA is 
               not used in THIS device)
             - uc 4.01, MaC misconfiguration (a UNICAST group value write where the GA is not used in THIS device)
          */

          if (msg->endpoint.flags & MULTICAST)
          {
            // multicast: silently discard — do not reply
            OC_DBG("could not find an access token (8.2 step 2) for 'kid' from inbound multicast 's-mode' request message, silently discarding");
          }
          else
          {
            // unicast: respond with unsecured 4.01 (RFC 8613 § 8.2 step 2 / KNX IoT § 3.6.5)
            OC_ERR("could not find an access token (8.2 step 2) for 'kid' from inbound unicast 's-mode' request message, return unsecured 4.01");
            oscore_send_error(coap_pkt, UNAUTHORIZED_4_01, &msg->endpoint, false);
          }
          oc_message_unref(msg);
          return -1;
        }

        /*

           'Server' Side (details see method 'oc_oscore_receive_message' header), create:
            Request Recipient Context (normal context for the inbound 'normal' message)
             - kid (taken from the inbound request message)
             - kid_context (taken from the inbound request message = by MaC written)
             - ms + salt from token
             - ssn 

        */

        uint64_t inbound_ssn;
        oscore_store_piv_to_ssn(coap_pkt->piv, coap_pkt->piv_len, &inbound_ssn);

        // get access token, idx cannot be out of range because of the check before, see method 'oc_core_find_at_entry_with_osc_id' 
        const oc_auth_at_t* at_entry = oc_get_auth_at_entry(idx);

        // take over client's ssn on synchronization, due to a lost sync by the client
        oscore_ctx = oc_oscore_add_recipient_context(oc_string(at_entry->osc_id), oc_byte_string_len(at_entry->osc_id),
                                                     inbound_ssn,
                                                     oc_string(at_entry->osc_ms), oc_byte_string_len(at_entry->osc_ms),
                                                     oc_string(at_entry->osc_salt), oc_byte_string_len(at_entry->osc_salt),
                                                     (char*)coap_pkt->kid_ctx, coap_pkt->kid_ctx_len, idx, false);

        if (!oscore_ctx)
        {
          // this should not happen, because there was with LRU a context released
          OC_ERR("could not create oscore recipient context, return unsecured 5.00");
          oscore_send_error(coap_pkt, INTERNAL_SERVER_ERROR_5_00, &msg->endpoint, false);
          oc_message_unref(msg);
          return -1;
        }
      }

      // use as kid (new or existing context) 
      request_kid = nonce_kid = oscore_ctx->recipient_id;
      request_kid_len = nonce_kid_len = oscore_ctx->recipient_id_len;
      
      // use piv from 'inbound' request
      request_piv = nonce_piv = coap_pkt->piv;
      request_piv_len = nonce_piv_len = coap_pkt->piv_len;

      if (coap_pkt->piv_len > 0)
      { // // 8.2, step 4/5 : kid > 0, piv > 0 -> use kid/piv from response message

        // AAD use always request kid/PIV (8.2 step 4)
        oc_oscore_compose_AAD(request_kid, request_kid_len,
                              request_piv, request_piv_len, aad, &aad_len);

        // AEAD (nonce) use request kid/PIV or response kid/PIV (8.2 step 5)
        oc_oscore_AEAD_nonce(nonce_kid, nonce_kid_len,
                             nonce_piv, nonce_piv_len,
                             oscore_ctx->common_iv, nonce, OSCORE_AEAD_NONCE_LEN);

        OC_DBG("8.2: ---> computed AEAD + AEAD nonce using Partial IV from 'inbound' request message and Recipient ID, nonce =\t: ");
        OC_LOGbytes(nonce, OSCORE_AEAD_NONCE_LEN);
      }
      else
      { // kid > 0, piv = 0

        OC_ERR("request lacks kid param (8.2 step 2) - piv = 0, return unsecured 4.02");
        oscore_send_error(coap_pkt, BAD_OPTION_4_02, &msg->endpoint, false);
        oc_message_unref(msg);
        return -1;
      }
    }
    else
    { // kid = 0
      OC_ERR("request lacks kid param (8.2 step 2) - kid = 0, return unsecured 4.02");
      oscore_send_error(coap_pkt, BAD_OPTION_4_02, &msg->endpoint, false);
      oc_message_unref(msg);
      return -1;
    }
  }
  else if (is_inbound_response)
  { // 8.4, kid = 0, kid > 0

    OC_DBG("searching OSCORE context by message 'mid' + 'token' (kid len %d) : ", coap_pkt->kid_len);

    if (coap_pkt->kid_ctx_len == 10)
    { // kid_ctx = 10

      // find auth/at entry with corresponding 'kid' from inbound message
      const int idx = oc_core_find_at_entry_with_osc_id(coap_pkt->kid, coap_pkt->kid_len);
      if (idx == -1)
      {
        /*
           'kid' from 'unicast echo response' not found as part of my onw access token table,
            the inbound sender is not known to the server
            - uc 4.01, MaC misconfiguration (a UNICAST echo response)
        */

        OC_ERR("could not find an access token (8.4 step 2) for 'kid' from inbound 'unicast echo response' response message, stop processing");
        oc_message_unref(msg);
        return -1;
      }

      /*

         'Client' Side (details see method 'oc_oscore_receive_message' header), create:
          Response Recipient Context (normal context for the inbound 'unicast echo response' message)
           - kid (taken from access token)
           - kid_context (taken from the inbound message = rnd)
           - ms + salt from token
           - ssn 
      */

     
      // TODO DL check on replay by compare ssn with white 'list' (last send out ssn, kid, kid context) / black 'list' (own list system , not reusing ctx , to big) 
      
      // get access token, cannot be out of range because of the check before, see method 'oc_core_find_at_entry_with_osc_id'
      const oc_auth_at_t* at_entry = oc_get_auth_at_entry(idx);

      // init ssn with '0', not used on any sending (BUT consider on TASK above) 
      oscore_ctx = oc_oscore_add_recipient_context(
        oc_string(at_entry->osc_id), oc_byte_string_len(at_entry->osc_id), 
        0,
        oc_string(at_entry->osc_ms), oc_byte_string_len(at_entry->osc_ms),
        oc_string(at_entry->osc_salt), oc_byte_string_len(at_entry->osc_salt),
        (char*)coap_pkt->kid_ctx, coap_pkt->kid_ctx_len, idx, false);

      if (!oscore_ctx)
      {
        // this should not happen, because there was with LRU a context released
        OC_ERR("could not create oscore recipient context, return unsecured 5.00");
        oscore_send_error(coap_pkt, INTERNAL_SERVER_ERROR_5_00, &msg->endpoint, false);
        oc_message_unref(msg);
        return -1;
      }

      // received a 'unicast echo response' -> need to send later an s-mode 'unicast echo re-request'
      s_mode_echo_re_request = true;
      
      // use as kid, a former request context IS NOT existing
      request_kid = oscore_ctx->recipient_id;
      request_kid_len = oscore_ctx->recipient_id_len;
      
      // use piv from 'inbound' response
      request_piv = coap_pkt->piv;
      request_piv_len = coap_pkt->piv_len;
    }
    else
    { // kid_ctx != 10

      //  find context from 'former' own request, but not for s-mode, here only requests exists, see "S-MODE" details (engine.c)
      oscore_ctx = oc_oscore_find_context_by_token_mid(coap_pkt->token, coap_pkt->token_len, coap_pkt->mid, &request_piv, &request_piv_len, false);
      if (!oscore_ctx)
      {
        OC_ERR("response error (8.4 step 2), ignore silently, cannot find a matching oscore Request Sender Context from inbound response");
        oc_message_unref(msg);
        return -1;
      }

      // use as kid, a former request context IS EXISTING (by token)
      request_kid = oscore_ctx->sender_id;
      request_kid_len = oscore_ctx->sender_id_len;
      
      // use piv from 'former' own request
    }

    if (coap_pkt->piv_len > 0)
    { // 8.4, step 4, *2 : kid > 0, piv > 0 -> use kid/piv from inbound response message

      nonce_piv = coap_pkt->piv;
      nonce_piv_len = coap_pkt->piv_len;
      nonce_kid = oscore_ctx->recipient_id;
      nonce_kid_len = oscore_ctx->recipient_id_len;

      OC_DBG("8.4: ---> computed AAD + AEAD nonce using PIV from 'inbound' response message, nonce =\t: ");
    }
    else
    { // 8.4, step 4, *1 : kid > 0, piv = 0 -> use kid/piv from token

      nonce_piv = request_piv;
      nonce_piv_len = request_piv_len;
      nonce_kid = request_kid;
      nonce_kid_len = request_kid_len;

      OC_DBG("8.4: ---> computed AAD + AEAD nonce using PIV from 'former own' request message, nonce =\t: ");
    }

    // AAD use always request kid/PIV (8.4 step 3)
    oc_oscore_compose_AAD(request_kid, request_kid_len,
                          request_piv, request_piv_len, aad, &aad_len);
    
    // AEAD (nonce) use request kid/PIV or response kid/PIV (8.4 step 4)
    oc_oscore_AEAD_nonce(nonce_kid, nonce_kid_len,
                         nonce_piv, nonce_piv_len,
                         oscore_ctx->common_iv, nonce, OSCORE_AEAD_NONCE_LEN);
    
    OC_LOGbytes(nonce, OSCORE_AEAD_NONCE_LEN);

    
  }
  else if (is_reset)
  {
    OC_ERR("message lacks kid parameter (8.4 step 8), ignore silently, was empty ack or reset ");
    oc_message_unref(msg);
    return -1;
  }

  // use recipient key for decryption
  uint8_t* decryption_key = oscore_ctx->recipient_key;

  // verify and decrypt OSCORE payload in coap packet , acc. MBEDTLS same input/output buffer can be used
  int ret = oc_oscore_decrypt(coap_pkt->payload, coap_pkt->payload_len, 
                              OSCORE_AEAD_TAG_LEN, decryption_key, OSCORE_KEY_LEN, nonce,
                              OSCORE_AEAD_NONCE_LEN, aad, aad_len, coap_pkt->payload);

  if (ret != 0)
  {
    /*
      response + encrypted problem = 8.4 step 5  = stop processing
      request + encrypted problem = 8.2 step 6 = return unsecured 4.00
    */

    if (is_inbound_request)
    {
      // request
      OC_ERR("decrypting OSCORE payload : error (%d), return unsecured 4.00", ret);
      oscore_send_error(coap_pkt, BAD_REQUEST_4_00, &msg->endpoint, false);
    }

    // response
    oc_message_unref(msg);
    return -1;
  }

  OC_DBG_OSCORE("decrypting OSCORE payload : success (0)");

  // adjust payload length to size after decryption (i.e. exclude the tag)
  coap_pkt->payload_len -= OSCORE_AEAD_TAG_LEN;

  /*
    prepare message (before parsing) by save inbound request 'kid', 'kid_context', ... and 'ssn' (piv)
    - used for regular message in replay protection check in CoAP (receive) layer
    - used for 4.02 error (see below)
  */

  // uc-a: save access token index, that was used to decrypt, see send_unicast
  msg->endpoint.auth_at_index_from_former_inbound_request = oscore_ctx->auth_at_index;

  // uc-b: save 'kid'/ 'Sender ID' from inbound message, that was used to decrypt, see send_unicast
  msg->endpoint.oscore_id_len = request_kid_len > OSCORE_SENDER_ID_LEN ? OSCORE_SENDER_ID_LEN : request_kid_len;
  memcpy(msg->endpoint.oscore_id, request_kid, msg->endpoint.oscore_id_len);

  msg->endpoint.kid_len = coap_pkt->kid_len > OSCORE_SENDER_ID_LEN ? OSCORE_SENDER_ID_LEN : coap_pkt->kid_len;
  memcpy(msg->endpoint.kid, coap_pkt->kid, msg->endpoint.kid_len);

  // uc-a: see send_unicast
  msg->endpoint.kid_ctx_len = coap_pkt->kid_ctx_len > OSCORE_ID_CONTEXT_LEN ? OSCORE_ID_CONTEXT_LEN : coap_pkt->kid_ctx_len;
  memcpy(msg->endpoint.kid_ctx, coap_pkt->kid_ctx, msg->endpoint.kid_ctx_len);

  // uc-e: see send_unicast
  msg->endpoint.request_piv_len = coap_pkt->piv_len > OSCORE_PIV_LEN ? OSCORE_PIV_LEN : coap_pkt->piv_len;
  memcpy(msg->endpoint.request_piv, coap_pkt->piv, msg->endpoint.request_piv_len);

  // remove specific echo context, not needed anymore and never auto released (was a sender context)
  if (s_mode_echo_re_request)
  {
    oc_oscore_free_context(oscore_ctx);
  }

  if (oscore_parse_inner_message(coap_pkt->payload, coap_pkt->payload_len, coap_pkt) != COAP_NO_ERROR)
  {
    /*
       (outer coap code) response + inner options problem = ignore
       (outer coap code ) request + inner options problem = return 4.02 secured (EITT test 5.10.5.3)
    */

    if (is_inbound_request)
    {
      // request
      OC_ERR("parsing inner message : error, return secured 4.02");
      oscore_send_error(coap_pkt, BAD_OPTION_4_02, &msg->endpoint, true);
    }

    // response + request
    oc_message_unref(msg);
    return -1;
  }

  OC_DBG("parsing inner message : ok, payload len  = %u", coap_pkt->payload_len);

  /*
    fill new transaction with prepared coap data and payload data from former transaction,
    serialize OSCORE message, add all inner/outer options and (inner) payload
  */
  msg->length = coap_oscore_serialize_message(coap_pkt, msg->data, true, true, true);

  // from here on the message is decrypted
  msg->endpoint.flags |= OSCORE_DECRYPTED;

  OC_DBG("serialized decrypted CoAP message to dispatch to the CoAP layer : ");
  PRINTipaddr_flags(msg->endpoint);

  // dispatch the (received and decrypted) message to the CoAP layer
  if (oc_process_post(&coap_engine, oc_events[INBOUND_RI_EVENT], msg) == OC_PROCESS_ERR_FULL)
  {
    // error is > 0 ...
    oc_message_unref(msg);
    return -1;
  }

  OC_DBG("####### OSCORE END #######");
  return 0;
}

#ifdef OC_CLIENT
/**
  @brief

  @param msg the message, pushed to queue OUTBOUND_OSCORE_EVENT since the OSCORE flag
             and MULTICAST flag is set

  @note See details in description on top of this file

*/
static int oc_oscore_send_multicast_message(oc_message_t* msg)
{
  OC_DBG_OSCORE("process outbound multicast OSCORE message");

  // new msg, send and release after sending -> the original message may be still needed for echos (NON messages)
  oc_message_t* from_org_msg_cloned_outgoing_msg = oc_internal_allocate_outgoing_message();
  if (!from_org_msg_cloned_outgoing_msg)
  {
    return -1;
  }

  // clone handed over 'oscore' message (msg) into sent out 'oscore' message, the outgoing msg takes care from now on
  from_org_msg_cloned_outgoing_msg->length = msg->length;
  memcpy(from_org_msg_cloned_outgoing_msg->data, msg->data, msg->length);
  memcpy(&from_org_msg_cloned_outgoing_msg->endpoint, &msg->endpoint, sizeof(oc_endpoint_t));
  
  // release original message
  oc_message_unref(msg);

  // create local CoAP packet
  coap_packet_t coap_pkt[1];

  const coap_status_t code = coap_parse_udp_message(coap_pkt, from_org_msg_cloned_outgoing_msg->data, from_org_msg_cloned_outgoing_msg->length);

  if (code != COAP_NO_ERROR)
  {
    OC_ERR("coap parse packet : error (multicast)");
    oc_message_unref(from_org_msg_cloned_outgoing_msg);
    return -1;
  }

  OC_INF("coap parse packet : ok (multicast)");
  
  // get sending ga
  const uint32_t group_address = from_org_msg_cloned_outgoing_msg->endpoint.group_address;

  /*
    find Sender Context (SID) for sending ga, in case
    - ga = '0' = NOT initialized, this call fails since no context will be available
    - ga = '0' = i want to send ga 0, this call succeeds since context will be available
  */
  oc_oscore_context_t* oscore_ctx = oc_oscore_find_context_by_group_address(group_address);

  if (oscore_ctx)
  {
    OC_DBG_OSCORE("found group OSCORE context for GA %04X", group_address);

    // use sender key for encryption
    uint8_t* key = oscore_ctx->sender_key;

    uint8_t piv[OSCORE_PIV_LEN], piv_len = 0; 
    uint8_t nonce[OSCORE_AEAD_NONCE_LEN];
    uint8_t aad[OSCORE_AAD_MAX_LEN], aad_len = 0;

    // request - use context SSN as Partial IV (before increment)
    oscore_store_ssn_to_piv(piv, &piv_len, oscore_ctx->ssn);

    // debugging
    OC_DBG_OSCORE("protecting outgoing multicast request, using SSN as Partial IV : %" PRIu64, oscore_ctx->ssn);

    /*
        increment SSN
        - an initial NON request (transaction not present) -> a read request
        - an initial NON request (transaction not present) -> a read response
    */
    increment_ssn_in_context(oscore_ctx);

    // compute nonce using 'kid' and PIV
    oc_oscore_AEAD_nonce(oscore_ctx->sender_id, oscore_ctx->sender_id_len,
                         piv, piv_len, 
                         oscore_ctx->common_iv, nonce, OSCORE_AEAD_NONCE_LEN);

    // compose AAD using 'kid' and PIV
    oc_oscore_compose_AAD(oscore_ctx->sender_id, oscore_ctx->sender_id_len, piv, piv_len, aad, &aad_len);
    
    OC_DBG("computed AEAD : ");
    OC_LOGbytes_OSCORE(nonce, OSCORE_AEAD_NONCE_LEN);

    OC_DBG("composed AAD  : ");
    OC_LOGbytes_OSCORE(aad, aad_len);

    uint8_t* dst1 = from_org_msg_cloned_outgoing_msg->data + COAP_MAX_HEADER_SIZE;

    if (coap_pkt->payload_len > 0)
    {
      // make room for outer/inner CoAP options + payload in OSCORE packet, move CoAP payload to offset 2*COAP_MAX_HEADER_SIZE
      uint8_t* dst2 = dst1 + COAP_MAX_HEADER_SIZE;

      // move payload and store/remind the new (moved) payload location in the CoAP packet
      memmove(dst2, coap_pkt->payload, coap_pkt->payload_len);
      coap_pkt->payload = dst2;
    }

    // serialize OSCORE plain text 'at' offset COAP_MAX_HEADER_SIZE (inner code, inner options, application payload) by using the 'moved' payload location ptr
    const size_t plaintext_size = oscore_serialize_plaintext(coap_pkt, dst1);

    OC_DBG("serialized OSCORE plaintext: %" PRIu64 " bytes", plaintext_size);

    // set the OSCORE packet pointer to location of the serialized inner message (inner code, inner options, payload)
    coap_pkt->payload = dst1;
    coap_pkt->payload_len = (uint32_t)plaintext_size;

    // verify and encrypt OSCORE payload in coap packet , acc. MBEDTLS same input/output buffer can be used
    const int ret = oc_oscore_encrypt(coap_pkt->payload, coap_pkt->payload_len,
                                      OSCORE_AEAD_TAG_LEN, key, OSCORE_KEY_LEN,
                                      nonce, OSCORE_AEAD_NONCE_LEN, aad, aad_len, coap_pkt->payload);

    if (ret != 0)
    {
      OC_ERR("decrypting OSCORE payload : error (%d), ignore message", ret);
      oc_message_unref(msg);
      return -1;
    }

    OC_DBG("decrypting OSCORE payload : success (0)");

    // adjust payload length to include the size of the authentication tag
    coap_pkt->payload_len += OSCORE_AEAD_TAG_LEN;

    // set the OUTER code for the OSCORE packet (on mc request = POST)
    coap_pkt->code = OC_POST;

    /*
      Wireshark fix - include the 'kid_context' (in msg) = 'ID Context' (OSCORE)
      on the wire as well, otherwise cannot decode OSCORE messages that use implicit ID contexts.
    */
    // set the OSCORE option (kid, kid_context, piv) in the OUTER message
    coap_set_header_oscore(coap_pkt, piv, piv_len, oscore_ctx->sender_id, oscore_ctx->sender_id_len, oscore_ctx->id_context, oscore_ctx->id_context_len);

    // serialize OSCORE message
    from_org_msg_cloned_outgoing_msg->length = oscore_serialize_message(coap_pkt, from_org_msg_cloned_outgoing_msg->data);
    OC_DBG("serialized OSCORE message");
  }
  else
  {
    OC_ERR("found NO group OSCORE context for GA %04X", group_address);
    oc_message_unref(from_org_msg_cloned_outgoing_msg);
    return -1;
  }

  // from here on any message is encrypted ...
  UNSET_BIT(from_org_msg_cloned_outgoing_msg->endpoint.flags, OSCORE_DECRYPTED);

  if (oc_process_post(&message_buffer_handler, oc_events[OUTBOUND_NETWORK_EVENT_ENCRYPTED], 
      from_org_msg_cloned_outgoing_msg) == OC_PROCESS_ERR_FULL)
  {
    OC_ERR("could not send message");
  }

  return 0;
}
#endif

/**
  @brief

  @param msg the message, pushed to queue OUTBOUND_OSCORE_EVENT since the OSCORE flag is set
             and MULTICAST flag is NOT set

  @note See details in description on top of this file

*/
static int oc_oscore_send_unicast_message(oc_message_t* msg)
{
  OC_DBG_OSCORE("process outbound unicast OSCORE message");

  // new msg, send and release after sending -> the original message may be still needed for reps (tracked or CON messages)
  oc_message_t* from_org_msg_cloned_outgoing_msg = oc_internal_allocate_outgoing_message();
  if (!from_org_msg_cloned_outgoing_msg)
  {
    return -1;
  }

  // clone handed over 'oscore' message (msg) into sent out 'oscore' message, the outgoing msg takes care from now on
  from_org_msg_cloned_outgoing_msg->length = msg->length;
  memcpy(from_org_msg_cloned_outgoing_msg->data, msg->data, msg->length);
  memcpy(&from_org_msg_cloned_outgoing_msg->endpoint, &msg->endpoint, sizeof(oc_endpoint_t));

  // save if original msg is 'tracked' AND remove one reference (either 'msg' is just released or still present)
  bool original_msg_is_currently_tracked = msg->ref_count > 1;
  oc_message_unref(msg);

  // create local CoAP packet
  coap_packet_t coap_pkt[1];

  // parse outgoing message and copy to CoAP packet ... ('msg' may just be released, -> data may be NULL)
  #ifdef OC_TCP
  if (from_org_msg_cloned_outgoing_msg->endpoint.flags & TCP)
    coap_status_t code = coap_tcp_parse_message(coap_pkt, from_org_msg_cloned_outgoing_msg->data, (uint32_t)from_org_msg_cloned_outgoing_msg->length);
  else
  #endif
    coap_status_t code = coap_parse_udp_message(coap_pkt, from_org_msg_cloned_outgoing_msg->data, from_org_msg_cloned_outgoing_msg->length);

  if (code != COAP_NO_ERROR)
  {
    OC_ERR("coap parse packet : error (unicast)");
    oc_message_unref(from_org_msg_cloned_outgoing_msg);
    return -1;
  }

  OC_INF("coap parse packet : ok (unicast)");

  // init context
  oc_oscore_context_t* oscore_ctx = NULL;

  /*
    Cases for a unicast (outbound) message:

    (uc-a) context will be retrieved by kid
     - do uc outbound (server) response -> after former inbound (ext) CON/NON request
     - example: do "Piggybacked Response"

    (uc-b) context will be retrieved by 'Sender ID'
     - do uc outbound (client) request -> on client application initial r/w request via e.g.; 'dev/pm'
     - TODO DL not needed since (ETS) client functionality not present anymore?

    (uc-c) context retried by group address
     - do uc outbound (client) s-mode request
     - example: do client application initial CON/NON r/w request via '/k'

    (uc-d) context retried by token/mid
     - do ??? -> after inbound "Empty ACK" (only mid is included)
     - example: ???
     - TODO DL action unclear, to be investigated if this is only a second uc-a if token is in

    (uc-e) context retried by NEW context
     - do uc outbound (server) response -> after former UNSYNCED inbound (ext) CON/NON request
     - example: do "Echo Response"
   */

  oc_auth_at_t* at_entry = oc_get_auth_at_entry(from_org_msg_cloned_outgoing_msg->endpoint.auth_at_index_from_former_inbound_request);

  OC_DBG_OSCORE("%s", at_entry ? "found access token, step 1" : "uc-a : non access token");

  if (at_entry)
  { // (uc-a)

    // use kid (at) + kid_context (msg) to find context (many GA contexts may exist for an access token, hence check kid_context)
    oscore_ctx = oc_oscore_find_context_by_kid_and_kid_context((uint8_t*)oc_string(at_entry->osc_id), oc_byte_string_len(at_entry->osc_id),
                                                               from_org_msg_cloned_outgoing_msg->endpoint.kid_ctx,
                                                               from_org_msg_cloned_outgoing_msg->endpoint.kid_ctx_len);

    OC_DBG_OSCORE("%s", oscore_ctx ? "found context by access token, step 2" : "uc-a : non context");
  }

  if (oscore_ctx == NULL)
  { // (uc-b)

    // search the oscore id, e.g. the cnf:osc:id ('kid' , 'Sender ID')
    oscore_ctx = oc_oscore_find_context_by_oscore_id(from_org_msg_cloned_outgoing_msg->endpoint.oscore_id,
                                                     from_org_msg_cloned_outgoing_msg->endpoint.oscore_id_len);

    OC_DBG_OSCORE("%s", oscore_ctx ? "found context by 'kid'/ 'Sender ID'" : "uc-b : non context");
  }

  if (oscore_ctx == NULL)
  { // (uc-c)

    oscore_ctx = oc_oscore_find_context_by_group_address(from_org_msg_cloned_outgoing_msg->endpoint.group_address);

    OC_DBG_OSCORE("%s", oscore_ctx ? "found context by 'ga'" : "uc-c : non context");
  }

  if (oscore_ctx == NULL)
  { // (uc-d)

    // find context from 'former' own request
    oscore_ctx = oc_oscore_find_context_by_token_mid(coap_pkt->token, coap_pkt->token_len, coap_pkt->mid, NULL, 0, false);

    OC_DBG_OSCORE("%s", oscore_ctx ? "found context by 'token/mid'" : "uc-d : non context");
  }

  // we haven't found any context (uc-a) ... (uc-d), so we free the message we just created
  if (oscore_ctx == NULL)
  {
    OC_ERR("no OSCORE context found, error");
    oc_message_unref(from_org_msg_cloned_outgoing_msg);
    return -1;
  }

  // msg was filled before from an inbound request or self issued request message, consider also COAP_TYPE_RST or COAP_TYPE_ACK
  bool is_reset = coap_pkt->type == COAP_TYPE_RST;
  bool is_con = coap_pkt->type == COAP_TYPE_CON;
  bool is_non = coap_pkt->type == COAP_TYPE_NON;

  bool is_outbound_request = (is_con || is_non) && coap_pkt->code >= OC_GET && coap_pkt->code <= OC_FETCH;
  bool is_outbound_response = !is_reset && coap_pkt->code > OC_FETCH; // NON response or CON(sep)|ACK(piggy) response
  
  bool unicast_echo_response_by_mc = from_org_msg_cloned_outgoing_msg->endpoint.flags & ECHO_CAUSED_BY_MC_SRC;
  bool unicast_echo_response_by_uc = from_org_msg_cloned_outgoing_msg->endpoint.flags & ECHO_CAUSED_BY_UC_SRC;

  /*
   s-mode = only if ga len is > '0'

   - (x0) inbound s-mode mc to /k : return 'uc echo response', use temp id_context 10 byte rnd 'Response Sender Context'
   - (x1) inbound s-mode uc to /k : return 'uc echo response', use id_context from inbound s-mode message 'Response Sender Context'

   - (x2) inbound s-mode mc to /k : return NO 'uc echo response', n/a in outbound unicast
   - (x3) inbound s-mode uc to /k : return NO 'uc echo response', pass through = normal response

   - (x4) inbound mc to *  : n/a in outbound unicast
   - (x5) inbound uc to *  : generic response, pass through
   - (x6) inbound uc to *  : uc echo response, = (x1)
  */

  // names are from RFC OSCORE option
  uint8_t piv[OSCORE_PIV_LEN], piv_len = 0;
  uint8_t aad[OSCORE_AAD_MAX_LEN], aad_len = 0, nonce[OSCORE_AEAD_NONCE_LEN];

  // request - use context SSN as Partial IV (before increment)
  oscore_store_ssn_to_piv(piv, &piv_len, oscore_ctx->ssn);

  coap_transaction_t* transaction = coap_get_transaction_by_token(coap_pkt->token, coap_pkt->token_len);
  bool is_a_con_repetition = transaction && transaction->retransmit_counter > 0;

  /*
    increment SSN
    - an initial CON request (transaction present) -> a s-mode ('r' -read) OR s-mode ('a' response)
    - an initial NON request (transaction not present) -> a s-mode ('r' -read) OR s-mode ('a' response)
    - an initial CON response (transaction present) -> a read response
    - an initial NON response (transaction not present) -> a read response

    Note s-mode uses only requests, see "S-MODE" details (engine.c).

    keep SSN
    - CON retransmissions (counter > 0) use the same SSN

  */
  if (!is_a_con_repetition)
    increment_ssn_in_context(oscore_ctx);

  // prepare kid/piv with request context data, may be overwritten later depending on the case (8.3)
  uint8_t *outbound_piv = piv, 
          *inbound_piv = from_org_msg_cloned_outgoing_msg->endpoint.request_piv, 
          *kid = oscore_ctx->sender_id,
          *kid_context = oscore_ctx->id_context;

  uint8_t outbound_piv_len = piv_len, 
          inbound_piv_len = from_org_msg_cloned_outgoing_msg->endpoint.request_piv_len, 
          kid_len = oscore_ctx->sender_id_len, 
          kid_context_len = oscore_ctx->id_context_len;
  
  if (is_outbound_request
  #ifdef OC_TCP
      || coap_pkt->code == PING_7_02 || coap_pkt->code == ABORT_7_05 || coap_pkt->code == CSM_7_01
  #endif
  )
  { // 8.1

    #ifdef OC_CLIENT

    // find client cb from the former request
    oc_client_cb_t* cb = oc_ri_find_client_cb_by_token(coap_pkt->token, coap_pkt->token_len);

    if (cb)
    {
      // copy NEW PIV into client cb data
      cb->piv_len = outbound_piv_len;
      memcpy(cb->piv, outbound_piv, outbound_piv_len);
    }

    #endif

    oc_oscore_AEAD_nonce(kid, kid_len,
                         outbound_piv, outbound_piv_len,
                         oscore_ctx->common_iv, nonce, OSCORE_AEAD_NONCE_LEN);

    oc_oscore_compose_AAD(kid, kid_len, outbound_piv, outbound_piv_len, aad, &aad_len);

    // TODO AH , for a request not needed ?
    // copy PIV to CoAP - handed over - unicast message (not the outgoing message)
    if (original_msg_is_currently_tracked)
    {
      memcpy(msg->endpoint.request_piv, piv, piv_len);
      msg->endpoint.request_piv_len = piv_len;

      OC_DBG("sending request is still tracked, caching PIV for later use ...");
      OC_LOGbytes_OSCORE(msg->endpoint.request_piv, msg->endpoint.request_piv_len);
    }

    // debugging
    OC_DBG_OSCORE("sending request, using SSN as Partial IV with len = %x and piv = ", outbound_piv_len);
    OC_LOGbytes_OSCORE(outbound_piv, outbound_piv_len);
  }
  else if (is_outbound_response)
  { // 8.3, 2.0x/4.0x response (incl. 4.01 echo response)

    // RFC 8613, 8.3 + KNX IoT 3.6.5
    if (unicast_echo_response_by_mc)
    { // s-mode (x0), (uc-e) - overwrites context retrieved by (uc-a)

      bool is_smode = oc_get_auth_at_entry(oscore_ctx->auth_at_index)->ga_len > 0;
      if (is_smode)
      { // any context using an access token with ga len > 0 is an s-mode message
        
        // mc echo data = random
        unsigned char rnd[10];

        mbedtls_ctr_drbg_context* ctr_drbg_context = oc_random_get_ctr_drbg_context();
        mbedtls_ctr_drbg_random(ctr_drbg_context, rnd, sizeof(rnd));

        // echo response - use s-mode inbound request SSN as Partial IV
        uint64_t inbound_ssn;
        oscore_store_piv_to_ssn(inbound_piv, inbound_piv_len, &inbound_ssn);

        /*
               'Server' Side (details see method 'oc_oscore_receive_message' header), create:
                Response Sender Context
               - kid
               - kid_context (rnd)
               - ms + salt from token
               - ssn = from s-mode request message, used for 'unicast echo responses'
        */

        oscore_ctx = oc_oscore_add_sender_context(
          oc_string(at_entry->osc_id), oc_byte_string_len(at_entry->osc_id),
          inbound_ssn,
          oc_string(at_entry->osc_ms), oc_byte_string_len(at_entry->osc_ms),
          oc_string(at_entry->osc_salt), oc_byte_string_len(at_entry->osc_salt),
          (char*)rnd, 10, 
          from_org_msg_cloned_outgoing_msg->endpoint.auth_at_index_from_former_inbound_request,
          false);

        if (!oscore_ctx)
        {
          // this should not happen, because there was with LRU a context released
          OC_ERR("could not create oscore sender context, return unsecured 5.00");
          oscore_send_error(coap_pkt, INTERNAL_SERVER_ERROR_5_00, &msg->endpoint, false);
          oc_message_unref(msg);
          return -1;
        }

        // RFC 8613, 8.3, point 3 lower *, echo response 
        // -> reuse the inbound PIV/SSN and Sender ID from just beforehand created context to compute a new AEAD nonce
        oc_oscore_AEAD_nonce(oscore_ctx->sender_id, oscore_ctx->sender_id_len,
                             inbound_piv, inbound_piv_len, oscore_ctx->common_iv, nonce,
                             OSCORE_AEAD_NONCE_LEN);

        // Sender ID from just beforehand created context  + inbound PIV -> https://www.rfc-editor.org/rfc/rfc8613#section-5.4
        oc_oscore_compose_AAD(oscore_ctx->sender_id, oscore_ctx->sender_id_len, 
                              inbound_piv, inbound_piv_len, aad, &aad_len);

        UNSET_BIT(from_org_msg_cloned_outgoing_msg->endpoint.flags, ECHO_CAUSED_BY_MC_SRC);
        
        OC_DBG_OSCORE("send 'unicast echo response' caused by inbound s-mode multicast message using PIV with len = %u :", inbound_piv_len);
        OC_LOGbytes_OSCORE(inbound_piv, inbound_piv_len);

        // 8.1 (a) : update context data for echo responses caused by s-mode multicast requests (was a new context)
        kid = oscore_ctx->sender_id;
        kid_len = oscore_ctx->sender_id_len;
        kid_context = oscore_ctx->id_context;
        kid_context_len = oscore_ctx->id_context_len;

        outbound_piv = inbound_piv;
        outbound_piv_len = inbound_piv_len;

      }
    }
    else if (unicast_echo_response_by_uc)
    { // s-mode (x1) | non s-mode (x6), context retrieved by (uc-a) ... (uc-d)

      // RFC 8613, 8.3, point 3 lower *, echo response 
      // -> reuse the inbound PIV/SSN and Sender ID to compute a new AEAD nonce
      oc_oscore_AEAD_nonce(oscore_ctx->sender_id, oscore_ctx->sender_id_len,
                           inbound_piv, inbound_piv_len, oscore_ctx->common_iv, nonce,
                           OSCORE_AEAD_NONCE_LEN);

      // Sender ID + inbound PIV -> https://www.rfc-editor.org/rfc/rfc8613#section-5.4
      oc_oscore_compose_AAD(oscore_ctx->recipient_id, oscore_ctx->recipient_id_len, 
                            inbound_piv, inbound_piv_len, aad, &aad_len);

      UNSET_BIT(from_org_msg_cloned_outgoing_msg->endpoint.flags, ECHO_CAUSED_BY_UC_SRC);
      
      OC_DBG_OSCORE("send 'unicast echo response' caused by inbound %s unicast message using PIV with len = %u :",
                    from_org_msg_cloned_outgoing_msg->endpoint.flags & S_MODE_NON_REQUEST + S_MODE_CON_REQUEST ? "s-mode" : "common", inbound_piv_len);
      OC_LOGbytes_OSCORE(inbound_piv, inbound_piv_len );

       // 8.1 (a) : update context data for echo responses caused by s-mode unicast requests
      outbound_piv = inbound_piv;
      outbound_piv_len = inbound_piv_len;

    }
    else
    {
      // RFC 8613, 8.3, point 3 upper *, 2.0x/4.0x response -> reuse the inbound PIV/SSN and Recipient ID 
      // (= Sender ID from the request) to compute the same AEAD nonce as for the inbound request
      oc_oscore_AEAD_nonce(oscore_ctx->recipient_id, oscore_ctx->recipient_id_len, 
                           inbound_piv, inbound_piv_len,
                           oscore_ctx->common_iv, nonce, OSCORE_AEAD_NONCE_LEN);

      // Recipient ID + Request PIV -> https://www.rfc-editor.org/rfc/rfc8613#section-5.4
      oc_oscore_compose_AAD(oscore_ctx->recipient_id, oscore_ctx->recipient_id_len,
                            inbound_piv, inbound_piv_len, aad, &aad_len);

       // 8.3 (b) : update context data for responses, no piv for common responses
      outbound_piv = NULL;
      outbound_piv_len = 0;

      // debugging
      OC_DBG("sending common response, using PIV with len = %u : ", inbound_piv_len);
      OC_LOGbytes_OSCORE(inbound_piv, inbound_piv_len);
    }
  }

  OC_DBG("computed AEAD : ");
  OC_LOGbytes_OSCORE(nonce, OSCORE_AEAD_NONCE_LEN);

  OC_DBG("composed AAD  : ");
  OC_LOGbytes_OSCORE(aad, aad_len);

  // here requests and responses end up

  uint8_t* dst1 = from_org_msg_cloned_outgoing_msg->data + COAP_MAX_HEADER_SIZE;

  if (coap_pkt->payload_len > 0)
  {
    // make room for outer/inner CoAP options + payload in OSCORE packet, move CoAP payload to offset 2*COAP_MAX_HEADER_SIZE
    uint8_t* dst2 = dst1 + COAP_MAX_HEADER_SIZE;

    // move payload and store/remind the new (moved) payload location in the CoAP packet
    memmove(dst2, coap_pkt->payload, coap_pkt->payload_len);
    coap_pkt->payload = dst2;
  }

  /*
    Store the observe option.
    - keep/backup the inner observe option value for observe registrations and cancellations
    - use an empty value for notifications
  */
  uint32_t observe_option = coap_pkt->observe;
  if (coap_pkt->observe > 1)
  {
    coap_pkt->observe = 0;
    OC_DBG("response is a notification; making inner 'Observe' option empty");
  }

  // serialize OSCORE plaintext 'at' offset COAP_MAX_HEADER_SIZE (inner code, inner options, payload) by using the 'moved' payload location ptr
  size_t plaintext_size = oscore_serialize_plaintext(coap_pkt, dst1);

  OC_DBG("serialized OSCORE plaintext: %" PRIu64 " bytes", plaintext_size);

  // set the OSCORE packet pointer to location of the serialized inner message (inner code, inner options, payload)
  coap_pkt->payload = dst1;
  coap_pkt->payload_len = (uint32_t)plaintext_size;

  // use sender key for encryption
  uint8_t* encryption_key = oscore_ctx->sender_key;

  // verify and encrypt OSCORE payload in coap packet , acc. MBEDTLS same input/output buffer can be used
  int ret = oc_oscore_encrypt(coap_pkt->payload, coap_pkt->payload_len, OSCORE_AEAD_TAG_LEN, encryption_key, OSCORE_KEY_LEN, nonce,
                              OSCORE_AEAD_NONCE_LEN, aad, aad_len, coap_pkt->payload);

  if (ret != 0)
  {
    OC_ERR("decrypting OSCORE payload : error (%d), ignore message", ret);
    oc_message_unref(from_org_msg_cloned_outgoing_msg);
    return -1;
  }

  OC_DBG("decrypting OSCORE payload : success (0)");

  // adjust payload length to include the size of the authentication tag
  coap_pkt->payload_len += OSCORE_AEAD_TAG_LEN;

  // set the OUTER code for the OSCORE packet (on uc request = POST/FETCH, response = 2.04/2.05)
  coap_pkt->code = oscore_get_outer_code(coap_pkt);

  // If outer code is 2.05 (OBSERVE option was set), then set the Max-Age option
  if (coap_pkt->code == CONTENT_2_05)
  {
    // no max age = no caching by client
    coap_set_header_max_age(coap_pkt, 0);
  }

  /*
      set the OSCORE option, note that checks uses the original (inner) CoAP code, not the outer code

      8.1

      (a) for a request or unicast_echo_response we always include the PIV, kid and kid_context in the OSCORE option,
          since these are needed for the receiver to derive the AEAD nonce and do replay protection

      8.3 + KNX IoT 3.6.5 (we use the AEAD nonce from request, hence no PIV)

      (b) for a response we do NOT include the PIV in the message, except for observe responses we always include the kid and kid_context
          in the OSCORE option and PIV (from second response SHALL, from first response MAY), since these are needed for
          the receiver to derive the AEAD nonce and do replay protection

      (c) note, an (4-byte) empty ACK is not processed by THIS OSCORE layer since it does not include any options/data

  */
  coap_set_header_oscore(coap_pkt, outbound_piv, outbound_piv_len, kid, kid_len, kid_context, kid_context_len);

  // reflects the 'observe' option (if present in the CoAP packet)
  coap_pkt->observe = observe_option;

  // serialize OSCORE message
  from_org_msg_cloned_outgoing_msg->length = oscore_serialize_message(coap_pkt, from_org_msg_cloned_outgoing_msg->data);
  OC_DBG("serialized OSCORE message");

  if (unicast_echo_response_by_mc)
  {
    // free context after use it above, since it was only created for multicast caused 'echo response'
    oc_oscore_free_context(oscore_ctx);
  }

  // from here on any message is encrypted ...
  UNSET_BIT(from_org_msg_cloned_outgoing_msg->endpoint.flags, OSCORE_DECRYPTED);

  #ifdef OC_CLIENT

  if (oc_process_post(&message_buffer_handler, oc_events[OUTBOUND_NETWORK_EVENT_ENCRYPTED], from_org_msg_cloned_outgoing_msg) == OC_PROCESS_ERR_FULL)
  {
    OC_ERR("could not send message");
  }

  return 0;

#endif

#if defined(OC_CLIENT) && defined(KNX_TCP_TLS)
  OC_DBG_OSCORE("Outbound network event: forwarding to TLS");
  if (!oc_tls_connected(&from_org_msg_cloned_outgoing_msg->endpoint))
  {
    OC_DBG_OSCORE("Posting INIT_TLS_CONN_EVENT");
    oc_process_post(&oc_tls_handler, oc_events[INIT_TLS_CONN_EVENT], from_org_msg_cloned_outgoing_msg);
  }
  else
#endif
  {
#ifdef KNX_TCP_TLS
    OC_DBG_OSCORE("Posting RI_TO_TLS_EVENT");
    oc_process_post(&oc_tls_handler, oc_events[RI_TO_TLS_EVENT], from_org_msg_cloned_outgoing_msg);
#endif
  }

  return 0;
}

OC_PROCESS_THREAD(oc_oscore_handler, ev, data)
{
  OC_PROCESS_BEGIN();
  while (1)
  {
    OC_PROCESS_YIELD();

    oc_message_t* message = (oc_message_t*)data;

    if (ev == oc_events[INBOUND_OSCORE_EVENT])
    {
      OC_DBG("Inbound OSCORE message, processing message");
      oc_oscore_receive_message(message);
    }
    else if (ev == oc_events[OUTBOUND_UC_OSCORE_EVENT])
    {
      OC_DBG("Outbound OSCORE message, protecting unicast message");
      oc_oscore_send_unicast_message(message);
    }
    #ifdef OC_CLIENT
    else if (ev == oc_events[OUTBOUND_MC_OSCORE_EVENT])
    {
      OC_DBG("Outbound OSCORE message, protecting multicast message");
      oc_oscore_send_multicast_message(message);
    }
    #endif
  }

  OC_PROCESS_END()
}

