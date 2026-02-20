/*
// Copyright (c) 2022 Cascoda Ltd.
// Copyright (c) 2024-2026 KNX Association
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/

/**
 * @file dns-sd.c
 *
 * Unified, lightweight, in-process mDNS/DNS-SD service announcement and query
 * response using the public-domain header-only mdns.h library (deps/mdns).
 *
 * Replaces the former platform-specific implementations that shelled out to
 * external tools (Avahi on Linux, Bonjour dns-sd / dns-sd_sharp on Windows).
 *
 * Service publishing consists of two complementary mechanisms:
 *   1. Proactive multicast announcements at startup / parameter change.
 *   2. A background listener thread that responds to incoming PTR queries
 *      for _knx._udp.local, ensuring that mDNS browsers (like dns-sd -B)
 *      discover our service at any time.
 */

/* ipadapter.h is resolved via PORT_DIR include path set by CMake.
 * It provides get_ip_context_for_device() -> ip_context_t* with .port member
 * and (via ipcontext.h) the ip_context_t struct with the OC_LIST eps list. */
#include "ipadapter.h"

#include "dns-sd.h"
#include "oc_log.h"
#include "util/oc_list.h" /* oc_list_head() for iterating endpoint list */

#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>  /* CreateThread, WaitForSingleObject */
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <pthread.h>
  #include <errno.h>
#endif

/* Pull in the header-only mdns implementation */
#include "mdns.h"

/* ------------------------------------------------------------------ */
/* Private helpers & state                                            */
/* ------------------------------------------------------------------ */

/* We keep the socket open between publishes so we can send goodbye + new
 * announce without a gap.  -1 means "not open".
 * IPv6-only per spec 2.6.1.2.1: "mDNS SHALL use UDP port 5353 with multicast
 * IP address FF02::FB for IPv6." */
static int mdns_sock6 = -1;

/* Listener socket (port 5353, multicast group FF02::FB) for answering
 * incoming mDNS queries.  -1 means "not open". */
static int mdns_listen_sock6 = -1;

/* Listener thread handle and termination flag */
static volatile bool listener_running = false;
#ifdef _WIN32
  static HANDLE listener_thread_handle = NULL;
#else
  static pthread_t listener_thread;
  static bool      listener_thread_created = false;
#endif

/* sleep-period TXT value, e.g. "30" (seconds). Empty string = no SP record. */
static char sp_value[16] = "";

/* ---- constants for building mDNS records ---- */
#define KNX_SERVICE_TYPE      "_knx._udp.local."
#define MDNS_BUF_SIZE         2048

/* Scratch buffer for building mDNS packets (no heap allocation).
 * Must be 32-bit aligned as required by mdns.h. */
#ifdef _MSC_VER
  __declspec(align(4)) static char mdns_buf[MDNS_BUF_SIZE];
  __declspec(align(4)) static char mdns_listen_buf[MDNS_BUF_SIZE];
#else
  static char mdns_buf[MDNS_BUF_SIZE] __attribute__((aligned(4)));
  static char mdns_listen_buf[MDNS_BUF_SIZE] __attribute__((aligned(4)));
#endif

/* ------------------------------------------------------------------ */
/* Socket lifecycle                                                   */
/* ------------------------------------------------------------------ */

/**
 * @brief Open a send-only IPv6 mDNS socket.
 *
 * mdns_socket_open_ipv6() joins the FF02::FB multicast group (IPV6_JOIN_GROUP)
 * and binds to port 5353.  Both can fail when another mDNS service is already
 * running.  Since we only *send* announcements (never receive), we skip the
 * group membership and bind to an ephemeral port instead.
 * mdns_multicast_send() detects AF_INET6 via getsockname() and sends to
 * [FF02::FB]:5353 regardless of our source port.
 */
static int
open_mdns_send_socket_ipv6(void)
{
  int sock = (int)socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0)
    return -1;

  /* Allow address reuse (best effort) */
  unsigned int reuseaddr = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
             (const char *)&reuseaddr, sizeof(reuseaddr));

  /* Multicast hop limit = 1 (link-local) */
  int hops = 1;
  setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
             (const char *)&hops, sizeof(hops));

  /* Enable loopback so local listeners also see our packets */
  unsigned int loopback = 1;
  setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
             (const char *)&loopback, sizeof(loopback));

  /* Bind to [::]:0  — ephemeral port, avoids conflict with existing mDNS */
  struct sockaddr_in6 saddr6;
  memset(&saddr6, 0, sizeof(saddr6));
  saddr6.sin6_family = AF_INET6;
  saddr6.sin6_addr   = in6addr_any;
  saddr6.sin6_port   = htons(0);

  if (bind(sock, (struct sockaddr *)&saddr6, sizeof(saddr6))) {
    OC_ERR("dns-sd: bind() failed for IPv6 send socket");
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    return -1;
  }

  /* Non-blocking */
#ifdef _WIN32
  unsigned long param = 1;
  ioctlsocket(sock, FIONBIO, &param);
#else
  {
    const int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
  }
#endif

  return sock;
}

/**
 * @brief Ensure the mDNS IPv6 socket is open.
 * Per spec 2.6.1.2.1: "mDNS SHALL use UDP port 5353 with multicast IP
 * address FF02::FB for IPv6."
 */
static int
ensure_socket(void)
{
  if (mdns_sock6 < 0) {
    mdns_sock6 = open_mdns_send_socket_ipv6();
    if (mdns_sock6 < 0) {
      OC_ERR("dns-sd: failed to open mDNS IPv6 socket");
      return -1;
    }
  }
  return 0;
}

/* ------------------------------------------------------------------ */
/* Announce / Goodbye                                                 */
/* ------------------------------------------------------------------ */

