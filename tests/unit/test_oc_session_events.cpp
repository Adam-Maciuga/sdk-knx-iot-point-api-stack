/*
 * Copyright (c) 2024-2026 KNX Association
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for api/oc_session_events.c
 *
 * Scope note
 * ----------
 * In the unit-test build configuration OC_TCP is NOT defined
 * (port/linux/oc_config.h), so the entire `#ifdef OC_TCP` block of this file is
 * compiled out: free_session_state_delayed, oc_session_events_is_ongoing,
 * oc_session_events_set_event_delay, oc_process_session_event, the
 * OC_PROCESS_THREAD body and oc_session_start_event / oc_session_end_event are
 * not part of the binary and therefore cannot be unit-tested here. They are
 * documented as TCP-only / INTEGRATION in tests/COVERAGE_LEDGER.md.
 *
 * The only function compiled in is oc_handle_session. With KNX_TCP_TLS off the
 * TLS branch is excluded; OC_SERVER is on so a DISCONNECTED session removes any
 * CoAP observers for the endpoint (a no-op when no observers exist); and
 * OC_SESSION_EVENTS is on so a registered session callback (none in this binary)
 * would be invoked. These tests confirm both session states are handled safely
 * on a stack endpoint without a bootstrapped device.
 */

extern "C" {
#include "oc_endpoint.h"
#include "oc_session_events.h"
#include "api/oc_session_events_internal.h"
}

#include "gtest/gtest.h"
#include <cstring>

static oc_endpoint_t
make_endpoint(unsigned flags)
{
  oc_endpoint_t ep;
  memset(&ep, 0, sizeof(ep));
  ep.flags = (enum transport_flags)flags;
  return ep;
}

// CONNECTED skips the disconnect cleanup entirely; with no registered callback
// this is a pure no-op and must not crash.
TEST(SessionEvents, HandleSessionConnectedIsSafe)
{
  oc_endpoint_t ep = make_endpoint(IPV6);
  oc_handle_session(&ep, OC_SESSION_CONNECTED);
  SUCCEED();
}

// DISCONNECTED removes observers for the endpoint; with no observers present
// this is a no-op and must not crash.
TEST(SessionEvents, HandleSessionDisconnectedNoObserversIsSafe)
{
  oc_endpoint_t ep = make_endpoint(IPV6);
  oc_handle_session(&ep, OC_SESSION_DISCONNECTED);
  SUCCEED();
}

// SECURED|TCP flagged endpoint on DISCONNECTED: the TLS removal branch is
// KNX_TCP_TLS-gated (off here), so this still resolves to observer removal only.
TEST(SessionEvents, HandleSessionDisconnectedSecuredTcpIsSafe)
{
  oc_endpoint_t ep = make_endpoint(SECURED | TCP);
  oc_handle_session(&ep, OC_SESSION_DISCONNECTED);
  SUCCEED();
}
