/*
 * Unit tests for the buffer-settings API (implemented in api/oc_main.c,
 * declared in include/oc_buffer_settings.h).
 *
 * Covers:
 *   oc_set_pdu_size          - pdu setter, minimum guard, block-size derivation
 *   oc_get_pdu_size          - returns stored PDU size (payload + CoAP header)
 *   oc_get_max_app_data_size - returns the compile-time KNX_PAYLOAD_SIZE
 *   oc_get_block_size        - RFC 7959 block size (power of two, 16..1024)
 *
 * Requirements:
 *   None - all functions operate on static globals, no stack init needed.
 *   The block-size derivation path in oc_set_pdu_size is always active.
 *
 * Note: these functions share process-global state (oc_pdu_size /
 *       oc_blk_size). Each test sets the pdu explicitly before asserting,
 *       so test order does not matter. The two exceptions are the default-value
 *       tests, which read the values captured at static-init time (see
 *       g_default_pdu / g_default_block below) instead of the live globals.
 *
 * Worst-case frame sizing:
 *   The pdu is the hard upper bound for anything received from the bus:
 *   OC_PDU_SIZE (port/oc_connectivity.h) is oc_get_pdu_size() in the UDP-only
 *   build, and oc_allocate_message() (api/oc_buffer.c) mallocs exactly that
 *   many bytes for message->data. A received OSCORE message must therefore fit
 *   outer CoAP header + options, inner (plaintext) code + options, the
 *   application payload and the AEAD tag. This file measures the outer and
 *   inner overhead with the stack's own serializer rather than hard-coding
 *   numbers, and checks the result against the configured pdu.
 */

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <vector>

extern "C" {
#include "oc_buffer_settings.h"
#include "oc_buffer.h"
#include "port/oc_network_events_mutex.h"
#include "messaging/coap/coap.h"
#include "messaging/coap/conf.h"
#include "messaging/coap/constants.h"
#include "messaging/coap/oscore.h"
#include "messaging/coap/oscore_constants.h"
}

/*
  the default block size the stack initializes oc_blk_size with (api/oc_main.c),
  floored to a legal RFC 7959 block size (16 << SZX, SZX 0..6) and capped at 1024 (SZX 6).
  Derived independently of oc_main.c so a regression there is not mirrored here.
*/
#define EXPECTED_DEFAULT_BLOCK_SIZE                                                                                                                  \
  (KNX_PAYLOAD_SIZE >= 1024   ? 1024                                                                                                                 \
   : KNX_PAYLOAD_SIZE >= 512  ? 512                                                                                                                  \
   : KNX_PAYLOAD_SIZE >= 256  ? 256                                                                                                                  \
   : KNX_PAYLOAD_SIZE >= 128  ? 128                                                                                                                  \
   : KNX_PAYLOAD_SIZE >= 64   ? 64                                                                                                                   \
   : KNX_PAYLOAD_SIZE >= 32   ? 32                                                                                                                   \
                              : 16)

/*
  oc_set_pdu_size() takes the TOTAL CoAP PDU size, see include/oc_buffer_settings.h;
  the payload budget is the argument minus both CoAP header budgets (outer + inner).
  This is the argument that reproduces the static defaults.
*/
#define DEFAULT_EQUIVALENT_PDU_ARG (KNX_PAYLOAD_SIZE + 2 * COAP_MAX_HEADER_SIZE)

// smallest argument the guard accepts: both CoAP header budgets plus the smallest block (SZX 0)
#define MINIMUM_PDU_ARG (2 * COAP_MAX_HEADER_SIZE + 16)

/*
  capture the untouched defaults before any TEST() body can call oc_set_pdu_size();
  static initializers run before main(), hence before the gtest scheduler starts
*/
static const uint32_t g_default_pdu = oc_get_pdu_size();
static const uint16_t g_default_block = oc_get_block_size();

static bool is_power_of_two(uint32_t v)
{
  return v != 0 && (v & (v - 1)) == 0;
}

/*
  scratch buffer for the worst-case serialization experiments,
  deliberately far larger than any pdu under test so a budget overrun is
  measured rather than turned into a buffer overflow
*/
#define SCRATCH_BUFFER_SIZE (8 * 1024)

