/*
 * Copyright (c) 2026 Alexander Burker
 * Copyright (c) 2026 KNX Association
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "oc_memb.h"
#include "port/oc_log.h"

void oc_memb_init(struct oc_memb* m)
{
  (void)m;
}

void* oc_memb_alloc(struct oc_memb* m)
{
  if (!m)
  {
    OC_ERR("Memory root node is NULL!");
    return NULL;
  }

  void* ptr = calloc(1, m->size);

  if (!ptr)
  {
    return NULL;
  }

  return ptr;
}

char oc_memb_free(struct oc_memb* m, void* ptr)
{
  if (!m)
  {
    OC_ERR("Memory root node is NULL!");
    return -1;
  }

  free(ptr);

  if (m->buffers_avail_cb)
  {
    m->buffers_avail_cb(oc_memb_numfree(m));
  }

  return 0;
}

int oc_memb_inmemb(struct oc_memb* m, void* ptr)
{
  (void)m;
  (void)ptr;
  return 0;
}

int oc_memb_numfree(struct oc_memb* m)
{
  (void)m;
  return 0;
}

void oc_memb_set_buffers_avail_cb(struct oc_memb* m, oc_memb_buffers_avail_callback_t cb)
{
  m->buffers_avail_cb = cb;
}
