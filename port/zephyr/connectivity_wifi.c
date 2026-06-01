/*
 * Copyright (c) 2026 KNX Association
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * KNX-IoT connectivity backend for Wi-Fi using Zephyr BSD socket API.
 *
 * Implements the oc_connectivity_*  and  oc_network_event_handler_mutex_*
 * interfaces declared in port/oc_connectivity.h.  The design mirrors
 * connectivity_thread.c but uses POSIX-compatible Zephyr sockets instead of
 * the OpenThread-specific otUdp API.
 *
 * Two sockets are used to distinguish unicast from multicast traffic, mirroring
 * the Linux ipadapter design:
 *   server_sock — unicast only; no multicast groups joined; used for all sends.
 *   mcast_sock  — multicast only; all group subscriptions are made here.
 * A receive thread polls both sockets and sets the MULTICAST endpoint flag for
 * packets arriving on mcast_sock.  This flag is required for correct S-mode
 * (group communication) Echo handling in the KNX-IoT CoAP/OSCORE engine.
 */

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include "oc_buffer.h"
#include "oc_endpoint.h"
#include "port/oc_connectivity.h"
#include "port/oc_log.h"

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/wifi_mgmt.h>

/* CoAP unencrypted port (RFC 7252) */
#define COAP_PORT_UNSECURED  5683

/* All CoAP nodes multicast addresses (RFC 7252 §12.8), three scopes.
 * Joined unconditionally so CoAP discovery requests reach this socket.
 * Mirrors the Linux ipadapter add_mcast_sock_to_ipv6_mcast_group(). */
static const uint8_t ALL_COAP_NODES_LL[] = { 0xff, 0x02, 0, 0, 0, 0, 0, 0,
                                              0,    0,    0, 0, 0, 0, 0, 0xFD };
static const uint8_t ALL_COAP_NODES_RL[] = { 0xff, 0x03, 0, 0, 0, 0, 0, 0,
                                              0,    0,    0, 0, 0, 0, 0, 0xFD };
static const uint8_t ALL_COAP_NODES_SL[] = { 0xff, 0x05, 0, 0, 0, 0, 0, 0,
                                              0,    0,    0, 0, 0, 0, 0, 0xFD };

/* Receive thread parameters */
#define RX_THREAD_STACK_SIZE 2048
#define RX_THREAD_PRIORITY      7   /* lower number = higher priority */

K_THREAD_STACK_DEFINE(rx_thread_stack, RX_THREAD_STACK_SIZE);
static struct k_thread rx_thread_data;
K_MUTEX_DEFINE(network_mutex);

/* server_sock: unicast traffic only; also used for all outgoing sends.
 * mcast_sock:  multicast group subscriptions; receive-only. */
static int server_sock = -1;
static int mcast_sock  = -1;
static k_tid_t rx_tid  = NULL;

/* ── Helpers ────────────────────────────────────────────────────────────────── */

/* Receive one datagram from fd using recvmsg() so that the IPV6_PKTINFO
 * ancillary message is available.  Populates source address, interface index,
 * and (for unicast sockets) the destination address into addr_local.
 * is_mcast must be true when fd is mcast_sock so that addr_local is cleared
 * instead (mirrors the Linux ipadapter recv_msg() behaviour). */
