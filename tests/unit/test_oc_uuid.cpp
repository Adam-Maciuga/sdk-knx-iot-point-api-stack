/*
 * Copyright (c) 2026 KNX Association
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for api/oc_uuid.c
 *
 * Covers:
 *   oc_str_to_uuid - string -> binary parsing
 *   oc_uuid_to_str - binary -> string formatting
 *   oc_gen_uuid    - random version-4 UUID generation + validation
 *
 * Requirements:
 *   oc_gen_uuid draws randomness from psa_generate_random(), so PSA crypto
 *   must be initialized once before any generation test runs.
 */

#include <gtest/gtest.h>
#include <cctype>
#include <cstring>

extern "C" {
#include "oc_uuid.h"
#include "psa/crypto.h"
}

/* Global setup: PSA crypto must be initialized before oc_gen_uuid() can draw
   random bytes via psa_generate_random(). */
class PsaCryptoEnv : public ::testing::Environment {
public:
  void SetUp() override { psa_crypto_init(); }
};
static auto *g_psa_env __attribute__((unused)) =
    ::testing::AddGlobalTestEnvironment(new PsaCryptoEnv);

// ---------------------------------------------------------------------------
// oc_str_to_uuid / oc_uuid_to_str round-trip
// ---------------------------------------------------------------------------

TEST(UuidStrToUuid, KnownUuid)
{
  oc_uuid_t uuid;
  oc_str_to_uuid("1628fbcc-13ce-4e37-b883-1fd8d2ad945d", &uuid);

  // Verify specific bytes from the known UUID
  EXPECT_EQ(uuid.id[0], 0x16);
  EXPECT_EQ(uuid.id[1], 0x28);
  EXPECT_EQ(uuid.id[2], 0xfb);
  EXPECT_EQ(uuid.id[3], 0xcc);
  EXPECT_EQ(uuid.id[4], 0x13);
  EXPECT_EQ(uuid.id[5], 0xce);
  EXPECT_EQ(uuid.id[6], 0x4e);
  EXPECT_EQ(uuid.id[7], 0x37);
  EXPECT_EQ(uuid.id[8], 0xb8);
  EXPECT_EQ(uuid.id[9], 0x83);
  EXPECT_EQ(uuid.id[10], 0x1f);
  EXPECT_EQ(uuid.id[11], 0xd8);
  EXPECT_EQ(uuid.id[12], 0xd2);
  EXPECT_EQ(uuid.id[13], 0xad);
  EXPECT_EQ(uuid.id[14], 0x94);
  EXPECT_EQ(uuid.id[15], 0x5d);
}

TEST(UuidToStr, KnownUuid)
{
  oc_uuid_t uuid = {{0x16, 0x28, 0xfb, 0xcc, 0x13, 0xce, 0x4e, 0x37,
                      0xb8, 0x83, 0x1f, 0xd8, 0xd2, 0xad, 0x94, 0x5d}};
  char buf[OC_UUID_LEN] = {};
  oc_uuid_to_str(&uuid, buf, OC_UUID_LEN);
  EXPECT_STREQ(buf, "1628fbcc-13ce-4e37-b883-1fd8d2ad945d");
}

TEST(UuidRoundTrip, StringToBinaryToString)
{
  const char *input = "a0b1c2d3-e4f5-6789-abcd-ef0123456789";
  oc_uuid_t uuid;
  oc_str_to_uuid(input, &uuid);

  char output[OC_UUID_LEN] = {};
  oc_uuid_to_str(&uuid, output, OC_UUID_LEN);
  EXPECT_STREQ(output, input);
}

// ---------------------------------------------------------------------------
// Wildcard / special cases
// ---------------------------------------------------------------------------

TEST(UuidStrToUuid, WildcardStar)
{
  oc_uuid_t uuid;
  oc_str_to_uuid("*", &uuid);

  EXPECT_EQ(uuid.id[0], '*');
  // All remaining bytes should be zero
  for (int i = 1; i < 16; ++i) {
    EXPECT_EQ(uuid.id[i], 0) << "byte " << i << " should be zero";
  }
}

