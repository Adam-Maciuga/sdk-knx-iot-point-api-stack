/*
 * Unit tests for api/oc_knx_fp.c — pure functions only
 *
 * Covers: oc_cflags_as_string (communication flag bitmask to string)
 *
 * Table CRUD functions require full stack init and are not tested here.
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "api/oc_knx_fp.h"

}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_cflags_as_string
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(CflagsAsString, AllFlags)
{
  char buf[16] = {};
  oc_cflag_mask_t all = (oc_cflag_mask_t)(
    OC_CFLAG_READ | OC_CFLAG_WRITE | OC_CFLAG_INIT |
    OC_CFLAG_TRANSMISSION | OC_CFLAG_UPDATE);
  oc_cflags_as_string(buf, all);
  EXPECT_STREQ(buf, "rwitu");
}

TEST(CflagsAsString, NoFlags)
{
  char buf[16] = {};
  oc_cflags_as_string(buf, OC_CFLAG_NONE);
  EXPECT_STREQ(buf, ".....");
}

TEST(CflagsAsString, ReadOnly)
{
  char buf[16] = {};
  oc_cflags_as_string(buf, OC_CFLAG_READ);
  EXPECT_STREQ(buf, "r....");
}

TEST(CflagsAsString, WriteOnly)
{
  char buf[16] = {};
  oc_cflags_as_string(buf, OC_CFLAG_WRITE);
  EXPECT_STREQ(buf, ".w...");
}

TEST(CflagsAsString, TransmissionOnly)
{
  char buf[16] = {};
  oc_cflags_as_string(buf, OC_CFLAG_TRANSMISSION);
  EXPECT_STREQ(buf, "...t.");
}

TEST(CflagsAsString, ReadWrite)
{
  char buf[16] = {};
  oc_cflag_mask_t rw = (oc_cflag_mask_t)(OC_CFLAG_READ | OC_CFLAG_WRITE);
  oc_cflags_as_string(buf, rw);
  EXPECT_STREQ(buf, "rw...");
}

TEST(CflagsAsString, InitAndUpdate)
{
  char buf[16] = {};
  oc_cflag_mask_t iu = (oc_cflag_mask_t)(OC_CFLAG_INIT | OC_CFLAG_UPDATE);
  oc_cflags_as_string(buf, iu);
  EXPECT_STREQ(buf, "..i.u");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Group Object (GO) table search
 *
 * These operate on the static g_got table, accessed for writing through the
 * public oc_core_get_group_object_table_entry() pointer — no full stack init
 * needed. The fixture clears every slot to the "empty" sentinel (id == -1)
 * before and after each test so cases are deterministic and isolated.
 *
 * Regression focus: oc_core_find_next_go_table_index_with_ga() bounds check —
 * a negative current_index (< -1) must not read before g_got[0].
 * ═══════════════════════════════════════════════════════════════════════════ */

class GoTableSearch : public ::testing::Test {
protected:
  void SetUp() override { clear_all(); }
  void TearDown() override { clear_all(); }

  static void clear_all()
  {
    int n = oc_core_get_group_object_table_total_size();
    for (int i = 0; i < n; i++) {
      oc_group_object_table_t *e = oc_core_get_group_object_table_entry(i);
      e->id = -1; /* empty slot sentinel */
      e->ga = nullptr;
      e->ga_len = 0;
    }
  }

  /* Populate slot `index` with id and a group-address array. The ga array must
   * outlive the call (caller keeps ownership). */
  static void set_entry(int index, int32_t id, uint32_t *ga, uint16_t ga_len)
  {
    oc_group_object_table_t *e = oc_core_get_group_object_table_entry(index);
    e->id = id;
    e->ga = ga;
    e->ga_len = ga_len;
  }
};

TEST_F(GoTableSearch, TotalSizeMatchesBuildConstant)
{
  EXPECT_EQ(oc_core_get_group_object_table_total_size(), GOT_MAX_ENTRIES);
}

TEST_F(GoTableSearch, GetEntryOutOfBoundsReturnsNull)
{
  EXPECT_EQ(oc_core_get_group_object_table_entry(-1), nullptr);
  EXPECT_EQ(oc_core_get_group_object_table_entry(GOT_MAX_ENTRIES), nullptr);
  EXPECT_NE(oc_core_get_group_object_table_entry(0), nullptr);
}

