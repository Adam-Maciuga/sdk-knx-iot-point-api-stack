/*
 * Copyright (c) 2020 Intel Corporation
 * Copyright (c) 2022 Cascoda Ltd
 * Copyright (c) 2024-2026 KNX Association
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OSCORE_H
#define OSCORE_H
#include "port/oc_connectivity.h"

#ifdef __cplusplus
extern "C"
{
#endif

  // send an oscore error as CoAP with a CoAP return code (as plain CoAP error or as secured OSCORE error)
  void oscore_send_error(void* packet, uint8_t code, oc_endpoint_t* endpoint, bool secured);

  // read piv (src) and stores it to 64-bit ssn (dst), ssn is cleared first and result is converted by little/big endian 
  int oscore_store_piv_to_ssn(uint8_t* piv, uint8_t piv_len, uint64_t* ssn);

  // store 64-bit ssn (src) to piv (dst), piv is cleared first and result is converted by little/big endian
  int oscore_store_ssn_to_piv(uint8_t* piv, uint8_t* piv_len, uint64_t ssn);

  // echo ring (single unified FIFO: anchor slots + seen-responder slots); exposed for unit testing and the send / receive paths

  // extra definition to avoid including coap.h in this header file (circular dependency)   
  struct coap_packet_t;

  void oc_oscore_free_all_echo_records(void);

  // write an anchor slot (former whitelist append); takes own ref on msg (NULL-safe); caller must unref msg after
  void oc_oscore_echo_whitelist_append(uint64_t ssn, const uint8_t* kid, uint8_t kid_len,
                                       const uint8_t* token, uint8_t token_len,
                                       oc_message_t* msg);

  // look up the retained PLAINTEXT s-mode message by CoAP token; no ownership transfer; NULL if not found
  oc_message_t* oc_oscore_echo_get_retained_plaintext(const struct coap_packet_t* pkt);

  // accept or reject an inbound echo response using the unified ring (anchor + seen-responder scan)
  bool oc_oscore_echo_check_and_consume(const struct coap_packet_t* pkt);

  bool oscore_is_oscore_message(oc_message_t* msg);

  int coap_parse_inner_oscore_option(void* packet, uint8_t* current_option, size_t option_length);

  size_t coap_serialize_oscore_option(unsigned int* current_number, void* packet, uint8_t* buffer);

  int coap_set_header_oscore(void* packet, uint8_t* piv, uint8_t piv_len, uint8_t* kid, uint8_t kid_len, uint8_t* kid_ctx, uint8_t kid_ctx_len);

  // a message is serialized by adding outer options AND the OSCORE option (application payload and inner options don't touched)
  size_t oscore_serialize_message(void* packet, uint8_t* buffer);

  // a message is serialized by adding inner code, Class E options, application payload (if present), see https://datatracker.ietf.org/doc/html/rfc8613#section-5.3
  size_t oscore_serialize_plaintext(void* packet, uint8_t* buffer);

#ifdef __cplusplus
}
#endif

#endif
