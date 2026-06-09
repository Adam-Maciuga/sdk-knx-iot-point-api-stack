/*
 * Unit tests for port/oc_log.c — the byte-array hex logging helper.
 *
 * knx_log_bytes_hex() formats a buffer as lowercase space-separated hex,
 * 32 bytes per line, prefixed by a label, and writes it via PRINTF (stdout)
 * on non-Zephyr builds. We capture stdout to assert the exact formatting,
 * including the 32-byte line-wrap boundary.
 *
 * oc_file_print() is compiled only under KNX_LOG_TO_FILE (off in the test
 * build), so it is not present here.
 */

#include <gtest/gtest.h>

#include <string>

extern "C" {
#include "port/oc_log.h"
}

TEST(OcLog, FormatsSingleLineLowercaseHex)
{
  uint8_t bytes[] = { 0xAB, 0xCD, 0x01, 0x00 };
  testing::internal::CaptureStdout();
  knx_log_bytes_hex("L:", bytes, sizeof(bytes));
  std::string out = testing::internal::GetCapturedStdout();
  EXPECT_EQ(out, "L:ab cd 01 00\n");
}

TEST(OcLog, EmptyBufferProducesNoOutput)
{
  testing::internal::CaptureStdout();
  knx_log_bytes_hex("L:", nullptr, 0);
  std::string out = testing::internal::GetCapturedStdout();
  EXPECT_EQ(out, "");
}

TEST(OcLog, SixteenBytesStayOnOneLine)
{
  uint8_t bytes[16];
  for (int i = 0; i < 16; i++) {
    bytes[i] = (uint8_t)i;
  }
  testing::internal::CaptureStdout();
  knx_log_bytes_hex("", bytes, sizeof(bytes));
  std::string out = testing::internal::GetCapturedStdout();
  EXPECT_EQ(out, "00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f\n");
}

TEST(OcLog, WrapsAfterThirtyTwoBytes)
{
  /* 33 bytes -> first line of 32, second line of 1. */
  uint8_t bytes[33];
  for (int i = 0; i < 33; i++) {
    bytes[i] = 0x00;
  }
  testing::internal::CaptureStdout();
  knx_log_bytes_hex("", bytes, sizeof(bytes));
  std::string out = testing::internal::GetCapturedStdout();

  /* Two newline-terminated lines. */
  size_t first_nl = out.find('\n');
  ASSERT_NE(first_nl, std::string::npos);
  /* First line: 32 "00" tokens separated by spaces = 32*3 - 1 = 95 chars. */
  EXPECT_EQ(first_nl, 95u);
  /* There is a second line after the first newline. */
  EXPECT_EQ(out.substr(first_nl + 1), "00\n");
}
