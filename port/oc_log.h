/* 
 * Copyright (c) 2016 Intel Corporation
 * Copyright (c) 2022 Cascoda Ltd
 * Copyright (c) 2024-2026 KNX Association
 *            
 * SPDX-License-Identifier: Apache-2.0
 *
 *  @brief platform abstraction of logging
 *  @file
 *
 *  Generic logging functions:
 *  - OC_INF
 *    prints information as Info level
 *  - OC_WRN
 *    prints information as Warning level
 *  - OC_ERR
 *    prints information as Error level
 *  - OC_DBG
 *    prints information as Debug level
 *  - OC_LOGipaddr
 *    prints the endpoint information to stdout
 *  - OC_LOGbytes
 *    prints the bytes to stdout
 *
 *  Compile flags:
 *  - OC_DEBUG
 *    enables output of logging functions
 *  - OC_NO_LOG_BYTES
 *    disables output of OC_LOGbytes logging function
 *    if OC_DEBUG is enabled.
 *  - KNX_LOG_TO_FILE
 *    logs the OC_INF, OC_WRN, OC_ERR, and OC_DBG statements to file
 */
#ifndef OC_LOG_H
#define OC_LOG_H

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "oc_clock_util.h"

// For clock function in debug output, maybe used for debugging in release builds, hence included globally.
#include "oc_clock.h"

#ifdef __ZEPHYR__
#include "log_zephyr.h"
#endif

#ifdef _WIN32
  #define __FILENAME__ (strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__)
#else
  #define __FILENAME__ (strrchr(__FILE__, '/')  ? strrchr(__FILE__, '/')  + 1 : __FILE__)
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SPRINTF(...) sprintf(__VA_ARGS__)
#define SNPRINTF(...) snprintf(__VA_ARGS__)

#define PRINTipaddr(endpoint)                   \
  do {                                                                         \
    const char *scheme = "coap";                                               \
    if ((endpoint).flags & SECURED)                                            \
      scheme = "coaps";                                                        \
    if ((endpoint).flags & TCP)                                                \
      scheme = "coap+tcp";                                                     \
    if ((endpoint).flags & TCP && (endpoint).flags & SECURED)                  \
      scheme = "coaps+tcp";                                                    \
    if ((endpoint).flags & IPV4) {                                             \
      PRINTF("%s://%d.%d.%d.%d:%d", scheme, ((endpoint).addr.ipv4.address)[0], \
            ((endpoint).addr.ipv4.address)[1],                                 \
            ((endpoint).addr.ipv4.address)[2],                                 \
            ((endpoint).addr.ipv4.address)[3], (endpoint).addr.ipv4.port);     \
    } else {                                                                   \
      PRINTF(                                                                  \
        "%s://[%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%"    \
        "02x:%"                                                                \
        "02x%"                                                                 \
        "02x]:%d",                                                             \
        scheme, ((endpoint).addr.ipv6.address)[0],                             \
        ((endpoint).addr.ipv6.address)[1], ((endpoint).addr.ipv6.address)[2],  \
        ((endpoint).addr.ipv6.address)[3], ((endpoint).addr.ipv6.address)[4],  \
        ((endpoint).addr.ipv6.address)[5], ((endpoint).addr.ipv6.address)[6],  \
        ((endpoint).addr.ipv6.address)[7], ((endpoint).addr.ipv6.address)[8],  \
        ((endpoint).addr.ipv6.address)[9], ((endpoint).addr.ipv6.address)[10], \
        ((endpoint).addr.ipv6.address)[11],                                    \
        ((endpoint).addr.ipv6.address)[12],                                    \
        ((endpoint).addr.ipv6.address)[13],                                    \
        ((endpoint).addr.ipv6.address)[14],                                    \
        ((endpoint).addr.ipv6.address)[15], (endpoint).addr.ipv6.port);        \
    }                                                                          \
  } while (0)