/**
 * @brief Send an mDNS multicast announcement (or goodbye with TTL=0).
 *
 * Records sent:
 *   answer:     PTR  _knx._udp.local.  ->  <sn>._knx._udp.local.
 *   additional: SRV  <sn>._knx._udp.local.  ->  knx-<sn>.local. : port
 *               AAAA knx-<sn>.local.        ->  <ipv6 address>  (per spec 2.6.1.2.2 SHALL)
 *               PTR  _<sn_lower>._sub._knx._udp.local.        ->  instance
 *               PTR  _ia<iid>-<ia>._sub._knx._udp.local.      ->  instance
 *               PTR  _pm._sub._knx._udp.local.                ->  instance  (if pm)
 *               TXT  <sn>._knx._udp.local.  SP=<seconds>                   (if sp set)
 */
static int
send_announcement(char *serial_no, uint64_t iid, uint16_t ia, bool pm, bool goodbye)
{
  if (ensure_socket() != 0)
    return -1;

  /* --- Build name strings ---------------------------------------- */

  /* Lowercase serial number — spec 2.6.1.2.2: "ASCII hexadecimal string
   * in lowercase letters" for the service name and all subtypes. */
  char sn_lower[20];
  strncpy(sn_lower, serial_no, sizeof(sn_lower) - 1);
  sn_lower[sizeof(sn_lower) - 1] = '\0';
  for (int i = 0; sn_lower[i]; ++i)
    sn_lower[i] = (char)tolower((unsigned char)sn_lower[i]);

  /* Instance: "<sn_lower>._knx._udp.local." */
  char instance_name[128];
  (void)snprintf(instance_name, sizeof(instance_name), "%s." KNX_SERVICE_TYPE, sn_lower);
  size_t instance_len = strlen(instance_name);

  /* Hostname: "knx-<sn_lower>.local."  (spec 2.6.1.2.3 SHOULD) */
  char hostname[80];
  (void)snprintf(hostname, sizeof(hostname), "knx-%s.local.", sn_lower);
  size_t hostname_len = strlen(hostname);

  /* Subtype strings */
  char sub_sn[96];
  (void)snprintf(sub_sn, sizeof(sub_sn),
                 "_%s._sub._knx._udp.local.", sn_lower);

  char sub_ia[96];
  (void)snprintf(sub_ia, sizeof(sub_ia),
                 "_ia%" PRIx64 "-%x._sub._knx._udp.local.",
                 iid, (unsigned)ia);

  static const char sub_pm[] = "_pm._sub._knx._udp.local.";

  /* --- Port (from stack) ----------------------------------------- */
  uint16_t port = knx_get_used_port();

  /* --- Build records --------------------------------------------- */

  /* Answer: PTR _knx._udp.local. -> instance */
  mdns_record_t answer;
  memset(&answer, 0, sizeof(answer));
  answer.name.str    = KNX_SERVICE_TYPE;
  answer.name.length = strlen(KNX_SERVICE_TYPE);
  answer.type        = MDNS_RECORDTYPE_PTR;
  answer.data.ptr.name.str    = instance_name;
  answer.data.ptr.name.length = instance_len;

  /* Additional records (max 10: SRV + up to 4 AAAA + 3 subtype PTRs + TXT) */
  #define MAX_AAAA_RECORDS 4
  mdns_record_t additional[6 + MAX_AAAA_RECORDS];
  size_t add_count = 0;
  memset(additional, 0, sizeof(additional));

  /* SRV */
  additional[add_count].name.str        = instance_name;
  additional[add_count].name.length     = instance_len;
  additional[add_count].type            = MDNS_RECORDTYPE_SRV;
  additional[add_count].data.srv.priority = 0;
  additional[add_count].data.srv.weight   = 0;
  additional[add_count].data.srv.port     = port;
  additional[add_count].data.srv.name.str    = hostname;
  additional[add_count].data.srv.name.length = hostname_len;
  add_count++;

  /* AAAA records: advertise IPv6 addresses from the stack's endpoint list.
   * Spec 2.6.1.2.2: "A KNX IoT device SHALL advertise DNS AAAA records since
   * KNX IoT uses IPv6 communication." */
  {
    ip_context_t *ctx = get_ip_context_for_device();
    if (ctx) {
      oc_endpoint_t *ep = (oc_endpoint_t *)oc_list_head(ctx->eps);
      int aaaa_count = 0;
      while (ep && aaaa_count < MAX_AAAA_RECORDS) {
        /* Pick non-multicast, non-secure, non-TCP IPv6 endpoints (unicast server) */
        if ((ep->flags & IPV6) && !(ep->flags & MULTICAST) &&
            !(ep->flags & SECURED) && !(ep->flags & TCP)) {
          additional[add_count].name.str    = hostname;
          additional[add_count].name.length = hostname_len;
          additional[add_count].type        = MDNS_RECORDTYPE_AAAA;
          memset(&additional[add_count].data.aaaa.addr, 0, sizeof(struct sockaddr_in6));
          additional[add_count].data.aaaa.addr.sin6_family = AF_INET6;
          memcpy(&additional[add_count].data.aaaa.addr.sin6_addr,
                 ep->addr.ipv6.address, 16);
          additional[add_count].data.aaaa.addr.sin6_port = htons(ep->addr.ipv6.port);
          add_count++;
          aaaa_count++;
        }
        ep = ep->next;
      }
      if (aaaa_count == 0) {
        OC_DBG("dns-sd: no IPv6 endpoints found for AAAA records");
      }
    }
  }

  /* Subtype PTR: serial number */
  additional[add_count].name.str            = sub_sn;
  additional[add_count].name.length         = strlen(sub_sn);
  additional[add_count].type                = MDNS_RECORDTYPE_PTR;
  additional[add_count].data.ptr.name.str    = instance_name;
  additional[add_count].data.ptr.name.length = instance_len;
  add_count++;

  /* Subtype PTR: installation-id + individual-address */
  additional[add_count].name.str            = sub_ia;
  additional[add_count].name.length         = strlen(sub_ia);
  additional[add_count].type                = MDNS_RECORDTYPE_PTR;
  additional[add_count].data.ptr.name.str    = instance_name;
  additional[add_count].data.ptr.name.length = instance_len;
  add_count++;

  /* Subtype PTR: programming mode (conditional) */
  if (pm) {
    additional[add_count].name.str            = sub_pm;
    additional[add_count].name.length         = strlen(sub_pm);
    additional[add_count].type                = MDNS_RECORDTYPE_PTR;
    additional[add_count].data.ptr.name.str    = instance_name;
    additional[add_count].data.ptr.name.length = instance_len;
    add_count++;
  }

  /* TXT: optional SP=<seconds> */
  if (sp_value[0] != '\0') {
    additional[add_count].name.str            = instance_name;
    additional[add_count].name.length         = instance_len;
    additional[add_count].type                = MDNS_RECORDTYPE_TXT;
    additional[add_count].data.txt.key.str    = "SP";
    additional[add_count].data.txt.key.length = 2;
    additional[add_count].data.txt.value.str    = sp_value;
    additional[add_count].data.txt.value.length = strlen(sp_value);
    add_count++;
  }

  /* --- Send multicast on every IPv6 interface ---------------------- */
  /* Collect unique interface indices from the endpoint list, then send
   * the announcement once per interface via IPV6_MULTICAST_IF.  This
   * ensures the packet reaches all local network segments. */
  #define MAX_IF_INDICES 8
  unsigned int if_indices[MAX_IF_INDICES];
  int          if_count = 0;

  {
    ip_context_t *ctx2 = get_ip_context_for_device();
    if (ctx2) {
      oc_endpoint_t *ep2 = (oc_endpoint_t *)oc_list_head(ctx2->eps);
      while (ep2 && if_count < MAX_IF_INDICES) {
        if ((ep2->flags & IPV6) && !(ep2->flags & MULTICAST) &&
            !(ep2->flags & SECURED) && !(ep2->flags & TCP) &&
            ep2->interface_index > 0) {
          /* De-duplicate: only add if not already in the list */
          unsigned int idx = (unsigned int)ep2->interface_index;
          bool already = false;
          for (int j = 0; j < if_count; ++j) {
            if (if_indices[j] == idx) { already = true; break; }
          }
          if (!already) {
            if_indices[if_count++] = idx;
          }
        }
        ep2 = ep2->next;
      }
    }
  }

  if (if_count == 0) {
    OC_DBG("dns-sd: no interfaces available for mDNS %s (network not ready?)",
           goodbye ? "goodbye" : "announce");
    return 0;
  }

  /* Send on each interface */
  int ok_count = 0;
  for (int iface = 0; iface < if_count; ++iface) {
    setsockopt(mdns_sock6, IPPROTO_IPV6, IPV6_MULTICAST_IF,
               (const char *)&if_indices[iface], sizeof(if_indices[iface]));

    int ret;
    if (goodbye) {
      ret = mdns_goodbye_multicast(mdns_sock6, mdns_buf, sizeof(mdns_buf),
                                   answer, NULL, 0,
                                   additional, add_count);
    } else {
      ret = mdns_announce_multicast(mdns_sock6, mdns_buf, sizeof(mdns_buf),
                                    answer, NULL, 0,
                                    additional, add_count);
    }

    if (ret < 0) {
      OC_DBG("dns-sd: IPv6 multicast %s failed on interface %u",
             goodbye ? "goodbye" : "announce", if_indices[iface]);
    } else {
      OC_DBG("dns-sd: sent mDNS %s on interface %u",
             goodbye ? "goodbye" : "announce", if_indices[iface]);
      ok_count++;
    }
  }

  if (ok_count == 0) {
    OC_DBG("dns-sd: IPv6 multicast %s could not be sent on any interface",
           goodbye ? "goodbye" : "announce");
    return 0;
  }

  OC_DBG("dns-sd: mDNS %s for %s sent on %d/%d interfaces (port %u, iid 0x%" PRIx64 ", ia 0x%x, pm=%d)",
         goodbye ? "goodbye" : "announce",
         serial_no, ok_count, if_count, (unsigned)port, iid, (unsigned)ia, (int)pm);

  return 0;
}

