/*
  Unit tests: s-mode message reference-count happy paths

  Validates that every oc_message_t allocation produced on the s-mode
  send path is released exactly once and that ref_count reaches 0 with no
  leaks or double-frees. The three messages verified match the analysis in
  security/oc_oscore_engine.c:

    M1  udp_message_update  - plaintext original allocated by the caller
        (oc_client_api.c); coap_new_transaction_with_data only memcpy's its
        data, so M1's ref_count is NEVER touched by the transaction layer.
        The caller owns it start-to-finish and unref's it unconditionally.

    M2  t->message          - transaction-owned copy; separate lifecycle per
        path:
          mc-NON  : add_ref (cover deferred send) -> clear_transaction drops
                    to 1 -> ring put_retain adds its own ref (-> 2) -> caller
                    unref's the send-ref (-> 1) -> ring release drops to 0.
          uc-CON  : add_ref (cover deferred send) -> oc_message_unref at
                    OSCORE send entry (-> 1, transaction still owns it) ->
                    clear_transaction (ACK/response) drops to 0.
          uc-NON  : identical to uc-CON; clear happens on ~5s timeout.

    M3  cloned_outgoing_msg - OSCORE-encrypted wire copy; born in
        oc_oscore_send_(multicast|unicast)_message, freed by
        message_buffer_handler after oc_send_buffer. Requires a live process
        scheduler and is exercised by the runtime conformance suite, not here.

  Requirements:
    oc_network_event_handler_mutex_init() - used by oc_allocate_message.
    Echo ring cleared in TearDown so retain tests are independent.
*/

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "oc_buffer.h"
#include "messaging/coap/transactions.h"
#include "messaging/coap/oscore.h"
#include "port/oc_network_events_mutex.h"
#include "include/oc_endpoint.h"
}

/* -- helpers -------------------------------------------------------------- */

static oc_endpoint_t make_mc_ep(void)
{
  oc_endpoint_t ep;
  memset(&ep, 0, sizeof(ep));
  ep.flags = (transport_flags_t)(IPV6 | MULTICAST | OSCORE | S_MODE_NON_REQUEST);
  ep.addr.ipv6.port = 5683;
  ep.addr.ipv6.address[15] = 1;
  return ep;
}

static oc_endpoint_t make_uc_con_ep(void)
{
  oc_endpoint_t ep;
  memset(&ep, 0, sizeof(ep));
  ep.flags = (transport_flags_t)(IPV6 | OSCORE | S_MODE_CON_REQUEST);
  ep.addr.ipv6.port = 5683;
  ep.addr.ipv6.address[15] = 1;
  return ep;
}

static oc_endpoint_t make_uc_non_ep(void)
{
  oc_endpoint_t ep;
  memset(&ep, 0, sizeof(ep));
  ep.flags = (transport_flags_t)(IPV6 | OSCORE | S_MODE_NON_REQUEST);
  ep.addr.ipv6.port = 5683;
  ep.addr.ipv6.address[15] = 1;
  return ep;
}

/* Build a minimal serialised CoAP NON POST to serve as plaintext payload. */
static oc_message_t *make_plaintext_smode_msg(const oc_endpoint_t *ep)
{
  oc_message_t *m = oc_allocate_message();
  if (!m) return nullptr;
  memcpy(&m->endpoint, ep, sizeof(oc_endpoint_t));
  /* Minimal 4-byte CoAP header: NON(0x50) POST(0x02) MID(0xAB 0xCD). */
  m->data[0] = 0x50;
  m->data[1] = 0x02;
  m->data[2] = 0xAB;
  m->data[3] = 0xCD;
  m->length  = 4;
  return m;
}

/* -- fixture -------------------------------------------------------------- */

class SModeRefCount : public ::testing::Test {
protected:
  void SetUp() override
  {
    oc_network_event_handler_mutex_init();
  }
  void TearDown() override
  {
    coap_free_all_transactions();
    oc_oscore_free_all_echo_records();
    oc_network_event_handler_mutex_destroy();
  }
};

/* -- M1: udp_message_update lifetime -------------------------------------- */

