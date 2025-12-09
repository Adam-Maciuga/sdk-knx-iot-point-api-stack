/*
-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
 Copyright (c) 2022-2023 Cascoda Ltd
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

//needs to be undefined so wx widgets will not use precompiled headers when compiling with msvc
#undef WX_PRECOMP

#include <wx/wxprec.h>
#include <wx/wx.h>
#include <wx/display.h>
#include "api/oc_knx_dev.h"
#include "oc_knx.h"
#include "apps/knx/knx_iot_virtual_knx.h"
#include "oc_knx_client.h"
#include "port/dns-sd.h"
#include "port/oc_network_interface.h"
#include "port/oc_storage.h"


extern lsxb_channel_t lsab[NUM_CHANNELS];

class CustomDialog : public wxDialog
{
public:
  CustomDialog(const wxString& title, const wxString& text);

private:
  void OnClose(wxCommandEvent& event);
};

CustomDialog::CustomDialog(const wxString& title, const wxString& text)
  : wxDialog(NULL, wxID_ANY, title,
             wxDefaultPosition, wxDefaultSize,
             wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxMAXIMIZE_BOX)
{
  wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL);

  wxTextCtrl* tc = new wxTextCtrl(this, wxID_ANY, text,
                                  wxDefaultPosition, wxDefaultSize,
                                  wxTE_MULTILINE | wxTE_READONLY | wxHSCROLL);

  vbox->Add(tc, 1, wxEXPAND | wxALL, 10);

  wxButton* closeButton = new wxButton(this, wxID_OK, "Close");
  closeButton->Bind(wxEVT_BUTTON, &CustomDialog::OnClose, this);
  vbox->Add(closeButton, 0, wxALIGN_CENTER | wxALL, 10);

  SetSizer(vbox);

  // ---- Compute content-based size ----
  wxClientDC dc(this);
  dc.SetFont(tc->GetFont());

  wxArrayString lines = wxSplit(text, '\n');
  int lineHeight = dc.GetCharHeight();
  int maxWidth = 0;
  for (auto& line : lines) {
    int w, h;
    dc.GetTextExtent(line, &w, &h);
    if (w > maxWidth) maxWidth = w;
  }

  // Estimated natural size
  int width = maxWidth + 75;                // padding
  int height = (lines.size() * lineHeight) + 150; // +button space

  // ---- Cap to 80% of screen ----
  wxDisplay display(wxDisplay::GetFromWindow(this));
  wxRect screenRect = display.GetGeometry();

  int maxW = screenRect.GetWidth() * 0.8;
  int maxH = screenRect.GetHeight() * 0.8;

  width = std::min(width, maxW);
  height = std::min(height, maxH);

  // Apply and allow resizing
  SetSize(width, height);
  SetMinSize(wxSize(100, 100));  // reasonable min

  Centre();
  ShowModal();
}

void CustomDialog::OnClose(wxCommandEvent& event)
{
  EndModal(wxID_OK);
}

class MyApp : public wxApp
{
public:
  virtual bool OnInit();
};

class MyFrame : public wxFrame
{
public:
  MyFrame();

private:
  void OnListAll(wxCommandEvent& event);
  void OnProgrammingMode(wxCommandEvent& event);
  void OnSleepyMode(wxCommandEvent& event);
  void OnReset(wxCommandEvent& event);
  void OnClearTables(wxCommandEvent& event);
  void OnRestartDevice(wxCommandEvent& event);
  void OnResolveIPv6Test(wxCommandEvent& event);
  void OnSendUnicastTest(wxCommandEvent& event);
  void OnNetworkInterfaces(wxCommandEvent& event);
  void OnExit(wxCommandEvent& event);
  void OnAbout(wxCommandEvent& event);
  void OnTimer(wxTimerEvent& event);
  void OnPressed_LSAB_SOO(wxCommandEvent& event);

  void updateCheckBoxesFromLiveIOOData();
  void updateDeviceData();

  wxMenu* m_menuFile;
  wxMenu* m_menuDisplay;
  wxMenu* m_menuOptions;
  wxTimer m_timer;

  // sleepy information
  int m_sleep_counter = 0;
  int m_sleep_milliseconds = 20000;

  // non static device properties
  wxTextCtrl* m_ia_text; // text control for internal address
  wxTextCtrl* m_iid_text; // text control for installation id
  wxTextCtrl* m_pm_text; // text control for programming mode
  wxTextCtrl* m_ls_text; // text control for load state
  wxTextCtrl* m_hn_text; // text control for host name

  // eitt
  wxButton *m_EITT_SOO;
  wxButton *m_IPV6_RESOLVE_TEST;
  wxButton *m_UNICAST_TEST;
};
#ifdef USE_CONSOLE
  wxIMPLEMENT_APP_CONSOLE(MyApp);
#else
  wxIMPLEMENT_APP(MyApp);
#endif

/**
 * @brief initialization of the application
 *
 * @return true
 * @return false
 */
