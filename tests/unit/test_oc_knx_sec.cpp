/*
 * Unit tests for api/oc_knx_sec.c — pure functions only
 *
 * Covers: oc_at_profile_to_string
 *
 * These are the only functions in oc_knx_sec.c testable without
 * full stack initialization (no oc_main_init, no storage, no device).
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "api/oc_knx_sec.h"
#include "messaging/coap/coap.h"
#include "oc_helpers.h"
#include "util/oc_mmem.h"
#include <string.h>
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
 * OSCORE config getters/setter (file-static RAM variables)
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(OscoreConfig, ReplayWindowSizeIsRfcDefault)
{
  /* g_oscore_replay_window_size is a const initialised to 32 (RFC OSCORE). */
  EXPECT_EQ(get_oscore_replay_window_size(), 32u);
}

TEST(OscoreConfig, OsnDelayRoundTrips)
{
  const uint16_t original = get_oscore_osn_delay_ms();
  set_oscore_osn_delay_ms(1234);
  EXPECT_EQ(get_oscore_osn_delay_ms(), 1234u);
  set_oscore_osn_delay_ms(0);
  EXPECT_EQ(get_oscore_osn_delay_ms(), 0u);
  set_oscore_osn_delay_ms(original); /* restore */
}

/* ═══════════════════════════════════════════════════════════════════════════
 * AT-table slot accessors (file-static g_at_entries[G_AT_MAX_ENTRIES])
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(AuthAtEntry, OutOfBoundsReturnsNull)
{
  EXPECT_EQ(oc_get_auth_at_entry(-1), nullptr);
  EXPECT_EQ(oc_get_auth_at_entry(oc_core_get_at_table_size()), nullptr);
}

TEST(AuthAtEntry, InBoundsReturnsEntry)
{
  EXPECT_NE(oc_get_auth_at_entry(0), nullptr);
  EXPECT_NE(oc_get_auth_at_entry(oc_core_get_at_table_size() - 1), nullptr);
}

TEST(AuthAtEntry, TableSizeIsPositive)
{
  EXPECT_GT(oc_core_get_at_table_size(), 0);
}

TEST(GetAtIndex, NullReturnsNeg1)
{
  EXPECT_EQ(get_at_index(nullptr), -1);
}

TEST(GetAtIndex, EntryPointerMapsToItsSlotIndex)
{
  const int last = oc_core_get_at_table_size() - 1;
  EXPECT_EQ(get_at_index(oc_get_auth_at_entry(0)), 0);
  EXPECT_EQ(get_at_index(oc_get_auth_at_entry(last)), last);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * AT-table content operations — driven directly against the static table.
 * Each test runs in its own process (gtest_discover_tests), but the fixture
 * still frees every slot in SetUp/TearDown to keep the table deterministic.
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Helper: build a minimal coap_packet_t carrying only the given kid (OSCORE sender ID). */
static coap_packet_t make_kid_pkt(const uint8_t *kid, uint8_t kid_len)
{
  coap_packet_t pkt;
  memset(&pkt, 0, sizeof(pkt));
  if (kid && kid_len > 0)
    memcpy(pkt.kid, kid, kid_len);
  pkt.kid_len = kid_len;
  return pkt;
}

class AtTable : public ::testing::Test {
protected:
  void SetUp() override
  {
    oc_mmem_init();
    reset();
  }
  void TearDown() override { reset(); }

  // Free every AT-table slot's heap-allocated fields and zero the scalars
  // WITHOUT re-allocating empty placeholder strings. The production routine
  // oc_delete_at_table_entry() frees each field and then re-initializes it to
  // an empty heap string; using it for fixture teardown would leave a fresh
  // generation of empty strings allocated after the final scrub, which
  // LeakSanitizer reports at process exit. Freeing directly keeps the table
  // deterministic and leak-free under ASAN.
  static void reset()
  {
    int n = oc_core_get_at_table_size();
    for (int i = 0; i < n; i++) {
      oc_auth_at_t *e = oc_get_auth_at_entry(i);
      oc_free_string(&e->id);
      oc_free_string(&e->osc_ms);
      oc_free_string(&e->osc_salt);
      oc_free_string(&e->osc_contextid);
      oc_free_string(&e->osc_id);
      free(e->ga);
      e->ga = nullptr;
      e->ga_len = 0;
      e->scope = OC_ACL_NONE;
      e->profile = OC_PROFILE_UNKNOWN;
    }
  }
};

TEST_F(AtTable, ItemsUsedCountsEntriesWithId)
{
  EXPECT_EQ(oc_core_items_used_in_auth_at_table(), 0);

  oc_auth_at_t *e = oc_get_auth_at_entry(2);
  oc_new_string(&e->id, "tok", 3);
  EXPECT_EQ(oc_core_items_used_in_auth_at_table(), 1);

  oc_auth_at_t *e2 = oc_get_auth_at_entry(5);
  oc_new_string(&e2->id, "tok2", 4);
  EXPECT_EQ(oc_core_items_used_in_auth_at_table(), 2);
}

TEST_F(AtTable, FindByOscIdMatchAndMiss)
{
  oc_auth_at_t *e = oc_get_auth_at_entry(3);
  const uint8_t osc[] = { 0x01, 0x02, 0x03 };
  oc_new_byte_string(&e->osc_id, (const char *)osc, sizeof(osc));

  coap_packet_t pkt = make_kid_pkt(osc, sizeof(osc));
  EXPECT_EQ(oc_core_find_at_entry_by_osc_id(&pkt), e);

  const uint8_t other[] = { 0x09, 0x09 };
  coap_packet_t pkt_other = make_kid_pkt(other, sizeof(other));
  EXPECT_EQ(oc_core_find_at_entry_by_osc_id(&pkt_other),
            nullptr);
}

TEST_F(AtTable, DeleteEntryClearsIdAndReturnsZero)
{
  oc_auth_at_t *e = oc_get_auth_at_entry(1);
  oc_new_string(&e->id, "tok", 3);
  ASSERT_EQ(oc_core_items_used_in_auth_at_table(), 1);

  EXPECT_EQ(oc_delete_at_table_entry(e), 0);
  EXPECT_EQ(oc_string_len(e->id), 0u);
  EXPECT_EQ(e->profile, OC_PROFILE_UNKNOWN);
  EXPECT_EQ(oc_core_items_used_in_auth_at_table(), 0);
}

TEST_F(AtTable, DeleteNullReturnsNeg1)
{
  EXPECT_EQ(oc_delete_at_table_entry(nullptr), -1);
}

TEST_F(AtTable, FindAndRemovePaseTokenClearsOnlyPaseEntries)
{
  oc_auth_at_t *pase = oc_get_auth_at_entry(4);
  oc_new_string(&pase->id, "pase", 4);
  pase->profile = OC_PROFILE_COAP_PASE;

  oc_auth_at_t *osc = oc_get_auth_at_entry(7);
  oc_new_string(&osc->id, "oscore", 6);
  osc->profile = OC_PROFILE_COAP_OSCORE;

  ASSERT_EQ(oc_core_items_used_in_auth_at_table(), 2);

  oc_core_find_and_remove_pase_token_in_at_table();

  EXPECT_EQ(oc_string_len(pase->id), 0u);          /* PASE removed */
  EXPECT_EQ(pase->profile, OC_PROFILE_UNKNOWN);
  EXPECT_GT(oc_string_len(osc->id), 0u);           /* OSCORE retained */
  EXPECT_EQ(oc_core_items_used_in_auth_at_table(), 1);
}
