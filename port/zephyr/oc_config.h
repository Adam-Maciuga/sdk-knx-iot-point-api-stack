/* 
 * Copyright (c) 2016 Intel Corporation
 * Copyright (c) 2021 Cascoda Ltd
 * Copyright (c) 2026 NXP
 * Copyright (c) 2025-2026 KNX Association
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OC_CONFIG_H
#define OC_CONFIG_H

#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Time Resolution */
typedef uint64_t oc_clock_time_t;
/* Note:
 * Clock tick rate must match oc_clock_time() which returns k_uptime_get() (milliseconds).
 * CONFIG_SYS_CLOCK_TICKS_PER_SEC is 1000 on ESP32-C6, giving 1 tick = 1 ms — correct.
 * Do NOT use CLOCKS_PER_SEC here: picolibc defines it as 1 000 000, which would make
 * all CoAP retransmit timers (e.g. COAP_RESPONSE_TIMEOUT_TICKS = 5 * 1 000 000 ticks)
 * expire after ~83 minutes instead of 5 seconds.
 */
#define OC_CLOCK_CONF_TICKS_PER_SECOND CONFIG_SYS_CLOCK_TICKS_PER_SEC

/* Security Layer */
// Max inactivity timeout before tearing down DTLS connection.
#define OC_DTLS_INACTIVITY_TIMEOUT (600)

// Maximum wait time for select function.
// TODO verify added by NXP
#define SELECT_TIMEOUT_SEC (1)

// Add support for passing TCP/TLS/DTLS session connection events to the
// application.
#define OC_SESSION_EVENTS
// Add request history for deduplicate UDP/DTLS messages.
#define OC_REQUEST_HISTORY

// TODO verify added by NXP
// Add support for dns lookup to the endpoint.
#define OC_DNS_LOOKUP
#define OC_DNS_CACHE

// Dynamic memory allocation.
#define OC_BLOCK_WISE

// The maximum size of a response to an OBSERVE request, in bytes.
#define OC_MAX_OBSERVE_SIZE 512

// Maximum number of interfaces for IP adapter.
#define OC_MAX_IP_INTERFACES (3)

// Maximum number of callbacks for Network interface event monitoring.
#define OC_MAX_NETWORK_INTERFACE_CBS (4)

// Maximum number of callbacks for connection of session.
#define OC_MAX_SESSION_EVENT_CBS (2)

#ifdef __cplusplus
}
#endif

#endif 
