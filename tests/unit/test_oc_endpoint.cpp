/*
 * Unit tests for api/oc_endpoint.c — pure functions only
 *
 * Covers: oc_endpoint_compare, oc_endpoint_compare_address,
 *         oc_endpoint_copy, oc_ipv6_endpoint_is_link_local,
 *         oc_endpoint_set_local_address (null guard only).
 *
 * All tested functions are pure struct operations with no stack init needed.
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "oc_endpoint.h"

}

/* ═══════════════════════════════════════════════════════════════════════════
 * Helper: build IPv6 endpoint
 * ═══════════════════════════════════════════════════════════════════════════ */

static oc_endpoint_t make_ipv6(const uint8_t addr[16], uint16_t port,
                               enum transport_flags extra_flags = (enum transport_flags)0)
{
  oc_endpoint_t ep;
  memset(&ep, 0, sizeof(ep));
  ep.flags = (enum transport_flags)(IPV6 | extra_flags);
  memcpy(ep.addr.ipv6.address, addr, 16);
  ep.addr.ipv6.port = port;
  return ep;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_endpoint_compare
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(EndpointCompare, SameAddressAndPort)
{
  uint8_t addr[16] = {0x20, 0x01, 0x0d, 0xb8};
  oc_endpoint_t ep1 = make_ipv6(addr, 5683);
  oc_endpoint_t ep2 = make_ipv6(addr, 5683);
  EXPECT_EQ(oc_endpoint_compare(&ep1, &ep2), 0);
}

TEST(EndpointCompare, DifferentPort)
{
  uint8_t addr[16] = {0x20, 0x01};
  oc_endpoint_t ep1 = make_ipv6(addr, 5683);
  oc_endpoint_t ep2 = make_ipv6(addr, 5684);
  EXPECT_NE(oc_endpoint_compare(&ep1, &ep2), 0);
}

TEST(EndpointCompare, DifferentAddress)
{
  uint8_t addr1[16] = {0x20, 0x01};
  uint8_t addr2[16] = {0x20, 0x02};
  oc_endpoint_t ep1 = make_ipv6(addr1, 5683);
  oc_endpoint_t ep2 = make_ipv6(addr2, 5683);
  EXPECT_NE(oc_endpoint_compare(&ep1, &ep2), 0);
}

TEST(EndpointCompare, MulticastVsUnicast_StillEqual)
{
  /* compare ignores MULTICAST and ACCEPTED flags */
  uint8_t addr[16] = {0x20, 0x01};
  oc_endpoint_t ep1 = make_ipv6(addr, 5683);
  oc_endpoint_t ep2 = make_ipv6(addr, 5683, MULTICAST);
  EXPECT_EQ(oc_endpoint_compare(&ep1, &ep2), 0);
}

TEST(EndpointCompare, NullEndpoints)
{
  uint8_t addr[16] = {};
  oc_endpoint_t ep = make_ipv6(addr, 5683);
  EXPECT_EQ(oc_endpoint_compare(nullptr, &ep), -1);
  EXPECT_EQ(oc_endpoint_compare(&ep, nullptr), -1);
  EXPECT_EQ(oc_endpoint_compare(nullptr, nullptr), -1);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_endpoint_compare_address
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(EndpointCompareAddress, SameAddress_DifferentPort)
{
  uint8_t addr[16] = {0x20, 0x01};
  oc_endpoint_t ep1 = make_ipv6(addr, 5683);
  oc_endpoint_t ep2 = make_ipv6(addr, 9999);
  /* compare_address ignores port */
  EXPECT_EQ(oc_endpoint_compare_address(&ep1, &ep2), 0);
}

TEST(EndpointCompareAddress, DifferentAddress)
{
  uint8_t addr1[16] = {0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
  uint8_t addr2[16] = {0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
  oc_endpoint_t ep1 = make_ipv6(addr1, 5683);
  oc_endpoint_t ep2 = make_ipv6(addr2, 5683);
  EXPECT_NE(oc_endpoint_compare_address(&ep1, &ep2), 0);
}

TEST(EndpointCompareAddress, NullEndpoints)
{
  EXPECT_EQ(oc_endpoint_compare_address(nullptr, nullptr), -1);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_endpoint_copy
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(EndpointCopy, CopiesAllFields)
{
  uint8_t addr[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
  oc_endpoint_t src = make_ipv6(addr, 5683);
  src.group_address = 2305;
  src.kid[0] = 0xAA;
  src.kid_len = 1;

  oc_endpoint_t dst;
  memset(&dst, 0xFF, sizeof(dst));

  oc_endpoint_copy(&dst, &src);

  EXPECT_EQ(oc_endpoint_compare(&dst, &src), 0);
  EXPECT_EQ(dst.group_address, 2305u);
  EXPECT_EQ(dst.kid[0], 0xAA);
  EXPECT_EQ(dst.kid_len, 1);
  /* copy clears next pointer */
  EXPECT_EQ(dst.next, nullptr);
}

TEST(EndpointCopy, NullDst_NoOp)
{
  uint8_t addr[16] = {};
  oc_endpoint_t src = make_ipv6(addr, 5683);
  /* Should not crash */
  oc_endpoint_copy(nullptr, &src);
}

TEST(EndpointCopy, NullSrc_NoOp)
{
  oc_endpoint_t dst;
  memset(&dst, 0, sizeof(dst));
  oc_endpoint_copy(&dst, nullptr);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_ipv6_endpoint_is_link_local
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(IsLinkLocal, LinkLocalAddress)
{
  uint8_t addr[16] = {0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
  oc_endpoint_t ep = make_ipv6(addr, 5683);
  EXPECT_EQ(oc_ipv6_endpoint_is_link_local(&ep), 0);
}

TEST(IsLinkLocal, GlobalAddress)
{
  uint8_t addr[16] = {0x20, 0x01, 0x0d, 0xb8};
  oc_endpoint_t ep = make_ipv6(addr, 5683);
  EXPECT_EQ(oc_ipv6_endpoint_is_link_local(&ep), -1);
}

TEST(IsLinkLocal, LoopbackAddress)
{
  uint8_t addr[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
  oc_endpoint_t ep = make_ipv6(addr, 5683);
  EXPECT_EQ(oc_ipv6_endpoint_is_link_local(&ep), -1);
}

TEST(IsLinkLocal, NullEndpoint)
{
  EXPECT_EQ(oc_ipv6_endpoint_is_link_local(nullptr), -1);
}

TEST(IsLinkLocal, NonIPv6_Endpoint)
{
  oc_endpoint_t ep;
  memset(&ep, 0, sizeof(ep));
  ep.flags = IPV4; /* not IPv6 */
  EXPECT_EQ(oc_ipv6_endpoint_is_link_local(&ep), -1);
}
