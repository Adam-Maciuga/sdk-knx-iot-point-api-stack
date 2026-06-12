/*
 * Unit tests for api/oc_knx_fp.c
 *
 * Covers:
 *   - oc_cflags_as_string (communication flag bitmask to string)
 *   - Group Object / Recipient / Publisher table search helpers
 *   - oc_belongs_href_to_resource
 *   - Group table persistence: store/load round trip for the Recipient and
 *     Publisher tables (driven through the public loader
 *     oc_create_knx_table_resources), including the 'non' boolean flag.
 */

#include <gtest/gtest.h>
#include <cstring>
#include <vector>

extern "C" {
#include "api/oc_knx_fp.h"
#include "oc_ri.h"
#include "oc_rep.h"
#include "oc_helpers.h"
#include "util/oc_mmem.h"
#include "port/oc_storage.h"

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

/* find_empty_slot returns the first slot whose id == -1 ("empty" sentinel) */
TEST_F(GoTableSearch, FindEmptySlotAllEmptyReturnsZero)
{
  EXPECT_EQ(find_empty_slot_in_group_object_table(), 0);
}

TEST_F(GoTableSearch, FindEmptySlotSkipsOccupiedLeadingSlots)
{
  uint32_t ga[] = { 1 };
  set_entry(0, 10, ga, 1);
  set_entry(1, 11, ga, 1);
  EXPECT_EQ(find_empty_slot_in_group_object_table(), 2);
}

TEST_F(GoTableSearch, FindEmptySlotFullTableReturnsNeg1)
{
  uint32_t ga[] = { 1 };
  for (int i = 0; i < GOT_MAX_ENTRIES; i++) {
    set_entry(i, 100 + i, ga, 1);
  }
  EXPECT_EQ(find_empty_slot_in_group_object_table(), -1);
}

/* index accessors that read non-href fields (cflags / ga_len) */
TEST_F(GoTableSearch, GetCflagsOutOfBoundsReturnsNone)
{
  EXPECT_EQ(oc_core_get_cflags_from_group_object_table_index(-1), OC_CFLAG_NONE);
  EXPECT_EQ(oc_core_get_cflags_from_group_object_table_index(GOT_MAX_ENTRIES),
            OC_CFLAG_NONE);
}

TEST_F(GoTableSearch, GetCflagsReturnsStoredFlags)
{
  oc_group_object_table_t *e = oc_core_get_group_object_table_entry(4);
  e->id = 7;
  e->cflags = (oc_cflag_mask_t)(OC_CFLAG_READ | OC_CFLAG_WRITE);
  EXPECT_EQ(oc_core_get_cflags_from_group_object_table_index(4),
            (oc_cflag_mask_t)(OC_CFLAG_READ | OC_CFLAG_WRITE));
}

TEST_F(GoTableSearch, GetGaLenOutOfBoundsReturnsZero)
{
  EXPECT_EQ(oc_core_get_ga_table_len_from_group_object_table_index(-1), 0);
  EXPECT_EQ(
    oc_core_get_ga_table_len_from_group_object_table_index(GOT_MAX_ENTRIES), 0);
}

TEST_F(GoTableSearch, GetGaLenReturnsStoredLength)
{
  uint32_t ga[] = { 1, 2, 3 };
  set_entry(8, 55, ga, 3);
  EXPECT_EQ(oc_core_get_ga_table_len_from_group_object_table_index(8), 3);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Table-size accessors + publisher-table iid flag (constant getters)
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(FpTableSizes, RecipientMatchesBuildConstant)
{
  EXPECT_EQ(oc_core_get_recipient_table_size(), GRT_MAX_ENTRIES);
}

TEST(FpTableSizes, PublisherMatchesBuildConstant)
{
  /* OC_PUBLISHER_TABLE is defined in this build */
  EXPECT_EQ(oc_core_get_publisher_table_size(), GPT_MAX_ENTRIES);
}

TEST(FpTableSizes, RecipientEntryBounds)
{
  EXPECT_EQ(oc_core_get_recipient_table_entry(-1), nullptr);
  EXPECT_EQ(oc_core_get_recipient_table_entry(GRT_MAX_ENTRIES), nullptr);
  EXPECT_NE(oc_core_get_recipient_table_entry(0), nullptr);
}

TEST(FpTableSizes, PublisherEntryBounds)
{
  EXPECT_EQ(oc_core_get_publisher_table_entry(-1), nullptr);
  EXPECT_EQ(oc_core_get_publisher_table_entry(GPT_MAX_ENTRIES), nullptr);
  EXPECT_NE(oc_core_get_publisher_table_entry(0), nullptr);
}

TEST(FpTableSizes, PubTableContainsNoIidDefaultsTrue)
{
  /* default state of the static flag; no test mutates the publisher table */
  EXPECT_TRUE(pub_table_contains_no_iid());
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_table_find_id_from_payload_and_check_if_in_16_bit_range — pure rep scan
 *   id field is iname == 0 of type INT; range is [0, 65535]
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(FpFindIdFromPayload, InRangeReturnsId)
{
  oc_rep_t rep{};
  rep.type = OC_REP_INT;
  rep.iname = 0;
  rep.value.integer = 1234;
  rep.next = nullptr;
  EXPECT_EQ(oc_table_find_id_from_payload_and_check_if_in_16_bit_range(&rep),
            1234);
}

TEST(FpFindIdFromPayload, AboveRangeReturnsNeg2)
{
  oc_rep_t rep{};
  rep.type = OC_REP_INT;
  rep.iname = 0;
  rep.value.integer = 65536; /* > 65535 */
  EXPECT_EQ(oc_table_find_id_from_payload_and_check_if_in_16_bit_range(&rep),
            -2);
}

TEST(FpFindIdFromPayload, NegativeReturnsNeg2)
{
  oc_rep_t rep{};
  rep.type = OC_REP_INT;
  rep.iname = 0;
  rep.value.integer = -1;
  EXPECT_EQ(oc_table_find_id_from_payload_and_check_if_in_16_bit_range(&rep),
            -2);
}

TEST(FpFindIdFromPayload, NoMatchingIntReturnsNeg1)
{
  /* only a non-id int (iname != 0) present -> id not found */
  oc_rep_t rep{};
  rep.type = OC_REP_INT;
  rep.iname = 5;
  rep.value.integer = 99;
  EXPECT_EQ(oc_table_find_id_from_payload_and_check_if_in_16_bit_range(&rep),
            -1);
}

TEST(FpFindIdFromPayload, NullPayloadReturnsNeg1)
{
  EXPECT_EQ(oc_table_find_id_from_payload_and_check_if_in_16_bit_range(nullptr),
            -1);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * href-based GO-table lookups (need real oc_string_t hrefs -> oc_mmem)
 * ═══════════════════════════════════════════════════════════════════════════ */

class GoTableHref : public ::testing::Test {
protected:
  void SetUp() override
  {
    oc_mmem_init();
    clear_all();
  }
  void TearDown() override { clear_all(); }

  /* clear every slot to empty and release any href we allocated */
  static void clear_all()
  {
    int n = oc_core_get_group_object_table_total_size();
    for (int i = 0; i < n; i++) {
      oc_group_object_table_t *e = oc_core_get_group_object_table_entry(i);
      if (oc_string_len(e->href) > 0) {
        oc_free_string(&e->href);
      }
      e->href = (oc_string_t){ 0 };
      e->id = -1;
      e->ga = nullptr;
      e->ga_len = 0;
      e->cflags = OC_CFLAG_NONE;
    }
  }

  static void set_entry(int index, int32_t id, const char *href, uint32_t *ga,
                        uint16_t ga_len)
  {
    oc_group_object_table_t *e = oc_core_get_group_object_table_entry(index);
    e->id = id;
    oc_new_string(&e->href, href, strlen(href));
    e->ga = ga;
    e->ga_len = ga_len;
  }
};

TEST_F(GoTableHref, GetHrefOutOfBoundsReturnsEmpty)
{
  EXPECT_EQ(oc_string_len(oc_core_get_href_from_group_object_table_index(-1)),
            0u);
  EXPECT_EQ(oc_string_len(oc_core_get_href_from_group_object_table_index(
              GOT_MAX_ENTRIES)),
            0u);
}

TEST_F(GoTableHref, GetHrefReturnsStoredString)
{
  uint32_t ga[] = { 1 };
  set_entry(3, 10, "/p/a", ga, 1);
  oc_string_t h = oc_core_get_href_from_group_object_table_index(3);
  ASSERT_EQ(oc_string_len(h), 4u);
  EXPECT_STREQ(oc_string(h), "/p/a");
}

TEST_F(GoTableHref, FindFirstFromHrefMatchAndMiss)
{
  uint32_t ga[] = { 1 };
  set_entry(7, 20, "/p/b", ga, 1);
  EXPECT_EQ(oc_core_find_first_group_object_table_index_from_href("/p/b"), 7);
  EXPECT_EQ(oc_core_find_first_group_object_table_index_from_href("/none"), -1);
}

TEST_F(GoTableHref, FindNextFromHrefWalksAllMatches)
{
  uint32_t ga[] = { 1 };
  set_entry(2, 21, "/p/c", ga, 1);
  set_entry(5, 22, "/p/c", ga, 1);
  int idx = oc_core_find_first_group_object_table_index_from_href("/p/c");
  EXPECT_EQ(idx, 2);
  idx = oc_core_find_next_group_object_table_index_from_href("/p/c", idx);
  EXPECT_EQ(idx, 5);
  idx = oc_core_find_next_group_object_table_index_from_href("/p/c", idx);
  EXPECT_EQ(idx, -1);
}

TEST_F(GoTableHref, FindNextFromHrefNegativeIndexDoesNotUnderflow)
{
  uint32_t ga[] = { 1 };
  set_entry(0, 23, "/p/d", ga, 1);
  EXPECT_EQ(oc_core_find_next_group_object_table_index_from_href("/p/d", -1), 0);
  /* current_index < -1 -> guarded, returns -1 without reading the array */
  EXPECT_EQ(oc_core_find_next_group_object_table_index_from_href("/p/d", -2),
            -1);
}

TEST_F(GoTableHref, FindSendingGaPosZeroReturnsLowestIdEntry)
{
  uint32_t ga_hi[] = { 200 };
  uint32_t ga_lo[] = { 100 };
  /* two entries with the same href; the one with the lower id (and ga_len>0)
   * holds the sending GA in position zero */
  set_entry(4, 12, "/p/e", ga_hi, 1);
  set_entry(9, 10, "/p/e", ga_lo, 1);
  oc_group_object_table_t *e =
    oc_core_find_sending_ga_in_pos_zero_for_href("/p/e");
  ASSERT_NE(e, nullptr);
  EXPECT_EQ(e->id, 10);
  EXPECT_EQ(e->ga[0], 100u);
}

TEST_F(GoTableHref, FindSendingGaNoMatchReturnsNull)
{
  EXPECT_EQ(oc_core_find_sending_ga_in_pos_zero_for_href("/nope"), nullptr);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * recipient table id lookup (exact-id match over g_grt)
 * ═══════════════════════════════════════════════════════════════════════════ */

class RecipientTable : public ::testing::Test {
protected:
  void SetUp() override { clear_all(); }
  void TearDown() override { clear_all(); }

  static void clear_all()
  {
    int n = oc_core_get_recipient_table_size();
    for (int i = 0; i < n; i++) {
      oc_group_table_t *e = oc_core_get_recipient_table_entry(i);
      e->id = -1; /* empty sentinel; no entry will match a real id */
    }
  }
};

TEST_F(RecipientTable, FindIndexFromIdMatchAndMiss)
{
  oc_core_get_recipient_table_entry(6)->id = 4321;
  EXPECT_EQ(oc_core_find_index_in_recipient_table_from_id(4321), 6);
  EXPECT_EQ(oc_core_find_index_in_recipient_table_from_id(9999), -1);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_belongs_href_to_resource — scans the app-resource list comparing the URL
 * ═══════════════════════════════════════════════════════════════════════════ */

static void fp_dummy_handler(oc_request_t *, oc_interface_mask_t, void *) {}

class BelongsHref : public ::testing::Test {
protected:
  std::vector<oc_resource_t *> added;

  void SetUp() override { oc_mmem_init(); }

  void TearDown() override
  {
    for (oc_resource_t *r : added) {
      oc_free_string(&r->uri);
      oc_ri_delete_resource(r);
    }
    added.clear();
  }

  void add_resource(const char *uri, bool discoverable)
  {
    oc_resource_t *r = oc_ri_alloc_resource();
    ASSERT_NE(r, nullptr);
    r->get_handler.cb = fp_dummy_handler;
    oc_new_string(&r->uri, uri, strlen(uri));
    if (discoverable) {
      r->properties = OC_DISCOVERABLE;
    }
    ASSERT_TRUE(oc_ri_add_resource(r));
    added.push_back(r);
  }
};

TEST_F(BelongsHref, MatchingHrefReturnsTrue)
{
  add_resource("/p/x", true);
  oc_string_t href;
  oc_new_string(&href, "/p/x", 4);
  EXPECT_TRUE(oc_belongs_href_to_resource(href, false));
  oc_free_string(&href);
}

TEST_F(BelongsHref, NonMatchingHrefReturnsFalse)
{
  add_resource("/p/x", true);
  oc_string_t href;
  oc_new_string(&href, "/p/y", 4);
  EXPECT_FALSE(oc_belongs_href_to_resource(href, false));
  oc_free_string(&href);
}

TEST_F(BelongsHref, NonDiscoverableSkippedWhenDiscoverableRequested)
{
  add_resource("/p/z", false); /* not discoverable */
  oc_string_t href;
  oc_new_string(&href, "/p/z", 4);
  /* discoverable=true -> the non-discoverable resource is skipped -> false */
  EXPECT_FALSE(oc_belongs_href_to_resource(href, true));
  /* discoverable=false -> it is considered -> true */
  EXPECT_TRUE(oc_belongs_href_to_resource(href, false));
  oc_free_string(&href);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Group table persistence (Recipient / Publisher) - store/load round trip
 *
 * The store/load helpers in oc_knx_fp.c are file-static, so the round trip is
 * exercised through the public surface:
 *   - the loader runs via oc_create_knx_table_resources() (-> oc_load_object_tables
 *     -> oc_load_group_table_entry for every slot);
 *   - the writer's CBOR contract is reproduced with the same oc_rep_* macros the
 *     production writer uses and persisted via oc_storage_write to the exact
 *     per-entry filename the loader reads ("<store>_<index>").
 *
 * Regression focus: the 'non' boolean must survive a round trip. The previous
 * writer encoded a text string (the AT value) under the 'non' key and the loader
 * had no boolean case, so 'non' was silently dropped on reload.
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Must match GRT_STORE / GPT_STORE in api/oc_knx_fp.c. */
static const char *kRcpStore = "dev_knx_rcv_entry";
static const char *kPubStore = "dev_knx_pub_entry";
static const char *kFpStoreDir = "knx_storage_fp_unit_test";

class GroupTableStorage : public ::testing::Test {
protected:
  static void SetUpTestSuite()
  {
    /* mkdir returns 0 on create, -1 if it already exists - both fine. */
    oc_storage_config(kFpStoreDir);
  }

  void SetUp() override
  {
    /* erase any stale per-entry files and reset the RAM tables */
    oc_delete_group_tables();
  }

  void TearDown() override
  {
    oc_delete_group_tables();
  }

  /* Encode a single group table entry to CBOR exactly as the production writer
   * (oc_store_group_table_entry) does, then persist it under "<store>_<index>".
   * 'ga' may be NULL with ga_len == 0 to exercise the empty-array path. */
  static void stage_entry(const char *store, int index, int32_t id, int32_t ia,
                          int64_t iid, int64_t fid, uint32_t grpid,
                          const char *at, const uint32_t *ga, uint16_t ga_len,
                          bool non)
  {
    uint8_t buf[512];
    oc_rep_new(buf, (int)sizeof(buf));

    oc_rep_begin_root_object();
    oc_rep_i_set_int(root, 0, id);
    oc_rep_i_set_int(root, 12, ia);
    oc_rep_i_set_int(root, 26, iid);
    oc_rep_i_set_int(root, 25, fid);
    oc_rep_i_set_int(root, 13, grpid);
    oc_rep_i_set_text_string(root, 14, at);
    oc_rep_i_set_int_array(root, 7, ga, ga_len);
    oc_rep_text_set_boolean(root, non, non);
    oc_rep_end_root_object();

    int size = oc_rep_get_encoded_payload_size();
    ASSERT_GT(size, 0);

    char filename[64];
    (void)snprintf(filename, sizeof(filename), "%s_%d", store, index);
    long written = oc_storage_write(filename, buf, (size_t)size);
    ASSERT_EQ(written, (long)size);
  }

  /* Encode an entry WITHOUT the 'non' key, to mimic a legacy / partial record
   * where the flag was never serialized. Used to verify the loader leaves
   * 'non' at its initialized default (false). */
  static void stage_entry_without_non(const char *store, int index, int32_t id,
                                      int32_t ia, int64_t iid, int64_t fid,
                                      uint32_t grpid, const char *at,
                                      const uint32_t *ga, uint16_t ga_len)
  {
    uint8_t buf[512];
    oc_rep_new(buf, (int)sizeof(buf));

    oc_rep_begin_root_object();
    oc_rep_i_set_int(root, 0, id);
    oc_rep_i_set_int(root, 12, ia);
    oc_rep_i_set_int(root, 26, iid);
    oc_rep_i_set_int(root, 25, fid);
    oc_rep_i_set_int(root, 13, grpid);
    oc_rep_i_set_text_string(root, 14, at);
    oc_rep_i_set_int_array(root, 7, ga, ga_len);
    /* deliberately no 'non' key */
    oc_rep_end_root_object();

    int size = oc_rep_get_encoded_payload_size();
    ASSERT_GT(size, 0);

    char filename[64];
    (void)snprintf(filename, sizeof(filename), "%s_%d", store, index);
    long written = oc_storage_write(filename, buf, (size_t)size);
    ASSERT_EQ(written, (long)size);
  }
};

TEST_F(GroupTableStorage, RecipientRoundTripNonTrue)
{
  const uint32_t ga[] = { 1, 5, 65535 };
  stage_entry(kRcpStore, 0, /*id*/ 7, /*ia*/ 0x110F, /*iid*/ 0x1199887766LL,
              /*fid*/ 0x9988776655LL, /*grpid*/ 0x80000001u,
              /*at*/ "token-rcp-0", ga, 3, /*non*/ true);

  oc_create_knx_table_resources();

  oc_group_table_t *e = oc_core_get_recipient_table_entry(0);
  ASSERT_NE(e, nullptr);
  EXPECT_EQ(e->id, 7);
  EXPECT_EQ(e->ia, 0x110F);
  EXPECT_EQ(e->iid, 0x1199887766LL);
  EXPECT_EQ(e->fid, 0x9988776655LL);
  EXPECT_EQ(e->grpid, 0x80000001u);
  EXPECT_STREQ(oc_string(e->at), "token-rcp-0");
  ASSERT_EQ(e->ga_len, 3);
  EXPECT_EQ(e->ga[0], 1u);
  EXPECT_EQ(e->ga[1], 5u);
  EXPECT_EQ(e->ga[2], 65535u);
  EXPECT_TRUE(e->non); /* regression: non must survive the reload */
}

TEST_F(GroupTableStorage, RecipientRoundTripNonFalse)
{
  const uint32_t ga[] = { 42 };
  stage_entry(kRcpStore, 2, /*id*/ 11, /*ia*/ 0x1101, /*iid*/ 1, /*fid*/ 2,
              /*grpid*/ 0x80000009u, /*at*/ "token-rcp-2", ga, 1,
              /*non*/ false);

  oc_create_knx_table_resources();

  oc_group_table_t *e = oc_core_get_recipient_table_entry(2);
  ASSERT_NE(e, nullptr);
  EXPECT_EQ(e->id, 11);
  EXPECT_EQ(e->grpid, 0x80000009u);
  EXPECT_STREQ(oc_string(e->at), "token-rcp-2");
  ASSERT_EQ(e->ga_len, 1);
  EXPECT_EQ(e->ga[0], 42u);
  EXPECT_FALSE(e->non);
}

TEST_F(GroupTableStorage, RecipientRoundTripEmptyGaArray)
{
  /* empty ga array is encoded as OC_REP_NIL; non must still round trip */
  stage_entry(kRcpStore, 1, /*id*/ 9, /*ia*/ -1, /*iid*/ -1, /*fid*/ -1,
              /*grpid*/ 0x80000005u, /*at*/ "", nullptr, 0, /*non*/ true);

  oc_create_knx_table_resources();

  oc_group_table_t *e = oc_core_get_recipient_table_entry(1);
  ASSERT_NE(e, nullptr);
  EXPECT_EQ(e->id, 9);
  EXPECT_EQ(e->grpid, 0x80000005u);
  EXPECT_EQ(e->ga_len, 0);
  EXPECT_EQ(e->ga, nullptr);
  EXPECT_TRUE(e->non);
}

TEST_F(GroupTableStorage, RecipientMultipleEntriesRoundTrip)
{
  const uint32_t ga0[] = { 100 };
  const uint32_t ga1[] = { 200, 201 };
  stage_entry(kRcpStore, 0, 1, 0x1000, 1, 1, 0x80000001u, "a", ga0, 1, true);
  stage_entry(kRcpStore, 3, 2, 0x1002, 1, 1, 0x80000002u, "b", ga1, 2, false);

  oc_create_knx_table_resources();

  oc_group_table_t *e0 = oc_core_get_recipient_table_entry(0);
  oc_group_table_t *e3 = oc_core_get_recipient_table_entry(3);
  ASSERT_NE(e0, nullptr);
  ASSERT_NE(e3, nullptr);
  EXPECT_EQ(e0->id, 1);
  EXPECT_TRUE(e0->non);
  EXPECT_EQ(e3->id, 2);
  EXPECT_FALSE(e3->non);

  /* an untouched slot stays empty after load */
  oc_group_table_t *e_empty = oc_core_get_recipient_table_entry(4);
  ASSERT_NE(e_empty, nullptr);
  EXPECT_EQ(e_empty->id, -1);
}

#ifdef OC_PUBLISHER_TABLE
TEST_F(GroupTableStorage, PublisherRoundTripNonFlag)
{
  const uint32_t ga[] = { 7, 8 };
  stage_entry(kPubStore, 0, /*id*/ 3, /*ia*/ 0x1234, /*iid*/ 5, /*fid*/ 6,
              /*grpid*/ 0x80000003u, /*at*/ "token-pub-0", ga, 2,
              /*non*/ true);

  oc_create_knx_table_resources();

  oc_group_table_t *e = oc_core_get_publisher_table_entry(0);
  ASSERT_NE(e, nullptr);
  EXPECT_EQ(e->id, 3);
  EXPECT_EQ(e->ia, 0x1234);
  EXPECT_EQ(e->grpid, 0x80000003u);
  EXPECT_STREQ(oc_string(e->at), "token-pub-0");
  ASSERT_EQ(e->ga_len, 2);
  EXPECT_EQ(e->ga[0], 7u);
  EXPECT_EQ(e->ga[1], 8u);
  EXPECT_TRUE(e->non);
}

TEST_F(GroupTableStorage, PublisherDefaultsNonToFalseWhenAbsent)
{
  /* A stored publisher entry that never serialized the 'non' key (legacy /
   * partial record). The loader must leave 'non' at its initialized default
   * of false - it is never "void" or garbage, because oc_init_tables resets
   * every entry (non = false) before any file is parsed. */
  const uint32_t ga[] = { 7, 8 };
  stage_entry_without_non(kPubStore, 0, /*id*/ 3, /*ia*/ 0x1234, /*iid*/ 5,
                          /*fid*/ 6, /*grpid*/ 0x80000003u,
                          /*at*/ "token-pub-0", ga, 2);

  oc_create_knx_table_resources();

  oc_group_table_t *e = oc_core_get_publisher_table_entry(0);
  ASSERT_NE(e, nullptr);
  /* the other fields still load, proving the entry was parsed */
  EXPECT_EQ(e->id, 3);
  EXPECT_EQ(e->grpid, 0x80000003u);
  ASSERT_EQ(e->ga_len, 2);
  /* 'non' was absent in storage -> stays at the false default */
  EXPECT_FALSE(e->non);
}
#endif /* OC_PUBLISHER_TABLE */

TEST_F(GroupTableStorage, WriterEncodesNonAsBoolean)
{
  /* Guards the original bug directly: the 'non' key must be encoded as a CBOR
   * boolean (not a text string), so that the loader's OC_REP_BOOL case picks it
   * up. We stage with the production macros and parse the persisted bytes. */
  stage_entry(kRcpStore, 0, /*id*/ 5, /*ia*/ -1, /*iid*/ -1, /*fid*/ -1,
              /*grpid*/ 0x80000001u, /*at*/ "some-at", nullptr, 0,
              /*non*/ true);

  char filename[64];
  (void)snprintf(filename, sizeof(filename), "%s_%d", kRcpStore, 0);
  uint8_t buf[512] = { 0 };
  long read = oc_storage_read(filename, buf, sizeof(buf));
  ASSERT_GT(read, 0);

  oc_rep_t *rep = nullptr;
  ASSERT_EQ(oc_parse_rep(buf, (int)read, &rep), 0);

  bool found_bool = false;
  bool non_value = false;
  for (oc_rep_t *r = rep; r != nullptr; r = r->next) {
    if (oc_string_len(r->name) > 0 &&
        strncmp(oc_string(r->name), "non", 3) == 0) {
      EXPECT_EQ(r->type, OC_REP_BOOL); /* must be a boolean, never a string */
      found_bool = (r->type == OC_REP_BOOL);
      non_value = r->value.boolean;
    }
  }
  oc_free_rep(rep);

  EXPECT_TRUE(found_bool) << "'non' key missing or not a boolean";
  EXPECT_TRUE(non_value);
}
