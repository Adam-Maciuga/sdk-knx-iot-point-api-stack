/*
 * Unit tests for the buffer-settings API (implemented in api/oc_main.c,
 * declared in include/oc_buffer_settings.h).
 *
 * Covers:
 *   oc_set_mtu_size          — MTU setter, minimum guard, block-size derivation
 *   oc_get_mtu_size          — returns stored PDU size (payload + CoAP header)
 *   oc_get_max_app_data_size — returns the compile-time KNX_PAYLOAD_SIZE
 *   oc_get_block_size        — RFC 7959 block size (power of two, 16..1024)
 *
 * Requirements:
 *   None — all functions operate on static globals, no stack init needed.
 *   OC_BLOCK_WISE is enabled via the port oc_config.h, so the block-size
 *   derivation path in oc_set_mtu_size is active.
 *
 * Note: these functions share process-global state (_OC_MTU_SIZE /
 *       _OC_BLOCK_SIZE). Each test sets the MTU explicitly before asserting,
 *       so test order does not matter.
 */

#include <gtest/gtest.h>
#include <cstdint>

extern "C" {
#include "oc_buffer_settings.h"
}

static bool is_power_of_two(uint32_t v)
{
  return v != 0 && (v & (v - 1)) == 0;
}

/* ───────────────────────────── oc_get_max_app_data_size ──────────────────── */

TEST(BufferSettings, MaxAppDataSizeReturnsKnxPayloadSize)
{
  /* KNX_PAYLOAD_SIZE is fixed at compile time (CMakeLists.txt) and cannot be
   * changed at runtime anymore (oc_set_max_app_data_size was removed). */
  EXPECT_EQ(oc_get_max_app_data_size(), (uint32_t)KNX_PAYLOAD_SIZE);
}

TEST(BufferSettings, MaxAppDataSizeIsStable)
{
  /* No setter exists — repeated reads must be identical. */
  EXPECT_EQ(oc_get_max_app_data_size(), oc_get_max_app_data_size());
}

/* ───────────────────────────── oc_set_mtu_size guard ─────────────────────── */

TEST(BufferSettings, SetMtuSizeBelowMinimumReturnsError)
{
  /* A tiny MTU cannot hold one block (16 bytes) plus the CoAP header. */
  EXPECT_EQ(oc_set_mtu_size(0), -1);
  EXPECT_EQ(oc_set_mtu_size(16), -1);
}

TEST(BufferSettings, SetMtuSizeBelowMinimumDoesNotChangeState)
{
  ASSERT_EQ(oc_set_mtu_size(2048), 0);
  uint32_t before_mtu = oc_get_mtu_size();
  uint16_t before_block = oc_get_block_size();

  EXPECT_EQ(oc_set_mtu_size(1), -1);

  EXPECT_EQ(oc_get_mtu_size(), before_mtu);
  EXPECT_EQ(oc_get_block_size(), before_block);
}

TEST(BufferSettings, SetMtuSizeValidReturnsZero)
{
  EXPECT_EQ(oc_set_mtu_size(2048), 0);
  EXPECT_EQ(oc_set_mtu_size(1024), 0);
}

/* ───────────────────────────── oc_get_mtu_size ───────────────────────────── */

TEST(BufferSettings, GetMtuSizeAddsConstantHeaderOffset)
{
  /* The stored MTU is the requested payload size plus a fixed CoAP header
   * offset. We don't hard-code the header size: instead we verify the offset
   * is the same for two different valid inputs. */
  ASSERT_EQ(oc_set_mtu_size(2048), 0);
  uint32_t stored_a = oc_get_mtu_size();
  uint32_t offset_a = stored_a - 2048;

  ASSERT_EQ(oc_set_mtu_size(1024), 0);
  uint32_t stored_b = oc_get_mtu_size();
  uint32_t offset_b = stored_b - 1024;

  EXPECT_EQ(offset_a, offset_b);
  EXPECT_GT(stored_a, 2048u); /* header offset is non-zero */
}

/* ───────────────────────────── oc_get_block_size ─────────────────────────── */

TEST(BufferSettings, BlockSizeIsPowerOfTwoInRange)
{
  ASSERT_EQ(oc_set_mtu_size(2048), 0);
  uint16_t block = oc_get_block_size();
  EXPECT_TRUE(is_power_of_two(block)) << "block size = " << block;
  EXPECT_GE(block, 16);
  EXPECT_LE(block, 1024);
}

TEST(BufferSettings, LargeMtuYieldsMaxBlockSize)
{
  /* 2048 payload comfortably exceeds 1024 even after the header subtraction,
   * so the largest supported block (1024) must be selected. */
  ASSERT_EQ(oc_set_mtu_size(2048), 0);
  EXPECT_EQ(oc_get_block_size(), 1024);
}

TEST(BufferSettings, SmallerMtuYieldsSmallerBlockSize)
{
  ASSERT_EQ(oc_set_mtu_size(2048), 0);
  uint16_t big = oc_get_block_size();

  ASSERT_EQ(oc_set_mtu_size(1024), 0);
  uint16_t small = oc_get_block_size();

  EXPECT_TRUE(is_power_of_two(small)) << "block size = " << small;
  EXPECT_LT(small, big);
  EXPECT_GE(small, 16);
}
