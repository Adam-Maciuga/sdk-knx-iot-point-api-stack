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

#ifndef OSCORE_CONSTANTS_H
#define OSCORE_CONSTANTS_H

#define OSCORE_AEAD_NONCE_LEN (13)      // AEAD Nonce length for AES-CCM-16-64-128, 1=len(Sender ID), 7=padded Sender ID, 5=padded Partial IV
#define OSCORE_PIV_LEN (5)              // Partial IV max length, see AEAD Nonce 
#define OSCORE_SENDER_ID_LEN (7)        // Sender/Recipient ID maxlength, see AEAD Nonce 
#define OSCORE_OPTION_LEN (2 + 21 + 43) // Option header + Option length + Proxy-uri 
#define OSCORE_MASTER_SECRET_LEN (32)
#define OSCORE_ID_CONTEXT_LEN (16)      // Arbitrarily chosen upper-bound on OSCORE Context ID length
#define OSCORE_KEY_LEN (16)             // AES-CCM-16-64-128 uses 128-bit keys
#define OSCORE_COMMON_IV_LEN   \
        OSCORE_AEAD_NONCE_LEN           // Same as AEAD Nonce length
#define OSCORE_AEAD_TAG_LEN (8)         // Size in bytes of AES-CCM-16-64-128 authentication tag
#define OSCORE_REPLAY_WINDOW_SIZE (8)

#define OSCORE_STORAGE_PREFIX "ssn_"
#define OSCORE_STORAGE_PREFIX_LEN (4)
#define OSCORE_STORAGE_DIVIDER_LEN (1)
#define OSCORE_STORAGE_KEY_LEN       \
        (OSCORE_STORAGE_PREFIX_LEN + \
         OSCORE_SENDER_ID_LEN  *2 +  \
         OSCORE_STORAGE_DIVIDER_LEN +\
         OSCORE_ID_CONTEXT_LEN *2 + 1)  // Save ssn per (byte to hex = * 2) sender id and (byte to hex = * 2) id context as 'ssn_sender_context'   

#define OSCORE_INFO_MAX_LEN (128)
#define OSCORE_AAD_MAX_LEN (128)

/* Preventing SSN reuse, based on recommendations in RFC 8613, Appendix B.1. */
#define OSCORE_SSN_WRITE_FREQ_K (32)
#define OSCORE_SSN_PAD_F (OSCORE_SSN_WRITE_FREQ_K * 4)

/*
  KNX IoT maximum Sender Sequence Number: 2^32 - 1 = 4,294,967,295.
  KNX IoT uses a 32-bit SSN space (4-byte PIV), independent of OSCORE_PIV_LEN.
  The SSN is allowed to roll over modulo (OSCORE_SSN_MAX + 1), so this
  constant is used as a rollover mask rather than a hard stop.
*/
#define OSCORE_SSN_MAX ((1ULL << 32) - 1)

#define OSCORE_FLAGS_BIT_KID_POSITION 3
#define OSCORE_FLAGS_BIT_KID_CTX_POSITION 4
#define OSCORE_FLAGS_PIVLEN_BITMASK 0x07
#define OSCORE_FLAGS_KIDCTX_BITMASK 0x10
#define OSCORE_FLAGS_KID_BITMASK 0x08

#endif /* OSCORE_CONSTANTS_H */
