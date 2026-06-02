/*
 * Unit tests for api/oc_ri.c and api/oc_knx.c — pure lookup functions
 *
 * Covers:
 *   oc_ri.c:  oc_status_code, oc_count_total_interfaces_in_mask,
 *             oc_count_total_scopes_in_mask, get_interface_string_full_urn,
 *             oc_ri_get_interface_mask
 *   oc_knx.c: oc_core_get_lsm_state_as_string, oc_core_get_lsm_event_as_string
 *
 * All are pure table lookups / bit-counting with no stack init needed.
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "oc_ri.h"
#include "oc_knx.h"

}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_status_code  (oc_status_t → CoAP code)
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(StatusCode, OK)
{
  /* OC_STATUS_OK → 2.05 Content = 69 (2*32 + 5) */
  EXPECT_EQ(oc_status_code(OC_STATUS_OK), CONTENT_2_05);
}

TEST(StatusCode, Created)
{
  EXPECT_EQ(oc_status_code(OC_STATUS_CREATED), CREATED_2_01);
}

TEST(StatusCode, Changed)
{
  EXPECT_EQ(oc_status_code(OC_STATUS_CHANGED), CHANGED_2_04);
}

TEST(StatusCode, Deleted)
{
  EXPECT_EQ(oc_status_code(OC_STATUS_DELETED), DELETED_2_02);
}

TEST(StatusCode, BadRequest)
{
  EXPECT_EQ(oc_status_code(OC_STATUS_BAD_REQUEST), BAD_REQUEST_4_00);
}

TEST(StatusCode, Unauthorized)
{
  EXPECT_EQ(oc_status_code(OC_STATUS_UNAUTHORIZED), UNAUTHORIZED_4_01);
}

TEST(StatusCode, NotFound)
{
  EXPECT_EQ(oc_status_code(OC_STATUS_NOT_FOUND), NOT_FOUND_4_04);
}

TEST(StatusCode, MethodNotAllowed)
{
  EXPECT_EQ(oc_status_code(OC_STATUS_METHOD_NOT_ALLOWED), METHOD_NOT_ALLOWED_4_05);
}

