/*
 * Copyright (c) 2020 Intel Corporation
 * Copyright (c) 2024-2026 KNX Association
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "oscore.h"
#include "coap.h"
#include "coap_signal.h"
#include "oc_ri.h"

void oscore_send_error(void* packet, uint8_t code, oc_endpoint_t* endpoint, bool secured)
{
  coap_packet_t* coap_pkt = (coap_packet_t*)packet;
  uint16_t mid;
  coap_message_type_t type;

  if (!secured)
  {
    UNSET_BIT(endpoint->flags, OSCORE);
    UNSET_OPTION(coap_pkt, COAP_OPTION_OSCORE);
  }

  if (coap_pkt->type == COAP_TYPE_CON)
  {
    // in case of confirmable send ack with mid from request as ACK
    type = COAP_TYPE_ACK;
    mid = coap_pkt->mid;
  }
  else
  {
    // send any message other than ack with OWN (next) mid as NON
    type = COAP_TYPE_NON;
    mid = coap_get_next_mid();
  }

  // note, this message is not the same as a coap packet from above
  oc_message_t* message = oc_internal_allocate_outgoing_message();
  if (message)
  {
    
    // one static CoAP packet
    coap_packet_t outgoing_coap_msg[1];

    // init and set all in coap msg to zero
    coap_udp_init_message(outgoing_coap_msg, type, code, mid);
    
    // copy original endpoint to local message
    memcpy(&message->endpoint, endpoint, sizeof(*endpoint));

    // copy token
    if (coap_pkt->token_len > 0)
    {
      coap_set_token(outgoing_coap_msg, coap_pkt->token, coap_pkt->token_len);
    }

    // no max age = no caching 
    coap_set_header_max_age(outgoing_coap_msg, 0);

    // copies coap msg to message
    message->length = coap_serialize_message(outgoing_coap_msg, message->data);
    if (message->length > 0)
    {
      coap_send_message(message);
      OC_DBG("send OSCORE error %s message in CoAP format with code (%u)", secured ? "'secured'" : "'plain'", code);
    }
  }
}

int oscore_store_piv_to_ssn(uint8_t* piv, uint8_t piv_len, uint64_t* ssn) 
{
  /*
   PIV is a big-endian encoded integer; accumulate each byte MSB-first into the numeric value without any endianness detection or byte-swapping.
   
   Platform: little-endian (x86, ARM Cortex-M)
   Platform: big-endian (PowerPC, SPARC)

   Example: piv = { 0xAA, 0xBB, 0xCC }, piv_len = 3
     i=0: ssn = (0x00000000 << 8) | 0xAA = 0x000000AA
     i=1: ssn = (0x000000AA << 8) | 0xBB = 0x0000AABB
     i=2: ssn = (0x0000AABB << 8) | 0xCC = 0x00AABBCC

     The new code uses only arithmetic operators (<< and |) which always operate on the numeric value of *ssn, never on its memory representation.
     The C standard guarantees that << and | on unsigned integers produce the same value on every platform.
  */

  *ssn = 0;

  for (uint8_t i = 0; i < piv_len; i++)
  {
    *ssn = (*ssn << 8) | piv[i];
  }

  return 0;
}


int oscore_store_ssn_to_piv(uint8_t* piv, uint8_t* piv_len, uint64_t ssn) 
{
  memset(piv, 0, OSCORE_PIV_LEN);

  if (ssn == 0)
  {
    // SSN of zero encodes as a single zero byte (RFC 8613 minimum PIV length = 1)
    piv[0] = 0;
    *piv_len = 1;
    return 0;
  }

  /*
    Emit SSN as big-endian byte sequence (RFC 8613 minimum-length encoding).
    Strips leading zero bytes using right-shifts on the numeric value.
    Platform-independent: no endianness detection required.
   
    Example: ssn = 0x00AABBCC, OSCORE_PIV_LEN = 5
      shift=32: byte = 0x00 -> skip (leading zero)
      shift=24: byte = 0x00 -> skip (leading zero)
      shift=16: byte = 0xAA -> write, piv_len = 1
      shift= 8: byte = 0xBB -> write, piv_len = 2
      shift= 0: byte = 0xCC -> write, piv_len = 3
      Result: piv = { 0xAA, 0xBB, 0xCC }, piv_len = 3
   */

  *piv_len = 0;
  for (int shift = (OSCORE_PIV_LEN - 1) * 8; shift >= 0; shift -= 8)
  {
    const uint8_t byte = (uint8_t)(ssn >> shift);
    if (*piv_len > 0 || byte != 0)  // skip leading zeros
      piv[(*piv_len)++] = byte;     // write byte and increment length (from here piv_len is not 0 anymore, so no more skipping)
  }

  return 0;
}

