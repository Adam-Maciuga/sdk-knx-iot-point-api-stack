/*
 * Unit tests for the echo TX ring in security/oc_oscore_engine.c (TODO 20)
 *
 * Covers: oc_oscore_echo_tx_append, oc_oscore_echo_tx_supersede_kid,
 *         oc_oscore_echo_tx_check_and_consume, oc_oscore_free_all_replay_echo_records
 *         — defends against replayed S-mode 'unicast echo responses'. Per 3.6.4.1.3
 *         the echo response replays the request's kid + sequence (ssn), while
 *         kid_context is a server-side random that DIFFERS per responder. A single
 *         multicast request may be answered by many peers, so the ring is keyed on
 *         the full (ssn, kid, kid_context) triple:
 *           - store() lays a send-anchor (kid_context len 0) proving we sent it;
 *           - the first time a responder's triple arrives it is accepted and the
 *             triple is appended; a later replay of that exact triple is dropped;
 *           - a different kid_context for the same (ssn, kid) is a NEW responder
 *             and is accepted too.
 *
 * Lifetime is event-bounded: the next multicast for a Group supersedes that kid's
 * anchors (3.6.4.1.3), with a MIN_ARM (~5s) floor and a generous memory backstop;
 * it is NOT a short clock-based TTL. Default size 32.
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "messaging/coap/oscore.h"
#include "messaging/coap/coap.h"
}

/* Default ring capacity (OC_ECHO_TX_RING_SIZE) — must stay a power of two. */
static const int ECHO_TX_RING_SIZE = 32;

/* Shared kid used for store/consume; kid_len > 0 makes the entry live. */
static uint8_t g_kid[] = {0x01};

/* Two distinct responder kid_contexts (server-side randoms, 10 bytes on the wire). */
static uint8_t g_ctx_a[] = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9};
static uint8_t g_ctx_b[] = {0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9};

/* Helper: lay a send-anchor (kid_ctx_len = 0) for the given ssn and the shared kid. */
static void store(uint64_t ssn)
{
  oc_oscore_echo_tx_append(ssn, g_kid, sizeof(g_kid), NULL, 0);
}

/* Helper: consume an echo carrying the echoed ssn (as PIV), a kid and a kid_context. */
static bool consume_full(uint64_t ssn, const uint8_t* kid, uint8_t kid_len,
                         const uint8_t* kid_ctx, uint8_t kid_ctx_len)
{
  coap_packet_t pkt;
  memset(&pkt, 0, sizeof(pkt));
  oscore_store_ssn_to_piv(pkt.piv, &pkt.piv_len, ssn);
  memcpy(pkt.kid, kid, kid_len);
  pkt.kid_len = kid_len;
  if (kid_ctx && kid_ctx_len > 0) {
    memcpy(pkt.kid_ctx, kid_ctx, kid_ctx_len);
    pkt.kid_ctx_len = kid_ctx_len;
  }
  return oc_oscore_echo_tx_check_and_consume(&pkt);
}

/* Helper: consume with the shared kid and the given kid_context. */
static bool consume_ctx(uint64_t ssn, const uint8_t* kid_ctx, uint8_t kid_ctx_len)
{
  return consume_full(ssn, g_kid, sizeof(g_kid), kid_ctx, kid_ctx_len);
}

/* Helper: consume with the shared kid and responder A. */
static bool consume(uint64_t ssn)
{
  return consume_ctx(ssn, g_ctx_a, sizeof(g_ctx_a));
}

/* Helper: supersede prior anchors/responders for the shared kid (next-multicast event). */
static void supersede()
{
  oc_oscore_echo_tx_supersede_kid(g_kid, sizeof(g_kid));
}

class EchoTxRingTest : public ::testing::Test {
protected:
  void SetUp() override { oc_oscore_free_all_replay_echo_records(); }
};

/* ── Good cases: a sent SSN is honoured exactly once ──────────────────── */

TEST_F(EchoTxRingTest, StoredSSN_FirstEcho_Accepted)
{
  store(10);
  EXPECT_TRUE(consume(10));
}

TEST_F(EchoTxRingTest, MultipleStored_EachResponderAccepted)
{
  store(10);
  store(11);
  store(12);
  EXPECT_TRUE(consume(11));
  EXPECT_TRUE(consume(10));
  EXPECT_TRUE(consume(12));
}

/* ── Within the window: replay of an already-consumed SSN is dropped ──── */

