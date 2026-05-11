/* 
 * Copyright (c) 2016 Intel Corporation
 * Copyright (c) 2024-2026 KNX Association
 *            
 * SPDX-License-Identifier: Apache-2.0
 */

/**
  @brief platform specific asserts
  @file
*/
#ifndef OC_ASSERT_H
#define OC_ASSERT_H

#include "port/oc_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief abort application
 *
 */
void abort_impl(void);

/**
 * @brief exit the application
 *
 * @param status the exist status
 */
void exit_impl(int status);

/**
 * @brief abort with message
 *
 * @param msg the message to be printed
 */
static inline void oc_abort(const char *msg)
{
  (void) msg;	// TODO 14 FIXME without compiler errors on second runs
// TODO 14 FIXME KNX_LOG_TO_FILE is not set correctly when building mbedtls, fix this!
#ifdef KNX_LOG_TO_FILE // TODO 14 FIXME is there a problem when we write to the file on abort?
  (void)msg;
#else
  //PRINTF("\n%s\nAbort.\n", msg); // TODO 14 FIXME shouldn't we always print a message on abort?
#endif
  abort_impl();
}

/**
 * @brief assert the condition and if it fails abort with message (reason)
 *
 */
#define oc_assert(cond)                                                        \
  do {                                                                         \
    if (!(cond)) {                                                             \
      oc_abort("Assertion (" #cond ") failed.");                               \
    }                                                                          \
  } while (0)

/**
 * @brief exit the application with status
 *
 * @param status the exist status
 */
static inline void oc_exit(int status)
{
  exit_impl(status);
}

#ifdef __cplusplus
}
#endif

#endif