/**
 * @brief get for a OSCORE request/ response the OUTER CoAp code for the CoAp message
 *
 * @note a request uses always POST a response always 2.04 Changed,
 *       except on a present observe option (FETCH, 2.05 OK)
 *
 * @param packet the CoAp packet to be scanned
 *
 */
uint8_t oscore_get_outer_code(void* packet) 
{
  coap_packet_t const* coap_pkt = (coap_packet_t*) packet;

  const bool observe = IS_OPTION(coap_pkt, COAP_OPTION_OBSERVE);

  if (coap_pkt->code >= OC_GET && coap_pkt->code <= OC_FETCH
  #ifdef OC_TCP
          || (coap_pkt->code == PING_7_02 || coap_pkt->code == ABORT_7_05 || coap_pkt->code == CSM_7_01)
  #endif 
  ) 
  { 
    // requests
    return observe ? OC_FETCH : OC_POST;
  }
	
  // responses
  return observe ? (uint8_t)oc_status_code(OC_STATUS_OK) : (uint8_t)oc_status_code(OC_STATUS_CHANGED);
}

int coap_set_header_oscore(void* packet, uint8_t* piv, uint8_t piv_len, uint8_t* kid, uint8_t kid_len, uint8_t* kid_ctx, uint8_t kid_ctx_len) 
{
  coap_packet_t* const coap_pkt = (coap_packet_t *)packet;

  // sets 'nnn' from 000|h|k|nnn flags (also kid length of 0!)
  coap_pkt->oscore_flags = piv_len & 0x07;

  // piv
  if (piv_len > 0) 
  {
    memcpy(coap_pkt->piv, piv, piv_len);
    coap_pkt->piv_len = piv_len;
  }

  // kid 
  if (kid_len > 0) 
  {
    memcpy(coap_pkt->kid, kid, kid_len);
    coap_pkt->kid_len = kid_len;

    // sets 'k' from 000|h|k|nnn flags
    coap_pkt->oscore_flags |= 1 << OSCORE_FLAGS_BIT_KID_POSITION;
  }

  // kid_context
  if (kid_ctx_len > 0) 
  {
    memcpy(coap_pkt->kid_ctx, kid_ctx, kid_ctx_len);
    coap_pkt->kid_ctx_len = kid_ctx_len;

    // sets 'h' from 000|h|k|nnn flags 
    coap_pkt->oscore_flags |= 1 << OSCORE_FLAGS_BIT_KID_CTX_POSITION;
  }

  SET_OPTION(coap_pkt, COAP_OPTION_OSCORE);
  return 1;
}

