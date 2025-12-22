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
#include "wx/sizer.h"
#include "wx/slider.h"
#include "wx/timer.h"
#include <wx/mstream.h>
#include <wx/image.h>
#include "api/oc_knx_dev.h"
#include "oc_knx_client.h"
#include "port/dns-sd.h"
#include "apps/hems/knx_iot_virtual_ems.h"
#include "apps/hems/icons/pv_ico.h"
#include <wx/clipbrd.h>
#include <wx/display.h>
#include <algorithm>

class CustomDialog : public wxDialog
{
public:
  CustomDialog(const wxString&, const wxString&);

private:
  void OnClose(wxCommandEvent& event);
};

void CustomDialog::OnClose(wxCommandEvent& event) { this->Destroy(); }

CustomDialog::CustomDialog(const wxString& title, const wxString& text) :
    wxDialog(NULL, wxID_ANY, title, wxDefaultPosition, wxDefaultSize,
             wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxMAXIMIZE_BOX)
{
  wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL);

  wxTextCtrl* tc =
    new wxTextCtrl(this, wxID_ANY, text, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxHSCROLL);

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
  for (auto& line : lines)
  {
    int w, h;
    dc.GetTextExtent(line, &w, &h);
    maxWidth = std::max(w, maxWidth);
  }

  // Estimated natural size
  int width = maxWidth + 75; // padding
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
  SetMinSize(wxSize(100, 100)); // reasonable min

  Centre();
  ShowModal();
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
  void OnReset(wxCommandEvent& event);
  void OnClearTables(wxCommandEvent& event);
  void OnExit(wxCommandEvent& event);
  void OnAbout(wxCommandEvent& event);
  void OnTimer(wxTimerEvent& event);

  void OnProcessSliderUpdate(wxCommandEvent& event);
  void updateDeviceData();

  wxMenu* m_menuFile;
  wxTimer m_timer;
  wxSlider* inverter_slider;
};

#ifdef USE_CONSOLE
  wxIMPLEMENT_APP_CONSOLE(MyApp);
#else
  wxIMPLEMENT_APP(MyApp);
#endif

bool MyApp::OnInit()
{
  // call in c-code
  app_initialize_stack("knx_iot_virtual_inverter");

  wxInitAllImageHandlers();

  auto frame = new MyFrame();

  frame->SetSize(350, 150);
  frame->Show(true);
  return true;
}

/**
 * @brief Construct a new My Frame:: My Frame object
 *
 */
