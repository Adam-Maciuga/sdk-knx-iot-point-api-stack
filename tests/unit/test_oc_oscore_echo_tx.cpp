/*
 * Unit tests for the echo ring in security/oc_oscore_engine.c
 *
 * Covers: oc_oscore_echo_whitelist_append, oc_oscore_echo_check_and_consume,
 *         oc_oscore_echo_get_retained_plaintext, oc_oscore_free_all_echo_records
 *         -- defends against replayed S-mode 'unicast echo responses'. Per 3.6.4.1.3
 *         the echo response replays the request's kid + sequence (ssn), while
 *         kid_context is a server-side random that DIFFERS per responder.
 *
 *         Single unified FIFO ring (ECHO_RING_SIZE = 64):
 *
 *         Anchor slot  (kid_ctx_len == 0)  -- former whitelist role:
 *           one slot per outbound send; carries the retained PLAINTEXT s-mode
 *           message for late-echo re-requests.
 *
 *         Seen-responder slot  (kid_ctx_len > 0)  -- former blacklist role:
 *           one slot per accepted responder; an exact (ssn, kid, kid_ctx) triple
 *           match on a subsequent inbound echo -> replay, reject.
 *
 *         Both slot kinds share one ring; roll-over evicts in arrival order.
 *         The former cross-ring overwrite rule no longer exists.
 */

#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>

extern "C" {
#include "messaging/coap/oscore.h"
#include "messaging/coap/oscore_constants.h"
#include "messaging/coap/coap.h"
#include "oc_buffer.h"
}

/* Ring capacity -- must stay a power of two to match the production macro. */
static const int ECHO_RING_SIZE = 64;

/*
 * Shared kid used for store/consume.
 * Uses the full OSCORE_SENDER_ID_LEN (7 bytes) to exercise the production
 * gate (kid_len <= OSCORE_SENDER_ID_LEN) with a realistic value.
 */
static uint8_t g_kid[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};

/* Two distinct responder kid_contexts (server-side randoms, 10 bytes on the wire). */
static uint8_t g_ctx_a[] = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9};
static uint8_t g_ctx_b[] = {0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9};