int coap_parse_inner_oscore_option(void* packet, uint8_t* current_option, size_t option_length) 
{
  coap_packet_t* const coap_pkt = (coap_packet_t*) packet;

  /* 
    OSCORE Option structure From RFC 8613:

    0 1 2 3 4 5 6 7 <------------- n bytes -------------->
    +-+-+-+-+-+-+-+-+--------------------------------------
    |0 0 0|h|k|  n  |       Partial IV (if any) ...
    +-+-+-+-+-+-+-+-+--------------------------------------

    <- 1 byte -> <----- s bytes ------>
    +------------+----------------------+------------------+
    | s (if any) | kid context (if any) | kid (if any) ... |
    +------------+----------------------+------------------+

  */
  OC_DBG("OSCORE option");
  if (option_length == 0) 
  {
    OC_DBG("\t... empty value, no need to parse");
    return 0;
  }

  // OSCORE flags (see above) , one byte 
  coap_pkt->oscore_flags = *current_option;
  current_option++;
  option_length--;

  OC_DBG("\t flags (000|h|k|nnn): %02x", coap_pkt->oscore_flags);

  // Partial IV length (n bytes)
  coap_pkt->piv_len = coap_pkt->oscore_flags & OSCORE_FLAGS_PIVLEN_BITMASK;

  if (coap_pkt->piv_len > 0) 
  {
    // copy PIV
    memcpy(coap_pkt->piv, current_option, coap_pkt->piv_len);
    current_option += coap_pkt->piv_len;
    option_length -= coap_pkt->piv_len;

    OC_DBG("\t Partial IV\t: ");
    OC_LOGbytes(coap_pkt->piv, coap_pkt->piv_len);
  }

  // kid context (if any), check if 'h' flag bit is set
  if (coap_pkt->oscore_flags & OSCORE_FLAGS_KIDCTX_BITMASK)
  {
    // (s) 1 byte
    coap_pkt->kid_ctx_len = *current_option;
    current_option++;
    option_length--;

    // copy kid context (s bytes)
    memcpy(coap_pkt->kid_ctx, current_option, coap_pkt->kid_ctx_len);
    current_option += coap_pkt->kid_ctx_len;
    option_length -= coap_pkt->kid_ctx_len;

    OC_DBG("\t kid_context\t: ");
    OC_LOGbytes(coap_pkt->kid_ctx, coap_pkt->kid_ctx_len);
  }

  // kid (if any), check if 'k' flag bit is set
  if (coap_pkt->oscore_flags & OSCORE_FLAGS_KID_BITMASK) 
  {
    // copy kid (remaining bytes in option)
    coap_pkt->kid_len = (uint8_t) option_length;
    memcpy(coap_pkt->kid, current_option, option_length);

    OC_DBG("\t kid\t\t: ");
    OC_LOGbytes(coap_pkt->kid, coap_pkt->kid_len);
  }

  return 0;
}

size_t coap_serialize_oscore_option(unsigned int* current_number, void* packet, uint8_t* buffer) 
{
  const coap_packet_t* const coap_pkt = (coap_packet_t*)packet;

  // calculate OSCORE option value length (piv, kid_context + kid)
  size_t option_length = coap_pkt->piv_len + coap_pkt->kid_len + coap_pkt->kid_ctx_len;

  if (coap_pkt->kid_ctx_len > 0) { 
    // context is present so increase option length (s = 1 byte, https://www.rfc-editor.org/rfc/rfc8613.html#section-6.1)
    ++option_length;
  }

  if (coap_pkt->oscore_flags > 0) { 
    // flags are present so increase option length (flags = 1 byte, see above)
    ++option_length;
  }

  // serialize OSCORE option header
  size_t header_length = coap_set_option_header(
          COAP_OPTION_OSCORE - *current_number, option_length, buffer);

  if (buffer) {
    buffer += header_length;

    OC_DBG_OSCORE("OSCORE option");
    OC_DBG_OSCORE("\t oscore flags (000|h|k|nnn) : %02x", coap_pkt->oscore_flags);
    if (coap_pkt->oscore_flags != 0) 
    {
      // serialize OSCORE option flags
      *buffer = coap_pkt->oscore_flags;
      ++buffer;

      // serialize Partial IV
      if (coap_pkt->piv_len > 0) 
      {
        memcpy(buffer, coap_pkt->piv, coap_pkt->piv_len);
        buffer += coap_pkt->piv_len;

        OC_DBG_OSCORE("\t Partial IV\t: ");
        OC_LOGbytes_OSCORE(coap_pkt->piv, coap_pkt->piv_len);
      }

      // serialize kid context
      if (coap_pkt->kid_ctx_len > 0) 
      {
        // kid context length
        *buffer = coap_pkt->kid_ctx_len;
        ++buffer;

        memcpy(buffer, coap_pkt->kid_ctx, coap_pkt->kid_ctx_len);
        buffer += coap_pkt->kid_ctx_len;

        OC_DBG_OSCORE("\t kid_context\t: ");
        OC_LOGbytes_OSCORE(coap_pkt->kid_ctx, coap_pkt->kid_ctx_len);
      }

      // remaining bytes, if any, represent the kid
      if (coap_pkt->kid_len > 0) {
        memcpy(buffer, coap_pkt->kid, coap_pkt->kid_len);
        buffer += coap_pkt->kid_len;

        OC_DBG_OSCORE("\t kid\t\t: ");
        OC_LOGbytes_OSCORE(coap_pkt->kid, coap_pkt->kid_len);
      }
    }
  }

  *current_number = COAP_OPTION_OSCORE;
  return option_length + header_length;
}

