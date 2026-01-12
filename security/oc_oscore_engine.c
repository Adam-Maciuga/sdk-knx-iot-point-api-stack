/*
// Copyright (c) 2020 Intel Corporation
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

#include "oc_replay.h"
#include "oc_storage.h"

#if defined OC_OSCORE
#include <inttypes.h>
#include "api/oc_events.h"
#include "mbedtls/ccm.h"
#include "messaging/coap/coap_signal.h"
#include "messaging/coap/engine.h"
#include "messaging/coap/transactions.h"
#include "oc_client_state.h"
#include "oc_oscore.h"
#include "oc_oscore_context.h"
#include "oc_oscore_crypto.h"
#include "oc_tls.h"
#include "util/oc_process.h"
#include "api/oc_knx_sec.h"

OC_PROCESS(oc_oscore_handler, "OSCORE Process");

// ssn++ and save to storage each 32 - value (lays within replay window ...)
static void increment_ssn_in_context(oc_oscore_context_t* ctx)
{
  ctx->ssn++;

  /*
   store current SSN with frequency OSCORE_WRITE_FREQ_K,
   based on recommendations in RFC 8613, appendix B.1. to prevent SSN reuse
  */
  if (ctx->ssn % OSCORE_SSN_WRITE_FREQ_K == 0)
  {

    // TODO Sender OR Recipient ID + ID Context must be used
    // in hex encoded ascii
    // ID Context may be empty 

    // save ssn per (hex) sender id and (hex) id context as storage name 'ssn+id+context'
    char storage_name[OSCORE_STORAGE_KEY_LEN] = {OSCORE_STORAGE_PREFIX};
    size_t storage_name_len;

    // claim that buffer is big enough
    storage_name_len = sizeof(storage_name);
    // add 'id'
    oc_conv_byte_array_to_hex_string(ctx->sender_id, 
                                     ctx->sender_id_len,
                                     storage_name + OSCORE_STORAGE_PREFIX_LEN, 
                                     &storage_name_len);

    // claim that buffer is big enough 
    storage_name_len = sizeof(storage_name);
    // add 'context'
    oc_conv_byte_array_to_hex_string(ctx->id_context, 
                                     ctx->id_context_len,
                                     storage_name + (OSCORE_STORAGE_PREFIX_LEN + OSCORE_SENDER_ID_LEN * 2), 
                                     &storage_name_len);


    #ifdef OC_USE_STORAGE
    oc_storage_write(storage_name, (uint8_t*)&ctx->ssn, sizeof(ctx->ssn));
    #endif
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
  An oscore context shares the client and server side context (optimization).

  # | - Message   | Client (1)        | Server (2)        | Derived Key
  A | - Request   | Sender Context    | Recipient Context | Request Key
  B | - Response  | Recipient Context | Sender Context    | Response Key

 Client  |                | Server
 A1 (8.1)| -> REQUEST  -> | A2 (8.2)
 B1 (8.4)| <- RESPONSE <- | B2 (8.3)

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
  @brief 

  @param msg the message, pushed to queue INBOUND_OSCORE_EVENT since the previous
             oscore header check was ok

  @note See details in description on top of this file 

*/
static int oc_oscore_receive_message(oc_message_t* msg)
{
  {
    bool s_mode_echo_re_request = false;
    OC_DBG_OSCORE("### process inbound OSCORE message ###");

    /*
      here we know (and set as default) it is an OSCORE message (it host the OSCORE option header)
      - not necessarily a message for us, we are able to decrypt
      - not necessarily a message without errors
      - IPv6 flag MUST already be set by lower layer (ip adapter)

    */
    msg->endpoint.flags |= OSCORE;

    // defaults
    oc_oscore_context_t* oscore_ctx = NULL;
    uint8_t* decryption_key = NULL;

    // temp local COAP packet copy 
    coap_packet_t oscore_pkt[1];

    uint8_t AAD[OSCORE_AAD_MAX_LEN], AAD_len = 0, nonce[OSCORE_AEAD_NONCE_LEN];

    OC_DBG_OSCORE("parse OUTER OSCORE message");
    if (oscore_parse_outer_message(msg, oscore_pkt) != COAP_NO_ERROR)
    {
      /*
       Here we are still scan normal COAP message content (not yet in the OSCORE part)

       - (a) response + outer option problem = 8.4, step NA  = stop processing
       - (b) request + outer option problem  = 8.2, step 6   = unsecured 4.02
      */

      if (oscore_pkt->code >= OC_GET && oscore_pkt->code <= OC_FETCH)
      { // (b)
        UNSET_BIT(msg->endpoint.flags, OSCORE);
        OC_ERR("error parsing outer message, unsecured 4.02");
        oscore_send_error(oscore_pkt, BAD_OPTION_4_02, &msg->endpoint);
      }

      // (a) + (b) 
      goto oscore_recv_error;
    }

    // check duplication on incoming UDP requests (GET, ...)
    if (oscore_pkt->transport_type == COAP_TRANSPORT_UDP && oscore_pkt->code <= OC_FETCH)
    {
      if (oc_coap_check_if_duplicate(oscore_pkt->mid, msg->endpoint.addr.ipv6.port, msg->endpoint.addr.ipv6.address))
      {
        // ignore duplicate request
        goto oscore_recv_error;
      }
    }

    /*
       Here we have:

       - request or response; with/without kid

     */

    uint8_t* request_piv = NULL;
    uint8_t request_piv_len = 0;

    if (oscore_pkt->kid_len > 0)
    { // kid is present - message is a request or 'unicast echo response'

      OC_DBG_OSCORE("searching OSCORE context from incoming request message by kid (len %d) : ", oscore_pkt->kid_len);
      OC_LOGbytes(oscore_pkt->kid, oscore_pkt->kid_len);

      // scan request recipient id from request
      oscore_ctx = oc_oscore_find_context_by_kid_and_kid_context(oscore_pkt->kid, oscore_pkt->kid_len, oscore_pkt->kid_ctx, oscore_pkt->kid_ctx_len);

      if (!oscore_ctx)
      {
        // handle s-mode 'unicast echo response' 
        if (oscore_pkt->kid_ctx_len == 10)
        {
          // need to send an s-mode 'unicast echo re-request'   
          s_mode_echo_re_request = true;

          request_piv = oscore_pkt->piv;
          request_piv_len = oscore_pkt->piv_len;

          oc_message_t* original_message = oc_replay_find_msg_by_token(oscore_pkt->token, oscore_pkt->token_len);

          // ignore an echo challenge from outside if
          // - not from me send beforehand (within timeout),
          // - was mine but already released (outside of timeout)
          if (!original_message)
          {
            goto oscore_recv_error;
          }

          // find auth/at entry with corresponding 'kid' from message
          int idx = oc_core_find_at_entry_with_osc_id(oscore_pkt->kid, oscore_pkt->kid_len);
          if (idx == -1)
          {
            /*
               response (echo challenge) not found = KNX IoT Point API (8.4 step 2) = stop processing
               - inform AL on failed echo challenge would be needed
            */

            OC_ERR("could not find Access Token matching KID, stop processing");
            goto oscore_recv_error;
          }

          // get access token
          oc_auth_at_t* at_entry = oc_get_auth_at_entry(idx);

          /*
           'Server' Side (details see method 'oc_oscore_receive_message' header)
           - create oscore Request Recipient Context  -> kid
           - create oscore Response Sender Context    -> kid

           - SSN initialized = 0, context is new
           - kid_context (ms/salt from access token)

          */
          oscore_ctx = oc_oscore_add_context(
            oc_string(at_entry->osc_id), oc_byte_string_len(at_entry->osc_id),
            oc_string(at_entry->osc_id), oc_byte_string_len(at_entry->osc_id), 
            0,
            oc_string(at_entry->osc_ms), oc_byte_string_len(at_entry->osc_ms),
            oc_string(at_entry->osc_salt), oc_byte_string_len(at_entry->osc_salt), 
            (char*)oscore_pkt->kid_ctx, oscore_pkt->kid_ctx_len, 
            idx,
            false);
        }
      }

      if (!oscore_ctx)
      { /*
           we do not have a beforehand cached context as part of the context list,
           so we have to make one (usually on a fresh request)
        */

        // find auth/at entry with corresponding kid
        int idx = oc_core_find_at_entry_with_osc_id(oscore_pkt->kid, oscore_pkt->kid_len);
        if (idx == -1)
        {
          /*
            request + secure key material (no matching kid) = 8.2 step 2 = unsecured 4.01
            - a GA that does not match to the server PUB table but is using the same multicast address
              - mc later ignored
              - uc 4.01
          */
          UNSET_BIT(msg->endpoint.flags, OSCORE);
          OC_ERR("could not find Access Token matching 'kid', unsecured 4.01");
          oscore_send_error(oscore_pkt, UNAUTHORIZED_4_01, &msg->endpoint);
          goto oscore_recv_error;
        }

        // get access token 
        oc_auth_at_t* at_entry = oc_get_auth_at_entry(idx);

        // details see method header
        oc_char_println_hex(oc_string(at_entry->osc_id), oc_byte_string_len(at_entry->osc_id));

        /*
           'Server' Side (details see method 'oc_oscore_receive_message' header)
           - create oscore Request Recipient Context  -> kid 
           - create oscore Response Sender Context    -> kid ('')

           - SSN initialized = 0, context is new
           - kid_context (ms/salt from access token)

        */
        oscore_ctx = oc_oscore_add_context("", 0,
                                           oc_string(at_entry->osc_id), oc_byte_string_len(at_entry->osc_id), 
                                           0,
                                           oc_string(at_entry->osc_ms), oc_byte_string_len(at_entry->osc_ms),
                                           oc_string(at_entry->osc_salt), oc_byte_string_len(at_entry->osc_salt),
                                           (char*)oscore_pkt->kid_ctx, oscore_pkt->kid_ctx_len, 
                                           idx,
                                           false);

        // if context is null, free one & try adding again
        if (!oscore_ctx)
        {
          oc_oscore_free_lru_recipient_context();

          /*
           'Server' Side (details see method 'oc_oscore_receive_message' header)
           - create oscore Request Recipient Context  -> kid
           - create oscore Response Sender Context    -> kid ('')

           - SSN initialized = 0, context is new
           - kid_context (ms/salt from access token)

          */
          oc_char_println_hex(oc_string(at_entry->osc_id), oc_byte_string_len(at_entry->osc_id));
          oscore_ctx = oc_oscore_add_context("", 0,
                                             oc_string(at_entry->osc_id), oc_byte_string_len(at_entry->osc_id), 
                                             0,
                                             oc_string(at_entry->osc_ms), oc_byte_string_len(at_entry->osc_ms),
                                             oc_string(at_entry->osc_salt), oc_byte_string_len(at_entry->osc_salt),
                                             (char*)oscore_pkt->kid_ctx, oscore_pkt->kid_ctx_len,
                                             idx,
                                             false);

          if (!oscore_ctx)
          {
            /*
              This should not happen, because there was with LRU a context released, 
              in case of it is request + server alloc problem before any decryption.
              - = 8.2 step 2 = unsecured 5.00
              - maybe an RST can also be implemented
            */
            UNSET_BIT(msg->endpoint.flags, OSCORE);
            OC_ERR("could not create oscore recipient context, unsecured 5.00");
            oscore_send_error(oscore_pkt, INTERNAL_SERVER_ERROR_5_00, &msg->endpoint);
            goto oscore_recv_error;
          }
        }
      }
    }
    else
    { // kid IS NOT present - message is a ... 

      if (oscore_pkt->code > OC_FETCH)
      { // ... response without KID (such as STATUS_OK = CONTENT_2_05 = 69)

        oscore_ctx = oc_oscore_find_context_by_token_mid(oscore_pkt->token, oscore_pkt->token_len,
                                                         oscore_pkt->mid, 
                                                         &request_piv, &request_piv_len,
                                                         false);

        if (!oscore_ctx)
        { // ... response ... without KID and without existing transaction associated with a security context  

          /*
           response + decryption failed = 8.4 step 8 = ignore
           - TODO check 3.6.5
          */

          OC_ERR("***could not find matching OSCORE context, ignore silently ***");
          goto oscore_recv_error;
        }

      }
      else
      { // ... request without KID  

        /*
           request + decryption problem = 8.2 step 2 = unsecured (4.02)
           - TODO check 3.6.5
        */
        UNSET_BIT(msg->endpoint.flags, OSCORE);
        OC_ERR("OSCORE protected request lacks kid param, return unsecured 4.02");
        oscore_send_error(oscore_pkt, BAD_OPTION_4_02, &msg->endpoint);
        goto oscore_recv_error;
      }
    }

    /*
       Here we have:

       - request with KID
       - response with KID
         - 'unicast echo response'
       - response without KID
         - 2.05

     */

    // set access token index that was used to decrypt 
    msg->endpoint.auth_at_index = oscore_ctx->auth_at_index;

    // 'Sender ID' is NOT NULL in case of 'echo re-request'  
    oc_endpoint_set_oscore_id(&msg->endpoint, oscore_ctx->sender_id, oscore_ctx->sender_id_len);

    // use recipient key for decryption
    decryption_key = oscore_ctx->recipient_key;

    /* If received Partial IV in message */
    if (oscore_pkt->piv_len > 0)
    {
      /* If received message is request */
      if (oscore_pkt->code >= OC_GET && oscore_pkt->code <= OC_FETCH)
      {
        uint64_t ssn; // piv -> ssn
        oscore_read_piv(oscore_pkt->piv, oscore_pkt->piv_len, &ssn);
        /* Compose AAD using received piv and context->recipient_id */
        oc_oscore_compose_AAD(oscore_ctx->recipient_id, oscore_ctx->recipient_id_len, oscore_pkt->piv, oscore_pkt->piv_len, AAD,
                              &AAD_len);
        OC_DBG_OSCORE("---> composed AAD using received Partial IV and Recipient ID");
        OC_LOGbytes_OSCORE(AAD, AAD_len);
      }

      OC_DBG_OSCORE("---> got Partial IV from incoming message : ");
      OC_LOGbytes_OSCORE(oscore_pkt->piv, oscore_pkt->piv_len);

      /* Copy received piv into oc_message_t->endpoint for requests */
      if (oscore_pkt->code >= OC_GET && oscore_pkt->code <= OC_FETCH)
      {
        memcpy(msg->endpoint.request_piv, oscore_pkt->piv, oscore_pkt->piv_len);
        msg->endpoint.request_piv_len = oscore_pkt->piv_len;
        OC_DBG_OSCORE("---> caching Partial IV for later use ...");
      }

      /* Compute nonce using received piv and context->recipient_id */
      oc_oscore_AEAD_nonce(oscore_ctx->recipient_id, oscore_ctx->recipient_id_len, oscore_pkt->piv, oscore_pkt->piv_len,
                           oscore_ctx->common_iv, nonce, OSCORE_AEAD_NONCE_LEN);

      OC_DBG_OSCORE("---> computed AEAD nonce using received Partial IV and Recipient ID");
      OC_LOGbytes_OSCORE(nonce, OSCORE_AEAD_NONCE_LEN);
    }

    /* If received message is response */
    if (oscore_pkt->code > OC_FETCH)
    {
      OC_DBG_OSCORE("---got request_piv from client callback");
      OC_LOGbytes_OSCORE(request_piv, request_piv_len);

      // the final ack of a separate response sequence is sent unencrypted
      // if the request_piv_length in the endpoint is 0. So, we cannot copy
      // it here in this case.
      /*
      if (message->endpoint.request_piv_len == 0)
      {
        // Copy request_piv from client cb/transaction into
        // oc_message_t->endpoint
        memcpy(message->endpoint.request_piv, request_piv, request_piv_len);
        message->endpoint.request_piv_len = request_piv_len;
      }
      */

      if (oscore_pkt->piv_len == 0)
      {
        /* Compute nonce using request_piv and context->sender_id */
        oc_oscore_AEAD_nonce(oscore_ctx->sender_id, oscore_ctx->sender_id_len, request_piv, request_piv_len, oscore_ctx->common_iv,
                             nonce, OSCORE_AEAD_NONCE_LEN);

        OC_DBG_OSCORE("---use AEAD nonce from request");
        OC_LOGbytes_OSCORE(nonce, OSCORE_AEAD_NONCE_LEN);
      }

      /* Compose AAD using request_piv and context->sender_id */
      oc_oscore_compose_AAD(oscore_ctx->sender_id, oscore_ctx->sender_id_len, request_piv, request_piv_len, AAD, &AAD_len);

      OC_DBG_OSCORE("---composed AAD using request_piv and Sender ID");
      OC_LOGbytes_OSCORE(AAD, AAD_len);
    }

    OC_DBG_OSCORE("### decrypting OSCORE payload ###");

    /* Verify and decrypt OSCORE payload */
    uint8_t* output = malloc(oscore_pkt->payload_len);

    // int ret = oc_oscore_decrypt(oscore_pkt->payload, oscore_pkt->payload_len,
    //                            OSCORE_AEAD_TAG_LEN, key, OSCORE_KEY_LEN,
    //                            nonce, OSCORE_AEAD_NONCE_LEN, AAD, AAD_len,
    //                            oscore_pkt->payload);
    int ret = oc_oscore_decrypt(oscore_pkt->payload, oscore_pkt->payload_len, OSCORE_AEAD_TAG_LEN, decryption_key, OSCORE_KEY_LEN,
                                nonce, OSCORE_AEAD_NONCE_LEN, AAD, AAD_len, output);

    memcpy(oscore_pkt->payload, output, oscore_pkt->payload_len);
    free(output);

    if (ret != 0)
    {
      /*
        response + encrypted problem = 8.4 step 5  = stop processing
        request + encrypted problem = 8.2 step 6 = unsecured 4.00
      */

      if (oscore_pkt->code >= OC_GET && oscore_pkt->code <= OC_FETCH)
      {
        // request
        UNSET_BIT(msg->endpoint.flags, OSCORE);
        OC_ERR("error decrypting/verifying response : (%d), unsecured 4.00", ret);
        oscore_send_error(oscore_pkt, BAD_REQUEST_4_00, &msg->endpoint);
      }

      // response
      goto oscore_recv_error;
    }

    OC_DBG_OSCORE("### successfully decrypted OSCORE payload ###");

    /* 
      Adjust payload length to size after decryption (i.e. exclude the tag)
     */
    oscore_pkt->payload_len -= OSCORE_AEAD_TAG_LEN;

    // local CoAP packet 
    coap_packet_t coap_pkt[1];

    OC_DBG_OSCORE("### parse INNER OSCORE message ###");
    if (oscore_parse_inner_message(oscore_pkt->payload, oscore_pkt->payload_len, &coap_pkt) != COAP_NO_ERROR)
    {

      /*
         response + encryption problem = ignore
         request +  problem with inner options  = 4.02 secured (EITT test 5.10.5.3)
      */
      
      if (oscore_pkt->code >= OC_GET && oscore_pkt->code <= OC_FETCH)
      {
        // request
        OC_ERR("error parsing inner message, secured 4.02");
        oscore_send_error(oscore_pkt, BAD_OPTION_4_02, &msg->endpoint);
      }

      // response 
      goto oscore_recv_error;
    }

    OC_DBG_OSCORE("### successfully parsed inner message ###");

    // if (c->credtype == OC_CREDTYPE_OSCORE_MCAST_SERVER &&
    //    coap_pkt->code != OC_POST) {
    //  OC_ERR("***non-UPDATE multicast request protected using group OSCORE "
    //         "context; silently ignore***");
    //  goto oscore_recv_error;
    //}

    // copy type, version, mid, token, observe fields from OSCORE packet to CoAP Packet
    coap_pkt->transport_type = oscore_pkt->transport_type;
    coap_pkt->version = oscore_pkt->version;
    coap_pkt->type = oscore_pkt->type;
    coap_pkt->mid = oscore_pkt->mid;
    memcpy(coap_pkt->token, oscore_pkt->token, oscore_pkt->token_len);
    coap_pkt->token_len = oscore_pkt->token_len;
    coap_pkt->observe = oscore_pkt->observe;

    // also copy kid, kid_ctx and ssn, for replay protection 

    msg->endpoint.kid_len = oscore_pkt->kid_len;
    memcpy(msg->endpoint.kid, oscore_pkt->kid, oscore_pkt->kid_len);
    msg->endpoint.kid_ctx_len = oscore_pkt->kid_ctx_len;
    memcpy(msg->endpoint.kid_ctx, oscore_pkt->kid_ctx, oscore_pkt->kid_ctx_len);

    OC_DBG_OSCORE("### serializing CoAP message ###");
    /* Serialize fully decrypted CoAP packet to message->data buffer */
    msg->length = coap_oscore_serialize_message((void*)coap_pkt, msg->data, true, true, true);

    // from here on the message is decrypted
    msg->endpoint.flags |= OSCORE_DECRYPTED;

    OC_DBG_OSCORE("### serialized decrypted CoAP message to dispatch to the CoAP layer ### :");
    PRINTipaddr_flags(msg->endpoint);

    if (s_mode_echo_re_request)
    {
        oc_oscore_free_context(oscore_ctx);
    }
  }

  OC_DBG_OSCORE("#################################");

  // dispatch the (received and decrypted) message to the CoAP layer
  if (oc_process_post(&coap_engine, oc_events[INBOUND_RI_EVENT], msg) == OC_PROCESS_ERR_FULL)
  {
    // error is > 0 ...
    goto oscore_recv_error;
  }
  return 0;

oscore_recv_error:
  oc_message_unref(msg);
  return -1;
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
  /* OSCORE layer secure multicast pseudocode
   * ----------------------------------------
   * Search for group OSCORE context
   * If found OSCORE context:
   *   Set context->sender_key as the encryption key
   *   Parse CoAP message
   *   If parse unsuccessful, return error
   *   Use context->SSN as partial IV
   *   Use context-sender_id as kid
   *   Compute nonce using partial IV and context->sender_id
   *   Compute AAD using partial IV and context->sender_id
   *   Make room for inner options and payload by moving CoAP payload to offset
   *    2 * COAP_MAX_HEADER_SIZE
   *   Serialize OSCORE plain text at offset COAP_MAX_HEADER_SIZE
   *   Encrypt OSCORE plain text at offset COAP_MAX_HEADER_SIZE
   *   Set OSCORE packet payload to location COAP_MAX_HEADER_SIZE
   *   Set OSCORE packet payload length to the plain text size + tag length (8)
   *   Set OSCORE option in OSCORE packet
   *   Serialize OSCORE message to oc_message_t
   * Dispatch oc_message_t to IP layer
   */

  // get sending ga
  const uint32_t group_address = msg->endpoint.group_address;
  OC_DBG_OSCORE("### process outbound multicast OSCORE message with ga : %04X ###", group_address);

  /* 
    find context for sending ga, in case 
    - ga = '0' = NOT initialized this call fails since no context will be available
    - ga = '0' = i want to send ga 0 this call succeeds since context will be available
  */
  oc_oscore_context_t* oscore_ctx = oc_oscore_find_context_by_group_address(group_address);
  if (oscore_ctx)
  {
    
    OC_DBG_OSCORE("found group OSCORE context for GA %04X", group_address);

    // use sender key for encryption
    uint8_t* key = oscore_ctx->sender_key;

    OC_DBG_OSCORE("### parse CoAP message ###");

    coap_packet_t coap_pkt[1];
    coap_status_t code = coap_udp_parse_message(coap_pkt, msg->data, msg->length);

    if (code != COAP_NO_ERROR)
    {
      OC_ERR("***error parsing multicast CoAP packet***");
      oc_message_unref(msg);
      return -1;
    }

    OC_DBG_OSCORE("### parsed CoAP multicast message ###");

    uint8_t piv[OSCORE_PIV_LEN], piv_len = 0,
            kid[OSCORE_SENDER_ID_LEN], kid_len = 0,
            nonce[OSCORE_AEAD_NONCE_LEN],
            AAD[OSCORE_AAD_MAX_LEN], AAD_len = 0;

    OC_DBG_OSCORE("### protecting multicast request ###");

    // request - use SSN as Partial IV
    oscore_store_piv(piv, &piv_len, oscore_ctx->ssn);
    OC_LOGbytes_OSCORE(piv, piv_len);

    /* Increment SSN */
    increment_ssn_in_context(oscore_ctx);

    /* Use context-sender_id as kid */
    memcpy(kid, oscore_ctx->sender_id, oscore_ctx->sender_id_len);
    kid_len = oscore_ctx->sender_id_len;

    /* Compute nonce using partial IV and context->sender_id */
    oc_oscore_AEAD_nonce(oscore_ctx->sender_id, oscore_ctx->sender_id_len, piv, piv_len, oscore_ctx->common_iv, nonce,
                         OSCORE_AEAD_NONCE_LEN);

    OC_DBG_OSCORE("---computed AEAD nonce using Partial IV and Sender ID");
    OC_LOGbytes_OSCORE(nonce, OSCORE_AEAD_NONCE_LEN);

    /* Compose AAD using partial IV and context->sender_id */
    oc_oscore_compose_AAD(oscore_ctx->sender_id, oscore_ctx->sender_id_len, piv, piv_len, AAD, &AAD_len);
    OC_DBG_OSCORE("---composed AAD using Partial IV and Sender ID");
    OC_LOGbytes_OSCORE(AAD, AAD_len);

    /* Move CoAP payload to offset 2*COAP_MAX_HEADER_SIZE to accommodate for
       Outer+Inner CoAP options in the OSCORE packet.
    */
    if (coap_pkt->payload_len > 0)
    {
      memmove(msg->data + 2 * COAP_MAX_HEADER_SIZE, coap_pkt->payload, coap_pkt->payload_len);

      /* Store the new payload location in the CoAP packet */
      coap_pkt->payload = msg->data + 2 * COAP_MAX_HEADER_SIZE;
    }

    // serialize OSCORE plain text at offset COAP_MAX_HEADER_SIZE (code, inner options, payload)
    size_t plaintext_size = oscore_serialize_plaintext(coap_pkt, msg->data + COAP_MAX_HEADER_SIZE);

    OC_DBG_OSCORE("### serialized OSCORE plaintext: %" PRIu64 " bytes ###", plaintext_size);

    /* Set the OSCORE packet payload to point to location of the serialized
       inner message.
    */
    coap_pkt->payload = msg->data + COAP_MAX_HEADER_SIZE;
    coap_pkt->payload_len = (uint32_t)plaintext_size;

    /* Encrypt OSCORE plaintext */
    OC_DBG_OSCORE("### encrypting OSCORE plaintext ###");

    int ret = oc_oscore_encrypt(coap_pkt->payload, coap_pkt->payload_len, OSCORE_AEAD_TAG_LEN, key, OSCORE_KEY_LEN, nonce,
                                OSCORE_AEAD_NONCE_LEN, AAD, AAD_len, coap_pkt->payload);

    if (ret != 0)
    {
      OC_ERR("***error encrypting OSCORE plaintext***");
      oc_message_unref(msg);
      return -1;
    }

    OC_DBG_OSCORE("### successfully encrypted OSCORE plaintext ###");

    /* Adjust payload length to include the size of the authentication tag */
    coap_pkt->payload_len += OSCORE_AEAD_TAG_LEN;

    /* Set the Outer code for the OSCORE packet (on mc request = POST) */
    coap_pkt->code = OC_POST;

    /*
      Wireshark fix - include the 'kid_context' (in msg) = 'ID Context' (OSCORE)
      on the wire as well, otherwise cannot decode OSCORE messages that use
      implicit ID contexts.
     
    */
    uint8_t kid_context[OSCORE_ID_CONTEXT_LEN];
    memcpy(kid_context, oscore_ctx->id_context, oscore_ctx->id_context_len);
    const uint8_t kid_context_len = oscore_ctx->id_context_len;

    // set the OSCORE option
    coap_set_header_oscore(coap_pkt, piv, piv_len, kid, kid_len, kid_context, kid_context_len);

    // serialize OSCORE message
    msg->length = oscore_serialize_message(coap_pkt, msg->data);
    OC_DBG_OSCORE("### OSCORE message serialized ###");
  }
  else
  {
    OC_ERR("*** could not find group OSCORE context for the given ga ***");
    oc_message_unref(msg);
    return -1;
  }

  // dispatch DIRECTLY to IP layer (no message queue anymore, as for unicast messages)
  oc_send_discovery_request(msg);
  oc_message_unref(msg);
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
  /*
    Cases for a unicast (outbound) message:

    (a) context will be retrieved by kid
        - uc outbound response -> former inbound request such as uc read request -> response OR uc write requests -> 2.04 changed
     
    (b) context will be retrieved by 'Sender ID'
        - uc initial outbound request -> server application uc request
        - TODO why needed, at token is always present 
   
    (c) context retried by group address
        - uc initial outbound request -> server application uc r/w request via '/k'  

    (d) context retried by token/mid
        - uc outbound ACK 2.04 Changed with payload -> inbound request
         - TODO why needed, at token is always present( for a response this makes no sense, only when receiving a 2.04 )

    (e) context retried by NEW context after (a)
        - uc echo response -> inbound mc/uc r/w request to '/k' OR inbound uc r/w request to '*' (e.g.; /dev/pm)
    
   */

  OC_DBG_OSCORE("### process outbound unicast OSCORE message ###");
  
  // get message, otherwise stop
  oc_message_t* outgoing_msg = oc_internal_allocate_outgoing_message();
  if (!outgoing_msg)
  {
    OC_ERR("***No memory to allocate outgoing message!***");
    goto oscore_send_error;
  }

  // clone handed over 'oscore' message (msg) into sent out 'oscore' message
  outgoing_msg->length = msg->length;
  memcpy(outgoing_msg->data, msg->data, msg->length);
  memcpy(&outgoing_msg->endpoint, &msg->endpoint, sizeof(oc_endpoint_t));

  // if msg is 'tracked' remove one reference, the outgoing msg takes care now    
  bool msg_is_currently_tracked = msg->ref_count > 1 ? true: false;
  // msg->data pointer is void from now on ...
  oc_message_unref(msg);

  // create local CoAP packet 
  coap_packet_t coap_pkt[1];
  coap_status_t code = EMPTY_0_00;

  #ifdef OC_TCP
  if (outgoing_msg->endpoint.flags & TCP)
  {
    code = coap_tcp_parse_message(coap_pkt, outgoing_msg->data, (uint32_t)outgoing_msg->length);
  }
  else
  #endif 
  {
    // parse outgoing message and copy to CoAP packet ... (handed over msg may be unreferenced, ->data may be NULL)
    code = coap_udp_parse_message(coap_pkt, outgoing_msg->data, outgoing_msg->length);
  }

  if (code != COAP_NO_ERROR)
  {
    OC_ERR("### parsed CoAP unicast packet : error");
    goto oscore_send_error;
  }

  OC_DBG_OSCORE("### parsed CoAP unicast packet : ok");

  // init context
  oc_oscore_context_t* oscore_ctx = NULL;

  oc_auth_at_t* auth_at_entry = oc_get_auth_at_entry(outgoing_msg->endpoint.auth_at_index);
  if (auth_at_entry != NULL)
  { // (a)

    // TODO do we need also ID Context here --> if we have same kid from several (test fails when changed)

    // get sender context for the access token from 'kid', picks the first hit
    oscore_ctx = oc_oscore_find_context_by_kid((uint8_t*)oc_string(auth_at_entry->osc_id), oc_byte_string_len(auth_at_entry->osc_id));

    /*
    oscore_ctx =
      oc_oscore_find_context_by_kid_and_kid_context(
        (uint8_t*)oc_string(auth_at_entry->osc_id), oc_byte_string_len(auth_at_entry->osc_id),
        (uint8_t*)oc_string(auth_at_entry->osc_contextid), oc_byte_string_len(auth_at_entry->osc_contextid));
        */
    

    OC_DBG_OSCORE("### (a) Found context by access token ###");
  }

  if (oscore_ctx == NULL)
  { // (b)

    // TODO check what is in osc_id included when receive a message - on request (SN) or response (empty string)

    // search the oscore id, e.g. the cnf:osc:id ('kid' , 'Sender ID')
    oscore_ctx = oc_oscore_find_context_by_oscore_id(outgoing_msg->endpoint.oscore_id, outgoing_msg->endpoint.oscore_id_len);

    OC_DBG_OSCORE("### (b) Found context by 'Sender ID' ###");
    oc_char_println_hex(outgoing_msg->endpoint.oscore_id, (int)outgoing_msg->endpoint.oscore_id_len);
  }

  if (oscore_ctx == NULL)
  { // (c)

    oscore_ctx = oc_oscore_find_context_by_group_address(outgoing_msg->endpoint.group_address);

    OC_DBG_OSCORE("### (c) Found context by 'ga' %04X ###", outgoing_msg->endpoint.group_address);
  }

  if (oscore_ctx == NULL)
  { // (d)  

    // search for context using token/mid 
    oscore_ctx = oc_oscore_find_context_by_token_mid(coap_pkt->token, coap_pkt->token_len,
                                                     coap_pkt->mid, 
                                                     NULL, NULL, 
                                                     false);
  }

  // we haven't found any context (a) ... (d), so we free the message we just created
  if (oscore_ctx == NULL)
  {
    oc_message_unref(outgoing_msg);
    OC_ERR("no OSCORE context found, error");
    goto oscore_send_error;
  }

  // some definitions
  bool is_request = coap_pkt->code >= OC_GET && coap_pkt->code <= OC_FETCH;
  bool is_ack_with_empty_payload = coap_pkt->type == COAP_TYPE_ACK && coap_pkt->code == EMPTY_0_00;
  bool is_separate_con_response = coap_pkt->type == COAP_TYPE_CON;
  bool unicast_echo_response = false;

  /*
   s-mode = only then ga len is > '0'

   - (x0) inbound s-mode mc to /k : uc echo response, id context 10 byte rnd, allocate a tmp echo challenge responder context
   - (x1) inbound s-mode uc to /k : uc echo response, id context from inbound client, use existing responder context

   - (x2) inbound s-mode mc to /k : NO uc echo response, n/a in outbound unicast
   - (x3) inbound s-mode uc to /k : NO uc echo response, pass through = normal response

   - (x4) inbound mc to *  : n/a in outbound unicast
   - (x5) inbound uc to *  : generic response, pass through
   - (x6) inbound uc to *  : uc echo response, = (x1) 
  */

  if (!is_request)
  { // is an origin unicast response

    if (auth_at_entry->ga_len > 0)
    { // s-mode

      if (outgoing_msg->endpoint.flags & ECHO_CAUSED_BY_MC_SRC)
      { // x0

          // (e) - overwrites context retrieved by (a) ... (d)

          // clear marker
          UNSET_BIT(outgoing_msg->endpoint.flags, ECHO_CAUSED_BY_MC_SRC);

          // mc echo data = random
          unsigned char rnd[10];

          mbedtls_ctr_drbg_context* ctr_drbg_context = oc_random_get_ctr_drbg_context();
          mbedtls_ctr_drbg_random(ctr_drbg_context, rnd, sizeof(rnd));

          // echo response - use request SSN as Partial IV
          uint64_t ssn_from_request; // piv -> ssn
          oscore_read_piv(outgoing_msg->endpoint.request_piv, outgoing_msg->endpoint.request_piv_len, &ssn_from_request);

          /*
                 'Server' Side (details see method 'oc_oscore_receive_message' header)
                 - create oscore Request Recipient Context  -> kid
                 - create oscore Response Sender Context    -> kid
                   - 'Recipient ID' = 'Sender ID', used for the AAD composition

                 - SSN initialized = 0, context is new
                 - kid_context = rnd, ms/salt from access token

          */
          oscore_ctx = oc_oscore_add_context(
            oc_string(auth_at_entry->osc_id), oc_byte_string_len(auth_at_entry->osc_id),
            oc_string(auth_at_entry->osc_id), oc_byte_string_len(auth_at_entry->osc_id),
            ssn_from_request,
            oc_string(auth_at_entry->osc_ms), oc_byte_string_len(auth_at_entry->osc_ms),
            oc_string(auth_at_entry->osc_salt), oc_byte_string_len(auth_at_entry->osc_salt),
            (char*)rnd, 10,
            outgoing_msg->endpoint.auth_at_index, 
            false);

          unicast_echo_response = true;

          OC_DBG_OSCORE("Echo Response caused by s-mode mc");
      }
      else if (outgoing_msg->endpoint.flags & ECHO_CAUSED_BY_UC_SRC)
      { // x1

        // context retrieved by (a) ... (d)

        // clear marker
        UNSET_BIT(outgoing_msg->endpoint.flags, ECHO_CAUSED_BY_UC_SRC);
        unicast_echo_response = true;

        OC_DBG_OSCORE("Echo Response caused by s-mode uc");
      }

      // x2, x3
    }
    else
    {
      if (outgoing_msg->endpoint.flags & ECHO_CAUSED_BY_UC_SRC)
      { // x6

        // context retrieved by (a) ... (d)

        // clear marker
        UNSET_BIT(outgoing_msg->endpoint.flags, ECHO_CAUSED_BY_UC_SRC);
        unicast_echo_response = true;

        OC_DBG_OSCORE("Echo Response caused by common uc");
      }

      // x4, x5 
      
    }
  }

  // (a) ... (e) - the context is a new in case (e) otherwise an existing one 
  if (oscore_ctx)
  { // check again since context from (e) may fail
    OC_DBG_OSCORE("#################################");
    OC_DBG_OSCORE("Use OSCORE context with 'Sender ID' = %p : ", oscore_ctx->sender_id);

    // use sender key for encryption
    uint8_t* key = oscore_ctx->sender_key;

    // names are from RFC OSCORE option 
    uint8_t piv[OSCORE_PIV_LEN], piv_len = 0;
    uint8_t kid[OSCORE_SENDER_ID_LEN], kid_len = 0;
    uint8_t kid_context[OSCORE_ID_CONTEXT_LEN], kid_context_len = 0;
    uint8_t nonce[OSCORE_AEAD_NONCE_LEN];
    uint8_t aad[OSCORE_AAD_MAX_LEN], aad_len = 0;

    if (is_request
        #ifdef OC_TCP
        || coap_pkt->code == PING_7_02 || coap_pkt->code == ABORT_7_05 || coap_pkt->code == CSM_7_01
        #endif
    ) 
    { // CoAP request

      OC_DBG_OSCORE("### protecting outgoing unicast request ###");

      // request - use context SSN as Partial IV
      oscore_store_piv(piv, &piv_len, oscore_ctx->ssn);

      // debugging
      OC_DBG_OSCORE("sending request, using SSN as Partial IV : ");
      OC_LOGbytes_OSCORE(piv, piv_len);

      /*
        increment SSN
        - an initial CON/NON request (transaction present) -> a first read request 
        - any request (transaction not present) -> a write request  

        keep SSN 
        - CON retransmissions lower than max retransmits (4) use the same SSN
      */
      coap_transaction_t* transaction = coap_get_transaction_by_token(coap_pkt->token, coap_pkt->token_len);

      bool is_initial_request = transaction && transaction->retrans_counter == 0;
      bool is_any_request = !transaction;

      if (is_initial_request || is_any_request)
        increment_ssn_in_context(oscore_ctx);

      #ifdef OC_CLIENT
      
      // find client cb from the former request
      oc_client_cb_t* cb = oc_ri_find_client_cb_by_token(coap_pkt->token, coap_pkt->token_len);

      if (cb)
      {
        // copy NEW PIV into client cb data
        memcpy(cb->piv, piv, piv_len);
        cb->piv_len = piv_len;
      }
      
      #endif

      // use 'Sender ID' as kid
      memcpy(kid, oscore_ctx->sender_id, oscore_ctx->sender_id_len);
      kid_len = oscore_ctx->sender_id_len;

      // use 'ID Context' as kid context
      memcpy(kid_context, oscore_ctx->id_context, oscore_ctx->id_context_len);
      kid_context_len = oscore_ctx->id_context_len;

      // compute AEAD nonce using partial IV and 'Sender ID'
      oc_oscore_AEAD_nonce(oscore_ctx->sender_id, oscore_ctx->sender_id_len, 
                           piv, piv_len, 
                           oscore_ctx->common_iv, nonce,
                           OSCORE_AEAD_NONCE_LEN);

      OC_DBG_OSCORE("---computed AEAD nonce using Partial IV and Sender ID");
      OC_LOGbytes_OSCORE(nonce, OSCORE_AEAD_NONCE_LEN);
      

      // compose AAD using partial IV and 'Sender ID'
      oc_oscore_compose_AAD(oscore_ctx->sender_id, oscore_ctx->sender_id_len, 
                            piv, piv_len, 
                            aad, &aad_len);

      OC_DBG_OSCORE("---composed AAD using Partial IV and Sender ID");
      OC_LOGbytes_OSCORE(aad, aad_len);
     

      // copy PIV to CoAP - handed over - unicast message (not the outgoing message)
      if (msg_is_currently_tracked)
      {
        memcpy(msg->endpoint.request_piv, piv, piv_len);
        msg->endpoint.request_piv_len = piv_len;

        OC_DBG_OSCORE("sending request, caching PIV for later use ...");
        OC_LOGbytes_OSCORE(msg->endpoint.request_piv, msg->endpoint.request_piv_len);
      }
    }
    else
    { // CoAP response

      // TODO why , what message is it in OSCORE (we are in OSC layer)? --> check to send oscore error in receiving layer
      if (outgoing_msg->endpoint.request_piv_len == 0)
      { // original request was not protected by OSCORE

        OC_DBG("not protecting outgoing unicast response, original request was not protected by OSCORE");
        goto oscore_send_dispatch;
      }

      // response - use SSN as Partial IV // TODO change parameter order 
      oscore_store_piv(piv, &piv_len, oscore_ctx->ssn);

      // debugging
      OC_DBG_OSCORE("protecting outgoing unicast response, using SSN as Partial IV : ");
      OC_LOGbytes_OSCORE(piv, piv_len);

      coap_transaction_t* transaction = coap_get_transaction_by_token(coap_pkt->token, coap_pkt->token_len);

      bool is_initial_response = transaction && transaction->retrans_counter == 0;
      bool is_any_response = !transaction;
     

      /*
        increment SSN
        - an initial CON/NON response (transaction present) -> a read response
        - any response (transaction not present) -> a read response 
        - any CON response (CON type present) 

        keep SSN
        - CON retransmissions lower than max retransmits (4) use the same SSN
        - echo response 
      */
      if (is_initial_response || is_any_response || is_separate_con_response)
        increment_ssn_in_context(oscore_ctx);

      // RFC 8613, 8.3 or KNX IoT 3.6.5 (# 2870)
      // echo response needs to be in here (see 3.6.5 point api) otherwise it may cause nonce reuse
      if (unicast_echo_response)
      {
        // RFC 8613, 8.3, point 3 lower * 
        // echo response +  -> use a new PIV to compute a new AEAD nonce 
        oc_oscore_AEAD_nonce(oscore_ctx->sender_id, oscore_ctx->sender_id_len, 
                             piv, piv_len,
                             oscore_ctx->common_iv, 
                             nonce, OSCORE_AEAD_NONCE_LEN);
        
        OC_DBG_OSCORE("computed AEAD nonce by using PIV + Response Sender ID (echo response) : ");
        OC_LOGbytes_OSCORE(nonce, OSCORE_AEAD_NONCE_LEN);
      }
      else
      {
        // RFC 8613, 8.3, point 3 upper *
        // separate response -> reuse the PIV and Sender ID from the request to compute the same AEAD nonce as used for the inbound request
        oc_oscore_AEAD_nonce(oscore_ctx->recipient_id, oscore_ctx->recipient_id_len, 
                             outgoing_msg->endpoint.request_piv, outgoing_msg->endpoint.request_piv_len,
                             oscore_ctx->common_iv, 
                             nonce, OSCORE_AEAD_NONCE_LEN);
        
        OC_DBG_OSCORE("computed AEAD nonce by using PIV and Request Sender ID (no echo response) : ");
        OC_LOGbytes_OSCORE(nonce, OSCORE_AEAD_NONCE_LEN);
      }

      // request Sender ID = request Recipient ID + request PIV -> https://www.rfc-editor.org/rfc/rfc8613#section-5.4
      oc_oscore_compose_AAD(oscore_ctx->recipient_id, oscore_ctx->recipient_id_len, 
                            outgoing_msg->endpoint.request_piv, outgoing_msg->endpoint.request_piv_len, 
                            aad, &aad_len);
      
      OC_DBG_OSCORE("composed AAD by using request PIV and Recipient ID : ");
      OC_LOGbytes_OSCORE(aad, aad_len);
    }

    /*
      Here requests and responses end up
      - 

    */

    // move CoAP payload to offset 2*COAP_MAX_HEADER_SIZE to accommodate for Outer+Inner CoAP options in the OSCORE packet
    if (coap_pkt->payload_len > 0)
    {
      memmove(outgoing_msg->data + 2 * COAP_MAX_HEADER_SIZE, coap_pkt->payload, coap_pkt->payload_len);

      /* Store the new payload location in the CoAP packet */
      coap_pkt->payload = outgoing_msg->data + 2 * COAP_MAX_HEADER_SIZE;
    }

    /* Store the observe option. Retain the inner observe option value
     * for observe registrations and cancellations. Use an empty value for
     * notifications.
     */
    int32_t observe_option = coap_pkt->observe;
    if (coap_pkt->observe > 1)
    {
      coap_pkt->observe = 0;
      OC_DBG(" response is a notification; making inner Observe option empty");
    }

    // serialize OSCORE plaintext at offset COAP_MAX_HEADER_SIZE (code, inner options, payload)
    size_t plaintext_size = oscore_serialize_plaintext(coap_pkt, outgoing_msg->data + COAP_MAX_HEADER_SIZE);

    OC_DBG_OSCORE("### serializing OSCORE plaintext with %" PRIu64 " bytes ###", plaintext_size);

    // set the OSCORE packet payload to point to location of the serialized inner message
    coap_pkt->payload = outgoing_msg->data + COAP_MAX_HEADER_SIZE;
    coap_pkt->payload_len = (uint32_t)plaintext_size;

    OC_DBG_OSCORE("### encrypting OSCORE plaintext ###");

    int ret = oc_oscore_encrypt(coap_pkt->payload, coap_pkt->payload_len, OSCORE_AEAD_TAG_LEN, key, OSCORE_KEY_LEN, nonce,
                                OSCORE_AEAD_NONCE_LEN, aad, aad_len, coap_pkt->payload);

    if (ret != 0)
    {
      OC_ERR("***error encrypting OSCORE plaintext***");
      goto oscore_send_error;
    }

    OC_DBG_OSCORE("### encrypted successfully OSCORE plaintext ###");

    // adjust payload length to include the size of the authentication tag
    coap_pkt->payload_len += OSCORE_AEAD_TAG_LEN;

    // set the OUTER code for the OSCORE packet (on uc request = POST/FETCH, response = 2.04/2.05)
    coap_pkt->code = oscore_get_outer_code(coap_pkt);

    // If outer code is 2.05 (OBSERVE option was set), then set the Max-Age option
    if (coap_pkt->code == CONTENT_2_05)
    {
      coap_set_header_max_age(coap_pkt, 0);
    }


    // set the OSCORE option, note that checks below uses the original CoAP code, not the OUTER (see above)
    // TODO update according to changes above 
    if (is_request || is_ack_with_empty_payload || is_separate_con_response || unicast_echo_response)
    {
      if (unicast_echo_response)
      {
        // include the Response Sender ID as kid 
        memcpy(kid, oscore_ctx->sender_id, oscore_ctx->sender_id_len);
        kid_len = oscore_ctx->sender_id_len;

        // include the 10 byte rnd id context as kid context
        memcpy(kid_context, oscore_ctx->id_context, oscore_ctx->id_context_len);
        kid_context_len = oscore_ctx->id_context_len;

        OC_DBG_OSCORE("### copy kid/kid_context for unicast echo response  ###");

        oc_oscore_free_context(oscore_ctx);
      }

      // set the OSCORE option TODO here also the uc echo response must end up 
      coap_set_header_oscore(coap_pkt, piv, piv_len, kid, kid_len, kid_context, kid_context_len);

      // debugging
      OC_DBG_OSCORE("sending response, using SSN as Partial IV (r/ack/s-con/echo)) : ");
      OC_LOGbytes_OSCORE(piv, piv_len);
    }
    else
    {
      // other responses use the (cached) piv of the matching request, stored in the ep/client_cb
      coap_set_header_oscore(coap_pkt, NULL, 0, kid, kid_len, kid_context, kid_context_len);

      // debugging
      OC_DBG_OSCORE("sending response, using SSN as Partial IV (others)) : ");
      OC_LOGbytes_OSCORE(piv, piv_len);
    }

    // reflects the 'observe' option (if present in the CoAP packet)
    coap_pkt->observe = observe_option;

    // serialize OSCORE message
    outgoing_msg->length = oscore_serialize_message(coap_pkt, outgoing_msg->data);
    OC_DBG_OSCORE("### OSCORE message serialized ###");
  }

  // from here on any message is encrypted ...
  UNSET_BIT(outgoing_msg->endpoint.flags, OSCORE_DECRYPTED);

oscore_send_dispatch:

#ifdef OC_CLIENT
  // dispatch message 
  if (oc_process_post(&message_buffer_handler, oc_events[OUTBOUND_NETWORK_EVENT_ENCRYPTED], outgoing_msg) == OC_PROCESS_ERR_FULL)
  {
    OC_ERR("could not send message");
  }
  return 0;
#endif

#ifdef OC_CLIENT
#ifdef OC_SECURITY
  OC_DBG_OSCORE("Outbound network event: forwarding to TLS");
  if (!oc_tls_connected(&outgoing_msg->endpoint))
  {
    OC_DBG_OSCORE("Posting INIT_TLS_CONN_EVENT");
    oc_process_post(&oc_tls_handler, oc_events[INIT_TLS_CONN_EVENT], outgoing_msg);
  }
  else
#endif 
#endif 
  {
#ifdef OC_SECURITY
    OC_DBG_OSCORE("Posting RI_TO_TLS_EVENT");
    oc_process_post(&oc_tls_handler, oc_events[RI_TO_TLS_EVENT], outgoing_msg);
#endif
  }
  return 0;

oscore_send_error:
  OC_ERR("received malformed CoAP packet from stack");
  oc_message_unref(outgoing_msg);
  return -1;
}

OC_PROCESS_THREAD(oc_oscore_handler, ev, data)
{
  OC_PROCESS_BEGIN();
  while (1)
  {
    OC_PROCESS_YIELD();

    if (ev == oc_events[INBOUND_OSCORE_EVENT])
    {
      OC_DBG_OSCORE("Incoming OSCORE event: encrypted request");
      oc_oscore_receive_message(data);
    }
    else if (ev == oc_events[OUTBOUND_UC_OSCORE_EVENT])
    {
      OC_DBG_OSCORE("Outgoing OSCORE event: protecting unicast message");
      oc_oscore_send_unicast_message(data);
    }
#ifdef OC_CLIENT
    else if (ev == oc_events[OUTBOUND_MC_OSCORE_EVENT])
    {
      OC_DBG_OSCORE("Outgoing OSCORE event: protecting multicast message");
      oc_oscore_send_multicast_message(data);
    }
#endif
  }

  OC_PROCESS_END()
}
#else 
typedef int dummy_declaration;
#endif 