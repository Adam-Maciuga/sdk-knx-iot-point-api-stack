/*
 * Unit tests for util/oc_process.c — the Contiki-derived cooperative
 * process/event scheduler.
 *
 * Covers the full public API by defining a real test process (via the
 * OC_PROCESS / OC_PROCESS_THREAD macros) and driving the scheduler:
 *   oc_process_init / oc_process_shutdown
 *   oc_process_start            — delivers a synchronous INIT event
 *   oc_process_is_running
 *   oc_process_post             — async event, delivered by oc_process_run
 *   oc_process_post_synch       — synchronous event, delivered immediately
 *   oc_process_poll             — schedules a POLL, drained by oc_process_run
 *   oc_process_run              — one event + pending polls per call
 *   oc_process_nevents          — queued events + pending poll
 *   oc_process_alloc_event      — monotonically increasing event ids
 *   oc_process_exit             — stops a running process
 *
 * The scheduler keeps file-static state (process list + event ring), so the
 * fixture re-inits it for every test and restarts the test process from a
 * clean protothread state.
 */

#include <gtest/gtest.h>

extern "C" {
#include "util/oc_process.h"
}

/* ----- A simple instrumented test process -------------------------------- */

static int g_init_count;
static int g_poll_count;
static int g_msg_count;
static int g_other_count;
static oc_process_event_t g_last_ev;
static oc_process_data_t g_last_data;

OC_PROCESS(test_proc, "unit test process");
OC_PROCESS_THREAD(test_proc, ev, data)
{
  OC_PROCESS_BEGIN();
  while (1) {
    if (ev == OC_PROCESS_EVENT_INIT) {
      g_init_count++;
    } else if (ev == OC_PROCESS_EVENT_POLL) {
      g_poll_count++;
    } else if (ev == OC_PROCESS_EVENT_MSG) {
      g_msg_count++;
    } else {
      g_other_count++;
    }
    g_last_ev = ev;
    g_last_data = data;
    OC_PROCESS_YIELD();
  }
  OC_PROCESS_END();
}

class OcProcess : public ::testing::Test {
protected:
  void SetUp() override
  {
    g_init_count = 0;
    g_poll_count = 0;
    g_msg_count = 0;
    g_other_count = 0;
    g_last_ev = 0;
    g_last_data = nullptr;
    oc_process_init();
    /* Reset the process struct's scheduler state so each test starts the
       protothread from the top. */
    test_proc.state = 0; /* OC_PROCESS_STATE_NONE */
    test_proc.needspoll = 0;
    test_proc.next = nullptr;
  }

  void TearDown() override
  {
    oc_process_shutdown();
  }
};

TEST_F(OcProcess, StartDeliversSynchronousInitEvent)
{
  EXPECT_FALSE(oc_process_is_running(&test_proc));
  oc_process_start(&test_proc, nullptr);
  EXPECT_TRUE(oc_process_is_running(&test_proc));
  EXPECT_EQ(g_init_count, 1);
  EXPECT_EQ(g_last_ev, OC_PROCESS_EVENT_INIT);
}

TEST_F(OcProcess, StartTwiceDoesNotReinitialize)
{
  oc_process_start(&test_proc, nullptr);
  EXPECT_EQ(g_init_count, 1);
  /* Starting an already-running process is a no-op. */
  oc_process_start(&test_proc, nullptr);
  EXPECT_EQ(g_init_count, 1);
}

TEST_F(OcProcess, PostQueuesEventDeliveredByRun)
{
  oc_process_start(&test_proc, nullptr);
  EXPECT_EQ(oc_process_post(&test_proc, OC_PROCESS_EVENT_MSG, nullptr),
            OC_PROCESS_ERR_OK);
  /* Queued but not yet delivered. */
  EXPECT_EQ(g_msg_count, 0);
  EXPECT_GT(oc_process_nevents(), 0);

  oc_process_run();
  EXPECT_EQ(g_msg_count, 1);
  EXPECT_EQ(g_last_ev, OC_PROCESS_EVENT_MSG);
}

