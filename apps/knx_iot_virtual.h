/* 
 * Copyright (c) 2022-2023 Cascoda Ltd
 * Copyright (c) 2024-2026 KNX Association
 *            
 * SPDX-License-Identifier: Apache-2.0
 */     

/*
 * Note that the file 'knx_iot_virtual.c/h' is NOT a part of the stack or not intended to be an 
 * 'application' library. It hosts only for the application demos commonly used functionality in one place.
 */

#ifndef KNX_IOT_VIRTUAL_H
#define KNX_IOT_VIRTUAL_H

#include <wx/defs.h> // for wxID_HIGHEST
#include <wx/dialog.h>
#include <wx/string.h>

// C-style IDs for the controls and the menu commands in C++ files
enum controls : uint16_t
{
  RESET = wxID_HIGHEST, // ID for reset button in the menu
  RESET_TABLE,          // ID for clear table button in the menu
  IA_TEXT,              // ID for internal address text
  IID_TEXT,             // ID for installation id text
  PM_TEXT,              // ID for programming mode text
  LS_TEXT,              // ID for load status text
  CHECK_GA_DISPLAY,     // ga display check
  CHECK_IID_DISPLAY,    // iid display check
  CHECK_GRPID_DISPLAY,  // grpid display check
  CHECK_SLEEPY,         // sleepy check
  CHECK_PM,             // programming mode check in menu bar
  LIST_ALL,             // list all tables (GO/PUB/RCP/AT)
  RESTART_DEVICE,       // restart device
  NETWORK_INTERFACES,   // network interfaces dialog
  REFRESH_INTERFACES,   // refresh network interface
  GET_NEW_PORTS,        // get new network ports

  EITT_SOO,             // EITT test button
  wxID_SLIDER,          // EMS Inverter slider
  LSSB_0_SOO,           // LSSB switch, channel 0
  LSSB_0_IOO,           // LSSB info, channel 0
  LSSB_1_SOO,           // LSSB switch, channel 1
  LSSB_1_IOO,           // LSSB info, channel 1
  LSAB_0_SOO,           // LSAB switch, channel 0
  LSAB_1_SOO,           // LSAB switch, channel 1
};

// Forward declarations
class wxWindow;
class wxComboBox;
class wxButton;
class wxTextCtrl;
class wxCommandEvent;

// Network Interface Dialog class
class NetworkInterfaceDialog : public wxDialog
{
public:
  NetworkInterfaceDialog(wxWindow* parent);
  
  // Get status message for current interface selection
  static wxString GetStatusMessage();

private:
  void OnRefresh(wxCommandEvent& event);
  void OnInterfaceChange(wxCommandEvent& event);
  void OnClose(wxCommandEvent& event);
  wxString GetIPv6AddressForInterface(int if_index);
  void PopulateInterfaces();

  wxComboBox* m_interface_combo;
  wxButton* m_refresh_btn;
  wxTextCtrl* m_ipv6_text;
};

// Utility functions for dumping tables (implemented in knx_iot_virtual.cpp)

/**
 * @brief Dump QR Code information
 *
 * @return wxString containing formatted QR Code
 */
wxString util_dumpQRCode();

/**
 * @brief Dump device identification information
 *
 * @return wxString containing formatted device IDs
 */
wxString util_dumpDeviceIDs();

/**
 * @brief Dump the Group Object Table into a string
 *
 * @param ga_conversion convert GA to ETS 3-level format
 * @return wxString containing formatted Group Object Table
 */
wxString util_dumpGroupObjectTable(bool ga_conversion);

/**
 * @brief Dump the Publisher Table into a string
 *
 * @param ga_conversion convert GA to ETS 3-level format
 * @param grpid_conversion convert grpid to hex format
 * @param iid_conversion convert iid to hex format
 * @return wxString containing formatted Publisher Table
 */
wxString util_dumpPublisherTable(bool ga_conversion, bool grpid_conversion, bool iid_conversion);

/**
 * @brief Dump the Recipient Table into a string
 *
 * @param ga_conversion convert GA to ETS 3-level format
 * @param grpid_conversion convert grpid to hex format
 * @param iid_conversion convert iid to hex format
 * @return wxString containing formatted Recipient Table
 */
wxString util_dumpRecipientTable(bool ga_conversion, bool grpid_conversion, bool iid_conversion);

/**
 * @brief Dump the Parameter List into a string
 *
 * @return wxString containing formatted Parameter List
 */
wxString util_dumpParameterList();

/**
 * @brief Dump the Auth/AT Table into a string
 *
 * @param ga_conversion convert GA to ETS 3-level format
 * @return wxString containing formatted Auth/AT Table
 */
wxString util_dumpAuthTable(bool ga_conversion);

/**
 * @brief returns load state information
 *
 * @return wxString containing lsm state info
 */
wxString util_dumpLsmState();

#endif