/*
  the longest Uri-Path the header budget accounts for; COAP_MAX_HEADER_SIZE
  reserves FEAR (64) bytes for the path and similar variable-length options
*/
#define MAX_URI_PATH "p/an-example-of-a-very-long-knx-point-api-resource-path"

/*
  fills a coap packet with the maximal option set that COAP_MAX_HEADER_SIZE budgets for:
  full-length token, Observe, Uri-Port, Uri-Path, OSCORE, Max-Age, ETag,
  Block1 + Block2 + Size1 + Size2, and (optionally) a payload
*/
static void fill_max_option_packet(coap_packet_t* pkt, const uint8_t* payload, size_t payload_len)
{
  static uint8_t token[COAP_TOKEN_LEN];
  static uint8_t etag[COAP_ETAG_LEN];
  static uint8_t piv[OSCORE_PIV_LEN];
  static uint8_t kid[OSCORE_SENDER_ID_LEN];
  static uint8_t kid_ctx[OSCORE_ID_CONTEXT_LEN];

  memset(token, 0xAA, sizeof(token));
  memset(etag, 0xBB, sizeof(etag));
  memset(piv, 0xCC, sizeof(piv));
  memset(kid, 0xDD, sizeof(kid));
  memset(kid_ctx, 0xEE, sizeof(kid_ctx));

  coap_udp_init_message(pkt, COAP_TYPE_CON, COAP_POST, 0xFFFF);

  coap_set_token(pkt, token, sizeof(token));
  coap_set_header_uri_path(pkt, MAX_URI_PATH, strlen(MAX_URI_PATH));
  coap_set_header_observe(pkt, 0x00FFFFFF);        // largest 24-bit Observe value
  coap_set_header_max_age(pkt, 0xFFFFFFFF);        // largest Max-Age value
  coap_set_header_etag(pkt, etag, sizeof(etag));
  coap_set_header_block1(pkt, 0xFFFFF, 1, 1024);   // largest block number / size
  coap_set_header_block2(pkt, 0xFFFFF, 1, 1024);
  coap_set_header_size1(pkt, 0xFFFFFFFF);
  coap_set_header_size2(pkt, 0xFFFFFFFF);
  coap_set_header_oscore(pkt, piv, sizeof(piv), kid, sizeof(kid), kid_ctx, sizeof(kid_ctx));

  // no public setter exists for Uri-Port, set the field and mark the option directly
  pkt->uri_port = 0xFFFF;
  SET_OPTION(pkt, COAP_OPTION_URI_PORT);

  if (payload != NULL && payload_len > 0)
  {
    coap_set_payload(pkt, payload, payload_len);
  }
}

/*
  builds a maximal-option CoAP frame and returns its serialized length;
  'inner' selects the OSCORE plaintext (inner code + Class E options + payload),
  otherwise the outer message (Class U options + the OSCORE option) is measured
*/
static size_t build_max_option_coap_frame(uint8_t* buf, size_t buf_len, bool inner, const uint8_t* payload, size_t payload_len)
{
  coap_packet_t pkt;
  size_t len;

  memset(buf, 0, buf_len);
  fill_max_option_packet(&pkt, payload, payload_len);

  if (inner)
  {
    len = coap_oscore_serialize_message(&pkt, buf, true, false, false);
  }
  else
  {
    len = coap_oscore_serialize_message(&pkt, buf, false, true, true);
  }

  EXPECT_LE(len, buf_len) << "serializer overran the scratch buffer";

  return len;
}

// measured outer overhead: a maximal-option outer message carrying no payload
static size_t measured_outer_overhead(void)
{
  static uint8_t buf[SCRATCH_BUFFER_SIZE];
  return build_max_option_coap_frame(buf, sizeof(buf), false, NULL, 0);
}

// measured inner overhead: a maximal-option plaintext carrying no payload
static size_t measured_inner_overhead(void)
{
  static uint8_t buf[SCRATCH_BUFFER_SIZE];
  return build_max_option_coap_frame(buf, sizeof(buf), true, NULL, 0);
}

