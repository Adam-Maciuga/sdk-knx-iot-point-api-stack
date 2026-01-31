/* 
 * Copyright (c) 2018 Samsung Electronics
 * Copyright (c) 2021 Cascoda Ltd
 * Copyright (c) 2024-2026 KNX Association
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define __USE_GNU

#include "tcpadapter.h"
#include "api/oc_session_events_internal.h"
#include "ipadapter.h"
#include "ipcontext.h"
#include "messaging/coap/coap.h"
#include "oc_endpoint.h"
#include "oc_session_events.h"
#include "port/oc_assert.h"
#include "util/oc_memb.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <stdlib.h>
#include <unistd.h>

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
  int sock;
  tcp_csm_state_t csm_state;
} tcp_session_t;

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
OC_LIST(session_list);
OC_LIST(free_session_list_async);
OC_MEMB(tcp_session_s, tcp_session_t, OC_MAX_TCP_PEERS);

static void signal_network_thread(ip_context_t *dev);

static int configure_tcp_socket(int sock, struct sockaddr_storage *sock_info)
{
  if (bind(sock, (struct sockaddr *)sock_info, sizeof(*sock_info)) == -1) {
    OC_ERR("Unable to bind socket (%d)!", errno);
    return -1;
  }

  if (listen(sock, OC_TCP_LISTEN_BACKLOG) == -1) {
    OC_ERR("Error listening to socket (%d)!", errno);
    return -1;
  }

  return 0;
}

static int get_assigned_tcp_port(int sock, struct sockaddr_storage *sock_info)
{

  socklen_t socklen = sizeof(*sock_info);
  if (getsockname(sock, (struct sockaddr *)sock_info, &socklen) == -1) {
    OC_ERR("Unable to obtain socket information (%d)!", errno);
    return -1;
  }

  return 0;
}

static int get_interface_index(int sock)
{
  int interface_index = -1;

  struct sockaddr_storage addr;
  socklen_t socklen = sizeof(addr);
  if (getsockname(sock, (struct sockaddr *)&addr, &socklen) == -1) {
    OC_ERR("Unable to obtain socket information (%d)!", errno);
    return -1;
  }

  struct ifaddrs *ifs = NULL, *interface = NULL;
  if (getifaddrs(&ifs) < 0) {
    OC_ERR("Unable to query interfaces (%d)!", errno);
    return -1;
  }

  for (interface = ifs; interface != NULL; interface = interface->ifa_next) {
    if (!(interface->ifa_flags & IFF_UP) || interface->ifa_flags & IFF_LOOPBACK)
      continue;
    if (interface->ifa_addr &&
        addr.ss_family == interface->ifa_addr->sa_family) {
      if (addr.ss_family == AF_INET6) {
        struct sockaddr_in6 *a = (struct sockaddr_in6 *)interface->ifa_addr;
        struct sockaddr_in6 *b = (struct sockaddr_in6 *)&addr;
        if (memcmp(a->sin6_addr.s6_addr, b->sin6_addr.s6_addr, 16) == 0) {
          interface_index = if_nametoindex(interface->ifa_name);
          break;
        }
      }
    }
  }

  freeifaddrs(ifs);
  return interface_index;
}

void oc_tcp_add_socks_to_fd_set(ip_context_t *dev)
{
  FD_SET(dev->tcp.server_sock, &dev->rfds);
#ifdef KNX_TCP_TLS
  FD_SET(dev->tcp.secure_sock, &dev->rfds);
#endif

  FD_SET(dev->tcp.connect_pipe[0], &dev->rfds);
}

static void free_tcp_session_async_locked(tcp_session_t *session)
{
  oc_list_remove(session_list, session);
  oc_list_add(free_session_list_async, session);

  signal_network_thread(session->dev);
  OC_DBG("Signaled network event thread to monitor that the session need to be removed.");
  OC_DBG("Free TCP session (async, locked).");
}

static void free_tcp_session(tcp_session_t *session)
{
  oc_list_remove(session_list, session);
  oc_list_remove(free_session_list_async, session);

  if (!oc_session_events_is_ongoing()) {
    oc_session_end_event(&session->endpoint);
  }

  ip_context_rfds_fd_clr(session->dev, session->sock);

  ssize_t len = 0;
  do {
    uint8_t dummy_value = 0xef;
    len = write(session->dev->tcp.connect_pipe[1], &dummy_value, 1);
  } while (len == -1 && errno == EINTR);

  close(session->sock);

  oc_memb_free(&tcp_session_s, session);

  OC_DBG("Freed TCP session.");
}

static void process_free_tcp_session_locked()
{
  while (true) {
    tcp_session_t *session =
      (tcp_session_t *)oc_list_pop(free_session_list_async);
    if (session == NULL)
      return;
    free_tcp_session(session);
  }
}

static int add_new_session(int sock, ip_context_t *dev, oc_endpoint_t *endpoint,
        tcp_csm_state_t state)
{
  tcp_session_t *session = oc_memb_alloc(&tcp_session_s);
  if (!session) {
    OC_ERR("Could not allocate new TCP session object!");
    return -1;
  }

  endpoint->interface_index = get_interface_index(sock);

  session->dev = dev;
  memcpy(&session->endpoint, endpoint, sizeof(oc_endpoint_t));
  session->endpoint.next = NULL;
  session->sock = sock;
  session->csm_state = state;

  oc_list_add(session_list, session);

  if (!(endpoint->flags & SECURED)) {
    oc_session_start_event((oc_endpoint_t *)endpoint);
  }

  OC_DBG("Recorded new TCP session.");

  return 0;
}

static int accept_new_session(ip_context_t *dev, int fd, fd_set *setfds,
        oc_endpoint_t *endpoint)
{
  struct sockaddr_storage receive_from;
  socklen_t receive_len = sizeof(receive_from);

  int new_socket = accept(fd, (struct sockaddr *)&receive_from, &receive_len);
  if (new_socket < 0) {
    OC_ERR("Failed to accept incoming TCP connection!");
    return -1;
  }
  OC_INF("Accepted incomming TCP connection.");

  if (endpoint->flags & IPV6) {
    struct sockaddr_in6 *r = (struct sockaddr_in6 *)&receive_from;
    memcpy(endpoint->addr.ipv6.address, r->sin6_addr.s6_addr,
           sizeof(r->sin6_addr.s6_addr));
    endpoint->addr.ipv6.scope = r->sin6_scope_id;
    endpoint->addr.ipv6.port = ntohs(r->sin6_port);
  }

  FD_CLR(fd, setfds);

  if (add_new_session(new_socket, dev, endpoint, CSM_NONE) < 0) {
    OC_ERR("Could not record new TCP session!");
    close(new_socket);
    return -1;
  }

  ip_context_rfds_fd_set(dev, new_socket);

  return 0;
}

static tcp_session_t * find_session_by_endpoint(oc_endpoint_t *endpoint)
{
  tcp_session_t *session = oc_list_head(session_list);
  while (session != NULL &&
         oc_endpoint_compare(&session->endpoint, endpoint) != 0) {
    session = session->next;
  }

  if (!session) {
#ifdef OC_DEBUG
    OC_DBG("Could not find ongoing TCP session for endpoint:");
    PRINTipaddr(*endpoint);
#endif
    return NULL;
  }
#ifdef OC_DEBUG
  OC_DBG("Found TCP session for endpoint:");
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

adapter_receive_state_t oc_tcp_receive_message(ip_context_t *dev, fd_set *fds, oc_message_t *message)
{
  pthread_mutex_lock(&mutex);
  process_free_tcp_session_locked();
#define ret_with_code(status)                                                  \
  ret = status;                                                                \
  goto oc_tcp_receive_message_done

  adapter_receive_state_t ret = ADAPTER_STATUS_ERROR;

  if (FD_ISSET(dev->tcp.server_sock, fds)) {
    message->endpoint.flags = IPV6 | TCP | ACCEPTED;
    if (accept_new_session(dev, dev->tcp.server_sock, fds, &message->endpoint) <
        0) {
      OC_ERR("Failed to accept new IPv6 TCP session!");
      ret_with_code(ADAPTER_STATUS_ERROR);
    }
    ret_with_code(ADAPTER_STATUS_ACCEPT);
#ifdef KNX_TCP_TLS
  } else if (FD_ISSET(dev->tcp.secure_sock, fds)) {
    message->endpoint.flags = IPV6 | SECURED | TCP | ACCEPTED;
    if (accept_new_session(dev, dev->tcp.secure_sock, fds, &message->endpoint) <
        0) {
      OC_ERR("Failed to accept new secure IPv6 TCP session!");
      ret_with_code(ADAPTER_STATUS_ERROR);
    }
    ret_with_code(ADAPTER_STATUS_ACCEPT);
#endif
  } else if (FD_ISSET(dev->tcp.connect_pipe[0], fds)) {
    ssize_t len = read(dev->tcp.connect_pipe[0], message->data, OC_PDU_SIZE);
    if (len < 0) {
      OC_ERR("Error while reading from TCP pipe (%d)!", errno);
      ret_with_code(ADAPTER_STATUS_ERROR);
    }
    FD_CLR(dev->tcp.connect_pipe[0], fds);
    ret_with_code(ADAPTER_STATUS_NONE);
  }

  // Find session.
  tcp_session_t *session = get_ready_to_read_session(fds);
  if (!session) {
    OC_DBG("Could not find TCP session socket in fd set.");
    ret_with_code(ADAPTER_STATUS_NONE);
  }

  // Receive message.
  size_t total_length = 0;
  size_t want_read = DEFAULT_RECEIVE_SIZE;
  message->length = 0;
  do {
    int count =
      recv(session->sock, message->data + message->length, want_read, 0);
    if (count < 0) {
      OC_ERR("Error while receiving message over TCP (%d)!", errno);
      free_tcp_session(session);
      ret_with_code(ADAPTER_STATUS_ERROR);
    } else if (count == 0) {
      OC_DBG("Peer closed TCP session.");
      free_tcp_session(session);
      ret_with_code(ADAPTER_STATUS_NONE);
    }

    OC_DBG("recv(): %d bytes.", count);
    message->length += (size_t)count;
    want_read -= (size_t)count;

    if (total_length == 0) {
      total_length = get_total_length_from_header(message, &session->endpoint);
      if (total_length > (size_t)(OC_MAX_APP_DATA_SIZE + COAP_MAX_HEADER_SIZE)) {
        OC_ERR("Total receive length (%zu) is bigger than max pdu size (%zu)!",
               total_length,
               (OC_MAX_APP_DATA_SIZE + COAP_MAX_HEADER_SIZE));
        OC_ERR("A buffer overflow may occur!");
        ret_with_code(ADAPTER_STATUS_ERROR);
      }
      OC_DBG("TCP packet total length: %zu bytes.", total_length);

      want_read = total_length - (size_t)count;
    }
  } while (total_length > message->length);

  memcpy(&message->endpoint, &session->endpoint, sizeof(oc_endpoint_t));

  if (message->endpoint.flags & SECURED) {
    message->encrypted = 1;
  }

  FD_CLR(session->sock, fds);
  ret = ADAPTER_STATUS_RECEIVE;

oc_tcp_receive_message_done:
  pthread_mutex_unlock(&mutex);
#undef ret_with_code
  return ret;
}

void oc_tcp_end_session(ip_context_t *dev, oc_endpoint_t *endpoint)
{
  (void)dev;
  pthread_mutex_lock(&mutex);
  tcp_session_t *session = find_session_by_endpoint(endpoint);
  if (session) {
    free_tcp_session_async_locked(session);
  }
  pthread_mutex_unlock(&mutex);
}

static int get_session_socket(oc_endpoint_t *endpoint)
{
  int sock = -1;
  tcp_session_t *session = find_session_by_endpoint(endpoint);
  if (!session) {
    return -1;
  }

  sock = session->sock;
  return sock;
}

static int connect_nonb(int sockfd, const struct sockaddr *r, int r_len, int nsec)
{
  int flags, n, error;
  socklen_t len;
  fd_set wset;
  struct timeval tval;

  flags = fcntl(sockfd, F_GETFL, 0);
  if (flags < 0) {
    return -1;
  }

  error = fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
  if (error < 0) {
    return -1;
  }

  error = 0;
  if ((n = connect(sockfd, (struct sockaddr *)r, r_len)) < 0) {
    if (errno != EINPROGRESS)
      return -1;
  }

  // Do whatever we want while the connect is taking place.
  if (n == 0) {
    // Connect completed immediately.
    goto done;
  }

  FD_ZERO(&wset);
  FD_SET(sockfd, &wset);
  tval.tv_sec = nsec;
  tval.tv_usec = 0;

  if ((n = select(sockfd + 1, NULL, &wset, NULL, nsec ? &tval : NULL)) == 0) {
    // timeout
    errno = ETIMEDOUT;
    return -1;
  }

  if (FD_ISSET(sockfd, &wset)) {
    len = sizeof(error);
    if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) < 0)
      // Solaris pending error
      return -1;
  } else {
    OC_DBG("Select error: sockfd not set");
    return -1;
  }

done:
  if (error < 0) {
    close(sockfd); // just in case
    errno = error;
    return -1;
  } else {
    // Restore file status flags.
    error = fcntl(sockfd, F_SETFL, flags);
    if (error < 0) {
      return -1;
    }
  }
  return 0;
}

static void signal_network_thread(ip_context_t *dev)
{
  ssize_t len = 0;
  do {
    uint8_t dummy_value = 0xef;
    len = write(dev->tcp.connect_pipe[1], &dummy_value, 1);
  } while (len == -1 && errno == EINTR);
}

static int initiate_new_session(ip_context_t *dev, oc_endpoint_t *endpoint,
        const struct sockaddr_storage *receiver)
{
  int sock = -1;
  uint8_t retry_cnt = 0;

  while (retry_cnt < LIMIT_RETRY_CONNECT) {
    if (endpoint->flags & IPV6) {
      sock = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    }

    if (sock < 0) {
      OC_ERR("Could not create socket for new TCP session!");
      return -1;
    }

    socklen_t receiver_size = sizeof(*receiver);
    int ret = 0;
    if ((ret = connect_nonb(sock, (struct sockaddr *)receiver, receiver_size,
                            TCP_CONNECT_TIMEOUT)) == 0) {
      break;
    }

    close(sock);
    retry_cnt++;
    OC_DBG("Connect failed with %d. retry (%d)", ret, retry_cnt);
  }

  if (retry_cnt >= LIMIT_RETRY_CONNECT) {
    OC_ERR("Could not initiate TCP connection (retry limit reached)!");
    return -1;
  }

  OC_DBG("Successfully initiated TCP connection.");

  if (add_new_session(sock, dev, endpoint, CSM_SENT) < 0) {
    OC_ERR("Could not record new TCP session!");
    close(sock);
    return -1;
  }

  ip_context_rfds_fd_set(dev, sock);

  signal_network_thread(dev);
  OC_DBG("Signaled network event thread to monitor the newly added session.");

  return sock;
}

int oc_tcp_send_buffer(ip_context_t *dev, oc_message_t *message,
        const struct sockaddr_storage *receiver)
{
  pthread_mutex_lock(&mutex);
  int send_sock = get_session_socket(&message->endpoint);

  size_t bytes_sent = 0;
  if (send_sock < 0) {
    if (message->endpoint.flags & ACCEPTED) {
      OC_ERR("Unable to send message, connection was closed!");
      goto oc_tcp_send_buffer_done;
    }
    if ((send_sock = initiate_new_session(dev, &message->endpoint, receiver)) <
        0) {
      OC_ERR("Unable to send message, could not initiate new TCP session!");
      goto oc_tcp_send_buffer_done;
    }
  }

  send_sock = get_session_socket(&message->endpoint);
  if (send_sock < 0) {
    goto oc_tcp_send_buffer_done;
  }

  do {
    ssize_t send_len = send(send_sock, message->data + bytes_sent,
                            message->length - bytes_sent, MSG_NOSIGNAL);
    if (send_len < 0) {
      OC_WRN("TCP send message returned errno %d"!, errno);
      goto oc_tcp_send_buffer_done;
    }
    bytes_sent += send_len;
  } while (bytes_sent < message->length);

  OC_DBG("Sent %zd bytes.", bytes_sent);
oc_tcp_send_buffer_done:
  pthread_mutex_unlock(&mutex);

  if (bytes_sent == 0) {
    return -1;
  }

  return bytes_sent;
}

int oc_tcp_connectivity_init(ip_context_t *dev)
{
  OC_DBG("Initializing TCP adapter for device %zd", dev->device);

  memset(&dev->tcp.server, 0, sizeof(struct sockaddr_storage));
  struct sockaddr_in6 *l = (struct sockaddr_in6 *)&dev->tcp.server;
  l->sin6_family = AF_INET6;
  l->sin6_addr = in6addr_any;
  l->sin6_port = 0;

#ifdef KNX_TCP_TLS
  memset(&dev->tcp.secure, 0, sizeof(struct sockaddr_storage));
  struct sockaddr_in6 *sm = (struct sockaddr_in6 *)&dev->tcp.secure;
  sm->sin6_family = AF_INET6;
  sm->sin6_addr = in6addr_any;
  sm->sin6_port = 0;
#endif

  dev->tcp.server_sock = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);

  if (dev->tcp.server_sock < 0) {
    OC_ERR("Unable to create IPv6 TCP server socket!");
    return -1;
  }

#ifdef KNX_TCP_TLS
  dev->tcp.secure_sock = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
  if (dev->tcp.secure_sock < 0) {
    OC_ERR("Unable to create TCP secure socket!");
    return -1;
  }
#endif

  if (configure_tcp_socket(dev->tcp.server_sock, &dev->tcp.server) < 0) {
    OC_ERR("Unabe to set socket option for TCP server socket!");
    return -1;
  }

  if (get_assigned_tcp_port(dev->tcp.server_sock, &dev->tcp.server) < 0) {
    OC_ERR("Unable to get port for TCP server socket!");
    return -1;
  }
  dev->tcp.port = ntohs(((struct sockaddr_in *)&dev->tcp.server)->sin_port);

#ifdef KNX_TCP_TLS
  if (configure_tcp_socket(dev->tcp.secure_sock, &dev->tcp.secure) < 0) {
    OC_ERR("Unable to set socket option for TCP secure socket!");
    return -1;
  }

  if (get_assigned_tcp_port(dev->tcp.secure_sock, &dev->tcp.secure) < 0) {
    OC_ERR("Unable to get port for TCP secure socket!");
    return -1;
  }
  dev->tcp.tls_port = ntohs(((struct sockaddr_in *)&dev->tcp.secure)->sin_port);
#endif

  if (pipe(dev->tcp.connect_pipe) < 0) {
    OC_ERR("Unable to initialize TCP connection pipe!");
    return -1;
  }
  if (set_nonblock_socket(dev->tcp.connect_pipe[0]) < 0) {
    OC_ERR("Unable to set non-block connect_pipe[0]!");
    return -1;
  }

  OC_INF("IPv6 TCP port: %u", dev->tcp.port);
#ifdef KNX_TCP_TLS
  OC_INF("IPv6 TCP secure port: %u", dev->tcp.tls_port);
#endif
  OC_INF("Successfully initialized TCP adapter.");
  return 0;
}

void oc_tcp_connectivity_shutdown(ip_context_t *dev)
{
  close(dev->tcp.server_sock);

#ifdef KNX_TCP_TLS
  close(dev->tcp.secure_sock);
#endif

  close(dev->tcp.connect_pipe[0]);
  close(dev->tcp.connect_pipe[1]);

  pthread_mutex_lock(&mutex);
  tcp_session_t *session = (tcp_session_t *)oc_list_head(session_list), *next;
  while (session != NULL) {
    next = session->next;
    free_tcp_session(session);
    OC_DBG("Freed TCP TCP session.");
    session = next;
  }
  process_free_tcp_session_locked();
  pthread_mutex_unlock(&mutex);

  OC_DBG("oc_tcp_connectivity_shutdown");	// TODO wording
}

tcp_csm_state_t oc_tcp_get_csm_state(oc_endpoint_t *endpoint)
{
  if (!endpoint) {
    return CSM_ERROR;
  }

  tcp_session_t *session = find_session_by_endpoint(endpoint);
  if (!session) {
    return CSM_NONE;
  }

  return session->csm_state;
}

int oc_tcp_update_csm_state(oc_endpoint_t *endpoint, tcp_csm_state_t csm)
{
  if (!endpoint) {
    return -1;
  }

  tcp_session_t *session = find_session_by_endpoint(endpoint);
  if (!session) {
    return -1;
  }

  session->csm_state = csm;
  return 0;
}

#endif /* OC_TCP */
