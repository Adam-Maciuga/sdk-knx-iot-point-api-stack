/* 
 * Copyright (c) 2019 Kistler Instrumente AG
 * Copyright (c) 2021 Cascoda Ltd
 * Copyright (c) 2024-2026 KNX Association
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define WIN32_LEAN_AND_MEAN
#include "tcpadapter.h"
#include "api/oc_session_events_internal.h"
#include "ipcontext.h"
#include "messaging/coap/coap.h"
#include "mutex.h"
#include "network_addresses.h"
#include "oc_endpoint.h"
#include "oc_session_events.h"
#include "port/oc_assert.h"
#include "util/oc_memb.h"
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>

#ifdef OC_TCP

#define OC_TCP_LISTEN_BACKLOG 3

#define TLS_HEADER_SIZE 5

#define DEFAULT_RECEIVE_SIZE                                                   \
  (COAP_TCP_DEFAULT_HEADER_LEN + COAP_TCP_MAX_EXTENDED_LENGTH_LEN)

#define LIMIT_RETRY_CONNECT 5

#define TCP_CONNECT_TIMEOUT 5

typedef struct tcp_session
{
  struct tcp_session *next;
  ip_context_t *dev;
  oc_endpoint_t endpoint;
  SOCKET sock;
  HANDLE sock_event;
  tcp_csm_state_t csm_state;
} tcp_session_t;

OC_LIST(session_list);
OC_LIST(free_session_list_async);
OC_MEMB(tcp_session_s, tcp_session_t, OC_MAX_TCP_PEERS);

static HANDLE mutex;

void oc_tcp_adapter_mutex_init(void)
{
  mutex = mutex_new();
}

void oc_tcp_adapter_mutex_destroy(void)
{
  mutex_free(mutex);
}

void oc_tcp_adapter_mutex_lock(void)
{
  mutex_lock(mutex);
}

void oc_tcp_adapter_mutex_unlock(void)
{
  mutex_unlock(mutex);
}

static int configure_tcp_socket(SOCKET sock, struct sockaddr_storage *sock_info)
{
  if (bind(sock, (struct sockaddr *)sock_info, sizeof(*sock_info)) == SOCKET_ERROR) {
    OC_ERR("Unable to bind socket (%d)!", WSAGetLastError());
    return SOCKET_ERROR;
  }

  int reuse = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse,
          sizeof(reuse)) == SOCKET_ERROR) {
    OC_ERR("Error setting reuseaddr option (%d)!", WSAGetLastError());
    return SOCKET_ERROR;
  }

  if (listen(sock, OC_TCP_LISTEN_BACKLOG) == SOCKET_ERROR) {
    OC_ERR("Error listening to socket (%d)!", errno);
    return SOCKET_ERROR;
  }

  return 0;
}

static int get_assigned_tcp_port(SOCKET sock, struct sockaddr_storage *sock_info)
{
  socklen_t socklen = sizeof(*sock_info);
  if (getsockname(sock, (struct sockaddr *)sock_info, &socklen) ==
      SOCKET_ERROR) {
    OC_ERR("Unable to obtain socket information (%d)!", WSAGetLastError());
    return SOCKET_ERROR;
  }

  return 0;
}

static int get_interface_index(SOCKET sock)
{
  int interface_index = SOCKET_ERROR;

  struct sockaddr_storage addr;
  if (get_assigned_tcp_port(sock, &addr) == SOCKET_ERROR) {
    return SOCKET_ERROR;
  }

  ifaddr_t *ifaddr_list = get_network_addresses();
  ifaddr_t *interface;

  for (interface = ifaddr_list; interface != NULL;
       interface = interface->next) {
    if (addr.ss_family == interface->addr.ss_family) {
      if (addr.ss_family == AF_INET6) {
        struct sockaddr_in6 *a = (struct sockaddr_in6 *)&interface->addr;
        struct sockaddr_in6 *b = (struct sockaddr_in6 *)&addr;
        if (memcmp(a->sin6_addr.s6_addr, b->sin6_addr.s6_addr, 16) == 0) {
          interface_index = interface->if_index;
          break;
        }
      }
    }
  }

  free_network_addresses(ifaddr_list);
  return interface_index;
}

static void free_tcp_session_locked(tcp_session_t *session, oc_endpoint_t *endpoint,
        SOCKET *sock, HANDLE *sock_event)
{
  oc_tcp_adapter_mutex_lock();
  oc_list_remove(session_list, session);
  memcpy_s(endpoint, sizeof(*endpoint), &session->endpoint,
           sizeof(session->endpoint));
  *sock = session->sock;
  *sock_event = session->sock_event;
  oc_memb_free(&tcp_session_s, session);
  oc_tcp_adapter_mutex_unlock();

  OC_DBG("Freed TCP session (locked).");
}

static void free_tcp_session(tcp_session_t *session)
{
  oc_endpoint_t endpoint;
  SOCKET sock;
  HANDLE sock_event;
  free_tcp_session_locked(session, &endpoint, &sock, &sock_event);
  WSACloseEvent(sock_event);
  closesocket(sock);
  if (!oc_session_events_is_ongoing()) {
    oc_session_end_event(&endpoint);
  }

  OC_DBG("Freed TCP session.");
}

static void free_tcp_session_async_locked(tcp_session_t *session)
{
  oc_list_remove(session_list, session);
  oc_list_add(free_session_list_async, session);

  if (!SetEvent(session->dev->tcp.signal_event)) {
    OC_ERR("Unable to trigger TCP signal event (%d)!", GetLastError());
  }
  OC_DBG("Freed TCP session (async, locked).");
}

static int set_socket_block_mode(SOCKET sockfd, u_long nonblock)
{
  int error = ioctlsocket(sockfd, FIONBIO, &nonblock);
  if (error == SOCKET_ERROR) {
    OC_ERR("Unable to set socket as blocking(%ul) (%d)!", nonblock, WSAGetLastError());
    return SOCKET_ERROR;
  }
  return 0;
}

static int add_new_session_locked(SOCKET sock, ip_context_t *dev, oc_endpoint_t *endpoint,
        tcp_csm_state_t state)
{
  HANDLE sock_event = WSACreateEvent();
  if (WSAEventSelect(sock, sock_event, FD_READ | FD_CLOSE) == SOCKET_ERROR) {
    OC_ERR("Unable to create socket session event (%d)!", WSAGetLastError());
    return SOCKET_ERROR;
  }
  tcp_session_t *session = oc_memb_alloc(&tcp_session_s);
  if (!session) {
    WSACloseEvent(sock_event);
    OC_ERR("Could not allocate new TCP session object!");
    return SOCKET_ERROR;
  }

  endpoint->interface_index = get_interface_index(sock);
  memcpy(&session->endpoint, endpoint, sizeof(oc_endpoint_t));
  session->dev = dev;
  session->endpoint.next = NULL;
  session->sock = sock;
  session->csm_state = state;
  session->sock_event = sock_event;

  oc_list_add(session_list, session);

  if (!(endpoint->flags & SECURED)) {
    oc_session_start_event((oc_endpoint_t *)endpoint);
  }

  OC_DBG("Recorded new TCP session.");

  return 0;
}

static int accept_new_session(ip_context_t *dev, SOCKET fd, oc_endpoint_t *endpoint)
{
  struct sockaddr_storage receive_from;
  socklen_t receive_len = sizeof(receive_from);

  SOCKET new_socket =
    accept(fd, (struct sockaddr *)&receive_from, &receive_len);
  if (new_socket == INVALID_SOCKET) {
    OC_ERR("failed to accept incoming TCP connection %d", WSAGetLastError());
    return SOCKET_ERROR;
  }
  OC_DBG("Accepted incomming TCP connection");

  if (endpoint->flags & IPV6) {
    struct sockaddr_in6 *r = (struct sockaddr_in6 *)&receive_from;
    memcpy(endpoint->addr.ipv6.address, r->sin6_addr.s6_addr,
           sizeof(r->sin6_addr.s6_addr));
    endpoint->addr.ipv6.scope = (uint8_t)r->sin6_scope_id;
    endpoint->addr.ipv6.port = ntohs(r->sin6_port);
  }

  oc_tcp_adapter_mutex_lock();
  if (add_new_session_locked(new_socket, dev, endpoint, CSM_NONE) < 0) {
    oc_tcp_adapter_mutex_unlock();
    OC_ERR("Could not record new TCP session!");
    closesocket(new_socket);
    return SOCKET_ERROR;
  }
  oc_tcp_adapter_mutex_unlock();

  if (!SetEvent(dev->tcp.signal_event)) {
    OC_ERR("Could not trigger TCP signal event (%d)!", GetLastError());
    return SOCKET_ERROR;
  }

  return 0;
}

static tcp_session_t * find_session_by_endpoint_locked(oc_endpoint_t *endpoint)
{
  tcp_session_t *session = oc_list_head(session_list);
  while (session != NULL &&
         oc_endpoint_compare(&session->endpoint, endpoint) != 0) {
    session = session->next;
  }

  if (!session) {
#ifdef OC_DEBUG
    PRINT("Could not find ongoing TCP session for endpoint:");
    PRINTipaddr(*endpoint);
#endif
    return NULL;
  }
#ifdef OC_DEBUG
  PRINT("Found TCP session for endpoint:");
  PRINTipaddr(*endpoint);
#endif
  return session;
}

static tcp_session_t * get_ready_to_read_session(fd_set *setfds)
{
  tcp_session_t *session = oc_list_head(session_list);
  while (session != NULL && !FD_ISSET(session->sock, setfds)) {
    session = session->next;
  }

  if (!session) {
    OC_ERR("Could not find any open ready-to-read session!");
    return NULL;
  }

  return session;
}

static size_t get_total_length_from_header(oc_message_t *message, oc_endpoint_t *endpoint)
{
  size_t total_length = 0;
  if (endpoint->flags & SECURED) {
    //[3][4] bytes in tls header are tls payload length
    total_length =
      TLS_HEADER_SIZE + (size_t)((message->data[3] << 8) | message->data[4]);
  } else {
    total_length = coap_tcp_get_packet_size(message->data);
  }

  return total_length;
}

void oc_tcp_end_session(oc_endpoint_t *endpoint)
{
  oc_tcp_adapter_mutex_lock();
  tcp_session_t *session = find_session_by_endpoint_locked(endpoint);
  if (session) {
    free_tcp_session_async_locked(session);
  }
  oc_tcp_adapter_mutex_unlock();
}

static SOCKET get_session_socket_locked(oc_endpoint_t *endpoint)
{
  SOCKET sock = INVALID_SOCKET;
  tcp_session_t *session = find_session_by_endpoint_locked(endpoint);
  if (!session) {
    return INVALID_SOCKET;
  }

  sock = session->sock;
  return sock;
}

static int connect_nonb(SOCKET sockfd, const struct sockaddr *r, int r_len, int nsec)
{
  if (set_socket_block_mode(sockfd, 1) == SOCKET_ERROR) {
    return SOCKET_ERROR;
  }

  int n = connect(sockfd, (struct sockaddr *)r, r_len);
  if (n == SOCKET_ERROR) {
    if (WSAGetLastError() != WSAEWOULDBLOCK) {
      OC_ERR("Error while connecting (%d)!", WSAGetLastError());
      return SOCKET_ERROR;
    }
  }

  // Do whatever we want while the connect is taking place.
  if (n == 0) {
    // Connect completed immediately.
    return 0;
  }

  fd_set wset;
  FD_ZERO(&wset);
  FD_SET(sockfd, &wset);
  struct timeval tval;
  tval.tv_sec = nsec;
  tval.tv_usec = 0;

  if ((n = select((int)sockfd + 1, NULL, &wset, NULL, nsec ? &tval : NULL)) ==
      0) {
    // timeout
    WSASetLastError(WSAETIMEDOUT);
    OC_ERR("connect %d", WSAGetLastError());
    return SOCKET_ERROR;
  }
  if (n == SOCKET_ERROR) {
    return SOCKET_ERROR;
  }

  if (FD_ISSET(sockfd, &wset)) {
    if (set_socket_block_mode(sockfd, 0) == SOCKET_ERROR) {
      return SOCKET_ERROR;
    }
    return 0;
  }
  OC_DBG("Select error: sockfd not set");
  return SOCKET_ERROR;
}

static SOCKET initiate_new_session_locked(ip_context_t *dev, oc_endpoint_t *endpoint,
        const struct sockaddr_storage *receiver)
{
  SOCKET sock = INVALID_SOCKET;
  uint8_t retry_cnt = 0;

  while (retry_cnt < LIMIT_RETRY_CONNECT) {
    if (endpoint->flags & IPV6) {
      sock = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    }

    if (sock == INVALID_SOCKET) {
      OC_ERR("Could not create socket for new TCP session (%d)!"
              WSAGetLastError());
      return sock;
    }

    socklen_t receiver_size = sizeof(*receiver);
    int ret = 0;
    if ((ret = connect_nonb(sock, (struct sockaddr *)receiver, receiver_size,
            TCP_CONNECT_TIMEOUT)) == 0) {
      break;
    }

    closesocket(sock);
    retry_cnt++;
    OC_DBG("Connect failed with %d. retry (%d)", ret, retry_cnt);
  }

  if (retry_cnt >= LIMIT_RETRY_CONNECT) {
    OC_ERR("Could not initiate TCP connection (retry limit reached)!");
    return INVALID_SOCKET;
  }

  OC_DBG("Successfully initiated TCP connection.");

  if (add_new_session_locked(sock, dev, endpoint, CSM_SENT) < 0) {
    OC_ERR("Could not record new TCP session!");
    closesocket(sock);
    return INVALID_SOCKET;
  }

  if (!SetEvent(dev->tcp.signal_event)) {
    OC_ERR("Could not trigger TCP signal event (%d)!", GetLastError());
  }

  OC_DBG("Signaled network event thread to monitor the newly added session.");

  return sock;
}

int oc_tcp_send_buffer(ip_context_t *dev, oc_message_t *message,
        const struct sockaddr_storage *receiver)
{
  oc_tcp_adapter_mutex_lock();
  SOCKET send_sock = get_session_socket_locked(&message->endpoint);
  int bytes_sent = 0;

  if (send_sock == INVALID_SOCKET) {
    if (message->endpoint.flags & ACCEPTED) {
      OC_ERR("Unable to send message over TCP, connection was closed!");
      goto oc_tcp_send_buffer_done;
    }

    if ((send_sock = initiate_new_session_locked(dev, &message->endpoint,
              receiver)) == INVALID_SOCKET) {
      OC_ERR("Unable to send message over TCP, could not initiate new TCP session!");
      goto oc_tcp_send_buffer_done;
    }
  }

  do {
    int send_len = send(send_sock, (const char *)message->data + bytes_sent,
                        (int)message->length - bytes_sent, 0);
    if (send_len == SOCKET_ERROR) {
      int err = WSAGetLastError();
      if (err == WSAEWOULDBLOCK) {
        continue;
      }
      OC_WRN("TCP send message returned error %d!", err);
      goto oc_tcp_send_buffer_done;
    }
    bytes_sent += send_len;
  } while (bytes_sent < (int)message->length);

  OC_DBG("Sent %d bytes.", bytes_sent);
oc_tcp_send_buffer_done:
  oc_tcp_adapter_mutex_unlock();

  if (bytes_sent == 0) {
    return -1;
  }

  return bytes_sent;
}

static int recv_message_with_tcp_session(tcp_session_t *session, oc_message_t *message)
{
  size_t total_length = 0;
  size_t want_read = DEFAULT_RECEIVE_SIZE;
  message->length = 0;
  do {
    int count = recv(session->sock, (char *)message->data + message->length,
                     (int)want_read, 0);
    if (count == SOCKET_ERROR) {
      int err = WSAGetLastError();
      if (err == WSAEWOULDBLOCK) {
        continue;
      }
      OC_ERR("Error while receiving message over TCP (%d)!", errno);
      free_tcp_session(session);
      return ADAPTER_STATUS_ERROR;
    } else if (count == 0) {
      OC_DBG("Peer closed TCP session.");
      free_tcp_session(session);
      return ADAPTER_STATUS_NONE;
    }

    OC_DBG("recv(): %d bytes.", count);
    message->length += (size_t)count;
    want_read -= (size_t)count;

    if (total_length == 0) {
      total_length = get_total_length_from_header(message, &session->endpoint);
      if (total_length > (size_t)(OC_MAX_APP_DATA_SIZE + COAP_MAX_HEADER_SIZE)) {
        OC_ERR("Total receive length (%zu) is bigger than max pdu size (%zu)",
               total_length,
               (size_t)(OC_MAX_APP_DATA_SIZE + COAP_MAX_HEADER_SIZE));
        OC_ERR("A buffer overflow may occur!");
        return ADAPTER_STATUS_ERROR;
      }
      OC_DBG("TCP packet total length: %zu bytes.", total_length);

      want_read = total_length - (size_t)count;
    }
  } while (total_length > message->length);

  memcpy(&message->endpoint, &session->endpoint, sizeof(oc_endpoint_t));
#ifdef KNX_TCP_TLS
  if (message->endpoint.flags & SECURED) {
    message->encrypted = 1;
  }
#endif

  return ADAPTER_STATUS_RECEIVE;
}

static void recv_message(SOCKET s, void *ctx)
{
  (void)s;
  tcp_session_t *session = (tcp_session_t *)ctx;
  WSANETWORKEVENTS network_events;
  if (WSAEnumNetworkEvents(session->sock, session->sock_event,
                           &network_events) == SOCKET_ERROR) {
    OC_ERR("Error while receiving message, enumerate network event %d!", WSAGetLastError());
    free_tcp_session(session);
    return;
  }
  if (!(network_events.lNetworkEvents & (FD_READ | FD_CLOSE))) {
    return;
  }
  oc_message_t *message = oc_allocate_message();
  if (!message) {
    return;
  }
  int ret = recv_message_with_tcp_session(session, message);
  if (ret != ADAPTER_STATUS_RECEIVE) {
    oc_message_unref(message);
    return;
  }

#ifdef OC_DEBUG
  PRINT("Incoming message of size %zd bytes from endpoint:", message->length);
  PRINTipaddr(message->endpoint);
#endif
  oc_network_event(message);
}

static void accept_socket(SOCKET s, void *ctx)
{
  ip_context_t *dev = (ip_context_t *)ctx;
  oc_endpoint_t endpoint;
  memset(&endpoint, 0, sizeof(endpoint));
  WSANETWORKEVENTS network_events;
  if (s == dev->tcp.server_sock) {
    if (WSAEnumNetworkEvents(dev->tcp.server_sock, dev->tcp.server_event,
              &network_events) == SOCKET_ERROR) {
      OC_ERR("Error while accepting TCP socket, enumerate network event %d!", WSAGetLastError());
      return;
    }

    if (!(network_events.lNetworkEvents & (FD_ACCEPT | FD_CLOSE))) {
      return;
    }

    endpoint.flags = IPV6 | TCP | ACCEPTED;
    if (accept_new_session(dev, dev->tcp.server_sock, &endpoint) == SOCKET_ERROR) {
      OC_ERR("Failed to accept new IPv6 TCP session!");
    }
    return;
#ifdef KNX_TCP_TLS
  } else if (s == dev->tcp.secure_sock) {
    if (WSAEnumNetworkEvents(dev->tcp.secure_sock, dev->tcp.secure_event,
                             &network_events) == SOCKET_ERROR) {
      OC_ERR("Error while accepting secure TCP socket, enumerate network event %d!", WSAGetLastError());
      return;
    }
    if (!(network_events.lNetworkEvents & (FD_ACCEPT | FD_CLOSE))) {
      return;
    }
    endpoint.flags = IPV6 | SECURED | TCP | ACCEPTED;
    if (accept_new_session(dev, dev->tcp.secure_sock, &endpoint) ==
        SOCKET_ERROR) {
      OC_ERR("Failed to accept new secure IPv6 TCP secure session!");
    }
    return;
#endif
  }
  OC_ERR("Invalid TCP socket %ld!", (long)s);
  return;
}

static void process_signal(SOCKET s, void *ctx)
{
  (void)ctx;
  (void)s;
  OC_DBG("Process TCP session signal.");
  tcp_session_t *session = NULL;
  do {
    oc_tcp_adapter_mutex_lock();
    session = (tcp_session_t *)oc_list_pop(free_session_list_async);
    oc_tcp_adapter_mutex_unlock();
    if (session != NULL) {
      free_tcp_session(session);
    }
  } while (session != NULL);
}

typedef void (*socket_handler_t)(SOCKET, void *);

typedef struct sockets_handler_t
{
  HANDLE handlers[MAXIMUM_WAIT_OBJECTS];
  socket_handler_t cbks[MAXIMUM_WAIT_OBJECTS];
  void *ctxs[MAXIMUM_WAIT_OBJECTS];
  SOCKET sockets[MAXIMUM_WAIT_OBJECTS];
} sockets_handler_t;

static DWORD fill_sockets_handlers(ip_context_t *dev, sockets_handler_t *s)
{
  DWORD n = 0;
  s->handlers[n] = dev->tcp.signal_event;
  s->cbks[n] = process_signal;
  s->ctxs[n] = dev;
  s->sockets[n] = INVALID_SOCKET;
  n++;
  s->handlers[n] = dev->tcp.server_event;
  s->cbks[n] = accept_socket;
  s->ctxs[n] = dev;
  s->sockets[n] = dev->tcp.server_sock;
  n++;
#ifdef KNX_TCP_TLS
  s->handlers[n] = dev->tcp.secure_event;
  s->cbks[n] = accept_socket;
  s->ctxs[n] = dev;
  s->sockets[n] = dev->tcp.secure_sock;
  n++;
#endif
  oc_tcp_adapter_mutex_lock();
  tcp_session_t *session = (tcp_session_t *)oc_list_head(session_list);
  while (session != NULL && n < MAXIMUM_WAIT_OBJECTS) {
    s->handlers[n] = session->sock_event;
    s->cbks[n] = recv_message;
    s->ctxs[n] = session;
    s->sockets[n] = session->sock;
    n++;
    session = session->next;
  }

  oc_tcp_adapter_mutex_unlock();
  return n;
}

static void * network_event_thread(void *data)
{
  ip_context_t *dev = (ip_context_t *)data;
  sockets_handler_t socks;
  while (!dev->terminate) {
    DWORD size = fill_sockets_handlers(dev, &socks);
    DWORD idx = WaitForMultipleObjects(size, socks.handlers, FALSE, INFINITE);
    if (idx == WAIT_FAILED) {
      OC_ERR("Wait for multiple network objects failed (%d)!", GetLastError());
    } else if (idx - WAIT_OBJECT_0 >= 0 && idx - WAIT_OBJECT_0 < size) {
      idx = idx - WAIT_OBJECT_0;
      socks.cbks[idx](socks.sockets[idx], socks.ctxs[idx]);
    } else {
      oc_abort("Fatal error in network event thread!");
    }
  }

  return NULL;
}

int oc_tcp_connectivity_init(ip_context_t *dev)
{
  OC_DBG("Initializing TCP adapter for device %zd", dev->device);

  dev->tcp.signal_event = CreateEvent(NULL,  // default security attributes
                                      FALSE, // manual-reset event
                                      FALSE, // initial state is nonsignaled
                                      NULL);

  memset(&dev->tcp.server, 0, sizeof(struct sockaddr_storage));
  struct sockaddr_in6 *l = (struct sockaddr_in6 *)&dev->tcp.server;
  l->sin6_family = AF_INET6;
  l->sin6_addr = in6addr_any;
  l->sin6_port = 0;

  dev->tcp.server_sock = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
  if (dev->tcp.server_sock == SOCKET_ERROR) {
    OC_ERR("Unable to create IPv6 TCP server socket (%d)!", WSAGetLastError());
    return -1;
  }

  if (configure_tcp_socket(dev->tcp.server_sock, &dev->tcp.server) < 0) {
    OC_ERR("Unable to set socket option for TCP server socket!");
    return -1;
  }

  if (get_assigned_tcp_port(dev->tcp.server_sock, &dev->tcp.server) < 0) {
    OC_ERR("Unable to get port for TCP server socket!");
    return -1;
  }

  dev->tcp.port = ntohs(((struct sockaddr_in *)&dev->tcp.server)->sin_port);
  dev->tcp.server_event = WSACreateEvent();
  if (WSAEventSelect(dev->tcp.server_sock, dev->tcp.server_event,
            FD_READ | FD_ACCEPT) == SOCKET_ERROR) {
    OC_ERR("Error creating TCP server socket event (%d)!", WSAGetLastError());
    WSACloseEvent(dev->tcp.server_event);
    closesocket(dev->tcp.server_sock);
    return -1;
  }

#ifdef KNX_TCP_TLS
  memset(&dev->tcp.secure, 0, sizeof(struct sockaddr_storage));
  struct sockaddr_in6 *sm = (struct sockaddr_in6 *)&dev->tcp.secure;
  sm->sin6_family = AF_INET6;
  sm->sin6_addr = in6addr_any;
  sm->sin6_port = 0;

  dev->tcp.secure_sock = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
  if (dev->tcp.secure_sock == SOCKET_ERROR) {
    OC_ERR("Unable to create IPv6 TCP secure socket (%d)!", WSAGetLastError());
    WSACloseEvent(dev->tcp.server_event);
    closesocket(dev->tcp.server_sock);
    return -1;
  }

  if (configure_tcp_socket(dev->tcp.secure_sock, &dev->tcp.secure) < 0) {
    OC_ERR("Unable to set socket option for TCP secure socket!");
    closesocket(dev->tcp.secure_sock);
    WSACloseEvent(dev->tcp.server_event);
    closesocket(dev->tcp.server_sock);
    return -1;
  }

  if (get_assigned_tcp_port(dev->tcp.secure_sock, &dev->tcp.secure) < 0) {
    OC_ERR("Unable to get port for TCP server socket!");
    closesocket(dev->tcp.secure_sock);
    WSACloseEvent(dev->tcp.server_event);
    closesocket(dev->tcp.server_sock);
    return -1;
  }
  dev->tcp.tls_port = ntohs(((struct sockaddr_in *)&dev->tcp.secure)->sin_port);
  dev->tcp.secure_event = WSACreateEvent();
  if (WSAEventSelect(dev->tcp.secure_sock, dev->tcp.secure_event,
                     FD_READ | FD_ACCEPT) == SOCKET_ERROR) {
    OC_ERR("Error creating TCP secure socket event (%d)!", WSAGetLastError());
    WSACloseEvent(dev->tcp.secure_event);
    closesocket(dev->tcp.secure_sock);
    WSACloseEvent(dev->tcp.server_event);
    closesocket(dev->tcp.server_sock);
    return -1;
  }
#endif /* KNX_TCP_TLS */

  dev->tcp.event_thread_handle =
    CreateThread(0, 0, (LPTHREAD_START_ROUTINE)network_event_thread, dev, 0,
                 &dev->tcp.event_thread);
  if (dev->tcp.event_thread_handle == NULL) {
    OC_ERR("Unable to creat TCP network polling thread (%d)!", GetLastError());
    WSACloseEvent(dev->tcp.server_event);
    closesocket(dev->tcp.server_sock);
#ifdef KNX_TCP_TLS
    WSACloseEvent(dev->tcp.secure_event);
    closesocket(dev->tcp.secure_sock);
#endif
    return -1;
  }

  OC_INF("IPv6 TCP port: %u", dev->tcp.port);
