/*
 * Copyright (c) 2022 Cascoda Ltd.
 * Copyright (c) 2024-2026 KNX Association
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file dns-sd_mdns.c
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

// ipadapter.h is resolved via PORT_DIR include path set by CMake. It provides get_ip_context_for_device() -> ip_context_t* with .port member and (via ipcontext.h) the ip_context_t struct with the OC_LIST eps list.
#ifndef __ZEPHYR__
#include "ipadapter.h" // get_ip_context_for_device(): Linux/Windows only
#endif

#include "dns-sd.h"
#include "oc_log.h"
#include "util/oc_list.h" // oc_list_head() for iterating endpoint list

#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#ifndef _MSC_VER
#include <strings.h> // strncasecmp, used by mdns.h (absent on MSVC)
#endif
#include <ctype.h>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>  // CreateThread, WaitForSingleObject
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <errno.h>
  #ifdef __ZEPHYR__
    // Native kernel threads instead of pthreads, the POSIX threads layer is not pulled in. The BSD socket names come from CONFIG_POSIX_NETWORKING.
    #include <zephyr/kernel.h>
  #else
    #include <pthread.h>
  #endif
#endif

/*
 * Zephyr: bare close()/fcntl() are declared in <unistd.h>/<fcntl.h> only under
 * CONFIG_POSIX_API, which we avoid to keep the POSIX threads layer out. Map them
 * to Zephyr's native socket calls. Every close()/fcntl() in this file operates on
 * a socket.
 *
 * NOTE: these MUST come before #include "mdns.h" below, because mdns.h's own
 * mdns_socket_close() uses bare close() and is compiled at its include point.
 */
#ifdef __ZEPHYR__
#define close zsock_close
#define fcntl zsock_fcntl
// Zephyr's struct net_ipv6_mreq names the interface member ipv6mr_ifindex, not the POSIX ipv6mr_interface. Remap so the shared multicast-join code compiles.
#define ipv6mr_interface ipv6mr_ifindex
#endif

// Pull in the header-only mDNS implementation
#include "mdns.h"

#ifdef __ZEPHYR__
/* ------------------------------------------------------------------ */
/* Zephyr shim for the Linux/Windows ip_context_t endpoint accessor   */
/* ------------------------------------------------------------------ */
/*
 * The shared code below reaches the stack's endpoint list and CoAP port via
 * get_ip_context_for_device() (declared in 'ipadapter.h' on Linux/Windows).
 * Zephyr has no 'ipadapter.h', so provide a minimal compatible shim backed by
 * oc_connectivity_get_endpoints(). oc_list_head() dereferences the oc_list_t
 * (void**), so eps points at a static head cell holding the endpoint list,
 * letting every call site compile unchanged.
 */
#include "oc_endpoint.h"
#include "port/oc_connectivity.h"

typedef struct {
  oc_list_t eps;
  uint16_t port;
} ip_context_t;

static oc_endpoint_t *knx_eps_head;

static ip_context_t *get_ip_context_for_device(void)
{
  static ip_context_t ctx;
  knx_eps_head = oc_connectivity_get_endpoints();
  ctx.eps = (oc_list_t)&knx_eps_head;

  // Advertise the real bound CoAP port from the unicast IPv6 endpoint. CONFIG_KNX_UNICAST_PORT is deliberately not used as it may be 0/ephemeral. The first match is sufficient.
  ctx.port = 0;
  for (oc_endpoint_t *ep = knx_eps_head; ep; ep = ep->next)
  {
    if ((ep->flags & IPV6) && !(ep->flags & (MULTICAST | SECURED | TCP)))
    {
      ctx.port = ep->addr.ipv6.port;
      break;
    }
  }

  if (ctx.port == 0)
  {
    OC_ERR("DNS-SD: No bound unicast IPv6 endpoint found, advertised SRV port is invalid!");
  }
  return &ctx;
}
#endif

/* ------------------------------------------------------------------ */
/* Private helpers & state                                            */
/* ------------------------------------------------------------------ */

#define SN_STR_LEN_MAX (12) // max SN length

typedef struct mdns_knx_record_t
{
  bool valid;
  struct knx
  {
    uint64_t iid;
    uint16_t ia;
    char sn[SN_STR_LEN_MAX + 1]; // +1 for null terminator
    bool pm;
    uint16_t sp;          
  } knx;

} mdns_knx_record_t;

static mdns_knx_record_t current_advertisement = 
{
  .valid = false,
  .knx = 
  {
      .iid = 0,
      .ia = 0,
      .sn = "", // serial number, default is empty string with termination '/0'
      .pm = false,
      .sp = 0,  // sleep period value, default is 0 -> value may be e.g. 30 (seconds) 
  }
};

/*
 * We keep the socket open between publishes so we can send goodbye + new
 * announce without a gap.  -1 means "not open".
 * IPv6-only per spec 2.6.1.2.1: "mDNS SHALL use UDP port 5353 with multicast
 * IP address FF02::FB for IPv6."
 */
static int mdns_sock6 = -1;

// Listener socket (port 5353, multicast group FF02::FB) for answering incoming mDNS queries.  -1 means "not open".
static int mdns_listen_sock6 = -1;

// Listener thread handle and termination flag
static volatile bool listener_running = false;
static bool listener_thread_created = false;
#ifdef _WIN32
static HANDLE listener_thread_handle = NULL;
#elif defined(__ZEPHYR__)
  // Native kernel thread instead of a pthread_t. Stack size mirrors the CoAP RX thread (RX_THREAD_STACK_SIZE) in connectivity_wifi.c.
  #define KNX_MDNS_LISTENER_STACK_SIZE 2048
  #define KNX_MDNS_LISTENER_PRIORITY 7 // mirrors RX_THREAD_PRIORITY in connectivity_wifi.c
  K_THREAD_STACK_DEFINE(knx_mdns_listener_stack, KNX_MDNS_LISTENER_STACK_SIZE);
  static struct k_thread knx_mdns_listener_thread_data;
#else
  static pthread_t listener_thread;
#endif

// constants for building mDNS records
#define KNX_SERVICE_TYPE "_knx._udp.local."
#define MDNS_BUF_SIZE 2048

