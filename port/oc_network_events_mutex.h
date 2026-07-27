/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/**
  @brief platform abstraction of networking events (mutexes)
  @file
*/
#ifndef OC_NETWORK_EVENTS_MUTEX_H
#define OC_NETWORK_EVENTS_MUTEX_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief initialize the network event handler mutex
 *
 */
void oc_network_event_handler_mutex_init(void);

/**
 * @brief lock the network event hander
 *
 */
void oc_network_event_handler_mutex_lock(void);

/**
 * @brief unlock the network event handler
 *
 */
void oc_network_event_handler_mutex_unlock(void);

/**
 * @brief destroy the network event handler mutex
 *
 */
void oc_network_event_handler_mutex_destroy(void);

#ifdef __cplusplus
}
#endif

#endif /* OC_NETWORK_EVENTS_MUTEX_H */
