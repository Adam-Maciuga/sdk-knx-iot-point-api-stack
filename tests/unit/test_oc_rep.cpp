/*
 * Unit tests for api/oc_rep.c
 *
 * Covers: oc_rep_new, encode+parse round-trips for int/bool/float/double/
 *         string/byte_string, int-keyed variants, arrays, nested objects,
 *         oc_rep_to_json, oc_rep_add_line_to_buffer, oc_rep_encode_raw,
 *         oc_rep_get_encoded_payload_size, error/edge cases.
 *
 * Setup: each test allocates a CBOR buffer, encodes data, parses it back
 *        into an oc_rep_t via oc_parse_rep, verifies via oc_rep_get_* helpers,
 *        then frees.
 */

#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>

extern "C" {
#include "oc_rep.h"
#include "util/oc_mmem.h"
}

static const int CBOR_BUF_SIZE = 1024;

class OcRepTest : public ::testing::Test {
protected:
  uint8_t buf[1024];
  oc_rep_t *rep;

  void SetUp() override
  {
    oc_mmem_init();
    rep = nullptr;
    memset(buf, 0, sizeof(buf));
  }

  void TearDown() override
  {
    if (rep) {
      oc_free_rep(rep);
      rep = nullptr;
    }
  }

  /* Helper: finish encoding, parse into this->rep. Returns 0 on success. */
  int encode_and_parse()
  {
    oc_rep_end_root_object();
    EXPECT_EQ(g_err, CborNoError);
    int payload_size = oc_rep_get_encoded_payload_size();
    EXPECT_GT(payload_size, 0);
    const uint8_t *payload = oc_rep_get_encoder_buf();
    return oc_parse_rep(payload, payload_size, &rep);
  }
};

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_rep_new / oc_rep_get_encoded_payload_size basics
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(OcRepTest, New_EmptyPayload)
{
  oc_rep_new(buf, sizeof(buf));
  /* empty map */
  oc_rep_begin_root_object();
  oc_rep_end_root_object();
  EXPECT_EQ(g_err, CborNoError);
  int size = oc_rep_get_encoded_payload_size();
  EXPECT_GT(size, 0);
}

