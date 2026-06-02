/*
 * Unit tests for api/oc_knx_sec.c — pure functions only
 *
 * Covers: oc_at_profile_to_string, oc_knx_contains_interface
 *
 * These are the only functions in oc_knx_sec.c testable without
 * full stack initialization (no oc_main_init, no storage, no device).
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "api/oc_knx_sec.h"

}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_at_profile_to_string
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(AtProfileToString, CoapOscore)
{
  EXPECT_STREQ(oc_at_profile_to_string(OC_PROFILE_COAP_OSCORE), "coap_oscore");
}

TEST(AtProfileToString, CoapDtls)
{
  EXPECT_STREQ(oc_at_profile_to_string(OC_PROFILE_COAP_DTLS), "coap_dtls");
}

TEST(AtProfileToString, CoapTls)
{
  EXPECT_STREQ(oc_at_profile_to_string(OC_PROFILE_COAP_TLS), "coap_tls");
}

TEST(AtProfileToString, CoapPase)
{
  EXPECT_STREQ(oc_at_profile_to_string(OC_PROFILE_COAP_PASE), "coap_pase");
}

TEST(AtProfileToString, Unknown)
{
  EXPECT_STREQ(oc_at_profile_to_string(OC_PROFILE_UNKNOWN), "");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_knx_contains_interface
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(ContainsInterface, ExactMatch)
{
  EXPECT_TRUE(oc_knx_contains_interface(OC_IF_I, OC_IF_I));
}

TEST(ContainsInterface, SubsetMatch)
{
  /* caller has I+O, resource has I → match (bitwise AND) */
  auto caller = (oc_interface_mask_t)(OC_IF_I | OC_IF_O);
  EXPECT_TRUE(oc_knx_contains_interface(caller, OC_IF_I));
}

TEST(ContainsInterface, NoOverlap)
{
  EXPECT_FALSE(oc_knx_contains_interface(OC_IF_I, OC_IF_O));
}

TEST(ContainsInterface, BothNone)
{
  EXPECT_FALSE(oc_knx_contains_interface(OC_IF_NONE, OC_IF_NONE));
}

TEST(ContainsInterface, CallerNone)
{
  EXPECT_FALSE(oc_knx_contains_interface(OC_IF_NONE, OC_IF_SEC));
}

TEST(ContainsInterface, MultipleBitsOverlap)
{
  auto caller = (oc_interface_mask_t)(OC_IF_I | OC_IF_SEC);
  auto resource = (oc_interface_mask_t)(OC_IF_SEC | OC_IF_P);
  EXPECT_TRUE(oc_knx_contains_interface(caller, resource));
}
