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
  @brief knx /f resource implementation
  @file

  This module implements the /f and /f/x resource
  The /f resource list all functional blocks.
  The functional blocks will have urls defined as
  `<functionalblocknumber>` (instance 0) or when there are more instances
  as`<functionalblocknumber>_instance`

*/
#ifndef OC_KNX_FB_INTERNAL_H
#define OC_KNX_FB_INTERNAL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief add functional blocks to the response
 *
 * adds the list of functional blocks for /f or ./well-known/core to the
 * response
 *
 * @param request the request
 * @param short_urn_form if the urn:knx needs to be added as part of response or not (/well-known vs /f)
 * @param response_length the current response length
 * @param matches number of matches (so far)
 * @param skipped number of entries already skipped
 * @param first_entry first entry to be included
 * @param last_entry last entry to be included (exclusive)
 * @return true (at least one FB was added)
 * @return false (no FB was added)
 *
 */
bool oc_was_adding_function_blocks_to_response(oc_request_t *request, bool short_urn_form,
                                        size_t *response_length, int *matches,
                                        int *skipped, int first_entry,
                                        int last_entry);

/**
 *@brief count functional blocks in a device
 */
int oc_count_functional_blocks(void);

/**
 * @brief check if functional blocks should be added to the response
 * @param request the request
 * @return true
 * @return false
 */
bool oc_check_if_functional_blocks_need_to_add(oc_request_t *request);

#ifdef __cplusplus
}
#endif

#endif /* OC_KNX_FB_INTERNAL_H */