// Scratch buffer for building mDNS packets (no heap allocation). Must be 32-bit aligned as required by mdns.h.
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
static int open_mdns_send_socket_ipv6(void)
{
  int sock = (int)socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0)
    return -1;

  // Allow address reuse (best effort)
  const unsigned int reuseaddr = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuseaddr, sizeof(reuseaddr));

  // Multicast hop limit = 1 (link-local)
  const int hops = 1;
  setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, (const char *)&hops, sizeof(hops));

  // Enable loopback so local listeners also see our packets
  const unsigned int loopback = 1;
  setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, (const char *)&loopback, sizeof(loopback));

  // Bind to [::]:0 -- ephemeral port, avoids conflict with existing mDNS
  struct sockaddr_in6 saddr6 = {0};
  saddr6.sin6_family = AF_INET6;
  saddr6.sin6_addr = in6addr_any;
  saddr6.sin6_port = htons(0);

  if (bind(sock, (struct sockaddr *)&saddr6, sizeof(saddr6))) 
  {
    OC_ERR("DNS-SD: bind() failed for IPv6 mDNS send socket!");
    #ifdef _WIN32
    closesocket(sock);
    #else
    close(sock);
    #endif
    return -1;
  }

  // Non-blocking
  #ifdef _WIN32
  unsigned long param = 1;
  ioctlsocket(sock, FIONBIO, &param);
  #else
  const int flags = fcntl(sock, F_GETFL, 0);
  fcntl(sock, F_SETFL, flags | O_NONBLOCK);
  #endif

  return sock;
}

/**
 * @brief Ensure the mDNS IPv6 socket is open.
 * Per spec 2.6.1.2.1: "mDNS SHALL use UDP port 5353 with multicast IP
 * address FF02::FB for IPv6."
 */
static int ensure_socket(void)
{
  if (mdns_sock6 < 0)
  {
    mdns_sock6 = open_mdns_send_socket_ipv6();
    if (mdns_sock6 < 0)
    {
      OC_ERR("DNS-SD: Failed to open mDNS IPv6 socket!");
      return -1;
    }
  }

  return 0;
}

/* ------------------------------------------------------------------ */
/* Announce / Goodbye                                                 */
/* ------------------------------------------------------------------ */

/**
 * @brief Send an DNS-SD mDNS multicast announcement (or goodbye with TTL=0).
 *
 * Records sent:
 *   answer:     PTR  _knx._udp.local. -> <sn>._knx._udp.local.
 *   additional: SRV  <sn>._knx._udp.local. -> knx-<sn>.local. : port
 *               AAAA knx-<sn>.local. -> <ipv6 address> (per spec 2.6.1.2.2 SHALL)
 *               PTR  _<sn_lower>._sub._knx._udp.local. -> instance
 *               PTR  _ia<iid>-<ia>._sub._knx._udp.local. -> instance
 *               PTR  _pm._sub._knx._udp.local. -> instance (if pm)
 *               TXT  <sn>._knx._udp.local.  SP=<seconds> (if sp set)
 */

#define MAX_MDNS_RECORD_SIZE 64
#define MAX_AAAA_RECORDS 4
#define MAX_IF_INDICES 8

/*
 * Collect unique unicast IPv6 interface indices from the endpoint list.
 * Returns the number of indices written into out[] (0..MAX_IF_INDICES).
 */
static int collect_if_indices(unsigned int out[MAX_IF_INDICES])
{
  int count = 0;
  const ip_context_t *ctx = get_ip_context_for_device();
  if (!ctx)
  {
    return 0;
  }
  const oc_endpoint_t *ep = (oc_endpoint_t *)oc_list_head(ctx->eps);
  while (ep && count < MAX_IF_INDICES)
  {
    if (ep->flags & IPV6
        && !(ep->flags & (MULTICAST | SECURED | TCP))
        && ep->interface_index > 0)
    {
      const unsigned int idx = (unsigned int)ep->interface_index;
      bool already = false;
      for (int j = 0; j < count; ++j)
      {
        if (out[j] == idx)
        {
          // interface already in list, skip it
          already = true;
          break;
        }
      }
      if (!already)
      {
        // interface not in list, add it, increase count
        out[count++] = idx;
      }
    }
    ep = ep->next;
  }
  return count;
}

/*
 * Populate out[] with AAAA records for every unicast IPv6 endpoint, binding
 * each record's name to hostname/hostname_len.
 * Returns the number of records written into out[] (0..MAX_AAAA_RECORDS).
 */
static int collect_aaaa_records(mdns_record_t out[MAX_AAAA_RECORDS],
                                const char *hostname, size_t hostname_len)
{
  int count = 0;
  const ip_context_t *ctx = get_ip_context_for_device();
  if (!ctx)
  {
    return 0;
  }
  const oc_endpoint_t *ep = (oc_endpoint_t *)oc_list_head(ctx->eps);
  while (ep && count < MAX_AAAA_RECORDS)
  {
    // pick non-multicast, non-secure, non-TCP IPv6 endpoints (unicast server)
    if (ep->flags & IPV6 && !(ep->flags & (MULTICAST | SECURED | TCP)))
    {
      out[count].name.str    = hostname;
      out[count].name.length = hostname_len;
      out[count].type        = MDNS_RECORDTYPE_AAAA;

      // wipe socket
      memset(&out[count].data.aaaa.addr, 0, sizeof(struct sockaddr_in6));

      // set socket properties family, ipv6, port
      out[count].data.aaaa.addr.sin6_family = AF_INET6;
      memcpy(&out[count].data.aaaa.addr.sin6_addr, ep->addr.ipv6.address, 16);
      out[count].data.aaaa.addr.sin6_port = htons(ep->addr.ipv6.port);

      count++;
    }
    ep = ep->next;
  }
  OC_DBG("DNS-SD: %d IPv6 endpoints found for AAAA records", count);
  return count;
}

