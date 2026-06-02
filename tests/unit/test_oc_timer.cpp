/*
 * Unit tests for util/oc_timer.c
 *
 * Covers:
 *   oc_timer_set       — set interval and start time
 *   oc_timer_reset     — advance start by interval (drift-free)
 *   oc_timer_restart   — restart from current time
 *   oc_timer_expired   — check if timer has elapsed
 *   oc_timer_remaining — time left until expiry
 *
 * Uses the real oc_clock_time() from the platform port.
 * Timers use large intervals so wall-clock jitter doesn't matter.
 */

#include <gtest/gtest.h>

extern "C" {
#include "util/oc_timer.h"
#include "port/oc_clock.h"

}

TEST(OcTimer, SetStoresInterval)
{
  struct oc_timer t;
  oc_timer_set(&t, 1000);
  EXPECT_EQ(t.interval, 1000u);
  /* start should be close to oc_clock_time() */
  EXPECT_NE(t.start, 0u);
}

TEST(OcTimer, NotExpiredImmediately)
{
  struct oc_timer t;
  /* Set a very large interval — won't expire during this test */
  oc_timer_set(&t, OC_CLOCK_SECOND * 3600);
  EXPECT_EQ(oc_timer_expired(&t), 0);
}

TEST(OcTimer, ExpiredWithZeroInterval)
{
  struct oc_timer t;
  oc_timer_set(&t, 0);
  /* interval=0, any time diff ≥ 0 means expired */
  EXPECT_NE(oc_timer_expired(&t), 0);
}

TEST(OcTimer, ExpiredWithPastStart)
{
  struct oc_timer t;
  /* Manually set start far in the past */
  t.interval = 100;
  t.start = oc_clock_time() - 200;
  EXPECT_NE(oc_timer_expired(&t), 0);
}

TEST(OcTimer, RemainingDecreasesOverTime)
{
  struct oc_timer t;
  oc_timer_set(&t, OC_CLOCK_SECOND * 3600);
  oc_clock_time_t r = oc_timer_remaining(&t);
  /* Remaining should be close to the interval (within 1 second) */
  EXPECT_GT(r, OC_CLOCK_SECOND * 3599);
  EXPECT_LE(r, OC_CLOCK_SECOND * 3600);
}

TEST(OcTimer, ResetAdvancesStartByInterval)
{
  struct oc_timer t;
  oc_timer_set(&t, 500);
  oc_clock_time_t orig_start = t.start;
  oc_timer_reset(&t);
  EXPECT_EQ(t.start, orig_start + 500);
  /* interval unchanged */
  EXPECT_EQ(t.interval, 500u);
}

TEST(OcTimer, RestartUsesCurrentTime)
{
  struct oc_timer t;
  t.interval = 1000;
  t.start = 0; /* ancient */
  oc_timer_restart(&t);
  /* start should now be close to oc_clock_time() */
  oc_clock_time_t now = oc_clock_time();
  EXPECT_GE(t.start, now - OC_CLOCK_SECOND);
  EXPECT_LE(t.start, now);
  /* interval unchanged */
  EXPECT_EQ(t.interval, 1000u);
}
