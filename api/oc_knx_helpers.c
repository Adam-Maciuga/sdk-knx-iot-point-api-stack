/*
 // Copyright (c) 2023 Cascoda Ltd
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

#include "oc_api.h"
#include "oc_knx_helpers.h"

bool query_l_was_processed(oc_request_t* request, const int ps, const int total)
{
  if (!oc_query_values_available(request))
  { // no query parameter at all exits 
    return false;
  }

  if (oc_query_value_exists(request, "l") == -1)
  { // query parameter 'l' does not exit 
    return false;
  }

  bool more_query_params;
  bool ps_exists = false;
  bool total_exists = false;

  char* value = NULL;
  int value_len = -1;

  oc_init_query_iterator();

  do
  {
    // find out if l=ps and/or l=total exists
    more_query_params = oc_iterate_query_get_values(request, "l", &value, &value_len);
    if (value_len == 2)
    {
      if (strncmp("ps", value, value_len) == 0)
      {
        ps_exists = true;
      }
    }
    if (value_len == 5)
    {
      if (strncmp("total", value, value_len) == 0)
      {
        total_exists = true;
      }
    }
  }
  while (more_query_params);

  if (!ps_exists && !total_exists)
  { // query l exist but with no 'ps' or 'total' 
    oc_send_response_no_format(request, OC_STATUS_NOT_FOUND);
    return true;        
  }

  if (ps_exists && total_exists && request->query_len > sizeof("l=total&l=ps") - 1)
  { // query l exist with 'ps' and 'total' but other query parameter as well 
    oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
    return true;        
  }

  if (ps_exists && !total_exists && request->query_len > sizeof("l=ps") - 1)
  { // query l exist with 'ps' but other query parameter as well 
    oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
    return true;        
  }

  if (!ps_exists && total_exists && request->query_len > sizeof("l=total") - 1)
  { // query l exist with 'total' but other query parameter as well 
    oc_send_response_no_format(request, OC_STATUS_BAD_REQUEST);
    return true;        
  }

  // good enough
  const int response_length = oc_frame_query_l(oc_string(request->resource->uri), ps_exists, ps, total_exists, total);
  oc_send_linkformat_response(request, OC_STATUS_OK, response_length);
  return true;          

}

int oc_frame_query_l(char* url, bool ps_exists, int ps, bool total_exists, int total)
{
  // request  .../fp/r?l=total&l=ps
  // response </fp/r>;l=22;ps=5

  // open 
  int response_length = oc_rep_add_line_to_buffer("<");

  // add URL
  response_length += oc_rep_add_line_to_buffer(url);

  // close 
  response_length += oc_rep_add_line_to_buffer(">");

  if (total_exists) // first total 
  {
    response_length += oc_rep_add_line_to_buffer(";total=");
    response_length += oc_frame_integer(total);
  }
  if (ps_exists)    // second page size (if total)
  {
    response_length += oc_rep_add_line_to_buffer(";ps=");
    response_length += oc_frame_integer(ps);
  }

  return response_length;
}

bool check_if_query_pn_exist(oc_request_t* request, int* pn_value)
{
  

  char* value = NULL;
  int value_len = -1;

  if (pn_value == NULL)
  {
    // need to have a ref ptr for the parent value
    return false;
  }

  *pn_value = 0;

  if (!oc_query_values_available(request))
  { // no query parameter at all exits 
    return false;
  }

  oc_init_query_iterator();
  if (oc_query_value_exists(request, "pn") > -1)
  {
    // fetch the first, ignore any possible (additional) next pn=xxx
    oc_iterate_query_get_values(request, "pn", &value, &value_len);
    // converts to '0' in case of error conversion 
    *pn_value = atoi(value); 
    return true;
  }

  return false;
}

int add_next_page_indicator(char* url, int next_page_num)
{
  // example : </p?pn=1>;rt="p.next";ct=40
  // 'p.next' is fix and 'p' is not an individual url part
  
  char next_page_str[20];
  (void)sprintf(next_page_str, "%d", next_page_num);

  int response_length = oc_rep_add_line_to_buffer(",\n<"); 
  response_length += oc_rep_add_line_to_buffer(url);
  response_length += oc_rep_add_line_to_buffer("?pn=");
  response_length += oc_rep_add_line_to_buffer(next_page_str);
  response_length += oc_rep_add_line_to_buffer(">;rt=\"p.next\";ct=40");

  return response_length;
}

int
oc_frame_integer(const int value)
{
  // supports max 32bit decimal number
  char string[10];
  // conversion fails > empty string ...
  return snprintf(string, 9, "%d", value) < 0 ? oc_rep_add_line_to_buffer("") : oc_rep_add_line_to_buffer(string);
}
