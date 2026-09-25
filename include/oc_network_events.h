/*
 * Copyright (c) 2016-2018 Intel Corporation
 * Copyright (c) 2022 Cascoda Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/**
  @brief network events (network interfaces going up/down)
  @file
*/
#pragma once

#include "port/oc_network_events_mutex.h"
#include "util/oc_process.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief network events
 *
 */
typedef enum {
  NETWORK_INTERFACE_DOWN, /**< network interface down */
  NETWORK_INTERFACE_UP    /**< network interface up */
} oc_interface_event_t;

/**
  @brief Callback function to pass the network interface up/down infomation
    to App.
  @param event  enum values in oc_interface_event_t.
*/
typedef void (*interface_event_handler_t)(oc_interface_event_t event);

/**
 * Structure to manage network interface handler list.
 */
typedef struct oc_network_interface_cb
{
  struct oc_network_interface_cb *next; /**< next in the list */
  interface_event_handler_t handler;    /**< the callback (handler) */
} oc_network_interface_cb_t;

/**
 * @brief process network events
 */
OC_PROCESS_NAME(oc_network_events);

typedef struct oc_message_s oc_message_t;

/**
 * @brief receive network event
 *
 * @param message the network message
 */
void oc_network_event(oc_message_t *message);

/**
 * @brief initiate network event
 *
 * @param event the event
 */
void oc_network_interface_event(oc_interface_event_t event);

#ifdef __cplusplus
}
#endif
