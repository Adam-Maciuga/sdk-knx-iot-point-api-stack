/*
 * Copyright (c) 2016 Intel Corporation
 * Copyright (c) 2022 Cascoda Ltd
 * Copyright (c) 2024-2026 KNX Association
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stdio.h>
#include "oc_config.h"
#include "port/oc_assert.h"
#include "port/oc_connectivity.h"
#include "port/oc_network_interface.h"
#include "port/dns-sd.h"
#include "util/oc_etimer.h"
#include "util/oc_process.h"
#include "oc_api.h"
#include "oc_core_res.h"
#include "oc_knx.h"
#include "oc_knx_dev.h"
#include "oc_knx_fp.h"
#include "conf.h"
#include "oc_main.h"

#include <stdlib.h>
#ifdef KNX_TCP_TLS
static bool drop_commands = false;
#endif

// marker if init was done, to handle a shutdown without init
static bool initialized = false;  

static const oc_handler_t* app_callbacks;
static oc_factory_presets_t app_factory_presets = {.cb = NULL, .data = NULL};       
static oc_reset_t app_reset = {.cb = NULL, .data = NULL};                       
static oc_restart_t app_restart = {.cb = NULL, .data = NULL};
static oc_hostname_t app_hostname = {.cb = NULL, .data = NULL};
static oc_programming_mode_t app_programming_mode = {.cb = NULL, .data = NULL};
static oc_loadstate_t app_loadstate = {.cb = NULL, .data = NULL};
static oc_swu_t app_swu = {.cb = NULL, .data = NULL};
static oc_swu_upgrade_t g_swu_upgrade_handler = {.cb = NULL, .data = NULL};


void oc_set_swu_cb(const oc_swu_cb_t cb, void* data)
{
  app_swu.cb = cb;
  app_swu.data = data;
}

const oc_swu_t* oc_get_swu_cb(void) { return &app_swu; }

void oc_set_swu_upgrade_cb(oc_swu_upgrade_cb_t cb, void* data)
{
  g_swu_upgrade_handler.cb = cb;
  g_swu_upgrade_handler.data = data;
}

const oc_swu_upgrade_t* oc_get_swu_upgrade_cb(void) { return &g_swu_upgrade_handler; }

void oc_set_factory_presets_cb(oc_factory_presets_cb_t cb, void* data)
{
  app_factory_presets.cb = cb;
  app_factory_presets.data = data;
}

oc_factory_presets_t* oc_get_factory_presets_cb(void)
{
  return &app_factory_presets;
}

void oc_set_reset_cb(oc_reset_cb_t cb, void* data)
{
  app_reset.cb = cb;
  app_reset.data = data;
}

oc_reset_t* oc_get_reset_cb(void)
{
  return &app_reset;
}

void oc_set_restart_cb(oc_restart_cb_t cb, void* data)
{
  app_restart.cb = cb;
  app_restart.data = data;
}

const oc_restart_t* oc_get_restart_cb(void)
{
  return &app_restart;
}

void oc_set_hostname_cb(const oc_hostname_cb_t cb, void* data)
{
  app_hostname.cb = cb;
  app_hostname.data = data;
}

oc_hostname_t* oc_get_hostname_cb(void)
{
  return &app_hostname;
}

void oc_set_programming_mode_cb(oc_programming_mode_cb_t cb, void* data)
{
  app_programming_mode.cb = cb;
  app_programming_mode.data = data;
}

oc_programming_mode_t* oc_get_programming_mode_cb(void)
{
  return &app_programming_mode;
}

void oc_set_lsm_change_cb(oc_lsm_change_cb_t cb, void* data)
{
  app_loadstate.cb = cb;
  app_loadstate.data = data;
}

oc_loadstate_t* oc_get_lsm_change_cb(void)
{
  return &app_loadstate;
}

/* 
   runtime variables for PDU and BLOCK size, initialized with default values that relates to each other 
   (same as oc_set_pdu_size() would calculate)

   - PDU    -> static runtime variable (set/get)
   - BLOCK  -> static runtime variable (get)

   Note:

   2 * COAP_MAX_HEADER_SIZE -> see oc_set_pdu_size() header definition for explanation 
   
   the default block size is derived from KNX_PAYLOAD_SIZE and capped at 1024 (RFC 7959 maximum, SZX 6),
   so it stays consistent when KNX_PAYLOAD_SIZE is configured smaller than 1024

   RFC 7959 Section 2.2 only allows block sizes of '16 << SZX' with SZX 0..6, i.e. 16, 32, 64, 128, 256, 512 or 1024,
   any other value cannot be encoded in the 3 bit SZX field of the Block1/Block2 option. KNX_PAYLOAD_SIZE is not
   required to be a power of two (e.g. 900), hence it is floored to the largest legal block size that still fits,
   which is exactly what the SZX loop in oc_set_pdu_size() derives at runtime
*/

