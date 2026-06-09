/*
 * Unit tests for messaging/coap/oscore.c — OSCORE option and PIV/SSN helpers
 *
 * Covers:
 *   oscore_store_piv_to_ssn       — big-endian PIV → uint64_t SSN
 *   oscore_store_ssn_to_piv       — uint64_t SSN → big-endian PIV
 *   coap_set_header_oscore        — populate OSCORE fields in coap_packet_t
 *   coap_parse_inner_oscore_option — parse raw OSCORE option bytes
 *   coap_serialize_oscore_option  — serialize OSCORE option to buffer
 *
 * All operate on plain structs or scalar values — no stack init required.
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "messaging/coap/oscore.h"
#include "messaging/coap/coap.h"
#include "messaging/coap/oscore_constants.h"

}

/* ═══════════════════════════════════════════════════════════════════════════
 * oscore_store_piv_to_ssn
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(PivToSsn, SingleByte)
{
  uint8_t piv[] = {0x42};
  uint64_t ssn = 0xDEAD;
  EXPECT_EQ(oscore_store_piv_to_ssn(piv, 1, &ssn), 0);
  EXPECT_EQ(ssn, 0x42u);
}

TEST(PivToSsn, ThreeBytes)
{
  uint8_t piv[] = {0xAA, 0xBB, 0xCC};
  uint64_t ssn = 0;
  oscore_store_piv_to_ssn(piv, 3, &ssn);
  EXPECT_EQ(ssn, 0x00AABBCCu);
}

TEST(PivToSsn, FiveBytes)
{
  uint8_t piv[] = {0x01, 0x02, 0x03, 0x04, 0x05};
  uint64_t ssn = 0;
  oscore_store_piv_to_ssn(piv, 5, &ssn);
  EXPECT_EQ(ssn, 0x0102030405ULL);
}

TEST(PivToSsn, ZeroLength)
{
  uint8_t piv[] = {0xFF};
  uint64_t ssn = 0xDEAD;
  oscore_store_piv_to_ssn(piv, 0, &ssn);
  EXPECT_EQ(ssn, 0u);
}

TEST(PivToSsn, SingleZero)
{
  uint8_t piv[] = {0x00};
  uint64_t ssn = 0xDEAD;
  oscore_store_piv_to_ssn(piv, 1, &ssn);
  EXPECT_EQ(ssn, 0u);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oscore_store_ssn_to_piv
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(SsnToPiv, Zero)
{
  uint8_t piv[OSCORE_PIV_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  uint8_t piv_len = 0;
  EXPECT_EQ(oscore_store_ssn_to_piv(piv, &piv_len, 0), 0);
  EXPECT_EQ(piv_len, 1);
  EXPECT_EQ(piv[0], 0x00);
}

TEST(SsnToPiv, SmallValue)
{
  uint8_t piv[OSCORE_PIV_LEN] = {};
  uint8_t piv_len = 0;
  oscore_store_ssn_to_piv(piv, &piv_len, 0x42);
  EXPECT_EQ(piv_len, 1);
  EXPECT_EQ(piv[0], 0x42);
}

TEST(SsnToPiv, ThreeBytes)
{
  uint8_t piv[OSCORE_PIV_LEN] = {};
  uint8_t piv_len = 0;
  oscore_store_ssn_to_piv(piv, &piv_len, 0x00AABBCC);
  EXPECT_EQ(piv_len, 3);
  EXPECT_EQ(piv[0], 0xAA);
  EXPECT_EQ(piv[1], 0xBB);
  EXPECT_EQ(piv[2], 0xCC);
}

TEST(SsnToPiv, MaxFourBytes)
{
  uint8_t piv[OSCORE_PIV_LEN] = {};
  uint8_t piv_len = 0;
  oscore_store_ssn_to_piv(piv, &piv_len, 0xFFFFFFFF);
  EXPECT_EQ(piv_len, 4);
  EXPECT_EQ(piv[0], 0xFF);
  EXPECT_EQ(piv[1], 0xFF);
  EXPECT_EQ(piv[2], 0xFF);
  EXPECT_EQ(piv[3], 0xFF);
}

TEST(SsnToPiv, WrapAround)
{
  /* SSN > 2^32-1 wraps via OSCORE_SSN_MAX mask */
  uint8_t piv[OSCORE_PIV_LEN] = {};
  uint8_t piv_len = 0;
  /* 0x100000001 & 0xFFFFFFFF = 1 */
  oscore_store_ssn_to_piv(piv, &piv_len, 0x100000001ULL);
  EXPECT_EQ(piv_len, 1);
  EXPECT_EQ(piv[0], 0x01);
}

