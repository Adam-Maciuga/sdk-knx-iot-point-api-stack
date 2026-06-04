/*
 * Copyright (c) 2026 KNX Association
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for api/oc_uuid.c
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "oc_uuid.h"
}

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