#define PRINTipaddr_flags(endpoint)             \
  do {                                                                         \
    if ((endpoint).flags & SECURED) {                                          \
      PRINTF("Secured ");                                                      \
    };                                                                         \
    if ((endpoint).flags & MULTICAST) {                                        \
      PRINTF("MULTICAST ");                                                    \
    };                                                                         \
    if ((endpoint).flags & TCP) {                                              \
      PRINTF("TCP ");                                                          \
    };                                                                         \
    if ((endpoint).flags & IPV4) {                                             \
      PRINTF("IPV4 ");                                                         \
    };                                                                         \
    if ((endpoint).flags & IPV6) {                                             \
      PRINTF("IPV6 ");                                                         \
    };                                                                         \
    if ((endpoint).flags & OSCORE) {                                           \
      PRINTF("OSCORE ");                                                       \
    };                                                                         \
    if ((endpoint).flags & ACCEPTED) {                                         \
      PRINTF("ACCEPTED ");                                                     \
    };                                                                         \
    if ((endpoint).flags & OSCORE_DECRYPTED) {                                 \
      PRINTF("OSCORE_DECRYPTED ");                                             \
    };                                                                         \
  } while (0)

#define SNPRINTFipaddr(str, size, endpoint)     \
  do {                                                                         \
    const char *scheme = "coap";                                               \
    if ((endpoint).flags & SECURED)                                            \
      scheme = "coaps";                                                        \
    if ((endpoint).flags & TCP)                                                \
      scheme = "coap+tcp";                                                     \
    if ((endpoint).flags & TCP && (endpoint).flags & SECURED)                  \
      scheme = "coaps+tcp";                                                    \
    memset(str, 0, size);                                                      \
    if ((endpoint).flags & IPV4) {                                             \
      SNPRINTF(str, size, "%s://%d.%d.%d.%d:%d", scheme,                       \
               ((endpoint).addr.ipv4.address)[0],                              \
               ((endpoint).addr.ipv4.address)[1],                              \
               ((endpoint).addr.ipv4.address)[2],                              \
               ((endpoint).addr.ipv4.address)[3], (endpoint).addr.ipv4.port);  \
    } else {                                                                   \
      SNPRINTF(                                                                \
        str, size,                                                             \
        "%s://"                                                                \
        "[%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:"     \
        "%02x%02x]:%d",                                                        \
        scheme, ((endpoint).addr.ipv6.address)[0],                             \
        ((endpoint).addr.ipv6.address)[1], ((endpoint).addr.ipv6.address)[2],  \
        ((endpoint).addr.ipv6.address)[3], ((endpoint).addr.ipv6.address)[4],  \
        ((endpoint).addr.ipv6.address)[5], ((endpoint).addr.ipv6.address)[6],  \
        ((endpoint).addr.ipv6.address)[7], ((endpoint).addr.ipv6.address)[8],  \
        ((endpoint).addr.ipv6.address)[9], ((endpoint).addr.ipv6.address)[10], \
        ((endpoint).addr.ipv6.address)[11],                                    \
        ((endpoint).addr.ipv6.address)[12],                                    \
        ((endpoint).addr.ipv6.address)[13],                                    \
        ((endpoint).addr.ipv6.address)[14],                                    \
        ((endpoint).addr.ipv6.address)[15], (endpoint).addr.ipv6.port);        \
    }                                                                          \
  } while (0)

#define SNPRINTFbytes(buff, size, data, len)                                   \
  do {                                                                         \
    char *beg = (buff);                                                        \
    char *end = (buff) + (size);                                               \
    for (size_t i = 0; beg <= (end - 3) && i < (len); i++) {                   \
      beg += (i == 0) ? SPRINTF(beg, "%02x", (data)[i])                        \
                      : SPRINTF(beg, ":%02x", (data)[i]);                      \
    }                                                                          \
  } while (0)

#define PRINT16BYTEHEX(text, data)              \
  text "%02X%02X%02X%02X:%02X%02X%02X%02X:"     \
       "%02X%02X%02X%02X:%02X%02X%02X%02X",     \
  (data)[0], (data)[1], (data)[2], (data)[3],   \
  (data)[4], (data)[5], (data)[6], (data)[7],   \
  (data)[8], (data)[9], (data)[10],(data)[11],  \
  (data)[12],(data)[13],(data)[14],(data)[15]

#define PRINT13BYTEHEX(text, data)              \
  text "%02X%02X%02X%02X:%02X%02X%02X%02X:"     \
       "%02X%02X%02X%02X:%02X",                 \
  (data)[0], (data)[1], (data)[2], (data)[3],   \
  (data)[4], (data)[5], (data)[6], (data)[7],   \
  (data)[8], (data)[9], (data)[10],(data)[11],  \
  (data)[12]