static bool recv_one(int fd, oc_message_t *message, bool is_mcast)
{
    struct sockaddr_in6 from = {0};
    uint8_t ctrl[CMSG_SPACE(sizeof(struct in6_pktinfo))];
    struct iovec iov = {
        .iov_base = message->data,
        .iov_len  = OC_PDU_SIZE,
    };
    struct msghdr mhdr = {
        .msg_name       = &from,
        .msg_namelen    = sizeof(from),
        .msg_iov        = &iov,
        .msg_iovlen     = 1,
        .msg_control    = ctrl,
        .msg_controllen = sizeof(ctrl),
    };

    ssize_t len = zsock_recvmsg(fd, &mhdr, 0);
    if (len < 0) {
        return false;
    }

    message->length                  = (size_t)len;
    message->endpoint.addr.ipv6.port = ntohs(from.sin6_port);
    memcpy(message->endpoint.addr.ipv6.address, from.sin6_addr.s6_addr, 16);

    /* Parse IPV6_PKTINFO ancillary data for interface index and
     * destination address (required for correct OSCORE response routing). */
    for (struct cmsghdr *cm = CMSG_FIRSTHDR(&mhdr); cm != NULL;
         cm = CMSG_NXTHDR(&mhdr, cm)) {
        if (cm->cmsg_level == IPPROTO_IPV6 && cm->cmsg_type == IPV6_PKTINFO) {
            struct in6_pktinfo *pi =
                (struct in6_pktinfo *)CMSG_DATA(cm);
            message->endpoint.interface_index = (int)pi->ipi6_ifindex;
            if (!is_mcast) {
                /* Unicast socket: record destination address so the stack
                 * can use it as the source address in replies. */
                memcpy(message->endpoint.addr_local.ipv6.address,
                       pi->ipi6_addr.s6_addr, 16);
            } else {
                /* Multicast socket: addr_local is not meaningful here;
                 * clear it to avoid stale data (matches Linux ipadapter). */
                memset(message->endpoint.addr_local.ipv6.address, 0, 16);
            }
            break;
        }
    }

    return true;
}

/* ── Receive thread ────────────────────────────────────────────────────────── */

static void rx_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    OC_DBG("CoAP RX thread started, polling server_sock=%d mcast_sock=%d.",
           server_sock, mcast_sock);

    while (true) {
        struct zsock_pollfd fds[2] = {
            { .fd = server_sock, .events = ZSOCK_POLLIN },
            { .fd = mcast_sock,  .events = ZSOCK_POLLIN },
        };

        int r = zsock_poll(fds, 2, -1);
        if (r < 0) {
            if (server_sock < 0 && mcast_sock < 0) {
                /* Both sockets closed — shutdown requested, exit thread. */
                break;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                OC_ERR("Failed to wait for incoming CoAP datagrams: %d", errno);
            }
            continue;
        }

        for (int i = 0; i < 2; i++) {
            if (!(fds[i].revents & ZSOCK_POLLIN)) {
                continue;
            }

            oc_message_t *message = oc_allocate_message();
            if (!message) {
                OC_ERR("Failed to allocate message buffer for incoming CoAP datagram!");
                /* Drain the socket to avoid getting stuck. */
                uint8_t drain[1];
                zsock_recv(fds[i].fd, drain, sizeof(drain), 0);
                continue;
            }

            /* i == 0: server_sock → unicast; i == 1: mcast_sock → multicast */
            bool is_mcast = (i == 1);
            message->endpoint.flags = IPV6 | (is_mcast ? MULTICAST : 0);

            if (!recv_one(fds[i].fd, message, is_mcast)) {
                oc_message_unref(message);
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    OC_ERR("Failed to receive CoAP datagram: %d", errno);
                }
                continue;
            }

            OC_INF("Incoming CoAP message of size %d bytes from ", (int)message->length);
            PRINTipaddr(message->endpoint);
            PRINTF("\r\n");

            oc_network_event(message);
        }
    }
}

/* ── Socket helpers ─────────────────────────────────────────────────────────── */

/* Open a UDP socket and bind it to the given port on any IPv6 address.
 * Pass port=0 to let the OS assign an ephemeral port (used for server_sock).
 * Pass port=COAP_PORT_UNSECURED for mcast_sock; SO_REUSEADDR is set so that
 * both sockets can coexist without SO_REUSEPORT — which would cause the kernel
 * to load-balance packets between sockets and break the unicast/multicast split.
 * description is a human-readable label used in log messages, e.g. "unicast". */
