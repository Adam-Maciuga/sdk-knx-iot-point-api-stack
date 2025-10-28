/*
 // Copyright (c) 2016 Intel Corporation
 // Copyright (c) 2021 Cascoda Ltd.

 //
 // Licensed under the Apache License, Version 2.0 (the "License");
 // you may not use this file except in compliance with the License.
 // You may obtain a copy of the License at
 //
 //      http://www.apache.org/licenses/LICENSE-2.0
 //
 // Unless required by applicable law or agreed to in writing, software
 // distributed under the License is distributed on an "AS IS" BASIS,
 // WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 // See the License for the specific language governing permissions and
 // limitations under the License.
 */
/**
*
  @brief the various events for the different quues
  @file
*/
#ifndef OC_EVENTS_H
#define OC_EVENTS_H

#include "oc_config.h"
#include "util/oc_process.h"

#ifdef __cplusplus
extern "C" {
#endif

  /*
    -> Network
    -> INBOUND_NETWORK_EVENT
       -> INBOUND_OSCORE_EVENT
       -> INBOUND_RI_EVENT

    -> OUTBOUND_UC_OSCORE_EVENT -> OUTBOUND_NETWORK_EVENT_ENCRYPTED -> Network
    -> OUTBOUND_MC_OSCORE_EVENT -> OUTBOUND_NETWORK_EVENT_ENCRYPTED -> Network

  */

typedef enum {
  INBOUND_NETWORK_EVENT, // inbound network event, ANY message
  UDP_TO_TLS_EVENT,
  INIT_TLS_CONN_EVENT,
  RI_TO_TLS_EVENT,
  INBOUND_RI_EVENT,                 // inbound network event, payload IS NOT encrypted (was already plain or was decrypted by OSCORE layer)
  OUTBOUND_NETWORK_EVENT,           // outbound network event, payload IS NOT (yet) encrypted
  OUTBOUND_NETWORK_EVENT_ENCRYPTED, // outbound network event, payload IS encrypted, received from OSCORE layer
  TLS_READ_DECRYPTED_DATA,
  TLS_WRITE_APPLICATION_DATA,
  INTERFACE_DOWN, /**< network interface down*/
  INTERFACE_UP,   /**< network interface up */
  TLS_CLOSE_ALL_SESSIONS,
  INBOUND_OSCORE_EVENT,     // inbound network event, payload IS encrypted with OSCORE
  OUTBOUND_UC_OSCORE_EVENT, // outbound unicast network event, payload WILL BE encrypted with OSCORE
  OUTBOUND_MC_OSCORE_EVENT, // outbound multicast network event, payload WILL BE encrypted with OSCORE
  NUM_OC_EVENT_TYPES
} oc_events_t;

extern oc_process_event_t oc_events[];

#ifdef __cplusplus
}
#endif

#endif 