bool MyApp::OnInit()
{
  // call in c-code
  app_initialize_stack("knx_iot_virtual_eitt");

  // reset the device (for EITT tests)
  oc_knx_device_storage_reset(RESET_TO_DEFAULT_STATE);

  MyFrame* frame = new MyFrame();

  frame->Fit();
  frame->Show(true);
  return true;
}

/**
 * @brief Construct a new My Frame:: My Frame object
 *
 */
MyFrame::MyFrame() : wxFrame(nullptr, wxID_ANY, "KNX EITT test application")
{
  m_menuFile = new wxMenu;
  m_menuFile->Append(LIST_ALL, "List All Tables", "List all tables in one window", false);
  m_menuFile->Append(CHECK_PM, "Programming Mode", "Sets the application in programming mode", true);
  m_menuFile->Append(RESET_TABLE, "Reset (7) (Tables)", "Reset 7 (Reset to default without IA).", false);
  m_menuFile->Append(RESET, "Reset (2) (ex-factory)", "Reset 2 (Reset to default state)", false);
  m_menuFile->Append(RESTART_DEVICE, "Restart Device", "Simulate a device restart", false);
  m_menuFile->AppendSeparator();
  m_menuFile->Append(NETWORK_INTERFACES, "Network Interfaces...", "Configure network interface selection", false);
  m_menuFile->AppendSeparator();
  m_menuFile->Append(wxID_EXIT);

  // display menu
  m_menuDisplay = new wxMenu;
  m_menuDisplay->Append(CHECK_GA_DISPLAY, "GA 3-level (ETS)", "Displays as GA 3-Level or as integer", true);
  m_menuDisplay->Check(CHECK_GA_DISPLAY, true);
  m_menuDisplay->Append(CHECK_GRPID_DISPLAY, "GRPID as partial ipv6 address (ETS)", "Displays the grpid as integer", true);
  m_menuDisplay->Check(CHECK_GRPID_DISPLAY, true);
  m_menuDisplay->Append(CHECK_IID_DISPLAY, "IID as partial ipv6 address (ETS)", "Displays the iid as integer", true);
  m_menuDisplay->Check(CHECK_IID_DISPLAY, true);

  // option menu
  m_menuOptions = new wxMenu;
  m_menuOptions->Append(CHECK_SLEEPY, "Act as Sleepy Device", "Sleeps for 20 seconds", true);
  m_menuOptions->Check(CHECK_SLEEPY, false);

  // help menu
  wxMenu* menuHelp = new wxMenu;
  menuHelp->Append(wxID_ABOUT);

  // full menu bar
  wxMenuBar* menuBar = new wxMenuBar;
  menuBar->Append(m_menuFile, "&File");
  menuBar->Append(m_menuDisplay, "&Display");
  menuBar->Append(m_menuOptions, "&Options");
  menuBar->Append(menuHelp, "&Help");
  wxFrameBase::SetMenuBar(menuBar);
  wxFrameBase::CreateStatusBar();
  wxFrameBase::SetStatusText("Welcome to EITT certification!");

  Bind(wxEVT_MENU, &MyFrame::OnReset, this, RESET);
  Bind(wxEVT_MENU, &MyFrame::OnClearTables, this, RESET_TABLE);
  Bind(wxEVT_MENU, &MyFrame::OnListAll, this, LIST_ALL);
  Bind(wxEVT_MENU, &MyFrame::OnProgrammingMode, this, CHECK_PM);
  Bind(wxEVT_MENU, &MyFrame::OnSleepyMode, this, CHECK_SLEEPY);
  Bind(wxEVT_MENU, &MyFrame::OnReset, this, RESET);
  Bind(wxEVT_MENU, &MyFrame::OnRestartDevice, this, RESTART_DEVICE);
  Bind(wxEVT_MENU, &MyFrame::OnNetworkInterfaces, this, NETWORK_INTERFACES);
  Bind(wxEVT_MENU, &MyFrame::OnAbout, this, wxID_ABOUT);
  Bind(wxEVT_MENU, &MyFrame::OnExit, this, wxID_EXIT);

  int x_width = 100; // width of the widgets
  int x_height = 25; // height of the widgets
  int max_instances = 4; // number of channels before the rest is shown
  int row;
  int column;

  // eitt - sensor
  {
    row = 0;
    column = 0;

    new wxStaticText(this, wxID_ANY, "EITT | Sensor",
                     wxPoint(10 + column * x_width, 10 + x_height * row),
                     wxSize(x_width, x_height), wxALIGN_LEFT);

    // SOO control button
    m_EITT_SOO = new wxButton(this, EITT_SOO, _T("SOO, press me ..."),
                                wxPoint(120 + column * x_width, 10 + x_height * row), wxSize(x_width, x_height), 0);
    m_EITT_SOO->Bind(wxEVT_BUTTON, &MyFrame::OnPressed_LSAB_SOO, this);
    m_EITT_SOO->Enable(true);
    
    // IPv6 resolution test button
    row++;
    new wxStaticText(this, wxID_ANY, "Test Functions",
                     wxPoint(10 + column * x_width, 10 + x_height * row),
                     wxSize(x_width, x_height), wxALIGN_LEFT);
    
    m_IPV6_RESOLVE_TEST = new wxButton(this, RESOLVE_IPV6_TEST, _T("Resolve IPv6 Test"),
                                wxPoint(120 + column * x_width, 10 + x_height * row), wxSize(x_width, x_height), 0);
    m_IPV6_RESOLVE_TEST->Bind(wxEVT_BUTTON, &MyFrame::OnResolveIPv6Test, this);
    m_IPV6_RESOLVE_TEST->Enable(true);
    
    // Unicast test button
    row++;
    m_UNICAST_TEST = new wxButton(this, SEND_UNICAST_TEST, _T("Send Unicast Test"),
                                wxPoint(120 + column * x_width, 10 + x_height * row), wxSize(x_width, x_height), 0);
    m_UNICAST_TEST->Bind(wxEVT_BUTTON, &MyFrame::OnSendUnicastTest, this);
    m_UNICAST_TEST->Enable(true);
  }

  constexpr int width_size = 220; // size of the knx info widgets
  char text[500];

  // serial number
  const oc_device_info_t* const  device = oc_core_get_device_info();
  (void)sprintf(text, "SN:\t%s", oc_string(device->serialnumber));

  wxTextCtrl* static_text0 = new wxTextCtrl(this, wxID_ANY, text, wxPoint(10, 10 + ((max_instances + 2) * x_height)),
                                            wxSize(width_size * 2, x_height), 0);
  static_text0->SetEditable(false);

  /* QR code
    KNX:S:serial number;P:password
    where:
    KNX: is a fixed prefix
    S: means a KNX serial number follows, sn itself is encoded as
       12 upper-case hexadecimal characters
    P: means a password follows, password itself is just
       the KNX IoT Point API password;
       this works as the allowed password characters do not interfere
       with the separator characters colon and semicolon and are in the alphanumeric range.
  */
  (void)sprintf(text, "QR:\tKNX:S:%s;P:%s", oc_string(device->serialnumber), app_get_password());
  app_str_to_upper(text);

  wxTextCtrl* static_text1 = new wxTextCtrl(this, wxID_ANY, text, wxPoint(10, 10 + ((max_instances + 3) * x_height)),
                                            wxSize(width_size * 2, x_height), 0);
  static_text1->SetEditable(false);

  // individual address, displayed data set/refreshed later
  m_ia_text =
    new wxTextCtrl(this, IA_TEXT, "", wxPoint(10, 10 + ((max_instances + 4) * x_height)), wxSize(width_size, x_height), 0);
  m_ia_text->SetEditable(false);

  // installation id, displayed data set/refreshed later
  m_iid_text = new wxTextCtrl(this, IID_TEXT, "", wxPoint(10 + width_size, 10 + ((max_instances + 4) * x_height)),
                              wxSize(width_size, x_height), 0);
  m_iid_text->SetEditable(false);

  // programming mode, displayed data set/refreshed later
  m_pm_text =
    new wxTextCtrl(this, PM_TEXT, "", wxPoint(10, 10 + ((max_instances + 5) * x_height)), wxSize(width_size, x_height), 0);
  m_pm_text->SetEditable(false);

  // installation id, displayed data set/refreshed later
  m_ls_text =
    new wxTextCtrl(this, LS_TEXT, "", wxPoint(10 + width_size, 10 + ((max_instances + 5) * 25)), wxSize(width_size, 25), 0);
  m_ls_text->SetEditable(false);

  // hostname, displayed data set/refreshed later
  m_hn_text = new wxTextCtrl(this, LS_TEXT, "", wxPoint(10, 10 + ((max_instances + 6) * 25)), wxSize(width_size, 25), 0);
  m_hn_text->SetEditable(false);

  // SPAKE2+ pwd
  (void)sprintf(text, "PWD:\t%s", app_get_password());
  wxTextCtrl* static_text2 = new wxTextCtrl(this, LS_TEXT, text, wxPoint(10 + width_size, 10 + ((max_instances + 6) * 25)),
                                            wxSize(width_size, 25), 0);
  static_text2->SetEditable(false);

  // update the UI
  this->updateDeviceData();
  this->updateCheckBoxesFromLiveIOOData();

  // Calculate bounding box of all children to make Window correct size
  int maxRight = 0;
  int maxBottom = 0;

  for (wxWindowList::iterator it = GetChildren().begin(); it != GetChildren().end(); ++it)
  {
      wxWindow* child = *it;
      if (child) {
          wxRect rect = child->GetRect();
          maxRight = std::max(maxRight, rect.GetRight());
          maxBottom = std::max(maxBottom, rect.GetBottom());
      }
  }

  // Add some padding (status bar, borders, etc.)
  int paddingX = 40;
  int paddingY = 60;

  // Set minimum size dynamically
  this->SetMinSize(wxSize(maxRight + paddingX, maxBottom + paddingY));
  this->SetSize(this->GetMinSize());
  
  // start the 1ms interval timer for UI updates and stack polls
  m_timer.Bind(wxEVT_TIMER, &MyFrame::OnTimer, this);
  m_timer.Start(1, wxTIMER_CONTINUOUS);
}

