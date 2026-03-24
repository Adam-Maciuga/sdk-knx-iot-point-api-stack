/*
// Copyright (c) 2018 Intel Corporation
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

// Note:
// OC Flags:
// OC_DYNAMIC_ALLOCATION
// OC_PDU_SIZE			// TODO what is it? often used, looks like message size?
// OC_PKI			// TODO => KNX_TLS_PKI, OC_PKI is only used with TLS.

#ifndef MBEDTLS_CONFIG_H
#define MBEDTLS_CONFIG_H

#include <oc_config.h>
#include "port/oc_assert.h"
#include "port/oc_connectivity.h"

#ifdef OC_DYNAMIC_ALLOCATION			// TODO looks like this needs to be undefined for Zephyr
#include <stdlib.h>
#define MBEDTLS_PLATFORM_STD_CALLOC calloc	// TODO FIXME not in Zephyr config
#define MBEDTLS_PLATFORM_STD_FREE free		// TODO FIXME not in Zephyr config
#else /* OC_DYNAMIC_ALLOCATION */
#define MBEDTLS_MEMORY_BUFFER_ALLOC_C		// TODO FIXME hmm, this is set on Zephyr
#endif /* !OC_DYNAMIC_ALLOCATION */

#ifdef OC_PKI
#if defined(_WIN64) || defined(_WIN32) || defined(__APPLE__) ||                \
  defined(__linux) || defined(__ANDROID__)
// Note:
// The hardware platform can provide a correct date and time. 
// This is used to verify the valididty period of X.509 certificates.
//
// Kconfig: CONFIG_MBEDTLS_HAVE_TIME_DATE
// CONFIG_MBEDTLS_HAVE_TIME_DATE also defines MBEDTLS_HAVE_TIME
#define MBEDTLS_HAVE_TIME
#define MBEDTLS_HAVE_TIME_DATE
//#define MBEDTLS_PLATFORM_MS_TIME_ALT		// TODO shouldn't this be set?
#endif
#endif

#define MBEDTLS_PLATFORM_STD_EXIT oc_exit	// TODO FIXME not in Zephyr config
#define MBEDTLS_PLATFORM_STD_SNPRINTF snprintf	// TODO, no need to be set, this is the default (also for Zephyr)

/* System support */
// Note:
// Included in Zephyr config-mbedtls.h
#define MBEDTLS_PLATFORM_C			// TODO
#define MBEDTLS_PLATFORM_MEMORY			// TODO
#define MBEDTLS_PLATFORM_EXIT_ALT		// TODO

// Kconfig: CONFIG_MBEDTLS_PLATFORM_NO_STD_FUNCTIONS
#define MBEDTLS_PLATFORM_NO_STD_FUNCTIONS

// Kconfig: CONFIG_MBEDTLS_PLATFORM_SNPRINTF_ALT
#define MBEDTLS_PLATFORM_SNPRINTF_ALT

/* mbedTLS feature support */

/* Supported TLS versions */
// Kconfig: CONFIG_MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_PROTO_TLS1_2

#if defined(CONFIG_MBEDTLS_SSL_PROTO_TLS1_2) || \
        defined(CONFIG_MBEDTLS_SSL_PROTO_TLS1_3)	// TODO FIXME CONFIG_

/* Common modules required for TLS 1.2 and 1.3 */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_SRV_C
#define MBEDTLS_SSL_CLI_C

/* This is not supported by Mbed TLS in TLS 1.3 mode
 * (see modules/crypto/mbedtls/docs/architecture/tls13-support.md).
 */
#if !defined(CONFIG_MBEDTLS_SSL_PROTO_TLS1_3)
#define MBEDTLS_SSL_MAX_FRAGMENT_LENGTH
#endif

#endif /* CONFIG_MBEDTLS_SSL_PROTO_TLS1_2 || CONFIG_MBEDTLS_SSL_PROTO_TLS1_3 */

// Kconfig: CONFIG_MBEDTLS_SSL_PROTO_DTLS
#define MBEDTLS_SSL_PROTO_DTLS
#define MBEDTLS_SSL_DTLS_ANTI_REPLAY
#define MBEDTLS_SSL_DTLS_HELLO_VERIFY
#define MBEDTLS_SSL_COOKIE_C

