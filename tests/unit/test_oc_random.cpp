/*
 * Unit tests for port/random_psa.c — the PSA-crypto-backed RNG port.
 *
 * Covers the full public API:
 *   oc_random_init     — initialise PSA crypto
 *   oc_random_value    — 32-bit random value
 *   oc_random_fill     — fill a buffer with random bytes
 *   oc_random_destroy  — tear down PSA crypto
 *
 * The fixture initialises PSA in SetUp and frees it in TearDown. Randomness
 * is asserted statistically (values/buffers must not be all-equal/all-zero);
 * the probability of a false failure is negligible for 32-bit / 32-byte draws.
 */

#include <gtest/gtest.h>

#include <cstring>

extern "C" {
#include "port/oc_random.h"
}

class OcRandom : public ::testing::Test {
protected:
  void SetUp() override { oc_random_init(); }
  void TearDown() override { oc_random_destroy(); }
};

TEST_F(OcRandom, ValueProducesVaryingOutput)
{
  unsigned int first = oc_random_value();
  bool any_different = false;
  for (int i = 0; i < 16; i++) {
    if (oc_random_value() != first) {
      any_different = true;
      break;
    }
  }
  EXPECT_TRUE(any_different);
}

TEST_F(OcRandom, FillReturnsZeroAndWritesBytes)
{
  uint8_t buf[32];
  memset(buf, 0, sizeof(buf));
  EXPECT_EQ(oc_random_fill(buf, sizeof(buf)), 0);

  /* A 32-byte random buffer being all-zero is effectively impossible. */
  bool all_zero = true;
  for (size_t i = 0; i < sizeof(buf); i++) {
    if (buf[i] != 0) {
      all_zero = false;
      break;
    }
  }
  EXPECT_FALSE(all_zero);
}

TEST_F(OcRandom, FillZeroLengthSucceeds)
{
  uint8_t dummy = 0;
  EXPECT_EQ(oc_random_fill(&dummy, 0), 0);
}

TEST_F(OcRandom, TwoFillsDiffer)
{
  uint8_t a[16];
  uint8_t b[16];
  EXPECT_EQ(oc_random_fill(a, sizeof(a)), 0);
  EXPECT_EQ(oc_random_fill(b, sizeof(b)), 0);
  EXPECT_NE(memcmp(a, b, sizeof(a)), 0);
}

TEST_F(OcRandom, InitIsIdempotent)
{
  /* Re-initialising an already-initialised PSA must keep the RNG working. */
  oc_random_init();
  uint8_t buf[8];
  EXPECT_EQ(oc_random_fill(buf, sizeof(buf)), 0);
}