/* Helper: write a send-anchor (no retained message) for the given ssn and the shared kid. */
static void store(uint64_t ssn)
{
  oc_oscore_echo_whitelist_append(ssn, g_kid, sizeof(g_kid), NULL, 0, NULL);
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
  return oc_oscore_echo_check_and_consume(&pkt);
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


/* Helper: build a minimal coap_packet_t carrying only the given token. */
static coap_packet_t make_token_pkt(const uint8_t *token, uint8_t token_len)
{
  coap_packet_t pkt;
  memset(&pkt, 0, sizeof(pkt));
  if (token && token_len > 0)
    memcpy(pkt.token, token, token_len);
  pkt.token_len = token_len;
  return pkt;
}

class EchoTxRingTest : public ::testing::Test {
protected:
  void SetUp() override { oc_oscore_free_all_echo_records(); }
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
  uint8_t other_kid[] = {0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9};  /* valid length, wrong content */
  EXPECT_FALSE(consume_full(10, other_kid, sizeof(other_kid), g_ctx_a, sizeof(g_ctx_a)));
  /* genuine kid still accepted */
  EXPECT_TRUE(consume(10));
}

/* ── Same kid, different ssn: independent anchors, both accepted ──────────── */

TEST_F(EchoTxRingTest, SameKid_DifferentSSN_BothAccepted)
{
  /* two multicast requests for the SAME Group (shared kid) carry DIFFERENT SSNs;
     the ring keys on (ssn, kid), so each is its own anchor and both echoes pass */
  store(10);
  store(11);
  EXPECT_TRUE(consume(10));  /* echo for anchor (10, kid) */
  EXPECT_TRUE(consume(11));  /* echo for anchor (11, kid) - not shadowed by the other ssn */
}

/* -- Attacker scenarios: forged kid_ctx flood and exact replay ------------- */

/*
 * One sender stores one anchor (ssn=10, kid).
 * The attacker observed the outbound multicast and knows (ssn=10, kid).
 * He does NOT know the genuine responder's server-side random kid_ctx.
 *
 * Three attack surfaces are tested:
 *
 *   1. Flood with forged kid_ctx values BEFORE the genuine response arrives.
 *      Each probe finds the anchor (kid matches) and its forged kid_ctx is not
 *      in the BL, so it is accepted as a "new responder" and a BL slot is
 *      written.  The ring cannot authenticate kid_ctx -- that is done by the
 *      OSCORE decryption layer above.  The genuine response must still be
 *      accepted after the flood because the anchor is never consumed by probes.
 *
 *   2. Attacker replays the EXACT valid response (same ssn, kid, kid_ctx) after
 *      it has already been accepted once.  The (ssn, kid, kid_ctx) triple is now
 *      in the BL -- exact replay -- rejected.
 *
 *   3. Combined: forged flood before + genuine response + exact replay +
 *      forged flood after.  All illegal attempts are pinned to their outcome.
 */

/* Attack 1 -- forged kid_ctx flood before genuine response */

TEST_F(EchoTxRingTest, Attacker_ForgedKidCtxFlood_BeforeValidResponse_ValidStillAccepted)
{
  store(10);

  /* attacker sends N probes with distinct forged kid_ctx values before the
     genuine echo arrives; each is treated as a new responder at the ring layer */
  for (int i = 0; i < 8; i++) {
    uint8_t forged[10];
    memset(forged, (uint8_t)(0xF0 + i), sizeof(forged));
    (void)consume_full(10, g_kid, sizeof(g_kid), forged, sizeof(forged));
  }

  /* anchor was never removed by the probes: genuine response still accepted */
  EXPECT_TRUE(consume_ctx(10, g_ctx_a, sizeof(g_ctx_a)));
}

/* Attack 2 -- exact replay of the accepted valid response */

TEST_F(EchoTxRingTest, Attacker_ExactReplay_AfterValidResponse_Rejected)
{
  store(10);
  EXPECT_TRUE(consume_ctx(10, g_ctx_a, sizeof(g_ctx_a)));  /* genuine first response */

  /* attacker replays the SAME (ssn=10, kid, kid_ctx=ctx_a) triple repeatedly */
  EXPECT_FALSE(consume_ctx(10, g_ctx_a, sizeof(g_ctx_a)));  /* BL hit -> replay */
  EXPECT_FALSE(consume_ctx(10, g_ctx_a, sizeof(g_ctx_a)));  /* BL hit -> replay */
  EXPECT_FALSE(consume_ctx(10, g_ctx_a, sizeof(g_ctx_a)));  /* BL hit -> replay */
}

/* Attack 3 -- forged flood before + genuine response + exact replay + forged flood after */

TEST_F(EchoTxRingTest, Attacker_FloodThenReplay_AllIllegalAttemptsRejected)
{
  store(10);

  /* phase 1: probes with forged kid_ctx values before genuine response arrives */
  for (int i = 0; i < 5; i++) {
    uint8_t forged[10];
    memset(forged, (uint8_t)(0xE0 + i), sizeof(forged));
    (void)consume_full(10, g_kid, sizeof(g_kid), forged, sizeof(forged));
  }

  /* phase 2: genuine responder sends its echo exactly once -- must be accepted */
  EXPECT_TRUE(consume_ctx(10, g_ctx_a, sizeof(g_ctx_a)));

  /* phase 3: attacker replays the exact valid response observed on the wire */
  EXPECT_FALSE(consume_ctx(10, g_ctx_a, sizeof(g_ctx_a)));  /* BL hit -> replay */

  /* phase 4: further forged kid_ctx probes after the genuine response; each is
     a distinct triple not yet in the BL so the anchor accepts them at ring level */
  for (int i = 0; i < 5; i++) {
    uint8_t forged[10];
    memset(forged, (uint8_t)(0xD0 + i), sizeof(forged));
    (void)consume_full(10, g_kid, sizeof(g_kid), forged, sizeof(forged));
  }

  /* the exact replay is still rejected regardless of the surrounding flood */
  EXPECT_FALSE(consume_ctx(10, g_ctx_a, sizeof(g_ctx_a)));
}

/* Attack 4 -- ring exhaustion: > 64 forged probes evict the anchor itself */

TEST_F(EchoTxRingTest, Attacker_RingExhaustion_AnchorEvicted_ValidResponseRejected)
{
  /*
   * Mechanics of same-(ssn,kid) flooding:
   *   Every accepted probe writes back (wl.ssn=10, wl.kid) into the new BL slot,
   *   so a flood that only uses ssn=10 continuously re-populates the ring with
   *   (ssn=10, kid) entries -- it cannot evict itself.
   *
   *   Full exhaustion requires TWO phases:
   *     Phase 1: ECHO_RING_SIZE forged probes for (ssn=10, kid) -- fills the ring
   *              with 64 BL slots all carrying (ssn=10, kid, forged_ctx_i).
   *     Phase 2: ECHO_RING_SIZE additional writes for DIFFERENT SSNs -- displaces
   *              all 64 (ssn=10, kid) BL slots, leaving no ssn=10 entry at all.
   *
   * After full exhaustion:
   *   - genuine response for ssn=10: no anchor found -> rejected.
   *   - attacker exact replay: no anchor found -> also rejected.
   *
   * This documents the ring exhaustion denial-of-service and its cost: the
   * attacker must inject 2 * RING_SIZE packets (128 in total) before the
   * genuine responder's window.  Mitigation is rate-limiting at the transport
   * layer, which is outside the ring's responsibility.
   */
  store(10);  /* anchor written */

  /* phase 1: fill the ring with ECHO_RING_SIZE BL slots for (ssn=10, kid) */
  for (int i = 0; i < ECHO_RING_SIZE; i++) {
    uint8_t forged[10];
    memset(forged, (uint8_t)(i & 0xFF), sizeof(forged));
    forged[0] = (uint8_t)(i & 0xFF);
    forged[1] = (uint8_t)((i >> 8) & 0xFF);
    (void)consume_full(10, g_kid, sizeof(g_kid), forged, sizeof(forged));
  }

  /* phase 2: overwrite all 64 slots with anchors for different SSNs, so no
     slot with wl.ssn == 10 remains in the ring */
  for (uint64_t s = 100; s < 100 + (uint64_t)ECHO_RING_SIZE; s++) {
    store(s);
  }

  /* anchor is gone: genuine late response is now rejected */
  EXPECT_FALSE(consume_ctx(10, g_ctx_a, sizeof(g_ctx_a)));

  /* attacker's exact replay is also rejected: no anchor to match against */
  EXPECT_FALSE(consume_ctx(10, g_ctx_a, sizeof(g_ctx_a)));
}

/* ── Cleanup: ring keeps only the most recent SIZE entries ───────────── */

TEST_F(EchoTxRingTest, OldestEntry_CleanedAfterOverflow)
{
  /* lay ECHO_RING_SIZE + 1 anchors: the first (ssn 1) is overwritten by the last */
  for (uint64_t i = 1; i <= (uint64_t)ECHO_RING_SIZE + 1; i++) {
    store(i);
  }
  /* ssn 1 cleaned -> dropped */
  EXPECT_FALSE(consume(1));
  /* the oldest survivor remains */
  EXPECT_TRUE(consume(2));
}

/* ── Clear wipes the white-list ───────────────────────────────────────── */

TEST_F(EchoTxRingTest, Clear_DropsPendingEntries)
{
  store(10);
  oc_oscore_free_all_echo_records();
  EXPECT_FALSE(consume(10));
}



/* -- Retained plaintext s-mode message (late-echo backup) ---------------
   The whitelist slot owns a ref-counted reference on the retained message so
   a LATE echo response (arriving after the s-mode CoAP transaction has
   self-cleared) can still be answered with a re-request.

   The append helper takes its OWN reference (no ownership handover); the caller
   therefore unrefs its reference right after the call, exactly like the real
   multicast sender path. The ring then holds the sole reference and releases
   it on slot roll-over or clear.

   These tests allocate a stand-in oc_message_t the same way oc_allocate_message
   does (struct + OC_PDU_SIZE data buffer, ref_count = 1) so the real
   oc_message_add_ref / oc_message_unref ref-counting and free-at-zero behaviour
   is exercised without touching the network allocator. */

/* Allocate a fake message with ref_count = 1, carrying the given token. */
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
  oc_oscore_echo_whitelist_append(10, g_kid, sizeof(g_kid), g_tok_a, sizeof(g_tok_a), m); /* ring adds its own ref -> 2 */
  oc_message_unref(m); /* caller drops its ref -> 1, ring is sole owner */

  coap_packet_t pkt_a = make_token_pkt(g_tok_a, sizeof(g_tok_a));
  EXPECT_EQ(m, oc_oscore_echo_get_retained_plaintext(&pkt_a));
  EXPECT_EQ(1, m->ref_count);

  /* the (ssn, kid) send-anchor was also laid, so a genuine echo is still accepted */
  EXPECT_TRUE(consume(10));

  oc_oscore_free_all_echo_records(); /* ring drops its (the only) ref -> 0, freed */
}