/* Saved previous-advertisement state so we can send a goodbye before
 * re-publishing with changed parameters.  Also read by the listener
 * callback to build query responses. */
static bool     prev_valid = false;
static char     prev_serial[20];
static uint64_t prev_iid;
static uint16_t prev_ia;
static bool     prev_pm;

/* ------------------------------------------------------------------ */
/* mDNS Query Listener                                                */
/* ------------------------------------------------------------------ */

/**
 * @brief Open a listen socket on port 5353 with FF02::FB multicast group.
 *
 * mdns_socket_open_ipv6() fails on IPV6_JOIN_GROUP with interface 0
 * when another mDNS responder (Bonjour) is running.  We create our own
 * socket, try joining the multicast group on each known interface, bind
 * to port 5353, and set non-blocking mode.
 */
static int
open_mdns_listen_socket_ipv6(void)
{
  int sock = (int)socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) {
    OC_ERR("dns-sd: socket() failed for listen socket");
    return -1;
  }

  /* Allow address reuse — required to coexist with Bonjour on port 5353 */
  unsigned int reuseaddr = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
             (const char *)&reuseaddr, sizeof(reuseaddr));
#ifdef SO_REUSEPORT
  setsockopt(sock, SOL_SOCKET, SO_REUSEPORT,
             (const char *)&reuseaddr, sizeof(reuseaddr));