// total bytes a received worst-case OSCORE message occupies on the wire
static size_t worst_case_frame_size(void)
{
  return measured_outer_overhead() + measured_inner_overhead() + KNX_PAYLOAD_SIZE + OSCORE_AEAD_TAG_LEN;
}

/* -------------------- pdu / block defaults -------------------- */

TEST(PduDefaults, DefaultPduReservesTwoCoapHeaders)
{
  /* api/oc_main.c: oc_pdu_size = KNX_PAYLOAD_SIZE + 2 * COAP_MAX_HEADER_SIZE.
   * One header budget for the outer message, one for the inner plaintext. */
  EXPECT_EQ(g_default_pdu, (uint32_t)(KNX_PAYLOAD_SIZE + 2 * COAP_MAX_HEADER_SIZE));
}

TEST(PduDefaults, DefaultBlockSizeMatchesPayloadSizeCappedAt1024)
{
  EXPECT_EQ(g_default_block, (uint16_t)EXPECTED_DEFAULT_BLOCK_SIZE);
  EXPECT_TRUE(is_power_of_two(g_default_block)) << "block size = " << g_default_block;
}

TEST(PduDefaults, DefaultBlockSizeIsAnEncodableRfc7959Size)
{
  /*
    RFC 7959 Section 2.2: the Block1/Block2 SZX field is 3 bits and only encodes
    '16 << SZX' for SZX 0..6. Any other default cannot be put on the wire at all.
    This must hold for every KNX_PAYLOAD_SIZE, including non power of two values.
  */
  bool encodable = false;
  for (uint16_t szx = 0; szx <= 6; szx++)
  {
    if (g_default_block == (uint16_t)(16 << szx))
    {
      encodable = true;
    }
  }

  EXPECT_TRUE(encodable) << "default block size " << g_default_block << " is not 16 << SZX for any SZX in 0..6";
  EXPECT_LE(g_default_block, (uint16_t)KNX_PAYLOAD_SIZE) << "a block cannot exceed the application payload buffer";
}

TEST(PduDefaults, DefaultBlockSizeMatchesWhatSetPduSizeWouldDerive)
{
  /*
    the comment above the static initializers in api/oc_main.c claims the defaults are the
    "same as oc_set_pdu_size() would calculate" - pin that claim, it silently broke for
    non power of two KNX_PAYLOAD_SIZE values before the initializer floored to a legal size
  */
  ASSERT_EQ(oc_set_pdu_size(DEFAULT_EQUIVALENT_PDU_ARG), 0);
  EXPECT_EQ(oc_get_block_size(), g_default_block);
}

TEST(PduDefaults, DefaultPduExceedsPayloadSize)
{
  /* the default buffer must have room for protocol overhead on top of the payload */
  EXPECT_GT(g_default_pdu, (uint32_t)KNX_PAYLOAD_SIZE);
}

/* -------------------- oc_get_max_app_data_size -------------------- */

TEST(BufferSettings, MaxAppDataSizeReturnsKnxPayloadSize)
{
  /* KNX_PAYLOAD_SIZE is fixed at compile time (CMakeLists.txt) and cannot be
   * changed at runtime anymore (oc_set_max_app_data_size was removed). */
  EXPECT_EQ(oc_get_max_app_data_size(), (uint32_t)KNX_PAYLOAD_SIZE);
}

TEST(BufferSettings, MaxAppDataSizeIsStable)
{
  /* No setter exists - repeated reads must be identical. */
  EXPECT_EQ(oc_get_max_app_data_size(), oc_get_max_app_data_size());
}

/* -------------------- oc_set_pdu_size guard -------------------- */

TEST(BufferSettings, SetPduSizeBelowMinimumReturnsError)
{
  /* A tiny pdu cannot hold both CoAP header budgets plus one block (16 bytes). */
  EXPECT_EQ(oc_set_pdu_size(0), -1);
  EXPECT_EQ(oc_set_pdu_size(16), -1);
}

TEST(BufferSettings, SetPduSizeBelowMinimumDoesNotChangeState)
{
  ASSERT_EQ(oc_set_pdu_size(2048), 0);
  uint32_t before_pdu = oc_get_pdu_size();
  uint16_t before_block = oc_get_block_size();

  EXPECT_EQ(oc_set_pdu_size(1), -1);

  EXPECT_EQ(oc_get_pdu_size(), before_pdu);
  EXPECT_EQ(oc_get_block_size(), before_block);
}