#define OC_FLOOR_BLOCK_SIZE(payload)   \
  (      (payload) >= 1024 ? 1024 :    \
         (payload) >= 512 ? 512   :    \
         (payload) >= 256 ? 256   :    \
         (payload) >= 128 ? 128   :    \
         (payload) >= 64 ? 64     :    \
         (payload) >= 32 ? 32     : 16 \
  )

static uint32_t oc_pdu_size = KNX_PAYLOAD_SIZE + 2 * COAP_MAX_HEADER_SIZE;  // holds current coap pdu size 
static uint16_t oc_blk_size = OC_FLOOR_BLOCK_SIZE(KNX_PAYLOAD_SIZE);        // holds current coap blockwise transfer block size

int oc_set_pdu_size(uint32_t pdu_size)
{
  // at least both CoAP header budgets (see note above) + 16 bytes (smallest CoAP block-wise block, i.e. SZX = 0 -> 16 << 0 = 16 bytes)
  if (pdu_size < 2 * COAP_MAX_HEADER_SIZE + 16)
  {
    return -1;
  }

  /*
    the buffer sizing is independent of block-wise transfer, hence the PDU size is always stored,
    only the derivation of the block size below requires OC_BLOCK_WISE
  */

  // the argument is the total PDU size, store it unchanged
  oc_pdu_size = pdu_size;

  // tmp variable for block size calculation, remove both CoAP header budgets to get the pure payload budget
  pdu_size -= 2 * COAP_MAX_HEADER_SIZE;

  #ifdef OC_BLOCK_WISE

  /*
    a block is a chunk of the application payload, hence it can never exceed the application data buffer
    (OC_MAX_APP_DATA_SIZE = KNX_PAYLOAD_SIZE), clamp the budget before deriving the size exponent
  */
  if (pdu_size > KNX_PAYLOAD_SIZE)
  {
    pdu_size = KNX_PAYLOAD_SIZE;
  }

  /* 
    derive block size SZX (RFC 7959 Section 2.2, Size Exponent) as largest power-of-2 that fits in the payload budget
    - range: 1024..16 bytes, SZX 6..0
    - 
  */
  uint16_t i;
  for (i = 10; i >= 4 && (pdu_size >> i) == 0; i--)
  {
  }

  oc_blk_size = (uint16_t)(1 << i);

  #else

  (void) pdu_size;

  #endif 

  return 0;
}

uint32_t oc_get_pdu_size (void)
{
  return oc_pdu_size;
}

uint32_t oc_get_max_app_data_size(void)
{
  return KNX_PAYLOAD_SIZE;
}

uint16_t oc_get_block_size(void) 
{
  return  oc_blk_size;
}

static void oc_shutdown_device(void)
{
  oc_connectivity_shutdown();
  oc_network_event_handler_mutex_destroy();
}

