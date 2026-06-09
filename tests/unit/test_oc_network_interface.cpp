/*
 * Unit tests for port/linux/oc_network_interface.c — interface enumeration
 * and the interface-filter getter/setter.
 *
 * Covers:
 *   oc_network_enumerate_interfaces  — argument guards + real getifaddrs() call
 *   oc_network_set_interface_filter  — stores the filter, returns true
 *   oc_network_get_interface_filter  — returns the stored filter
 *
 * The enumeration result depends on the host's NICs (it calls getifaddrs),
 * so the positive enumeration test only asserts invariants (0..max, names
 * NUL-terminated) rather than an exact interface list.
 */

#include <gtest/gtest.h>

extern "C" {
#include "port/oc_network_interface.h"
}

TEST(OcNetworkInterface, EnumerateRejectsNullBuffer)
{
  EXPECT_EQ(oc_network_enumerate_interfaces(nullptr, 4), 0);
}

TEST(OcNetworkInterface, EnumerateRejectsNonPositiveMax)
{
  oc_network_interface_info_t ifs[4];
  EXPECT_EQ(oc_network_enumerate_interfaces(ifs, 0), 0);
  EXPECT_EQ(oc_network_enumerate_interfaces(ifs, -1), 0);
}

TEST(OcNetworkInterface, EnumerateReturnsWithinBounds)
{
  oc_network_interface_info_t ifs[8];
  int n = oc_network_enumerate_interfaces(ifs, 8);
  /* Host-dependent count, but must be a sane, bounded value. */
  EXPECT_GE(n, 0);
  EXPECT_LE(n, 8);
  for (int i = 0; i < n; i++) {
    /* Names are strncpy'd with a reserved final NUL slot. */
    EXPECT_EQ(ifs[i].name[OC_NETWORK_IF_NAME_MAX - 1], '\0');
    /* Enumerated interfaces are reported up (loopback/down are skipped). */
    EXPECT_TRUE(ifs[i].is_up);
  }
}

TEST(OcNetworkInterface, EnumerateRespectsMaxInterfaces)
{
  oc_network_interface_info_t one[1];
  int n = oc_network_enumerate_interfaces(one, 1);
  EXPECT_GE(n, 0);
  EXPECT_LE(n, 1);
}

TEST(OcNetworkInterface, FilterSetAndGetRoundTrip)
{
  uint32_t saved = oc_network_get_interface_filter();

  EXPECT_TRUE(oc_network_set_interface_filter(5));
  EXPECT_EQ(oc_network_get_interface_filter(), 5u);

  EXPECT_TRUE(oc_network_set_interface_filter(0));
  EXPECT_EQ(oc_network_get_interface_filter(), 0u);

  /* Restore whatever the filter was before this test. */
  oc_network_set_interface_filter(saved);
}