TEST(UuidToStr, WildcardStar)
{
  oc_uuid_t uuid = {{}};
  memset(&uuid, 0, sizeof(uuid));
  uuid.id[0] = '*';

  char buf[OC_UUID_LEN] = {};
  oc_uuid_to_str(&uuid, buf, OC_UUID_LEN);
  EXPECT_STREQ(buf, "*");
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

TEST(UuidToStr, BufferTooSmall)
{
  oc_uuid_t uuid = {{0x16, 0x28, 0xfb, 0xcc, 0x13, 0xce, 0x4e, 0x37,
                      0xb8, 0x83, 0x1f, 0xd8, 0xd2, 0xad, 0x94, 0x5d}};
  char buf[10] = "unchanged";
  oc_uuid_to_str(&uuid, buf, sizeof(buf));
  // Buffer too small — function should not modify it
  EXPECT_STREQ(buf, "unchanged");
}

TEST(UuidToStr, AllZeros)
{
  oc_uuid_t uuid = {{}};
  memset(&uuid, 0, sizeof(uuid));
  char buf[OC_UUID_LEN] = {};
  oc_uuid_to_str(&uuid, buf, OC_UUID_LEN);
  EXPECT_STREQ(buf, "00000000-0000-0000-0000-000000000000");
}

TEST(UuidToStr, AllOnes)
{
  oc_uuid_t uuid;
  memset(&uuid, 0xff, sizeof(uuid));
  char buf[OC_UUID_LEN] = {};
  oc_uuid_to_str(&uuid, buf, OC_UUID_LEN);
  EXPECT_STREQ(buf, "ffffffff-ffff-ffff-ffff-ffffffffffff");
}

TEST(UuidStrToUuid, UppercaseHex)
{
  // The parser should handle both upper and lower case hex
  oc_uuid_t uuid;
  oc_str_to_uuid("AABBCCDD-EEFF-0011-2233-445566778899", &uuid);

  char buf[OC_UUID_LEN] = {};
  oc_uuid_to_str(&uuid, buf, OC_UUID_LEN);
  // oc_uuid_to_str always outputs lowercase
  EXPECT_STREQ(buf, "aabbccdd-eeff-0011-2233-445566778899");
}

// ---------------------------------------------------------------------------
// oc_gen_uuid — generate a random version-4 UUID, then validate it
// ---------------------------------------------------------------------------

/*
  Validate that a string is a canonical RFC 4122 UUID:
  8-4-4-4-12 lowercase hex digits separated by hyphens.
*/
static bool
is_valid_uuid_string(const char *str)
{
  if (str == nullptr || strlen(str) != 36) {
    return false;
  }
  for (int i = 0; i < 36; ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (str[i] != '-') {
        return false;
      }
    } else if (!isxdigit((unsigned char)str[i])) {
      return false;
    }
  }
  return true;
}

TEST(UuidGenerate, ProducesValidFormat)
{
  oc_uuid_t uuid;
  memset(&uuid, 0, sizeof(uuid));
  oc_gen_uuid(&uuid);

  char buf[OC_UUID_LEN] = {};
  oc_uuid_to_str(&uuid, buf, OC_UUID_LEN);
  EXPECT_TRUE(is_valid_uuid_string(buf)) << "generated UUID '" << buf << "' is not canonical";
}

TEST(UuidGenerate, ProducesVersion4)
{
  oc_uuid_t uuid;
  memset(&uuid, 0, sizeof(uuid));
  oc_gen_uuid(&uuid);

  // RFC 4122: the high nibble of octet 6 carries the version (4 for random)
  EXPECT_EQ(uuid.id[6] & 0xf0, 0x40) << "version field must be 4";
}

TEST(UuidGenerate, RoundTripsThroughString)
{
  oc_uuid_t uuid;
  memset(&uuid, 0, sizeof(uuid));
  oc_gen_uuid(&uuid);

  char str[OC_UUID_LEN] = {};
  oc_uuid_to_str(&uuid, str, OC_UUID_LEN);

  oc_uuid_t parsed;
  memset(&parsed, 0, sizeof(parsed));
  oc_str_to_uuid(str, &parsed);

  EXPECT_EQ(memcmp(uuid.id, parsed.id, sizeof(uuid.id)), 0);
}

TEST(UuidGenerate, ProducesDistinctValues)
{
  oc_uuid_t a, b;
  memset(&a, 0, sizeof(a));
  memset(&b, 0, sizeof(b));
  oc_gen_uuid(&a);
  oc_gen_uuid(&b);

  // Two independently generated UUIDs should not collide
  EXPECT_NE(memcmp(a.id, b.id, sizeof(a.id)), 0);
}
