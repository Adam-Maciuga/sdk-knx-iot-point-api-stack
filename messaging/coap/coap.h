/* 
 * Copyright (c) 2016, 2020 Intel Corporation
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
 */

#ifndef COAP_H
#define COAP_H

#include "conf.h"
#include "constants.h"
#include <stddef.h> // for size_t
#include <stdint.h>
#include "oscore.h"
#include "oc_buffer.h"
#include "oc_config.h"
#include "oc_ri.h"
#include "port/oc_connectivity.h"
#include "port/oc_log.h"
#include "port/oc_random.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MAX
#define MAX(n, m) (((n) < (m)) ? (m) : (n))
#endif

#ifndef MIN
#define MIN(n, m) (((n) < (m)) ? (n) : (m))
#endif

#ifndef ABS
#define ABS(n) (((n) < 0) ? -(n) : (n))
#endif

/* bitmap for set options */
enum {
  OPTION_MAP_SIZE = sizeof(uint8_t) * 8
};

/**
 * @brief set an option in the packet MUST-HAVE 'options' member (that is an array)
 *
 * @note the numerical option value sets the bit at the corresponding array position,
 *           example array with 32 byte (0...256 bit positions), 
 *           COAP_OPTION_ECHO (252) = [252/8] = [31] |= (1 << 4) = 'bbb1bbbb'	         
 */
#define SET_OPTION(packet, opt) ((packet)->options[(opt) / OPTION_MAP_SIZE] |= (1 << ((opt) % OPTION_MAP_SIZE)))

/**
 * @brief reset an option in the packet MUST-HAVE 'options' member (that is an array)
 *
 * @note the numerical option value sets the bit at the corresponding array position,
 *       example array with 32 byte (0...256 bit positions), 
 *       COAP_OPTION_ECHO (252) = [252/8] = [31] &= ~(1 << 4) = 'bbb0bbbb'
 */
#define UNSET_OPTION(packet, opt) ((packet)->options[(opt) / OPTION_MAP_SIZE] &= ~(1 << ((opt) % OPTION_MAP_SIZE)))

/**
 * @brief checks if an option is set in the packet MUST-HAVE 'options' member (that is an array)
 *
 * @note the numerical option value gets the bit at the corresponding array position,
 *       example array with 32 byte (0...256 bit positions), 
 *       COAP_OPTION_ECHO (252) = [252/8] = [31] & (1 << 4) -> 'bbb1bbbb' = true
 */
#define IS_OPTION(packet, opt) ((packet)->options[(opt) / OPTION_MAP_SIZE] & (1 << ((opt) % OPTION_MAP_SIZE)))

/** enum value for coap transport type  */
typedef enum 
{
  COAP_TRANSPORT_UDP, COAP_TRANSPORT_TCP
} coap_transport_type_t;

