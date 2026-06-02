/*
 * Unit tests for oc_knx.c (LSM string conversions) and
 * oc_knx_client.c (oc_is_redirected_request_from).
 *
 * Tests pure functions that need no stack initialization.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "oc_ri.h"
#include "include/oc_knx.h"
#include "api/oc_knx_client.h"
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_core_get_lsm_state_as_string — pure enum→string lookup
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

TEST(LsmStateString, UnknownReturnsEmpty)
{
  EXPECT_STREQ(oc_core_get_lsm_state_as_string((oc_lsm_state_t)99), "");
}

TEST(LsmStateString, ErrorReturnsEmpty)
{
  EXPECT_STREQ(oc_core_get_lsm_state_as_string(LSM_S_ERROR), "");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_core_get_lsm_event_as_string — pure enum→string lookup
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

TEST(LsmEventString, UnknownReturnsEmpty)
{
  EXPECT_STREQ(oc_core_get_lsm_event_as_string((oc_lsm_event_t)99), "");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_is_redirected_request_from — near-pure, reads only uri_path[0]
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(IsRedirectedRequestFrom, NullRequestReturnsNeg1)
{
  EXPECT_EQ(oc_is_redirected_request_from(nullptr), -1);
}

TEST(IsRedirectedRequestFrom, EmptyUriReturnsNeg1)
{
  oc_request_t req;
  memset(&req, 0, sizeof(req));
  req.uri_path_len = 0;
  EXPECT_EQ(oc_is_redirected_request_from(&req), -1);
}

TEST(IsRedirectedRequestFrom, KResourceReturns0)
{
  oc_request_t req;
  memset(&req, 0, sizeof(req));
  req.uri_path = "k";
  req.uri_path_len = 1;
  EXPECT_EQ(oc_is_redirected_request_from(&req), 0);
}

TEST(IsRedirectedRequestFrom, PResourceReturns1)
{
  oc_request_t req;
  memset(&req, 0, sizeof(req));
  req.uri_path = "p/lsab/0/a00";
  req.uri_path_len = 13;
  EXPECT_EQ(oc_is_redirected_request_from(&req), 1);
}

TEST(IsRedirectedRequestFrom, OtherResourceReturns2)
{
  oc_request_t req;
  memset(&req, 0, sizeof(req));
  req.uri_path = "dev/sn";
  req.uri_path_len = 6;
  EXPECT_EQ(oc_is_redirected_request_from(&req), 2);
}
