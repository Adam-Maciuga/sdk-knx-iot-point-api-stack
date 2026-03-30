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

    while (true) {
        oc_message_t *message = oc_allocate_message();
        if (!message) {
            OC_ERR("rx_thread: failed to allocate OC message\r\n");
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
                OC_ERR("rx_thread: recvfrom error %d\r\n", errno);
            }
            continue;
        }

        message->length                    = (size_t)len;
        message->endpoint.flags            = IPV6;
        message->endpoint.addr.ipv6.port   = ntohs(from.sin6_port);
        memcpy(message->endpoint.addr.ipv6.address,
               from.sin6_addr.s6_addr, 16);

        OC_INF("Incoming message of size %d bytes from ", (int)len);
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
        OC_ERR("oc_connectivity_init: socket() failed: %d\r\n", errno);
        return -1;
    }

    /* Allow re-binding after a quick restart */
    zsock_setsockopt(coap_sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    if (zsock_bind(coap_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        OC_ERR("oc_connectivity_init: bind() failed: %d\r\n", errno);
        zsock_close(coap_sock);
        coap_sock = -1;
        return -1;
    }

    rx_tid = k_thread_create(&rx_thread_data, rx_thread_stack,
                             K_THREAD_STACK_SIZEOF(rx_thread_stack),
                             rx_thread, NULL, NULL, NULL,
                             RX_THREAD_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(rx_tid, "coap_rx_wifi");

    OC_INF("WiFi connectivity initialized on UDP port %d\r\n",
           COAP_PORT_UNSECURED);
    return 0;
}

int oc_send_buffer(oc_message_t *message)
{
    struct sockaddr_in6 to = {
        .sin6_family = AF_INET6,
    };

    if (!message) {
        OC_ERR("oc_send_buffer: NULL message\r\n");
        return -1;
    }
    if (coap_sock < 0) {
        OC_ERR("oc_send_buffer: socket not open\r\n");
        return -1;
    }

#ifdef OC_DEBUG
    OC_INF("Outgoing message of size %d bytes to ", (int)message->length);
    PRINTipaddr(message->endpoint);
    PRINT("\r\n");
#endif

    to.sin6_port = htons(message->endpoint.addr.ipv6.port);
    memcpy(to.sin6_addr.s6_addr, message->endpoint.addr.ipv6.address, 16);

    ssize_t sent = zsock_sendto(coap_sock, message->data, message->length, 0,
                                (struct sockaddr *)&to, sizeof(to));
    if (sent < 0) {
        OC_ERR("oc_send_buffer: sendto failed: %d\r\n", errno);
        return -1;
    }

    return 0;
}

void oc_send_discovery_request(oc_message_t *message)
{
    OC_INF("Sending discovery request\r\n");
    oc_send_buffer(message);
}

oc_endpoint_t *oc_connectivity_get_endpoints(void)
{
    return NULL;
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
    mreq.ipv6mr_ifindex = 0; /* 0 = let the kernel choose the interface */

    if (zsock_setsockopt(coap_sock, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP,
                   &mreq, sizeof(mreq)) < 0) {
        OC_ERR("IPV6_ADD_MEMBERSHIP failed: %d\r\n", errno);
    }
}

void oc_connectivity_unsubscribe_mcast_ipv6(oc_endpoint_t *address)
{
    struct ipv6_mreq mreq = {0};

    if (coap_sock < 0 || !address) {
        return;
    }

    memcpy(&mreq.ipv6mr_multiaddr, address->addr.ipv6.address, 16);
    mreq.ipv6mr_ifindex = 0;

    if (zsock_setsockopt(coap_sock, IPPROTO_IPV6, IPV6_DROP_MEMBERSHIP,
                   &mreq, sizeof(mreq)) < 0) {
        OC_ERR("IPV6_DROP_MEMBERSHIP failed: %d\r\n", errno);
    }
}

int oc_network_refresh_endpoints(void)
{
    return 0;
}