#endif

  /* Multicast hop limit = 1 (for responses) */
  int hops = 1;
  setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
             (const char *)&hops, sizeof(hops));

  /* Enable loopback */
  unsigned int loopback = 1;
  setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
             (const char *)&loopback, sizeof(loopback));

  /* Join FF02::FB multicast group — try on each known interface.
   * Interface 0 (any) often fails on Windows when Bonjour is running. */
  struct ipv6_mreq mreq;
  memset(&mreq, 0, sizeof(mreq));
  mreq.ipv6mr_multiaddr.s6_addr[0]  = 0xFF;
  mreq.ipv6mr_multiaddr.s6_addr[1]  = 0x02;
  mreq.ipv6mr_multiaddr.s6_addr[15] = 0xFB;

  int joined = 0;
  ip_context_t *ctx = get_ip_context_for_device();
  if (ctx) {
    oc_endpoint_t *ep = (oc_endpoint_t *)oc_list_head(ctx->eps);
    /* Collect unique interface indices */
    unsigned int tried[MAX_IF_INDICES];
    int tried_count = 0;
    while (ep && tried_count < MAX_IF_INDICES) {
      if ((ep->flags & IPV6) && !(ep->flags & MULTICAST) && ep->interface_index > 0) {
        unsigned int idx = (unsigned int)ep->interface_index;
        bool dup = false;
        for (int j = 0; j < tried_count; ++j) {
          if (tried[j] == idx) { dup = true; break; }
        }
        if (!dup) {
          tried[tried_count++] = idx;
          mreq.ipv6mr_interface = idx;
          if (setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP,
                         (const char *)&mreq, sizeof(mreq)) == 0) {
            OC_DBG("dns-sd: joined FF02::FB on interface %u", idx);
            joined++;
          } else {
            OC_DBG("dns-sd: IPV6_JOIN_GROUP failed on interface %u", idx);
          }
        }
      }
      ep = ep->next;
    }
  }

  /* Fallback: try interface 0 (any) */
  if (joined == 0) {
    mreq.ipv6mr_interface = 0;
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP,
                   (const char *)&mreq, sizeof(mreq)) == 0) {
      OC_DBG("dns-sd: joined FF02::FB on default interface");
      joined++;
    }
  }

  if (joined == 0) {
    OC_ERR("dns-sd: could not join FF02::FB on any interface");
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    return -1;
  }

  /* Bind to [::]:5353 */
  struct sockaddr_in6 saddr6;
  memset(&saddr6, 0, sizeof(saddr6));
  saddr6.sin6_family = AF_INET6;
  saddr6.sin6_addr   = in6addr_any;
  saddr6.sin6_port   = htons(MDNS_PORT);

  if (bind(sock, (struct sockaddr *)&saddr6, sizeof(saddr6))) {
    OC_ERR("dns-sd: bind(:5353) failed for listen socket");
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    return -1;
  }

  /* Non-blocking */
#ifdef _WIN32
  unsigned long param = 1;
  ioctlsocket(sock, FIONBIO, &param);
#else
  {
    const int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
  }
#endif

  OC_INF("dns-sd: listen socket open on port 5353 (joined %d interfaces)", joined);
  return sock;
}

/**
 * @brief Case-insensitive comparison of DNS name strings, ignoring trailing dots.
 */
static bool
dns_name_equal(const char *a, size_t alen, const char *b, size_t blen)
{
  /* Strip trailing dots */
  if (alen > 0 && a[alen - 1] == '.') alen--;
  if (blen > 0 && b[blen - 1] == '.') blen--;
  if (alen != blen) return false;
  for (size_t i = 0; i < alen; ++i) {
    if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
      return false;
  }
  return true;
}

/**
 * @brief Check if the query name ends with "._sub._knx._udp.local" (case-insensitive).
 */
static bool
is_knx_subtype_query(const char *qname, size_t qlen)
{
  const char *suffix = "._sub._knx._udp.local";
  size_t slen = strlen(suffix);
  /* Strip trailing dot */
  if (qlen > 0 && qname[qlen - 1] == '.') qlen--;
  if (qlen <= slen) return false;
  for (size_t i = 0; i < slen; ++i) {
    if (tolower((unsigned char)qname[qlen - slen + i]) !=
        tolower((unsigned char)suffix[i]))
      return false;
  }
  return true;
}

/**
 * @brief Build and send the current service records on `sock`.
 *
 * @param sock       The mDNS socket to send on.
 * @param ptr_name   The name for the PTR answer record.  For a general
 *                   browse this is "_knx._udp.local.", for a subtype
 *                   browse this is e.g. "_pm._sub._knx._udp.local.".
 *
 * Used both by the proactive announcement and the query callback.
 * Returns 0 on success, <0 on error.
 */
