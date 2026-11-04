/*
 * Unit tests for the RI timed-event list (api/oc_ri.c: oc_ri_add_timed_event_callback_*, oc_ri_remove_timed_event_callback,
 * poll_event_callback_timers) through the public API oc_set_delayed_callback / oc_remove_delayed_callback.
 *
 * Requirements:
 *   oc_ri_init() - the timed-event list and its process; oc_main_poll() drives it.
 *   oc_clock_time - wrapped (-Wl,--wrap=oc_clock_time), a test-controlled clock so the one second delay needs no sleep.
 *
 * Focus: a callback that removes itself. The poll loop used to free the entry a second time (OC_EVENT_DONE) or to
 * restart the timer of the freed entry (OC_EVENT_CONTINUE); both show up under the ASan preset, the counts below
 * show the visible effect (the callback runs once and is gone).
 */

/*
 * Unit tests for the timed event callbacks of api/oc_ri.c
 *
 * Covers:
 *   oc_ri_add_timed_event_callback_ticks, oc_ri_remove_timed_event_callback and the event loop
 *   that runs the due callbacks (poll_event_callback_timers, static, reached through
 *   oc_main_poll):
 *   - a callback that removes its own entry while it runs runs once, with OC_EVENT_DONE and with
 *     OC_EVENT_CONTINUE as its return value
 *   - a callback that removes itself and arms itself again with another delay runs again after
 *     that delay
 *   - a callback that removes another pending callback keeps that one from running
 *
 * Before the running entry was protected, a callback that removed itself caused a double free,
 * and a callback that armed itself again lost the new entry. The ASan build of these tests
 * catches a regression.
 *
 * The clock is test-controlled (--wrap=oc_clock_time), the tests move it themselves.
 */

#include <gtest/gtest.h>

extern "C" {
#include "oc_api.h"
#include "oc_ri.h"
#include "port/oc_clock.h"
}

/* -- test clock --------------------------------------------------------------- */

static oc_clock_time_t g_now;

extern "C" oc_clock_time_t __wrap_oc_clock_time(void)
{
  return g_now;
}

static int g_calls;
static oc_event_callback_retval_t g_retval;
static int g_marker; /* cb_data, identifies the entry for oc_remove_delayed_callback */

/* removes its own entry, then returns g_retval */
static oc_event_callback_retval_t self_removing_cb(void* data)
{
  g_calls++;
  oc_remove_delayed_callback(data, self_removing_cb);
  return g_retval;
}

/* keeps running until removed from outside */
static oc_event_callback_retval_t continue_cb(void* data)
{
  g_runs++;
  if (g_runs == 1) {
    oc_ri_remove_timed_event_callback(data, arms_itself_again);
    oc_ri_add_timed_event_callback_ticks(data, arms_itself_again,
                                         (oc_clock_time_t)2 * OC_CLOCK_SECOND);
  }

  return OC_EVENT_DONE;
}

static oc_event_callback_retval_t other_callback(void* data)
{
  (void)data;
  g_calls++;
  return OC_EVENT_CONTINUE;
}

class TimedCallbacks : public ::testing::Test {
protected:
  void SetUp() override
  {
    /* start away from 0, so an expired timer never looks like an unset one */
    g_now = 100 * OC_CLOCK_SECOND;
    g_calls = 0;
    oc_ri_init();
  }

  void TearDown() override { oc_ri_shutdown(); }

  /* move the clock and let the RI event loop run every due callback */
  static void advance_ms(unsigned ms)
  {
    g_now += (oc_clock_time_t)ms * OC_CLOCK_SECOND / 1000;
    (void)oc_main_poll();
    (void)oc_main_poll();
  }
};

TEST_F(TimedCallbacks, SelfRemovingCallbackReturningDoneRunsOnce)
{
  g_retval = OC_EVENT_DONE;
  oc_set_delayed_callback(&g_marker, self_removing_cb, 1);

  advance_ms(1100);
  EXPECT_EQ(g_calls, 1);

  advance_ms(3000);
  EXPECT_EQ(g_calls, 1);
}

TEST_F(TimedCallbacks, SelfRemovingCallbackReturningContinueRunsOnce)
{
  g_retval = OC_EVENT_CONTINUE;
  oc_set_delayed_callback(&g_marker, self_removing_cb, 1);

  advance_ms(1100);
  EXPECT_EQ(g_calls, 1);

  /* the removed entry must not be restarted, so it never fires again */
  advance_ms(1100);
  advance_ms(1100);
  EXPECT_EQ(g_calls, 1);
}

/* the normal paths stay as they are: CONTINUE repeats, a removal from outside stops it */
TEST_F(TimedCallbacks, ContinueRepeatsUntilRemovedFromOutside)
{
  oc_set_delayed_callback(&g_marker, continue_cb, 1);

  advance_ms(1100);
  advance_ms(1100);
  EXPECT_EQ(g_calls, 2);

  oc_remove_delayed_callback(&g_marker, continue_cb);
  advance_ms(1100);
  EXPECT_EQ(g_calls, 2);
}