static int open_and_bind_socket(uint16_t port, const char *description)
{
    struct sockaddr_in6 addr = {
        .sin6_family = AF_INET6,
        .sin6_port   = htons(port),
        .sin6_addr   = IN6ADDR_ANY_INIT,
    };
    int on = 1;
    int fd = zsock_socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        OC_ERR("Failed to open %s UDP socket for port %u: %d",
               description, (unsigned)port, errno);
        return -1;
    }

    /* Allow two sockets to share port 5683 without SO_REUSEPORT load-balancing. */
    if (zsock_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
        OC_ERR("Failed to enable address reuse (SO_REUSEADDR) on %s socket: %d",
               description, errno);
    }

    /* Deliver destination address and receive interface index via recvmsg()
     * ancillary data (IPV6_PKTINFO).  Required to populate
     * endpoint->interface_index and endpoint->addr_local correctly. */
    if (zsock_setsockopt(fd, IPPROTO_IPV6, IPV6_RECVPKTINFO, &on, sizeof(on)) < 0) {
        OC_ERR("Failed to enable receive packet info (IPV6_RECVPKTINFO) on %s socket: %d",
               description, errno);
    }

    /* Restrict to real IPv6; reject IPv4-mapped addresses (mirrors Linux ipadapter). */
    if (zsock_setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on)) < 0) {
        OC_ERR("Failed to restrict to IPv6 only (IPV6_V6ONLY) on %s socket: %d",
               description, errno);
    }

    /* Prefer stable public SLAAC address as source to prevent source address
     * flip between S-mode retransmissions, which breaks the Echo sync loop.
     * NOTE: IPV6_ADDR_PREFERENCES is defined as a macro in <zephyr/net/socket.h>
     * so this #ifdef always evaluates to true on Zephyr — the guard is kept only
     * for portability with the Linux ipadapter pattern. */
#ifdef IPV6_ADDR_PREFERENCES
    {
        int prefer = IPV6_PREFER_SRC_PUBLIC;
        if (zsock_setsockopt(fd, IPPROTO_IPV6, IPV6_ADDR_PREFERENCES,
                             &prefer, sizeof(prefer)) < 0) {
            OC_ERR("Failed to set source address preference (IPV6_ADDR_PREFERENCES) on %s socket: %d",
                   description, errno);
        }
    }
#endif

    if (zsock_bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        int bind_err = errno;  /* preserve errno before zsock_close() can overwrite it */
        OC_ERR("Failed to bind %s UDP socket to port %u: %d",
               description, (unsigned)port, bind_err);
        zsock_close(fd);
        errno = bind_err;
        return -1;
    }
    return fd;
}

/* ── Public interface ─────────────────────────────────────────────────────── */