TEST_F(EchoTxRingTest, TwoResponders_DifferentKidCtx_BothAccepted)
{
  store(10);
  EXPECT_TRUE(consume_ctx(10, g_ctx_a, sizeof(g_ctx_a)));  /* responder A */
  EXPECT_TRUE(consume_ctx(10, g_ctx_b, sizeof(g_ctx_b)));  /* responder B */
}

TEST_F(EchoTxRingTest, SameResponder_SecondEcho_IsReplay)
{
  store(10);
  EXPECT_TRUE(consume_ctx(10, g_ctx_a, sizeof(g_ctx_a)));   /* fresh responder A */
  EXPECT_FALSE(consume_ctx(10, g_ctx_a, sizeof(g_ctx_a)));  /* exact replay -> drop */
  /* a different responder for the same request is still accepted */
  EXPECT_TRUE(consume_ctx(10, g_ctx_b, sizeof(g_ctx_b)));
}

/* ── Outside the white-list: unknown SSN is dropped ───────────────────── */

TEST_F(EchoTxRingTest, UnknownSSN_NoStore_IsDropped)
{
  EXPECT_FALSE(consume(42));
}

TEST_F(EchoTxRingTest, UnknownSSN_AfterStore_IsDropped)
{
  store(10);
  EXPECT_FALSE(consume(11));
}

/* ── Outside the white-list: matching SSN but wrong kid is dropped ───────── */

TEST_F(EchoTxRingTest, WrongKid_SameSSN_IsDropped)
{
  store(10);
  uint8_t other_kid[] = {0x02};
  EXPECT_FALSE(consume_full(10, other_kid, sizeof(other_kid), g_ctx_a, sizeof(g_ctx_a)));
  /* genuine kid still accepted */
  EXPECT_TRUE(consume(10));
}

/* ── Eviction: ring keeps only the most recent SIZE entries ───────────── */

TEST_F(EchoTxRingTest, OldestEntry_EvictedAfterOverflow)
{
  /* lay SIZE + 1 anchors: the first (ssn 1) is overwritten by the last (ssn SIZE+1) */
  for (uint64_t i = 1; i <= (uint64_t)ECHO_TX_RING_SIZE + 1; i++) {
    store(i);
  }
  /* ssn 1 evicted -> dropped (a reject does not append, so it cannot evict survivors) */
  EXPECT_FALSE(consume(1));
  /* the oldest survivor remains; an accept appends a responder triple and would
     start evicting the other anchors, so assert only the oldest survivor here */
  EXPECT_TRUE(consume(2));
}

/* ── Clear wipes the white-list ───────────────────────────────────────── */

TEST_F(EchoTxRingTest, Clear_DropsPendingEntries)
{
  store(10);
  oc_oscore_free_all_replay_echo_records();
  EXPECT_FALSE(consume(10));
}

/* ── Supersession: the next multicast for the Group reclaims that kid's anchors ──
   Lifetime is event-bounded ("until the next multicast for the Group", 3.6.4.1.3),
   guarded by a MIN_ARM (~5s) floor so an in-flight echo is never dropped. Unit tests
   cover only the deterministic, time-independent surface: a freshly stored anchor is
   YOUNGER than MIN_ARM and therefore SURVIVES supersession, and supersession is a
   no-op for empty args / a non-matching kid. The age>MIN_ARM reclaim branch is
   inherently time-dependent (no mock clock; MIN_ARM is compiled into the library)
   and is exercised at the runtime/integration layer instead. */

TEST_F(EchoTxRingTest, Supersede_YoungAnchor_SurvivesAndStaysAccepted)
{
  store(10);
  /* anchor age << MIN_ARM (~5s): the in-flight echo MUST NOT be dropped */
  supersede();
  EXPECT_TRUE(consume(10));
}

TEST_F(EchoTxRingTest, Supersede_NullOrEmptyKid_IsNoOp)
{
  store(10);
  oc_oscore_echo_tx_supersede_kid(NULL, sizeof(g_kid));
  oc_oscore_echo_tx_supersede_kid(g_kid, 0);
  /* ring untouched -> genuine echo still accepted */
  EXPECT_TRUE(consume(10));
}

TEST_F(EchoTxRingTest, Supersede_DifferentKid_LeavesOtherKidIntact)
{
  store(10);
  uint8_t other_kid[] = {0x02};
  /* a different Group's multicast must not reclaim our kid's anchor */
  oc_oscore_echo_tx_supersede_kid(other_kid, sizeof(other_kid));
  EXPECT_TRUE(consume(10));
}
