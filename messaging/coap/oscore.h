/*
// Copyright (c) 2020 Intel Corporation
// Copyright (c) 2022 Cascoda Ltd
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

#ifndef OSCORE_H
#define OSCORE_H
#include "constants.h"
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

  uint8_t oscore_get_outer_code(void* packet);

  bool oscore_is_oscore_message(oc_message_t* msg);

  int coap_parse_inner_oscore_option(void* packet, uint8_t* current_option, size_t option_length);

  size_t coap_serialize_oscore_option(unsigned int* current_number, void* packet, uint8_t* buffer);

  int coap_get_header_oscore(void* packet, uint8_t** piv, uint8_t* piv_len, uint8_t** kid, uint8_t* kid_len,
                             uint8_t** kid_ctx, uint8_t* kid_ctx_len);

  int coap_set_header_oscore(void* packet, uint8_t* piv, uint8_t piv_len, uint8_t* kid, uint8_t kid_len, uint8_t* kid_ctx,
                             uint8_t kid_ctx_len);

  coap_status_t oscore_parse_inner_message(uint8_t* data, size_t data_len, void* packet);

  coap_status_t oscore_parse_outer_message(oc_message_t* msg, void* packet);

  // a message is serialized by adding outer options AND the OSCORE option
  size_t oscore_serialize_message(void* packet, uint8_t* buffer);

  // a message is serialized by adding inner code, Class E options, payload (if present), see https://datatracker.ietf.org/doc/html/rfc8613#section-5.3
  size_t oscore_serialize_plaintext(void* packet, uint8_t* buffer);

#ifdef __cplusplus
}
#endif

#endif
