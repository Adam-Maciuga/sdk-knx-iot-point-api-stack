/*
// Copyright (c) 2021 Cascoda Ltd
// Copyright (c) 2024-2025 KNX Association
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
  @brief implementation of /.well-known/knx and /k resources
  @file
*/

#ifndef OC_KNX_INTERNAL_H
#define OC_KNX_INTERNAL_H

#include <stddef.h>
#include "oc_api.h"

#ifdef __cplusplus
extern "C"
{
#endif

  // reset status
#define RESET_NO_ERROR (0)
#define RESET_UNSUPPORTED_ERASE_CODE (2)

// reset cmd
#define RESET_TO_DEFAULT_STATE (2)  // factory reset
#define RESET_TO_DEFAULT_WO_IA (7)  // factory reset w/o IA

  /**
   * @brief PASE Resource Object 
   *
   *  - A PAKE (Password Authenticated Key Exchange) 'protocol' is used to authenticate
   *    communication partners by establishing PASE (Password Authenticated Session Establishment) session keys
   *    between the parties. 
   *  - SPAKE2+ is an augmented/extended PAKE 'protocol', where only one party (usually a MaC) knows (and uses) the password.
   *    The other party (usually the server device) knows only a derivative of the password. 
   *
   *  The virtual demo applications uses the password also on server side, a real device shall use the password derivative,
   *  see SPAKE2+, 3.2. Offline Registration. 
   *
   *  The steps for the key enrolment are described in KNX IoT specification 3/10/5 clause 3.6.6.3
   *
   *  Key Translation
   *  ===============
   *
   *  | Json Key | Integer Value |  type       |
   *  | -------- | ------------- |-------------|
   *  | salt     | 5             | byte string |
   *  | shareP   | 10            | byte string |
   *  | shareV   | 11            | byte string |
   *  | pbkdf2   | 12            | map         |
   *  | confirmV | 13            | byte string |
   *  | confirmP | 14            | byte string |
   *  | rnd      | 15            | byte string |
   *  | it       | 16            | unsigned    |
   *
   *  (1)
   *  The iteration value (it) is a fixed value, that needs to be set in accordance to
   *  the server device hardware calculation capabilities.
   *  The stack allows to define this (it) value as part of the CMake compile definitions.
   *  Note that a randomized iteration value (it) at runtime does not increase the security,
   *  it only reduces the effort for an attacker.  
   *
   *  (2)
   *  The salt goes always together with an individual password.  
   *
   *  Example JSON
   *  ============
   * 
   *  { "rnd"    : x}
   *  { "shareP" : x}
   *  { "shareV" : x}
   *  { "ca"     : x}
   *  { "pbkdf2" : { "salt" : "xxxx", "it" : 5}}
   * 
   *
   * @note
   *  - no extra storage needed for map (12)
   *  - 
   */
  typedef struct oc_pase_t
  {
    oc_string_t id;       // recipient id 
    uint8_t salt[32];     // salt 
    uint8_t shareP[65];   // pa from RFC 9382 in Spake2+ = shareP   
    uint8_t shareV[65];   // pb from RFC 9382 in Spake2+ = shareV
    uint8_t confirmP[32]; // ca from RFC 9382 in Spake2+ = confirmP
    uint8_t confirmV[32]; // cb from RFC 9382 in Spake2+ = confirmV
    uint8_t rnd[32];      // random
    uint32_t it;          // iterations (see hints above) 
  } oc_pase_t;

  /**
   * @brief Group Object Notification (s-mode messages)
   * Can be used for receiving messages or sending messages.
   *
   *  generic structures:
   * ```
   *  { 4: "sia", 5: { 6: "st", 7: "ga", 1: "value" } }
   * ```
   *
   * Key translation
   * | Json Key | Integer Value | type     |
   * | -------- | ------------- |----------|
   * | value    | 1             | object*  |
   * | sia      | 4             | uint32_t |
   * | s        | 5             | object** |
   * | st       | 6             | string   |
   * | ga       | 7             | uint32_t |
   *
   * *  may be not present (GET = OK, PUT = NOT OK), handed then over as default NULL to the AL callback handlers
   *
   * ** not modelled below
   */
  typedef struct oc_group_object_notification
  {
    oc_rep_t* value_object; // pointer to CBOR value object, see notes above
    uint32_t sia;           // source individual address
    oc_string_t st;         // service type code (write=w, read=r, response=a)
    uint32_t ga;            // group address
  } oc_group_object_notification_t;

  /**
   * @brief LSM state values
   *
   */
  typedef enum oc_lsm_state 
  {
    LSM_S_UNLOADED = 0,       // (0) unloaded, e.g. ready for loading, (m) 
    LSM_S_LOADED = 1,         // (1) loaded, e.g. normal operation, (m) 
    LSM_S_LOADING = 2,        // (2) loading, (m) 
    LSM_S_UNLOADING = 4,      // (4) unloading, (o) 
    LSM_S_LOADCOMPLETING = 5, // (5) load completing, (o) 
    LSM_S_ERROR = 6           // (6) error, not defined in KNX IoT specification, but useful to filter events
  } oc_lsm_state_t;

  /**
   * @brief LSM event values
   *
   */
  typedef enum oc_lsm_event
  {
    LSM_E_NOP = 0, /**< (0) no operation */
    LSM_E_STARTLOADING = 1, /**< (1) request to start the loading of the loadable part */
    LSM_E_LOADCOMPLETE = 2, /**< (2) cmd loading complete, state will be LOADED */
    LSM_E_UNLOAD = 4 /**< (4) cmd unload: state will be UNLOADED */
  } oc_lsm_event_t;

  /**
   * @brief retrieve the current LSM state
   *
   * @return the LSM state
   */
  oc_lsm_state_t oc_knx_get_lsm(void);

  /**
   * @brief sets the current LSM state and writes it to storage
   *
   * @param new_state the new LSM
   * @return 0 == success
   */
void oc_knx_set_and_store_lsm(oc_lsm_state_t new_state);

  /**
   * @brief convert the load state machine (lsm) event to string
   *
   * @param lsm_e the event
   *
   * @note used only for debug and test
   *
   * @return const char* The state as string
   */
  const char* oc_core_get_lsm_event_as_string(oc_lsm_event_t lsm_e);

  /**
   * @brief convert the load state machine (lsm) state to string
   *
   * @param lsm_s the state
   *
   * @note used only for debug and test
   *
   * @return const char* The state as string
   */
  const char* oc_core_get_lsm_state_as_string(oc_lsm_state_t lsm_s);

  /**
   * Callback invoked by the stack to inform the change of the lsm
   *
   * @param[out] lsm_state the new state of the lsm
   * @param[in] data the user supplied data
   *
   */
  typedef void (*oc_lsm_change_cb_t)(oc_lsm_state_t lsm_state, void* data);

  /**
   * Set the load state machine change callback.
   *
   * The callback is called by the stack when lsm is changed
   *
   * @note oc_set_hostname_cb() must be called before oc_main_init().
   *
   * @param[in] cb oc_hostname_cb_t function pointer to be called
   * @param[in] data context pointer that is passed to the oc_restart_cb_t
   *                 the pointer must be a valid pointer till after oc_main_init()
   *                 call completes.
   */
  void oc_set_lsm_change_cb(oc_lsm_change_cb_t cb, void* data);

  /**
   * @brief checks if the device is in "runtime" mode, which is:
   * - iid initialized (e.g. larger than 0)
   * - load state machine (lsm) == loaded
   *
   * @note devices from manufacturing will not work out of the box, only
   * if a MaC was setting the iid to a value > 0
   *
   * @return true in runtime
   * @return false not in run time
   */
  bool oc_is_device_in_runtime(void);

  /**
   * @brief sets the idevid
   *
   * @param idevid the idevid certificate
   * @param length the length of the certificate
   */
  void oc_knx_set_idevid(const char* idevid, int length);

  /**
   * @brief sets the ldevid
   *
   * @param ldevid the ldevid certificate
   * @param len the length of the certificate
   */
  void oc_knx_set_ldevid(char* ldevid, int len);

  /**
   * @brief increase the fingerprint value and writes the value to storage (file system)
   *
   * @note updated on LSM to loaded and on write to /p (if property is set)
   *
   * @see oc_resource_set_write_access_affects_fingerprint 
   *
   */
  void oc_knx_increase_fingerprint(void);

  /**
   * @brief load the fingerprint value from storage (file system)
   *
   */
  void oc_knx_load_fingerprint(void);

  /**
   * @brief Initialise the RNG used for SPAKE2+ and global data structures
   * @return int -1 error, 0 success
   *
   */
  int oc_initialise_spake_data(void);

#ifdef __cplusplus
}
#endif

#endif
