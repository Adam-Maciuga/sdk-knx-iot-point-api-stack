/*
// Copyright (c) 2022-2023 Cascoda Ltd
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
  @brief client code for the device (s-mode)
  @file

  compile flag:
  - OC_USE_MULTICAST_SCOPE_2
    also sends the multicast group events with scope =2
    this is needed when the devices are running on the same PC
*/
#ifndef OC_KNX_CLIENT_INTERNAL_H
#define OC_KNX_CLIENT_INTERNAL_H

#include <stddef.h>
#include "oc_core_res.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*oc_s_mode_response_cb_t)(char *url, oc_rep_t *rep, oc_rep_t *rep_value);

/**
  @defgroup doc_module_tag_s_mode_server s-mode server
  S-mode server side support functions.

  This module contains the receiving side of the s-mode functionality.
  The received s-mode messages are routed to the appropriate POST methods of the
  data point. However, since not all data is in the s-mode message the POST
  method needs to retrieve the data from the s-mode message differently than
  for a normal CoAP post message (the message payload is constructed
  differently).

  @{
*/

/**
 * @brief set the s-mode response callback
 * e.g. function is called when a s-mode response is coming back
 *
 * @param my_func the callback function
 * @return true function set
 * @return false function set failed
 */
bool oc_set_s_mode_response_cb(oc_s_mode_response_cb_t my_func);

/**
 * @brief retrieve the callback function
 *
 * @return oc_s_mode_response_cb_t the callback function that has been set
 */
oc_s_mode_response_cb_t oc_get_s_mode_response_cb(void);

/**
 * @brief  checks if the request is a redirected request from /k or /p,
 *         when that happened, extra information can be in the CBOR object.
 *
 * @note   an endpoint allows a stack 'redirect' call such as:
 *         - from s-mode /k with { 4: <IA>, 5: { 6: w, 7: 1234, 1: true } }
 *         - from CoAP /p with { 1: true, 'min': 50, ... },
 *           here extra CBOR data may be applied to the value  
 * 
 *
 * @param request the request to be checked
 * @return 1, call came from /p 
 * @return 0, call came from /k
 * @return -1, call came from anything else or request was NULL
 */
int oc_is_redirected_request_from(const oc_request_t *request);

/**
  @defgroup doc_module_tag_s_mode_client s-mode client
  S-mode Client side support functions.

  This module contains the sending side of the s-mode functionality.
  The s-mode messages are send from the device that implements a resource with
  the CoAP GET functionality. The s-mode functions will retrieve the data values
  and place it in the s-mode message. The s-mode message will only be send to
  the groups that are listed in the Group Object Table with the appropriate
  flags.

  @{
*/

/**
 * @brief parses out the value OBJECT of the s-mode request.
 *
 * @param request the request
 * @return oc_rep_t* the rep object
 */
oc_rep_t *oc_s_mode_get_value_object(oc_request_t *request);

/** @} */ // end of doc_module_tag_s_mode_server

/**
 * @brief sends (transmits) an s-mode message
 *
 * - the value comes from the GET of the resource indicated by the resource_url
 * - the path is "k"
 * - the sia (sender individual address) is taken from the device
 * - the ga is coming from the group address table that is listing the resource
 * - the url is the url of the resource to obtain the value from
 *
 *
 * Only the first group address is used to send the s-mode message.
 * For the recipient table all entries are used to send the unicast
 * communication.
 *
 * @note Usually the function does not check the T flag on the resource
 *       (e.g. always send the s-mode message) in case of srv_type
 *       value = "a" when sending a read response on a previous read request ("r")
 *
 * @param scope the multi-cast scope
 * @param resource_path caller resource URL (e.g. implemented on the device that is calling this function)
 * @param srv_type the "st" value to send e.g. "w" | "a" | "r"
 * @param consider_transmission_flag
 *        - false: does not check the transmit flag
 *        - true: checks transmit flag
 */
 void oc_do_s_mode_with_scope_and_check(const int scope, const char* resource_path, char* srv_type,
                                        bool consider_transmission_flag);

 void oc_issue_s_mode(int ipv6_adr_scope, uint16_t sia_value, const uint32_t grpid, const uint32_t group_address,
                      const uint64_t iid, char* mode, uint8_t* value_data, const int value_size);

/** @} */ // end of doc_module_tag_s_mode_client

#ifdef __cplusplus
}
#endif

#endif 