int oc_main_init(const oc_handler_t* handler)
{
  // prevent multiple init calls --> already done ...
  if (initialized)
  { 
    return 0;
  }

  /* 
     assert mandatory application data (callbacks, ...), all was set by 'initialize stack' 
     note: the getters return the address of a file-static struct and can therefore never be NULL
  */
  const bool mandatory_callbacks_defined = 
    oc_get_swu_cb()->cb &&          // put a/swu callback
    oc_get_swu_upgrade_cb()->cb &&  // put swu/update callback
    oc_get_restart_cb()->cb;        // post /.well-known/knx callback

  oc_assert(mandatory_callbacks_defined);                 

  // set application handlers
  app_callbacks = handler;

  oc_ri_init();
  oc_network_event_handler_mutex_init();

  // call one time on startup (must be successful)
  if (oc_spake2plus_init_data() < 0)
  {
    OC_ERR("Error in SPAKE2+ initialization, spake data init failed");

    oc_ri_shutdown();
    oc_shutdown_device();
    return -1;
  }

  // call one time on startup (must be successful)
  if (app_callbacks->init() < 0)
  {
    OC_ERR("Error in stack initialization, application init handler failed");

    oc_ri_shutdown();
    oc_shutdown_device();
    return -1;
    
  }

  #ifdef KNX_TCP_TLS
  drop_commands = false;

  ret = oc_tls_init_context();
  if (ret < 0)
  {
    oc_ri_shutdown();
    oc_shutdown_device();
    goto err;
  }
  #endif

  oc_knx_load_device();
  oc_knx_load_fingerprint();

  #ifdef KNX_TCP_TLS
  oc_sec_load_unique_ids(0);
  #ifdef OC_PKI
    OC_DBG("loading ECDSA keypair");
    oc_sec_load_ecdsa_keypair(0);
  #endif
  #endif

  #ifdef OC_SERVER
  // called one time on startup
  if (app_callbacks->register_resources)
  {
    app_callbacks->register_resources();
  }
  #endif 

  /* 
    NOTE: The post-init steps below (group-multicast registration, the
    read-on-init datapoint reads and the DNS-SD registration) assume the
    network is already up. On the Zephyr Wi-Fi reorder the stack initializes
    before Wi-Fi is associated, so they run with no link.
    - some may be (re)done by the NETWORK_INTERFACE_UP handler instead
    - read-on-init datapoint checks NETWORK_INTERFACE_UP for a decent period of time 
     
   */

  #ifdef OC_SERVER
  // listen to the group addresses multicasts that are registered in the PUB table
  oc_register_group_multicasts();
  #endif

  /* 
     Synchronously populate the endpoint list so the mDNS announcement
     can include AAAA records.  
     
     - oc_connectivity_init() only starts the network thread; the endpoints are 
       not yet enumerated at this point

     - dns-sd updates are scheduled here (stack init) but also on interface up events (oc_network_interface_up) 
       to handle the case where the network is not yet up at stack init time 
       (may lead to situation that 2 x dns-sd is triggered).
  */
  oc_network_refresh_endpoints();

  OC_INF("Reregister DNS-SD service after stack initialization");
  const oc_device_info_t* const  device = oc_core_get_device_info();
  knx_dns_sd_update_service(device->serialnumber, device->iid, device->ia, device->pm);

  #ifdef OC_CLIENT
  // called one time on startup after all network initialization
  if (app_callbacks->requests_entry)
  {
    app_callbacks->requests_entry();
  }

  // check and send on i-flags
  oc_init_datapoints_at_initialization();
  #endif

  // add as last step to avoid half init's 
  OC_DBG("Stack initialized ...");
  initialized = true;

  return 0;
}

oc_clock_time_t oc_main_poll(void)
{
  oc_clock_time_t ticks_until_next_event = oc_etimer_request_poll();
  while (oc_process_run())
  {
    ticks_until_next_event = oc_etimer_request_poll();
  }
  return ticks_until_next_event;
}

void oc_main_shutdown(void)
{
  // no shutdown if not already initialized
  if (!initialized)
    return;

  initialized = false;

  /*
     Stop the DNS-SD service advertisement before tearing down networking.
     On the mDNS transports (Wi-Fi, Linux, Windows) this sends the goodbye, stops
     the listener thread, and closes the socket. On Zephyr Thread the SRP teardown
     is done instead (currently a stub). 
   */
  knx_dns_sd_stop();

  // drop registered multicast groups 
  oc_unregister_group_multicasts();

  oc_ri_shutdown();

  #ifdef KNX_TCP_TLS
  oc_tls_shutdown();
  drop_commands = false;
  #endif 

  oc_shutdown_device();

  app_callbacks = NULL;
}

bool oc_main_initialized(void)
{
  return initialized;
}

void _oc_signal_event_loop(void)
{
  if (app_callbacks)
  {
    app_callbacks->signal_event_loop();
  }
}

#ifdef KNX_TCP_TLS
void oc_set_drop_commands(bool drop)
{
  drop_commands = drop;
}

bool oc_drop_command(void)
{
  return drop_commands;
}
#endif
