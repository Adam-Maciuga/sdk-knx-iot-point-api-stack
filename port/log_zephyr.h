/* 
 * Copyright (c) 2026 Alexander Burker
 * Copyright (c) 2026 KNX Association
 *            
 * SPDX-License-Identifier: Apache-2.0
 *
 *  @brief platform abstraction of logging
 *  @file
 *
 */

#pragma once

#include <zephyr/logging/log.h>

/* Zephyr native logging
 * When building for Zephyr OC_ERR/OC_WRN/OC_INF/OC_DBG map directly to 
 * LOG_ERR/LOG_WRN/LOG_INF/LOG_DBG instead of the custom OC_LOG timestamp path below.
 *
 * Log level is controlled by CONFIG_KNXIOT_LOG_LEVEL (0=off, 4=DBG).
 * The module "knx_iot" can be filtered at runtime via the Zephyr shell:
 * > log enable/disable <level> knx_iot
 *
 * NOTE: Thread-safety:
 * Zephyr's logging subsystem serialises all output through a lock-free ring buffer.
 * Multiple threads can call LOG_* concurrently without their lines interleaving.
 * A plain printf/UART path has no such protection.
 */
/* LOG_MODULE_REGISTER is called once in port/zephyr/log.c.
 * Every translation unit that includes this header declares itself as part of
 * the "knx_iot" module so all OC_* log calls carry the same module tag.
 */
#if defined(CONFIG_KNXIOT_DEBUG) || defined(CONFIG_KNXIOT_DEBUG_OSCORE)
LOG_MODULE_DECLARE(knx_iot, LOG_LEVEL_DBG);
#else
LOG_MODULE_DECLARE(knx_iot, CONFIG_KNXIOT_LOG_LEVEL);
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* OC_ERR / OC_WRN / OC_INF
 * Platform-specific log macros for error, warning and info messages.
 *
 * Use Zephyr's LOG_ERR/WRN/INF. Output is thread-safe and deferred via
 * a ring buffer. Zephyr adds standard metadata (timestamp, level, module).
 */
#define OC_ERR(...) LOG_ERR(__VA_ARGS__)
#define OC_WRN(...) LOG_WRN(__VA_ARGS__)
#define OC_INF(...) LOG_INF(__VA_ARGS__)

/* OC_DBG / OC_LOGbytes */
#ifdef OC_DEBUG
  /* On Zephyr debug output via LOG_DBG, filtered by CONFIG_KNXIOT_LOG_LEVEL.
   *
   * OC_LOGbytes uses LOG_HEXDUMP_DBG when CONFIG_KNXIOT_LOG_HEXDUMP_DBG=y.
   * Without that option it is a no-op. Per-byte LOG_INF calls would generate
   * one log message per byte and flood the output.
   */
  #define OC_DBG(...) LOG_DBG(__VA_ARGS__)
  #ifdef CONFIG_KNXIOT_LOG_HEXDUMP_DBG
    #define OC_LOGbytes(bytes, length) LOG_HEXDUMP_DBG(bytes, length, "")
  #else
    #define OC_LOGbytes(bytes, length)
  #endif
#else
  #define OC_DBG(...)
  #define OC_LOGbytes(bytes, length)
#endif

/* OC_DBG_OSCORE / OC_DBG_SPAKE */
#ifdef OC_DEBUG_OSCORE
  /* Routes OSCORE and SPAKE debug output to LOG_DBG with a fixed prefix.
   *
   * The prefix is prepended by compile-time string concatenation, so the
   * first argument must always be a string literal format string.
   */
  #define OC_DBG_OSCORE(...) LOG_DBG("OSCORE: " __VA_ARGS__)
  #define OC_DBG_SPAKE(...)  LOG_DBG("SPAKE: "  __VA_ARGS__)
  #define OC_LOGbytes_OSCORE(bytes, length) OC_LOGbytes(bytes, length)
#else
  #define OC_DBG_OSCORE(...)
  #define OC_DBG_SPAKE(...)
  #define OC_LOGbytes_OSCORE(bytes, length)
#endif

#ifdef __cplusplus
}
#endif
