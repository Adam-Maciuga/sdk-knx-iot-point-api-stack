/*
// Copyright (c) 2023 Cascoda Ltd.
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
  @brief generic knx helpers
  @file
*/
#ifndef OC_KNX_HELPERS_H
#define OC_KNX_HELPERS_H

#include "oc_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BATCH_SIZE 1  // see if.b in 2.5.3.8, if ps is not part of the request then this assumes a ps =1

#ifndef PAGE_SIZE     // don't (re)define if set by CMAKE compile definitions 
#define PAGE_SIZE 20  // default server size in case the ps query parameter is absent
#endif

  /**
   * @brief helper function to process entire query parameter 'l' handling 
   *
   * @example: /fp/r?l=total&l=ps
   * @param request the request
   * @param ps the current page size 
   * @param total the current total amount of resource items 
   *
   * @note 'l' and 'other' query parameters SHALL NOT be combined in a request
   *
   * @return false if 'l' doesn't exist (continue to process the message)
   * @return true if 'l' exists (all 'l' steps are done, stop processing the message)
   * 
   */
  bool query_l_was_processed(oc_request_t* request, int ps, int total);

  /**
   * @brief helper function to frame url part of query response:
   *  *
   * @param url the url to be framed
   * @param ps_exists frame ps
   * @param ps page size
   * @param total_exists frame total
   * @param total total items
   * @return total bytes framed
   */
  int oc_frame_query_l(char* url, bool ps_exists, int ps, bool total_exists, int total);

  /**
   * @brief helper function to check if query parameter pn exists
   *
   * example: /dev/ipv6?pn=0&ps=3
   * @param request the request
   * @param pn_value returns '0' if not exist otherwise value
   *
   * @note
   * - value '0' is also the default for requests without pn present (pn=0 )
   * - EPs with interface type if.ll or if.b must support pn + ps in a request
   * - page size (ps) is not supported, EPS not supporting it must return a 4.00 
   *
   * @return true == pn exists
   */
  bool check_if_query_pn_exist(oc_request_t* request, int* pn_value);

  /**
   * @brief helper function to frame next page indicator, if more requests (pages)
   * are needed to get the full list
   *
   * @param url the url to be framed
   * @param next_page_num the next page number to be framed
   * @return total bytes framed
   */
  int add_next_page_indicator(char* url, int next_page_num);

  /**
   * @brief helper function to frame an integer in the response:
   * @param value the value to be framed, max 9 chars
   * @return total bytes framed
   */
  int oc_frame_integer(int value);

#ifdef __cplusplus
}
#endif

#endif