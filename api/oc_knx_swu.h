/*
// Copyright (c) 2021-2022 Cascoda Ltd
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
  @brief software update
  @file
*/

#ifndef OC_KNX_SWU_INTERNAL_H
#define OC_KNX_SWU_INTERNAL_H

#include "oc_core_res.h"

#ifdef __cplusplus
extern "C" {
#endif

  typedef enum oc_swu_method
  {
    PULL = 0,
    PUSH = 1,     // only PUSH mode is used in this stack
    BOTH = 2      // PULL + PUSH 
  } oc_swu_method_t;

  typedef enum oc_swu_protocol
  {
    CoAP = 0,     // unicast CoAP + OSCORE (RFC 7252) and block-wise transfer 
    CoAPS = 1,    // as defined in RFC 7252 with optional support for block-wise transfer
    CoAP_TCP = 2, // CoAP + OSCORE over TCP as defined in RFC 8323 
    CoAP_TLS = 3, // CoAP + TLS as defined in RFC 8323
    Vendor = 254  // manufacturer specific
  } oc_swu_protocol_t;

  /**
  * @brief The software update states
  *
  */
  typedef enum
  {
    OC_SWU_STATE_IDLE = 0,    // idle (no FWU package is on the way)
    OC_SWU_STATE_DOWNLOADING, // downloading (a FWU package is currently on the way)
    OC_SWU_STATE_DOWNLOADED,  // downloaded (the FWU package is fully available at server)
    OC_SWU_STATE_UPGRADING    // upgrading (the FWU package is currently applied to the server)
  } oc_swu_state_t;

  /**
  * @brief The software result states
  *
  */
  typedef enum
  {
    OC_SWU_RESULT_INIT = 0,  /**< 0 Initial value. Once the updating process is initiated (Download/Update), this Resource MUST be reset to Initial value. */
    OC_SWU_RESULT_SUCCESS,   /**< 1 Software updated successfully.*/
    OC_SWU_RESULT_ERR_FLASH, /**< 2 Not enough flash memory for the new software package.*/
    OC_SWU_RESULT_ERR_RAM,   /**< 3 Out of RAM during downloading process*/
    OC_SWU_RESULT_ERR_CONN,  /**< 4 Connection lost during downloading process.*/
    OC_SWU_RESULT_ERR_ICF,   /**< 5 Integrity check failure for new downloaded package.*/
    OC_SWU_RESULT_ERR_UPT,   /**< 6 Unsupported package type.*/
    OC_SWU_RESULT_ERR_URL,   /**< 7 Invalid URL.*/
    OC_SWU_RESULT_ERR_SUF,   /**< 8 Software update failed.*/
    OC_SWU_RESULT_ERR_UP,    /**< 9 Unsupported protocol. */
  } oc_swu_result_t;


  /**
  * @brief device swu information
  */
  typedef struct oc_device_swu
  {
    int max_defer;            // maximum number of seconds an AUTOMATIC software update can be deferred, 0 = NO AUTOMATIC update possible
    int current_defer;        // current number of seconds a software update will be deferred (0...max defer)
    int update_method;        // swu update method (0=pull, 1=push=default or 2=both)
    oc_string_t pkg_name;
    oc_string_t last_update;
    int pkg_bytes;
    oc_knx_version_info_t pkg_version;
    oc_swu_state_t state;
    oc_string_t query_url;
    oc_swu_result_t result;   // download result
    bool downloaded_once;     // marker for a never updated device
    int protocol;             // only 0=unicast CoAP supported
  } oc_device_swu_t;

  /**
   * @brief Creation of the KNX software update resources.
   *
   * @param device index of the device to which the resources are to be created
   */
  void oc_create_knx_swu_resources(size_t device);

  /**
   * @brief set the current firmware package name
   *
   * @param name the name of the firmware package
   */
  void oc_swu_set_package_name(const char* name);

  /**
   * @brief set the current last update time
   *
   * @param time the update time in IETF RFC 3339
   */
  void oc_swu_set_last_update(const char* time);

  /**
   * @brief set the current amount of the bytes written
   *
   * @param package_bytes the amount of bytes written
   */
  void oc_swu_set_package_bytes(int package_bytes);

  /**
   * @brief Sets the current package version
   *
   * @param major the major number e.g. 1 of [1, 2, 3]
   * @param minor the minor number e.g. 2 of [1, 2, 3]
   * @param patch the patch number e.g. 3 of [1, 2, 3]
   */
  void oc_swu_set_package_version(int major, int minor, int patch);

  /**
   * @brief sets the current download state
   *
   * @param state the download state
   */
  void oc_swu_set_state(oc_swu_state_t state);

  /**
   * @brief sets the url to be queried for downloading
   *
   * @param url the url
   */
  void oc_swu_set_query_url(const char* url);

  /**
   * @brief sets the result of the download procedure
   *
   * @param result the result, including possible errors
   */
  void oc_swu_set_result(oc_swu_result_t result);

#ifdef __cplusplus
}
#endif

#endif