TEST(SsnToPiv, ExactPowerOf32)
{
  /* 2^32 & 0xFFFFFFFF = 0 → special zero case */
  uint8_t piv[OSCORE_PIV_LEN] = {};
  uint8_t piv_len = 0;
  oscore_store_ssn_to_piv(piv, &piv_len, 1ULL << 32);
  EXPECT_EQ(piv_len, 1);
  EXPECT_EQ(piv[0], 0x00);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PIV ↔ SSN round-trip
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(PivSsnRoundTrip, SmallValue)
{
  uint8_t piv[OSCORE_PIV_LEN] = {};
  uint8_t piv_len = 0;
  uint64_t ssn_out = 0;

  oscore_store_ssn_to_piv(piv, &piv_len, 12345);
  oscore_store_piv_to_ssn(piv, piv_len, &ssn_out);
  EXPECT_EQ(ssn_out, 12345u);
}

TEST(PivSsnRoundTrip, LargeValue)
{
  uint8_t piv[OSCORE_PIV_LEN] = {};
  uint8_t piv_len = 0;
  uint64_t ssn_out = 0;

  oscore_store_ssn_to_piv(piv, &piv_len, 0xDEADBEEF);
  oscore_store_piv_to_ssn(piv, piv_len, &ssn_out);
  EXPECT_EQ(ssn_out, 0xDEADBEEFu);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * coap_set_header_oscore + coap_parse_inner_oscore_option round-trip
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(OscoreOption, SetAndParsePivOnly)
{
  coap_packet_t pkt;
  memset(&pkt, 0, sizeof(pkt));

  uint8_t piv[] = {0x01, 0x02};
  coap_set_header_oscore(&pkt, piv, 2, NULL, 0, NULL, 0);

  /* Verify fields set */
  EXPECT_EQ(pkt.piv_len, 2);
  EXPECT_EQ(memcmp(pkt.piv, piv, 2), 0);
  EXPECT_EQ(pkt.kid_len, 0);
  EXPECT_EQ(pkt.kid_ctx_len, 0);
  /* flags: nnn=2 (piv_len), k=0, h=0 */
  EXPECT_EQ(pkt.oscore_flags, 0x02);
}

TEST(OscoreOption, SetAndParsePivAndKid)
{
  coap_packet_t pkt;
  memset(&pkt, 0, sizeof(pkt));

  uint8_t piv[] = {0x03};
  uint8_t kid[] = {0xAA, 0xBB};
  coap_set_header_oscore(&pkt, piv, 1, kid, 2, NULL, 0);

  EXPECT_EQ(pkt.piv_len, 1);
  EXPECT_EQ(pkt.kid_len, 2);
  EXPECT_EQ(pkt.kid_ctx_len, 0);
  /* flags: nnn=1, k=1 → 0x01 | 0x08 = 0x09 */
  EXPECT_EQ(pkt.oscore_flags, 0x09);
}

TEST(OscoreOption, SetAndParseFull)
{
  coap_packet_t pkt;
  memset(&pkt, 0, sizeof(pkt));

  uint8_t piv[] = {0x01, 0x02, 0x03};
  uint8_t kid[] = {0xDD, 0xEE};
  uint8_t kid_ctx[] = {0x11, 0x22, 0x33, 0x44};
  coap_set_header_oscore(&pkt, piv, 3, kid, 2, kid_ctx, 4);

  EXPECT_EQ(pkt.piv_len, 3);
  EXPECT_EQ(pkt.kid_len, 2);
  EXPECT_EQ(pkt.kid_ctx_len, 4);
  /* flags: nnn=3, k=1, h=1 → 0x03 | 0x08 | 0x10 = 0x1B */
  EXPECT_EQ(pkt.oscore_flags, 0x1B);
}

TEST(OscoreOption, ParseInnerEmpty)
{
  coap_packet_t pkt;
  memset(&pkt, 0, sizeof(pkt));

  /* Empty option → no fields parsed */
  uint8_t option[] = {};
  EXPECT_EQ(coap_parse_inner_oscore_option(&pkt, option, 0), 0);
  EXPECT_EQ(pkt.piv_len, 0);
  EXPECT_EQ(pkt.kid_len, 0);
  EXPECT_EQ(pkt.kid_ctx_len, 0);
}

TEST(OscoreOption, ParseInnerPivOnly)
{
  coap_packet_t pkt;
  memset(&pkt, 0, sizeof(pkt));

  /* flags=0x02 (nnn=2, k=0, h=0), then 2 PIV bytes */
  uint8_t option[] = {0x02, 0xAA, 0xBB};
  EXPECT_EQ(coap_parse_inner_oscore_option(&pkt, option, 3), 0);
  EXPECT_EQ(pkt.piv_len, 2);
  EXPECT_EQ(pkt.piv[0], 0xAA);
  EXPECT_EQ(pkt.piv[1], 0xBB);
  EXPECT_EQ(pkt.kid_len, 0);
  EXPECT_EQ(pkt.kid_ctx_len, 0);
}

TEST(OscoreOption, ParseInnerFull)
{
  coap_packet_t pkt;
  memset(&pkt, 0, sizeof(pkt));

  /* flags=0x1B: nnn=3, k=1, h=1
   * PIV: 0x01 0x02 0x03
   * kid_ctx_len: 0x02, kid_ctx: 0xCC 0xDD
   * kid (remaining): 0xEE 0xFF
   */
  uint8_t option[] = {0x1B, 0x01, 0x02, 0x03, 0x02, 0xCC, 0xDD, 0xEE, 0xFF};
  EXPECT_EQ(coap_parse_inner_oscore_option(&pkt, option, sizeof(option)), 0);
  EXPECT_EQ(pkt.piv_len, 3);
  EXPECT_EQ(pkt.piv[0], 0x01);
  EXPECT_EQ(pkt.piv[1], 0x02);
  EXPECT_EQ(pkt.piv[2], 0x03);
  EXPECT_EQ(pkt.kid_ctx_len, 2);
  EXPECT_EQ(pkt.kid_ctx[0], 0xCC);
  EXPECT_EQ(pkt.kid_ctx[1], 0xDD);
  EXPECT_EQ(pkt.kid_len, 2);
  EXPECT_EQ(pkt.kid[0], 0xEE);
  EXPECT_EQ(pkt.kid[1], 0xFF);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * coap_serialize_oscore_option
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(OscoreSerialize, PivOnly)
{
  coap_packet_t pkt;
  memset(&pkt, 0, sizeof(pkt));

  uint8_t piv[] = {0x42};
  coap_set_header_oscore(&pkt, piv, 1, NULL, 0, NULL, 0);

  /* Calculate expected length first (buffer=NULL) */
  unsigned int cur_num = 0;
  size_t len = coap_serialize_oscore_option(&cur_num, &pkt, NULL);
  EXPECT_GT(len, 0u);

  /* Then serialize into buffer */
  uint8_t buf[64] = {};
  cur_num = 0;
  size_t written = coap_serialize_oscore_option(&cur_num, &pkt, buf);
  EXPECT_EQ(written, len);
}

TEST(OscoreSerialize, FullRoundTrip)
{
  /* Set up a packet with PIV + KID + KID_CTX */
  coap_packet_t pkt_out;
  memset(&pkt_out, 0, sizeof(pkt_out));

  uint8_t piv[] = {0x01, 0x02};
  uint8_t kid[] = {0xAA};
  uint8_t kid_ctx[] = {0xBB, 0xCC};
  coap_set_header_oscore(&pkt_out, piv, 2, kid, 1, kid_ctx, 2);

  /* Serialize to buffer */
  uint8_t buf[64] = {};
  unsigned int cur_num = 0;
  size_t written = coap_serialize_oscore_option(&cur_num, &pkt_out, buf);
  EXPECT_GT(written, 0u);

  /* Skip the option header to find the option value.
   * The option header encodes delta + length.
   * For OSCORE (option 9), delta=9, length=flags(1)+piv(2)+kid_ctx_len(1)+kid_ctx(2)+kid(1)=7
   * That's a single-byte header: (9<<4)|7 = 0x97
   */
  ASSERT_GE(written, 2u);

  /* Parse the option value from the serialized buffer */
  coap_packet_t pkt_in;
  memset(&pkt_in, 0, sizeof(pkt_in));

  /* Option value starts after the 1-byte header */
  uint8_t *option_value = buf + 1;
  size_t option_value_len = written - 1;

  EXPECT_EQ(coap_parse_inner_oscore_option(&pkt_in, option_value, option_value_len), 0);
  EXPECT_EQ(pkt_in.piv_len, 2);
  EXPECT_EQ(memcmp(pkt_in.piv, piv, 2), 0);
  EXPECT_EQ(pkt_in.kid_len, 1);
  EXPECT_EQ(pkt_in.kid[0], 0xAA);
  EXPECT_EQ(pkt_in.kid_ctx_len, 2);
  EXPECT_EQ(memcmp(pkt_in.kid_ctx, kid_ctx, 2), 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oscore_is_oscore_message — scans the raw CoAP option stream for option 9
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(OscoreIsOscoreMessage, DetectsOscoreOption)
{
  /* UDP header (4 bytes), no token, single OSCORE option (number 9). */
  uint8_t data[5];
  data[0] = 0x40; /* version 1, type CON, token length 0 */
  data[1] = 0x02; /* code POST */
  data[2] = 0x12; /* mid hi */
  data[3] = 0x34; /* mid lo */
  data[4] = 0x90; /* option delta 9 (OSCORE), length 0 */

  oc_message_t msg{};
  msg.data = data;
  msg.length = sizeof(data);

  EXPECT_TRUE(oscore_is_oscore_message(&msg));
}

TEST(OscoreIsOscoreMessage, ReturnsFalseForNonOscoreOption)
{
  /* Same header but a URI-PATH option (number 11) instead of OSCORE. */
  uint8_t data[5];
  data[0] = 0x40;
  data[1] = 0x02;
  data[2] = 0x12;
  data[3] = 0x34;
  data[4] = 0xB0; /* option delta 11 (URI_PATH), length 0 */

  oc_message_t msg{};
  msg.data = data;
  msg.length = sizeof(data);

  EXPECT_FALSE(oscore_is_oscore_message(&msg));
}

TEST(OscoreIsOscoreMessage, ReturnsFalseWhenNoOptions)
{
  /* Header only, no options at all. */
  uint8_t data[4];
  data[0] = 0x40;
  data[1] = 0x02;
  data[2] = 0x12;
  data[3] = 0x34;

  oc_message_t msg{};
  msg.data = data;
  msg.length = sizeof(data);

  EXPECT_FALSE(oscore_is_oscore_message(&msg));
}