int oc_connectivity_init(void)
{
    /* server_sock binds to port 0 so the OS assigns an ephemeral port.
     * This mirrors the Linux ipadapter: server_sock never competes with
     * mcast_sock for port 5683, so no SO_REUSEPORT is needed and the kernel
     * delivers port-5683 traffic exclusively to mcast_sock. */
    server_sock = open_and_bind_socket(0, "unicast");
    if (server_sock < 0) {
        OC_ERR("Failed to create unicast CoAP socket: %d", errno);
        return -1;
    }

    /* Discover and log the actual ephemeral port assigned to server_sock. */
    {
        struct sockaddr_in6 sa = {0};
        socklen_t sa_len = sizeof(sa);
        if (zsock_getsockname(server_sock, (struct sockaddr *)&sa, &sa_len) == 0) {
            OC_DBG("Unicast socket ready, fd=%d, ephemeral port=%u.",
                   server_sock, (unsigned)ntohs(sa.sin6_port));
        } else {
            OC_DBG("Unicast socket ready, fd=%d.", server_sock);
        }
    }

    mcast_sock = open_and_bind_socket(COAP_PORT_UNSECURED, "multicast");
    if (mcast_sock < 0) {
        OC_ERR("Failed to create multicast CoAP socket: %d", errno);
        zsock_close(server_sock);
        server_sock = -1;
        return -1;
    }
    OC_DBG("Multicast socket ready, fd=%d, port=%u.", mcast_sock, COAP_PORT_UNSECURED);

    /* Pin multicast sends to the WiFi STA interface.
     * net_if_get_default() may return the Ethernet interface on boards with
     * both Ethernet and WiFi (e.g. FRDM-RW612), so we explicitly look up the
     * first WiFi interface instead.  Fall back to default for Ethernet-only
     * builds where no WiFi interface exists. */
    struct net_if *wifi_iface = net_if_get_first_wifi();
    if (!wifi_iface) {
        wifi_iface = net_if_get_default();
    }
    int ifidx = wifi_iface ? net_if_get_by_iface(wifi_iface) : -1;
    if (ifidx > 0) {
        if (zsock_setsockopt(server_sock, IPPROTO_IPV6, IPV6_MULTICAST_IF,
                             &ifidx, sizeof(ifidx)) < 0) {
            OC_WRN("Failed to pin multicast sends to interface: %d", errno);
        } else {
            OC_DBG("Multicast sends pinned to interface ifidx=%d.", ifidx);
        }

        /* Subscribe to all-CoAP-nodes multicast groups on mcast_sock so
         * discovery requests reach the device regardless of GOT configuration.
         * Mirrors Linux ipadapter add_mcast_sock_to_ipv6_mcast_group(). */
        {
            static const uint8_t *coap_mcast[] = {
                ALL_COAP_NODES_LL, ALL_COAP_NODES_RL, ALL_COAP_NODES_SL
            };
            struct ipv6_mreq mreq_coap = { 0 };
            mreq_coap.ipv6mr_ifindex = (unsigned int)ifidx;
            int n_joined = 0;
            for (int i = 0; i < 3; i++) {
                memcpy(&mreq_coap.ipv6mr_multiaddr, coap_mcast[i], 16);
                if (zsock_setsockopt(mcast_sock, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP,
                                     &mreq_coap, sizeof(mreq_coap)) < 0) {
                    OC_ERR("Failed to join all-CoAP-nodes group [%d]: %d", i, errno);
                } else {
                    n_joined++;
                }
            }
            OC_INF("Joined %d/3 all-CoAP-nodes multicast groups.", n_joined);
        }
    } else {
        OC_WRN("No network interface found, skipping multicast interface binding!");
    }

    rx_tid = k_thread_create(&rx_thread_data, rx_thread_stack,
                             K_THREAD_STACK_SIZEOF(rx_thread_stack),
                             rx_thread, NULL, NULL, NULL,
                             RX_THREAD_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(rx_tid, "coap_rx_wifi");
    OC_DBG("CoAP RX thread started.");

    OC_INF("WiFi connectivity initialized on UDP port %d.", COAP_PORT_UNSECURED);
    return 0;
}

int oc_send_buffer(oc_message_t *message)
{
    struct sockaddr_in6 to = {
        .sin6_family = AF_INET6,
    };

    if (!message) {
        OC_ERR("Attempted to send a NULL message!");
        return -1;
    }
    if (server_sock < 0) {
        OC_ERR("Cannot send CoAP message: socket is not open!");
        return -1;
    }

#ifdef OC_DEBUG
    OC_DBG("Outgoing CoAP message of size %d bytes to ", (int)message->length);
    PRINTipaddr(message->endpoint);
    PRINTF("\r\n");
#endif

    to.sin6_port = htons(message->endpoint.addr.ipv6.port);
    memcpy(to.sin6_addr.s6_addr, message->endpoint.addr.ipv6.address, 16);

    ssize_t sent = zsock_sendto(server_sock, message->data, message->length, 0,
                                (struct sockaddr *)&to, sizeof(to));
    if (sent < 0) {
        OC_ERR("Failed to send CoAP message: %d", errno);
        return -1;
    }

    return 0;
}

void oc_send_discovery_request(oc_message_t *message)
{
    OC_INF("Sending discovery request.");
    oc_send_buffer(message);
}

/* Static snapshot of the WiFi interface's IPv6 unicast addresses.
 * Rebuilt on every call; callers must not hold the pointer across yields. */
#define MAX_WIFI_ENDPOINTS 5
static oc_endpoint_t wifi_endpoints[MAX_WIFI_ENDPOINTS];

oc_endpoint_t *oc_connectivity_get_endpoints(void)
{
    struct net_if *iface = net_if_get_first_wifi();
    if (!iface) {
        iface = net_if_get_default();
    }

    if (!iface) {
        return NULL;
    }

    struct net_if_ipv6 *ipv6 = iface->config.ip.ipv6;

    if (!ipv6) {
        return NULL;
    }

    memset(wifi_endpoints, 0, sizeof(wifi_endpoints));
    int n = 0;

    for (int i = 0; i < NET_IF_MAX_IPV6_ADDR && n < MAX_WIFI_ENDPOINTS; i++) {
        if (!ipv6->unicast[i].is_used) {
            continue;
        }
        wifi_endpoints[n].flags         = IPV6;
        wifi_endpoints[n].addr.ipv6.port = COAP_PORT_UNSECURED;
        wifi_endpoints[n].interface_index = net_if_get_by_iface(iface);
        memcpy(wifi_endpoints[n].addr.ipv6.address,
               ipv6->unicast[i].address.in6_addr.s6_addr, 16);
        if (n > 0) {
            wifi_endpoints[n - 1].next = &wifi_endpoints[n];
        }
        n++;
    }

    return (n > 0) ? &wifi_endpoints[0] : NULL;
}

void oc_connectivity_shutdown(void)
{
    /* Set both fds to -1 before closing so the rx_thread poll loop detects
     * shutdown and exits cleanly. */
    if (server_sock >= 0) {
        int fd = server_sock;
        server_sock = -1;
        zsock_close(fd);
    }
    if (mcast_sock >= 0) {
        int fd = mcast_sock;
        mcast_sock = -1;
        zsock_close(fd);
    }
}

/* ── Network event handler mutex ─────────────────────────────────────────── */

void oc_network_event_handler_mutex_init(void)
{
    /* network_mutex already initialized statically by K_MUTEX_DEFINE */
}

void oc_network_event_handler_mutex_lock(void)
{
    k_mutex_lock(&network_mutex, K_FOREVER);
}

void oc_network_event_handler_mutex_unlock(void)
{
    k_mutex_unlock(&network_mutex);
}

void oc_network_event_handler_mutex_destroy(void)
{
    /* Zephyr kernel mutexes do not require explicit destruction */
}

/* ── Multicast subscriptions ─────────────────────────────────────────────── */

void oc_connectivity_subscribe_mcast_ipv6(oc_endpoint_t *address)
{
    struct ipv6_mreq mreq = {0};

    if (mcast_sock < 0 || !address) {
        return;
    }

    memcpy(&mreq.ipv6mr_multiaddr, address->addr.ipv6.address, 16);
    struct net_if *wifi_if = net_if_get_first_wifi();
    if (!wifi_if) {
        wifi_if = net_if_get_default();
    }
    mreq.ipv6mr_ifindex = wifi_if ? (unsigned int)net_if_get_by_iface(wifi_if) : 0;

    OC_DBG("Subscribing to multicast group on ifidx=%u.", mreq.ipv6mr_ifindex);
    if (zsock_setsockopt(mcast_sock, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP,
                   &mreq, sizeof(mreq)) < 0) {
        OC_ERR("Failed to subscribe to multicast group: %d", errno);
    } else {
        OC_INF("Subscribed to multicast group on ifidx=%u.", mreq.ipv6mr_ifindex);
    }
}

void oc_connectivity_unsubscribe_mcast_ipv6(oc_endpoint_t *address)
{
    struct ipv6_mreq mreq = {0};

    if (mcast_sock < 0 || !address) {
        return;
    }

    memcpy(&mreq.ipv6mr_multiaddr, address->addr.ipv6.address, 16);
    struct net_if *wifi_if = net_if_get_first_wifi();
    if (!wifi_if) {
        wifi_if = net_if_get_default();
    }
    mreq.ipv6mr_ifindex = wifi_if ? (unsigned int)net_if_get_by_iface(wifi_if) : 0;

    OC_DBG("Unsubscribing from multicast group on ifidx=%u.", mreq.ipv6mr_ifindex);
    if (zsock_setsockopt(mcast_sock, IPPROTO_IPV6, IPV6_DROP_MEMBERSHIP,
                   &mreq, sizeof(mreq)) < 0) {
        OC_ERR("Failed to unsubscribe from multicast group: %d", errno);
    } else {
        OC_INF("Unsubscribed from multicast group on ifidx=%u.", mreq.ipv6mr_ifindex);
    }
}

int oc_network_refresh_endpoints(void)
{
    return 0;
}