/**
 * @brief exit the application
 *
 * @param event command triggered by the framework
 */
void MyFrame::OnExit(wxCommandEvent& event)
{
  oc_main_shutdown();
  Close(true);
}

/**
 * @brief checks/unchecks the programming mode
 *
 * @param event command triggered by the menu button
 */
void MyFrame::OnProgrammingMode(wxCommandEvent& event)
{
  SetStatusText("Changing programming mode");

  bool my_val = m_menuFile->IsChecked(CHECK_PM);
  oc_device_info_t* const device = oc_core_get_device_info();
  device->pm = my_val;

  // update the UI
  this->updateDeviceData();
  // update mdns
  knx_publish_service(oc_string(device->serialnumber), device->iid, device->ia, device->pm);
}

/**
 * @brief checks/unchecks the sleepy mode
 *
 * @param event command triggered by the menu button
 */
void MyFrame::OnSleepyMode(wxCommandEvent& event)
{
  SetStatusText("Changing sleepy mode");

  bool my_sleepy = m_menuOptions->IsChecked(CHECK_SLEEPY);
  const oc_device_info_t* const  device = oc_core_get_device_info();

  if (my_sleepy)
  {
    knx_service_sleep_period(20);
  }
  else
  {
    knx_service_sleep_period(0);
  }
  // update mdns
  knx_publish_service(oc_string(device->serialnumber), device->iid, device->ia, device->pm);
}

