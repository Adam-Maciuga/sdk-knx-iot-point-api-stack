/*
// Copyright (c) 2020 Intel Corporation
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
                                     storage_name + (int)OSCORE_STORAGE_PREFIX_LEN, 
                                     &storage_name_len);

    // claim that buffer is big enough 
    storage_name_len = sizeof(storage_name);
    // add 'context'
    oc_conv_byte_array_to_hex_string(ctx->id_context, 
                                     ctx->id_context_len,
                                     storage_name + (int)(OSCORE_STORAGE_PREFIX_LEN + OSCORE_SENDER_ID_LEN * 2), 
                                     &storage_name_len);


    #ifdef OC_USE_STORAGE
    oc_storage_write(storage_name, (uint8_t*)&ctx->ssn, sizeof(ctx->ssn));
    #endif
  }
}

/*
  GENERAL RULE
  ============
  For an incoming request or response, if I can decode it successfully,
  my answer is always secure.

  MESSAGE
  =======
  An oscore context shares the client and server side context (optimization).

  # | - Message   | Client (1)        | Server (2)        | Derived Key
  A | - Request   | Sender Context    | Recipient Context | Request Key
  B | - Response  | Recipient Context | Sender Context    | Response Key

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

  Receive a message
  =================

    Sender (IA 2.0.1)
    -----------------
      Access Token (configured by MaC)
      - osc.id 0001 (usually the sending GA (mc) or device SN (uc))
      - osc.ms <ms_1>
      - osc.salt <salt_1>
      - osc.contextId 2001140921 (IA + time stamp (seconds))

      Sender Context
      - Sender, ID 0001
      - ID Context, 2001140921
      > AES Key, by device generated 

    Message (unicast or multicast)
    ------------------------------
    - kid 0001
    - kid_context 2001140921

    Receiver (IA 2.0.2)
    -------------------
      Access Token (configured by MaC)
      - osc.id 0001 (usually the sending GA (mc) or device SN (uc))
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

  @param msg the message, pushed to queue INBOUND_OSCORE_EVENT since the previous oscore header check was ok

  @note See details in description on top of this file 

*/
static int oc_oscore_receive_message(oc_message_t* msg)
{
  {
    bool s_mode_re_request = false;
    OC_DBG_OSCORE("### process OSCORE message ###");

    /*
      here we know (and set as default) it is an OSCORE message (it host the OSCORE option header)
      - not necessarily a message for us, we are able to decrypt
      - not necessarily a message without errors 

    */
    msg->endpoint.flags |= OSCORE;

    // defaults
    oc_oscore_context_t* oscore_ctx = NULL;
    uint8_t* decryption_key = NULL;

    // temp local COAP packet copy 
    coap_packet_t oscore_pkt[1];

    uint8_t AAD[OSCORE_AAD_MAX_LEN], AAD_len = 0, nonce[OSCORE_AEAD_NONCE_LEN];

    OC_DBG_OSCORE("### parse OUTER OSCORE message ###");
    if (oscore_parse_outer_message(msg, oscore_pkt) != COAP_NO_ERROR)
    {
      /*
       Here we are still scan normal COAP message content (not yet in the OSCORE part)

       - response + outer option problem = 8.4, step NA  = stop processing
       - request + outer option problem  = 8.2, step 6   = unsecured 4.02
      */

      if (oscore_pkt->code >= OC_GET && oscore_pkt->code <= OC_FETCH)
      {
        // request
        msg->endpoint.flags &= ~OSCORE;
        OC_ERR("***error parsing outer message, unsecured 4.02***");
        oscore_send_error(oscore_pkt, BAD_OPTION_4_02, &msg->endpoint);
      }

      // response 
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

    uint8_t* request_piv = NULL;
    uint8_t request_piv_len = 0;

    if (oscore_pkt->kid_len > 0)
    { // kid IS present - message is a request 

      OC_DBG_OSCORE("---> searching OSCORE context from incoming message by kid (len %d) : ", oscore_pkt->kid_len);
      OC_LOGbytes(oscore_pkt->kid, oscore_pkt->kid_len);

      oscore_ctx = oc_oscore_find_context_by_kid_and_kid_context(oscore_pkt->kid, oscore_pkt->kid_len, oscore_pkt->kid_ctx, oscore_pkt->kid_ctx_len);

      if (!oscore_ctx)
      {
        // Handle S-Mode Echo Challenge Responses
        if (oscore_pkt->kid_ctx_len == 10)
        {
          s_mode_re_request = true;

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

          // find auth/at entry with corresponding 'kid' from message, as osc:id part of access token
          int idx = oc_core_find_at_entry_with_osc_id(oscore_pkt->kid, oscore_pkt->kid_len);
          if (idx == -1)
          {
            /*
               response (echo challenge) not found = KNX IoT Point API (8.4 step 2) = stop processing
               - inform AL on failed echo challenge would be needed
            */

            OC_ERR("***Could not find Access Token matching KID, stop processing***");
            goto oscore_recv_error;
          }

          // get access token
          oc_auth_at_t* entry = oc_get_auth_at_entry(idx);

          // Create response recipient context
          oscore_ctx = oc_oscore_add_context(
            oc_string(entry->osc_id), oc_byte_string_len(entry->osc_id), 
            oc_string(entry->osc_id), oc_byte_string_len(entry->osc_id), // Recipient Id is osc.id
            0, // one time use anyway
            oc_string(entry->osc_ms), oc_byte_string_len(entry->osc_ms),
            oc_string(entry->osc_salt), oc_byte_string_len(entry->osc_salt), 
            (char*)oscore_pkt->kid_ctx, oscore_pkt->kid_ctx_len, 
            idx,
            false);
        }
      }

      if (!oscore_ctx)
      { // we do not have a beforehand cached context as part of the context list,
        // so we have to make one (usually on a fresh request)

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
          msg->endpoint.flags &= ~OSCORE;

          OC_ERR("***Could not find Access Token matching 'kid', unsecured 4.01***");
          oscore_send_error(oscore_pkt, UNAUTHORIZED_4_01, &msg->endpoint);
          goto oscore_recv_error;
        }

        // get access token 
        oc_auth_at_t* at_entry = oc_get_auth_at_entry(idx);

        // details see method header
        OC_DBG_OSCORE("adding oscore REQUEST recipient context + RESPONSE sender context with Recipient ID : ");
        oc_char_println_hex(oc_string(at_entry->osc_id), oc_byte_string_len(at_entry->osc_id));

        // 'Server' Side (details see method 'oc_oscore_receive_message' header)
        // create oscore REQUEST  recipient context
        // create oscore RESPONSE sender context = kid  (h '') + kid_context  (ms/salt from token)
        oscore_ctx = oc_oscore_add_context("", 0, 
                                           oc_string(at_entry->osc_id), oc_byte_string_len(at_entry->osc_id), 
                                           0, // SSN for RESPONSE sender context (initialized, context is new)
                                           oc_string(at_entry->osc_ms), oc_byte_string_len(at_entry->osc_ms),
                                           oc_string(at_entry->osc_salt), oc_byte_string_len(at_entry->osc_salt),
                                           (char*)oscore_pkt->kid_ctx, oscore_pkt->kid_ctx_len, 
                                           idx,
                                           false);

        // if context is null, free one & try adding again
        if (!oscore_ctx)
        {
          oc_oscore_free_lru_recipient_context();

          // SERVER SIDE on request: create oscore REQUEST recipient context + RESPONSE sender context from that entry
          OC_DBG_OSCORE("adding oscore REQUEST recipient context + RESPONSE sender context with Recipient ID : ");
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
            msg->endpoint.flags &= ~OSCORE;

            OC_ERR("***Could not create oscore recipient context, unsecured 5.00***");
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

        OC_DBG_OSCORE("### searching for OSCORE context by token ###");
        oscore_ctx = oc_oscore_find_context_by_token_mid(oscore_pkt->token, oscore_pkt->token_len,
                                                         oscore_pkt->mid, &request_piv, &request_piv_len,
                                                         msg->endpoint.flags & TCP);

        if (!oscore_ctx)
        { // ... response ... without existing transaction associated with a security context  

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
        msg->endpoint.flags &= ~OSCORE;

        OC_ERR("***OSCORE protected request lacks kid param, unsecured 4.02***");
        oscore_send_error(oscore_pkt, BAD_OPTION_4_02, &msg->endpoint);
        goto oscore_recv_error;
      }
    }

    // set access token index that was used to decrypt 
    msg->endpoint.auth_at_index = oscore_ctx->auth_at_index;

    // TODO check is always h'' why used 
    oc_endpoint_set_oscore_id(&msg->endpoint, (char*)oscore_ctx->sender_id, oscore_ctx->sender_id_len);

    // use recipient key for decryption
    decryption_key = oscore_ctx->recipient_key;

    /* If received Partial IV in message */
    if (oscore_pkt->piv_len > 0)
    {
      /* If message is request */
      if (oscore_pkt->code >= OC_GET && oscore_pkt->code <= OC_FETCH)
      {
        uint64_t piv = 0;
        oscore_read_piv(oscore_pkt->piv, oscore_pkt->piv_len, &piv);
        /* Compose AAD using received piv and context->recipient_id */
        oc_oscore_compose_AAD(oscore_ctx->recipient_id, oscore_ctx->recipient_id_len, oscore_pkt->piv, oscore_pkt->piv_len, AAD,
                              &AAD_len);
        OC_DBG_OSCORE("---> composed AAD using received Partial IV and Recipient ID");
        OC_LOGbytes_OSCORE(AAD, AAD_len);
      }

      OC_DBG_OSCORE("---> got Partial IV from incoming message : ");
      OC_LOGbytes_OSCORE(oscore_pkt->piv, oscore_pkt->piv_len);

      /* Copy received piv into oc_message_t->endpoint for requests */
      if (oscore_pkt->code >= OC_GET && oscore_pkt->code <= OC_DELETE)
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

    /* If message is response */
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
    uint8_t* output = (uint8_t*)malloc(oscore_pkt->payload_len);

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
        response + encrypted problem = 8.04 step 5  = stop processing
        request + encrypted problem = 8.2 step 6 = unsecured 4.00
      */

      if (oscore_pkt->code >= OC_GET && oscore_pkt->code <= OC_FETCH)
      {
        // request
        msg->endpoint.flags &= ~OSCORE;
        OC_ERR("***error decrypting/verifying response : (%d), unsecured 4.00***", ret);
        oscore_send_error(oscore_pkt, BAD_REQUEST_4_00, &msg->endpoint);
      }

      // response
      goto oscore_recv_error;
    }

    OC_DBG_OSCORE("### successfully decrypted OSCORE payload ###");

    /* Adjust payload length to size after decryption (i.e. exclude the tag)
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
        OC_ERR("***error parsing inner message, secured 4.02***");
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

    /* Copy type, version, mid, token, observe fields from OSCORE packet to
     * CoAP Packet */
    coap_pkt->transport_type = oscore_pkt->transport_type;
    coap_pkt->version = oscore_pkt->version;
    coap_pkt->type = oscore_pkt->type;
    coap_pkt->mid = oscore_pkt->mid;
    memcpy(coap_pkt->token, oscore_pkt->token, oscore_pkt->token_len);
    coap_pkt->token_len = oscore_pkt->token_len;
    coap_pkt->observe = oscore_pkt->observe;

    /* Also copy kid, kid_ctx and ssn, for replay protection */

    msg->endpoint.kid_len = oscore_pkt->kid_len;
    memcpy(msg->endpoint.kid, oscore_pkt->kid, oscore_pkt->kid_len);
    msg->endpoint.kid_ctx_len = oscore_pkt->kid_ctx_len;
    memcpy(msg->endpoint.kid_ctx, oscore_pkt->kid_ctx, oscore_pkt->kid_ctx_len);

    OC_DBG_OSCORE("### serializing CoAP message ###");
    /* Serialize fully decrypted CoAP packet to message->data buffer */
    msg->length = coap_oscore_serialize_message((void*)coap_pkt, msg->data, true, true, true);

    OC_DBG_OSCORE("### setting OSCORE and OSCORE_DECRYPTED ###");

    /* set the oscore encryption and decryption flags*/
    msg->endpoint.flags |= OSCORE_DECRYPTED + OSCORE + IPV6;

    PRINTipaddr_flags(msg->endpoint);

    OC_DBG_OSCORE("### serialized decrypted CoAP message to dispatch to the CoAP layer ###");

    if (s_mode_re_request)
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

  @param msg the message, pushed to queue OUTBOUND_OSCORE_EVENT since the OSCORE flag and MULTICAST flag is set

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

  const uint32_t group_address = msg->endpoint.group_address;
  if (group_address == 0)
  {
    OC_ERR("group_address id == 0");
    goto oscore_group_send_error;
  }

  oc_oscore_context_t* oscore_ctx = oc_oscore_find_context_by_group_address(group_address);
  PRINT("oc_oscore_send_multicast_message : group_address = %u", group_address);
  if (oscore_ctx)
  {
    OC_DBG_OSCORE("#################################");
    OC_DBG_OSCORE("found group OSCORE context for GA %u", group_address);

    // use sender key for encryption
    uint8_t* key = oscore_ctx->sender_key;

    OC_DBG_OSCORE("### parse CoAP message ###");
    /* Parse CoAP message */
    coap_packet_t coap_pkt[1];
    coap_status_t code = coap_udp_parse_message(coap_pkt, msg->data, msg->length);

    if (code != COAP_NO_ERROR)
    {
      OC_ERR("***error parsing CoAP packet***");
      goto oscore_group_send_error;
    }

    OC_DBG_OSCORE("### parsed CoAP message ###");

    uint8_t piv[OSCORE_PIV_LEN], piv_len = 0,
            kid[OSCORE_SENDER_ID_LEN], kid_len = 0,
            nonce[OSCORE_AEAD_NONCE_LEN],
            AAD[OSCORE_AAD_MAX_LEN], AAD_len = 0;

    OC_DBG_OSCORE("### protecting multicast request ###");

    // use SSN as Partial IV
    oscore_store_piv(oscore_ctx->ssn, piv, &piv_len);
    // OC_DBG_OSCORE("---using SSN as Partial IV: %lu", oscore_ctx->ssn);
    OC_LOGbytes_OSCORE(piv, piv_len);

    /* Increment SSN */
    increment_ssn_in_context(oscore_ctx);

    /* Use context-sender_id as kid */
    memcpy(kid, oscore_ctx->sender_id, oscore_ctx->sender_id_len);
    kid_len = oscore_ctx->sender_id_len;

    /* Compute nonce using partial IV and context->sender_id */
    oc_oscore_AEAD_nonce(oscore_ctx->sender_id, oscore_ctx->sender_id_len, piv, piv_len, oscore_ctx->common_iv, nonce,
                         OSCORE_AEAD_NONCE_LEN);

    OC_DBG_OSCORE("---computed AEAD nonce using Partial IV (SSN) and Sender ID");
    OC_LOGbytes_OSCORE(nonce, OSCORE_AEAD_NONCE_LEN);

    /* Compose AAD using partial IV and context->sender_id */
    oc_oscore_compose_AAD(oscore_ctx->sender_id, oscore_ctx->sender_id_len, piv, piv_len, AAD, &AAD_len);
    OC_DBG_OSCORE("---composed AAD using Partial IV (SSN) and Sender ID");
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

    OC_DBG_OSCORE("### serializing OSCORE plaintext ###");
    /* Serialize OSCORE plain text at offset COAP_MAX_HEADER_SIZE
       (code, inner options, payload)
    */
    size_t plaintext_size = oscore_serialize_plaintext(coap_pkt, msg->data + COAP_MAX_HEADER_SIZE);

    OC_DBG_OSCORE("### serialized OSCORE plaintext: %zd bytes ###", plaintext_size);

    /* Set the OSCORE packet payload to point to location of the serialized
       inner message.
    */
    coap_pkt->payload = msg->data + COAP_MAX_HEADER_SIZE;
    coap_pkt->payload_len = plaintext_size;

    /* Encrypt OSCORE plaintext */
    OC_DBG_OSCORE("### encrypting OSCORE plaintext ###");

    int ret = oc_oscore_encrypt(coap_pkt->payload, coap_pkt->payload_len, OSCORE_AEAD_TAG_LEN, key, OSCORE_KEY_LEN, nonce,
                                OSCORE_AEAD_NONCE_LEN, AAD, AAD_len, coap_pkt->payload);

    if (ret != 0)
    {
      OC_ERR("***error encrypting OSCORE plaintext***");
      goto oscore_group_send_error;
    }

    OC_DBG_OSCORE("### successfully encrypted OSCORE plaintext ###");

    /* Adjust payload length to include the size of the authentication tag */
    coap_pkt->payload_len += OSCORE_AEAD_TAG_LEN;

    /* Set the Outer code for the OSCORE packet (POST/FETCH:2.04/2.05) */
    coap_pkt->code = OC_POST;

    /* Wireshark fix - include the context ID on the wire as well */
    /* otherwise cannot decode OSCORE messages that use implicit ID contexts */
    uint8_t idctx[16], idctx_len;
    memcpy(idctx, oscore_ctx->id_context, oscore_ctx->id_context_len);
    idctx_len = oscore_ctx->id_context_len;

    /* Set the OSCORE option */
    coap_set_header_oscore(coap_pkt, piv, piv_len, kid, kid_len, idctx, idctx_len);

    /* Serialize OSCORE message to oc_message_t */
    OC_DBG_OSCORE("### serializing OSCORE message ###");
    msg->length = oscore_serialize_message(coap_pkt, msg->data);
    OC_DBG_OSCORE("### serialized OSCORE message ###");
  }
  else
  {
    OC_ERR("*** could not find group OSCORE context ***");
    goto oscore_group_send_error;
  }

  OC_DBG_OSCORE("#################################");
  /* Dispatch oc_message_t to the IP layer */
  OC_DBG_OSCORE("Outbound network event: forwarding to IP Connectivity layer");
  oc_send_discovery_request(msg);
  oc_message_unref(msg);
  return 0;

oscore_group_send_error:
  OC_ERR("received malformed CoAP packet from stack");
  oc_message_unref(msg);
  return -1;
}
#endif 

/**
  @brief

  @param msg the message, pushed to queue OUTBOUND_OSCORE_EVENT since the OSCORE flag is set and MULTICAST flag is NOT set 

  @note See details in description on top of this file

*/
static int oc_oscore_send_unicast_message(oc_message_t* msg)
{
  /* OSCORE layer sending path pseudocode
   * ------------------------------------
   * Search for OSCORE context by peer UUID
   * If found OSCORE context:
   *   Set context->sender_key as the encryption key
   *   Clone incoming oc_message_t (*msg) from CoAP layer
   *   Parse CoAP message
   *   If parse unsuccessful, return error
   *   If CoAP message is request:
   *     Search for client cb by request token
   *     If found client cb:
   *       Use context->SSN as partial IV
   *       Use context-sender_id as kid
   *       Copy partial IV into client cb
   *       Compute nonce using partial IV and context->sender_id
   *       Compute AAD using partial IV and context->sender_id
   *       Copy partial IV into incoming oc_message_t (*msg), if valid
   *     Else:
   *       Return error
   *   Else: (CoAP message is response)
   *     Use context->SSN as partial IV
   *     Compute nonce using partial IV and context->sender_id
   *     Compute AAD using request_piv and context->recipient_id
   *     Copy partial IV into incoming oc_message_t (*msg), if valid
   *    Make room for inner options and payload by moving CoAP payload to offset
   *    2 * COAP_MAX_HEADER_SIZE
   *    Store Observe option; if message is a notification, make Observe option
   *    value empty
   *    Serialize OSCORE plaintext at offset COAP_MAX_HEADER_SIZE
   *    Encrypt OSCORE plaintext at offset COAP_MAX_HEADER_SIZE
   *    Set OSCORE packet payload to location COAP_MAX_HEADER_SIZE
   *    Set OSCORE packet payload length to the plain text size + tag length (8)
   *    Set OSCORE option in OSCORE packet
   *    Reflect the Observe option (if present in the CoAP packet)
   *    Set the Outer code for the OSCORE packet (POST/FETCH:2.04/2.05)
   *    Serialize OSCORE message to oc_message_t
   * Dispatch oc_message_t to the (TLS or) network layer
   */

  // no multicast response to a (previous) secure multicast request message 
  // TODO cannot happen here, check to remove 
  if (msg->endpoint.flags & MULTICAST)
  {
    OC_DBG_OSCORE("### secure multicast requests do not elicit a response, discard ###");
    oc_message_unref(msg);
    return 0;
  }

  // most common case for unicast response to a (uc/mc) request, 
  // we just get the cached index from the original (multicast) request
  oc_auth_at_t* entry = oc_get_auth_at_entry(msg->endpoint.auth_at_index);

  oc_oscore_context_t* oscore_ctx = NULL;

  // if found, get the corresponding context for the access token,
  // will be found only in case of a response 
  if (entry)
  {
    // TODO do we need also ID Context here --> if we have same kid from several
    // sender (context) then we pick the first hit 
    OC_DBG_OSCORE("### Found auth at entry, getting context ###");
    oscore_ctx = oc_oscore_find_context_by_kid(NULL,
                                               (uint8_t*)oc_string(entry->osc_id),
                                               oc_byte_string_len(entry->osc_id));
  }

  // search for OSCORE context using addressing information usually on request 
  PRINT("SID ");
  oc_char_println_hex(msg->endpoint.oscore_id, (int)msg->endpoint.oscore_id_len);

  if (oscore_ctx == NULL)
  {
    // search the oscore id, e.g. the SID
    oscore_ctx = oc_oscore_find_context_by_oscore_id(msg->endpoint.oscore_id,
                                                     msg->endpoint.oscore_id_len);
  }

  // search for OSCORE context using addressing information
  if (oscore_ctx == NULL)
  {
    oscore_ctx = oc_oscore_find_context_by_group_address(msg->endpoint.group_address);
  }

  oc_message_t*  message = oc_internal_allocate_outgoing_message();
  if (message == NULL)
  {
    OC_ERR("***No memory to allocate outgoing message!***");
    goto oscore_send_error;
  }

  // clone incoming msg from CoAP layer
  message->length = msg->length;
  memcpy(message->data, msg->data, msg->length);
  memcpy(&message->endpoint, &msg->endpoint, sizeof(oc_endpoint_t));

  // 
  bool msg_valid = msg->ref_count > 1 ? true: false;
  oc_message_unref(msg);

  OC_DBG_OSCORE("### parse CoAP message ###");

  // local CoAP packet 
  coap_packet_t coap_pkt[1];
  coap_status_t code = 0;
#ifdef OC_TCP
  if (message->endpoint.flags & TCP)
  {
    code = coap_tcp_parse_message(coap_pkt, message->data, (uint32_t)message->length);
  }
  else
#endif 
  {
    // parse CoAP message
    code = coap_udp_parse_message(coap_pkt, message->data, message->length);
  }

  if (code != COAP_NO_ERROR)
  {
    OC_ERR("***error parsing CoAP packet***");
    goto oscore_send_error;
  }

  OC_DBG_OSCORE("### parsed CoAP message ###");

  // search for context using token/mid 
  if (oscore_ctx == NULL)
  {
    oscore_ctx = oc_oscore_find_context_by_token_mid(coap_pkt->token, coap_pkt->token_len,
                                                     coap_pkt->mid, NULL, NULL, false);
  }
  // we haven't found a context, so we free the message we just created
  if (oscore_ctx == NULL)
  {
    oc_message_unref(message);
    OC_ERR("No OSCORE context found. ERROR");
    goto oscore_send_error;
  }

  // is an (unicast) echo challenge for a mc s-mode request  
  bool s_mode_echo = false;


  // TODO check on mc/uc directly and then decide 

  // Allocate a temp S-Mode Echo Challenge Responder Context
  if (coap_pkt->code > OC_FETCH // Response
      && coap_pkt->echo_len > 0 // With Echo Challenge
      && entry->ga_len > 0 // For an S-Mode Message
      )
  {
    unsigned char rnd[10];

    mbedtls_ctr_drbg_context* ctr_drbg_context = oc_random_get_ctr_drbg_context();
    mbedtls_ctr_drbg_random(ctr_drbg_context, rnd, sizeof(rnd));

    uint64_t request_ssn = 0;

    oscore_read_piv(message->endpoint.request_piv, message->endpoint.request_piv_len, &request_ssn);

    oscore_ctx = oc_oscore_add_context(
        oc_string(entry->osc_id), oc_byte_string_len(entry->osc_id), // Sender Id is osc.id
      oc_string(entry->osc_id), oc_byte_string_len(entry->osc_id), // Recipient ID (gets used as request_kid for the AAD composition => use Request Sender ID)
        request_ssn, // one time use anyway
        oc_string(entry->osc_ms), oc_byte_string_len(entry->osc_ms),
        oc_string(entry->osc_salt), oc_byte_string_len(entry->osc_salt),
        (char*)rnd, 10, 
        message->endpoint.auth_at_index, false);


    s_mode_echo = true;
  }

  // TODO cannot be NULL , to be removed 
  if (oscore_ctx)
  {
    OC_DBG_OSCORE("#################################");
    OC_DBG_OSCORE("found OSCORE context with sender id=%s", oscore_ctx->sender_id);

    /* Use sender key for encryption */
    uint8_t* key = oscore_ctx->sender_key;

    // names from RFC OSCORE option 
    uint8_t piv[OSCORE_PIV_LEN], piv_len = 0, kid[OSCORE_SENDER_ID_LEN], kid_len = 0, kid_context[OSCORE_ID_CONTEXT_LEN], kid_context_len = 0,
                                 nonce[OSCORE_AEAD_NONCE_LEN], AAD[OSCORE_AAD_MAX_LEN], AAD_len = 0;

    /* If CoAP message is request */
    if ((coap_pkt->code >= OC_GET && coap_pkt->code <= OC_DELETE)
#ifdef OC_TCP
        || coap_pkt->code == PING_7_02 || coap_pkt->code == ABORT_7_05 || coap_pkt->code == CSM_7_01
#endif
    )
    {

      OC_DBG_OSCORE("### protecting outgoing request ###");
      
      /* Request */
      /* Use context->SSN as Partial IV */
      oscore_store_piv(oscore_ctx->ssn, piv, &piv_len);
      // oscore_store_piv(0, piv, &piv_len);
      // OC_DBG_OSCORE("---using SSN as Partial IV: %lu", oscore_ctx->ssn);
      OC_LOGbytes_OSCORE(piv, piv_len);

      // increment SSN for the original request, retransmissions below max retransmits (4) use the same SSN
      // if present (token) the request/response cycle is issued 
      coap_transaction_t* transaction = coap_get_transaction_by_token(coap_pkt->token, coap_pkt->token_len);

      // TODO Coding style  AND assumption that payload is teh same 
      if (transaction && transaction->retrans_counter == 0)
        increment_ssn_in_context(oscore_ctx); // fresh request, CON ???
      else if (!transaction)
        increment_ssn_in_context(oscore_ctx); // fresh request, NON ??? 

#ifdef OC_CLIENT
      if (coap_pkt->code >= OC_GET && coap_pkt->code <= OC_DELETE)
      {
        /* Find client cb for the request */
        oc_client_cb_t* cb = oc_ri_find_client_cb_by_token(coap_pkt->token, coap_pkt->token_len);

        if (cb)
        {
          /* Copy partial IV into client cb */
          memcpy(cb->piv, piv, piv_len);
          cb->piv_len = piv_len;
        }
      }
#endif /* OC_CLIENT */

      /* Use context-sender_id as kid */
      memcpy(kid, oscore_ctx->sender_id, oscore_ctx->sender_id_len);
      kid_len = oscore_ctx->sender_id_len;

      /* use id_context as context_id */
      memcpy(kid_context, oscore_ctx->id_context, oscore_ctx->id_context_len);
      kid_context_len = oscore_ctx->id_context_len;

      /* Compute nonce using partial IV and context->sender_id */
      oc_oscore_AEAD_nonce(oscore_ctx->sender_id, oscore_ctx->sender_id_len, piv, piv_len, oscore_ctx->common_iv, nonce,
                           OSCORE_AEAD_NONCE_LEN);

      OC_DBG_OSCORE("---computed AEAD nonce using Partial IV (SSN) and Sender ID");
      OC_LOGbytes_OSCORE(nonce, OSCORE_AEAD_NONCE_LEN);
      OC_DBG_OSCORE("---");

      /* Compose AAD using partial IV and context->sender_id */
      oc_oscore_compose_AAD(oscore_ctx->sender_id, oscore_ctx->sender_id_len, piv, piv_len, AAD, &AAD_len);
      OC_DBG_OSCORE("---composed AAD using Partial IV (SSN) and Sender ID");
      OC_LOGbytes_OSCORE(AAD, AAD_len);
      OC_DBG_OSCORE("---");

      /* Copy partial IV into incoming oc_message_t (*msg), if valid */
      if (msg_valid)
      {
        memcpy(msg->endpoint.request_piv, piv, piv_len);
        msg->endpoint.request_piv_len = piv_len;
      }
    }
    else
    {
      /* We are dealing with a response */

      /* Request was not protected by OSCORE */
      if (message->endpoint.request_piv_len == 0)
      {
        OC_DBG("request was not protected by OSCORE");
        goto oscore_send_dispatch;
      }

      OC_DBG("### protecting outgoing response ###");

      /* Use context->SSN as partial IV */
      oscore_store_piv(oscore_ctx->ssn, piv, &piv_len);
      OC_DBG_OSCORE("---using SSN as Partial IV : ");
      OC_LOGbytes_OSCORE(piv, piv_len);
      OC_DBG_OSCORE("---");
      /* Increment SSN for the original request, retransmissions use the same
       * SSN */
      coap_transaction_t* transaction = coap_get_transaction_by_token(coap_pkt->token, coap_pkt->token_len);

      bool is_initial_transmission = transaction && transaction->retrans_counter == 0;
      bool is_empty_ack = coap_pkt->type == COAP_TYPE_ACK && coap_pkt->code == 0;
      bool is_separate_response = coap_pkt->type == COAP_TYPE_CON;
      bool is_not_transaction = !transaction;

      // 
      if (is_initial_transmission || is_empty_ack || is_separate_response || is_not_transaction)
        increment_ssn_in_context(oscore_ctx);

      // RFC 8613, 8.3 or KNX IoT 3.6.5 (# 2870)
      // TODO an empty ack (4 byte) is not and cannot be encrypted
      // sep response goes to lower path (check implications)
      // echo challenge needs to be in upper path (see 3.6.5 point api ) otherwise it may cause nonce reuse
      if (is_empty_ack || is_separate_response || s_mode_echo )
      {
        // RFC 8613, 8.3, point 3 lower * 
        // ack and separate responses use a new PIV 
        OC_DBG_OSCORE("---piv : ");
        OC_LOGbytes_OSCORE(piv, piv_len);
        oc_oscore_AEAD_nonce(oscore_ctx->sender_id, oscore_ctx->sender_id_len, 
                             piv, piv_len,
                             oscore_ctx->common_iv, 
                             nonce, OSCORE_AEAD_NONCE_LEN);
        /* Compute nonce using partial IV and sender ID of the sender ( =
         * receiver ID )*/
        OC_DBG_OSCORE("---computed AEAD nonce using new Partial IV (SSN) and Response Sender ID : ");
        OC_LOGbytes_OSCORE(nonce, OSCORE_AEAD_NONCE_LEN);
      }
      else
      {
        // RFC 8613, 8.3, point 3 upper *
        // other responses reuse the PIV and Sender ID from the request to compute the same AEAD nonce as used for the request
        OC_DBG_OSCORE("---request_piv : ");
        OC_LOGbytes_OSCORE(message->endpoint.request_piv, message->endpoint.request_piv_len);
        oc_oscore_AEAD_nonce(oscore_ctx->recipient_id, oscore_ctx->recipient_id_len, 
                             message->endpoint.request_piv, message->endpoint.request_piv_len,
                             oscore_ctx->common_iv, 
                             nonce, OSCORE_AEAD_NONCE_LEN);
        /* Compute nonce using partial IV and sender ID of the sender ( =
         * receiver ID )*/
        OC_DBG_OSCORE("---computed AEAD nonce using Partial IV (SSN) and Request Sender ID : ");
        OC_LOGbytes_OSCORE(nonce, OSCORE_AEAD_NONCE_LEN);
      }

      // AAD always uses the request PIV

      // This block, alongside endpoint.rx_msg_is_response, is needed for
      // encrypting the final ack of a separate response sequence. For now, we
      // have decided to send that ack in plaintext, so this is all commented
      // out
      /*
      if (is_empty_ack && msg->endpoint.rx_msg_is_response)
      {
        // only fires when the client is sending the acknowledgement
        // for the confirmable, separate response of the server
        OC_DBG_OSCORE("--- is empty ACK, using details from the request");
        // We have already sent an ACK for the original request, so the
      transaction
        // is gone and the other side cannot find the keying material. so for
      this
        // message only we include it again.
        memcpy(kid, oscore_ctx->sender_id, oscore_ctx->sender_id_len);
        kid_len = oscore_ctx->sender_id_len;
        memcpy(ctx_id, oscore_ctx->id_context, oscore_ctx->id_context_len);
        ctx_id_len = oscore_ctx->id_context_len;

        OC_DBG_OSCORE("--- Including KID:");
        OC_LOGbytes_OSCORE(kid, kid_len);

        oc_oscore_compose_AAD(oscore_ctx->sender_id, oscore_ctx->sender_id_len,
                            message->endpoint.request_piv,
      message->endpoint.request_piv_len, AAD, &AAD_len);
      }
      else
      */
      {
        // Request sender ID = Req Recipient ID + REQUEST PIV -> https://www.rfc-editor.org/rfc/rfc8613#section-5.4
        oc_oscore_compose_AAD(oscore_ctx->recipient_id, oscore_ctx->recipient_id_len, message->endpoint.request_piv,
                              message->endpoint.request_piv_len, AAD, &AAD_len);
      }
      OC_DBG_OSCORE("---composed AAD using request piv and Recipient ID : ");
      OC_LOGbytes_OSCORE(AAD, AAD_len);

      /* Copy partial IV into incoming oc_message_t (*msg), if valid and if
       * message is request */
      // TODO check if unreachable code because of upper IF 
      if (msg_valid && coap_pkt->code >= OC_GET && coap_pkt->code <= OC_DELETE)
      {
        memcpy(msg->endpoint.request_piv, piv, piv_len);
        msg->endpoint.request_piv_len = piv_len;
        OC_DBG_OSCORE("--- Caching PIV for later use...");
        OC_LOGbytes_OSCORE(msg->endpoint.request_piv, msg->endpoint.request_piv_len);
      }
    }

    // store the inner CoAP code
    uint8_t inner_code = coap_pkt->code;

    /* Move CoAP payload to offset 2*COAP_MAX_HEADER_SIZE to accommodate for
       Outer+Inner CoAP options in the OSCORE packet.
    */
    if (coap_pkt->payload_len > 0)
    {
      memmove(message->data + 2 * COAP_MAX_HEADER_SIZE, coap_pkt->payload, coap_pkt->payload_len);

      /* Store the new payload location in the CoAP packet */
      coap_pkt->payload = message->data + 2 * COAP_MAX_HEADER_SIZE;
    }

    /* Store the observe option. Retain the inner observe option value
     * for observe registrations and cancellations. Use an empty value for
     * notifications.
     */
    int32_t observe_option = coap_pkt->observe;
    if (coap_pkt->observe > 1)
    {
      coap_pkt->observe = 0;
      OC_DBG("---response is a notification; making inner Observe option empty");
    }

    OC_DBG("### serializing OSCORE plaintext ###");
    /* Serialize OSCORE plaintext at offset COAP_MAX_HEADER_SIZE
       (code, inner options, payload)
    */
    size_t plaintext_size = oscore_serialize_plaintext(coap_pkt, message->data + COAP_MAX_HEADER_SIZE);

    OC_DBG_OSCORE("### serialized OSCORE plaintext: %zd bytes ###", plaintext_size);

    /* Set the OSCORE packet payload to point to location of the serialized
       inner message.
    */
    coap_pkt->payload = message->data + COAP_MAX_HEADER_SIZE;
    coap_pkt->payload_len = plaintext_size;

    /* Encrypt OSCORE plaintext */
    OC_DBG_OSCORE("### encrypting OSCORE plaintext ###");

    int ret = oc_oscore_encrypt(coap_pkt->payload, coap_pkt->payload_len, OSCORE_AEAD_TAG_LEN, key, OSCORE_KEY_LEN, nonce,
                                OSCORE_AEAD_NONCE_LEN, AAD, AAD_len, coap_pkt->payload);

    if (ret != 0)
    {
      OC_ERR("***error encrypting OSCORE plaintext***");
      goto oscore_send_error;
    }

    OC_DBG_OSCORE("### successfully encrypted OSCORE plaintext ###");

    /* Adjust payload length to include the size of the authentication tag */
    coap_pkt->payload_len += OSCORE_AEAD_TAG_LEN;

    /* Set the Outer code for the OSCORE packet (POST/FETCH:2.04/2.05) */
    coap_pkt->code = oscore_get_outer_code(coap_pkt);

    /* If outer code is 2.05, then set the Max-Age option */
    if (coap_pkt->code == CONTENT_2_05)
    {
      coap_set_header_max_age(coap_pkt, 0);
    }

    bool is_request = coap_pkt->code >= OC_GET && coap_pkt->code <= OC_FETCH;
    bool is_empty_ack = coap_pkt->type == COAP_TYPE_ACK && inner_code == 0;
    bool is_separate_response = coap_pkt->type == COAP_TYPE_CON;

    /* Set the OSCORE option */
    // TODO update according to changes above 
    if (is_request || is_empty_ack || is_separate_response || s_mode_echo)
    {
      if (s_mode_echo)
      {
        /* For an S-Mode Echo Challenge the Response Sender ID must be included  */
        memcpy(kid, oscore_ctx->sender_id, oscore_ctx->sender_id_len);
        kid_len = oscore_ctx->sender_id_len;

        /* include the 10 byte id context in the header */
        memcpy(kid_context, oscore_ctx->id_context, oscore_ctx->id_context_len);
        kid_context_len = oscore_ctx->id_context_len;

        oc_oscore_free_context(oscore_ctx);
      }

      // TODO here also the  uc echo response must end up 
      coap_set_header_oscore(coap_pkt, piv, piv_len, kid, kid_len, kid_context, kid_context_len);
    }
    else
    {
      // other responses use the (cached) piv of the matching request, stored in the ep/client_cb
      coap_set_header_oscore(coap_pkt, NULL, 0, kid, kid_len, kid_context, kid_context_len);
    }

    /* Reflect the Observe option (if present in the CoAP packet) */
    coap_pkt->observe = observe_option;

    /* Serialize OSCORE message to oc_message_t */
    OC_DBG_OSCORE("### serializing OSCORE message ###");
    message->length = oscore_serialize_message(coap_pkt, message->data);
    OC_DBG_OSCORE("### serialized OSCORE message ###");
    // oc_free_string(&proxy_uri);
  }
oscore_send_dispatch:
  OC_DBG_OSCORE("#################################");

  // from here on any message is encrypted ...
  message->endpoint.flags |= OSCORE_ENCRYPTED;

#ifdef OC_CLIENT
  /* Dispatch oc_message_t to the message buffer layer */
  if (oc_process_post(&message_buffer_handler, oc_events[OUTBOUND_NETWORK_EVENT_ENCRYPTED], message) == OC_PROCESS_ERR_FULL)
  {
    OC_ERR(" could not send message");
  }
  return 0;
#endif

#ifdef OC_CLIENT
#ifdef OC_SECURITY
  OC_DBG_OSCORE("Outbound network event: forwarding to TLS");
  if (!oc_tls_connected(&message->endpoint))
  {
    OC_DBG_OSCORE("Posting INIT_TLS_CONN_EVENT");
    oc_process_post(&oc_tls_handler, oc_events[INIT_TLS_CONN_EVENT], message);
  }
  else
#endif 
#endif 
  {
#ifdef OC_SECURITY
    OC_DBG_OSCORE("Posting RI_TO_TLS_EVENT");
    oc_process_post(&oc_tls_handler, oc_events[RI_TO_TLS_EVENT], message);
#endif /* OC_SECURITY */
  }
  return 0;

oscore_send_error:
  OC_ERR("received malformed CoAP packet from stack");
  oc_message_unref(message);
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
    else if (ev == oc_events[OUTBOUND_OSCORE_EVENT])
    {
      OC_DBG_OSCORE("Outgoing OSCORE event: protecting unicast message");
      oc_oscore_send_unicast_message(data);
    }
#ifdef OC_CLIENT
    else if (ev == oc_events[OUTBOUND_GROUP_OSCORE_EVENT])
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