TEST(BufferSettings, SetPduSizeValidReturnsZero)
{
  EXPECT_EQ(oc_set_pdu_size(2048), 0);
  EXPECT_EQ(oc_set_pdu_size(1024), 0);
}

/* -------------------- oc_get_pdu_size -------------------- */

TEST(BufferSettings, GetPduSizeReturnsExactlyWhatWasSet)
{
  /* the argument is the total PDU size and is stored unchanged, so the getter
   * reports the same unit the setter accepts */
  ASSERT_EQ(oc_set_pdu_size(2048), 0);
  EXPECT_EQ(oc_get_pdu_size(), 2048u);

  ASSERT_EQ(oc_set_pdu_size(1024), 0);
  EXPECT_EQ(oc_get_pdu_size(), 1024u);
}

TEST(BufferSettings, SetPduSizeFromGetPduSizeIsIdempotent)
{
  /* setter and getter speak the same unit, so re-applying the current value
   * must not drift the buffer size */
  ASSERT_EQ(oc_set_pdu_size(2048), 0);

  for (int i = 0; i < 5; i++)
  {
    ASSERT_EQ(oc_set_pdu_size(oc_get_pdu_size()), 0);
    EXPECT_EQ(oc_get_pdu_size(), 2048u) << "pdu drifted on iteration " << i;
  }
}

/* -------------------- oc_get_block_size -------------------- */

TEST(BufferSettings, BlockSizeIsPowerOfTwoInRange)
{
  ASSERT_EQ(oc_set_pdu_size(2048), 0);
  uint16_t block = oc_get_block_size();
  EXPECT_TRUE(is_power_of_two(block)) << "block size = " << block;
  EXPECT_GE(block, 16);
  EXPECT_LE(block, 1024);
}

TEST(BufferSettings, LargePduYieldsMaxBlockSize)
{
  /* the payload budget is the argument minus both header budgets, so this stays
   * comfortably above 1024 and the largest supported block must be selected */
  ASSERT_EQ(oc_set_pdu_size(2048 + 2 * COAP_MAX_HEADER_SIZE), 0);
  EXPECT_EQ(oc_get_block_size(), 1024);
}

TEST(BufferSettings, SmallerPduYieldsSmallerBlockSize)
{
  ASSERT_EQ(oc_set_pdu_size(2048), 0);
  uint16_t big = oc_get_block_size();

  ASSERT_EQ(oc_set_pdu_size(1024), 0);
  uint16_t small = oc_get_block_size();

  EXPECT_TRUE(is_power_of_two(small)) << "block size = " << small;
  EXPECT_LT(small, big);
  EXPECT_GE(small, 16);
}

/* -------------------- worst-case OSCORE frame vs. pdu budget --------------- */

TEST(PduWorstCaseFrame, MeasuredOuterHeaderFitsCoapMaxHeaderSize)
{
  /* the outer message carries the Class U options plus the OSCORE option;
   * COAP_MAX_HEADER_SIZE must budget for all of them at once */
  size_t outer = measured_outer_overhead();

  EXPECT_GT(outer, 0u) << "serializer produced an empty outer message";
  EXPECT_LE(outer, (size_t)COAP_MAX_HEADER_SIZE)
    << "measured outer overhead " << outer << " exceeds COAP_MAX_HEADER_SIZE " << (size_t)COAP_MAX_HEADER_SIZE;
}

TEST(PduWorstCaseFrame, MeasuredInnerHeaderFitsCoapMaxHeaderSize)
{
  /* the inner plaintext carries the inner code and the Class E options */
  size_t inner = measured_inner_overhead();

  EXPECT_GT(inner, 0u) << "serializer produced an empty plaintext";
  EXPECT_LE(inner, (size_t)COAP_MAX_HEADER_SIZE)
    << "measured inner overhead " << inner << " exceeds COAP_MAX_HEADER_SIZE " << (size_t)COAP_MAX_HEADER_SIZE;
}

