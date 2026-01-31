/* 
 * Copyright (c) 2017 Lynx Technology
 * Copyright (c) 2018 Intel Corporation
 * Copyright (c) 2019 Kistler Instrumente AG
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IPCONTEXT_H
#define IPCONTEXT_H

#define WIN32_LEAN_AND_MEAN
// clang-format off
#include <windows.h>
#include <winsock2.h>
#include "oc_endpoint.h"
#include <mswsock.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
// clang-format on

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  ADAPTER_STATUS_NONE = 0, // Nothing happens
  ADAPTER_STATUS_ACCEPT,   // Receiving no meaningful data
  ADAPTER_STATUS_RECEIVE,  // Receiving meaningful data
  ADAPTER_STATUS_ERROR     // Error
} adapter_receive_state_t;

#ifdef OC_TCP
typedef struct tcp_context_t
{
  struct sockaddr_storage server;
  SOCKET server_sock;
  uint16_t port;
  WSAEVENT server_event;
#ifdef KNX_TCP_TLS
  struct sockaddr_storage secure;
  SOCKET secure_sock;
  uint16_t tls_port;
  WSAEVENT secure_event;
#endif
  HANDLE signal_event;
  HANDLE event_thread_handle;
  DWORD event_thread;
} tcp_context_t;
#endif

typedef struct ip_context_t
{
  struct ip_context_t *next;
  OC_LIST_STRUCT(eps);
  struct sockaddr_storage mcast;
  struct sockaddr_storage server;
  SOCKET mcast_sock;
  SOCKET server_sock;
  uint16_t port;
#ifdef KNX_UDP_DTLS
  struct sockaddr_storage secure;
  SOCKET secure_sock;
  uint16_t dtls_port;
#endif 
#ifdef OC_TCP
  tcp_context_t tcp;
#endif
  HANDLE event_thread_handle;
  HANDLE event_server_handle;
  DWORD event_thread;
  BOOL terminate;
} ip_context_t;

#ifdef __cplusplus
}
#endif

#endif /* IPCONTEXT_H */
