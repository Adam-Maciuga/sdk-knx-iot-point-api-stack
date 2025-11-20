/*
-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
 Copyright (c) 2024-2025 KNX Association
-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.

-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
*/

/**
 * @file knx_iot_virtual.cpp
 * @brief Shared functions for KNX IoT virtual demo applications
 *
 * This module provides common functions to display various KNX IoT tables
 * and device information in a consistent format across all demo applications.
 *
 */

#include "knx_iot_virtual.h"
#include "api/oc_knx_fp.h"
#include "api/oc_knx_sec.h"
#include "oc_core_res.h"
#include "oc_knx.h"
#include <wx/string.h>
#include <cstdio>
#include <cstring>


/**
 * @brief Dump QR Code
 *
 * Displays:
 * - Serial Number and QR Code to copy/paste it to a configuration client 
 *
 * @return wxString containing formatted QR Code
 */
wxString util_dumpQRCode()
{
  oc_device_info_t* device = oc_core_get_device_info();

  wxString out("=== QR Code ===\n");
  char line[256];

  // QR code
  (void)sprintf(line, "KNX:S:%s;P:%s\n", oc_string(device->serialnumber), app_get_password());
  app_str_to_upper(line);
  out += line;

  return out;
}


/**
 * @brief Dump device identification information
 *
 * Displays:
 * - Serial number
 * - Individual address (IA) in 3-level format (area.line.device)
 * - Installation ID (IID) as hex bytes
 *
 * @return wxString containing formatted device IDs
 */
wxString util_dumpDeviceIDs()
{
  oc_device_info_t* device = oc_core_get_device_info();
  
  wxString out("=== Device IDs ===\n");
  char line[256];

  // Serial number
  (void)sprintf(line, "Serial number: '%s'\n", oc_string(device->serialnumber));
  out += line;

  // Individual address
  const uint16_t ia_a = device->ia >> 12; // area
  const uint16_t ia_l = device->ia >> 8 & 0xF; // line
  const uint16_t ia_d = device->ia & 0x00FF; // device
  (void)sprintf(line, "Individual address: %d.%d.%d (%04x)\n", ia_a, ia_l, ia_d, device->ia);
  out += line;

  // Installation ID
  uint64_t value = device->iid;
  uint8_t byte_1 = static_cast<uint8_t>(value);
  uint8_t byte_2 = static_cast<uint8_t>(value >> 8);
  uint8_t byte_3 = static_cast<uint8_t>(value >> 16);
  uint8_t byte_4 = static_cast<uint8_t>(value >> 24);
  uint8_t byte_5 = static_cast<uint8_t>(value >> 32);

  if (byte_5 == 0)
  {
    (void)sprintf(line, "Installation ID: %02x%02x:%02x%02x\n", byte_4, byte_3, byte_2, byte_1);
  }
  else
  {
    (void)sprintf(line, "Installation ID: %02x:%02x%02x:%02x%02x\n", byte_5, byte_4, byte_3, byte_2, byte_1);
  }
  out += line;

  return out;
}

/**
 * @brief returns load state information
 *
 * @return wxString containing lsm state info
 */
wxString util_dumpLsmState()
{
  oc_device_info_t* device = oc_core_get_device_info();
  
  wxString out("- LSM State:\n");

  if (device->lsm_s == LSM_S_UNLOADED)
  {
    out += "  LSM: unloaded\n";
  }
  else if (device->lsm_s == LSM_S_LOADING)
  {
    out += "  LSM: loading\n";
  }
  else if (device->lsm_s == LSM_S_LOADED)
  {
    out += "  LSM: loaded\n";
  }

  return out;
}

/**
 * @brief Dump the Group Object Table into a string
 *
 * Iterates through all group object table entries and prints:
 * - Index
 * - id
 * - url (resource path)
 * - cflags (communication flags, both numeric and textual)
 * - ga (group addresses list)
 *
 * @param ga_conversion If true, display GAs in ETS 3-level format
 * @return wxString containing formatted Group Object Table
 */
wxString util_dumpGroupObjectTable(bool ga_conversion)
{
  wxString out("=== Group Object Table ===\n");
  char line[512];

  int total = oc_core_get_group_object_table_total_size();
  for (int i = 0; i < total; i++) {
    oc_group_object_table_t* entry = oc_core_get_group_object_table_entry(i);
    if (entry && entry->ga_len > 0) {
      sprintf(line, "Index %d ", i);
      out += line;

      sprintf(line, "  id: '%d'  ", entry->id);
      out += line;

      sprintf(line, "  url: '%s' ", oc_string(entry->href));
      out += line;

      // numeric + textual cflags in ONE go
      sprintf(line, "  cflags : '%d' ", (int)entry->cflags);
      oc_cflags_as_string(line, entry->cflags);
      out += line;

      // ga list
      strcpy(line, "  ga : [");
      for (int j = 0; j < entry->ga_len; j++) {
        util_int2ga_text(entry->ga[j], line, ga_conversion);
      }
      strcat(line, " ]");
      out += line;

      out += "\n";
    }
  }
  return out;
}

