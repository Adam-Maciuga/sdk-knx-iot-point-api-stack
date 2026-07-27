/*
 * Copyright (c) 2019 Intel Corporation
 * Copyright (c) 2026 KNX Association
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OC_SESSION_EVENTS_INTERNAL_H
#define OC_SESSION_EVENTS_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief handle sessions on an endpoint
 *
 * @param endpoint the endpoint
 * @param state the session state
 */
void oc_handle_session(oc_endpoint_t *endpoint, oc_session_state_t state);

/**
 * @brief check if there is a session
 *
 * @return true
 * @return false
 */
bool oc_session_events_is_ongoing(void);

#ifdef __cplusplus
}
#endif

#endif /* OC_SESSION_EVENTS_INTERNAL_H */
