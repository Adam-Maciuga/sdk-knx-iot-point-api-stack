/*
 * Copyright (c) 2016 Intel Corporation
 *
 * Copyright (c) 2005, Swedish Institute of Computer Science
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 */

#include "oc_mmem.h"
#include "oc_config.h"
#include "oc_list.h"
#include "port/oc_log.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

size_t oc_mmem_alloc(struct oc_mmem* m, size_t size, pool pool_type)
{
  if (!m)
  {
    OC_ERR("oc_mmem is NULL");
    return 0;
  }

  size_t bytes_allocated = 0;

  switch (pool_type)
  {
  case BYTE_POOL:
    bytes_allocated += size * sizeof(uint8_t);
    m->ptr = malloc(size);
    m->size = size;
    break;
  case INT_POOL:
    bytes_allocated += size * sizeof(int64_t);
    m->ptr = malloc(size * sizeof(int64_t));
    m->size = size;
    break;
  case FLOAT_POOL:
    bytes_allocated += size * sizeof(float);
    m->ptr = malloc(size * sizeof(float));
    m->size = size;
  case DOUBLE_POOL:
    bytes_allocated += size * sizeof(double);
    m->ptr = malloc(size * sizeof(double));
    m->size = size;
    break;
  default:
    break;
  }

  return (int)bytes_allocated;
}

void oc_mmem_free(struct oc_mmem* m, pool pool_type)
{
  if (!m)
  {
    OC_ERR("oc_mmem is NULL");
    return;
  }

  (void)pool_type;
  free(m->ptr);
  m->size = 0;
}

void oc_mmem_init(void)
{
}