/**
 * @brief update the text buttons
 * - IA
 * - Loadstate
 * - programming mode
 * - IID
 * - Hostname
 */
void MyFrame::updateDeviceData()
{

  char text[500];

  bool iid_conversion = m_menuDisplay->IsChecked(CHECK_IID_DISPLAY);

  // get the device data structure
  const oc_device_info_t* const  device = oc_core_get_device_info();

  const uint16_t ia_a = device->ia >> 12;       // area
  const uint16_t ia_l = device->ia >> 8 & 0xF;  // line
  const uint16_t ia_d = device->ia & 0x00FF;    // device
  (void)sprintf(text, "IA:\t%d.%d.%d [%d]", ia_a, ia_l, ia_d, device->ia);
  m_ia_text->SetValue(text);

  (void)sprintf(text, "LSM:\t%s", oc_core_get_lsm_state_as_string(device->lsm_s));
  m_pm_text->SetValue(text);

  (void)sprintf(text, "PM:\t%s", device->pm ? "on" : "off");
  m_ls_text->SetValue(text);

  strcpy(text, "IID:\t");
  util_int2grpid_text(device->iid, text, iid_conversion);
  m_iid_text->SetValue(text);

  (void)sprintf(text, "HOST:\t%s", oc_string_checked(device->iot_hostname));
  m_hn_text->SetValue(text);

  // set in menu the programming mode to what the device has
  m_menuFile->Check(CHECK_PM, device->pm);
}

