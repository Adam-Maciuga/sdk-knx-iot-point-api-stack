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
 * KNX_TCP_TLS is not defined in any current build configuration (Linux,
 * Windows, Zephyr). The TLS/DTLS code in security/oc_tls.c is therefore
 * dormant. All TLS-specific options below are kept for completeness and
 * future activation.
 *
 * Note:
 * OC_PKI is currently not used. X.509 options are kept for when PKI
 * support is added.
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
 * Note: KNX_TCP_TLS is not currently defined anywhere in the stack.
 * These options are kept for when TLS/DTLS support is activated.
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

/* Supported key exchange methods */
// Kconfig: CONFIG_MBEDTLS_KEY_EXCHANGE_PSK_ENABLED
// Kconfig: CONFIG_MBEDTLS_KEY_EXCHANGE_ECDHE_PSK_ENABLED
// Kconfig: CONFIG_MBEDTLS_PSK_MAX_LEN = 16  // TODO is 16 enough for all Zephyr modules?
// Note:
// Save some RAM by adjusting to your exact needs.
// 16: 128-bits keys are generally enough.
// Kconfig: CONFIG_MBEDTLS_KEY_EXCHANGE_RSA_ENABLED  // TODO shouldn't this be set?
// Kconfig: CONFIG_MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_PSK_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_PSK_ENABLED
#define MBEDTLS_PSK_MAX_LEN 16
//#define MBEDTLS_KEY_EXCHANGE_RSA_ENABLED  // TODO shouldn't this be set?
#ifdef KNX_TCP_TLS
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#endif /* KNX_TCP_TLS */

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

/* ── Automatic dependencies ──────────────────────────────────────────────────
 *
 * The following macros are derived from the TLS/X.509 options above.
 * Mbed TLS 4.x resolves many of these automatically; they are kept here for
 * clarity and to ensure correctness in standalone (non-Zephyr) builds.
 *
 * Note: Guarded by KNX_TCP_TLS because MBEDTLS_OID_C was removed in Mbed TLS 4.0.
 * Without the guard, MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED cascades into
 * MBEDTLS_X509_CRT_PARSE_C → MBEDTLS_X509_USE_C → MBEDTLS_OID_C and causes a
 * compile error even when TLS is not activated.
 */
#ifdef KNX_TCP_TLS

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

//#define MBEDTLS_X509_CHECK_EXTENDED_KEY_USAGE   // Remove the MBEDTLS_X509_CHECK_KEY_USAGE and MBEDTLS_X509_CHECK_EXTENDED_KEY_USAGE config.h options and let the code behave as if they were always enabled. Fixes #4405.

/* Note: MBEDTLS_SSL_SRV_RESPECT_CLIENT_PREFERENCE was removed in Mbed TLS 4.x.
 * Use the runtime API instead: mbedtls_ssl_conf_preference_order().
 * Fixes #4398.
 */

#endif /* KNX_TCP_TLS */

// #include "mbedtls/check_config.h"

#endif /* MBEDTLS_CONFIG_H */