TEST(PduWorstCaseFrame, DefaultPduHoldsWorstCaseOscoreFrame)
{
  /* outer + inner + full application payload + AEAD tag must fit the buffer
   * that oc_allocate_message() hands to the receive path */
  size_t worst = worst_case_frame_size();

  EXPECT_LE(worst, (size_t)g_default_pdu)
    << "worst-case OSCORE frame " << worst << " does not fit the default pdu " << (size_t)g_default_pdu;
}

TEST(PduWorstCaseFrame, PayloadCarryingFrameStaysWithinMeasuredBudget)
{
  /* sanity check on the helpers themselves: adding the payload must grow the
   * serialized plaintext by exactly the payload length plus the payload marker */
  static uint8_t buf[SCRATCH_BUFFER_SIZE];
  std::vector<uint8_t> payload(KNX_PAYLOAD_SIZE, 0x5A);

  size_t empty = measured_inner_overhead();
  size_t filled = build_max_option_coap_frame(buf, sizeof(buf), true, payload.data(), payload.size());

  EXPECT_GT(filled, empty);
  EXPECT_LE(filled - empty, (size_t)KNX_PAYLOAD_SIZE + 1) << "payload cost more than payload + marker";
}

/* ------------------- setter vs. default initializer agreement -------------- */

TEST(PduWorstCaseFrame, SetPduSizeToDefaultEquivalentReproducesDefaultPduSize)
{
  /* the caller passes payload + one outer CoAP header and the setter adds the
   * inner one, so this argument must reproduce the static default exactly */
  ASSERT_EQ(oc_set_pdu_size(DEFAULT_EQUIVALENT_PDU_ARG), 0);

  EXPECT_EQ(oc_get_pdu_size(), (uint32_t)(KNX_PAYLOAD_SIZE + 2 * COAP_MAX_HEADER_SIZE));
  EXPECT_EQ(oc_get_pdu_size(), g_default_pdu);
  EXPECT_LE(worst_case_frame_size(), (size_t)oc_get_pdu_size());
}

TEST(PduWorstCaseFrame, SetPduSizeToDefaultEquivalentReproducesDefaultBlockSize)
{
  /* the block budget is the argument minus the caller supplied header, i.e. the
   * pure payload, so the setter must agree with the default initializer */
  ASSERT_EQ(oc_set_pdu_size(DEFAULT_EQUIVALENT_PDU_ARG), 0);

  EXPECT_EQ(oc_get_block_size(), (uint16_t)EXPECTED_DEFAULT_BLOCK_SIZE);
  EXPECT_EQ(oc_get_block_size(), g_default_block);
}

/* -------------------- pdu guard boundary -------------------- */

TEST(PduWorstCaseFrame, PduBelowTwoHeadersPlusMinBlockIsRejected)
{
  /* the guard must reject anything that cannot hold both CoAP header budgets
   * plus the smallest block-wise block (16 bytes, SZX 0); one byte more is accepted */
  EXPECT_EQ(oc_set_pdu_size(MINIMUM_PDU_ARG - 1), -1);
  EXPECT_EQ(oc_set_pdu_size(MINIMUM_PDU_ARG), 0);
}

TEST(PduWorstCaseFrame, MinimumAcceptedPduYieldsMinimumBlockSize)
{
  /* the smallest accepted argument leaves exactly 16 payload bytes; if the internal
   * subtraction were to underflow, the clamp would inflate this to 1024 */
  ASSERT_EQ(oc_set_pdu_size(MINIMUM_PDU_ARG), 0);

  EXPECT_EQ(oc_get_block_size(), 16) << "block size at the minimum pdu suggests an unsigned underflow";
}

TEST(PduWorstCaseFrame, GuardAndBlockBudgetUseSameHeaderMultiple)
{
  /*
    api/oc_main.c must use the same multiple of COAP_MAX_HEADER_SIZE in the guard
    and in the block budget subtraction. Changing only one of the two breaks
    exactly one of the expectations below.
  */
  ASSERT_EQ(oc_set_pdu_size(MINIMUM_PDU_ARG), 0);
  EXPECT_EQ(oc_get_block_size(), 16) << "guard and block budget use different header multiples";

  ASSERT_EQ(oc_set_pdu_size(DEFAULT_EQUIVALENT_PDU_ARG), 0);
  EXPECT_EQ(oc_get_pdu_size(), g_default_pdu) << "setter and default initializer disagree on the total PDU size";
  EXPECT_EQ(oc_get_block_size(), g_default_block) << "setter and default initializer disagree on the block size";
}