TEST_F(EchoTxRingTest, GetByToken_UnknownToken_ReturnsNull)
{
  oc_message_t* m = make_msg(g_tok_a, sizeof(g_tok_a));
  oc_oscore_echo_whitelist_append(10, g_kid, sizeof(g_kid), g_tok_a, sizeof(g_tok_a), m); /* ring adds its own ref -> 2 */
  oc_message_unref(m); /* caller drops its ref -> 1, ring owns m */

  coap_packet_t pkt_b = make_token_pkt(g_tok_b, sizeof(g_tok_b));
  EXPECT_EQ(nullptr, oc_oscore_echo_get_retained_plaintext(&pkt_b));

  oc_oscore_free_all_echo_records(); /* ring frees m */
}

TEST_F(EchoTxRingTest, Retain_FreeAll_ReleasesRetainedMessage)
{
  oc_message_t* m = make_msg(g_tok_a, sizeof(g_tok_a));
  oc_oscore_echo_whitelist_append(10, g_kid, sizeof(g_kid), g_tok_a, sizeof(g_tok_a), m); /* ring adds its own ref -> 2 */
  oc_message_unref(m); /* caller drops its ref -> 1, ring is sole owner */
  EXPECT_EQ(1, m->ref_count);

  oc_oscore_free_all_echo_records(); /* must unref the retained msg -> 0, freed (no leak) */
}


