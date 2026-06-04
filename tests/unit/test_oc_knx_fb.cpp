/*
 * Unit tests for api/oc_knx_fb.c — get_fb_number_from_dp
 *
 * Pure string-parsing function: extracts the functional block number
 * from a datapoint type string like "dpa.352.51" or "urn:knx:dpa.352.51".
 */

#include <gtest/gtest.h>

extern "C" {
#include "oc_ri.h"
#include "api/oc_knx_fb.h"

}

TEST(GetFbNumberFromDp, SimpleDpa)
{
  EXPECT_EQ(get_fb_number_from_dp("dpa.352.51"), 352);
}

TEST(GetFbNumberFromDp, UrnPrefixed)
{
  EXPECT_EQ(get_fb_number_from_dp("urn:knx:dpa.352.51"), 352);
}

TEST(GetFbNumberFromDp, SingleDigitFb)
{
  EXPECT_EQ(get_fb_number_from_dp("dpa.0.13"), 0);
}

TEST(GetFbNumberFromDp, LargeFbNumber)
{
  EXPECT_EQ(get_fb_number_from_dp("dpa.65535.1"), 65535);
}

TEST(GetFbNumberFromDp, NoDotReturnsNegOne)
{
  EXPECT_EQ(get_fb_number_from_dp("nodot"), -1);
}

TEST(GetFbNumberFromDp, EmptyString)
{
  EXPECT_EQ(get_fb_number_from_dp(""), -1);
}

TEST(GetFbNumberFromDp, DotAtEnd)
{
  /* "dpa." → number after dot is 0 (strtol of empty = 0, errno not set) */
  EXPECT_EQ(get_fb_number_from_dp("dpa."), 0);
}

TEST(GetFbNumberFromDp, MultipleDotsOnlyFirstMatters)
{
  /* "a.123.456.789" → first dot, strtol("123.456.789") = 123 */
  EXPECT_EQ(get_fb_number_from_dp("a.123.456.789"), 123);
}
