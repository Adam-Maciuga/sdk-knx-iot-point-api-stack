/* 
 * Copyright 2018 Samsung Electronics All Rights Reserved.
 * Copyright (c) 2024-2026 KNX Association
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TCP_ADAPTER_H
#define TCP_ADAPTER_H

#include "ipcontext.h"
#include "port/oc_connectivity.h"
#include <sys/select.h>

#ifdef __cplusplus
extern "C" {
#endif

int oc_tcp_connectivity_init(ip_context_t *dev);
void oc_tcp_connectivity_shutdown(ip_context_t *dev);
int oc_tcp_send_buffer(ip_context_t *dev, oc_message_t *message,
        const struct sockaddr_storage *receiver);
void oc_tcp_add_socks_to_fd_set(ip_context_t *dev);
void oc_tcp_set_session_fds(fd_set *fds);
adapter_receive_state_t oc_tcp_receive_message(ip_context_t *dev, fd_set *fds,
        oc_message_t *message);
void oc_tcp_end_session(ip_context_t *dev, oc_endpoint_t *endpoint);

#ifdef __cplusplus
}
#endif

#endif /* TCP_ADAPTER_H */
