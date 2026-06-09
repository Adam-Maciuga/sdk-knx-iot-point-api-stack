/*
 * Unit tests for messaging/coap/coap.c
 *
 * Covers: coap_udp_init_message, serialize/parse round-trips,
 *         header get/set (token, content-format, accept, max-age,
 *         etag, observe, uri-path, block1/2, size1/2, echo, payload),
 *         coap_set_option_header, coap_set_status_code.
 *
 * All tests use UDP transport.  No network or OSCORE dependencies.
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "coap.h"
#include "oc_ri.h"
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test fixture — allocates a serialization buffer and a coap_packet_t
 * ═══════════════════════════════════════════════════════════════════════════ */

class CoapTest : public ::testing::Test {
protected:
  coap_packet_t pkt;
  uint8_t wire[512]; /* serialization buffer */

  void SetUp() override
  {
    memset(&pkt, 0, sizeof(pkt));
    memset(wire, 0, sizeof(wire));
  }

  /* Init a CON GET with a given MID */
  void init_con_get(uint16_t mid = 0x1234)
  {
    coap_udp_init_message(&pkt, COAP_TYPE_CON, COAP_GET, mid);
  }
};

/* ═══════════════════════════════════════════════════════════════════════════
 * coap_udp_init_message
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(CoapTest, InitMessage_CON_GET)
{
  coap_udp_init_message(&pkt, COAP_TYPE_CON, COAP_GET, 0xABCD);

  EXPECT_EQ(pkt.transport_type, COAP_TRANSPORT_UDP);
  EXPECT_EQ(pkt.type, COAP_TYPE_CON);
  EXPECT_EQ(pkt.code, COAP_GET);
  EXPECT_EQ(pkt.mid, 0xABCD);
  EXPECT_EQ(pkt.version, 1);
}

TEST_F(CoapTest, InitMessage_NON_PUT)
{
  coap_udp_init_message(&pkt, COAP_TYPE_NON, COAP_PUT, 0x0001);

  EXPECT_EQ(pkt.type, COAP_TYPE_NON);
  EXPECT_EQ(pkt.code, COAP_PUT);
  EXPECT_EQ(pkt.mid, 0x0001);
}

TEST_F(CoapTest, InitMessage_ACK_Content)
{
  coap_udp_init_message(&pkt, COAP_TYPE_ACK, CONTENT_2_05, 0xFFFF);

  EXPECT_EQ(pkt.type, COAP_TYPE_ACK);
  EXPECT_EQ(pkt.code, CONTENT_2_05);
  EXPECT_EQ(pkt.mid, 0xFFFF);
}

TEST_F(CoapTest, InitMessage_ClearsFields)
{
  /* Set some fields, then reinit — everything should be zeroed */
  pkt.payload_len = 999;
  pkt.token_len = 8;
  coap_udp_init_message(&pkt, COAP_TYPE_RST, 0, 0);

  EXPECT_EQ(pkt.payload_len, 0u);
  EXPECT_EQ(pkt.token_len, 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Token
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(CoapTest, Token_Set4Bytes)
{
  init_con_get();
  uint8_t tok[] = {0xDE, 0xAD, 0xBE, 0xEF};
  int len = coap_set_token(&pkt, tok, sizeof(tok));
  EXPECT_EQ(len, 4);
  EXPECT_EQ(pkt.token_len, 4);
  EXPECT_EQ(memcmp(pkt.token, tok, 4), 0);
}

TEST_F(CoapTest, Token_Set8Bytes)
{
  init_con_get();
  uint8_t tok[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  int len = coap_set_token(&pkt, tok, 8);
  EXPECT_EQ(len, 8);
  EXPECT_EQ(pkt.token_len, 8);
}

TEST_F(CoapTest, Token_TruncatedAt8)
{
  init_con_get();
  uint8_t tok[16] = {};
  int len = coap_set_token(&pkt, tok, 16);
  EXPECT_EQ(len, COAP_TOKEN_LEN); /* max 8 */
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Content-Format
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(CoapTest, ContentFormat_SetGet)
{
  init_con_get();
  coap_set_header_content_format(&pkt, APPLICATION_CBOR);

  oc_content_format_t fmt = CONTENT_NONE;
  int rc = coap_get_header_content_format(&pkt, &fmt);
  EXPECT_EQ(rc, 1);
  EXPECT_EQ(fmt, APPLICATION_CBOR);
}

TEST_F(CoapTest, ContentFormat_NotSet)
{
  init_con_get();
  oc_content_format_t fmt = CONTENT_NONE;
  int rc = coap_get_header_content_format(&pkt, &fmt);
  EXPECT_EQ(rc, 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Accept
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(CoapTest, Accept_SetGet)
{
  init_con_get();
  coap_set_header_accept(&pkt, APPLICATION_JSON);

  unsigned int accept = 0;
  int rc = coap_get_header_accept(&pkt, &accept);
  EXPECT_EQ(rc, 1);
  EXPECT_EQ(accept, (unsigned)APPLICATION_JSON);
}

TEST_F(CoapTest, Accept_NotSet)
{
  init_con_get();
  unsigned int accept = 0;
  EXPECT_EQ(coap_get_header_accept(&pkt, &accept), 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Max-Age
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(CoapTest, MaxAge_SetGet)
{
  init_con_get();
  coap_set_header_max_age(&pkt, 120);

  uint32_t age = 0;
  coap_get_header_max_age(&pkt, &age);
  EXPECT_EQ(age, 120u);
}

TEST_F(CoapTest, MaxAge_DefaultWhenNotSet)
{
  init_con_get();
  uint32_t age = 0;
  coap_get_header_max_age(&pkt, &age);
  EXPECT_EQ(age, COAP_DEFAULT_MAX_AGE); /* 60 */
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ETag
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(CoapTest, ETag_SetGet)
{
  init_con_get();
  uint8_t etag[] = {0x01, 0x02, 0x03, 0x04};
  int len = coap_set_header_etag(&pkt, etag, sizeof(etag));
  EXPECT_EQ(len, 4);

  const uint8_t *out = nullptr;
  int rlen = coap_get_header_etag(&pkt, &out);
  EXPECT_EQ(rlen, 4);
  EXPECT_EQ(memcmp(out, etag, 4), 0);
}

TEST_F(CoapTest, ETag_NotSet)
{
  init_con_get();
  const uint8_t *out = nullptr;
  EXPECT_EQ(coap_get_header_etag(&pkt, &out), 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Observe
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(CoapTest, Observe_SetGet)
{
  init_con_get();
  coap_set_header_observe(&pkt, 42);

  oc_client_observe_t obs = OC_OBSERVE_NOT_APPLICABLE;
  EXPECT_TRUE(coap_get_header_observe(&pkt, &obs));
  EXPECT_EQ(obs, (oc_client_observe_t)42);
}

TEST_F(CoapTest, Observe_NotSet)
{
  init_con_get();
  oc_client_observe_t obs = OC_OBSERVE_NOT_APPLICABLE;
  EXPECT_FALSE(coap_get_header_observe(&pkt, &obs));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Uri-Path
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(CoapTest, UriPath_SetGet)
{
  init_con_get();
  const char *path = "a/b/c";
  coap_set_header_uri_path(&pkt, path, strlen(path));

  const char *out = nullptr;
  size_t len = coap_get_header_uri_path(&pkt, &out);
  EXPECT_EQ(len, 5u);
  EXPECT_EQ(strncmp(out, "a/b/c", 5), 0);
}

TEST_F(CoapTest, UriPath_StripsLeadingSlash)
{
  init_con_get();
  const char *path = "/resource";
  coap_set_header_uri_path(&pkt, path, strlen(path));

  const char *out = nullptr;
  size_t len = coap_get_header_uri_path(&pkt, &out);
  EXPECT_EQ(len, 8u); /* "resource" without leading '/' */
  EXPECT_EQ(strncmp(out, "resource", 8), 0);
}

TEST_F(CoapTest, UriPath_NotSet)
{
  init_con_get();
  const char *out = nullptr;
  EXPECT_EQ(coap_get_header_uri_path(&pkt, &out), 0u);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Block2
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(CoapTest, Block2_SetGet)
{
  init_con_get();
  EXPECT_EQ(coap_set_header_block2(&pkt, 0, 1, 256), 1);

  uint32_t num = 0;
  uint8_t more = 0;
  uint16_t size = 0;
  uint32_t offset = 0;
  EXPECT_EQ(coap_get_header_block2(&pkt, &num, &more, &size, &offset), 1);
  EXPECT_EQ(num, 0u);
  EXPECT_EQ(more, 1);
  EXPECT_EQ(size, 256);
}

TEST_F(CoapTest, Block2_InvalidSizeTooSmall)
{
  init_con_get();
  EXPECT_EQ(coap_set_header_block2(&pkt, 0, 0, 8), 0); /* < 16 */
}

TEST_F(CoapTest, Block2_InvalidSizeTooLarge)
{
  init_con_get();
  EXPECT_EQ(coap_set_header_block2(&pkt, 0, 0, 4096), 0); /* > 2048 */
}

TEST_F(CoapTest, Block2_InvalidNumTooLarge)
{
  init_con_get();
  EXPECT_EQ(coap_set_header_block2(&pkt, 0x100000, 0, 256), 0); /* > 0x0FFFFF */
}

TEST_F(CoapTest, Block2_NotSet)
{
  init_con_get();
  EXPECT_EQ(coap_get_header_block2(&pkt, nullptr, nullptr, nullptr, nullptr), 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Block1
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(CoapTest, Block1_SetGet)
{
  init_con_get();
  EXPECT_EQ(coap_set_header_block1(&pkt, 3, 0, 64), 1);

  uint32_t num = 0;
  uint8_t more = 0;
  uint16_t size = 0;
  uint32_t offset = 0;
  EXPECT_EQ(coap_get_header_block1(&pkt, &num, &more, &size, &offset), 1);
  EXPECT_EQ(num, 3u);
  EXPECT_EQ(more, 0);
  EXPECT_EQ(size, 64);
}

TEST_F(CoapTest, Block1_NotSet)
{
  init_con_get();
  EXPECT_EQ(coap_get_header_block1(&pkt, nullptr, nullptr, nullptr, nullptr), 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Size1 / Size2
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(CoapTest, Size1_SetGet)
{
  init_con_get();
  coap_set_header_size1(&pkt, 1024);
  uint32_t s = 0;
  EXPECT_EQ(coap_get_header_size1(&pkt, &s), 1);
  EXPECT_EQ(s, 1024u);
}

TEST_F(CoapTest, Size1_NotSet)
{
  init_con_get();
  uint32_t s = 0;
  EXPECT_EQ(coap_get_header_size1(&pkt, &s), 0);
}

TEST_F(CoapTest, Size2_SetGet)
{
  init_con_get();
  coap_set_header_size2(&pkt, 2048);
  uint32_t s = 0;
  EXPECT_EQ(coap_get_header_size2(&pkt, &s), 1);
  EXPECT_EQ(s, 2048u);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Echo
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(CoapTest, Echo_SetGet)
{
  init_con_get();
  uint8_t echo_data[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22, 0x33, 0x44};
  coap_set_header_echo(&pkt, echo_data, sizeof(echo_data));

  uint8_t out[8] = {};
  int len = coap_get_header_echo(&pkt, out);
  EXPECT_EQ(len, 8);
  EXPECT_EQ(memcmp(out, echo_data, 8), 0);
}

TEST_F(CoapTest, Echo_NotSet)
{
  init_con_get();
  uint8_t out[8] = {};
  EXPECT_EQ(coap_get_header_echo(&pkt, out), 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Payload
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(CoapTest, Payload_SetGet)
{
  init_con_get();
  const uint8_t data[] = {0x01, 0x02, 0x03};
  uint32_t len = coap_set_payload(&pkt, data, sizeof(data));
  EXPECT_EQ(len, 3u);

  const uint8_t *out = nullptr;
  uint32_t olen = coap_get_payload(&pkt, &out);
  EXPECT_EQ(olen, 3u);
  EXPECT_EQ(memcmp(out, data, 3), 0);
}

TEST_F(CoapTest, Payload_NotSet)
{
  init_con_get();
  const uint8_t *out = nullptr;
  EXPECT_EQ(coap_get_payload(&pkt, &out), 0u);
  EXPECT_EQ(out, nullptr);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * coap_set_status_code
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(CoapTest, SetStatusCode_Valid)
{
  init_con_get();
  EXPECT_EQ(coap_set_status_code(&pkt, CHANGED_2_04), 1);
  EXPECT_EQ(pkt.code, CHANGED_2_04);
}

TEST_F(CoapTest, SetStatusCode_TooLarge)
{
  init_con_get();
  EXPECT_EQ(coap_set_status_code(&pkt, 0x100), 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * coap_set_option_header
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(CoapTest, OptionHeader_SmallDelta)
{
  uint8_t buf[8] = {};
  size_t len = coap_set_option_header(5, 3, buf);
  /* delta=5, length=3 → 1 byte: 0x53 */
  EXPECT_EQ(len, 1u);
  EXPECT_EQ(buf[0], 0x53);
}

TEST_F(CoapTest, OptionHeader_ExtendedDelta)
{
  uint8_t buf[8] = {};
  /* delta=20 → nibble=13, extended byte=20-13=7 */
  size_t len = coap_set_option_header(20, 0, buf);
  EXPECT_EQ(len, 2u);
  EXPECT_EQ((buf[0] >> 4), 13);
  EXPECT_EQ(buf[1], 7);
}

TEST_F(CoapTest, OptionHeader_TwoByteExtendedDelta)
{
  uint8_t buf[8] = {};
  /* delta=300 → nibble=14, extended 2 bytes = 300-269=31 */
  size_t len = coap_set_option_header(300, 0, buf);
  EXPECT_EQ(len, 3u);
  EXPECT_EQ((buf[0] >> 4), 14);
  uint16_t ext = (buf[1] << 8) | buf[2];
  EXPECT_EQ(ext, 31);
}

TEST_F(CoapTest, OptionHeader_NullBuffer_CountOnly)
{
  /* passing NULL should just count bytes without writing */
  size_t len = coap_set_option_header(5, 3, nullptr);
  EXPECT_EQ(len, 1u);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Serialize → Parse round-trip
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(CoapTest, RoundTrip_EmptyMessage)
{
  coap_udp_init_message(&pkt, COAP_TYPE_CON, COAP_GET, 0x1234);
  size_t len = coap_serialize_message(&pkt, wire);
  EXPECT_GE(len, 4u); /* at least header */

  coap_packet_t parsed;
  coap_status_t st = coap_parse_udp_message(&parsed, wire, len);
  EXPECT_EQ(st, COAP_NO_ERROR);
  EXPECT_EQ(parsed.type, COAP_TYPE_CON);
  EXPECT_EQ(parsed.code, COAP_GET);
  EXPECT_EQ(parsed.mid, 0x1234);
  EXPECT_EQ(parsed.version, 1);
}

TEST_F(CoapTest, RoundTrip_WithToken)
{
  coap_udp_init_message(&pkt, COAP_TYPE_CON, COAP_GET, 0x5678);
  uint8_t tok[] = {0xAA, 0xBB, 0xCC, 0xDD};
  coap_set_token(&pkt, tok, sizeof(tok));

  size_t len = coap_serialize_message(&pkt, wire);
  EXPECT_GT(len, 4u);

  coap_packet_t parsed;
  coap_status_t st = coap_parse_udp_message(&parsed, wire, len);
  EXPECT_EQ(st, COAP_NO_ERROR);
  EXPECT_EQ(parsed.token_len, 4);
  EXPECT_EQ(memcmp(parsed.token, tok, 4), 0);
}

TEST_F(CoapTest, RoundTrip_WithContentFormat)
{
  coap_udp_init_message(&pkt, COAP_TYPE_ACK, CONTENT_2_05, 0x0001);
  coap_set_header_content_format(&pkt, APPLICATION_CBOR);

  size_t len = coap_serialize_message(&pkt, wire);

  coap_packet_t parsed;
  EXPECT_EQ(coap_parse_udp_message(&parsed, wire, len), COAP_NO_ERROR);

  oc_content_format_t fmt = CONTENT_NONE;
  EXPECT_EQ(coap_get_header_content_format(&parsed, &fmt), 1);
  EXPECT_EQ(fmt, APPLICATION_CBOR);
}

TEST_F(CoapTest, RoundTrip_WithPayload)
{
  coap_udp_init_message(&pkt, COAP_TYPE_ACK, CONTENT_2_05, 0x0002);
  uint8_t payload[] = {0xBF, 0x01, 0x18, 0x2A, 0xFF}; /* CBOR {1: 42} */
  coap_set_payload(&pkt, payload, sizeof(payload));
  coap_set_header_content_format(&pkt, APPLICATION_CBOR);

  size_t len = coap_serialize_message(&pkt, wire);

  coap_packet_t parsed;
  EXPECT_EQ(coap_parse_udp_message(&parsed, wire, len), COAP_NO_ERROR);
  EXPECT_EQ(parsed.payload_len, sizeof(payload));
  EXPECT_EQ(memcmp(parsed.payload, payload, sizeof(payload)), 0);
}

TEST_F(CoapTest, RoundTrip_WithUriPath)
{
  coap_udp_init_message(&pkt, COAP_TYPE_CON, COAP_GET, 0x0003);
  const char *path = "a/b/c";
  coap_set_header_uri_path(&pkt, path, strlen(path));

  size_t len = coap_serialize_message(&pkt, wire);

  coap_packet_t parsed;
  EXPECT_EQ(coap_parse_udp_message(&parsed, wire, len), COAP_NO_ERROR);

  const char *out = nullptr;
  size_t plen = coap_get_header_uri_path(&parsed, &out);
  EXPECT_EQ(plen, 5u);
  EXPECT_EQ(strncmp(out, "a/b/c", 5), 0);
}

TEST_F(CoapTest, RoundTrip_WithObserve)
{
  coap_udp_init_message(&pkt, COAP_TYPE_CON, COAP_GET, 0x0004);
  coap_set_header_observe(&pkt, 0);

  size_t len = coap_serialize_message(&pkt, wire);

  coap_packet_t parsed;
  EXPECT_EQ(coap_parse_udp_message(&parsed, wire, len), COAP_NO_ERROR);

  oc_client_observe_t obs = OC_OBSERVE_NOT_APPLICABLE;
  EXPECT_TRUE(coap_get_header_observe(&parsed, &obs));
  EXPECT_EQ(obs, OC_OBSERVE_REGISTER);
}

TEST_F(CoapTest, RoundTrip_WithETag)
{
  coap_udp_init_message(&pkt, COAP_TYPE_ACK, CONTENT_2_05, 0x0005);
  uint8_t etag[] = {0x11, 0x22, 0x33};
  coap_set_header_etag(&pkt, etag, sizeof(etag));

  size_t len = coap_serialize_message(&pkt, wire);

  coap_packet_t parsed;
  EXPECT_EQ(coap_parse_udp_message(&parsed, wire, len), COAP_NO_ERROR);

  const uint8_t *out = nullptr;
  int elen = coap_get_header_etag(&parsed, &out);
  EXPECT_EQ(elen, 3);
  EXPECT_EQ(memcmp(out, etag, 3), 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Parse error cases
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(CoapTest, Parse_WrongVersion)
{
  coap_udp_init_message(&pkt, COAP_TYPE_CON, COAP_GET, 0x0001);
  size_t len = coap_serialize_message(&pkt, wire);
  ASSERT_GE(len, 4u);

  /* corrupt version to 2 */
  wire[0] = (wire[0] & ~COAP_HEADER_VERSION_MASK) | (2 << COAP_HEADER_VERSION_POSITION);

  coap_packet_t parsed;
  EXPECT_NE(coap_parse_udp_message(&parsed, wire, len), COAP_NO_ERROR);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Message-ID generation: coap_init_connection / coap_get_next_mid
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(CoapTest, GetNextMid_IsMonotonicallyIncreasing)
{
  uint16_t a = coap_get_next_mid();
  uint16_t b = coap_get_next_mid();
  uint16_t c = coap_get_next_mid();

  EXPECT_EQ((uint16_t)(a + 1), b);
  EXPECT_EQ((uint16_t)(b + 1), c);
}

TEST_F(CoapTest, InitConnection_SeedsMidAndNextIsConsecutive)
{
  /* After seeding, the next two MIDs are consecutive regardless of seed. */
  coap_init_connection();
  uint16_t first = coap_get_next_mid();
  uint16_t second = coap_get_next_mid();

  EXPECT_EQ((uint16_t)(first + 1), second);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * coap_get_query_variable
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(CoapTest, GetQueryVariable_NoQueryOption_ReturnsZero)
{
  init_con_get();
  const char *out = nullptr;
  /* No URI_QUERY option set → returns 0 */
  EXPECT_EQ(coap_get_query_variable(&pkt, "pn", &out), 0);
}

TEST_F(CoapTest, GetQueryVariable_FindsValue)
{
  init_con_get();
  coap_set_header_uri_query(&pkt, "pn=12&ps=64");

  const char *out = nullptr;
  int len = coap_get_query_variable(&pkt, "pn", &out);
  ASSERT_EQ(len, 2);
  EXPECT_EQ(strncmp(out, "12", 2), 0);

  out = nullptr;
  len = coap_get_query_variable(&pkt, "ps", &out);
  ASSERT_EQ(len, 2);
  EXPECT_EQ(strncmp(out, "64", 2), 0);
}

TEST_F(CoapTest, GetQueryVariable_MissingName_ReturnsZero)
{
  init_con_get();
  coap_set_header_uri_query(&pkt, "pn=12&ps=64");

  const char *out = nullptr;
  EXPECT_EQ(coap_get_query_variable(&pkt, "zz", &out), 0);
  EXPECT_EQ(out, nullptr);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Proxy-URI header get/set
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(CoapTest, ProxyUri_GetWithoutOption_ReturnsZero)
{
  init_con_get();
  const char *out = nullptr;
  EXPECT_EQ(coap_get_header_proxy_uri(&pkt, &out), 0);
}

TEST_F(CoapTest, ProxyUri_SetThenGet)
{
  init_con_get();
  const char *uri = "coap://[fe80::1]/p/1";
  int set_len = coap_set_header_proxy_uri(&pkt, uri);
  EXPECT_EQ(set_len, (int)strlen(uri));

  const char *out = nullptr;
  int get_len = coap_get_header_proxy_uri(&pkt, &out);
  EXPECT_EQ(get_len, (int)strlen(uri));
  EXPECT_EQ(out, uri);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Uri-Query header get
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(CoapTest, GetHeaderUriQuery_WithoutOption_ReturnsZero)
{
  init_con_get();
  const char *out = nullptr;
  EXPECT_EQ(coap_get_header_uri_query(&pkt, &out), 0u);
}

TEST_F(CoapTest, GetHeaderUriQuery_SetThenGet)
{
  init_con_get();
  const char *q = "pn=0&ps=5";
  coap_set_header_uri_query(&pkt, q);

  const char *out = nullptr;
  size_t len = coap_get_header_uri_query(&pkt, &out);
  EXPECT_EQ(len, strlen(q));
  EXPECT_EQ(out, q);
}

TEST_F(CoapTest, SetHeaderUriQuery_SkipsLeadingQuestionMarks)
{
  init_con_get();
  coap_set_header_uri_query(&pkt, "??pn=0");

  const char *out = nullptr;
  size_t len = coap_get_header_uri_query(&pkt, &out);
  EXPECT_EQ(len, strlen("pn=0"));
  EXPECT_EQ(strncmp(out, "pn=0", 4), 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Location-Query header set
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(CoapTest, SetHeaderLocationQuery_StoresValueAndSetsOption)
{
  init_con_get();
  const char *q = "token=abc";
  size_t len = coap_set_header_location_query(&pkt, q);
  EXPECT_EQ(len, strlen(q));
  EXPECT_EQ(pkt.location_query, q);
  EXPECT_EQ(pkt.location_query_len, strlen(q));
}

TEST_F(CoapTest, SetHeaderLocationQuery_SkipsLeadingQuestionMarks)
{
  init_con_get();
  size_t len = coap_set_header_location_query(&pkt, "?id=7");
  EXPECT_EQ(len, strlen("id=7"));
  EXPECT_EQ(strncmp(pkt.location_query, "id=7", 4), 0);
}