/**
 * @brief Dump the Publisher Table into a string
 *
 * Iterates through all publisher table entries and prints:
 * - Index
 * - id, ia, iid, fid
 * - grpid (converted if enabled)
 * - at string
 * - group addresses (GA list)
 *
 * @param ga_conversion If true, display GAs in ETS 3-level format
 * @param grpid_conversion If true, display GRPIDs as partial IPv6 addresses
 * @param iid_conversion If true, display IIDs as partial IPv6 addresses
 * @return wxString containing formatted Publisher Table
 */
wxString util_dumpPublisherTable(bool ga_conversion, bool grpid_conversion, bool iid_conversion)
{
  wxString out("=== Publisher Table ===\n");
  char line[256];

  int total = oc_core_get_publisher_table_size();
  for (int i = 0; i < total; i++) {
    oc_group_table_t* entry = oc_core_get_publisher_table_entry(i);
    if (entry && entry->id >= 0) {
      sprintf(line, "Index %d ", i); out += line;
      sprintf(line, "  id: '%d'  ", entry->id); out += line;
      if (entry->ia >= 0) { sprintf(line, "  ia: '%d' ", entry->ia); out += line; }
      if (entry->iid >= 0) { strcpy(line, "  iid: "); util_int2grpid_text(entry->iid, line, iid_conversion); out += line; }
      if (entry->fid >= 0) { sprintf(line, "  fid: '%lld' ", entry->fid); out += line; }
      if (entry->grpid > 0) { strcpy(line, "  grpid: "); util_int2grpid_text(entry->grpid, line, grpid_conversion); out += line; }
      if (oc_string_len(entry->at) > 0) { sprintf(line, "  at: '%s' ", oc_string(entry->at)); out += line; }
      if (entry->ga_len > 0) {
        strcpy(line, "  ga : [");
        for (int j = 0; j < entry->ga_len; j++) {
          util_int2ga_text(entry->ga[j], line, ga_conversion);
        }
        strcat(line, " ]");
        out += line;
      }
      out += "\n";
    }
  }
  return out;
}

/**
 * @brief Dump the Recipient Table into a string
 *
 * Iterates through all recipient table entries and prints:
 * - Index
 * - id, ia, iid, fid
 * - grpid (converted if enabled)
 * - at string
 * - group addresses (GA list)
 *
 * @param ga_conversion If true, display GAs in ETS 3-level format
 * @param grpid_conversion If true, display GRPIDs as partial IPv6 addresses
 * @param iid_conversion If true, display IIDs as partial IPv6 addresses
 * @return wxString containing formatted Recipient Table
 */
wxString util_dumpRecipientTable(bool ga_conversion, bool grpid_conversion, bool iid_conversion)
{
  wxString out("=== Recipient Table ===\n");
  char line[256];

  int total = oc_core_get_recipient_table_size();
  for (int i = 0; i < total; i++) {
    oc_group_table_t* entry = oc_core_get_recipient_table_entry(i);
    if (entry && entry->id >= 0) {
      sprintf(line, "Index %d ", i); out += line;
      sprintf(line, "  id: '%d'  ", entry->id); out += line;
      if (entry->ia >= 0) { sprintf(line, "  ia: '%d' ", entry->ia); out += line; }
      if (entry->iid >= 0) { strcpy(line, "  iid: "); util_int2grpid_text(entry->iid, line, iid_conversion); out += line; }
      if (entry->fid >= 0) { sprintf(line, "  fid: '%lld' ", entry->fid); out += line; }
      if (entry->grpid > 0) { strcpy(line, "  grpid: "); util_int2grpid_text(entry->grpid, line, grpid_conversion); out += line; }
      if (oc_string_len(entry->at) > 0) { sprintf(line, "  at: '%s' ", oc_string(entry->at)); out += line; }
      if (entry->ga_len > 0) {
        strcpy(line, "  ga : [");
        for (int j = 0; j < entry->ga_len; j++) {
          util_int2ga_text(entry->ga[j], line, ga_conversion);
        }
        strcat(line, " ]");
        out += line;
      }
      out += "\n";
    }
  }
  return out;
}

/**
 * @brief Dump the Parameter List into a string
 *
 * Iterates through all application parameters and prints:
 * - Index
 * - URL
 * - name
 *
 * If no parameters exist, prints "no parameters in this device".
 *
 * @return wxString containing formatted Parameter List
 */
