/*
 * Copyright (c) 2026 Alexander Burker
 * Copyright (c) 2026 KNX Association
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * \defgroup memb Memory block management functions
 *
 * Thin wrapper around calloc() and free() using named memory pools.
 * Declare a pool with OC_MEMB(), allocate from it with oc_memb_alloc(),
 * and release with oc_memb_free().
 *
 * \note This module is a transitional compatibility layer. All call sites
 * should be migrated to direct calloc() and free() calls. The module
 * will be removed once the migration is complete.
 *
 */

#ifndef OC_MEMB_H
#define OC_MEMB_H

#include "oc_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Declare a named dynamic memory pool for a given structure type.
 *
 * Allocations are served from the system heap via calloc(). The \p num
 * parameter is accepted for source compatibility with existing call sites
 * but is ignored at runtime.
 *
 * Example:
 \code
 OC_MEMB(connections, struct connection, 0);
 \endcode
 *
 * \param name      The name of the pool (used with oc_memb_alloc() and oc_memb_free()).
 * \param structure The struct type held by this pool.
 * \param num       Ignored. Kept for source compatibility.
 *
 */
#ifdef __cplusplus
}
#endif
#include <stdlib.h>
#ifdef __cplusplus
extern "C" {
#endif
#define OC_MEMB(name, structure, num)                                          \
  static struct oc_memb name = { sizeof(structure), 0 }

typedef void (*oc_memb_buffers_avail_callback_t)(int);

struct oc_memb
{
  /** Size of each memory block in bytes (sizeof the element type) */
  unsigned short size;
  /** Called when the number of available buffers changes */
  oc_memb_buffers_avail_callback_t buffers_avail_cb;
};

/**
 * Initialize a memory block that was declared with MEMB().
 *
 * \param m A memory block previously declared with MEMB().
 */
void oc_memb_init(struct oc_memb* m);

/**
 * Allocate a memory block from a block of memory declared with MEMB() and init it with '0'.
 *
 * \param m A memory block previously declared with MEMB().
 */
void* oc_memb_alloc(struct oc_memb* m);

/**
 * Deallocate a memory block previously allocated with oc_memb_alloc().
 *
 * \param m   A memory pool previously declared with OC_MEMB().
 * \param ptr A pointer to the memory block to deallocate.
 *
 * \return 0 on success, -1 if \p m is NULL.
 */
char oc_memb_free(struct oc_memb* m, void* ptr);

void oc_memb_set_buffers_avail_cb(struct oc_memb* m, oc_memb_buffers_avail_callback_t cb);

int oc_memb_inmemb(struct oc_memb* m, void* ptr);

int oc_memb_numfree(struct oc_memb* m);

#ifdef __cplusplus
}
#endif

#endif
