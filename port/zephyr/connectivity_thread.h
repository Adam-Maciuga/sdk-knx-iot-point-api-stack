/*
 * Copyright (c) 2026 KNX Association
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief OpenThread connectivity port, Zephyr-internal interface.
 *
 * Shared between connectivity_thread.c and dns-sd-thread.c only. The public
 * connectivity API lives in port/oc_connectivity.h.
 */

#ifndef CONNECTIVITY_THREAD_H
#define CONNECTIVITY_THREAD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief UDP port the KNX IoT unicast socket is actually bound to.
 *
 * Mirrors the socket port, where DNS-SD advertises the port returned by
 * getsockname(). Before oc_connectivity_init() this is the configured port
 * (CONFIG_KNX_UNICAST_PORT); afterwards it is the bound one, which differs
 * when the configured port was 0 (ephemeral).
 *
 * @return uint16_t port number
 */
uint16_t oc_connectivity_thread_get_port(void);

#ifdef __cplusplus
}
#endif

#endif /* CONNECTIVITY_THREAD_H */
