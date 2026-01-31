/* 
 * Copyright (c) 2019 Kistler Instrumente AG.
 * Copyright (c) 2024-2026 KNX Association
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TCP_ADAPTER_H
#define TCP_ADAPTER_H

#include "ipcontext.h"
#include "port/oc_connectivity.h"

#ifdef __cplusplus
extern "C" {
#endif

int oc_tcp_connectivity_init(ip_context_t *dev);
void oc_tcp_connectivity_shutdown(ip_context_t *dev);
int oc_tcp_send_buffer(ip_context_t *dev, oc_message_t *message,
        const struct sockaddr_storage *receiver);
void oc_tcp_end_session(oc_endpoint_t *endpoint);
void oc_tcp_adapter_mutex_init(void);
void oc_tcp_adapter_mutex_destroy(void);

#ifdef __cplusplus
}
#endif

#endif /* TCP_ADAPTER_H */