static int send_announcement(mdns_knx_record_t* record, bool goodbye)
{
  if (ensure_socket() != 0) 
  {
    return -1;
  }

  // Instance: "<sn_lower>._knx._udp.local."  --> sn must be in lower case, spec 2.6.1.2.2
  char instance_name[MAX_MDNS_RECORD_SIZE];
  (void)snprintf(instance_name, sizeof(instance_name), "%s." KNX_SERVICE_TYPE, record->knx.sn);
  const size_t instance_len = strlen(instance_name);

  // Hostname: "knx-<sn_lower>.local."  (spec 2.6.1.2.3 SHOULD) 
  char hostname[MAX_MDNS_RECORD_SIZE];
  (void)snprintf(hostname, sizeof(hostname), "knx-%s.local.", record->knx.sn);
  const size_t hostname_len = strlen(hostname);

  // Subtype strings 
  char sub_sn[MAX_MDNS_RECORD_SIZE];
  (void)snprintf(sub_sn, sizeof(sub_sn), "_%s._sub._knx._udp.local.", record->knx.sn);

  char sub_ia[MAX_MDNS_RECORD_SIZE];
  (void)snprintf(sub_ia, sizeof(sub_ia), "_ia%" PRIx64 "-%x._sub._knx._udp.local.", record->knx.iid, (unsigned)record->knx.ia);

  #define sub_pm "_pm._sub._knx._udp.local."

  // port
  const uint16_t port = knx_dns_sd_get_used_port();

  // Answer: PTR _knx._udp.local. -> instance
  const mdns_record_t answer =
  {
    .name.str = KNX_SERVICE_TYPE,
    .name.length = strlen(KNX_SERVICE_TYPE),
    .type = MDNS_RECORDTYPE_PTR,
    .data.ptr.name.str = instance_name,
    .data.ptr.name.length = instance_len
  };

  // Additional records (max 10: SRV + up to 4 AAAA + 3 subtype PTRs + TXT) 
  mdns_record_t additional[6 + MAX_AAAA_RECORDS] = {0};

  // SRV 
  additional[0].name.str = instance_name;
  additional[0].name.length = instance_len;
  additional[0].type = MDNS_RECORDTYPE_SRV;
  additional[0].data.srv.priority = 0;
  additional[0].data.srv.weight = 0;
  additional[0].data.srv.port = port;
  additional[0].data.srv.name.str = hostname;
  additional[0].data.srv.name.length = hostname_len;
  
  size_t add_count = 1;

  // AAAA records
  const ip_context_t *ctx = get_ip_context_for_device();
  if (ctx) 
  {
    const oc_endpoint_t *ep = (oc_endpoint_t *)oc_list_head(ctx->eps);
    int aaaa_count = 0;
    
    while (ep && aaaa_count < MAX_AAAA_RECORDS) 
    {
      // pick non-multicast, non-secure, non-TCP IPv6 endpoints (unicast server) 
      if (ep->flags & IPV6 && !(ep->flags & (MULTICAST | SECURED | TCP))) 
      {
        additional[add_count].name.str = hostname;
        additional[add_count].name.length = hostname_len;
        additional[add_count].type = MDNS_RECORDTYPE_AAAA;
        
        // wipe socket
        memset(&additional[add_count].data.aaaa.addr, 0, sizeof(struct sockaddr_in6));

        // set socket properties family, ipv6, port
        additional[add_count].data.aaaa.addr.sin6_family = AF_INET6;
        memcpy(&additional[add_count].data.aaaa.addr.sin6_addr, ep->addr.ipv6.address, 16);
        additional[add_count].data.aaaa.addr.sin6_port = htons(ep->addr.ipv6.port);
        
        add_count++;  // next additional record 1..5
        aaaa_count++; // max AAAA records
      }

      ep = ep->next;
    }

    OC_DBG("DNS-SD: %d IPv6 endpoints found for AAAA records", aaaa_count);
  }

  // subtype PTR: serial number 
  additional[add_count].name.str = sub_sn;
  additional[add_count].name.length = strlen(sub_sn);
  additional[add_count].type = MDNS_RECORDTYPE_PTR;
  additional[add_count].data.ptr.name.str = instance_name;
  additional[add_count].data.ptr.name.length = instance_len;
  add_count++;

  // subtype PTR: installation-id + individual-address 
  additional[add_count].name.str = sub_ia;
  additional[add_count].name.length = strlen(sub_ia);
  additional[add_count].type = MDNS_RECORDTYPE_PTR;
  additional[add_count].data.ptr.name.str = instance_name;
  additional[add_count].data.ptr.name.length = instance_len;
  add_count++;

  // Subtype PTR: programming mode (conditional) 
  if (record->knx.pm) 
  {
    additional[add_count].name.str = sub_pm;
    additional[add_count].name.length = strlen(sub_pm);
    additional[add_count].type = MDNS_RECORDTYPE_PTR;
    additional[add_count].data.ptr.name.str = instance_name;
    additional[add_count].data.ptr.name.length = instance_len;
    add_count++;
  }

  // TXT: optional SP=<seconds> (conditional) 
  if (record->knx.sp != 0) 
  {
    // always places after last char a '\0' - even if sp is larger than 5 digits such as 123456 = 12345 + '\0' (truncated)
    char sp_str[5 + 1];
    (void)snprintf(sp_str, sizeof(sp_str), "%d", record->knx.sp);
    
    // we have a non-empty string 
    additional[add_count].name.str = instance_name;
    additional[add_count].name.length = instance_len;
    additional[add_count].type = MDNS_RECORDTYPE_TXT;
    additional[add_count].data.txt.key.str = "SP";
    additional[add_count].data.txt.key.length = 2;
    additional[add_count].data.txt.value.str = sp_str;
    additional[add_count].data.txt.value.length = strlen(sp_str);
    add_count++;
  }

  /*
   * Send multicast on every IPv6 interface.
   * Collect unique interface indices from the endpoint list,
   * then send the announcement once per interface via IPV6_MULTICAST_IF.
   * This ensures the packet reaches all local network segments.
   */
  
  // collect unique unicast IPv6 interface indices
  unsigned int if_indices[MAX_IF_INDICES];
  const int if_count = collect_if_indices(if_indices);

  if (if_count == 0)
  {
    OC_WRN("DNS-SD: No interfaces available for mDNS %s (network not ready?)!",  goodbye ? "goodbye" : "announce");
    return -1;
  }

  int summary = 0;

  for (int iface_idx = 0; iface_idx < if_count; iface_idx++) 
  {
    setsockopt(mdns_sock6, IPPROTO_IPV6, IPV6_MULTICAST_IF,
               (const char *)&if_indices[iface_idx], sizeof(if_indices[iface_idx]));

    int ret;
    if (goodbye) 
    {
      ret = mdns_goodbye_multicast(mdns_sock6, mdns_buf, sizeof(mdns_buf),
                                   answer, NULL, 0,
                                   additional, add_count);
    } 
    else 
    {
      ret = mdns_announce_multicast(mdns_sock6, mdns_buf, sizeof(mdns_buf),
                                    answer, NULL, 0,
                                    additional, add_count);
    }

    OC_DBG("DNS-SD: mDNS %s %s on interface %u (port %u, iid 0x%" PRIx64 ", ia 0x%x, pm=%d)", 
           goodbye ? "goodbye" : "announce",
           ret < 0 ? "failed" : "succeeded",
           if_indices[iface_idx], port, record->knx.iid, record->knx.ia, (int)record->knx.pm);

    // latch failure, once an interface fails, summary stays -1
    if (ret < 0) { summary = -1; }

  }
  return summary;
}


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
static int open_mdns_listen_socket_ipv6(void)
{
  int sock = (int)socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0)
  {
    OC_ERR("mDNS: socket() failed for listen socket!");
    return -1;
  }

  // Allow address reuse, required to coexist with Bonjour on port 5353
  unsigned int reuseaddr = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuseaddr, sizeof(reuseaddr));

  #ifdef SO_REUSEPORT
  setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, (const char *)&reuseaddr, sizeof(reuseaddr));
  #endif

  // Multicast hop limit = 1 (for responses)
  int hops = 1;
  setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, (const char *)&hops, sizeof(hops));

  // Enable loopback
  unsigned int loopback = 1;
  setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, (const char *)&loopback, sizeof(loopback));

  // Join FF02::FB multicast group -- try on each known interface. Interface 0 (any) often fails on Windows when Bonjour is running.
  struct ipv6_mreq mreq = {0};
  mreq.ipv6mr_multiaddr.s6_addr[0] = 0xFF;
  mreq.ipv6mr_multiaddr.s6_addr[1] = 0x02;
  mreq.ipv6mr_multiaddr.s6_addr[15] = 0xFB;

  int joined = 0;
  ip_context_t *ctx = get_ip_context_for_device();
  if (ctx) 
  {
    oc_endpoint_t *ep = (oc_endpoint_t *)oc_list_head(ctx->eps);
    // Collect unique interface indices
    unsigned int tried[MAX_IF_INDICES];
    int tried_count = 0;
    while (ep && tried_count < MAX_IF_INDICES) 
    {
      if (ep->flags & IPV6 && !(ep->flags & MULTICAST) && ep->interface_index > 0) 
      {
        const unsigned int idx = (unsigned int)ep->interface_index;
        bool dup = false;
        for (int j = 0; j < tried_count; ++j)
        {
          if (tried[j] == idx)
          {
            dup = true;
            break;
          }
        }

        if (!dup)
        {
          tried[tried_count++] = idx;
          mreq.ipv6mr_interface = idx;
          if (setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP,
                         (const char *)&mreq, sizeof(mreq)) == 0)
          {
            OC_INF("mDNS: Joined FF02::FB on interface %u.", idx);
            joined++;
          }
          else
          {
            OC_DBG("mDNS: IPV6_JOIN_GROUP failed on interface %u!", idx);
          }
        }
      }

      ep = ep->next;
    }
  }

  // Fallback: try interface 0 (any)
  if (joined == 0)
  {
    mreq.ipv6mr_interface = 0;
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP,
                   (const char *)&mreq, sizeof(mreq)) == 0)
    {
      OC_INF("mDNS: Joined FF02::FB on default interface.");
      joined++;
    }
  }

  if (joined == 0)
  {
    OC_ERR("mDNS: Could not join FF02::FB on any interface!");
    #ifdef _WIN32
    closesocket(sock);
    #else
    close(sock);
    #endif
    return -1;
  }

  // Bind to [::]:5353
  struct sockaddr_in6 saddr6 = {0};
  saddr6.sin6_family = AF_INET6;
  saddr6.sin6_addr = in6addr_any;
  saddr6.sin6_port = htons(MDNS_PORT);

  if (bind(sock, (struct sockaddr *)&saddr6, sizeof(saddr6)))
  {
    OC_ERR("mDNS: bind(:%d) failed for listen socket!", MDNS_PORT);
    #ifdef _WIN32
    closesocket(sock);
    #else
    close(sock);
    #endif
    return -1;
  }

  // Non-blocking
  #ifdef _WIN32
  unsigned long param = 1;
  ioctlsocket(sock, FIONBIO, &param);
  #else
  {
    const int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
  }
  #endif

  OC_INF("mDNS: Listen socket open on port %d (joined %d interfaces).", MDNS_PORT, joined);
  return sock;
}