/* Supported key exchange methods */
// Kconfig: CONFIG_MBEDTLS_KEY_EXCHANGE_PSK_ENABLED 
// Kconfig: CONFIG_MBEDTLS_KEY_EXCHANGE_ECDHE_PSK_ENABLED
// Kconfig: CONFIG_MBEDTLS_PSK_MAX_LEN = 16	// TODO is 16 enough for all Zephyr modules?
// Note:
// Save some RAM by adjusting to your exact needs.
// 16: 128-bits keys are generally enough.
// Kconfig: CONFIG_MBEDTLS_KEY_EXCHANGE_RSA_ENABLED	// TODO shouldn't this be set?
// Kconfig: CONFIG_MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_PSK_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_PSK_ENABLED
#define MBEDTLS_PSK_MAX_LEN 16
//#define MBEDTLS_KEY_EXCHANGE_RSA_ENABLED	// TODO shouldn't this be set?
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED

// Kconfig: CONFIG_MBEDTLS_HKDF_C
#define MBEDTLS_HKDF_C

/* Supported cipher modes */
// Kconfig: CONFIG_MBEDTLS_CIPHER_AES_ENABLED
// Kconfig: CONFIG_MBEDTLS_AES_ROM_TABLES
// Note:
// Save RAM at the expense of ROM
#define MBEDTLS_AES_C
#define MBEDTLS_AES_ROM_TABLES

// Kconfig: CONFIG_MBEDTLS_CIPHER_CCM_ENABLED
// Note:
// oscore
#define MBEDTLS_CCM_C

// Kconfig: CONFIG_MBEDTLS_CIPHER_MODE_CBC_ENABLED 
#define MBEDTLS_CIPHER_MODE_CBC

/* Supported elliptic curve libraries */
// Kconfig: CONFIG_MBEDTLS_ECDH_C
// Kconfig: CONFIG_MBEDTLS_ECP_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECP_C

/* Supported elliptic curves */
// Kconfig: CONFIG_MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED

/* Supported hash algorithms */
// Kconfig: CONFIG_MBEDTLS_SHA256
// TODO Zephyr checkout CONFIG_MBEDTLS_HARDWARE_SHA which enables MBEDTLS_SHA256_ALT (harware with sofware fallback)
#define MBEDTLS_SHA256_C	// TODO replace with MBEDTLS_256_ALT? see: Zephyr hal/espressif/components/mbedtls/port/include/mbedtls/esp_config.h 
// Kconfig: CONFIG_MBEDTLS_SHA512
//#define MBEDTLS_SHA512_C	// TODO Should we favour SHA512 over SHA256 in general? e.g. ESP32-C6 SHA256 HW, SHA512 SW

/* mbedTLS modules */
// Kconfig: CONFIG_MBEDTLS_CTR_DRBG_C
#define MBEDTLS_CTR_DRBG_C

// Kconfig: CONFIG_MBEDTLS_DEBUG
#define MBEDTLS_ERROR_C
#define MBEDTLS_DEBUG_C
//#define MBEDTLS_SSL_DEBUG_ALL			// TODO shouldn't this be set?
#define MBEDTLS_SSL_ALL_ALERT_MESSAGES

// Kconfig: CONFIG_MBEDTLS_ENTROPY_C
// Kconfig: CONFIG_MBEDTLS_CIPHER
// Kconfig: CONFIG_MBEDTLS_MD_C
// Kconfig: CONFIG_MBEDTLS_RSA_C
// Kconfig: CONFIG_MBEDTLS_PKCS1_V15
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_MD_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15

/* KNX-IoT */
#ifdef OC_PKI

// Kconfig: CONFIG_MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED

// Kconfig: CONFIG_MBEDTLS_CIPHER_CCM_ENABLED 
// Kconfig: CONFIG_MBEDTLS_CIPHER_GCM_ENABLED 
#define MBEDTLS_CCM_C
#define MBEDTLS_GCM_C

// Kconfig: CONFIG_MBEDTLS_ECDSA_C 
// Kconfig: CONFIG_MBEDTLS_ECP_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_C

// Kconfig: CONFIG_MBEDTLS_PK_WRITE_C		// TODO Zephyr: hmm, this is in Automatic depnendencies
#define MBEDTLS_PK_WRITE_C // extract public key

// Kconfig: CONFIG_MBEDTLS_X509_CSR_WRITE_C	// TODO Zephyr: hmm, this is in Automatic depnendencies
// Note:
// CONFIG_MBEDTLS_X509_CSR_WRITE_C also defines MBEDTLS_X509_CREATE_C
// Kconfig: CONFIG_MBEDTLS_X509_CSR_PARSE_C
// Kconfig: CONFIG_MBEDTLS_X509_CRT_WRITE_C	// TODO Zephyr: hmm, this is in Automatic depnendencies
#define MBEDTLS_X509_CSR_WRITE_C
#define MBEDTLS_X509_CREATE_C
#define MBEDTLS_X509_CSR_PARSE_C
#define MBEDTLS_X509_CRT_WRITE_C

