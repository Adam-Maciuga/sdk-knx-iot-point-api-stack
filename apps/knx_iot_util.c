/* 
 * Copyright (c) 2024-2026 KNX Association
 *            
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file knx_iot_util.c
 * @brief Shared utilitiy functions for KNX-IoT applications
 *
 * This module provides common utility functions to display various KNX IoT tables
 * and device information in a consistent format across all applications.
 *
 */

#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "knx_iot_util.h"

void util_bool2text(bool on_off, char* text)
{
  if (on_off)
  {
    strcat(text, " On");
  }
  else
  {
    strcat(text, " Off");
  }
}

void util_int2text(int value, char* text)
{
  char value_text[50];
  (void)sprintf(value_text, " %d", value);
  strcat(text, value_text);
}

void util_double2text(double value, char* text)
{
  char new_text[200];
  (void)sprintf(new_text, " %f", value);
  strcat(text, new_text);
}

void util_str2upper(char* str)
{
  while (*str != '\0')
  {
    *str = (char)toupper(*str);
    str++;
  }
} 

/**
 * @brief Convert integer GA value to text representation
 *
 * The Group Address structure correlates with its representation style in ETS.
 * The information about the ETS Group Address representation style itself is NOT
 * included in the Group Address.
 *
 * '3-level' = main/middle/sub
 * - main = D7+D6+D5+D4+D3 of the first octet (high address)
 * - middle = D2+D1+D0 of the first octet (high address)
 * - sub = the entire second octet (low address)
 * - ranges: main = 0..31, middle = 0..7, sub = 0..255
 *
 * @param value The GA value
 * @param text Buffer to append the formatted text to
 * @param as_ets If true, format as ETS 3-level (main/middle/sub), otherwise as integer
 */
void util_int2ga_text(uint32_t value, char* text, bool as_ets)
{
  char value_text[50];

  if (as_ets)
  {
    uint32_t ga = value;
    uint32_t ga_main = (ga >> 11);
    uint32_t ga_middle = (ga >> 8) & 0x7;
    uint32_t ga_sub = (ga & 0x000000FF);
    (void)sprintf(value_text, " %u/%u/%u", ga_main, ga_middle, ga_sub);
    strcat(text, value_text);
  }
  else
  {
    (void)sprintf(value_text, " %u", value);
    strcat(text, value_text);
  }
}

/**
 * @brief Convert the scope to text for display
 *
 * @param value The scope value
 * @param text Buffer to append the formatted text to
 */
void util_int2scope_text(uint32_t value, char* text)
{
  char value_text[150];

  (void)sprintf(value_text, " [%u]", value);

  strcat(text, value_text);
  // should be the same as
  if (value & (1 << 1))
    strcat(text, " if.i");
  if (value & (1 << 2))
    strcat(text, " if.o");
  if (value & (1 << 3))
    strcat(text, " if.g.s");
  if (value & (1 << 4))
    strcat(text, " if.c");
  if (value & (1 << 5))
    strcat(text, " if.p");
  if (value & (1 << 6))
    strcat(text, " if.d");
  if (value & (1 << 7))
    strcat(text, " if.a");
  if (value & (1 << 8))
    strcat(text, " if.s");
  if (value & (1 << 9))
    strcat(text, " if.ll");
  if (value & (1 << 10))
    strcat(text, " if.b");
  if (value & (1 << 11))
    strcat(text, " if.sec");
  if (value & (1 << 12))
    strcat(text, " if.swu");
  if (value & (1 << 13))
    strcat(text, " if.pm");
  if (value & (1 << 14))
    strcat(text, " if.m");
}

/**
 * @brief Convert the group ID to text for display
 *
 * Creates the multicast address from group and scope:
 * FF3_:FD__:____:____:(8-f)___:____
 * FF35:30:<ULA-routing-prefix>::<group id>
 *    | 5 == scope
 *    | 3 == scope
 *
 * Multicast prefix: FF35:0030:  [4 bytes]
 * ULA routing prefix: FD11:2222:3333::  [6 bytes + 2 empty bytes]
 * Group Identifier: 8000 : 0068 [4 bytes ]
 *
 * @param value The group ID value
 * @param text Buffer to append the formatted text to
 * @param as_ets If true, format as partial IPv6 address, otherwise as integer
 */
void util_int2grpid_text(uint64_t value, char* text, bool as_ets)
{
  char value_text[50];

  if (as_ets)
  {
    // group number to the various bytes
    uint8_t byte_1 = (uint8_t)(value >> 0);
    uint8_t byte_2 = (uint8_t)(value >> 8);
    uint8_t byte_3 = (uint8_t)(value >> 16);
    uint8_t byte_4 = (uint8_t)(value >> 24);
    uint8_t byte_5 = (uint8_t)(value >> 32);

    if (byte_5 == 0)
    {
      (void)sprintf(value_text, " %02x%02x:%02x%02x", byte_4, byte_3, byte_2, byte_1);
    }
    else
    {
      (void)sprintf(value_text, " %02x:%02x%02x:%02x%02x", byte_5, byte_4, byte_3, byte_2, byte_1);
    }

    strcat(text, value_text);
  }
  else
  {
    (void)sprintf(value_text, " %" PRIu64, value);
    strcat(text, value_text);
  }
}