TEST_F(GoTableSearch, FindFirstNoMatchReturnsNeg1)
{
  EXPECT_EQ(oc_core_find_first_go_table_index_with_ga(1234), -1);
}

TEST_F(GoTableSearch, FindFirstSingleMatch)
{
  uint32_t ga[] = { 1, 2, 3 };
  set_entry(5, 100, ga, 3);
  EXPECT_EQ(oc_core_find_first_go_table_index_with_ga(2), 5);
}

TEST_F(GoTableSearch, FindFirstSkipsEmptySlots)
{
  uint32_t ga[] = { 42 };
  set_entry(10, 200, ga, 1);
  /* slots 0..9 are empty (id == -1) and must be skipped */
  EXPECT_EQ(oc_core_find_first_go_table_index_with_ga(42), 10);
}

TEST_F(GoTableSearch, FindNextWalksAllMatchesThenNeg1)
{
  uint32_t ga_a[] = { 7 };
  uint32_t ga_b[] = { 7 };
  uint32_t ga_c[] = { 7 };
  set_entry(1, 11, ga_a, 1);
  set_entry(4, 22, ga_b, 1);
  set_entry(9, 33, ga_c, 1);

  int idx = oc_core_find_first_go_table_index_with_ga(7);
  EXPECT_EQ(idx, 1);
  idx = oc_core_find_next_go_table_index_with_ga(7, idx);
  EXPECT_EQ(idx, 4);
  idx = oc_core_find_next_go_table_index_with_ga(7, idx);
  EXPECT_EQ(idx, 9);
  idx = oc_core_find_next_go_table_index_with_ga(7, idx);
  EXPECT_EQ(idx, -1);
}

/* Regression for the array-bounds fix (commit b677b04f):
 * the scan loop `for (i = current_index + 1; i >= 0 && i < GOT_MAX_ENTRIES; i++)`
 * must never read g_got[i] with i < 0. current_index == -1 is the canonical
 * "start" (used by find_first) and scans from index 0; any current_index < -1
 * yields a start index that is still negative, so the loop body never runs and
 * the function safely returns -1 — without ever underflowing the array. */
TEST_F(GoTableSearch, FindNextNegativeCurrentIndexDoesNotUnderflow)
{
  uint32_t ga[] = { 55 };
  set_entry(0, 77, ga, 1);

  /* current_index == -1 → start at 0 → finds the entry at index 0 */
  EXPECT_EQ(oc_core_find_next_go_table_index_with_ga(55, -1), 0);
  /* current_index < -1 → start index still negative → no read, returns -1 */
  EXPECT_EQ(oc_core_find_next_go_table_index_with_ga(55, -2), -1);
  EXPECT_EQ(oc_core_find_next_go_table_index_with_ga(55, -100), -1);
}

TEST_F(GoTableSearch, FindNextCurrentIndexAtOrPastEndReturnsNeg1)
{
  uint32_t ga[] = { 9 };
  set_entry(3, 88, ga, 1);
  EXPECT_EQ(oc_core_find_next_go_table_index_with_ga(9, GOT_MAX_ENTRIES - 1), -1);
  EXPECT_EQ(oc_core_find_next_go_table_index_with_ga(9, GOT_MAX_ENTRIES), -1);
  EXPECT_EQ(oc_core_find_next_go_table_index_with_ga(9, GOT_MAX_ENTRIES + 50), -1);
}

TEST_F(GoTableSearch, FindMatchesCorrectGaWithinMultiGaEntry)
{
  uint32_t ga[] = { 100, 200, 300 };
  set_entry(2, 90, ga, 3);
  EXPECT_EQ(oc_core_find_first_go_table_index_with_ga(300), 2);
  EXPECT_EQ(oc_core_find_first_go_table_index_with_ga(200), 2);
  EXPECT_EQ(oc_core_find_first_go_table_index_with_ga(999), -1);
}

TEST_F(GoTableSearch, FindIndexFromId)
{
  uint32_t ga[] = { 1 };
  set_entry(6, 4242, ga, 1);
  EXPECT_EQ(oc_core_find_index_in_group_object_table_from_id(4242), 6);
  EXPECT_EQ(oc_core_find_index_in_group_object_table_from_id(9999), -1);
}
