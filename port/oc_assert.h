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
#if OC_LOG_TO_FILE // TODO FIXME is there a problem when we write to the file on abort?
  (void)msg;
#else
  PRINTF("\n%s\nAbort.\n", msg); // TODO FIXME shouldn't we always print a message on abort?
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
static inline void
oc_exit(int status)
{
  exit_impl(status);
}

#ifdef __cplusplus
}
#endif

#endif /* OC_ASSERT_H */