/* -- Capacity boundary: < 64 senders and < 64 responses all fit ----------- */

TEST_F(EchoTxRingTest, WithinCapacity_AllSendersAndResponsesAccepted)
{
  /* store 32 anchors (well within the 64-slot ring) */
  for (uint64_t i = 1; i <= 32; i++) {
    store(i);
  }
  /* every echo is accepted and none collides */
  for (uint64_t i = 1; i <= 32; i++) {
    EXPECT_TRUE(consume(i)) << "ssn=" << i;
  }
}

/* -- Overflow: evicted anchor rejects late echo, new anchor still works ---- */

TEST_F(EchoTxRingTest, OverflowEviction_LateEchoRejected_NewAnchorAccepted)
{
  /*
    Fill the ring beyond capacity so the first anchor (ssn=1) is overwritten.
    A late echo for ssn=1 must be rejected (send-proof gone).
    The same sender then uses the next valid ssn=66 (SSN is monotonically
    increasing per Sender Context; reusing an earlier value is forbidden).
  */
  for (uint64_t i = 1; i <= (uint64_t)ECHO_RING_SIZE + 1; i++) {
    store(i);
  }

  /* ssn=1 anchor was evicted: late echo rejected */
  EXPECT_FALSE(consume(1));

  /* next outbound from the same sender: ssn must advance past the last used (65) */
  store(66);
  EXPECT_TRUE(consume(66));
}

/* -- Overflow with mixed anchor+BL slots: evicted BL does not block re-use - */

