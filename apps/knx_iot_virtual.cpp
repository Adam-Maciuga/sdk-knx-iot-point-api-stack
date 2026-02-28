/* 
 * Copyright (c) 2024-2026 KNX Association
 *            
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file knx_iot_virtual.cpp
 * @brief Shared functions for KNX IoT virtual demo applications
 *
 * This module provides common functions to display various KNX IoT tables
 * and device information in a consistent format across all demo applications.
 *
 */

#include <inttypes.h>
#include <cstdio>
#include <cstring>
#include <wx/string.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/combobox.h>
#include <wx/button.h>
#include <wx/textctrl.h>
#include <wx/settings.h>
#include "knx_iot_app.h"
#include "knx_iot_util.h"
#include "knx_iot_virtual.h"
#include "api/oc_knx_fp.h"
#include "api/oc_knx_sec.h"
#include "oc_core_res.h"
#include "oc_knx.h"
#include "port/oc_network_interface.h"
#include "port/oc_storage.h"

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
  
  wxString out("=== Load State Machine (LSM) ===\n");

  if (device->lsm_s == LSM_S_UNLOADED)
  {
    out += "Unloaded\n";
  }
  else if (device->lsm_s == LSM_S_LOADING)
  {
    out += "Loading\n";
  }
  else if (device->lsm_s == LSM_S_LOADED)
  {
    out += "Loaded\n";
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
      if (entry->ia >= 0) { sprintf(line, "  ia: %d ", entry->ia); out += line; }
      if (entry->iid >= 0) { strcpy(line, "  iid: "); util_int2grpid_text(entry->iid, line, iid_conversion); out += line; }
      if (entry->fid >= 0) { sprintf(line, "  fid: '% " PRIi64 "' ", entry->fid); out += line; }
      if (entry->grpid > 0) { strcpy(line, "  grpid: "); util_int2grpid_text(entry->grpid, line, grpid_conversion); out += line; }
      if (oc_string_len(entry->at) > 0) { sprintf(line, "  at: %s ", oc_string(entry->at)); out += line; }
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
      if (entry->ia >= 0) { sprintf(line, "  ia: %d ", entry->ia); out += line; }
      if (entry->iid >= 0) { strcpy(line, "  iid: "); util_int2grpid_text(entry->iid, line, iid_conversion); out += line; }
      if (entry->fid >= 0) { sprintf(line, "  fid: '% " PRIi64 "' ", entry->fid); out += line; }
      if (entry->grpid > 0) { strcpy(line, "  grpid: "); util_int2grpid_text(entry->grpid, line, grpid_conversion); out += line; }
      if (oc_string_len(entry->at) > 0) { sprintf(line, "  at: %s ", oc_string(entry->at)); out += line; }
      if (entry->ga_len > 0) {
        strcpy(line, "  ga : [");
        for (int j = 0; j < entry->ga_len; j++) {
          util_int2ga_text(entry->ga[j], line, ga_conversion);
        }
        strcat(line, " ]");
        out += line;
      }
      
      // Display IPv6 resolution status and address
      if (entry->ipv6_res.resolve_status == OC_IP_STATUS_RESOLVED)
      {
        sprintf(line, "  ipv6: %02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x (resolved)",
                entry->ipv6_adr.ipv6[0], entry->ipv6_adr.ipv6[1], entry->ipv6_adr.ipv6[2], entry->ipv6_adr.ipv6[3],
                entry->ipv6_adr.ipv6[4], entry->ipv6_adr.ipv6[5], entry->ipv6_adr.ipv6[6], entry->ipv6_adr.ipv6[7],
                entry->ipv6_adr.ipv6[8], entry->ipv6_adr.ipv6[9], entry->ipv6_adr.ipv6[10], entry->ipv6_adr.ipv6[11],
                entry->ipv6_adr.ipv6[12], entry->ipv6_adr.ipv6[13], entry->ipv6_adr.ipv6[14], entry->ipv6_adr.ipv6[15]);
        out += line;
      }
      else if (entry->ipv6_res.resolve_status == OC_IP_STATUS_UNRESOLVED)
      {
        sprintf(line, "  ipv6: (not resolved)");
        out += line;
      }
      else if (entry->ipv6_res.resolve_status == OC_IP_STATUS_RESOLVING)
      {
        sprintf(line, "  ipv6: (resolving...)");
        out += line;
      }
      else if (entry->ipv6_res.resolve_status == OC_IP_STATUS_FAILED)
      {
        sprintf(line, "  ipv6: (resolution failed)");
        out += line;
      }
      else if (entry->ipv6_res.resolve_status == OC_IP_STATUS_EXPIRED)
      {
        sprintf(line, "  ipv6: (expired)");
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

  int index = 0;
  char* url = app_get_parameter_url(index);
  while (url) {
    sprintf(line, "index %02d ", index); out += line;
    sprintf(line, "\turl: '%s' ", url); out += line;
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

// Network Interface Dialog Implementation

NetworkInterfaceDialog::NetworkInterfaceDialog(wxWindow* parent)
  : wxDialog(parent, wxID_ANY, "Network Interfaces",
             wxDefaultPosition, wxSize(500, 200),
             wxDEFAULT_DIALOG_STYLE)
{
  wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL);
  
  // Interface selector row
  wxBoxSizer* ifaceBox = new wxBoxSizer(wxHORIZONTAL);
  wxStaticText* label = new wxStaticText(this, wxID_ANY, "Network Interface:");
  ifaceBox->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
  
  m_interface_combo = new wxComboBox(this, wxID_ANY, "All interfaces",
                                     wxDefaultPosition, wxSize(300, -1),
                                     wxArrayString(), wxCB_READONLY);
  m_interface_combo->Bind(wxEVT_COMBOBOX, &NetworkInterfaceDialog::OnInterfaceChange, this);
  ifaceBox->Add(m_interface_combo, 1, wxEXPAND | wxRIGHT, 5);
  
  m_refresh_btn = new wxButton(this, wxID_ANY, wxT("\u21BB"), wxDefaultPosition, wxSize(30, 30));
  m_refresh_btn->SetToolTip("Refresh Interfaces");
  m_refresh_btn->Bind(wxEVT_BUTTON, &NetworkInterfaceDialog::OnRefresh, this);
  ifaceBox->Add(m_refresh_btn, 0);
  
  vbox->Add(ifaceBox, 0, wxEXPAND | wxALL, 10);
  
  // IPv6 address row
  wxBoxSizer* ipBox = new wxBoxSizer(wxHORIZONTAL);
  wxStaticText* ipLabel = new wxStaticText(this, wxID_ANY, "IPv6 Address:");
  ipBox->Add(ipLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
  
  m_ipv6_text = new wxTextCtrl(this, wxID_ANY, "(not available)",
                                wxDefaultPosition, wxSize(300, -1), wxTE_READONLY);
  m_ipv6_text->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
  ipBox->Add(m_ipv6_text, 1, wxEXPAND);
  
  vbox->Add(ipBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
  
  // Close button
  wxButton* closeBtn = new wxButton(this, wxID_OK, "Close");
  closeBtn->Bind(wxEVT_BUTTON, &NetworkInterfaceDialog::OnClose, this);
  vbox->Add(closeBtn, 0, wxALIGN_CENTER | wxALL, 10);
  
  SetSizer(vbox);
  Centre();
  
  // Populate interfaces and restore saved selection
  PopulateInterfaces();
  
  uint32_t saved_filter = oc_network_get_interface_filter();
  if (saved_filter == 0) {
    m_interface_combo->SetSelection(0);
    m_ipv6_text->SetValue("(multiple)");
  } else {
    bool found = false;
    for (unsigned int i = 0; i < m_interface_combo->GetCount(); i++) {
      wxString choice = m_interface_combo->GetString(i);
      long if_index = 0;
      wxString index_str = choice.AfterFirst('[').BeforeFirst(']');
      if (index_str.ToLong(&if_index) && (uint32_t)if_index == saved_filter) {
        m_interface_combo->SetSelection(i);
        wxString ipv6_str = GetIPv6AddressForInterface((int)if_index);
        m_ipv6_text->SetValue(ipv6_str);
        found = true;
        break;
      }
    }
    if (!found) {
      m_interface_combo->SetSelection(0);
      m_ipv6_text->SetValue("(multiple)");
    }
  }
}

wxString NetworkInterfaceDialog::GetStatusMessage()
{
  uint32_t filter = oc_network_get_interface_filter();
  if (filter == 0) {
    return "Network: All interfaces enabled";
  } else {
    return wxString::Format("Network: Using interface %u", filter);
  }
}

void NetworkInterfaceDialog::PopulateInterfaces()
{
  m_interface_combo->Clear();
  
  wxArrayString interface_choices;
  interface_choices.Add("All interfaces");
  
  oc_network_interface_info_t interfaces[32];
  int iface_count = oc_network_enumerate_interfaces(interfaces, 32);
  for (int i = 0; i < iface_count; i++) {
    if (!interfaces[i].is_up || !interfaces[i].has_ipv6) {
      continue;
    }
    wxString choice = wxString::Format("[%d] %s", interfaces[i].if_index, interfaces[i].name);
    interface_choices.Add(choice);
  }
  
  m_interface_combo->Append(interface_choices);
}

void NetworkInterfaceDialog::OnRefresh(wxCommandEvent& event)
{
  uint32_t current_filter = oc_network_get_interface_filter();
  PopulateInterfaces();
  
  if (current_filter == 0) {
    m_interface_combo->SetSelection(0);
    m_ipv6_text->SetValue("(multiple)");
  } else {
    bool found = false;
    for (unsigned int i = 0; i < m_interface_combo->GetCount(); i++) {
      wxString choice = m_interface_combo->GetString(i);
      long if_index = 0;
      wxString index_str = choice.AfterFirst('[').BeforeFirst(']');
      if (index_str.ToLong(&if_index) && (uint32_t)if_index == current_filter) {
        m_interface_combo->SetSelection(i);
        wxString ipv6_str = GetIPv6AddressForInterface((int)if_index);
        m_ipv6_text->SetValue(ipv6_str);
        found = true;
        break;
      }
    }
    if (!found) {
      m_interface_combo->SetSelection(0);
      m_ipv6_text->SetValue("(previous interface unavailable)");
      oc_network_set_interface_filter(0);
      oc_network_refresh_endpoints();
    }
  }
}

void NetworkInterfaceDialog::OnInterfaceChange(wxCommandEvent& event)
{
  int selection = m_interface_combo->GetSelection();
  
  if (selection == 0) {
    oc_network_set_interface_filter(0);
    oc_network_refresh_endpoints();
    m_ipv6_text->SetValue("(multiple)");
    
    uint32_t filter = 0;
    oc_storage_write("network_interface", (uint8_t*)&filter, sizeof(filter));
  } else {
    wxString choice = m_interface_combo->GetStringSelection();
    long if_index = 0;
    wxString index_str = choice.AfterFirst('[').BeforeFirst(']');
    if (index_str.ToLong(&if_index)) {
      oc_network_set_interface_filter((int)if_index);
      oc_network_refresh_endpoints();
      
      wxString ipv6_str = GetIPv6AddressForInterface((int)if_index);
      m_ipv6_text->SetValue(ipv6_str);
      
      uint32_t filter = (uint32_t)if_index;
      oc_storage_write("network_interface", (uint8_t*)&filter, sizeof(filter));
    }
  }
}

wxString NetworkInterfaceDialog::GetIPv6AddressForInterface(int if_index)
{
  oc_endpoint_t *ep = oc_connectivity_get_endpoints();
  
  while (ep) 
  {
    if (ep->flags & IPV6 && ep->interface_index == if_index) 
    {
      char ipv6_str[64];
      snprintf(ipv6_str, sizeof(ipv6_str),
               "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
               ep->addr.ipv6.address[0], ep->addr.ipv6.address[1],
               ep->addr.ipv6.address[2], ep->addr.ipv6.address[3],
               ep->addr.ipv6.address[4], ep->addr.ipv6.address[5],
               ep->addr.ipv6.address[6], ep->addr.ipv6.address[7],
               ep->addr.ipv6.address[8], ep->addr.ipv6.address[9],
               ep->addr.ipv6.address[10], ep->addr.ipv6.address[11],
               ep->addr.ipv6.address[12], ep->addr.ipv6.address[13],
               ep->addr.ipv6.address[14], ep->addr.ipv6.address[15]);
      return wxString(ipv6_str);
    }
    ep = ep->next;
  }
  
  return "(not available)";
}

void NetworkInterfaceDialog::OnClose(wxCommandEvent& event)
{
  EndModal(wxID_OK);
}