static int
send_query_response(int sock, const char *ptr_name)
{
  char sn_lower[20];
  strncpy(sn_lower, prev_serial, sizeof(sn_lower) - 1);
  sn_lower[sizeof(sn_lower) - 1] = '\0';
  for (int i = 0; sn_lower[i]; ++i)
    sn_lower[i] = (char)tolower((unsigned char)sn_lower[i]);

  char instance_name[128];
  (void)snprintf(instance_name, sizeof(instance_name), "%s." KNX_SERVICE_TYPE, sn_lower);
  size_t instance_len = strlen(instance_name);

  char hostname[80];
  (void)snprintf(hostname, sizeof(hostname), "knx-%s.local.", sn_lower);
  size_t hostname_len = strlen(hostname);

  uint16_t port = knx_get_used_port();

  /* Answer: PTR — the name must match the query the client sent */
  mdns_record_t answer;
  memset(&answer, 0, sizeof(answer));
  answer.name.str    = ptr_name;
  answer.name.length = strlen(ptr_name);
  answer.type        = MDNS_RECORDTYPE_PTR;
  answer.data.ptr.name.str    = instance_name;
  answer.data.ptr.name.length = instance_len;

  /* Additional records */
  mdns_record_t additional[6 + MAX_AAAA_RECORDS];
  size_t add_count = 0;
  memset(additional, 0, sizeof(additional));

  /* SRV */
  additional[add_count].name.str        = instance_name;
  additional[add_count].name.length     = instance_len;
  additional[add_count].type            = MDNS_RECORDTYPE_SRV;
  additional[add_count].data.srv.priority = 0;
  additional[add_count].data.srv.weight   = 0;
  additional[add_count].data.srv.port     = port;
  additional[add_count].data.srv.name.str    = hostname;
  additional[add_count].data.srv.name.length = hostname_len;
  add_count++;

  /* AAAA */
  {
    ip_context_t *ctx = get_ip_context_for_device();
    if (ctx) {
      oc_endpoint_t *ep = (oc_endpoint_t *)oc_list_head(ctx->eps);
      int aaaa_count = 0;
      while (ep && aaaa_count < MAX_AAAA_RECORDS) {
        if ((ep->flags & IPV6) && !(ep->flags & MULTICAST) &&
            !(ep->flags & SECURED) && !(ep->flags & TCP)) {
          additional[add_count].name.str    = hostname;
          additional[add_count].name.length = hostname_len;
          additional[add_count].type        = MDNS_RECORDTYPE_AAAA;
          memset(&additional[add_count].data.aaaa.addr, 0, sizeof(struct sockaddr_in6));
          additional[add_count].data.aaaa.addr.sin6_family = AF_INET6;
          memcpy(&additional[add_count].data.aaaa.addr.sin6_addr,
                 ep->addr.ipv6.address, 16);
          additional[add_count].data.aaaa.addr.sin6_port = htons(ep->addr.ipv6.port);
          add_count++;
          aaaa_count++;
        }
        ep = ep->next;
      }
    }
  }

  /* Subtype PTRs */
  char sub_sn[96];
  (void)snprintf(sub_sn, sizeof(sub_sn), "_%s._sub._knx._udp.local.", sn_lower);
  additional[add_count].name.str            = sub_sn;
  additional[add_count].name.length         = strlen(sub_sn);
  additional[add_count].type                = MDNS_RECORDTYPE_PTR;
  additional[add_count].data.ptr.name.str    = instance_name;
  additional[add_count].data.ptr.name.length = instance_len;
  add_count++;

  char sub_ia[96];
  (void)snprintf(sub_ia, sizeof(sub_ia),
                 "_ia%" PRIx64 "-%x._sub._knx._udp.local.",
                 prev_iid, (unsigned)prev_ia);
  additional[add_count].name.str            = sub_ia;
  additional[add_count].name.length         = strlen(sub_ia);
  additional[add_count].type                = MDNS_RECORDTYPE_PTR;
  additional[add_count].data.ptr.name.str    = instance_name;
  additional[add_count].data.ptr.name.length = instance_len;
  add_count++;

  if (prev_pm) {
    static const char sub_pm[] = "_pm._sub._knx._udp.local.";
    additional[add_count].name.str            = sub_pm;
    additional[add_count].name.length         = strlen(sub_pm);
    additional[add_count].type                = MDNS_RECORDTYPE_PTR;
    additional[add_count].data.ptr.name.str    = instance_name;
    additional[add_count].data.ptr.name.length = instance_len;
    add_count++;
  }

  /* TXT */
  if (sp_value[0] != '\0') {
    additional[add_count].name.str            = instance_name;
    additional[add_count].name.length         = instance_len;
    additional[add_count].type                = MDNS_RECORDTYPE_TXT;
    additional[add_count].data.txt.key.str    = "SP";
    additional[add_count].data.txt.key.length = 2;
    additional[add_count].data.txt.value.str    = sp_value;
    additional[add_count].data.txt.value.length = strlen(sp_value);
    add_count++;
  }

  /* Use a stack buffer for the response */
#ifdef _MSC_VER
  __declspec(align(4)) char resp_buf[MDNS_BUF_SIZE];
#else
  char resp_buf[MDNS_BUF_SIZE] __attribute__((aligned(4)));
#endif

  int ret = mdns_query_answer_multicast(sock, resp_buf, sizeof(resp_buf),
                                        answer, NULL, 0,
                                        additional, add_count);
  return ret;
}

/**
 * @brief Send an SRV answer for our service instance.
 *
 * Answer:      SRV {instance}._knx._udp.local. → knx-{sn}.local. : port
 * Additional:  AAAA knx-{sn}.local. → IPv6 addresses
 */