#ifdef KNX_TCP_TLS
  OC_INF("IPv6 TCP secure port: %u", dev->tcp.tls_port);
#endif
  OC_INF("Successfully initialized TCP adapter for device %zd", dev->device);
  return 0;
}

void oc_tcp_connectivity_shutdown(ip_context_t *dev)
{
  if (!SetEvent(dev->tcp.signal_event)) {
    OC_ERR("Could not trigger signal event (%d)!", GetLastError());
  }
  WaitForSingleObject(dev->tcp.event_thread_handle, INFINITE);
  TerminateThread(dev->tcp.event_thread_handle, 0);

  process_signal(INVALID_SOCKET, dev);

  WSACloseEvent(dev->tcp.server_event);
  closesocket(dev->tcp.server_sock);

#ifdef KNX_TCP_TLS
  WSACloseEvent(dev->tcp.secure_event);
  closesocket(dev->tcp.secure_sock);
#endif

  oc_tcp_adapter_mutex_lock();
  tcp_session_t *session = (tcp_session_t *)oc_list_head(session_list), *next;
  while (session != NULL) {
    next = session->next;

    oc_endpoint_t endpoint;
    SOCKET sock;
    HANDLE sock_event;
    free_tcp_session_locked(session, &endpoint, &sock, &sock_event);
    OC_DBG("Freed TCP TCP session.");

    WSACloseEvent(sock_event);
    closesocket(sock);
    if (!oc_session_events_is_ongoing()) {
      oc_session_end_event(&endpoint);
    }

    session = next;
  }

  oc_tcp_adapter_mutex_unlock();
  CloseHandle(dev->tcp.signal_event);

  OC_DBG("oc_tcp_connectivity_shutdown for device %zd", dev->device);	// TODO wording
}

tcp_csm_state_t
oc_tcp_get_csm_state(oc_endpoint_t *endpoint)
{
  if (!endpoint) {
    return CSM_ERROR;
  }

  oc_tcp_adapter_mutex_lock();
  tcp_session_t *session = find_session_by_endpoint_locked(endpoint);
  if (!session) {
    oc_tcp_adapter_mutex_unlock();
    return CSM_NONE;
  }
  tcp_csm_state_t state = session->csm_state;
  oc_tcp_adapter_mutex_unlock();

  return state;
}

int oc_tcp_update_csm_state(oc_endpoint_t *endpoint, tcp_csm_state_t csm)
{
  if (!endpoint) {
    return -1;
  }

  oc_tcp_adapter_mutex_lock();
  tcp_session_t *session = find_session_by_endpoint_locked(endpoint);
  if (!session) {
    oc_tcp_adapter_mutex_unlock();
    return -1;
  }

  session->csm_state = csm;
  oc_tcp_adapter_mutex_unlock();

  return 0;
}

#endif /* OC_TCP */