size_t oscore_serialize_plaintext(void* packet, uint8_t* buffer) 
{
  return coap_oscore_serialize_message(packet, buffer, true, false, false);
}

size_t oscore_serialize_message(void* packet, uint8_t* buffer) 
{
  return coap_oscore_serialize_message(packet, buffer, false, true, true);
}

size_t coap_serialize_message(void* packet, uint8_t* buffer) 
{
  return coap_oscore_serialize_message(packet, buffer, true, true, false);
}



// checks message header to find the CoAP OSCORE option header
bool oscore_is_oscore_message(oc_message_t* msg)
{
  const uint8_t* current_option = NULL;

  // determine exact location of the CoAP options in the packet buffer
  #ifdef OC_TCP
  if (msg->endpoint.flags & TCP)
  {
    // Calculate CoAP_TCP header length
    size_t message_length = 0;
    uint8_t num_extended_length_bytes = 0;
    coap_tcp_parse_message_length(msg->data, &message_length, &num_extended_length_bytes);

    current_option = msg->data + COAP_TCP_DEFAULT_HEADER_LEN + num_extended_length_bytes;
  }
  else
  #endif
  {
    // header position for UDP 
    current_option = msg->data + COAP_HEADER_LEN;
  }

  // add token size and jump to the end 
  size_t token_len = (COAP_HEADER_TOKEN_LEN_MASK & msg->data[0]) >> COAP_HEADER_TOKEN_LEN_POSITION;
  current_option += token_len;

  // parse outer options, first option instance is defined as zero https://datatracker.ietf.org/doc/html/rfc7252#section-3.1
  unsigned int option_number = 0;

  while (current_option < msg->data + msg->length)
  {
    if ((current_option[0] & 0xF0) == 0xF0)
    {
      // payload marker 0xFF, currently only checking for 0xF* because rest is reserved
      break;
    }

    /* option = previous option number + delta (number is not used directly), examples:

     (13): option = 0, option += delta (13); option += option[next 1 byte] (7)
     -> option = 20
     Note: The delta 7 is 20 - 13, see RFC

     (14): option = 0, option += delta (14); option += option[next 2 byte] (431)
     -> option = 445
     Note: The delta 431 is 700 - 269, see RFC
    */

    // first option fields
    unsigned int option_delta = current_option[0] >> 4; // 0..14
    size_t option_length = current_option[0] & 0x0F; // 0..14

    // skip the current option field as such
    current_option++;

    if (option_delta == 13)
    {
      // extended options, add 8-bit number from next option byte
      option_delta += current_option[0];

      // jump to next byte
      current_option++;
    }
    else if (option_delta == 14)
    {
      // extended options, 
      option_delta += 255; // add always 255 = 269 - 14

      // add 16 bit, hi byte
      option_delta += current_option[0] << 8;
      // jump to next byte
      current_option++;
      // add 16 bit, lo byte
      option_delta += current_option[0];
      // jump to next byte
      current_option++;
    }

    if (option_length == 13)
    {
      // see above
      option_length += current_option[0];
      current_option++;
    }
    else if (option_length == 14)
    {
      // see above
      option_length += 255;
      option_length += current_option[0] << 8;

      current_option++;
      option_length += current_option[0];

      current_option++;
    }

    option_number += option_delta;

    if (option_number == COAP_OPTION_OSCORE)
    {
      // found the OSCORE option, return success
      return true;
    }

    // jump to next byte after option 
    current_option += option_length;
  }

  // found NO OSCORE option, return NO success
  return false;
}