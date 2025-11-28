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

#ifdef __cplusplus
extern "C" {
	#endif

	// default batch size if ps is not part of the request, see clause 2.5.3.8, GET dev/ipv6
	#define BATCH_SIZE 1
	// default page number if pn is not part of the request
	#define PAGE_NUMBER 0

	/**
	* @brief summarize the (1...n) response status from the different
	*        application callback handler
	*
	* @note  a higher (error) status overwrites a lower (ok) status, the rank
	*        is ordered from lo to hi as defined in oc_status, example as follows:
	*        - 0 : OC_STATUS_OK (2.05)
	*				 - 1 : OC_STATUS_CREATED (2.01)
	*				 - 2 : OC_STATUS_CHANGED (2.04)
	*				 - 3 : OC_STATUS_DELETED (2.02)
	*				 - x : ...
	*				 - 5 : OC_STATUS_BAD_REQUEST (4.00)
	*				 - y : OC_STATUS_XXX (4.xx)
	*
	*/
	void collect_and_rank_status(int coap_status, oc_status_t* current_oc_status);

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
	 * @return true if 'l' exists (stop processing the message, response was done)
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
	* @brief helper function to check if query parameter pn/ps exists
	*        and if so calculate the first item (index) that will be placed
	*        on the page
	*
	* example: /f/g?pn=0&ps=3
	* @param request the request
	* @param pn_value returns pn or '0' (if query parameter does not exist)
	* @param ps_value returns ps or 'server default' (if query parameter does not exist)
	*
	* @note
	* - EPs with interface type if.ll must support pn + ps in a request
	*
	* @return pn * ps
	*/
	int evaluate_query_px(oc_request_t* request, int* pn_value, int* ps_value);

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

	/**
   * @brief helper function to check if a string contains only of '0'
   * @param stream the string pointer
   * @return all zero, true
   */
	bool oc_check_string_on_zero_content(const char* stream);

	#ifdef __cplusplus
}
#endif

#endif