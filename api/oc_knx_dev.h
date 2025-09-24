/*
// Copyright (c) 2021 Cascoda Ltd
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
  @brief knx /dev resource implementation
  @file
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
#define KNX_STORAGE_PM        "dev_knx_pm"
#define KNX_STORAGE_LSM       "dev_knx_lsm"
#define KNX_STORAGE_AP_MAJOR  "knx_ap_major"
#define KNX_STORAGE_AP_MINOR  "knx_ap_minor"
#define KNX_STORAGE_AP_PATCH  "knx_ap_patch"

/**
@brief load the device from storage (file system)
 *  
 *  - hname (host name)
 *  - ia (individual address)
 *  - pm (prg mode)
 *  - iid (installation id)
 *  - fid (fabric id)
 *  - ap (application version)
 *  - lsm (load state)

@param device index of the device to which the data is to be read
*/
void oc_knx_load_device();

/**
 * @brief clear the persistent storage
 * reset behavior according to the supplied erase code
 * - reset = 2 (Factory Reset) :
 *   - host name (hname)
 *   - Installation ID (iid)
 *   - programming mode (pm)
 *   - device address (da)
 *   - sub address (sa)
 *   - individual address (ia)
 *   - load state machine 
 *   - group object / recipient / publisher object table
 *   - access token table
 * - reset = 7 (Factory Reset without IA):
 *   - load state machine 
 *   - group object / recipient / publisher object table
 *   - access token table (except entries with 'if.sec')
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
bool oc_knx_device_in_programming_mode();

/**
 * @brief function set the programming mode of the device to true or false
 *
 * @param programming_mode true to set the device in programming mode, false
 * otherwise
 */
void oc_knx_device_set_programming_mode(bool programming_mode);

/**
 * @brief Restart the KNX device
 *
 * Performs the KNX restart operation:
 * - resets programming mode to false
 * - terminates PASE token
 * - applies configuration parameters
 * - calls application restart callback handler
 *
 */
void oc_knx_device_restart(void);


#ifdef __cplusplus
}
#endif

#endif 