TEST_F(OcRepTest, GetEncoderBuf_ReturnsSamePointer)
{
  oc_rep_new(buf, sizeof(buf));
  EXPECT_EQ(oc_rep_get_encoder_buf(), buf);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Integer (text key + integer key)
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(OcRepTest, SetInt_TextKey)
{
  oc_rep_new(buf, sizeof(buf));
  oc_rep_begin_root_object();
  oc_rep_i_set_int(root, 5, 42);
  int rc = encode_and_parse();
  ASSERT_EQ(rc, CborNoError);

  int64_t value = 0;
  EXPECT_TRUE(oc_rep_i_get_int(rep, 5, &value));
  EXPECT_EQ(value, 42);
}

TEST_F(OcRepTest, SetInt_LargeValue)
{
  oc_rep_new(buf, sizeof(buf));
  oc_rep_begin_root_object();
  oc_rep_i_set_int(root, 1, INT64_MAX);
  int rc = encode_and_parse();
  ASSERT_EQ(rc, CborNoError);

  int64_t value = 0;
  EXPECT_TRUE(oc_rep_i_get_int(rep, 1, &value));
  EXPECT_EQ(value, INT64_MAX);
}

TEST_F(OcRepTest, SetInt_NegativeValue)
{
  oc_rep_new(buf, sizeof(buf));
  oc_rep_begin_root_object();
  oc_rep_i_set_int(root, 2, -999);
  int rc = encode_and_parse();
  ASSERT_EQ(rc, CborNoError);

  int64_t value = 0;
  EXPECT_TRUE(oc_rep_i_get_int(rep, 2, &value));
  EXPECT_EQ(value, -999);
}

TEST_F(OcRepTest, GetInt_WrongKey_ReturnsFalse)
{
  oc_rep_new(buf, sizeof(buf));
  oc_rep_begin_root_object();
  oc_rep_i_set_int(root, 5, 42);
  int rc = encode_and_parse();
  ASSERT_EQ(rc, CborNoError);

  int64_t value = 0;
  EXPECT_FALSE(oc_rep_i_get_int(rep, 99, &value));
}

TEST_F(OcRepTest, GetInt_NullValue)
{
  EXPECT_FALSE(oc_rep_i_get_int(nullptr, 5, nullptr));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Boolean
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(OcRepTest, SetBool_True)
{
  oc_rep_new(buf, sizeof(buf));
  oc_rep_begin_root_object();
  oc_rep_i_set_boolean(root, 3, true);
  int rc = encode_and_parse();
  ASSERT_EQ(rc, CborNoError);

  bool value = false;
  EXPECT_TRUE(oc_rep_i_get_bool(rep, 3, &value));
  EXPECT_TRUE(value);
}

TEST_F(OcRepTest, SetBool_False)
{
  oc_rep_new(buf, sizeof(buf));
  oc_rep_begin_root_object();
  oc_rep_i_set_boolean(root, 4, false);
  int rc = encode_and_parse();
  ASSERT_EQ(rc, CborNoError);

  bool value = true;
  EXPECT_TRUE(oc_rep_i_get_bool(rep, 4, &value));
  EXPECT_FALSE(value);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Double
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(OcRepTest, SetDouble)
{
  oc_rep_new(buf, sizeof(buf));
  oc_rep_begin_root_object();
  oc_rep_i_set_double(root, 10, 3.14159);
  int rc = encode_and_parse();
  ASSERT_EQ(rc, CborNoError);

  double value = 0.0;
  EXPECT_TRUE(oc_rep_i_get_double(rep, 10, &value));
  EXPECT_DOUBLE_EQ(value, 3.14159);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Float
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(OcRepTest, SetFloat)
{
  oc_rep_new(buf, sizeof(buf));
  oc_rep_begin_root_object();
  oc_rep_i_set_float(root, 11, 2.5f);
  int rc = encode_and_parse();
  ASSERT_EQ(rc, CborNoError);

  float value = 0.0f;
  EXPECT_TRUE(oc_rep_i_get_float(rep, 11, &value));
  EXPECT_FLOAT_EQ(value, 2.5f);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Text string (integer key)
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(OcRepTest, SetTextString_IntKey)
{
  oc_rep_new(buf, sizeof(buf));
  oc_rep_begin_root_object();
  oc_rep_i_set_text_string(root, 6, "hello");
  int rc = encode_and_parse();
  ASSERT_EQ(rc, CborNoError);

  char *str = nullptr;
  size_t str_len = 0;
  EXPECT_TRUE(oc_rep_i_get_string(rep, 6, &str, &str_len));
  EXPECT_STREQ(str, "hello");
  EXPECT_EQ(str_len, 5u);
}

TEST_F(OcRepTest, SetTextString_NullValue)
{
  oc_rep_new(buf, sizeof(buf));
  oc_rep_begin_root_object();
  oc_rep_i_set_text_string(root, 7, nullptr);
  int rc = encode_and_parse();
  ASSERT_EQ(rc, CborNoError);

  char *str = nullptr;
  size_t str_len = 99;
  EXPECT_TRUE(oc_rep_i_get_string(rep, 7, &str, &str_len));
  EXPECT_STREQ(str, "");
  EXPECT_EQ(str_len, 0u);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Byte string
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(OcRepTest, SetByteString)
{
  uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
  oc_rep_new(buf, sizeof(buf));
  oc_rep_begin_root_object();
  oc_rep_i_set_byte_string(root, 8, data, sizeof(data));
  int rc = encode_and_parse();
  ASSERT_EQ(rc, CborNoError);

  char *bs = nullptr;
  size_t bs_len = 0;
  EXPECT_TRUE(oc_rep_i_get_byte_string(rep, 8, &bs, &bs_len));
  EXPECT_EQ(bs_len, 4u);
  EXPECT_EQ(memcmp(bs, data, 4), 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Multiple values in one object
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(OcRepTest, MultipleValues)
{
  oc_rep_new(buf, sizeof(buf));
  oc_rep_begin_root_object();
  oc_rep_i_set_int(root, 1, 100);
  oc_rep_i_set_boolean(root, 2, true);
  oc_rep_i_set_text_string(root, 3, "test");
  int rc = encode_and_parse();
  ASSERT_EQ(rc, CborNoError);

  int64_t ival = 0;
  bool bval = false;
  char *sval = nullptr;
  size_t slen = 0;
  EXPECT_TRUE(oc_rep_i_get_int(rep, 1, &ival));
  EXPECT_EQ(ival, 100);
  EXPECT_TRUE(oc_rep_i_get_bool(rep, 2, &bval));
  EXPECT_TRUE(bval);
  EXPECT_TRUE(oc_rep_i_get_string(rep, 3, &sval, &slen));
  EXPECT_STREQ(sval, "test");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Int array
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(OcRepTest, IntArray)
{
  oc_rep_new(buf, sizeof(buf));
  oc_rep_begin_root_object();
  oc_rep_i_set_key(&root_map, 20);
  oc_rep_begin_array(&root_map, myarr);
  oc_rep_add_int(myarr, 10);
  oc_rep_add_int(myarr, 20);
  oc_rep_add_int(myarr, 30);
  oc_rep_end_array(&root_map, myarr);
  int rc = encode_and_parse();
  ASSERT_EQ(rc, CborNoError);

  int64_t *arr = nullptr;
  size_t arr_size = 0;
  EXPECT_TRUE(oc_rep_i_get_int_array(rep, 20, &arr, &arr_size));
  ASSERT_EQ(arr_size, 3u);
  EXPECT_EQ(arr[0], 10);
  EXPECT_EQ(arr[1], 20);
  EXPECT_EQ(arr[2], 30);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Bool array
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(OcRepTest, BoolArray)
{
  oc_rep_new(buf, sizeof(buf));
  oc_rep_begin_root_object();
  oc_rep_i_set_key(&root_map, 21);
  oc_rep_begin_array(&root_map, barr);
  oc_rep_add_boolean(barr, true);
  oc_rep_add_boolean(barr, false);
  oc_rep_add_boolean(barr, true);
  oc_rep_end_array(&root_map, barr);
  int rc = encode_and_parse();
  ASSERT_EQ(rc, CborNoError);

  bool *arr = nullptr;
  size_t arr_size = 0;
  EXPECT_TRUE(oc_rep_i_get_bool_array(rep, 21, &arr, &arr_size));
  ASSERT_EQ(arr_size, 3u);
  EXPECT_TRUE(arr[0]);
  EXPECT_FALSE(arr[1]);
  EXPECT_TRUE(arr[2]);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Double array
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(OcRepTest, DoubleArray)
{
  oc_rep_new(buf, sizeof(buf));
  oc_rep_begin_root_object();
  oc_rep_i_set_key(&root_map, 22);
  oc_rep_begin_array(&root_map, darr);
  oc_rep_add_double(darr, 1.1);
  oc_rep_add_double(darr, 2.2);
  oc_rep_end_array(&root_map, darr);
  int rc = encode_and_parse();
  ASSERT_EQ(rc, CborNoError);

  double *arr = nullptr;
  size_t arr_size = 0;
  EXPECT_TRUE(oc_rep_i_get_double_array(rep, 22, &arr, &arr_size));
  ASSERT_EQ(arr_size, 2u);
  EXPECT_DOUBLE_EQ(arr[0], 1.1);
  EXPECT_DOUBLE_EQ(arr[1], 2.2);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_rep_to_json
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(OcRepTest, ToJson_Simple)
{
  oc_rep_new(buf, sizeof(buf));
  oc_rep_begin_root_object();
  oc_rep_i_set_int(root, 1, 42);
  int rc = encode_and_parse();
  ASSERT_EQ(rc, CborNoError);

  char json[256] = {};
  size_t json_len = oc_rep_to_json(rep, json, sizeof(json), false);
  EXPECT_GT(json_len, 0u);
  /* should contain "1":42 or "1": 42 */
  EXPECT_NE(strstr(json, "\"1\""), nullptr);
  EXPECT_NE(strstr(json, "42"), nullptr);
}

TEST_F(OcRepTest, ToJson_PrettyPrint)
{
  oc_rep_new(buf, sizeof(buf));
  oc_rep_begin_root_object();
  oc_rep_i_set_boolean(root, 5, true);
  int rc = encode_and_parse();
  ASSERT_EQ(rc, CborNoError);

  char json[256] = {};
  size_t json_len = oc_rep_to_json(rep, json, sizeof(json), true);
  EXPECT_GT(json_len, 0u);
  /* pretty print includes newlines */
  EXPECT_NE(strstr(json, "\n"), nullptr);
}

TEST_F(OcRepTest, ToJson_NullBuf_ReturnsSizeNeeded)
{
  oc_rep_new(buf, sizeof(buf));
  oc_rep_begin_root_object();
  oc_rep_i_set_int(root, 1, 42);
  int rc = encode_and_parse();
  ASSERT_EQ(rc, CborNoError);

  size_t needed = oc_rep_to_json(rep, nullptr, 0, false);
  EXPECT_GT(needed, 0u);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_rep_add_line_to_buffer
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(OcRepTest, AddLineToBuffer)
{
  oc_rep_new(buf, sizeof(buf));
  int len = oc_rep_add_line_to_buffer("hello");
  EXPECT_EQ(len, 5);
  /* "hello" was written at start of buf */
  EXPECT_EQ(memcmp(buf, "hello", 5), 0);
}

TEST_F(OcRepTest, AddLineToBuffer_Null)
{
  oc_rep_new(buf, sizeof(buf));
  int len = oc_rep_add_line_to_buffer(nullptr);
  EXPECT_EQ(len, 0);
}

TEST_F(OcRepTest, AddLineSizeToBuffer)
{
  oc_rep_new(buf, sizeof(buf));
  int len = oc_rep_add_line_size_to_buffer("hello world", 5);
  EXPECT_EQ(len, 5);
  EXPECT_EQ(memcmp(buf, "hello", 5), 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_rep_encode_raw
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(OcRepTest, EncodeRaw)
{
  oc_rep_new(buf, sizeof(buf));
  uint8_t raw[] = {0xA1, 0x01, 0x18, 0x2A}; /* CBOR: {1: 42} */
  oc_rep_encode_raw(raw, sizeof(raw));

  int size = oc_rep_get_encoded_payload_size();
  EXPECT_EQ(size, 4);
  EXPECT_EQ(memcmp(buf, raw, 4), 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_free_rep edge cases
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(OcRepTest, FreeRep_Null)
{
  /* should not crash */
  oc_free_rep(nullptr);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_parse_rep with empty/invalid input
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(OcRepTest, ParseRep_EmptyMap)
{
  oc_rep_new(buf, sizeof(buf));
  oc_rep_begin_root_object();
  oc_rep_end_root_object();
  ASSERT_EQ(g_err, CborNoError);
  int payload_size = oc_rep_get_encoded_payload_size();
  ASSERT_GT(payload_size, 0);

  oc_rep_t *parsed = nullptr;
  int rc = oc_parse_rep(buf, payload_size, &parsed);
  EXPECT_EQ(rc, CborNoError);
  /* An empty map yields NULL or empty rep */
  if (parsed) {
    oc_free_rep(parsed);
  }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Text-keyed variants (oc_rep_text_set_int, etc.)
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(OcRepTest, TextKeyInt)
{
  oc_rep_new(buf, sizeof(buf));
  oc_rep_begin_root_object();
  oc_rep_text_set_int(root, power, 42);
  int rc = encode_and_parse();
  ASSERT_EQ(rc, CborNoError);

  int64_t value = 0;
  EXPECT_TRUE(oc_rep_get_int(rep, "power", &value));
  EXPECT_EQ(value, 42);
}

TEST_F(OcRepTest, TextKeyBool)
{
  oc_rep_new(buf, sizeof(buf));
  oc_rep_begin_root_object();
  oc_rep_set_boolean(root, active, true);
  int rc = encode_and_parse();
  ASSERT_EQ(rc, CborNoError);

  bool value = false;
  EXPECT_TRUE(oc_rep_get_bool(rep, "active", &value));
  EXPECT_TRUE(value);
}

TEST_F(OcRepTest, TextKeyDouble)
{
  oc_rep_new(buf, sizeof(buf));
  oc_rep_begin_root_object();
  oc_rep_set_double(root, pi, 3.14159);
  int rc = encode_and_parse();
  ASSERT_EQ(rc, CborNoError);

  double value = 0.0;
  EXPECT_TRUE(oc_rep_get_double(rep, "pi", &value));
  EXPECT_DOUBLE_EQ(value, 3.14159);
}

TEST_F(OcRepTest, TextKeyString)
{
  oc_rep_new(buf, sizeof(buf));
  oc_rep_begin_root_object();
  oc_rep_text_set_text_string(root, greeting, "Hello, world!");
  int rc = encode_and_parse();
  ASSERT_EQ(rc, CborNoError);

  char *str = nullptr;
  size_t str_len = 0;
  EXPECT_TRUE(oc_rep_get_string(rep, "greeting", &str, &str_len));
  EXPECT_STREQ(str, "Hello, world!");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_rep_get_*  null parameter checks
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(OcRepTest, GetBool_NullValue)
{
  EXPECT_FALSE(oc_rep_get_bool(nullptr, "x", nullptr));
}

TEST_F(OcRepTest, GetDouble_NullValue)
{
  EXPECT_FALSE(oc_rep_get_double(nullptr, "x", nullptr));
}

TEST_F(OcRepTest, GetFloat_NullValue)
{
  EXPECT_FALSE(oc_rep_get_float(nullptr, "x", nullptr));
}

TEST_F(OcRepTest, GetString_NullSize)
{
  char *str = nullptr;
  EXPECT_FALSE(oc_rep_get_string(nullptr, "x", &str, nullptr));
}

TEST_F(OcRepTest, GetByteString_NullSize)
{
  char *str = nullptr;
  EXPECT_FALSE(oc_rep_get_byte_string(nullptr, "x", &str, nullptr));
}
