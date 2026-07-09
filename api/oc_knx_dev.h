/*
 * Copyright (c) 2022-2023 Cascoda Ltd
 * Copyright (c) 2024-2026 KNX Association
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OC_KNX_DEV_INTERNAL_H
#define OC_KNX_DEV_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#define KNX_STORAGE_IA        "dev_knx_ia"
#define KNX_STORAGE_IID       "dev_knx_iid"
#define KNX_STORAGE_FID       "dev_knx_fid"
#define KNX_STORAGE_HOSTNAME  "dev_knx_hostname"
#define FINGERPRINT_STORE     "dev_knx_fingerprint"
#define KNX_STORAGE_LSM       "dev_knx_lsm"
#define KNX_STORAGE_AP_VER    "knx_ap_version"
#define KNX_STORAGE_FW_VER    "knx_fw_version"

#define OSC_STORAGE_OSN_DELAY "oscore_osn_delay"

/**
@brief load the device from storage (file system)
 *  
 *  - hname (host name)
 *  - ia (individual address)
 *  - iid (installation id)
 *  - fid (fabric id)
 *  - ap (application version)
 *  - fwv (firmware version)
 *  - lsm (load state)
 *  - oscore osn delay
 *
 *  @note if storage cannot be read, their default values will be applied to the properties
 *
 */
void oc_knx_load_device(void);

/**
 * @brief reset the device by: 
 *        - clear mDNS (goodbye with current values)
 *        - set device default values in the persistent 
 *          device storage (according to the reset_mode)
 *        - terminate pase sessions, unregister mc groups, 
 *        - clear request (repeat) / response (replay) buffers
 *        - reannounce mDNS with the new values
 *
 *   The factory preset/reset callback handler is NOT called in this method, it is called 
*    only on an inbound message together with this method.
 * 
 * @note  reset behavior according to the supplied reset_mode
 * - 2 (Factory Reset) :
 *   - host name (hname)
 *   - Installation ID (iid)
 *   - programming mode (pm)
 *   - device address (da)
 *   - sub address (sa)
 *   - individual address (ia)
 *   - load state machine 
 *   - group object / recipient / publisher object table
 *   - access token table
 * - 7 (Factory Reset without IA):
 *    - load state machine 
 *    - group object / recipient / publisher object table
 *    - access token table (except entries with 'if.sec')
 *
 * @param reset_mode the KNX reset mode
 */
void oc_knx_device_storage_reset(int reset_mode);

/**
 * @brief function checks if the device is in programming mode
 *
 * @return true in programming mode
 * @return false not in programming mode
 */
bool oc_knx_device_in_programming_mode(void);

/**
 * @brief function set the programming mode of the device to true or false and updates the 
 *        DNS-SD service advertisement with new programming mode (and iid/ia) information
 *
 * @param programming_mode true to set the device in programming mode, false otherwise
 */
void oc_knx_device_set_programming_mode(bool programming_mode);

/**
 * @brief Restart the KNX device
 *
 * @note Performs the KNX restart operation:
 * - clear mDNS (goodbye with current values)
 * - resets programming mode to false
 * - terminates PASE token
 * - applies configuration parameters
 * - reannounce mDNS with the new values
 *
 */
void oc_knx_device_restart(void);

#ifdef __cplusplus
}
#endif

#endif 