/**
 * @brief clear the tables of the device
 *
 * @param event command triggered by button in the menu
 */
void MyFrame::OnClearTables(wxCommandEvent& event)
{
  SetStatusText("Clear Tables");
  // reset the device
  oc_knx_device_storage_reset(RESET_TO_DEFAULT_WO_IA);
  // update the UI
  this->updateDeviceData();
}

/**
 * @brief reset the device
 *
 * @param event command triggered by button in the menu
 */
void MyFrame::OnReset(wxCommandEvent& event)
{
  SetStatusText("Device Reset");
  // reset the device
  oc_knx_device_storage_reset(RESET_TO_DEFAULT_STATE);
  // update the UI
  this->updateDeviceData();
}

/**
 * @brief initiate a restart of the stack 
 *
 * @param event command triggered by the gui menu
 */
void MyFrame::OnRestartDevice(wxCommandEvent& event)
{
  SetStatusText("Restarting...");

  // Use the new public API to trigger the exact same restart as the KNX stack
  oc_knx_device_restart();

  // Update the UI immediately (restart happens asynchronously)
  this->updateDeviceData();
  this->updateCheckBoxesFromLiveIOOData();

  SetStatusText("Restart Initiated");
}

void MyFrame::OnNetworkInterfaces(wxCommandEvent& event)
{
  NetworkInterfaceDialog dialog(this);
  dialog.ShowModal();
  SetStatusText(NetworkInterfaceDialog::GetStatusMessage());
}

/**
 * @brief Test IPv6 resolution via CoAP discovery
 *
 * Sends CoAP GET to ff02::fd with query ep=knx://ia.1199887766.110F
 */
void MyFrame::OnResolveIPv6Test(wxCommandEvent& event)
{
  SetStatusText("Sending CoAP Discovery...");

  // Test parameters
  uint32_t target_ia = 0x110F;
  uint64_t target_iid = 0x1199887766ULL;

  wxString result;
  result += "CoAP Discovery Test\n";
  result += "===================\n\n";
  result += wxString::Format("Target: knx://ia.%llX.%X\n", (unsigned long long)target_iid, (unsigned int)target_ia);
  result += wxString::Format("IID: 0x%llX (%llu)\n", (unsigned long long)target_iid, (unsigned long long)target_iid);
  result += wxString::Format("IA: 0x%X (%u)\n\n", (unsigned int)target_ia, (unsigned int)target_ia);

  result += "Sending CoAP GET to: ff02::fd\n";
  result += "Query: /.well-known/core?ep=knx://ia.1199887766.110F\n\n";

  // Send discovery (recipient_index = -1 for test mode)
  int ret = knx_resolve_via_coap_discovery(target_ia, target_iid, -1);

  if (ret == 0) {
    result += "✓ Discovery packet sent successfully!\n\n";
    result += "Check console for response logs.\n";
    result += "The response handler will log any received responses.";
    SetStatusText("Discovery Sent");
  } else {
    result += wxString::Format("✗ Failed to send discovery! Error code: %d\n", ret);
    SetStatusText("Send Failed");
  }

  CustomDialog dialog("CoAP Discovery Test", result);
}