/** parsed message struct — fields ordered by descending alignment to minimize padding */
typedef struct coap_packet_t
{
  // --- 8-byte aligned: pointers and size_t ---
  uint8_t*    buffer;                     // host the serialized (to be sent out) CoAP packet byte stream with header/type/token/...
  uint8_t*    payload;                    // hosts the coap application payload byte stream (usually in CBOR or LINK format)
  const char* proxy_uri;
  const char* proxy_scheme;
  const char* uri_host;
  const char* location_path;
  const char* location_query;
  const char* uri_path;
  const char* uri_query;
  size_t proxy_uri_len;
  size_t proxy_scheme_len;
  size_t uri_host_len;
  size_t location_path_len;
  size_t location_query_len;
  size_t uri_path_len;
  size_t uri_query_len;
  size_t echo_len;

  // --- 4-byte aligned: uint32_t and enums ---
  coap_transport_type_t transport_type;   // UDP or TCP
  coap_message_type_t   type;             // CON, NON, ACK, ...
  uint32_t max_age;
  uint32_t observe;
  uint32_t block2_num;
  uint32_t block2_offset;
  uint32_t block1_num;
  uint32_t block1_offset;
  uint32_t size2;
  uint32_t size1;
  uint32_t payload_len;                   // application payload len, not including any CoAP header, token or (OSCORE) options

  // --- 2-byte aligned: uint16_t ---
  uint16_t mid;                           // transport level: client relates outbound CON message with inbound ACK,
                                          // receiver uses it to ignore an already received message
  uint16_t content_format;                // parse options once and store; allows setting options in random order
  uint16_t uri_port;
  uint16_t accept;
  uint16_t block2_size;
  uint16_t block1_size;

  // --- 1-byte: uint8_t scalars and arrays ---
  uint8_t version;                        // current version is '1'
  uint8_t code;                           // CoAP code such as GET = 1, CHANGED_2_04 = 68
  uint8_t token_len;
  uint8_t token[COAP_TOKEN_LEN];          // application level: a client matches a request with a response
  uint8_t options[COAP_OPTION_ECHO / OPTION_MAP_SIZE + 1]; // 32-byte bitmap, used to set/check options (see macros)
  uint8_t etag_len;
  uint8_t etag[COAP_ETAG_LEN];
  uint8_t if_match_len;
  uint8_t if_match[COAP_ETAG_LEN];
  uint8_t block2_more;
  uint8_t block1_more;
  uint8_t if_none_match;

  // --- OSCORE: all uint8_t ---
  uint8_t oscore_flags;                   // flags 000|h|k|nnn, as described in RFC 8613
  uint8_t piv[OSCORE_PIV_LEN];            // 'Partial IV' in OSCORE
  uint8_t piv_len;
  uint8_t kid_ctx[OSCORE_ID_CONTEXT_LEN]; // 'kid_context' in message, 'ID Context' in OSCORE, osc:contextid in OSCORE Profile
  uint8_t kid_ctx_len;
  uint8_t kid[OSCORE_SENDER_ID_LEN];      // 'kid' in message, 'Sender ID' in OSCORE, osc:id in OSCORE Profile
  uint8_t kid_len;
  uint8_t echo[COAP_ECHO_LEN];            // echo challenge random data (is part of inner options, RFC 9175)

  #ifdef OC_TCP
  // --- TCP: ordered by alignment ---
  const char* alt_addr;
  size_t      alt_addr_len;
  uint32_t    max_msg_size;
  uint32_t    hold_off;
  uint16_t    bad_csm_opt;
  uint8_t     blockwise_transfer;
  uint8_t     custody;
  #endif
} coap_packet_t;

/** option format serialization */
#define COAP_SERIALIZE_INT_OPTION(number, field, text)          \
  if (IS_OPTION(coap_pkt, number))                              \
  {                                                             \
    option_length += coap_serialize_int_option(number,          \
                                               current_number,  \
                                               option,          \
                                               coap_pkt->field);\
    if (option)                                                 \
    {                                                           \
      OC_DBG(text " [%u]", (unsigned int)coap_pkt->field);      \
      option = option_array + option_length;                    \
    }                                                           \
    current_number = number;                                    \
  }