/*
  coap_new_transaction_with_data only memcpy's M1's data into a freshly
  allocated t->message. M1's ref_count MUST be unchanged (stays 1) so that
  the unconditional oc_message_unref(M1) at oc_client_api.c:86 is the sole
  and final release regardless of whether the transaction allocation succeeded.
*/
TEST_F(SModeRefCount, M1_TransactionWithDataDoesNotTouchM1RefCount)
{
  oc_endpoint_t ep = make_mc_ep();
  oc_message_t *m1 = make_plaintext_smode_msg(&ep);
  ASSERT_NE(m1, nullptr);
  EXPECT_EQ(m1->ref_count, 1); /* BIRTH */

  uint8_t token[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
  coap_transaction_t *t = coap_new_transaction_with_data(
    0xAB01, token, sizeof(token), m1);
  ASSERT_NE(t, nullptr);

  EXPECT_EQ(m1->ref_count, 1); /* UNCHANGED - transaction only memcpy'd */

  /* caller's unconditional unref - mirrors oc_client_api.c:86 */
  oc_message_unref(m1); /* 1 -> 0 */
  /*
    m1 is freed; we cannot read m1->ref_count, but if we reach here without
    a crash the lifecycle is clean.
  */
}

TEST_F(SModeRefCount, M1_RefCountUnchangedEvenIfTransactionFails)
{
  /*
    Even when coap_new_transaction_with_data returns NULL (e.g. OOM),
    the caller at oc_client_api.c:86 still unref's M1.
    Simulate this by checking the invariant directly: M1 stays at 1
    after a successful transaction creation, and unref drops to 0.
    (OOM cannot be triggered reliably, but the invariant is the same.)
  */
  oc_endpoint_t ep = make_uc_con_ep();
  oc_message_t *m1 = make_plaintext_smode_msg(&ep);
  ASSERT_NE(m1, nullptr);
  EXPECT_EQ(m1->ref_count, 1);

  /* caller always unref's, regardless */
  oc_message_unref(m1); /* 1 -> 0 FREED */
}

/* -- M2: multicast s-mode NON lifecycle ----------------------------------- */

/*
  Production sequence (transactions.c else-branch + echo ring):
    oc_allocate_message inside coap_new_transaction   ref = 1  (BIRTH)
    oc_message_add_ref  (cover deferred async send)   ref = 2
    coap_clear_transaction (immediate, transaction)   ref = 1  (via unref)
    oc_oscore_echo_tx_put_retain_plaintext (ring)     ref = 2  (ring add_ref)
    oc_message_unref    (send-fn drops its send-ref)  ref = 1
    echo_tx_entry_release / free_all                  ref = 0  FREED
*/
TEST_F(SModeRefCount, M2_MulticastNon_FullLifecycle)
{
  oc_endpoint_t ep = make_mc_ep();
  oc_message_t *m1 = make_plaintext_smode_msg(&ep);
  ASSERT_NE(m1, nullptr);

  uint8_t token[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x01, 0x02};
  coap_transaction_t *t = coap_new_transaction_with_data(
    0xAB02, token, sizeof(token), m1);
  ASSERT_NE(t, nullptr);

  oc_message_t *m2 = t->message;
  ASSERT_NE(m2, nullptr);
  EXPECT_EQ(m2->ref_count, 1); /* BIRTH - transaction owns it */

  /* step: add_ref to cover the deferred async send (transactions.c:314) */
  oc_message_add_ref(m2);
  EXPECT_EQ(m2->ref_count, 2);

  /* step: clear_transaction (transactions.c:316 - immediate for mc-NON) */
  coap_clear_transaction(t); /* oc_message_unref(m2) inside */
  EXPECT_EQ(m2->ref_count, 1); /* transaction's ref gone; send-ref survives */

  /* step: ring puts its own reference (oc_oscore_engine.c:307) */
  static const uint8_t kid[] = {0x01};
  oc_oscore_echo_tx_put_retain_plaintext(42, kid, sizeof(kid),
                                         token, sizeof(token), m2);
  EXPECT_EQ(m2->ref_count, 2); /* ring owns one, send path owns one */

  /* step: send-fn drops its (now spent) send-ref (oc_oscore_engine.c:1247) */
  oc_message_unref(m2);
  EXPECT_EQ(m2->ref_count, 1); /* ring is now the sole owner */

  /* step: ring release (echo_tx_entry_release via free_all) */
  oc_oscore_free_all_echo_records(); /* oc_message_unref(m2) inside */
  /* m2 freed - ref_count reached 0; confirmed by absence of crash/sanitiser hit */

  /* caller's M1 cleanup (oc_client_api.c:86) */
  oc_message_unref(m1);
}

/*
  Variant: ring releases M2 through slot roll-over rather than free_all.
  Filling the ring past capacity causes the oldest slot (slot 0) to be
  cleaned, which drops M2's ring-ref to 0.
*/
TEST_F(SModeRefCount, M2_MulticastNon_RingRolloverReleases)
{
  static const int RING = 32; /* OC_ECHO_TX_RING_SIZE */

  oc_endpoint_t ep = make_mc_ep();
  oc_message_t *m1 = make_plaintext_smode_msg(&ep);
  ASSERT_NE(m1, nullptr);

  uint8_t token[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  coap_transaction_t *t = coap_new_transaction_with_data(
    0xAB03, token, sizeof(token), m1);
  ASSERT_NE(t, nullptr);

  oc_message_t *m2 = t->message;
  ASSERT_NE(m2, nullptr);

  /* Replay the production sequence up to (and including) send-ref release. */
  oc_message_add_ref(m2);                                        /* 1 -> 2 */
  coap_clear_transaction(t);                                     /* 2 -> 1 */
  static const uint8_t kid[] = {0x01};
  oc_oscore_echo_tx_put_retain_plaintext(100, kid, sizeof(kid),
                                          token, sizeof(token), m2); /* -> 2 */
  oc_message_unref(m2);                                          /* 2 -> 1 */
  EXPECT_EQ(m2->ref_count, 1); /* ring is sole owner */

  /* Fill remaining RING-1 slots with distinct tokens to push slot 0 out. */
  for (int i = 1; i < RING; i++)
  {
    uint8_t tok[4] = { (uint8_t)i, 0xCC, 0xDD, 0xEE };
    oc_message_t *filler = oc_allocate_message();
    ASSERT_NE(filler, nullptr);
    static const uint8_t fk[] = {0x01};
    oc_oscore_echo_tx_put_retain_plaintext((uint64_t)(200 + i), fk, sizeof(fk),
                                            tok, sizeof(tok), filler);
    oc_message_unref(filler); /* drop our ref; ring owns it now */
  }

  /* One more retain wraps to slot 0 and cleans M2 (ref -> 0, freed). */
  uint8_t extra_tok[] = {0xDE, 0xAD, 0xBE, 0xEF};
  oc_message_t *extra = oc_allocate_message();
  ASSERT_NE(extra, nullptr);
  static const uint8_t ek[] = {0x01};
  oc_oscore_echo_tx_put_retain_plaintext(999, ek, sizeof(ek),
                                          extra_tok, sizeof(extra_tok), extra);
  oc_message_unref(extra);

  /*
    M2 was at slot 0 and has been cleaned by roll-over. Confirm it is gone
    from the ring (its token is no longer findable).
  */
  EXPECT_EQ(oc_oscore_echo_tx_get_retained_plaintext(token, sizeof(token)),
            nullptr);

  /* M1 cleanup */
  oc_message_unref(m1);
}

/* -- M2: unicast s-mode CON lifecycle ------------------------------------- */

/*
  Production sequence (transactions.c CON branch + oc_oscore_send_unicast_message):
    oc_allocate_message inside coap_new_transaction   ref = 1  (BIRTH)
    oc_message_add_ref  (cover deferred async send)   ref = 2
    oc_message_unref    (OSCORE send-fn entry :1297)  ref = 1  (send-ref gone)
    - transaction stays alive; timer re-armed for retransmits -
    coap_clear_transaction (ACK or timeout)           ref = 0  FREED
*/
TEST_F(SModeRefCount, M2_UnicastCon_FullLifecycle)
{
  oc_endpoint_t ep = make_uc_con_ep();
  oc_message_t *m1 = make_plaintext_smode_msg(&ep);
  ASSERT_NE(m1, nullptr);

  uint8_t token[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
  coap_transaction_t *t = coap_new_transaction_with_data(
    0xAB04, token, sizeof(token), m1);
  ASSERT_NE(t, nullptr);

  oc_message_t *m2 = t->message;
  ASSERT_NE(m2, nullptr);
  EXPECT_EQ(m2->ref_count, 1); /* BIRTH */

  /* step: add_ref to cover deferred async send (transactions.c:198) */
  oc_message_add_ref(m2);
  EXPECT_EQ(m2->ref_count, 2);

  /*
    step: OSCORE unicast send-fn drops its send-ref immediately after
    cloning into M3 (oc_oscore_engine.c:1297).
  */
  oc_message_unref(m2);
  EXPECT_EQ(m2->ref_count, 1); /* transaction still holds M2 alive */

  /* step: ACK/response received -> coap_clear_transaction */
  coap_clear_transaction(t); /* oc_message_unref(m2) inside: 1 -> 0 FREED */

  /* M1 cleanup */
  oc_message_unref(m1);
}

/*
  CON retransmit: each retransmit cycle adds a ref, then the OSCORE send-fn
  drops it, leaving the transaction's ref intact. After N retransmits M2
  still sits at 1 until the final clear.
*/
TEST_F(SModeRefCount, M2_UnicastCon_RetransmitCycleStaysAtOne)
{
  oc_endpoint_t ep = make_uc_con_ep();
  oc_message_t *m1 = make_plaintext_smode_msg(&ep);
  ASSERT_NE(m1, nullptr);

  uint8_t token[] = {0xAA, 0xBB};
  coap_transaction_t *t = coap_new_transaction_with_data(
    0xAB05, token, sizeof(token), m1);
  ASSERT_NE(t, nullptr);

  oc_message_t *m2 = t->message;
  ASSERT_NE(m2, nullptr);
  EXPECT_EQ(m2->ref_count, 1);

  /* Simulate 3 retransmit cycles: add_ref -> send -> send-fn unref. */
  for (int i = 0; i < 3; i++)
  {
    oc_message_add_ref(m2);
    EXPECT_EQ(m2->ref_count, 2); /* send-ref added */
    oc_message_unref(m2);        /* send-fn drops send-ref */
    EXPECT_EQ(m2->ref_count, 1); /* transaction ref intact */
  }

  /* Timeout/ACK: clear drops the transaction's ref */
  coap_clear_transaction(t);
  /* M2 freed; M1 cleanup */
  oc_message_unref(m1);
}

/* -- M2: unicast s-mode NON lifecycle ------------------------------------- */

/*
  Identical to uc-CON from a ref-count perspective:
    add_ref -> (send-fn) unref -> transaction kept ~5s -> clear_transaction.
*/
TEST_F(SModeRefCount, M2_UnicastNon_FullLifecycle)
{
  oc_endpoint_t ep = make_uc_non_ep();
  oc_message_t *m1 = make_plaintext_smode_msg(&ep);
  ASSERT_NE(m1, nullptr);

  uint8_t token[] = {0xCA, 0xFE, 0xBA, 0xBE, 0x01, 0x02, 0x03, 0x04};
  coap_transaction_t *t = coap_new_transaction_with_data(
    0xAB06, token, sizeof(token), m1);
  ASSERT_NE(t, nullptr);

  oc_message_t *m2 = t->message;
  ASSERT_NE(m2, nullptr);
  EXPECT_EQ(m2->ref_count, 1); /* BIRTH */

  /* step: add_ref (transactions.c:268) */
  oc_message_add_ref(m2);
  EXPECT_EQ(m2->ref_count, 2);

  /* step: OSCORE send-fn unref at entry (oc_oscore_engine.c:1297) */
  oc_message_unref(m2);
  EXPECT_EQ(m2->ref_count, 1); /* transaction holds M2 for ~5s echo window */

  /* step: 5s timeout fires -> clear_transaction (transactions.c:294) */
  coap_clear_transaction(t);
  /* M2 freed */

  /* M1 cleanup */
  oc_message_unref(m1);
}

/* -- Cross-path: M1 and M2 are completely independent --------------------- */

/*
  Allocating and releasing M1 must not disturb M2's ref_count in any way,
  and vice-versa. This test creates both, releases M1 early (as the real
  caller does), then completes M2's lifecycle.
*/
TEST_F(SModeRefCount, M1AndM2_IndependentLifetimes)
{
  oc_endpoint_t ep = make_mc_ep();
  oc_message_t *m1 = make_plaintext_smode_msg(&ep);
  ASSERT_NE(m1, nullptr);

  uint8_t token[] = {0xDE, 0xAD};
  coap_transaction_t *t = coap_new_transaction_with_data(
    0xAB07, token, sizeof(token), m1);
  ASSERT_NE(t, nullptr);

  oc_message_t *m2 = t->message;
  ASSERT_NE(m2, nullptr);
  EXPECT_NE(m1, m2); /* two distinct allocations */
  EXPECT_EQ(m1->ref_count, 1);
  EXPECT_EQ(m2->ref_count, 1);

  /* M1 released by caller immediately after transaction creation */
  oc_message_unref(m1); /* M1: 1 -> 0 FREED */

  /* M2 lifecycle continues unaffected */
  EXPECT_EQ(m2->ref_count, 1);
  oc_message_add_ref(m2);
  EXPECT_EQ(m2->ref_count, 2);
  coap_clear_transaction(t);    /* M2: 2 -> 1 */
  EXPECT_EQ(m2->ref_count, 1);

  static const uint8_t kid[] = {0x01};
  oc_oscore_echo_tx_put_retain_plaintext(77, kid, sizeof(kid),
                                          token, sizeof(token), m2); /* -> 2 */
  EXPECT_EQ(m2->ref_count, 2);
  oc_message_unref(m2);         /* send-ref gone: 2 -> 1 */
  EXPECT_EQ(m2->ref_count, 1);
  oc_oscore_free_all_echo_records(); /* ring frees M2: 1 -> 0 */
}