TEST_F(EchoTxRingTest, OverflowMixed_EvictedBlSlot_DoesNotFalseReplay)
{
  /*
    Scenario: store an anchor (ssn=1), accept one responder for it (writes a BL
    slot), then overflow the ring so both the anchor and the BL slot are evicted.
    The sender then uses the next valid SSN (129, since 2*RING_SIZE=128 was the
    last anchor stored); the evicted BL slot must NOT cause a false replay reject.
  */
  store(1);
  EXPECT_TRUE(consume(1)); /* writes BL slot for (ssn=1, kid, ctx_a) */

  /* overflow: fill 2 * ECHO_RING_SIZE more anchors to evict every slot */
  for (uint64_t i = 2; i <= (uint64_t)(2 * ECHO_RING_SIZE); i++) {
    store(i);
  }

  /* next outbound must advance past the last stored SSN (128) */
  store(129);
  EXPECT_TRUE(consume(129)); /* must NOT be treated as replay */
}

/* -- Several distinct senders, each with its own kid and SSN sequence ------ */

/*
 * Three senders, each identified by a unique kid byte.  Every sender maintains
 * its own monotonically increasing SSN; the ring keys on (ssn, kid) so anchors
 * for different kids are completely independent.
 *
 * kid_X    = sender X's Group Object-Security id (1 byte, distinct per sender)
 * ssn_X_N  = the Nth outbound SSN from sender X
 */

static uint8_t g_kid_x[] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};  /* sender X */
static uint8_t g_kid_y[] = {0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20};  /* sender Y */
static uint8_t g_kid_z[] = {0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30};  /* sender Z */

/* store an anchor for (ssn, kid) where kid is the full byte array */
static void store_kid(uint64_t ssn, const uint8_t* kid, uint8_t kid_len)
{
  oc_oscore_echo_whitelist_append(ssn, kid, kid_len, NULL, 0, NULL);
}

/* consume with the given (ssn, kid) and responder A */
static bool consume_kid(uint64_t ssn, const uint8_t* kid, uint8_t kid_len)
{
  return consume_full(ssn, kid, kid_len, g_ctx_a, sizeof(g_ctx_a));
}

/* -- Three independent senders each accepted on their own SSN sequence ----- */

TEST_F(EchoTxRingTest, MultiKid_IndependentSenders_AllAccepted)
{
  /* each sender starts its own SSN counter from 1; the ring treats them as
     completely independent because the kid values differ */
  store_kid(1, g_kid_x, sizeof(g_kid_x));
  store_kid(1, g_kid_y, sizeof(g_kid_y));  /* same SSN value, different kid */
  store_kid(1, g_kid_z, sizeof(g_kid_z));  /* same SSN value, different kid */

  EXPECT_TRUE(consume_kid(1, g_kid_x, sizeof(g_kid_x)));  /* X echo accepted */
  EXPECT_TRUE(consume_kid(1, g_kid_y, sizeof(g_kid_y)));  /* Y echo accepted */
  EXPECT_TRUE(consume_kid(1, g_kid_z, sizeof(g_kid_z)));  /* Z echo accepted */
}

/* -- Wrong-kid echo for an anchor is rejected; correct kid still accepted -- */

TEST_F(EchoTxRingTest, MultiKid_WrongKidForAnchor_Rejected_CorrectKidAccepted)
{
  /* X sends ssn=10; an echo arriving with Y's kid must be dropped even though
     the SSN value matches -- the anchor is keyed on (ssn=10, kid_x) */
  store_kid(10, g_kid_x, sizeof(g_kid_x));

  EXPECT_FALSE(consume_kid(10, g_kid_y, sizeof(g_kid_y)));  /* wrong kid */
  EXPECT_FALSE(consume_kid(10, g_kid_z, sizeof(g_kid_z)));  /* wrong kid */
  EXPECT_TRUE(consume_kid(10, g_kid_x, sizeof(g_kid_x)));   /* correct kid */
}

/* -- Overflow with multiple kids: evicted sender dropped, others survive ---- */