static int
send_srv_response(int sock)
{
  char sn_lower[20];
  strncpy(sn_lower, prev_serial, sizeof(sn_lower) - 1);
  sn_lower[sizeof(sn_lower) - 1] = '\0';
  for (int i = 0; sn_lower[i]; ++i)
    sn_lower[i] = (char)tolower((unsigned char)sn_lower[i]);

  char instance_name[128];
  (void)snprintf(instance_name, sizeof(instance_name), "%s." KNX_SERVICE_TYPE, sn_lower);
  size_t instance_len = strlen(instance_name);

  char hostname[80];
  (void)snprintf(hostname, sizeof(hostname), "knx-%s.local.", sn_lower);
  size_t hostname_len = strlen(hostname);

  uint16_t port = knx_get_used_port();

  /* Answer: SRV */
  mdns_record_t answer;
  memset(&answer, 0, sizeof(answer));
  answer.name.str               = instance_name;
  answer.name.length            = instance_len;
  answer.type                   = MDNS_RECORDTYPE_SRV;
  answer.data.srv.priority      = 0;
  answer.data.srv.weight        = 0;
  answer.data.srv.port          = port;
  answer.data.srv.name.str      = hostname;
  answer.data.srv.name.length   = hostname_len;

  /* Additional: AAAA records */
  mdns_record_t additional[MAX_AAAA_RECORDS];
  size_t add_count = 0;
  memset(additional, 0, sizeof(additional));

  ip_context_t *ctx = get_ip_context_for_device();
  if (ctx) {
    oc_endpoint_t *ep = (oc_endpoint_t *)oc_list_head(ctx->eps);
    while (ep && add_count < MAX_AAAA_RECORDS) {
      if ((ep->flags & IPV6) && !(ep->flags & MULTICAST) &&
          !(ep->flags & SECURED) && !(ep->flags & TCP)) {
        additional[add_count].name.str    = hostname;
        additional[add_count].name.length = hostname_len;
        additional[add_count].type        = MDNS_RECORDTYPE_AAAA;
        memset(&additional[add_count].data.aaaa.addr, 0, sizeof(struct sockaddr_in6));
        additional[add_count].data.aaaa.addr.sin6_family = AF_INET6;
        memcpy(&additional[add_count].data.aaaa.addr.sin6_addr,
               ep->addr.ipv6.address, 16);
        additional[add_count].data.aaaa.addr.sin6_port = htons(ep->addr.ipv6.port);
        add_count++;
      }
      ep = ep->next;
    }
  }

#ifdef _MSC_VER
  __declspec(align(4)) char resp_buf[MDNS_BUF_SIZE];
#else
  char resp_buf[MDNS_BUF_SIZE] __attribute__((aligned(4)));
#endif

  return mdns_query_answer_multicast(sock, resp_buf, sizeof(resp_buf),
                                     answer, NULL, 0,
                                     additional, add_count);
}

/**
 * @brief Send an AAAA answer for our hostname.
 *
 * Answer:  AAAA knx-{sn}.local. → first IPv6 address
 * Additional:  remaining AAAA records (if more than one address)
 */
static int
send_aaaa_response(int sock)
{
  char sn_lower[20];
  strncpy(sn_lower, prev_serial, sizeof(sn_lower) - 1);
  sn_lower[sizeof(sn_lower) - 1] = '\0';
  for (int i = 0; sn_lower[i]; ++i)
    sn_lower[i] = (char)tolower((unsigned char)sn_lower[i]);

  char hostname[80];
  (void)snprintf(hostname, sizeof(hostname), "knx-%s.local.", sn_lower);
  size_t hostname_len = strlen(hostname);

  /* Collect all AAAA records */
  mdns_record_t aaaa_records[MAX_AAAA_RECORDS];
  size_t aaaa_count = 0;
  memset(aaaa_records, 0, sizeof(aaaa_records));

  ip_context_t *ctx = get_ip_context_for_device();
  if (ctx) {
    oc_endpoint_t *ep = (oc_endpoint_t *)oc_list_head(ctx->eps);
    while (ep && aaaa_count < MAX_AAAA_RECORDS) {
      if ((ep->flags & IPV6) && !(ep->flags & MULTICAST) &&
          !(ep->flags & SECURED) && !(ep->flags & TCP)) {
        aaaa_records[aaaa_count].name.str    = hostname;
        aaaa_records[aaaa_count].name.length = hostname_len;
        aaaa_records[aaaa_count].type        = MDNS_RECORDTYPE_AAAA;
        memset(&aaaa_records[aaaa_count].data.aaaa.addr, 0, sizeof(struct sockaddr_in6));
        aaaa_records[aaaa_count].data.aaaa.addr.sin6_family = AF_INET6;
        memcpy(&aaaa_records[aaaa_count].data.aaaa.addr.sin6_addr,
               ep->addr.ipv6.address, 16);
        aaaa_records[aaaa_count].data.aaaa.addr.sin6_port = htons(ep->addr.ipv6.port);
        aaaa_count++;
      }
      ep = ep->next;
    }
  }

  if (aaaa_count == 0) {
    OC_DBG("dns-sd: no IPv6 addresses to respond with for AAAA query");
    return -1;
  }

  /* First AAAA record is the answer, rest go in additional */
  mdns_record_t answer = aaaa_records[0];

#ifdef _MSC_VER
  __declspec(align(4)) char resp_buf[MDNS_BUF_SIZE];
#else
  char resp_buf[MDNS_BUF_SIZE] __attribute__((aligned(4)));
#endif

  const mdns_record_t *add_ptr = (aaaa_count > 1) ? &aaaa_records[1] : NULL;
  size_t add_count = (aaaa_count > 1) ? (aaaa_count - 1) : 0;

  return mdns_query_answer_multicast(sock, resp_buf, sizeof(resp_buf),
                                     answer, NULL, 0,
                                     add_ptr, add_count);
}

/**
 * @brief mdns_socket_listen callback — invoked for each question or record
 *        in a received mDNS packet.
 *
 * Responds to:
 *   - PTR/ANY queries for _knx._udp.local.          (general service browse)
 *   - PTR/ANY queries for _pm._sub._knx._udp.local. (programming mode browse)
 *   - PTR/ANY queries for _<sn>._sub._knx._udp.local. (serial number browse)
 *   - PTR/ANY queries for _ia<iid>-<ia>._sub._knx._udp.local. (IA browse)
 *   - SRV/ANY queries for <sn>._knx._udp.local.     (instance → hostname+port)
 *   - AAAA/ANY queries for knx-<sn>.local.           (hostname → IPv6 address)
 */