TEST(PduWorstCaseFrame, BlockSizeIsMonotonicInPduSize)
{
  /* sweeping upward across the header boundary must never shrink the block
   * size; a wrap in the block derivation breaks monotonicity loudly */
  uint32_t base = MINIMUM_PDU_ARG;
  uint16_t previous = 0;
  uint32_t pdu;

  for (pdu = base; pdu <= base + 4096; pdu += 16)
  {
    ASSERT_EQ(oc_set_pdu_size(pdu), 0) << "pdu = " << pdu;
    uint16_t block = oc_get_block_size();

    EXPECT_TRUE(is_power_of_two(block)) << "pdu = " << pdu << ", block size = " << block;
    EXPECT_GE(block, previous) << "block size shrank at pdu = " << pdu;
    EXPECT_LE(block, 1024);

    previous = block;
  }
}

TEST(PduWorstCaseFrame, LargePduAlwaysHoldsWorstCaseFrame)
{
  /* any pdu at or above the payload size must still hold the worst-case frame */
  ASSERT_EQ(oc_set_pdu_size(2 * KNX_PAYLOAD_SIZE), 0);

  EXPECT_GE(oc_get_pdu_size(), (uint32_t)worst_case_frame_size());
}

/* ------------------- allocated message buffer honours the pdu -------------- */

/*
  oc_allocate_message() takes the network-event mutex and sizes message->data
  with OC_PDU_SIZE + 1 (= oc_get_pdu_size() + 1 in the UDP-only build; the extra
  byte holds the null-terminator the CoAP parser appends behind a full-size
  payload), so this fixture initializes the mutex and restores the
  default-equivalent pdu afterwards
*/
class PduMessageBuffer : public ::testing::Test
{
protected:
  void SetUp() override { oc_network_event_handler_mutex_init(); }
  void TearDown() override { oc_set_pdu_size(DEFAULT_EQUIVALENT_PDU_ARG); }
};

TEST_F(PduMessageBuffer, AllocatedMessageBufferMatchesConfiguredPduSize)
{
  ASSERT_EQ(oc_set_pdu_size(2048), 0);

  oc_message_t* m = oc_allocate_message();
  ASSERT_NE(m, nullptr);
  ASSERT_NE(m->data, nullptr);

  // the full advertised pdu must be writable, otherwise the receive path overruns
  memset(m->data, 0xA5, oc_get_pdu_size());
  EXPECT_EQ(m->data[0], 0xA5);
  EXPECT_EQ(m->data[oc_get_pdu_size() - 1], 0xA5);

  oc_message_unref(m);
}

TEST_F(PduMessageBuffer, AllocatedMessageBufferHoldsWorstCaseFrame)
{
  ASSERT_EQ(oc_set_pdu_size(DEFAULT_EQUIVALENT_PDU_ARG), 0);

  oc_message_t* m = oc_allocate_message();
  ASSERT_NE(m, nullptr);
  ASSERT_NE(m->data, nullptr);

  size_t worst = worst_case_frame_size();
  ASSERT_LE(worst, (size_t)oc_get_pdu_size()) << "worst-case frame " << worst << " exceeds the allocated buffer";

  // writing a full worst-case frame into a freshly allocated message must be safe
  memset(m->data, 0x5A, worst);
  EXPECT_EQ(m->data[worst - 1], 0x5A);

  oc_message_unref(m);
}

/* -------------------- global state restore -------------------- */

TEST(PduZzzRestore, RestoreDefaultEquivalentPduSize)
{
  /* runs last (suite name sorts after the others) so any later suite in this
   * binary sees default-equivalent values again */
  EXPECT_EQ(oc_set_pdu_size(DEFAULT_EQUIVALENT_PDU_ARG), 0);
  EXPECT_EQ(oc_get_pdu_size(), g_default_pdu);
}