TEST(StatusCode, InternalServerError)
{
  EXPECT_EQ(oc_status_code(OC_STATUS_INTERNAL_SERVER_ERROR),
            INTERNAL_SERVER_ERROR_5_00);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_count_total_interfaces_in_mask
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(CountInterfaces, None)
{
  EXPECT_EQ(oc_count_total_interfaces_in_mask(OC_IF_NONE), 0u);
}

TEST(CountInterfaces, SingleBit)
{
  EXPECT_EQ(oc_count_total_interfaces_in_mask(OC_IF_I), 1u);
  EXPECT_EQ(oc_count_total_interfaces_in_mask(OC_IF_SEC), 1u);
}

TEST(CountInterfaces, MultipleBits)
{
  auto mask = (oc_interface_mask_t)(OC_IF_I | OC_IF_O | OC_IF_C);
  EXPECT_EQ(oc_count_total_interfaces_in_mask(mask), 3u);
}

TEST(CountInterfaces, AllBits)
{
  auto mask = (oc_interface_mask_t)(
    OC_IF_I | OC_IF_O | OC_IF_G | OC_IF_C | OC_IF_P |
    OC_IF_D | OC_IF_A | OC_IF_S | OC_IF_LI | OC_IF_B |
    OC_IF_SEC | OC_IF_SWU | OC_IF_PM | OC_IF_M);
  EXPECT_EQ(oc_count_total_interfaces_in_mask(mask), 14u);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_count_total_scopes_in_mask
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(CountScopes, None)
{
  EXPECT_EQ(oc_count_total_scopes_in_mask(OC_ACL_NONE), 0u);
}

TEST(CountScopes, SingleScope)
{
  EXPECT_EQ(oc_count_total_scopes_in_mask(OC_ACL_I), 1u);
}

TEST(CountScopes, MultipleScopes)
{
  auto mask = (oc_acl_mask_t)(OC_ACL_I | OC_ACL_SEC | OC_ACL_P);
  EXPECT_EQ(oc_count_total_scopes_in_mask(mask), 3u);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * get_interface_string_full_urn
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(InterfaceStringFullUrn, IndexI)
{
  /* OC_IF_I = 1 << 1 → index 1 → "urn:knx:if.i" */
  EXPECT_STREQ(get_interface_string_full_urn(1), "urn:knx:if.i");
}

TEST(InterfaceStringFullUrn, IndexO)
{
  EXPECT_STREQ(get_interface_string_full_urn(2), "urn:knx:if.o");
}

TEST(InterfaceStringFullUrn, IndexSec)
{
  EXPECT_STREQ(get_interface_string_full_urn(11), "urn:knx:if.sec");
}

TEST(InterfaceStringFullUrn, IndexNone)
{
  EXPECT_STREQ(get_interface_string_full_urn(0), "");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_ri_get_interface_mask
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(GetInterfaceMask, KnownUrn)
{
  const char *urn = "urn:knx:if.i";
  EXPECT_EQ(oc_ri_get_interface_mask(urn, strlen(urn)), OC_IF_I);
}

TEST(GetInterfaceMask, SecUrn)
{
  const char *urn = "urn:knx:if.sec";
  EXPECT_EQ(oc_ri_get_interface_mask(urn, strlen(urn)), OC_IF_SEC);
}

TEST(GetInterfaceMask, UnknownUrn)
{
  const char *urn = "urn:knx:if.unknown";
  EXPECT_EQ(oc_ri_get_interface_mask(urn, strlen(urn)), OC_IF_NONE);
}

TEST(GetInterfaceMask, EmptyUrn)
{
  /* Bug F-002: index 0 of interface_string_full_urn is "", so an empty
   * input matches it and returns 1<<0 = 1 instead of OC_IF_NONE (0). */
  EXPECT_EQ(oc_ri_get_interface_mask("", 0), (oc_interface_mask_t)1);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_core_get_lsm_state_as_string
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(LsmStateString, Unloaded)
{
  EXPECT_STREQ(oc_core_get_lsm_state_as_string(LSM_S_UNLOADED), "unloaded");
}

TEST(LsmStateString, Loaded)
{
  EXPECT_STREQ(oc_core_get_lsm_state_as_string(LSM_S_LOADED), "loaded");
}

TEST(LsmStateString, Loading)
{
  EXPECT_STREQ(oc_core_get_lsm_state_as_string(LSM_S_LOADING), "loading");
}

TEST(LsmStateString, Unloading)
{
  EXPECT_STREQ(oc_core_get_lsm_state_as_string(LSM_S_UNLOADING), "unloading");
}

TEST(LsmStateString, LoadCompleting)
{
  EXPECT_STREQ(oc_core_get_lsm_state_as_string(LSM_S_LOADCOMPLETING),
               "load completing");
}

TEST(LsmStateString, Unknown)
{
  EXPECT_STREQ(oc_core_get_lsm_state_as_string(LSM_S_ERROR), "");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_core_get_lsm_event_as_string
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(LsmEventString, Nop)
{
  EXPECT_STREQ(oc_core_get_lsm_event_as_string(LSM_E_NOP), "nop");
}

TEST(LsmEventString, StartLoading)
{
  EXPECT_STREQ(oc_core_get_lsm_event_as_string(LSM_E_STARTLOADING),
               "start loading");
}

TEST(LsmEventString, LoadComplete)
{
  EXPECT_STREQ(oc_core_get_lsm_event_as_string(LSM_E_LOADCOMPLETE),
               "load complete");
}

TEST(LsmEventString, Unload)
{
  EXPECT_STREQ(oc_core_get_lsm_event_as_string(LSM_E_UNLOAD), "unload");
}

TEST(LsmEventString, Unknown)
{
  EXPECT_STREQ(oc_core_get_lsm_event_as_string((oc_lsm_event_t)99), "");
}
