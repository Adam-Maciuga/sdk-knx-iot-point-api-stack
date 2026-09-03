/*
 * Copyright (c) 2026 Alexander Burker
 * Copyright (c) 2026 KNX Association
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file knx_mbedtls_config.h
 * @brief Mbed TLS 4.x TLS/X.509/platform configuration for the KNX-IoT Point API Stack.
 *
 * This file is the Mbed TLS configuration, set via MBEDTLS_CONFIG_FILE.
 * In Mbed TLS 4.x the configuration is split into two files:
 *   - This file:  TLS protocol, X.509, key exchange, and platform settings.
 *                 (MBEDTLS_* defines only; no crypto primitives.)
 *   - Companion:  security/knx_psa_crypto_config.h — PSA_WANT_* crypto
 *                 primitive selection (set via TF_PSA_CRYPTO_CONFIG_FILE).
 *
 * Note:
 * All crypto primitives (AES, SHA-256, HMAC, HKDF, ECC, BigNum, CTR-DRBG,
 * Entropy) are now configured in knx_psa_crypto_config.h via PSA_WANT_* macros
 * and are no longer defined here.
 *
 * Note:
 * The KNX-IoT Point API Stack does not support TLS or DTLS as a transport;
 * only UDP CoAP + OSCORE is used. The former KNX_TCP_TLS transport code and
 * its guards were removed. The remaining TLS/DTLS protocol options below are
 * only needed to build the Mbed TLS library itself and do not enable a TLS
 * transport in the stack.
 *
 * Note:
 * OC_PKI is currently not used and never defined by any build configuration.
 * X.509 options are kept for when PKI support is added.
 *
 * OC flags used in this file:
 * OC_PKI          // TODO => KNX_TLS_PKI, OC_PKI is only used with TLS.
 */

#ifndef MBEDTLS_CONFIG_H
#define MBEDTLS_CONFIG_H

/* Required in Mbed TLS 4.x to enable version-specific compatibility handling.
 * Value 0x04000000 corresponds to the Mbed TLS 4.0 config format.
 */
#define MBEDTLS_CONFIG_VERSION 0x04000000

#include <oc_config.h>

#ifdef OC_PKI
#if defined(_WIN64) || defined(_WIN32) || defined(__APPLE__) || \
  defined(__linux) || defined(__ANDROID__)
// Note:
// The hardware platform can provide a correct date and time.
// This is used to verify the validity period of X.509 certificates.
//
// Kconfig: CONFIG_MBEDTLS_HAVE_TIME_DATE
// CONFIG_MBEDTLS_HAVE_TIME_DATE also defines MBEDTLS_HAVE_TIME
#define MBEDTLS_HAVE_TIME
#define MBEDTLS_HAVE_TIME_DATE
//#define MBEDTLS_PLATFORM_MS_TIME_ALT // TODO shouldn't this be set?
#endif
#endif

/* ── Error / debug ───────────────────────────────────────────────────────────
 */

// Kconfig: CONFIG_MBEDTLS_DEBUG
#define MBEDTLS_ERROR_C
#define MBEDTLS_DEBUG_C
//#define MBEDTLS_SSL_DEBUG_ALL   // TODO shouldn't this be set?

/* ── TLS/DTLS protocol ───────────────────────────────────────────────────────
 *
 * Note: the stack has no TLS/DTLS transport. These options only configure the
 * Mbed TLS library build.
 */

/* Supported TLS versions */
// Kconfig: CONFIG_MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_PROTO_TLS1_2

#if defined(CONFIG_MBEDTLS_SSL_PROTO_TLS1_2) || \
        defined(CONFIG_MBEDTLS_SSL_PROTO_TLS1_3)  // TODO FIXME CONFIG_

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

#define MBEDTLS_SSL_ALL_ALERT_MESSAGES

// Kconfig: CONFIG_MBEDTLS_SSL_EXTENDED_MASTER_SECRET  // TODO hmm, this is NOT an automatic dependency, is it?
#define MBEDTLS_SSL_EXTENDED_MASTER_SECRET

/* ── X.509 and PKI (OC_PKI only) ────────────────────────────────────────────
 *
 * All X.509/PKI options are conditional on OC_PKI. None are used in any
 * current build configuration.
 */
#ifdef OC_PKI

// Kconfig: CONFIG_MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED

// Kconfig: CONFIG_MBEDTLS_CIPHER_GCM_ENABLED
#define MBEDTLS_GCM_C

// Kconfig: CONFIG_MBEDTLS_ECDSA_C
#define MBEDTLS_ECDSA_C

// Kconfig: CONFIG_MBEDTLS_PK_WRITE_C  // TODO Zephyr: hmm, this is in Automatic dependencies
#define MBEDTLS_PK_WRITE_C // extract public key

// Kconfig: CONFIG_MBEDTLS_X509_CSR_WRITE_C  // TODO Zephyr: hmm, this is in Automatic dependencies
// Note:
// CONFIG_MBEDTLS_X509_CSR_WRITE_C also defines MBEDTLS_X509_CREATE_C
// Kconfig: CONFIG_MBEDTLS_X509_CSR_PARSE_C
// Kconfig: CONFIG_MBEDTLS_X509_CRT_WRITE_C  // TODO Zephyr: hmm, this is in Automatic dependencies
#define MBEDTLS_X509_CSR_WRITE_C
#define MBEDTLS_X509_CREATE_C
#define MBEDTLS_X509_CSR_PARSE_C
#define MBEDTLS_X509_CRT_WRITE_C

#endif

/* Note: the former "Automatic dependencies" block was guarded by KNX_TCP_TLS
 * (the removed TLS/DTLS transport). It derived MBEDTLS_X509_, PK_ and ASN1_ macros
 * from the TLS key-exchange options and was never compiled, because enabling
 * it cascaded into MBEDTLS_OID_C, which was removed in Mbed TLS 4.0. It has
 * been dropped along with the transport. Mbed TLS 4.x resolves these
 * dependencies itself if X.509/PKI support is ever activated.
 *
 * Note: MBEDTLS_SSL_SRV_RESPECT_CLIENT_PREFERENCE was removed in Mbed TLS 4.x.
 * Use the runtime API instead: mbedtls_ssl_conf_preference_order().
 * Fixes #4398.
 */

// #include "mbedtls/check_config.h"

#endif /* MBEDTLS_CONFIG_H */