TEST_F(OcProcess, PostSynchDeliversImmediately)
{
  oc_process_start(&test_proc, nullptr);
  oc_process_post_synch(&test_proc, OC_PROCESS_EVENT_MSG, nullptr);
  /* No oc_process_run() needed. */
  EXPECT_EQ(g_msg_count, 1);
  EXPECT_EQ(g_last_ev, OC_PROCESS_EVENT_MSG);
}

TEST_F(OcProcess, PostPropagatesData)
{
  oc_process_start(&test_proc, nullptr);
  int marker = 42;
  oc_process_post(&test_proc, OC_PROCESS_EVENT_MSG, &marker);
  oc_process_run();
  EXPECT_EQ(g_last_data, static_cast<oc_process_data_t>(&marker));
}

TEST_F(OcProcess, PollSchedulesPollEventDrainedByRun)
{
  oc_process_start(&test_proc, nullptr);
  oc_process_poll(&test_proc);
  /* A poll is pending. */
  EXPECT_GT(oc_process_nevents(), 0);
  EXPECT_EQ(g_poll_count, 0);

  oc_process_run();
  EXPECT_EQ(g_poll_count, 1);
  EXPECT_EQ(g_last_ev, OC_PROCESS_EVENT_POLL);
}

TEST_F(OcProcess, PollOnStoppedProcessIsIgnored)
{
  /* test_proc not started -> state NONE -> poll must be a no-op. */
  oc_process_poll(&test_proc);
  EXPECT_EQ(oc_process_nevents(), 0);
}

TEST_F(OcProcess, NEventsReflectsQueueDepth)
{
  oc_process_start(&test_proc, nullptr);
  EXPECT_EQ(oc_process_nevents(), 0);
  oc_process_post(&test_proc, OC_PROCESS_EVENT_MSG, nullptr);
  oc_process_post(&test_proc, OC_PROCESS_EVENT_MSG, nullptr);
  EXPECT_EQ(oc_process_nevents(), 2);
  oc_process_run();
  EXPECT_EQ(oc_process_nevents(), 1);
  oc_process_run();
  EXPECT_EQ(oc_process_nevents(), 0);
}

TEST_F(OcProcess, RunReturnsRemainingEventCount)
{
  oc_process_start(&test_proc, nullptr);
  oc_process_post(&test_proc, OC_PROCESS_EVENT_MSG, nullptr);
  oc_process_post(&test_proc, OC_PROCESS_EVENT_MSG, nullptr);
  EXPECT_EQ(oc_process_run(), 1); /* one delivered, one left */
  EXPECT_EQ(oc_process_run(), 0); /* last delivered */
}

TEST_F(OcProcess, AllocEventReturnsIncreasingIds)
{
  oc_process_event_t a = oc_process_alloc_event();
  oc_process_event_t b = oc_process_alloc_event();
  EXPECT_EQ(static_cast<int>(b), static_cast<int>(a) + 1);
}

TEST_F(OcProcess, AllocEventStartsAfterReservedRange)
{
  /* The first allocated event must be at/after OC_PROCESS_EVENT_MAX so it
     never collides with a built-in event id. */
  oc_process_event_t a = oc_process_alloc_event();
  EXPECT_GE(static_cast<int>(a), OC_PROCESS_EVENT_MAX);
}

TEST_F(OcProcess, ExitStopsRunningProcess)
{
  oc_process_start(&test_proc, nullptr);
  EXPECT_TRUE(oc_process_is_running(&test_proc));
  oc_process_exit(&test_proc);
  EXPECT_FALSE(oc_process_is_running(&test_proc));
}

TEST_F(OcProcess, BroadcastEventReachesRunningProcess)
{
  oc_process_start(&test_proc, nullptr);
  oc_process_post(OC_PROCESS_BROADCAST, OC_PROCESS_EVENT_MSG, nullptr);
  oc_process_run();
  EXPECT_EQ(g_msg_count, 1);
}
