/*
 * Unit tests for the echo TX ring in security/oc_oscore_engine.c (TODO 20)
 *
 * Covers: oc_oscore_echo_tx_append, oc_oscore_echo_tx_replace_kid,
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
 * Lifetime is event-bounded: the next multicast for a Group replaces that kid's
 * anchors (3.6.4.1.3), with a MIN_ARM (~5s) floor; it is NOT a short clock-based TTL.
 * Default size 32.
 */

#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>

extern "C" {
#include "messaging/coap/oscore.h"
#include "messaging/coap/coap.h"
#include "oc_buffer.h"
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

/* Helper: replace prior anchors/responders for the shared kid (next-multicast event). */
static void replace_kid()
{
  oc_oscore_echo_tx_replace_kid(g_kid, sizeof(g_kid));
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

/* ── Cleanup: ring keeps only the most recent SIZE entries ───────────── */

TEST_F(EchoTxRingTest, OldestEntry_CleanedAfterOverflow)
{
  /* lay SIZE + 1 anchors: the first (ssn 1) is overwritten by the last (ssn SIZE+1) */
  for (uint64_t i = 1; i <= (uint64_t)ECHO_TX_RING_SIZE + 1; i++) {
    store(i);
  }
  /* ssn 1 cleaned -> dropped (a reject does not append, so it cannot clean survivors) */
  EXPECT_FALSE(consume(1));
  /* the oldest survivor remains; an accept appends a responder triple and would
     start cleaning the other anchors, so assert only the oldest survivor here */
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

TEST_F(EchoTxRingTest, Replace_YoungAnchor_SurvivesAndStaysAccepted)
{
  store(10);
  /* anchor age << MIN_ARM (~5s): the in-flight echo MUST NOT be dropped */
  replace_kid();
  EXPECT_TRUE(consume(10));
}

TEST_F(EchoTxRingTest, Replace_NullOrEmptyKid_IsNoOp)
{
  store(10);
  oc_oscore_echo_tx_replace_kid(NULL, sizeof(g_kid));
  oc_oscore_echo_tx_replace_kid(g_kid, 0);
  /* ring untouched -> genuine echo still accepted */
  EXPECT_TRUE(consume(10));
}

TEST_F(EchoTxRingTest, Replace_DifferentKid_LeavesOtherKidIntact)
{
  store(10);
  uint8_t other_kid[] = {0x02};
  /* a different Group's multicast must not reclaim our kid's anchor */
  oc_oscore_echo_tx_replace_kid(other_kid, sizeof(other_kid));
  EXPECT_TRUE(consume(10));
}

/* ── Retained plaintext s-mode message (late-echo backup) ─────────────────────
   oc_oscore_echo_tx_put_retain_plaintext pins the PLAINTEXT s-mode message (ref-counted) with a
   send-anchor so a LATE 'echo response' (arriving after the s-mode CoAP transaction
   has self-cleared) can still be answered. The retained message is correlated by the
   request's CoAP TOKEN (the key the CoAP layer uses), bounded by the ring size
   (slot roll-over releases the message that lived there), and released on
   replace / clear / roll-over.

   These tests allocate a stand-in oc_message_t the same way oc_allocate_message does
   (struct + OC_PDU_SIZE data buffer, ref_count = 1) so the real oc_message_add_ref /
   oc_message_unref ref-counting and free-at-zero behaviour is exercised. put_retain HANDS
   OVER (adopts) the caller's single reference - it does NOT take an extra one - so after
   the call the ring is the sole owner (ref_count stays 1) and the test must NOT unref the
   message itself; the ring frees it via free_all / replace / roll-over. */

/* allocate a fake message with ref_count = 1 (mirrors oc_allocate_message without the
   network mutex), carrying the given token in its serialized-ish data buffer. */
static oc_message_t* make_msg(const uint8_t* token, uint8_t token_len)
{
  oc_message_t* m = (oc_message_t*)calloc(1, sizeof(oc_message_t));
  m->data = (uint8_t*)calloc(1, OC_PDU_SIZE);
  m->ref_count = 1;
  m->length = token_len;
  if (token && token_len > 0) {
    memcpy(m->data, token, token_len);
  }
  return m;
}

static uint8_t g_tok_a[] = {0x11, 0x22, 0x33, 0x44};
static uint8_t g_tok_b[] = {0x55, 0x66, 0x77, 0x88};

TEST_F(EchoTxRingTest, Retain_GetByToken_ReturnsSameMessage)
{
  oc_message_t* m = make_msg(g_tok_a, sizeof(g_tok_a)); /* ref_count = 1 (test owns it) */
  oc_oscore_echo_tx_put_retain_plaintext(10, g_kid, sizeof(g_kid), g_tok_a, sizeof(g_tok_a), m); /* HANDOVER: ring adopts the ref -> stays 1, ring now owns it */

  EXPECT_EQ(m, oc_oscore_echo_tx_get_retained_plaintext(g_tok_a, sizeof(g_tok_a)));
  EXPECT_EQ(1, m->ref_count);

  /* the (ssn, kid) send-anchor was also laid, so a genuine echo is still accepted */
  EXPECT_TRUE(consume(10));

  oc_oscore_free_all_replay_echo_records(); /* ring drops its (the only) ref -> 0, freed */
}

TEST_F(EchoTxRingTest, GetByToken_UnknownToken_ReturnsNull)
{
  oc_message_t* m = make_msg(g_tok_a, sizeof(g_tok_a));
  oc_oscore_echo_tx_put_retain_plaintext(10, g_kid, sizeof(g_kid), g_tok_a, sizeof(g_tok_a), m); /* HANDOVER: ring owns m */

  EXPECT_EQ(nullptr, oc_oscore_echo_tx_get_retained_plaintext(g_tok_b, sizeof(g_tok_b)));

  oc_oscore_free_all_replay_echo_records(); /* ring frees m */
}

TEST_F(EchoTxRingTest, Retain_FreeAll_ReleasesRetainedMessage)
{
  oc_message_t* m = make_msg(g_tok_a, sizeof(g_tok_a));
  oc_oscore_echo_tx_put_retain_plaintext(10, g_kid, sizeof(g_kid), g_tok_a, sizeof(g_tok_a), m); /* HANDOVER: ring owns m, ref stays 1 */
  EXPECT_EQ(1, m->ref_count);

  oc_oscore_free_all_replay_echo_records(); /* must unref the retained msg -> 0, freed (no leak) */
}

TEST_F(EchoTxRingTest, Replace_ReleasesYoungRetained_OnlyWhenAged)
{
  /* a freshly retained message is YOUNGER than MIN_ARM, so replace keeps it (and its ref) */
  oc_message_t* m = make_msg(g_tok_a, sizeof(g_tok_a));
  oc_oscore_echo_tx_put_retain_plaintext(10, g_kid, sizeof(g_kid), g_tok_a, sizeof(g_tok_a), m); /* HANDOVER: ring owns m, ref stays 1 */

  oc_oscore_echo_tx_replace_kid(g_kid, sizeof(g_kid)); /* young -> survives */
  EXPECT_EQ(1, m->ref_count);
  EXPECT_EQ(m, oc_oscore_echo_tx_get_retained_plaintext(g_tok_a, sizeof(g_tok_a)));

  oc_oscore_free_all_replay_echo_records(); /* ring frees m */
}

TEST_F(EchoTxRingTest, Retain_RingRollOverCleansOldestRetainedMessage)
{
  /* fill exactly ECHO_TX_RING_SIZE retained messages, each with a UNIQUE token, then add
     one more: the ring index wraps to slot 0, so the FIRST-inserted (oldest) retained
     message must be cleaned (its ring ref dropped -> freed under HANDOVER ownership). The
     number of retained messages is bounded by the ring size alone - there is no separate
     retained-message cap.

     NOTE (handover): put_retain adopts the caller's single reference, so the ring is the
     SOLE owner. Cleanup FREES the oldest message, therefore its ref_count MUST NOT be
     read afterwards - cleanup is observed via the token lookup instead. The test keeps no
     own reference and performs no own unref; free_all reclaims every still-retained slot. */
  const int cap = ECHO_TX_RING_SIZE;

  for (int i = 0; i < cap; i++) {
    uint8_t tok[4] = { (uint8_t)i, 0xAA, 0xBB, 0xCC };
    oc_message_t* m = make_msg(tok, sizeof(tok));                          /* ref 1 (adopted by ring) */
    oc_oscore_echo_tx_put_retain_plaintext((uint64_t)(100 + i), g_kid, sizeof(g_kid),
                             tok, sizeof(tok), m);                         /* HANDOVER: ring owns m */
  }

  /* every retained message is still findable by its token (pinned by the ring) */
  for (int i = 0; i < cap; i++) {
    uint8_t tok[4] = { (uint8_t)i, 0xAA, 0xBB, 0xCC };
    EXPECT_NE(nullptr, oc_oscore_echo_tx_get_retained_plaintext(tok, sizeof(tok)));
  }

  /* one more retain -> ring wraps to slot 0 -> OLDEST (i == 0) cleaned and FREED */
  uint8_t extra_tok[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
  oc_message_t* extra = make_msg(extra_tok, sizeof(extra_tok));           /* ref 1 (adopted by ring) */
  oc_oscore_echo_tx_put_retain_plaintext((uint64_t)(100 + cap), g_kid, sizeof(g_kid),
                           extra_tok, sizeof(extra_tok), extra);          /* HANDOVER: ring owns extra */

  /* the oldest token is gone (its message was cleaned + freed), the newest is present */
  uint8_t cleaned_tok[4] = { 0x00, 0xAA, 0xBB, 0xCC };
  EXPECT_EQ(nullptr, oc_oscore_echo_tx_get_retained_plaintext(cleaned_tok, sizeof(cleaned_tok)));
  EXPECT_EQ(extra, oc_oscore_echo_tx_get_retained_plaintext(extra_tok, sizeof(extra_tok)));

  /* cleanup: the ring owns all still-retained messages -> free_all releases them (no test-side unref) */
  oc_oscore_free_all_replay_echo_records();
}
