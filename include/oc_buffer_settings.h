/*
 * Copyright (c) 2017 Intel Corporation
 * Copyright (c) 2026 KNX Association
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/**
  @brief CoAP message buffer setting implementation.
  @file
*/
#ifndef OC_BUFFER_SETTINGS_H
#define OC_BUFFER_SETTINGS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief sets the size of the MTU to be used
 *        
 *
 * @param mtu_size size in bytes 
 *        (MTU size covers CoAP payload/header, not UDP/IPv6 header)
 *
 * @return int 0-success
 */
int oc_set_mtu_size(uint32_t mtu_size);

/**
 * @brief retrieve the currently used MTU size 
 *        
 * @return uint32_t the MTU size in bytes 
 *         (MTU size covers CoAP payload/header, not UDP/IPv6 header)
 */
uint32_t oc_get_mtu_size(void);

/**
 * @brief retrieve the max application data size
 *
 * @note value is defined in CMakeLists.txt
 *
 * @return uint32_t size in bytes
 */
uint32_t oc_get_max_app_data_size(void);

/**
 * @brief retrieve the block size
 *
 * @return uint16_t the block size in bytes
 */
uint16_t oc_get_block_size(void);

#ifdef __cplusplus
}
#endif

#endif