// Note: it is recommended to use a console for the output that allows a 'no word wrap'

#ifdef OC_PRINT
  #ifdef __ZEPHYR__
    // logging via Zephyr logging subsystem (thread-safe ring buffer)
    #define PRINT(...)  LOG_INF(__VA_ARGS__)
    #define PRINTF(...) LOG_INF(__VA_ARGS__)
  #elif defined(KNX_LOG_TO_FILE)
    // print to file
    void oc_file_print(char* format, ...);

    // logging to file
    #define PRINT(...) oc_file_print(__VA_ARGS__)
    #define PRINTF(...) oc_file_print(__VA_ARGS__)
  #else
    // logging to console
    #define PRINT(...) OC_INF(__VA_ARGS__)
    #define PRINTF(...) printf(__VA_ARGS__)
  #endif
#else
    // logging to void
    #define PRINT(...)
    #define PRINTF(...)
#endif

/* OC_LOG
 * Prepends timestamp, file:line and function name, then the message.
 *
 * Only used on non-Zephyr platforms. On Zephyr, OC_ERR/WRN/INF/DBG map directly
 * to LOG_ERR/WRN/INF/DBG and Zephyr adds its own standard metadata.
 */ // TODO 17 Do we want a switch for this in the config?
#ifndef __ZEPHYR__
#define OC_LOG(level, ...)                                      \
  do {                                                          \
  \
  char fileShort[20] = "...................";                   \
  strncpy(fileShort, __FILENAME__, 15);                         \
  \
  char funcShort[30] = ".............................";         \
  strncpy(funcShort, __func__, 24);                             \
  PRINTF("\n"                                                   \
         "%-14" PRIu64 ": "                                     \
         "%-4s: "                                               \
         "%-20.18s"                                             \
         "%-5d: "                                               \
         "%-30.27s> ",                                          \
         oc_clock_time(),                                       \
         level,                                                 \
         strlen(__FILENAME__) > 18 ? fileShort : __FILENAME__,  \
         __LINE__,                                              \
         strlen(__func__) > 27 ? funcShort : __func__);         \
  \
  PRINTF(__VA_ARGS__);                                          \
  } while (0)

/* OC_ERR / OC_WRN / OC_INF
 * Platform-specific log macros for error, warning and info messages.
 *
 * Windows/Unix
 * Routed through OC_LOG which prepends its own metadata.
 * Always do OC_ERR and OC_WRN logs.
 */
#define OC_ERR(...) OC_LOG("ERR", __VA_ARGS__)
#define OC_WRN(...) OC_LOG("WRN", __VA_ARGS__)
#define OC_INF(...) OC_LOG("INF", __VA_ARGS__)

/* knx_log_bytes_hex: Print label then bytes as lowercase hex ("xx "), 32 bytes per line.
 * Defined in port/oc_log.c. Used by OC_LOGbytes and OC_LOGbytes_OSCORE.
 */
void knx_log_bytes_hex(const char *label, const uint8_t *bytes, size_t length);

/* OC_DBG / OC_LOGbytes */
#ifdef OC_DEBUG
  #define OC_DBG(...) OC_LOG("DBG", __VA_ARGS__)
  #define OC_LOGbytes(bytes, length) \
    knx_log_bytes_hex(#bytes "\t: ", (const uint8_t *)(bytes), (size_t)(length))
#else
  #define OC_DBG(...)
  #define OC_LOGbytes(bytes, length)
#endif

/* OC_DBG_OSCORE / OC_DBG_SPAKE */
#ifdef OC_DEBUG_OSCORE
  #define OC_DBG_OSCORE(...) OC_LOG("OSC", __VA_ARGS__)
  #define OC_DBG_SPAKE(...)  OC_LOG("SPK", __VA_ARGS__)
  #define OC_LOGbytes_OSCORE(bytes, length) \
    knx_log_bytes_hex("OSCORE: " #bytes "\t: ", (const uint8_t *)(bytes), (size_t)(length))
#else
  #define OC_DBG_OSCORE(...)
  #define OC_DBG_SPAKE(...)
  #define OC_LOGbytes_OSCORE(bytes, length)
#endif

#endif

#ifdef __cplusplus
}
#endif

#endif 
