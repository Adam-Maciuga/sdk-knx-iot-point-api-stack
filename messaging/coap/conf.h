/*
 * Copyright (c) 2016 Intel Corporation
 * Copyright (c) 2026 KNX Association
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/*
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
 *
 * This file is part of the Contiki operating system.
 */

#ifndef CONF_H
#define CONF_H

#include "constants.h"

#include "oc_config.h"

#ifdef __cplusplus
extern "C" {
#endif


// the number of concurrent messages that can be stored for retransmission in the transaction layer
#ifndef COAP_MAX_OPEN_TRANSACTIONS
#define COAP_MAX_OPEN_TRANSACTIONS (OC_MAX_NUM_CONCURRENT_REQUESTS)
#endif 

#define FEAR (64)
/* 
   Conservative size limit, as not all options have to be set at the same time 
   (recheck when Proxy-Uri, Uri-Host option is used)
   -> hdr (4), token (8), fear, options 
      Observe (5)
      Uri-Port (3)
      OSCORE (32) 
      Max-Age (5)
      Payload marker (1)
      ETag (8)
      BLOCKWISE (34 -> blk 4 + 4 + size 6 + 6 + q-blk 5 + 5)  
*/
#ifndef COAP_MAX_HEADER_SIZE 

  #ifdef OC_BLOCK_WISE
    #define COAP_MAX_HEADER_SIZE (4 + COAP_TOKEN_LEN + FEAR + 5 + 3 + 32 + 5 + 1 + COAP_ETAG_LEN + 34)
  #else 
    #define COAP_MAX_HEADER_SIZE (4 + COAP_TOKEN_LEN + FEAR + 5 + 3 + 32 + 5 + 1 + COAP_ETAG_LEN)
  #endif 

#endif 

// number of observer slots (each takes about xxx bytes)
#ifndef COAP_MAX_OBSERVERS
  #define COAP_MAX_OBSERVERS  (OC_MAX_APP_RESOURCES + OC_MAX_NUM_CONCURRENT_REQUESTS)
#endif 

// interval in notifies in which NON notifies are changed to CON notifies to check client
#define COAP_OBSERVE_REFRESH_INTERVAL 5

#ifdef __cplusplus
}
#endif

#endif 