/**
 * @brief Case-insensitive comparison of DNS name strings, ignoring trailing dots.
 */
static bool dns_name_equal(const char *a, size_t alen, const char *b, size_t blen)
{
  // strip trailing dots at the end of DNS name if present such as "_coap._udp.local." 
  if (alen > 0 && a[alen - 1] == '.')
  {
    alen--;
  }

  // strip trailing dots at the end of DNS name if present such as "_coap._udp.local." 
  if (blen > 0 && b[blen - 1] == '.')
  {
    blen--;
  }

  if (alen != blen)
  {
    // lengths differ, cannot be equal
    return false;
  }

  for (size_t i = 0; i < alen; i++)
  {
    // compare characters case-insensitively
    if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
    {
      return false;
    }
  }

  return true;
}

/**
 * @brief Check if the query name ends with "._sub._knx._udp.local" (case-insensitive).
 */
static bool is_knx_subtype_query(const char *qname, size_t q_len)
{
  const char* suffix = "._sub._knx._udp.local";
  const size_t s_len = strlen(suffix);
  
  // strip trailing dot at the end of qname if present such as "._sub._knx._udp.local."
  if (q_len > 0 && qname[q_len - 1] == '.')
  {
    q_len--;
  }

  if (q_len <= s_len)
  {
    /* 
      qname is shorter or equal to the suffix, cannot match 
      (equal since there must be at least one character before the suffix such as "a._sub._knx._udp.local")
    */
    return false;
  }

  for (size_t i = 0; i < s_len; i++)
  {
    /* 
       compares from the end of qname with the suffix 
       - example qname='my-device._sub._knx._udp.local', q_len = 31 with '._sub._knx._udp.local', s_len = 21
       - q_len - s_len + i = 31 - 21 + i = 10 + i, so we compare qname[10] = '_' with suffix[i] = '_' and further
    */
    if (tolower((unsigned char)qname[q_len - s_len + i]) != tolower((unsigned char)suffix[i]))
    {
      return false;
    }
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
static int send_query_response(int sock, const char *ptr_name)
{
  
  // Instance: "<sn_lower>._knx._udp.local."  --> sn must be in lower case, spec 2.6.1.2.2
  char instance_name[MAX_MDNS_RECORD_SIZE];
  (void)snprintf(instance_name, sizeof(instance_name), "%s." KNX_SERVICE_TYPE, current_advertisement.knx.sn);
  const size_t instance_len = strlen(instance_name);

  // Hostname: "knx-<sn_lower>.local."  (spec 2.6.1.2.3 SHOULD) 
  char hostname[MAX_MDNS_RECORD_SIZE];
  (void)snprintf(hostname, sizeof(hostname), "knx-%s.local.", current_advertisement.knx.sn);
  const size_t hostname_len = strlen(hostname);

  // Subtype strings 
  char sub_sn[MAX_MDNS_RECORD_SIZE];
  (void)snprintf(sub_sn, sizeof(sub_sn), "_%s._sub._knx._udp.local.", current_advertisement.knx.sn);
  
  char sub_ia[MAX_MDNS_RECORD_SIZE];
  (void)snprintf(sub_ia, sizeof(sub_ia), "_ia%" PRIx64 "-%x._sub._knx._udp.local.", current_advertisement.knx.iid, (unsigned)current_advertisement.knx.ia);
  
  // port
  const uint16_t port = knx_dns_sd_get_used_port();

  // Answer: PTR -- the name must match the query the client sent
  const mdns_record_t answer =
  {
    .name.str = ptr_name,
    .name.length = strlen(ptr_name),
    .type = MDNS_RECORDTYPE_PTR,
    .data.ptr.name.str = instance_name,
    .data.ptr.name.length = instance_len
  };

  // Additional records (max 10: SRV + up to 4 AAAA + 3 subtype PTRs + TXT) 
  mdns_record_t additional[6 + MAX_AAAA_RECORDS] = {0};

  // SRV
  additional[0].name.str = instance_name;
  additional[0].name.length = instance_len;
  additional[0].type = MDNS_RECORDTYPE_SRV;
  additional[0].data.srv.priority = 0;
  additional[0].data.srv.weight = 0;
  additional[0].data.srv.port = port;
  additional[0].data.srv.name.str = hostname;
  additional[0].data.srv.name.length = hostname_len;
 
  size_t add_count = 1;

  // AAAA records
  const ip_context_t *ctx = get_ip_context_for_device();
  if (ctx)
  {
    const oc_endpoint_t *ep = (oc_endpoint_t *)oc_list_head(ctx->eps);
    int aaaa_count = 0;

    while (ep && aaaa_count < MAX_AAAA_RECORDS)
    {
      // pick non-multicast, non-secure, non-TCP IPv6 endpoints (unicast server)
      if (ep->flags & IPV6 && !(ep->flags & (MULTICAST | SECURED | TCP)))
      {
        additional[add_count].name.str = hostname;
        additional[add_count].name.length = hostname_len;
        additional[add_count].type = MDNS_RECORDTYPE_AAAA;

        // wipe socket
        memset(&additional[add_count].data.aaaa.addr, 0, sizeof(struct sockaddr_in6));

        // set socket properties family, ipv6, port
        additional[add_count].data.aaaa.addr.sin6_family = AF_INET6;
        memcpy(&additional[add_count].data.aaaa.addr.sin6_addr, ep->addr.ipv6.address, 16);
        additional[add_count].data.aaaa.addr.sin6_port = htons(ep->addr.ipv6.port);

        add_count++;  // next additional record 1..5
        aaaa_count++; // max AAAA records
      }

      ep = ep->next;
    }

    OC_DBG("DNS-SD: %d IPv6 endpoints found for AAAA records", aaaa_count);
  }
  

  // subtype PTR: serial number 
  additional[add_count].name.str = sub_sn;
  additional[add_count].name.length = strlen(sub_sn);
  additional[add_count].type = MDNS_RECORDTYPE_PTR;
  additional[add_count].data.ptr.name.str = instance_name;
  additional[add_count].data.ptr.name.length = instance_len;
  add_count++;

  // subtype PTR: installation-id + individual-address 
  additional[add_count].name.str = sub_ia;
  additional[add_count].name.length = strlen(sub_ia);
  additional[add_count].type = MDNS_RECORDTYPE_PTR;
  additional[add_count].data.ptr.name.str = instance_name;
  additional[add_count].data.ptr.name.length = instance_len;
  add_count++;

  // subtype PTR: programming mode (conditional) 
  if (current_advertisement.knx.pm) 
  {
    additional[add_count].name.str = sub_pm;
    additional[add_count].name.length = strlen(sub_pm);
    additional[add_count].type = MDNS_RECORDTYPE_PTR;
    additional[add_count].data.ptr.name.str = instance_name;
    additional[add_count].data.ptr.name.length = instance_len;
    add_count++;
  }

  // TXT: optional SP=<seconds> (conditional) 
  if (current_advertisement.knx.sp != 0) 
  {
    // we have a non-zero sleep period

    // always places after last char a '\0' - even if sp is larger than 5 digits such as 123456 = 12345 + '\0' (truncated)
    char sp_str[5 + 1];
    (void) snprintf(sp_str, sizeof(sp_str), "%d", current_advertisement.knx.sp);

    additional[add_count].name.str = instance_name;
    additional[add_count].name.length = instance_len;
    additional[add_count].type = MDNS_RECORDTYPE_TXT;
    additional[add_count].data.txt.key.str = "SP";
    additional[add_count].data.txt.key.length = 2;
    additional[add_count].data.txt.value.str = sp_str;
    additional[add_count].data.txt.value.length = strlen(sp_str);
    add_count++;
  }

  // use a stack buffer for the response
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
 * @brief Send an SRV answer for our service instance.
 *
 * Answer:      SRV {instance}._knx._udp.local. -> knx-{sn}.local. : port
 * Additional:  AAAA knx-{sn}.local. -> IPv6 addresses
 */
static int send_srv_response(int sock)
{
  // Instance: "<sn_lower>._knx._udp.local."  --> sn must be in lower case, spec 2.6.1.2.2
  char instance_name[MAX_MDNS_RECORD_SIZE];
  (void)snprintf(instance_name, sizeof(instance_name), "%s." KNX_SERVICE_TYPE, current_advertisement.knx.sn);
  const size_t instance_len = strlen(instance_name);

  // Hostname: "knx-<sn_lower>.local."  (spec 2.6.1.2.3 SHOULD)
  char hostname[MAX_MDNS_RECORD_SIZE];
  (void)snprintf(hostname, sizeof(hostname), "knx-%s.local.", current_advertisement.knx.sn);
  const size_t hostname_len = strlen(hostname);

  // port
  const uint16_t port = knx_dns_sd_get_used_port();

  // SRV {instance}._knx._udp.local. -> knx-{sn}.local. : port
  const mdns_record_t answer =
  {
    .name.str = instance_name,
    .name.length = instance_len,
    .type = MDNS_RECORDTYPE_SRV,
    .data.srv.priority = 0,
    .data.srv.weight = 0,
    .data.srv.port = port,
    .data.srv.name.str = hostname,
    .data.srv.name.length = hostname_len
  };

  // collect all AAAA records
  mdns_record_t aaaa_records[MAX_AAAA_RECORDS] = {0};
  const int aaaa_count = collect_aaaa_records(aaaa_records, hostname, hostname_len);

  // use a stack buffer for the response
  #ifdef _MSC_VER
  __declspec(align(4)) char resp_buf[MDNS_BUF_SIZE];
  #else
  char resp_buf[MDNS_BUF_SIZE] __attribute__((aligned(4)));
  #endif

  // first the answer; then remaining AAAA records (if any)
  const mdns_record_t* add_ptr = aaaa_count > 0 ? aaaa_records : NULL;
  const size_t add_count       = aaaa_count;
  
  return mdns_query_answer_multicast(sock, resp_buf, sizeof(resp_buf),
                                     answer, NULL, 0,
                                     add_ptr, add_count);
}

/**
 * @brief Send an AAAA answer for our hostname.
 *
 * Answer:      AAAA knx-{sn}.local. -> first IPv6 address
 * Additional:  remaining AAAA records (if more than one address)
 */
static int send_aaaa_response(int sock)
{
  // Hostname: "knx-<sn_lower>.local."  (spec 2.6.1.2.3 SHOULD)
  char hostname[MAX_MDNS_RECORD_SIZE];
  (void)snprintf(hostname, sizeof(hostname), "knx-%s.local.", current_advertisement.knx.sn);
  const size_t hostname_len = strlen(hostname);

  // collect all AAAA records
  mdns_record_t aaaa_records[MAX_AAAA_RECORDS] = {0};
  const int aaaa_count = collect_aaaa_records(aaaa_records, hostname, hostname_len);

  if (aaaa_count == 0)
  {
    OC_WRN("DNS-SD: No IPv6 addresses to respond with for AAAA query!");
    return -1;
  }

  // first AAAA record is the answer, rest go in additional
  const mdns_record_t answer = aaaa_records[0];

  // use a stack buffer for the response
  #ifdef _MSC_VER
  __declspec(align(4)) char resp_buf[MDNS_BUF_SIZE];
  #else
  char resp_buf[MDNS_BUF_SIZE] __attribute__((aligned(4)));
  #endif

  // first AAAA is the answer; remaining ones (if any) go in additional
  const mdns_record_t *add_ptr = aaaa_count > 1 ? &aaaa_records[1] : NULL;
  const size_t add_count       = aaaa_count - 1; // 1 -> 0, 2 -> 1, ...

  return mdns_query_answer_multicast(sock, resp_buf, sizeof(resp_buf),
                                        answer, NULL, 0,
                                        add_ptr, add_count);
}

/**
 * @brief mdns_socket_listen callback — invoked for each question or record
 *        in a received mDNS packet.
 *
 * Responds to:
 *   - PTR/ANY queries for _knx._udp.local. (general service browse)
 *   - PTR/ANY queries for _pm._sub._knx._udp.local. (programming mode browse)
 *   - PTR/ANY queries for _<sn>._sub._knx._udp.local. (serial number browse)
 *   - PTR/ANY queries for _ia<iid>-<ia>._sub._knx._udp.local. (IA browse)
 *   - SRV/ANY queries for <sn>._knx._udp.local. (instance → hostname+port)
 *   - AAAA/ANY queries for knx-<sn>.local. (hostname → IPv6 address)
 */
static int mdns_query_callback(int sock, const struct sockaddr *from, size_t addrlen,
                               mdns_entry_type_t entry, uint16_t query_id,
                               uint16_t rtype, uint16_t rclass, uint32_t ttl,
                               const void *data, size_t size,
                               size_t name_offset, size_t name_length,
                               size_t record_offset, size_t record_length,
                               void *user_data)
{
  (void)from; (void)addrlen; (void)query_id; (void)rclass; (void)ttl;
  (void)record_offset; (void)record_length; (void)user_data;

  // Only interested in questions
  if (entry != MDNS_ENTRYTYPE_QUESTION)
  {
    return 0;
  }

  // must have a published service
  if (!current_advertisement.valid)
  {
    OC_ERR("DNS-SD: Invalid 'current' advertisement found!");
    return 0;
  }

  // filter to record types we handle
  if (rtype != MDNS_RECORDTYPE_PTR 
      && rtype != MDNS_RECORDTYPE_SRV
      && rtype != MDNS_RECORDTYPE_AAAA 
      && rtype != MDNS_RECORDTYPE_ANY)
  {
    return 0;
  }

  // extract queried name
  char qname[256];
  const mdns_string_t qstr = mdns_string_extract(data, size, &name_offset, qname, sizeof(qname));
  if (qstr.length == 0) 
  {
    return 0;
  }

  // Instance: "<sn_lower>._knx._udp.local."  --> sn must be in lower case, spec 2.6.1.2.2
  char instance_name[MAX_MDNS_RECORD_SIZE];
  (void)snprintf(instance_name, sizeof(instance_name), "%s." KNX_SERVICE_TYPE, current_advertisement.knx.sn);

  // Hostname: "knx-<sn_lower>.local."  (spec 2.6.1.2.3 SHOULD) 
  char hostname[MAX_MDNS_RECORD_SIZE];
  (void)snprintf(hostname, sizeof(hostname), "knx-%s.local.", current_advertisement.knx.sn);

  int ret;

  // ---- 1. PTR / ANY: service browse & subtype browse ----
  if (rtype == MDNS_RECORDTYPE_PTR || rtype == MDNS_RECORDTYPE_ANY)
  {
    bool ptr_respond = false;

    // 1a. General service browse: _knx._udp.local.
    if (dns_name_equal(qname, qstr.length, KNX_SERVICE_TYPE, strlen(KNX_SERVICE_TYPE)))
    {
      OC_INF("DNS-SD: Received browse query for %s.", KNX_SERVICE_TYPE);
      ptr_respond = true;
    }

    // 1b. Subtype queries: *._sub._knx._udp.local.
    if (!ptr_respond && is_knx_subtype_query(qname, qstr.length))
    {
      // _pm subtype
      if (current_advertisement.knx.pm)
      {
        const char *pm_sub = "_pm._sub._knx._udp.local.";
        if (dns_name_equal(qname, qstr.length, pm_sub, strlen(pm_sub)))
        {
          OC_INF("DNS-SD: Received _pm subtype query, device is in programming mode.");
          ptr_respond = true;
        }
      }

      // _<sn> subtype
      if (!ptr_respond)
      {
        char sub_sn[MAX_MDNS_RECORD_SIZE];
        (void)snprintf(sub_sn, sizeof(sub_sn), "_%s._sub._knx._udp.local.", current_advertisement.knx.sn);
        if (dns_name_equal(qname, qstr.length, sub_sn, strlen(sub_sn)))
        {
          OC_INF("DNS-SD: Received serial number subtype query.");
          ptr_respond = true;
        }
      }

      // _ia<iid>-<ia> subtype
      if (!ptr_respond)
      {
        char sub_ia[MAX_MDNS_RECORD_SIZE];
        (void)snprintf(sub_ia, sizeof(sub_ia),
                       "_ia%" PRIx64 "-%x._sub._knx._udp.local.",
                       current_advertisement.knx.iid, (unsigned)current_advertisement.knx.ia);
        if (dns_name_equal(qname, qstr.length, sub_ia, strlen(sub_ia)))
        {
          OC_INF("DNS-SD: Received IA subtype query.");
          ptr_respond = true;
        }
      }

      if (!ptr_respond)
      {
        OC_DBG("DNS-SD: Received _sub query for unmatched subtype: %.*s", (int)qstr.length, qname);
      }
    }

    if (ptr_respond)
    {
      // use the queried name as PTR answer name
      char ptr_name[256];
      const size_t ptr_len = qstr.length >= sizeof(ptr_name) ? sizeof(ptr_name) - 1 : qstr.length;

      memcpy(ptr_name, qname, ptr_len);
      ptr_name[ptr_len] = '\0';

      if (ptr_len > 0 && ptr_name[ptr_len - 1] != '.' && ptr_len + 1 < sizeof(ptr_name))
      {
        // add trailing dot if not present
        ptr_name[ptr_len] = '.';
        ptr_name[ptr_len + 1] = '\0';
      }

      ret = send_query_response(sock, ptr_name);
      if (ret < 0)
      {
        OC_ERR("DNS-SD: Failed to send PTR answer (ret=%d)!", ret);
      }
      else
      {
        OC_INF("DNS-SD: PTR answer sent for %s", ptr_name);
      }
      return 0;
    }
  }

  // ---- 2. SRV / ANY: instance name -> hostname + port ----
  if (rtype == MDNS_RECORDTYPE_SRV || rtype == MDNS_RECORDTYPE_ANY)
  {
    if (dns_name_equal(qname, qstr.length, instance_name, strlen(instance_name)))
    {
      OC_INF("DNS-SD: Received SRV query for %s.", instance_name);
      ret = send_srv_response(sock);
      if (ret < 0)
      {
        OC_ERR("DNS-SD: Failed to send SRV answer (ret=%d)!", ret);
      }
      else
      {
        OC_INF("DNS-SD: SRV answer sent for %s.", instance_name);
      }
      return 0;
    }
  }

  // ---- 3. AAAA / ANY: hostname -> IPv6 address ----
  if (rtype == MDNS_RECORDTYPE_AAAA || rtype == MDNS_RECORDTYPE_ANY)
  {
    if (dns_name_equal(qname, qstr.length, hostname, strlen(hostname)))
    {
      OC_INF("DNS-SD: Received AAAA query for %s.", hostname);
      ret = send_aaaa_response(sock);
      if (ret < 0)
      {
        OC_ERR("DNS-SD: Failed to send AAAA answer (ret=%d)!", ret);
      }
      else
      {
        OC_INF("DNS-SD: AAAA answer sent for %s.", hostname);
      }
      return 0;
    }
  }

  return 0; // unmatched query -- ignore
}

/**
 * @brief Listener thread entry point.
 *
 * Runs in a loop calling mdns_socket_listen() which does a blocking-ish
 * recvfrom().  The socket is non-blocking (set by mdns_socket_open_ipv6),
 * so we add a small sleep to avoid busy-looping when no packets arrive.
 */
#ifdef _WIN32
static DWORD WINAPI mdns_listener_thread(LPVOID param)
#elif defined(__ZEPHYR__)
static void mdns_listener_thread(void *param, void *p2, void *p3)
#else
static void* mdns_listener_thread(void *param)
#endif
{
  (void)param;
#ifdef __ZEPHYR__
  (void)p2;
  (void)p3;
#endif
  OC_INF("DNS-SD: mDNS listener thread started on port %d.", MDNS_PORT);

  while (listener_running)
  {
    size_t parsed = mdns_socket_listen(mdns_listen_sock6, mdns_listen_buf,
                                       sizeof(mdns_listen_buf),
                                       mdns_query_callback, NULL);
    if (parsed == 0)
    {
      // No packet received -- sleep briefly to avoid busy loop
#ifdef _WIN32
      Sleep(100);
#elif defined(__ZEPHYR__)
      k_msleep(100);
#else
      usleep(100000);
#endif
    }
  }

  OC_INF("DNS-SD: mDNS listener thread stopped.");
#ifdef _WIN32
  return 0;
#elif defined(__ZEPHYR__)
  return;
#else
  return NULL;
#endif
}

/**
 * @brief Start the mDNS listener thread (if not already running).
 */
static void start_listener(void)
{
  if (listener_running) 
  {
    return;
  }

  mdns_listen_sock6 = open_mdns_listen_socket_ipv6();
  if (mdns_listen_sock6 < 0) 
  {
    OC_ERR("DNS-SD: Query responder not started (mDNS listen socket unavailable)!");
    return;
  }

  listener_running = true;

  #ifdef _WIN32
  listener_thread_handle = CreateThread(NULL, 0, mdns_listener_thread, NULL, 0, NULL);
  if (listener_thread_handle == NULL)
  {
  #elif defined(__ZEPHYR__)
  k_tid_t tid = k_thread_create(&knx_mdns_listener_thread_data,
                                knx_mdns_listener_stack,
                                K_THREAD_STACK_SIZEOF(knx_mdns_listener_stack),
                                mdns_listener_thread, NULL, NULL, NULL,
                                KNX_MDNS_LISTENER_PRIORITY, 0, K_NO_WAIT);
  if (tid == NULL)
  {
  #else
  if (pthread_create(&listener_thread, NULL, mdns_listener_thread, NULL) != 0)
  {
  #endif
    OC_ERR("DNS-SD: Failed to create mDNS listener thread!");
    listener_running = false;
    mdns_socket_close(mdns_listen_sock6);
    mdns_listen_sock6 = -1;
    return;
  }

  #ifdef __ZEPHYR__
  k_thread_name_set(tid, "knx_mdns_listener");
  #endif
  listener_thread_created = true;

  OC_INF("DNS-SD: DNS-SD query responder started.");
}

/**
 * @brief Stop the mDNS listener thread and close the listen socket.
 */
static void stop_listener(void)
{
  if (!listener_running) 
  {
    return;
  }

  listener_running = false;

  if (listener_thread_created)
  {
    #ifdef _WIN32
    WaitForSingleObject(listener_thread_handle, 2000);
    CloseHandle(listener_thread_handle);
    listener_thread_handle = NULL;
    #elif defined(__ZEPHYR__)
    k_thread_join(&knx_mdns_listener_thread_data, K_FOREVER);
    #else
    pthread_join(listener_thread, NULL);
    #endif
  }

  listener_thread_created = false;

  if (mdns_listen_sock6 >= 0)
  {
    mdns_socket_close(mdns_listen_sock6);
    mdns_listen_sock6 = -1;
  }

  OC_INF("DNS-SD: DNS-SD query responder stopped.");
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

int knx_dns_sd_update_service(char *serial_no, uint64_t iid, uint16_t ia, bool pm)
{
  
  if (!serial_no)
  {
    OC_WRN("DNS-SD: Invalid (NULL) serial number provided, cannot update service.");
    return -1;
  }
  
  // goodbye for current_advertisement advertisement (if any, 'goodbye' may be already send out on stop listener)
  if (current_advertisement.valid) 
  {
    (void)send_announcement(&current_advertisement, true);
  }

  /*
     assign new announcement data
     sn:
     - new.knx.sn is already zero-initialised (C99 partial init rule);
     - copying exactly SN_STR_LEN_MAX bytes is always safe because sn[SN_STR_LEN_MAX](index 12)
       is never written and stays '\0', moreover sn with less than 12 bytes also have a terminator
       right after the last character, so strlen() is safe to use on it
     - safe for sn len > 12 ; copy stops at 12, sn[12] is '\0' and strlen() returns 12
     sp:
     - new.knx.sp is taken over from previous announcement, as long as it is not changed from outside 
       it stays
  */

  mdns_knx_record_t new = 
  {
    .valid = true,
    .knx.iid = iid, 
    .knx.ia = ia, 
    .knx.pm = pm, 
    .knx.sp = current_advertisement.knx.sp
  };

  memcpy(new.knx.sn, serial_no, SN_STR_LEN_MAX);

  // new announcement
  const int ret = send_announcement(&new, false);

  /* 
     remember for next goodbye + for listener callback, on success store
     - if failed and goodbye was successful -> no announcement is active
     - if failed and goodbye was NOT successful -> current_advertisement is unchanged (still valid) 
       and listener callback will still respond with old data 
       -> this is the best we can do, we cannot roll back the goodbye
  */
  if (ret == 0)
  {
    current_advertisement = new;
  }

  OC_DBG("DNS-SD: announcement %s", ret == 0 ? "succeeded" : "failed");

  // start the query-responder thread (idempotent -- only starts once)
  start_listener();

  return ret;
}

void knx_dns_sd_set_sleep_period(uint16_t sp)
{
  if (sp == 0) 
  {
    /* 
       SP = 0 and previous SP was NOT 0
       
       When clearing SP, send a goodbye (TTL=0) for the old SP TXT record so that mDNS listeners evict it 
       immediately (RFC 6762 11.x). A plain re-announcement WITHOUT the 'SP' key is not enough -> listeners keep 
       the cached record until its TTL expires.
    */
    if (current_advertisement.valid && current_advertisement.knx.sp != 0)
    {
      if (ensure_socket() == 0)
      {
        // build a standalone TXT goodbye for the SP record
        char instance_name[MAX_MDNS_RECORD_SIZE];
        (void)snprintf(instance_name, sizeof(instance_name), "%s." KNX_SERVICE_TYPE,
                       current_advertisement.knx.sn);
        
        // always places after last char a '\0' - even if sp is larger than 5 digits such as 123456 = 12345 + '\0' (truncated)
        char sp_str[5 +1]; 
        (void)snprintf(sp_str, sizeof(sp_str), "%d", current_advertisement.knx.sp); 

        const mdns_record_t sp_txt =
        {
          .name.str    = instance_name,
          .name.length = strlen(instance_name),
          .type        = MDNS_RECORDTYPE_TXT,
          .data.txt.key.str       = "SP",
          .data.txt.key.length    = 2,
          .data.txt.value.str     = sp_str,
          .data.txt.value.length  = strlen(sp_str)
        };

        // send SP TXT goodbye (TTL=0) on every active interface
        unsigned int if_indices[MAX_IF_INDICES];
        const int if_count = collect_if_indices(if_indices);
        for (int i = 0; i < if_count; i++)
        {
          setsockopt(mdns_sock6, IPPROTO_IPV6, IPV6_MULTICAST_IF,(const char *)&if_indices[i], sizeof(if_indices[i]));

          (void)mdns_goodbye_multicast(mdns_sock6, mdns_buf, sizeof(mdns_buf),
                                       sp_txt,
                                       NULL, 0,
                                       NULL, 0);
        }
      }
    }
  }
  
  // set SP in current advertisement (0 or > 0)
  current_advertisement.knx.sp = sp;

  // reannounce immediately so the updated TXT record (SP=<n> or empty) is picked up w/o the caller having to call knx_dns_sd_update_service separately
  if (current_advertisement.valid) 
  {
    (void)send_announcement(&current_advertisement, false);
  }

  OC_INF("DNS-SD: DNS-SD sleep period set to %d.", sp);
}

uint16_t knx_dns_sd_get_used_port(void)
{
  return get_ip_context_for_device()->port;
}

void knx_dns_sd_stop(void)
{
  // send goodbye for current advertisement
  if (current_advertisement.valid) 
  {
    (void)send_announcement(&current_advertisement, true);
    current_advertisement.valid = false;
  }

  // stop the listener thread
  stop_listener();

  // close the send socket
  if (mdns_sock6 >= 0) 
  {
    #ifdef _WIN32
    closesocket(mdns_sock6);
    #else
    close(mdns_sock6);
    #endif
    mdns_sock6 = -1;
  }
}
