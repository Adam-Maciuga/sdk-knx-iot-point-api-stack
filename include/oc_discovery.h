/*
// Copyright (c) 2016 Intel Corporation
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
  @brief CoAP Discovery.
  @file
*/
#ifndef OC_DISCOVERY_H
#define OC_DISCOVERY_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  filters on an individual resource for a match with the request query parameters, on 
 *         a match include the resource in the response (in link-format)
 *
 * @param resource the resource to be checked
 * @param request  the request, with all query parameters
 * @param response_length the current response length
 * @param skipped number of entries already skipped
 * @param first_entry first entry to be included
 * @param truncate if true the response payload SHALL carry the short URN for the resource types,
 *                 otherwise it SHALL carry the full URN with leading 'urn:knx' for the resources.
 *
 * @note parameter truncate is always false when called from the well-known/core EP 
 *
 * @return true individual resource added to the response payload (incl. rt's, types, ...) 
 * @return false individual resource was not added to the response payload
 */
bool oc_check_resource_by_request(const oc_resource_t *resource, oc_request_t *request, size_t *response_length,
                        int *skipped, int first_entry, bool truncate);
/**
 * @brief add the resource (uri, if, rt, ct) to the response in application link format
 *
 * @param resource the resource
 * @param response_length the response length (to be increased)
 * @param truncate if true the response payload SHALL not carry 'urn:knx' as part of the resource and interface types
 *
 * @note truncate identifies if the method call is originated by a non knx (e.g; well-known) EP or any knx related EP,
 *       in case of knx EP the truncation is ALWAYS expected
 *
 * @return true 
 * @return false (if resource or resource uri are not present) 
 */
bool oc_add_resource_to_response_payload(const oc_resource_t *resource,
                                         size_t *response_length,
                                         const bool truncate);

#ifdef __cplusplus
}
#endif

#endif /* OC_DISCOVERY_H */