static int
mdns_query_callback(int sock, const struct sockaddr *from, size_t addrlen,
                    mdns_entry_type_t entry, uint16_t query_id,
                    uint16_t rtype, uint16_t rclass, uint32_t ttl,
                    const void *data, size_t size,
                    size_t name_offset, size_t name_length,
                    size_t record_offset, size_t record_length,
                    void *user_data)
{
  (void)from; (void)addrlen; (void)query_id; (void)rclass; (void)ttl;
  (void)record_offset; (void)record_length; (void)user_data;

  /* Only interested in questions */
  if (entry != MDNS_ENTRYTYPE_QUESTION)
    return 0;

  /* Must have a published service */
  if (!prev_valid)
    return 0;

  /* Filter to record types we handle */
  if (rtype != MDNS_RECORDTYPE_PTR && rtype != MDNS_RECORDTYPE_SRV &&
      rtype != MDNS_RECORDTYPE_AAAA && rtype != MDNS_RECORDTYPE_ANY)
    return 0;

  /* Extract queried name */
  char qname[256];
  mdns_string_t qstr = mdns_string_extract(data, size, &name_offset,
                                           qname, sizeof(qname));
  if (qstr.length == 0)
    return 0;

  /* Pre-build our canonical names for matching */
  char sn_lower[20];
  strncpy(sn_lower, prev_serial, sizeof(sn_lower) - 1);
  sn_lower[sizeof(sn_lower) - 1] = '\0';
  for (int i = 0; sn_lower[i]; ++i)
    sn_lower[i] = (char)tolower((unsigned char)sn_lower[i]);

  char instance_name[128];
  (void)snprintf(instance_name, sizeof(instance_name), "%s." KNX_SERVICE_TYPE, sn_lower);

  char hostname[80];
  (void)snprintf(hostname, sizeof(hostname), "knx-%s.local.", sn_lower);

  int ret;

  /* ---- 1. PTR / ANY: service browse & subtype browse ---- */
  if (rtype == MDNS_RECORDTYPE_PTR || rtype == MDNS_RECORDTYPE_ANY) {
    bool ptr_respond = false;

    /* 1a. General service browse: _knx._udp.local. */
    if (dns_name_equal(qname, qstr.length, KNX_SERVICE_TYPE, strlen(KNX_SERVICE_TYPE))) {
      OC_INF("dns-sd: received browse query for %s", KNX_SERVICE_TYPE);
      ptr_respond = true;
    }

    /* 1b. Subtype queries: *._sub._knx._udp.local. */
    if (!ptr_respond && is_knx_subtype_query(qname, qstr.length)) {
      /* _pm subtype */
      if (prev_pm) {
        const char *pm_sub = "_pm._sub._knx._udp.local.";
        if (dns_name_equal(qname, qstr.length, pm_sub, strlen(pm_sub))) {
          OC_INF("dns-sd: received _pm subtype query - device IS in programming mode");
          ptr_respond = true;
        }
      }

      /* _<sn> subtype */
      if (!ptr_respond) {
        char sub_sn[96];
        (void)snprintf(sub_sn, sizeof(sub_sn), "_%s._sub._knx._udp.local.", sn_lower);
        if (dns_name_equal(qname, qstr.length, sub_sn, strlen(sub_sn))) {
          OC_INF("dns-sd: received serial number subtype query");
          ptr_respond = true;
        }
      }

      /* _ia<iid>-<ia> subtype */
      if (!ptr_respond) {
        char sub_ia[96];
        (void)snprintf(sub_ia, sizeof(sub_ia),
                       "_ia%" PRIx64 "-%x._sub._knx._udp.local.",
                       prev_iid, (unsigned)prev_ia);
        if (dns_name_equal(qname, qstr.length, sub_ia, strlen(sub_ia))) {
          OC_INF("dns-sd: received IA subtype query");
          ptr_respond = true;
        }
      }

      if (!ptr_respond) {
        OC_DBG("dns-sd: received _sub query for unmatched subtype: %.*s",
               (int)qstr.length, qname);
      }
    }

    if (ptr_respond) {
      /* Use the queried name as PTR answer name */
      char ptr_name[256];
      size_t plen = qstr.length;
      if (plen >= sizeof(ptr_name)) plen = sizeof(ptr_name) - 1;
      memcpy(ptr_name, qname, plen);
      ptr_name[plen] = '\0';
      if (plen > 0 && ptr_name[plen - 1] != '.' && plen + 1 < sizeof(ptr_name)) {
        ptr_name[plen] = '.';
        ptr_name[plen + 1] = '\0';
      }

      ret = send_query_response(sock, ptr_name);
      if (ret < 0) {
        OC_ERR("dns-sd: failed to send PTR answer (ret=%d)", ret);
      } else {
        OC_INF("dns-sd: PTR answer sent for %s", ptr_name);
      }
      return 0;
    }
  }

  /* ---- 2. SRV / ANY: instance name → hostname + port ---- */
  if (rtype == MDNS_RECORDTYPE_SRV || rtype == MDNS_RECORDTYPE_ANY) {
    if (dns_name_equal(qname, qstr.length, instance_name, strlen(instance_name))) {
      OC_INF("dns-sd: received SRV query for %s", instance_name);
      ret = send_srv_response(sock);
      if (ret < 0) {
        OC_ERR("dns-sd: failed to send SRV answer (ret=%d)", ret);
      } else {
        OC_INF("dns-sd: SRV answer sent for %s", instance_name);
      }
      return 0;
    }
  }

  /* ---- 3. AAAA / ANY: hostname → IPv6 address ---- */
  if (rtype == MDNS_RECORDTYPE_AAAA || rtype == MDNS_RECORDTYPE_ANY) {
    if (dns_name_equal(qname, qstr.length, hostname, strlen(hostname))) {
      OC_INF("dns-sd: received AAAA query for %s", hostname);
      ret = send_aaaa_response(sock);
      if (ret < 0) {
        OC_ERR("dns-sd: failed to send AAAA answer (ret=%d)", ret);
      } else {
        OC_INF("dns-sd: AAAA answer sent for %s", hostname);
      }
      return 0;
    }
  }

  return 0;  /* unmatched query — ignore */
}