MyFrame::MyFrame() : wxFrame(nullptr, wxID_ANY, "Inverter")
{  
  wxMemoryInputStream iconStream(pv_ico, pv_ico_len);
  wxImage img(iconStream, wxBITMAP_TYPE_ICO);
  wxBitmap bmp(img);
  wxIcon icon;
  icon.CopyFromBitmap(bmp);
  SetIcon(icon);

  // file menu
  m_menuFile = new wxMenu;
  m_menuFile->Append(LIST_ALL, "List All Tables", "List all tables in one window", false);
  m_menuFile->AppendSeparator();
  m_menuFile->Append(CHECK_PM, "Programming Mode", "Sets the application in programming mode", true);
  m_menuFile->Append(RESET_TABLE, "Reset (7) (Tables)", "Reset 7 (Reset to default without IA)", false);
  m_menuFile->Append(RESET, "Reset (2) (ex-factory)", "Reset 2 (Reset to default state)", false);
  m_menuFile->AppendSeparator();
  m_menuFile->Append(wxID_EXIT);

  // help menu
  const auto menuHelp = new wxMenu;
  menuHelp->Append(wxID_ABOUT, "About & Usage", "Show device information and usage instructions", false);

  // full menu bar
  const auto menuBar = new wxMenuBar;
  menuBar->Append(m_menuFile, "&File");
  menuBar->Append(menuHelp, "&Help");
  wxFrameBase::SetMenuBar(menuBar);
  wxFrameBase::CreateStatusBar();

  Bind(wxEVT_MENU, &MyFrame::OnListAll, this, LIST_ALL);
  Bind(wxEVT_MENU, &MyFrame::OnProgrammingMode, this, CHECK_PM);
  Bind(wxEVT_MENU, &MyFrame::OnReset, this, RESET);
  Bind(wxEVT_MENU, &MyFrame::OnClearTables, this, RESET_TABLE);
  Bind(wxEVT_MENU, &MyFrame::OnAbout, this, wxID_ABOUT);
  Bind(wxEVT_MENU, &MyFrame::OnExit, this, wxID_EXIT);

  // box + slider for inverter
  const auto mainSizer = new wxBoxSizer(wxVERTICAL);

  const auto label = new wxStaticText(this, wxID_ANY, "Present DC Power (0..10 kW)");
  mainSizer->Add(label, 0, wxALL, 5);

  inverter_slider = new wxSlider(this, wxID_SLIDER, 0, 0, 10, wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
  mainSizer->Add(inverter_slider, 0, wxEXPAND | wxALL, 5);
  SetSizerAndFit(mainSizer);

  // slider events
  inverter_slider->Bind(wxEVT_SCROLL_CHANGED, &MyFrame::OnProcessSliderUpdate, this);
  inverter_slider->Enable(true);
  inverter_slider->SetValue(0);
  
  // start the 1ms interval timer for UI updates and stack polls
  m_timer.Bind(wxEVT_TIMER, &MyFrame::OnTimer, this);
  m_timer.Start(1, wxTIMER_CONTINUOUS);
}

/**
 * @brief exit the application
 *
 * @param event command triggered by the framework
 */
void MyFrame::OnExit(wxCommandEvent& event) { Close(true); }

/**
 * @brief checks/unchecks the programming mode
 *
 * @param event command triggered by the menu button
 */
void MyFrame::OnProgrammingMode(wxCommandEvent& event)
{
  SetStatusText("Changing programming mode");

  bool my_val = m_menuFile->IsChecked(CHECK_PM);
  oc_device_info_t* device = oc_core_get_device_info();
  device->pm = my_val;

  // update the UI
  this->updateDeviceData();

  // update mdns
  knx_publish_service(oc_string(device->serialnumber), device->iid, device->ia, device->pm);
}

void MyFrame::updateDeviceData()
{
  oc_device_info_t* device = oc_core_get_device_info();

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

void MyFrame::OnListAll(wxCommandEvent& event)
{
  const oc_device_info_t* const device = oc_core_get_device_info();

  bool ga_conversion = true;
  bool grpid_conversion = true;
  bool iid_conversion = true;

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
  title.Printf("Inverter - Device & Tables - %s", oc_string(device->serialnumber));

  CustomDialog(title, all);
  SetStatusText("List Device & All Tables");
}

/**
 * @brief shows static info about the application
 *
 * @param event command triggered by a menu button
 */
void MyFrame::OnAbout(wxCommandEvent& event)
{
  wxString usage;
  usage << "The inverter simulates a solar solar production value (0-10 kW) and provides it to the CEM." << "\n"
        << "\n"
        << "(c) KNX Association, 2025";

  CustomDialog("About", usage);
}

/**
 * @brief update the UI on the timer ticks
 * updates:
 * - check boxes
 * - info buttons
 * - text buttons
 * does an oc_main_poll to give a tick to the stack
 * takes into account if the device is sleepy
 * e.g. then it only does an poll each 20 seconds
 * @param event triggered by a timer
 */
void MyFrame::OnTimer(wxTimerEvent& event)
{
  // stack polling 
  (void)oc_main_poll();

  // update possible user events
  this->updateDeviceData();
}

void MyFrame::OnProcessSliderUpdate(wxCommandEvent& event)
{
  // get the slider value in kW
  const float val = static_cast<float>(inverter_slider->GetValue());

  // set value in watt
  set_inverter_value(val * 1000);

  // get url
  char* url = app_retrieve_href_from_inverter();

  oc_send_s_mode_mc_or_uc_message(OC_SENDER_MULTICAST_SCOPE, url, "w");

  // show in status bar
  char statusBarText[100];

  // Present DC power of all solar panels connected to this PV control
  (void)sprintf(statusBarText, "Present DC solar power = %.02f kW", val);
  SetStatusText(statusBarText);
}
