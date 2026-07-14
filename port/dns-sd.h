/*
 * Copyright (c) 2022 Cascoda Ltd.
 * Copyright (c) 2026 KNX Association
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DNS_SD_H
#define DNS_SD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "../api/oc_knx_fp.h"  // for oc_ip_status_t

/**
   TODO 13 FIXME Review mDNS vs DNS-SD terminology in the API and docs.
   @brief Publish a KNX mDNS service in order to enable DNS-SD discovery on server side.
  
   @param serial_no KNX serial number
   @param iid KNX Installation ID, set to 0 if the device has not been commissioned yet
   @param ia KNX Individual Address, set to 0 if the device has not been commissioned yet
   @param pm True if the device is in Programming Mode, false otherwise

   @return int 0 on success, -1 on error
  
   @note
   DNS-SD <domain>
      - .local
   DNS-SD <service>
      - _knx._udp 
   DNS-SD <sub service>
      - _{serialnumber}             ->  _00fa10020800._sub  (ascii hex, lower case)
      - _ia{installation-id}-{ia}   -> _ia33a3-20a._sub     (ascii hex, lower case)
      - _pm                         -> _pm._sub
    1. Get all knx services         -> IN <service>.<domain> -> OUT <instance>.<service>.<domain>
    2. Get specific knx service     -> IN <service>.<domain> -> OUT <instance>.<service>.<domain>

   
 */
int knx_dns_sd_update_service(const char *serial_no, uint64_t iid, uint16_t ia, bool pm);

 
/**
 * @brief Set the advertised sleep period within the mDNS service.
 *
 * @param sp The period, in milliseconds. A value of 0 removes the
 *           advertisement, signalling that the device is wakeful.
 */
void knx_dns_sd_set_sleep_period(uint16_t sp);

 /**
 * @brief Returns the device used unicast port.
 *
 * @note added as extra function allowing to adapt for
 *       different OS versions by not demanding to include the OS
 *       specific IP header file in the (OS) shared stack code.
 *
 */
uint16_t knx_dns_sd_get_used_port(void);

/** Maximum serial-number string length (12 lower-case hex chars, excluding null terminator). */
#define SN_STR_LEN_MAX (12)

/**
 * @brief Snapshot of the current mDNS advertisement state.
 *
 * Returned by value from knx_dns_sd_get_current_record(). When valid is false
 * the remaining fields are zero/empty and no mDNS service is being announced.
 */
typedef struct mdns_knx_record_t
{
  bool valid; /**< true = mDNS is currently announced */
  struct knx
  {
    uint64_t iid;                      /**< KNX installation ID */
    uint16_t ia;                       /**< KNX individual address */
    char sn[SN_STR_LEN_MAX + 1];      /**< lower-case hex serial number, null-terminated */
    bool pm;                           /**< programming mode */
    uint16_t sp;                       /**< sleep period in seconds (0 = wakeful) */
  } knx;
} mdns_knx_record_t;

/**
 * @brief Return a by-value snapshot of the current mDNS advertisement record.
 *
 * Safe to call at any time. When the returned record's valid field is false,
 * no mDNS service is currently being announced and all other fields are zero.
 *
 * IP addresses and port are not included; obtain them from
 * oc_connectivity_get_endpoints() and knx_dns_sd_get_used_port().
 */
mdns_knx_record_t knx_dns_sd_get_current_record(void);

/**
 * @brief Stop the device's DNS-SD service advertisement during shutdown.
 *
 * On the mDNS transports (Wi-Fi on Zephyr, Linux, Windows) this sends the mDNS
 * goodbye (TTL 0), stops the listener thread, and closes the multicast socket,
 * fully tearing down the service. On Zephyr Thread (SRP) it is currently a stub.
 *
 * Call this from oc_main_shutdown before tearing down the network stack.
 */
void knx_dns_sd_stop(void);

#ifdef __cplusplus
}
#endif

#endif