#endif

/* Automatic dependencies */
#if defined(MBEDTLS_KEY_EXCHANGE_RSA_PSK_ENABLED) || \
        defined(MBEDTLS_KEY_EXCHANGE_RSA_ENABLED) || \
        defined(MBEDTLS_KEY_EXCHANGE_DHE_RSA_ENABLED) || \
        defined(MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED) || \
        defined(MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED) || \
        defined(MBEDTLS_KEY_EXCHANGE_ECDH_ECDSA_ENABLED) || \
        defined(MBEDTLS_KEY_EXCHANGE_ECDH_RSA_ENABLED) || \
        defined(MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL_ENABLED)
#define MBEDTLS_X509_CRT_PARSE_C
#endif

#if defined(CONFIG_MBEDTLS_PEM_CERTIFICATE_FORMAT) && \
    defined(MBEDTLS_X509_CRT_PARSE_C)
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_PEM_WRITE_C
#define MBEDTLS_BASE64_C
#endif

#if defined(MBEDTLS_X509_CRT_PARSE_C)
#define MBEDTLS_X509_USE_C
#endif

#if defined(MBEDTLS_DHM_C) || \
    defined(MBEDTLS_ECP_C) || \
    defined(MBEDTLS_RSA_C) || \
    defined(MBEDTLS_X509_USE_C) || \
    defined(MBEDTLS_GENPRIME)
#define MBEDTLS_BIGNUM_C
#endif

#if defined(MBEDTLS_RSA_C) || \
    defined(MBEDTLS_X509_USE_C)
#define MBEDTLS_OID_C
#endif

#if defined(MBEDTLS_X509_USE_C)
#define MBEDTLS_PK_PARSE_C
#endif

#if defined(MBEDTLS_PK_PARSE_C) || defined(MBEDTLS_PK_WRITE_C)
#define MBEDTLS_PK_C
#endif

#if defined(CONFIG_MBEDTLS_ASN1_PARSE_C) || defined(MBEDTLS_X509_USE_C)
#define MBEDTLS_ASN1_PARSE_C
#endif

#if defined(MBEDTLS_ECDSA_C) || defined(MBEDTLS_RSA_C) || defined(MBEDTLS_PK_WRITE_C)
#define MBEDTLS_ASN1_WRITE_C
#endif

// Kconfig: CONFIG_MBEDTLS_PKCS5_C	// TODO hmm, this is NOT an automatic dependency, is it?
#define MBEDTLS_PKCS5_C

// Kconfig: CONFIG_MBEDTLS_SSL_EXTENDED_MASTER_SECRET	// TODO hmm, this is NOT an automatic dependency, is it?
#define MBEDTLS_SSL_EXTENDED_MASTER_SECRET

/*
 * You should adjust this to the exact number of sources you're using: default
 * is the "platform_entropy_poll" source, but you may want to add other ones
 * Minimum is 2 for the entropy test suite.
 */
#define MBEDTLS_ENTROPY_MAX_SOURCES 2		// TODO FIXME if defined(CONFIG_MBEDTLS_OPENTHREAD_OPTIMIZATIONS_ENABLED) this is set to 1.

#ifndef OC_DYNAMIC_ALLOCATION
// Kconfig: CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN	// TODO is OC_PDU_SIZE enough for all Zephyr modules?
#define MBEDTLS_SSL_MAX_CONTENT_LEN (OC_PDU_SIZE)
#endif /* !OC_DYNAMIC_ALLOCATION */

/* TODO FIXME */
//#define MBEDTLS_X509_CHECK_EXTENDED_KEY_USAGE		// Remove the MBEDTLS_X509_CHECK_KEY_USAGE and MBEDTLS_X509_CHECK_EXTENDED_KEY_USAGE config.h options and let the code behave as if they were always enabled. Fixes #4405.
#define MBEDTLS_X509_EXPANDED_SUBJECT_ALT_NAME_SUPPORT	// TODO FIXME not in MBEDTLS library
#define MBEDTLS_SSL_BUFFER_MIN 512			// TODO FIXME not in MBEDTLS library
#define MBEDTLS_KEY_EXCHANGE_ECDH_ANON_ENABLED		// TODO FIXME not in MBEDTLS library
#define MBEDTLS_SSL_SRV_RESPECT_CLIENT_PREFERENCE	// TODO FIXME Replace MBEDTLS_SSL_SRV_RESPECT_CLIENT_PREFERENCE by a runtime configuration function mbedtls_ssl_conf_preference_order(). Fixes #4398.

// #include "mbedtls/check_config.h"

#endif /* MBEDTLS_CONFIG_H */