void MyFrame::OnSendUnicastTest(wxCommandEvent& event)
{
  SetStatusText("Sending Unicast Test...");

  // Test parameters - search for recipient with GA 65535
  const uint32_t target_ga = 65535;  // Group Address from test spec

  wxString result;
  result += "Unicast S-Mode Test\n";
  result += "===================\n\n";
  result += wxString::Format("Group Address: %u (31/7/255)\n\n", target_ga);

  // Get device info for sender IA
  const oc_device_info_t* device = oc_core_get_device_info();
  if (!device) {
    result += "✗ Failed to get device info\n";
    SetStatusText("Send Failed");
    CustomDialog dialog("Unicast Test Error", result);
    return;
  }

  // Find recipient table entry with GA=65535
  int total = oc_core_get_recipient_table_size();
  oc_group_table_t* recipient_entry = NULL;
  int recipient_index = -1;
  
  for (int i = 0; i < total; i++) {
    oc_group_table_t* entry = oc_core_get_recipient_table_entry(i);
    if (entry && entry->id >= 0) {
      // Check if this entry has GA 65535
      for (int j = 0; j < entry->ga_len; j++) {
        if (entry->ga[j] == target_ga) {
          recipient_entry = entry;
          recipient_index = i;
          break;
        }
      }
      if (recipient_entry) break;
    }
  }

  if (!recipient_entry) {
    result += "\u2717 No recipient table entry found with GA=65535\n";
    result += "\nPlease configure recipient table with GA [65535]\n";
    SetStatusText("No Recipient Entry");
    CustomDialog dialog("Unicast Test Error", result);
    return;
  }

  result += wxString::Format("Found Recipient Entry (id=%d):\n", recipient_entry->id);
  result += wxString::Format("  Destination IA: %u (0x%X)\n", recipient_entry->ia, recipient_entry->ia);
  result += wxString::Format("  AT: %s\n\n", oc_string_checked(recipient_entry->at));

  // Use device's own IID for discovery (not recipient table's IID)
  uint64_t device_iid = device->iid;
  result += wxString::Format("Using device IID for discovery: %llu (0x%llX)\n\n", 
                             (unsigned long long)device_iid, (unsigned long long)device_iid);

  // Check resolution status
  if (recipient_entry->ipadd.init_status != OC_IP_STATUS_RESOLVED) {
    result += "IPv6 not yet resolved - will trigger resolution and queue message...\n\n";
  } else {
    result += "Resolved IPv6: ";
    for (int i = 0; i < 16; i++) {
      result += wxString::Format("%02x", recipient_entry->ipadd.ipv6[i]);
      if (i % 2 == 1 && i < 15) result += ":";
    }
    result += "\n\n";
  }

  result += "Sending unicast s-mode message...\n";

  // Get the resource path
  char* url = app_retrieve_href_from_channel(1, SOO);
  
  // Get resource value using the same method as NON messages
  uint8_t resource_value_buffer[OC_MAX_APP_DATA_SIZE_STATIC];
  int resource_value_size = 0;
  
  // Call the resource GET handler to get CBOR-encoded value
  const oc_resource_t* resource = oc_ri_get_app_resource_by_resource_path(url, strlen(url));
  if (resource && resource->get_handler.cb) {
    oc_request_t request_obj = {0};
    oc_response_t response_obj = {0}; 
    oc_response_buffer_t response_buffer = {0};
    
    response_buffer.buffer = resource_value_buffer;
    response_buffer.buffer_size = sizeof(resource_value_buffer);
    response_obj.response_buffer = &response_buffer;
    request_obj.response = &response_obj;
    request_obj.resource = resource;
    request_obj.accept = APPLICATION_CBOR;
    
    // Initialize CBOR encoder
    oc_rep_new(response_buffer.buffer, response_buffer.buffer_size);
    
    // Call GET handler
    resource->get_handler.cb(&request_obj, resource->get_handler.interface_mask, resource->get_handler.user_data);
    resource_value_size = oc_rep_get_encoded_payload_size();
  }
  
  // Send confirmable message (stack handles resolution and queuing automatically)
  int send_result = oc_send_s_mode_confirmable_unicast_message(recipient_entry->ia, target_ga,
                                                                 "w", resource_value_buffer, resource_value_size,
                                                                 recipient_index);
  
  if (send_result == 0) {
    if (recipient_entry->ipadd.init_status == OC_IP_STATUS_RESOLVED) {
      result += "\n\u2713 CON unicast message sent!\n";
    } else {
      result += "\n\u2713 Message queued, IPv6 resolution triggered!\n";
      result += "Message will be sent automatically after resolution completes.\n";
    }
  } else {
    result += wxString::Format("\n\u2717 Failed to send/queue message (error %d)\n", send_result);
  }
  
  result += "\nMessage Format:\n";
  result += "  Endpoint: /k\n";
  result += "  Method: POST CON (confirmable)\n";
  result += "  Secured: yes (OSCORE)\n";
  result += wxString::Format("  SIA (sender): %u\n", device->ia);
  result += wxString::Format("  Destination IA: %u\n", recipient_entry->ia);
  result += wxString::Format("  GA: %u\n", target_ga);
  result += "  ST: w (write)\n";
  result += wxString::Format("  Resource: %s\n", url);

  SetStatusText(send_result == 0 ? "Unicast Test Sent" : "Send Failed");
  CustomDialog dialog("Unicast Test", result);
}