#define COAP_SERIALIZE_BYTE_OPTION(number, field, text)                               \
  if (IS_OPTION(coap_pkt, number))                                                    \
  {                                                                                   \
    option_length += coap_serialize_array_option(number, current_number,              \
                                                 option, coap_pkt->field,             \
                                                 coap_pkt->field##_len, '\0');        \
    if (option)                                                                       \
    {                                                                                 \
      OC_DBG(text " %u [0x%02X%02X%02X%02X%02X%02X%02X%02X]",                         \
             (unsigned int)coap_pkt->field##_len, coap_pkt->field[0],                 \
             coap_pkt->field[1], coap_pkt->field[2], coap_pkt->field[3],              \
             coap_pkt->field[4], coap_pkt->field[5], coap_pkt->field[6],              \
             coap_pkt->field[7]); /* FIXME always prints 8 bytes */                   \
      option = option_array + option_length;                                          \
    }                                                                                 \
    current_number = number;                                                          \
  }

#define COAP_SERIALIZE_STRING_OPTION(number, field, splitter, text)                   \
  if (IS_OPTION(coap_pkt, number))                                                    \
  {                                                                                   \
    option_length += coap_serialize_array_option(number, current_number,              \
                                                 option,                              \
                                                 (uint8_t*)coap_pkt->field,           \
                                                 coap_pkt->field##_len,               \
                                                 splitter);                           \
    if (option)                                                                       \
    {                                                                                 \
      OC_DBG(text " [%.*s]", (int)coap_pkt->field##_len, coap_pkt->field);            \
      option = option_array + option_length;                                          \
    }                                                                                 \
    current_number = number;                                                          \
  }

#define COAP_SERIALIZE_BLOCK_OPTION(number, field, text)                              \
  if (IS_OPTION(coap_pkt, number))                                                    \
  {                                                                                   \
    uint32_t block = coap_pkt->field##_num << 4;                                      \
    if (coap_pkt->field##_more)                                                       \
    {                                                                                 \
      block |= 0x8;                                                                   \
    }                                                                                 \
    block |= 0xF & coap_log_2(coap_pkt->field##_size / 16);                           \
    option_length += coap_serialize_int_option(number, current_number, option, block);\
    if (option)                                                                       \
    {                                                                                 \
      OC_DBG(text " [%lu%s (%u B/blk)]", (unsigned long)coap_pkt->field##_num,        \
             coap_pkt->field##_more ? "+" : "", coap_pkt->field##_size);              \
      OC_DBG(text " encoded: 0x%lX", (unsigned long)block);                           \
      option = option_array + option_length;                                          \
    }                                                                                 \
    current_number = number;                                                          \
  }

/** stores error code */
extern coap_status_t coap_status_code;

void coap_init_connection(void);
uint16_t coap_get_next_mid(void);

  /**
 * @brief Init a UDP packet with major data (type, code, mid, fixed transport type UDP) and clears all the rest with '0' (options, payload, etc.)
 *
 * @param packet pointer to a coap packet struct, that will be filled with the given data
 * @param type CoAP message type, such as CON, NON, ACK, ...
 * @param code CoAP code such as GET = 1, CHANGED_2_04 = 68
 * @param mid message id (mid) for UDP
 *
 * @note sets as transport type UDP
 *
 */
void coap_udp_init_message(void* packet, coap_message_type_t type, uint8_t code, uint16_t mid);

// 
/**
 * @brief a message is serialized by adding plaintext (inner code, inner options, payload) and outer options,
 *        BUT not adding the outer OSCORE option
 *
 * @param packet the coap packet struct that will be serialized, it MUST have the inner code, inner options and payload already set
 * @param buffer the buffer where the serialized message will be stored, it MUST have enough space for the serialized message
 *
 * @note method is NOT used for empty ACK messages, see send 'coap_send_response_with_empty_ack'
 *	
 */
size_t coap_serialize_message(void* packet, uint8_t* buffer);

/**
* @brief serializes an OSCORE message (may also be an empty ack/rst message)
*  
* @param inner  = true: add RFC 8613 4.1.1 Class E (inner) options (encrypt and integrity protect) in plaintext of COSE object
* @param outer  = true: add RFC 8613 4.1.2 Class U (outer) options (unprotected) in option part of OSCORE message
*	@param oscore = true: add OSCORE option (flags, kid, kid_context, piv)
*
*/
size_t coap_oscore_serialize_message(void* packet, uint8_t* buffer, bool inner, bool outer, bool oscore);

void print_coap_service(uint8_t code, char* text);

/*
 @brief  forwards a CoAP message to lower (OSCORE) layers,
 *       defined as extra message wrapper that allows for TCP = enabled extra code -> one code place,
 *       after finally sending the (plain/encrypted) message the message will be "released" - if not tracked
 */
void coap_send_message(oc_message_t* message);

/*
* @brief parses *data pointer for coap options and (if found) assigns it to the coap packet 
*        (coap structure elements, such as token, etag, piv, ...), 
*        also calculates the application payload size (read the notes)
*
* @param accept_outer_options parses from *data Class U options, if found 
*          : TRUE = store them in coap *packet 
*          : FALSE = return 4.02
* @param accept_inner_options parses from *data Class E options, if found 
*          : TRUE = store them in coap *packet 
*          : FALSE = return 4.02
* @param accept_oscore_option parses from *data OSCORE options, if found 
*          : TRUE = store them in coap *packet 
*          : FALSE = return 4.02
*
* @note   used combinations: 
*         inner | outer | oscore | used by method    | comment
*         true  | true  | false  | parse udp         | *
*         true  | false | false  | parse inner       | **
*         false | true  | true   | parse outer       | ***
*
*   *** parse outer options of 'decrypted' message and if found assign to coap structure element such as the token,
*       any present - not allowed - outer option causes a 4.02
*
*   **  parse inner options of 'decrypted' message, 
*       any present - not allowed - inner option causes a 4.02
*
*   *   parse inner + outer options of 'decrypted' message, OSCORE option must be already removed (otherwise 4.02)    
*  
*   - outer  = RFC 8613 4.1.2 Class U options (unprotected), in option part of OSCORE message
*   - inner  = RFC 8613 4.1.1 Class E options (encrypt and integrity protect), in plaintext of COSE object  
*	  - oscore = OSCORE option data (flags, kid, kid_context, piv)
*  
*/
coap_status_t coap_oscore_parse_options(void* packet, uint8_t* data,
                                        uint32_t data_len, uint8_t* current_options, bool accept_inner_options,
                                        bool accept_outer_options, bool accept_oscore_option);

/**
 *
 * @brief parses *data and copy from it coap header/token/mid/options
 *        to *packet
 *
 * @note  does not copy OSCORE option security content
 *
 */
coap_status_t coap_parse_udp_message(void* packet, uint8_t* data, size_t data_len);

int coap_get_query_variable(void* packet, const char* name, const char** output);
int coap_get_post_variable(void* packet, const char* name, const char** output);

int coap_set_status_code(void* packet, unsigned int code);

int coap_set_token(void* packet, const uint8_t* token, size_t token_len);

int coap_get_header_content_format(void* packet, oc_content_format_t* format);
int coap_set_header_content_format(void* packet, oc_content_format_t format);

int coap_get_header_accept(void* packet, unsigned int* accept);
int coap_set_header_accept(void* packet, unsigned int accept);

int coap_get_header_max_age(void* packet, uint32_t* age);
int coap_set_header_max_age(void* packet, uint32_t age);

int coap_get_header_etag(void* packet, const uint8_t** etag);
int coap_set_header_etag(void* packet, const uint8_t* etag, size_t etag_len);

int coap_get_header_if_match(void* packet, const uint8_t** etag);
int coap_set_header_if_match(void* packet, const uint8_t* etag, size_t etag_len);

int coap_get_header_if_none_match(void* packet);
int coap_set_header_if_none_match(void* packet);

int coap_get_header_proxy_uri( void* packet, const char** uri); // In-place string might not be 0-terminated.
int coap_set_header_proxy_uri(void* packet, const char* uri);

int coap_get_header_proxy_scheme( void* packet, const char** scheme); // In-place string might not be 0-terminated.
int coap_set_header_proxy_scheme(void* packet, const char* scheme);

int coap_get_header_uri_host(void* packet, const char** host); // In-place string might not be 0-terminated.
int coap_set_header_uri_host(void* packet, const char* host);

size_t coap_get_header_uri_path(void* packet, const char** path); // In-place string might not be 0-terminated.
size_t coap_set_header_uri_path(void* packet, const char* path, size_t path_len);

size_t coap_get_header_uri_query(void* packet, const char** query); // In-place string might not be 0-terminated.
size_t coap_set_header_uri_query(void* packet, const char* query);

int coap_get_header_location_path(void* packet, const char** path); // In-place string might not be 0-terminated.
int coap_set_header_location_path(void* packet, const char* path); // Also splits optional query into Location-Query option.

int coap_get_header_location_query( void* packet, const char** query); // In-place string might not be 0-terminated.
size_t coap_set_header_location_query(void* packet, const char* query);

bool coap_get_header_observe(void* packet, uint32_t* observe);
void coap_set_header_observe(void* packet, uint32_t observe);

int coap_get_header_block2(void* packet, uint32_t* num, uint8_t* more, uint16_t* size, uint32_t* offset);
int coap_set_header_block2(void* packet, uint32_t num, uint8_t more, uint16_t size);

int coap_get_header_block1(void* packet, uint32_t* num, uint8_t* more, uint16_t* size, uint32_t* offset);
int coap_set_header_block1(void* packet, uint32_t num, uint8_t more, uint16_t size);

int coap_get_header_size2(void* packet, uint32_t* size);
int coap_set_header_size2(void* packet, uint32_t size);

int coap_get_header_size1(void* packet, uint32_t* size);
int coap_set_header_size1(void* packet, uint32_t size);

int coap_get_header_echo(void* packet, uint8_t* echo);
int coap_set_header_echo(void* packet, const uint8_t* echo, size_t len);

uint32_t coap_get_payload(void* packet, const uint8_t** payload);
uint32_t coap_set_payload(void* packet, const uint8_t* payload, size_t length);

size_t coap_set_option_header(unsigned int delta, size_t length, uint8_t* buffer);

/**
 * @brief
 *
 * @param incoming_message the message, pushed to queue INBOUND_RI_EVENT since 
 *        the previous oscore decryption was ok, or it was a plain message
 */
int coap_receive(oc_message_t* incoming_message);

#ifdef OC_TCP
// TCP
void coap_tcp_init_message(void* packet, uint8_t code);
size_t coap_tcp_get_packet_size(const uint8_t* data);
coap_status_t coap_tcp_parse_message(void* packet, uint8_t* data, uint32_t data_len);
void coap_tcp_parse_message_length(const uint8_t* data, size_t* message_length,
        uint8_t* num_extended_length_bytes);
#endif 

#ifdef __cplusplus
}
#endif

#endif 
