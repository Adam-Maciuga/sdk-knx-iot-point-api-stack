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
 * A dedicated receive thread blocks on recvfrom() and feeds incoming datagrams
 * into the KNX-IoT stack via oc_network_event().
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

static int   coap_sock    = -1;
static k_tid_t rx_tid     = NULL;

/* ── Receive thread ────────────────────────────────────────────────────────── */

static void rx_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    OC_DBG("CoAP RX thread started, blocking on recvfrom fd=%d.", coap_sock);

    while (true) {
        oc_message_t *message = oc_allocate_message();
        if (!message) {
            OC_ERR("Failed to allocate message for CoAP RX!");
            k_msleep(10);
            continue;
        }

        struct sockaddr_in6 from;
        socklen_t from_len = sizeof(from);

        ssize_t len = zsock_recvfrom(coap_sock, message->data, OC_PDU_SIZE, 0,
                                     (struct sockaddr *)&from, &from_len);
        if (len < 0) {
            oc_message_unref(message);
            if (coap_sock < 0) {
                /* Socket closed — shutdown requested, exit thread. */
                break;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                OC_ERR("recvfrom error: %d", errno);// TODO wording
            }
            continue;
        }

        message->length                    = (size_t)len;
        message->endpoint.flags            = IPV6;
        message->endpoint.addr.ipv6.port   = ntohs(from.sin6_port);
        memcpy(message->endpoint.addr.ipv6.address,
               from.sin6_addr.s6_addr, 16);

        OC_INF("Incoming CoAP message of size %d bytes from ", (int)len);
        PRINTipaddr(message->endpoint);
        PRINT("\r\n");

        oc_network_event(message);
    }
}

/* ── Public interface ─────────────────────────────────────────────────────── */

int oc_connectivity_init(void)
{
    struct sockaddr_in6 addr = {
        .sin6_family = AF_INET6,
        .sin6_port   = htons(COAP_PORT_UNSECURED),
        .sin6_addr   = IN6ADDR_ANY_INIT,
    };
    int on = 1;

    coap_sock = zsock_socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (coap_sock < 0) {
        OC_ERR("UDP socket creation failed: %d", errno);
        return -1;
    }
    OC_DBG("UDP socket created, fd=%d.", coap_sock);

    /* Allow re-binding after a quick restart */
    zsock_setsockopt(coap_sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    if (zsock_bind(coap_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        OC_ERR("Bind to UDP port %d failed: %d", COAP_PORT_UNSECURED, errno);
        zsock_close(coap_sock);
        coap_sock = -1;
        return -1;
    }
    OC_DBG("Bound to UDP port %d.", COAP_PORT_UNSECURED);

    /* Pin multicast sends to the default (WiFi) interface.
     * Without this, sendto() on ff02::/ff03:: multicast destinations would rely
     * on kernel routing which may fail if no explicit multicast route is set. */
    int ifidx = net_if_get_by_iface(net_if_get_default());
    if (ifidx > 0) {
        if (zsock_setsockopt(coap_sock, IPPROTO_IPV6, IPV6_MULTICAST_IF,
                             &ifidx, sizeof(ifidx)) < 0) {
            OC_WRN("Failed to pin multicast sends to interface: %d", errno);
        } else {
            OC_DBG("Multicast sends pinned to interface ifidx=%d.", ifidx);
        }

        /* Subscribe to all-CoAP-nodes multicast groups so discovery
         * requests reach this socket regardless of GOT configuration.
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
                if (zsock_setsockopt(coap_sock, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP,
                                     &mreq_coap, sizeof(mreq_coap)) < 0) {
                    OC_ERR("Failed to join all-CoAP-nodes group [%d]: %d", i, errno);
                } else {
                    n_joined++;
                }
            }
            OC_INF("Joined %d/3 all-CoAP-nodes multicast groups.", n_joined);
        }
    } else {
        OC_WRN("Default network interface not found, skipping multicast interface binding!");
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
        OC_ERR("NULL message!");
        return -1;
    }
    if (coap_sock < 0) {
        OC_ERR("CoAP socket not open!");
        return -1;
    }

#ifdef OC_DEBUG
    OC_DBG("Outgoing CoAP message of size %d bytes to ", (int)message->length);
    PRINTipaddr(message->endpoint);
    PRINT("\r\n");
#endif

    to.sin6_port = htons(message->endpoint.addr.ipv6.port);
    memcpy(to.sin6_addr.s6_addr, message->endpoint.addr.ipv6.address, 16);

    ssize_t sent = zsock_sendto(coap_sock, message->data, message->length, 0,
                                (struct sockaddr *)&to, sizeof(to));
    if (sent < 0) {
        OC_ERR("sendto failed: %d", errno); // TODO wording
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
    struct net_if *iface = net_if_get_default();

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
    if (coap_sock >= 0) {
        int fd   = coap_sock;
        coap_sock = -1;    /* signal rx_thread to exit before closing */
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

    if (coap_sock < 0 || !address) {
        return;
    }

    memcpy(&mreq.ipv6mr_multiaddr, address->addr.ipv6.address, 16);
    mreq.ipv6mr_ifindex = (unsigned int)net_if_get_by_iface(net_if_get_default());

    OC_DBG("Subscribing to multicast group on ifidx=%u.", mreq.ipv6mr_ifindex);
    if (zsock_setsockopt(coap_sock, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP,
                   &mreq, sizeof(mreq)) < 0) {
        OC_ERR("Failed to subscribe to multicast group: %d", errno);
    } else {
        OC_INF("Subscribed to multicast group on ifidx=%u.", mreq.ipv6mr_ifindex);
    }
}

void oc_connectivity_unsubscribe_mcast_ipv6(oc_endpoint_t *address)
{
    struct ipv6_mreq mreq = {0};

    if (coap_sock < 0 || !address) {
        return;
    }

    memcpy(&mreq.ipv6mr_multiaddr, address->addr.ipv6.address, 16);
    mreq.ipv6mr_ifindex = (unsigned int)net_if_get_by_iface(net_if_get_default());

    OC_DBG("Unsubscribing from multicast group on ifidx=%u.", mreq.ipv6mr_ifindex);
    if (zsock_setsockopt(coap_sock, IPPROTO_IPV6, IPV6_DROP_MEMBERSHIP,
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
