/*
 * Unit tests for api/oc_knx_fb.c — get_fb_number_from_dp
 *
 * Pure string-parsing function: extracts the functional block number
 * from a datapoint type string like "dpa.352.51" or "urn:knx:dpa.352.51".
 */

#include <gtest/gtest.h>
#include <vector>

extern "C" {
#include "oc_ri.h"
#include "api/oc_knx_fb.h"
#include "util/oc_mmem.h"
#include <string.h>

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

/*
 * oc_check_if_functional_blocks_need_to_add — public driver that exercises the
 * file-static bounded_strstr() (static, no external linkage, and the TU cannot
 * be #include'd because it carries const oc_resource_t aggregate initializers
 * that do not compile under g++). It is reachable and fully observable through
 * this public function, so every bounded_strstr branch is covered behaviorally
 * here via the "rt"/"if" query parameters.
 */
static oc_request_t make_fb_query_request(const char *query)
{
  oc_request_t req{};
  req.query = (char *)query;
  req.query_len = query ? strlen(query) : 0;
  return req;
}

TEST(CheckIfFunctionalBlocksNeedToAdd, NoRtNoIfReturnsTrue)
{
  oc_request_t req = make_fb_query_request("foo=bar");
  EXPECT_TRUE(oc_check_if_functional_blocks_need_to_add(&req));
}

TEST(CheckIfFunctionalBlocksNeedToAdd, RtWildcardReturnsTrue)
{
  oc_request_t req = make_fb_query_request("rt=*");
  EXPECT_TRUE(oc_check_if_functional_blocks_need_to_add(&req));
}

TEST(CheckIfFunctionalBlocksNeedToAdd, RtContainsFbReturnsTrue)
{
  /* bounded_strstr(rt, "fb") matches inside the value */
  oc_request_t req = make_fb_query_request("rt=urn:knx:fb.321");
  EXPECT_TRUE(oc_check_if_functional_blocks_need_to_add(&req));
}

TEST(CheckIfFunctionalBlocksNeedToAdd, RtWithoutFbNoWildcardReturnsFalse)
{
  /* bounded_strstr(rt, "fb") finds nothing, no wildcard -> false */
  oc_request_t req = make_fb_query_request("rt=urn:knx:dpa.352");
  EXPECT_FALSE(oc_check_if_functional_blocks_need_to_add(&req));
}

TEST(CheckIfFunctionalBlocksNeedToAdd, IfWildcardReturnsTrue)
{
  oc_request_t req = make_fb_query_request("if=*");
  EXPECT_TRUE(oc_check_if_functional_blocks_need_to_add(&req));
}

TEST(CheckIfFunctionalBlocksNeedToAdd, IfContainsLlReturnsTrue)
{
  /* bounded_strstr(if, "ll") matches inside the value */
  oc_request_t req = make_fb_query_request("if=urn:knx:if.ll");
  EXPECT_TRUE(oc_check_if_functional_blocks_need_to_add(&req));
}

TEST(CheckIfFunctionalBlocksNeedToAdd, IfWithoutLlNoWildcardReturnsFalse)
{
  /* bounded_strstr(if, "ll") finds nothing, no wildcard -> false */
  oc_request_t req = make_fb_query_request("if=urn:knx:if.pa");
  EXPECT_FALSE(oc_check_if_functional_blocks_need_to_add(&req));
}

/*
 * oc_count_functional_blocks_from_application — public driver that exercises
 * the file-static check_array_for_scanned_fbs_and_add_if_fresh() (static, no
 * external linkage, and oc_knx_fb.c cannot be #include'd because it carries
 * const oc_resource_t aggregate initializers that do not compile under g++).
 * The dedup/add/skip logic of the static helper is fully observable through
 * the returned count, so it is covered behaviorally here by registering real
 * application resources with crafted fb_data / OC_DISCOVERABLE bits.
 *
 * fb_data layout (see oc_count_functional_blocks_from_application):
 *   fb_number   = fb_data >> 16
 *   fb_instance = (fb_data >> 8) & 0xFF
 */
static void fb_dummy_handler(oc_request_t *, oc_interface_mask_t, void *) {}

class CountFunctionalBlocks : public ::testing::Test {
protected:
  std::vector<oc_resource_t *> added;

  void SetUp() override { oc_mmem_init(); }

  void TearDown() override
  {
    for (oc_resource_t *r : added) {
      oc_ri_delete_resource(r);
    }
    added.clear();
  }

  void add_fb(uint16_t number, uint8_t instance, bool discoverable)
  {
    oc_resource_t *r = oc_ri_alloc_resource();
    ASSERT_NE(r, nullptr);
    r->get_handler.cb = fb_dummy_handler;
    r->fb_data = ((uint32_t)number << 16) | ((uint32_t)instance << 8);
    if (discoverable) {
      r->properties = OC_DISCOVERABLE;
    }
    ASSERT_TRUE(oc_ri_add_resource(r));
    added.push_back(r);
  }
};

TEST_F(CountFunctionalBlocks, NoAppResourcesReturnsZero)
{
  EXPECT_EQ(oc_count_functional_blocks_from_application(), 0);
}

TEST_F(CountFunctionalBlocks, TwoDistinctFbsCountedAsTwo)
{
  add_fb(417, 1, true);
  add_fb(418, 1, true);
  EXPECT_EQ(oc_count_functional_blocks_from_application(), 2);
}

TEST_F(CountFunctionalBlocks, SameNumberDifferentInstanceCountedSeparately)
{
  add_fb(417, 1, true);
  add_fb(417, 3, true);
  EXPECT_EQ(oc_count_functional_blocks_from_application(), 2);
}

TEST_F(CountFunctionalBlocks, DuplicateFbNotCountedTwice)
{
  /* same number + instance -> the static helper de-duplicates */
  add_fb(417, 1, true);
  add_fb(417, 1, true);
  EXPECT_EQ(oc_count_functional_blocks_from_application(), 1);
}

TEST_F(CountFunctionalBlocks, NonDiscoverableResourceSkipped)
{
  add_fb(417, 1, true);
  add_fb(500, 1, false); /* not discoverable -> not counted */
  EXPECT_EQ(oc_count_functional_blocks_from_application(), 1);
}
