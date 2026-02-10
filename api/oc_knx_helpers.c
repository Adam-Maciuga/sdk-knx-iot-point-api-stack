/*      
 * Copyright (c) 2023 Cascoda Ltd.
 * Copyright (c) 2024-2026 KNX Association
 *            
 * SPDX-License-Identifier: Apache-2.0
 */

#include "oc_api.h"
#include "oc_knx_helpers.h"

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
 * @brief helper function to frame an integer in the response:
 * @param value the value to be framed, max 9 chars
 * @return total bytes framed
 */
int oc_frame_integer(int value);

bool query_l_was_processed(oc_request_t* request, const int ps, const int total)
{
  if (!oc_query_values_available(request)) {
    // no query parameter at all exits 
    return false;
  }

  if (oc_query_value_exists(request, "l") == -1) {
    // query parameter 'l' does not exit 
    return false;
  }

  bool more_query_params;
  bool ps_exists = false;
  bool total_exists = false;

  char* value = NULL;
  int value_len = -1;

  oc_init_query_iterator();
  do {
    // find out if l=ps and/or l=total exists
    more_query_params = oc_iterate_query_get_values(request, "l", &value, &value_len);
    if (value_len == 2) {
      if (strncmp("ps", value, value_len) == 0) {
        ps_exists = true;
      }
    }

    if (value_len == 5) {
      if (strncmp("total", value, value_len) == 0) {
        total_exists = true;
      }
    }
  } while (more_query_params);

  if (!ps_exists && !total_exists) {
    // query l exist but with no 'ps' or 'total' 
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_NOT_FOUND);
    return true;        
  }

  if (ps_exists && total_exists && request->query_len > sizeof("l=total&l=ps") - 1) {
    // query l exist with 'ps' and 'total' but other query parameter as well 
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
    return true;        
  }

  if (ps_exists && !total_exists && request->query_len > sizeof("l=ps") - 1) {
    // query l exist with 'ps' but other query parameter as well 
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
    return true;        
  }

  if (!ps_exists && total_exists && request->query_len > sizeof("l=total") - 1) {
    // query l exist with 'total' but other query parameter as well 
    oc_prepare_no_format_response_no_payload(request, OC_STATUS_BAD_REQUEST);
    return true;        
  }

  // good enough
  const int response_length = oc_frame_query_l(
          oc_string(request->resource->uri), ps_exists, ps, total_exists, total);
  oc_prepare_linkformat_response(request, OC_STATUS_OK, response_length);
  return true;          
}

int oc_frame_query_l(char* url, bool ps_exists, int ps, bool total_exists, int total)
{
  // request  .../fp/r?l=total&l=ps
  // response </fp/r>;total=22;ps=5

  // open 
  int response_length = oc_rep_add_line_to_buffer("<");

  // add URL
  response_length += oc_rep_add_line_to_buffer(url);

  // close 
  response_length += oc_rep_add_line_to_buffer(">");

  if (total_exists) {
    // first total 
    response_length += oc_rep_add_line_to_buffer(";total=");
    response_length += oc_frame_integer(total);
  }

  if (ps_exists) {
    // second page size (if total)
    response_length += oc_rep_add_line_to_buffer(";ps=");
    response_length += oc_frame_integer(ps);
  }

  return response_length;
}

int evaluate_query_px(oc_request_t* request, int* pn_value, int* ps_value)
{
  char* value = NULL;
  int value_len = -1;

  if (!oc_query_values_available(request)) {
    // no query parameter at all exits, first entry = 0 
    return 0;
  }

  oc_init_query_iterator();
  if (oc_query_value_exists(request, "pn") == 1) {
    // fetch the first, ignore any possible (additional) next pn=xxx
    oc_iterate_query_get_values(request, "pn", &value, &value_len);
    // converts to '0' in case of error conversion 
    *pn_value = atoi(value); 
  }

  oc_init_query_iterator();
  if (oc_query_value_exists(request, "ps") == 1) {
    // fetch the first, ignore any possible (additional) next pn=xxx
    oc_iterate_query_get_values(request, "ps", &value, &value_len);
    // converts to '0' in case of error conversion 
    *ps_value = atoi(value);
  }

  // first entry = pn * ps (optimization possible, but 32-bit platform usually uses hw multiplier)
  return *pn_value * *ps_value;
}

void collect_and_rank_status(int coap_status, oc_status_t* current_oc_status)
{
  const oc_status_t new_oc_status = get_oc_status_code_from_coap_code(coap_status);

  // the new oc_status code is higher than that before ... 
  if (new_oc_status > *current_oc_status) {
    *current_oc_status = new_oc_status;
  }
}

int add_next_page_indicator(char* url, int next_page_num)
{
  // example </p?pn=1>;rt="p.next";ct=40, 'p.next' is fix and 'p' is not an individual url part
  
  #define MAX_PAGE_NUM (5) // max number pn=99999 

  char next_page_str[MAX_PAGE_NUM];
  (void)snprintf(next_page_str, MAX_PAGE_NUM, "%d", next_page_num);

  int response_length = oc_rep_add_line_to_buffer(",<"); 
  response_length += oc_rep_add_line_to_buffer(url);
  response_length += oc_rep_add_line_to_buffer("?pn=");
  response_length += oc_rep_add_line_to_buffer(next_page_str);
  response_length += oc_rep_add_line_to_buffer(">;rt=\"p.next\";ct=40");

  return response_length;
}

int oc_frame_integer(const int value)
{
  // supports max 32bit decimal number
  char string[10];
  // conversion fails > empty string ...
  return snprintf(string, 9, "%d", value) < 0 ? oc_rep_add_line_to_buffer("") : oc_rep_add_line_to_buffer(string);
}