/**
 * @brief Listener thread entry point.
 *
 * Runs in a loop calling mdns_socket_listen() which does a blocking-ish
 * recvfrom().  The socket is non-blocking (set by mdns_socket_open_ipv6),
 * so we add a small sleep to avoid busy-looping when no packets arrive.
 */
#ifdef _WIN32
static DWORD WINAPI
mdns_listener_thread(LPVOID param)
#else
static void *
mdns_listener_thread(void *param)
#endif
{
  (void)param;
  OC_INF("dns-sd: listener thread started on port 5353");

  while (listener_running) {
    size_t parsed = mdns_socket_listen(mdns_listen_sock6, mdns_listen_buf,
                                       sizeof(mdns_listen_buf),
                                       mdns_query_callback, NULL);
    if (parsed == 0) {
      /* No packet received — sleep briefly to avoid busy loop */
#ifdef _WIN32
      Sleep(100);
#else
      usleep(100000);
#endif
    }
  }

  OC_INF("dns-sd: listener thread stopped");
#ifdef _WIN32
  return 0;
#else
  return NULL;
#endif
}

/**
 * @brief Start the mDNS listener thread (if not already running).
 */
static void
start_listener(void)
{
  if (listener_running)
    return;

  mdns_listen_sock6 = open_mdns_listen_socket_ipv6();
  if (mdns_listen_sock6 < 0) {
    OC_ERR("dns-sd: query responder not started (listen socket unavailable)");
    return;
  }

  listener_running = true;

#ifdef _WIN32
  listener_thread_handle =
    CreateThread(NULL, 0, mdns_listener_thread, NULL, 0, NULL);
  if (listener_thread_handle == NULL) {
    OC_ERR("dns-sd: failed to create listener thread");
    listener_running = false;
    mdns_socket_close(mdns_listen_sock6);
    mdns_listen_sock6 = -1;
  }
#else
  if (pthread_create(&listener_thread, NULL, mdns_listener_thread, NULL) != 0) {
    OC_ERR("dns-sd: failed to create listener thread");
    listener_running = false;
    mdns_socket_close(mdns_listen_sock6);
    mdns_listen_sock6 = -1;
  } else {
    listener_thread_created = true;
  }
#endif
}

/**
 * @brief Stop the mDNS listener thread and close the listen socket.
 */
static void
stop_listener(void)
{
  if (!listener_running)
    return;

  listener_running = false;

#ifdef _WIN32
  if (listener_thread_handle) {
    WaitForSingleObject(listener_thread_handle, 2000);
    CloseHandle(listener_thread_handle);
    listener_thread_handle = NULL;
  }
#else
  if (listener_thread_created) {
    pthread_join(listener_thread, NULL);
    listener_thread_created = false;
  }
#endif

  if (mdns_listen_sock6 >= 0) {
    mdns_socket_close(mdns_listen_sock6);
    mdns_listen_sock6 = -1;
  }
}

/* ------------------------------------------------------------------ */
/* Public API  (see port/dns-sd.h)                                    */
/* ------------------------------------------------------------------ */

int
knx_publish_service(char *serial_no, uint64_t iid, uint16_t ia, bool pm)
{
#ifndef OC_DNS_SD
  (void)serial_no;
  (void)iid;
  (void)ia;
  (void)pm;
  return 0;
#else

  /* Goodbye for previous advertisement (if any) */
  if (prev_valid) {
    (void)send_announcement(prev_serial, prev_iid, prev_ia, prev_pm, /*goodbye=*/true);
  }

  /* New announcement */
  int ret = send_announcement(serial_no, iid, ia, pm, /*goodbye=*/false);

  /* Remember for next goodbye + for listener callback */
  if (ret == 0) {
    strncpy(prev_serial, serial_no, sizeof(prev_serial) - 1);
    prev_serial[sizeof(prev_serial) - 1] = '\0';
    prev_iid = iid;
    prev_ia  = ia;
    prev_pm  = pm;
    prev_valid = true;
  }

  /* Start the query-responder thread (idempotent — only starts once) */
  start_listener();

  return ret;
#endif /* OC_DNS_SD */
}

void
knx_service_sleep_period(int sp)
{
  if (sp)
    (void)snprintf(sp_value, sizeof(sp_value), "%d", sp);
  else
    memset(sp_value, 0, sizeof(sp_value));
}

uint16_t
knx_get_used_port(void)
{
  return get_ip_context_for_device()->port;
}

void
knx_stop_mdns(void)
{
#ifdef OC_DNS_SD
  /* Send goodbye for current advertisement */
  if (prev_valid) {
    (void)send_announcement(prev_serial, prev_iid, prev_ia, prev_pm, /*goodbye=*/true);
    prev_valid = false;
  }

  /* Stop the listener thread */
  stop_listener();

  /* Close the send socket */
  if (mdns_sock6 >= 0) {
#ifdef _WIN32
    closesocket(mdns_sock6);
#else
    close(mdns_sock6);
#endif
    mdns_sock6 = -1;
  }
#endif
}