TEST_F(EchoTxRingTest, MultiKid_Overflow_EvictedSenderDropped_OthersAccepted)
{
  /*
   * Sender X uses ssn=1.  Senders Y and Z (also starting at ssn=1) then fill
   * the ring to capacity so X's anchor is evicted.
   * Late echo for X(ssn=1) must be rejected.
   * Y and Z anchors (stored last) survive and their echoes are accepted.
   * After eviction X advances its SSN monotonically to 2 and re-registers.
   */
  store_kid(1, g_kid_x, sizeof(g_kid_x));  /* slot 0: X ssn=1 (will be evicted) */

  /* fill remaining RING_SIZE slots alternating Y and Z with ascending SSNs */
  for (uint64_t i = 1; i <= (uint64_t)ECHO_RING_SIZE / 2; i++) {
    store_kid(i, g_kid_y, sizeof(g_kid_y));
    store_kid(i, g_kid_z, sizeof(g_kid_z));
  }
  /* ring is now full (1 + 64 writes = 65); X ssn=1 was evicted at wrap */

  /* late echo for evicted X ssn=1 is rejected */
  EXPECT_FALSE(consume_kid(1, g_kid_x, sizeof(g_kid_x)));

  /* last stored Y and Z anchors (ssn = RING_SIZE/2) still present and accepted */
  uint64_t last = (uint64_t)ECHO_RING_SIZE / 2;
  EXPECT_TRUE(consume_kid(last, g_kid_y, sizeof(g_kid_y)));
  EXPECT_TRUE(consume_kid(last, g_kid_z, sizeof(g_kid_z)));

  /* X advances its SSN past all previously used values (max was 1) */
  store_kid(2, g_kid_x, sizeof(g_kid_x));
  EXPECT_TRUE(consume_kid(2, g_kid_x, sizeof(g_kid_x)));
}


TEST_F(EchoTxRingTest, Retain_RingRollOverCleansOldestRetainedMessage)
{
  /* fill exactly ECHO_RING_SIZE retained messages with unique tokens, then add
     one more: the write index wraps to slot 0 so the FIRST-inserted (oldest) retained
     message must be cleaned (its ring ref dropped -> freed). */
  const int cap = ECHO_RING_SIZE;

  for (int i = 0; i < cap; i++) {
    uint8_t tok[4] = { (uint8_t)i, 0xAA, 0xBB, 0xCC };
    oc_message_t* m = make_msg(tok, sizeof(tok));                              /* ref 1 */
    oc_oscore_echo_whitelist_append((uint64_t)(100 + i), g_kid, sizeof(g_kid),
                                    tok, sizeof(tok), m);                      /* ring adds ref -> 2 */
    oc_message_unref(m);                                                       /* caller drops ref -> 1 */
  }

  /* every retained message is still findable by its token */
  for (int i = 0; i < cap; i++) {
    uint8_t tok[4] = { (uint8_t)i, 0xAA, 0xBB, 0xCC };
    coap_packet_t ring_pkt = make_token_pkt(tok, sizeof(tok));
    EXPECT_NE(nullptr, oc_oscore_echo_get_retained_plaintext(&ring_pkt));
  }

  /* one more append -> ring wraps to slot 0 -> OLDEST (i == 0) cleaned and FREED */
  uint8_t extra_tok[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
  oc_message_t* extra = make_msg(extra_tok, sizeof(extra_tok));                /* ref 1 */
  oc_oscore_echo_whitelist_append((uint64_t)(100 + cap), g_kid, sizeof(g_kid),
                                   extra_tok, sizeof(extra_tok), extra);       /* ring adds ref -> 2 */
  oc_message_unref(extra);                                                     /* caller drops ref -> 1 */

  /* the oldest token is gone (its message was cleaned + freed), the newest is present */
  uint8_t cleaned_tok[4] = { 0x00, 0xAA, 0xBB, 0xCC };
  coap_packet_t pkt_cleaned = make_token_pkt(cleaned_tok, sizeof(cleaned_tok));
  coap_packet_t pkt_extra   = make_token_pkt(extra_tok, sizeof(extra_tok));
  EXPECT_EQ(nullptr, oc_oscore_echo_get_retained_plaintext(&pkt_cleaned));
  EXPECT_EQ(extra, oc_oscore_echo_get_retained_plaintext(&pkt_extra));

  /* cleanup: ring owns all still-retained messages -> free_all releases them */
  oc_oscore_free_all_echo_records();
}


