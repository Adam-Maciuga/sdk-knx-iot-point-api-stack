/*
 * Copyright (c) 2020 Intel Corporation
 * Copyright (c) 2024-2026 KNX Association
 *            
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OC_OSCORE_H
#define OC_OSCORE_H

#include "util/oc_process.h"

#ifdef __cplusplus
extern "C" {
#endif

OC_PROCESS_NAME(oc_oscore_handler);

void oc_oscore_set_next_ssn(uint64_t ssn);
uint64_t oc_oscore_get_next_ssn(void);

#ifdef __cplusplus
}
#endif

#endif 
