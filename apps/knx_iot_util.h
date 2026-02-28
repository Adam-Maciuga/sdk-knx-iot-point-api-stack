/* 
 * Copyright (c) 2022-2023 Cascoda Ltd
 * Copyright (c) 2024-2026 KNX Association
 *            
 * SPDX-License-Identifier: Apache-2.0
 */     

/*
 * Note that the file 'knx_iot_util.c/h' is NOT a part of the stack or not intended to be an 
 * 'application' library. It hosts only for the application demos commonly used functionality in one place.
 */

#ifndef KNX_IOT_UTIL_H
#define KNX_IOT_UTIL_H

#ifdef __cplusplus
extern "C"
{
#endif

  /**
   * @brief convert the boolean to text and appends it to the given text
   *
   * @param on_off the boolean
   * @param text the text to add the boolean as text
   */
  void util_bool2text(bool on_off, char* text);
  
  /**
   * @brief convert the integer to text for display
   *
   * @param value the integer
   * @param text the text to add info to
   */
  void util_int2text(int value, char* text);
  
  /**
   * @brief convert the double (e.g. float)  to text for display
   *
   * @param value the value
   * @param text the text to add info too
   */
  void util_double2text(double value, char* text);

  /**
   * @brief convert the group address to text for display
   *
   * @param value the integer
   * @param text the text to add info to
   * @param as_ets the text as terminology as used in ETS
   */
  void util_int2ga_text(uint32_t value, char* text, bool as_ets);
  
  /**
   * @brief convert the scope to text for display
   *
   * @param value the scope
   * @param text the text to add info too
   */
  void util_int2scope_text(uint32_t value, char* text);
  
  /**
   * @brief convert the group id to text for display
   *
   * @param value the group id
   * @param text the text to add info too
   * @param as_ets the text as terminology as used in ETS
   */
  void util_int2grpid_text(uint64_t value, char* text, bool as_ets);
  
#ifdef __cplusplus
}
#endif

#endif
