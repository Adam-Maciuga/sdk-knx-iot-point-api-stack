/*
// Copyright (c) 2022 Cascoda Ltd.
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

#ifndef DNS_SD_H
#define DNS_SD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "../api/oc_knx_fp.h"  // for oc_ip_status_t

/**
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
int knx_publish_service(char *serial_no, uint64_t iid, uint16_t ia, bool pm);

 
/**
 * @brief Set the advertised sleep period within the mDNS service.
 *
 * @param sp The period, in milliseconds. A value of 0 removes the
 * advertisement, signalling that the device is wakeful.
 */
void knx_service_sleep_period(int sp);

 /**
 * @brief Returns the device used unicast port.
 *
 * @note added as extra function allowing to adapt for
 *       different OS versions by not demanding to include the OS
 *       specific IP header file in the (OS) shared stack code.
 *
 */
uint16_t knx_get_used_port(void);

/**
 * @brief Stop the mDNS service: send goodbye, stop listener thread, close sockets.
 *
 * Call this during application shutdown (e.g. from oc_main_shutdown).
 */
void knx_stop_mdns(void);


#ifdef __cplusplus
}
#endif

#endif