/**
 * @brief shows all tables combined in a window
 *
 * Opens a CustomDialog and concatenates the outputs of:
 * - Group Object Table
 * - Publisher Table
 * - Recipient Table
 * - Parameter List
 * - Auth/AT Table
 *
 * Each section is separated with headers.
 *
 * @param event command triggered by the menu button
 */
void MyFrame::OnListAll(wxCommandEvent& event)
{
  const oc_device_info_t* const  device = oc_core_get_device_info();

  bool ga_conversion = m_menuDisplay->IsChecked(CHECK_GA_DISPLAY);
  bool grpid_conversion = m_menuDisplay->IsChecked(CHECK_GRPID_DISPLAY);
  bool iid_conversion = m_menuDisplay->IsChecked(CHECK_IID_DISPLAY);

  wxString all;
  all << util_dumpQRCode()     << "\n\n"  
      << util_dumpDeviceIDs()  << "\n\n"
      << util_dumpLsmState()   << "\n\n"
      << util_dumpGroupObjectTable(ga_conversion) << "\n\n"
      << util_dumpPublisherTable(ga_conversion, grpid_conversion, iid_conversion)   << "\n\n"
      << util_dumpRecipientTable(ga_conversion, grpid_conversion, iid_conversion)   << "\n\n"
      << util_dumpParameterList()    << "\n\n"
      << util_dumpAuthTable(ga_conversion);

  wxString title;
  title.Printf("EITT - All Tables - %s", oc_string(device->serialnumber));

  CustomDialog(title, all);
  SetStatusText("List All Tables");
}
 
/**
 * @brief shows static info about the application
 *
 * @param event command triggered by a menu button
 */
void MyFrame::OnAbout(wxCommandEvent& event)
{
  constexpr char text[] = "(c) KNX Association, 2025-05-13";
  CustomDialog("About", text);
}

/**
 * @brief update the UI on the timer ticks
 * updates:
 * - check boxes
 * - info buttons
 * - text buttons
 * does an oc_main_poll to give a tick to the stack
 * takes into account if the device is sleepy
 * e.g. then it only does a poll each 20 seconds
 * @param event triggered by a timer
 */
void MyFrame::OnTimer(wxTimerEvent& event)
{
  bool do_poll = true;

  const bool sleepy = m_menuOptions->IsChecked(CHECK_SLEEPY);

  // do whatever you want to do every millisecond here
  if (sleepy)
  {
    do_poll = false;
    m_sleep_counter++;

    if (m_sleep_counter > m_sleep_milliseconds)
    {
      // only do a poll each x (20) seconds
      do_poll = true;
      m_sleep_counter = 0;
    }
    if (oc_knx_device_in_programming_mode())
    {
      // make sure that the device is reactive in programming mode, so keep on polling
      do_poll = true;
    }
  }

  if (do_poll)
  {
    (void)oc_main_poll();
  }

  // update possible events
  this->updateCheckBoxesFromLiveIOOData();
  this->updateDeviceData();
}

/**
 * @brief update the UI e.g. check boxes in the UI
 * updates:
 * does a oc_main_poll to give a tick to the stack
 *
 * @param event triggered by a timer
 */
void MyFrame::updateCheckBoxesFromLiveIOOData()
{

  // no check boxes so far
}

void MyFrame::OnPressed_LSAB_SOO(wxCommandEvent& event)
{
  // get url from SOO (channel 1 out of 2) as defined in EITT template
  char* url = app_retrieve_href_from_channel(1, SOO);
  bool p = app_retrieve_bool_variable_from_channel(1, SOO);

  // toggle value
  p = !p;

  // set value
  app_set_bool_variable_from_channel(1, SOO, p);

  // send out, multicast
  oc_send_s_mode_mc_or_uc_message(SENDER_SCOPE, url, "w");

  // update button text
  char text[200];
  strcpy(text, "SOO = ");

  util_bool2text(p, text);
  m_EITT_SOO->SetLabel(text);

  // show in status bar
  char statusBarText[100];
  (void)sprintf(statusBarText, "Switch On/Off @ '%s' pressed: %s", url, p ? "On" : "Off");
  SetStatusText(statusBarText);
}

