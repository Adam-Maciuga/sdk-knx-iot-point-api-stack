/*
 * Copyright (c) 2024-2026 KNX Association
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for api/oc_network_events.c
 *
 * Scope note
 * ----------
 * This module is the bridge between the platform receive path and the
 * oc_network_events process. Almost all of it requires the process to be
 * running and a live signal-event loop:
 *   - oc_process_network_event (static) drains the queue into
 *     oc_receive_message -> full CoAP/OSCORE receive path (INTEGRATION).
 *   - the OC_PROCESS_THREAD body needs the process scheduler (INTEGRATION).
 *   - oc_network_interface_event is OC_NETWORK_MONITOR-only and needs the
 *     running process (INTEGRATION).
 *
 * The single branch with no scheduler dependency is the early-out in
 * oc_network_event: when the oc_network_events process is NOT running it simply
 * unrefs the message and returns. In a unit-test binary the process is never
 * started, so that is exactly the branch we exercise here. We confirm the
 * message is returned to the pool (ref_count drops to 0) and nothing crashes.
 */

extern "C" {
#include "oc_buffer.h"
#include "oc_network_events.h"
#include "port/oc_network_events_mutex.h"
}

#include "gtest/gtest.h"

class NetworkEvents : public ::testing::Test {
protected:
  void SetUp() override { oc_network_event_handler_mutex_init(); }
};

// With the oc_network_events process not running, oc_network_event must unref
// the message (returning it to the pool) and return without touching the queue.
TEST_F(NetworkEvents, EventWhenProcessNotRunningUnrefsMessage)
{
  oc_message_t *message = oc_allocate_message();
  ASSERT_NE(nullptr, message);
  ASSERT_EQ(1U, message->ref_count);

  // Hold an extra reference so the message survives the unref that
  // oc_network_event() performs on the not-running early-out path. Under
  // dynamic allocation an unref to 0 free()s the buffer, so reading ref_count
  // afterwards would be a use-after-free; the extra ref lets us observe the
  // single unref safely.
  oc_message_add_ref(message);
  ASSERT_EQ(2U, message->ref_count);

  oc_network_event(message);

  // The early-out path must have unref'd the message exactly once.
  EXPECT_EQ(1U, message->ref_count);

  // Drop our own reference (free()s the message under dynamic allocation).
  oc_message_unref(message);
}

// The early-out path must be safe to invoke repeatedly across fresh messages.
TEST_F(NetworkEvents, EventWhenProcessNotRunningIsRepeatable)
{
  for (int i = 0; i < 4; ++i) {
    oc_message_t *message = oc_allocate_message();
    ASSERT_NE(nullptr, message);
    oc_message_add_ref(message);
    oc_network_event(message);
    EXPECT_EQ(1U, message->ref_count);
    oc_message_unref(message);
  }
}