wxString util_dumpParameterList()
{
  wxString out("=== Parameter List ===\n");
  char line[256];

  int index = 1;
  char* url = app_get_parameter_url(index);
  if (url == NULL) {
    out += "no parameters in this device\n";
  }
  while (url) {
    sprintf(line, "\nIndex %02d ", index); out += line;
    sprintf(line, "  url : '%s'  ", url); out += line;
    char* name = app_get_parameter_name(index);
    if (name) { sprintf(line, "  name: '%s'  ", name); out += line; }
    index++;
    url = app_get_parameter_url(index);
  }
  return out;
}

/**
 * @brief Dump the Auth/AT Table into a string
 *
 * Iterates through all authentication/authorization entries and prints:
 * - Index
 * - id
 * - profile
 * - For DTLS: sub, kid
 * - For OSCORE: osc_id, osc_ms, osc_contextid (hex dumps)
 * - scope or osc_ga (with GA list)
 *
 * @param ga_conversion If true, display GAs in ETS 3-level format
 * @return wxString containing formatted Auth/AT Table
 */
wxString util_dumpAuthTable(bool ga_conversion)
{
  wxString out("=== Auth/AT Table ===\n");
  char line[512];

  int max_entries = oc_core_get_at_table_size();
  for (int i = 0; i < max_entries; i++) {
    oc_auth_at_t* entry = oc_get_auth_at_entry(i);
    if (entry && oc_string_len(entry->id)) {
      sprintf(line, "index : '%d' id = '%s' ", i, oc_string(entry->id));
      out += line;
      sprintf(line, "  profile : %d (%s)", entry->profile,
              oc_at_profile_to_string(entry->profile));
      out += line;

      if (entry->profile == OC_PROFILE_COAP_DTLS) {
        if (oc_string_len(entry->sub) > 0) {
          sprintf(line, "  sub : %s", oc_string(entry->sub)); out += line;
        }
        if (oc_string_len(entry->kid) > 0) {
          sprintf(line, "  kid : %s", oc_string(entry->kid)); out += line;
        }
      }

      if (entry->profile == OC_PROFILE_COAP_OSCORE) {
        // osc_id
        if (oc_byte_string_len(entry->osc_id) > 0) {
          sprintf(line, "  osc_id [%d]: ",
                  (int)oc_byte_string_len(entry->osc_id));
          out += line;
          char* ms = oc_string(entry->osc_id);
          for (int j = 0; j < (int)oc_byte_string_len(entry->osc_id); j++) {
            sprintf(line, "%02x", (unsigned char)ms[j]);
            out += line;
          }
        }
        // osc_ms
        if (oc_byte_string_len(entry->osc_ms) > 0) {
          sprintf(line, "  osc_ms [%d]: ",
                  (int)oc_byte_string_len(entry->osc_ms));
          out += line;
          char* ms = oc_string(entry->osc_ms);
          for (int j = 0; j < (int)oc_byte_string_len(entry->osc_ms); j++) {
            sprintf(line, "%02x", (unsigned char)ms[j]);
            out += line;
          }
        }
        // osc_contextid
        if (oc_byte_string_len(entry->osc_contextid) > 0) {
          sprintf(line, "  osc_contextid [%d]: ",
                  (int)oc_byte_string_len(entry->osc_contextid));
          out += line;
          char* ms = oc_string(entry->osc_contextid);
          for (int j = 0; j < (int)oc_byte_string_len(entry->osc_contextid); j++) {
            sprintf(line, "%02x", (unsigned char)ms[j]);
            out += line;
          }
        }
        // scope / osc_ga
        if (entry->scope == OC_ACL_GA) {
          strcpy(line, "  osc_ga : [");
          out += line;
          for (int j = 0; j < entry->ga_len; j++) {
            util_int2ga_text(entry->ga[j], line, ga_conversion);
          }
          strcat(line, " ]");
          out += line;
        } else {
          sprintf(line, "  scope : ");
          util_int2scope_text(entry->scope, line);
          out += line;
        }
      }
      out += "\n";
    }
  }
  return out;
}

// ===== Utility Functions =====

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
    uint8_t byte_1 = static_cast<uint8_t>(value >> 0);
    uint8_t byte_2 = static_cast<uint8_t>(value >> 8);
    uint8_t byte_3 = static_cast<uint8_t>(value >> 16);
    uint8_t byte_4 = static_cast<uint8_t>(value >> 24);
    uint8_t byte_5 = static_cast<uint8_t>(value >> 32);

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
    (void)sprintf(value_text, " %llu", value);
    strcat(text, value_text);
  }
}