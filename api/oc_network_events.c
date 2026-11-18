/*
 * Copyright (c) 2016, 2018 Intel Corporation
 * Copyright (c) 2024-2026 KNX Association
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "oc_network_events.h"
#include "oc_buffer.h"
#include "oc_events.h"
#include "oc_signal_event_loop.h"
#include "port/oc_connectivity.h"
#include "util/oc_list.h"

OC_LIST(network_events);
static bool interface_up, interface_down, endpoints_changed;

/*
  Poll handler of oc_network_events (main thread).

  network thread:
    IP address change                 (netlink handler / oc_network_refresh_endpoints)
    -> dirty                          (mark_endpoints_dirty)
    -> endpoints_changed + poll       (oc_network_endpoints_changed)
    -> interface_up/down + poll       (oc_network_interface_event)

  main thread:
    [set mutex] messages              (oc_receive_message)
                post UP/DOWN          (oc_process_post)
    [clr mutex] rebuild list          (oc_connectivity_get_endpoints)
    -> UP/DOWN callback (mDNS)        (handle_network_interface_event_callback -> knx_dns_sd_update_service)

  Only the main thread frees the endpoint list.
*/
static void oc_process_network_event(void)
{
  oc_network_event_handler_mutex_lock();
  bool rebuild = endpoints_changed;
  endpoints_changed = false;
  oc_message_t* message = (oc_message_t*)oc_list_pop(network_events);

  while (message != NULL)
  {
    oc_receive_message(message);
    message = (oc_message_t*)oc_list_pop(network_events);
  }

  /*
    an interface event stays pending until it is queued (as the etimer does for its timer event)
    - OC_PROCESS_ERR_FULL: the flag is kept and the process polled again, the next run tries once more
  */
  if (interface_up)
  {
    interface_up = oc_process_post(&oc_network_events, oc_events[INTERFACE_UP], NULL) != OC_PROCESS_ERR_OK;
  }
  if (interface_down)
  {
    interface_down = oc_process_post(&oc_network_events, oc_events[INTERFACE_DOWN], NULL) != OC_PROCESS_ERR_OK;
  }
  /*
    still pending (post rejected) -> poll the own process again
    
    - this poll handler only runs when the process is polled, normally by the network thread (next packet / interface change)
    - without own poll below the pending event would wait for the next network activity, maybe forever
    
    -> the next oc_process_run() calls this handler again and retries the post, once a queue slot is free it is queued
       and the flag is reset (no further poll)
  */
  if (interface_up || interface_down)
  {
    oc_process_poll(&oc_network_events);
  }
  oc_network_event_handler_mutex_unlock();

  /*
    rebuild after unlock 
    -> oc_connectivity_get_endpoints() locks oc_network_event_handler_mutex itself to rebuild the list 
    -> the mutex is not recursive (Linux: default pthread mutex), calling it while holding the mutex would deadlock
       the posted INTERFACE_UP/DOWN events run after this handler, so their callbacks (mDNS) see the rebuilt list
  */
  if (rebuild)
  {
    oc_connectivity_get_endpoints();
  }
}

OC_PROCESS(oc_network_events, "");
OC_PROCESS_THREAD(oc_network_events, ev, data)
{
  (void)data;
  OC_PROCESS_POLLHANDLER(oc_process_network_event());
  OC_PROCESS_BEGIN();
    while (oc_process_is_running(&(oc_network_events)))
    {
      OC_PROCESS_YIELD();
      if (ev == oc_events[INTERFACE_DOWN])
      {
        handle_network_interface_event_callback(NETWORK_INTERFACE_DOWN);
      }
      else if (ev == oc_events[INTERFACE_UP])
      {
        handle_network_interface_event_callback(NETWORK_INTERFACE_UP);
      }
    }
  OC_PROCESS_END();
}

void oc_network_event(oc_message_t* message)
{
  if (!oc_process_is_running(&oc_network_events))
  {
    oc_message_unref(message);
    return;
  }
  oc_network_event_handler_mutex_lock();
  oc_list_add(network_events, message);
  oc_network_event_handler_mutex_unlock();

  oc_process_poll(&(oc_network_events));
  _oc_signal_event_loop();
}

void oc_network_interface_event(oc_interface_event_t event)
{
  if (!oc_process_is_running(&oc_network_events) || (event != NETWORK_INTERFACE_DOWN && event != NETWORK_INTERFACE_UP))
  {
    return;
  }

  oc_network_event_handler_mutex_lock();
  if (event == NETWORK_INTERFACE_DOWN)
  {
    interface_down = true;
  }
  else
  {
    interface_up = true;
  }
  oc_network_event_handler_mutex_unlock();

  oc_process_poll(&oc_network_events);
  _oc_signal_event_loop();
}

void oc_network_endpoints_changed(void)
{
  if (!oc_process_is_running(&oc_network_events))
  {
    return;
  }

  oc_network_event_handler_mutex_lock();
  endpoints_changed = true;
  oc_network_event_handler_mutex_unlock();

  oc_process_poll(&oc_network_events);
  _oc_signal_event_loop();
}
