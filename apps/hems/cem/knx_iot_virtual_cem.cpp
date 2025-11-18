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

#include <wx/cmdline.h>
#include <wx/scrolbar.h>
#include <wx/wxprec.h>
#include <wx/wx.h>
#include "wx/config.h"
#include "wx/dnd.h"
#include "wx/filedlg.h"
#include "wx/filename.h"
#include "wx/listctrl.h"
#include "wx/mediactrl.h"
#include "wx/notebook.h"
#include "wx/sizer.h"
#include "wx/slider.h"
#include "wx/textdlg.h"
#include "wx/timer.h"
#include "wx/vector.h"
#include <wx/stdpaths.h>
#include <wx/artprov.h>
#include <wx/mstream.h>
#include <wx/image.h>
#include <wx/bitmap.h>
#include <wx/valnum.h>

#include "api/oc_knx_dev.h"
#include "api/oc_knx_fp.h"
#include "api/oc_knx_sec.h"
#include "apps/knx_iot_virtual.h"
#include "oc_knx_client.h"
#include "port/dns-sd.h"
#include "oc_oscore_context.h"

#include "apps/hems/knx_iot_virtual_ems.h"
#include "apps/hems/icons/cem_ico.h"

#include <wx/clipbrd.h>


class CustomDialog : public wxDialog
{
public:
  CustomDialog(const wxString&, const wxString&, int, int);

private:
  wxTextCtrl* inputField = nullptr;
  void on_close(wxCommandEvent& event);
  void on_set_link(wxCommandEvent& event);
  void on_reset_link(wxCommandEvent& event);
};
void CustomDialog::on_close(wxCommandEvent& event) { this->Destroy(); }

void CustomDialog::on_set_link(wxCommandEvent& event)
{
  if (inputField)
  {
    wxString sn_link = inputField->GetValue();

    unsigned char sn_L[6];
    int len;

    for (size_t i = 0; i < 6; i++)
      sn_L[i] = 0x00;

    len = strlen(sn_link);

    if (len % 2 == 0)
    {
      if (len / 2 == 6)
      {
        for (size_t i = 0; i < 6; i++)
        {
          char buf[3] = {sn_link[2 * i], sn_link[2 * i + 1], '\0'}; // 2 hex chars
          sn_L[i] = (unsigned char)strtol(buf, NULL, 16);
        }
      }
      else
      {
        return;
      }
    }
    else
    {
      return;
    }

    uint64_t sn;

    sn = 0x00;
    sn += sn_L[0];
    sn <<= 8;
    sn += sn_L[1];
    sn <<= 8;
    sn += sn_L[2];
    sn <<= 8;
    sn += sn_L[3];
    sn <<= 8;
    sn += sn_L[4];
    sn <<= 8;
    sn += sn_L[5];

    // Allocate enough space: 2 hex chars per byte + 1 for null terminator
    char str[13]; // 6 bytes → 12 hex chars + '\0'

    // Format with leading zeros
    snprintf(str, sizeof(str), "%012llx", (unsigned long long)sn);

    // CEM_init_tables(str);

    
    char* url = app_retrieve_href_from_cem_inverter();
    oc_send_s_mode_mc_or_uc_message(SENDER_SCOPE, url, "w");
  }

  this->Destroy();
}
void CustomDialog::on_reset_link(wxCommandEvent& event)
{
  if (inputField)
  {
    wxString sn_link = inputField->GetValue();

    unsigned char sn_L[6];
    int len;

    for (size_t i = 0; i < 6; i++)
      sn_L[i] = 0x00;

    len = strlen(sn_link);

    if (len % 2 == 0)
    {
      if (len / 2 == 6)
      {
        for (size_t i = 0; i < 6; i++)
        {
          char buf[3] = {sn_link[2 * i], sn_link[2 * i + 1], '\0'}; // 2 hex chars
          sn_L[i] = (unsigned char)strtol(buf, NULL, 16);
        }
      }
      else
      {
        return;
      }
    }
    else
    {
      return;
    }

    uint64_t sn;

    sn = 0x00;
    sn += sn_L[0];
    sn <<= 8;
    sn += sn_L[1];
    sn <<= 8;
    sn += sn_L[2];
    sn <<= 8;
    sn += sn_L[3];
    sn <<= 8;
    sn += sn_L[4];
    sn <<= 8;
    sn += sn_L[5];

    // Allocate enough space: 2 hex chars per byte + 1 for null terminator
    char str[13]; // 6 bytes → 12 hex chars + '\0'

    // Format with leading zeros
    snprintf(str, sizeof(str), "%012llx", (unsigned long long)sn);

    //CEM_init_tables(str);

    
     char* url = app_retrieve_href_from_cem_inverter();
    oc_send_s_mode_mc_or_uc_message(SENDER_SCOPE, url, "w");
  }

  this->Destroy();
}

CustomDialog::CustomDialog(const wxString& title, const wxString& text, int size_x, int size_y) :
    wxDialog(NULL, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{  
  this->SetSize(wxSize(size_x + 30, size_y));
  
  wxPanel* panel = new wxPanel(this, -1);

  wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL);
  wxBoxSizer* hbox = new wxBoxSizer(wxHORIZONTAL);

  wxTextCtrl* tc = new wxTextCtrl(panel, -1, text, wxPoint(10, 10), wxSize(size_x, size_y), wxTE_MULTILINE | wxTE_READONLY);

  wxButton* closeButton = new wxButton(this, -1, wxT("Close"), wxDefaultPosition, wxDefaultSize);
  closeButton->Bind(wxEVT_BUTTON, &CustomDialog::on_close, this);

  hbox->Add(closeButton, 1, wxLEFT, 5);
  vbox->Add(panel, 1);
  vbox->Add(hbox, 0, wxALIGN_CENTER | wxTOP | wxBOTTOM, 10);

  SetSizerAndFit(vbox);
  Centre();
  ShowModal();
  Destroy();
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
  void OnUsage(wxCommandEvent& event);

  void OnListAll(wxCommandEvent& event);

  void OnProgrammingMode(wxCommandEvent& event);
  void OnReset(wxCommandEvent& event);
  void OnClearTables(wxCommandEvent& event);
  void OnExit(wxCommandEvent& event);
  void OnAbout(wxCommandEvent& event);
  void OnTimer(wxTimerEvent& event);

  void ProcessUpdateFromModeButton(wxCommandEvent& event);

  void ProcessUpdateFromBus();
  void updateDeviceData();

  wxMenu* m_menuFile;
  wxTimer m_timer;

  wxButton* m_mode_button;

  wxTextCtrl* m_pv_text;      // text control for pv
  wxTextCtrl* m_charger_text; // text control for charger
};

#ifdef USE_CONSOLE
wxIMPLEMENT_APP_CONSOLE(MyApp);
#else
wxIMPLEMENT_APP(MyApp);
#endif

bool MyApp::OnInit()
{
  // call in c-code
  app_initialize_stack("knx_iot_virtual_cem");

  wxInitAllImageHandlers();

  MyFrame* frame = new MyFrame();

  frame->SetSize(350, 200);
  frame->Show(true);
  return true;
}

MyFrame::MyFrame() : wxFrame(nullptr, wxID_ANY, "CEM")
{   
  wxMemoryInputStream iconStream(cem_ico, cem_ico_len);
  wxImage img(iconStream, wxBITMAP_TYPE_ICO);
  wxBitmap bmp(img);
  wxIcon icon;
  icon.CopyFromBitmap(bmp);
  SetIcon(icon);

  m_menuFile = new wxMenu;
  m_menuFile->Append(LIST_ALL, "List All Tables", "List all tables in one window", false);
  m_menuFile->AppendSeparator();
  m_menuFile->Append(CHECK_PM, "Programming Mode", "Sets the application in programming mode", true);
  m_menuFile->Append(RESET_TABLE, "Reset (7) (Tables)", "Reset 7 (Reset to default without IA)", false);
  m_menuFile->Append(RESET, "Reset (2) (ex-factory)", "Reset 2 (Reset to default state)", false);
  m_menuFile->AppendSeparator();
  m_menuFile->Append(wxID_EXIT);

  // help menu
  wxMenu* menuHelp = new wxMenu;
  menuHelp->Append(DEVICE_USAGE, "Usage", "Show device information and usage instructions", false);
  menuHelp->Append(wxID_ABOUT);

  // full menu bar
  wxMenuBar* menuBar = new wxMenuBar;
  menuBar->Append(m_menuFile, "&File");
  menuBar->Append(menuHelp, "&Help");
  wxFrameBase::SetMenuBar(menuBar);
  wxFrameBase::CreateStatusBar();

  Bind(wxEVT_MENU, &MyFrame::OnUsage, this, DEVICE_USAGE);
  Bind(wxEVT_MENU, &MyFrame::OnListAll, this, LIST_ALL);
  Bind(wxEVT_MENU, &MyFrame::OnProgrammingMode, this, CHECK_PM);
  Bind(wxEVT_MENU, &MyFrame::OnReset, this, RESET);
  Bind(wxEVT_MENU, &MyFrame::OnClearTables, this, RESET_TABLE);
  Bind(wxEVT_MENU, &MyFrame::OnAbout, this, wxID_ABOUT);
  Bind(wxEVT_MENU, &MyFrame::OnExit, this, wxID_EXIT);

  // Create a vertical box sizer for the whole section
  wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL);

  wxColor wxBackColor = this->GetBackgroundColour();  

  // --- First row: mode button ---
  wxBoxSizer* hbox1 = new wxBoxSizer(wxHORIZONTAL);
  m_mode_button = new wxButton(this, LS_TEXT, "sun mode", wxDefaultPosition, wxSize(100, 25));
  //m_mode_button->SetFont(wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
  //m_mode_button->SetWindowStyleFlag(wxBORDER_SUNKEN); // remove extra border  
  m_mode_button->Bind(wxEVT_BUTTON, &MyFrame::ProcessUpdateFromModeButton, this);
  
  //m_mode_button->SetBackgroundColour(wxBackColor);

  hbox1->Add(m_mode_button, 0, wxEXPAND); // 0 = fixed width
  vbox->Add(hbox1, 0, wxEXPAND | wxALL, 10);

  // --- Second row: PV text ---
  wxBoxSizer* hbox2 = new wxBoxSizer(wxHORIZONTAL);
  m_pv_text = new wxTextCtrl(this, LS_TEXT, "in: 0 kW", wxDefaultPosition, wxSize(100, 25), wxBORDER_NONE);

          // Adjust margins to vertically center text
  // top margin ~ (control height - font height) / 2
  int fontHeight = m_pv_text->GetFont().GetPointSize();
  int ctrlHeight = m_pv_text->GetSize().GetHeight();
  int topMargin = (ctrlHeight - fontHeight) / 2 - 1; // tweak -1 if needed
  m_pv_text->SetMargins(2, topMargin);

  // Optional: set background and border
  m_pv_text->SetBackgroundColour(wxColour(127, 127, 127));
  //m_pv_text->SetWindowStyle(wxBORDER_SIMPLE); // or wxBORDER_NONE

  //m_pv_text->SetEditable(true);
  hbox2->Add(m_pv_text, 1, wxEXPAND); // stretches horizontally
  vbox->Add(hbox2, 0, wxEXPAND | wxALL, 10);

  // --- Third row: Charger text ---
  wxBoxSizer* hbox3 = new wxBoxSizer(wxHORIZONTAL);
  m_charger_text = new wxTextCtrl(this, LS_TEXT, "out: 0 kW", wxDefaultPosition, wxSize(100, 25), wxBORDER_NONE);
  m_charger_text->SetBackgroundColour(wxColour(127, 127, 127));
  //m_charger_text->SetEditable(true);
  hbox3->Add(m_charger_text, 1, wxEXPAND); // stretches horizontally
  vbox->Add(hbox3, 0, wxEXPAND | wxALL, 10);

  this->SetSizerAndFit(vbox);

  // start the 1ms interval timer for UI updates and stack polls
  m_timer.Bind(wxEVT_TIMER, &MyFrame::OnTimer, this);
  m_timer.Start(1, wxTIMER_CONTINUOUS); 
}

void MyFrame::OnExit(wxCommandEvent& event) { Close(true); }

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

void MyFrame::OnClearTables(wxCommandEvent& event)
{
  SetStatusText("Clear Tables");
  // reset the device
  oc_knx_device_storage_reset(RESET_TO_DEFAULT_WO_IA);
  // update the UI
  this->updateDeviceData();
}

void MyFrame::OnReset(wxCommandEvent& event)
{
  SetStatusText("Device Reset");
  // reset the device
  oc_knx_device_storage_reset(RESET_TO_DEFAULT_STATE);
  // update the UI
  this->updateDeviceData();
}

void MyFrame::OnUsage(wxCommandEvent& event)
{
  wxString usage;
  usage << "Usage:" << "\n"
        << "- Acts as the brain of the EMS system." << "\n"
        << "- Two operation modes:" << "\n"
        << "    1) Sun Mode: Car charges only if solar production is at least 4 kW." << "\n"
        << "    2) Mix Mode: Car charges at 4 kW regardless of solar availability." << "\n"
        << "- Calculates and sends the appropriate charging rate." << "\n";

  CustomDialog("CEM Usage", usage, 480, 180);
}

void MyFrame::ProcessUpdateFromModeButton(wxCommandEvent& event)
{
  cem_mode_t m = retrieve_cem_mode();

  if (m == sun_mode)
  { // toggle
    m = mix_mode;
    m_mode_button->SetLabel("mix mode");
  }
  else
  { // toggle
    m = sun_mode;
    m_mode_button->SetLabel("sun mode");
  }

  // store
  set_cem_mode(m);

  // here called only on a button change
  cem_process_charger_output();

  this->ProcessUpdateFromBus();
}

void MyFrame::OnListAll(wxCommandEvent& event)
{
  const oc_device_info_t* const device = oc_core_get_device_info();
  if (!device)
  {
    return;
  }

  bool ga_conversion = true;
  bool grpid_conversion = true;
  bool iid_conversion = true;

  wxString all;
  all << util_dumpDeviceIDs()  << "\n\n"
      << util_dumpLsmState()   << "\n\n"
      << util_dumpGroupObjectTable(ga_conversion) << "\n\n"
      << util_dumpPublisherTable(ga_conversion, grpid_conversion, iid_conversion)   << "\n\n"
      << util_dumpRecipientTable(ga_conversion, grpid_conversion, iid_conversion)   << "\n\n"
      << util_dumpParameterList()    << "\n\n"
      << util_dumpAuthTable(ga_conversion);

  wxString title;
  title.Printf("CEM - Device & Tables - %s", oc_string(device->serialnumber));

  CustomDialog(title, all, 520, 400);
  SetStatusText("List Device & All Tables");
}

void MyFrame::OnAbout(wxCommandEvent& event)
{
  constexpr char text[] = "(c) KNX Association, 2025-09";
  CustomDialog("About", text, 520, 300);
}

void MyFrame::OnTimer(wxTimerEvent& event)
{
  // stack polling
  (void)oc_main_poll();

  // update possible system events
  this->ProcessUpdateFromBus();

  // update possible user events
  this->updateDeviceData();
}

void MyFrame::ProcessUpdateFromBus()
{

  char text[200];
  
  cem_mode_t m = retrieve_cem_mode();
  float inverter = get_cem_inverter_value() / 1000;
  float charger = get_cem_charger_value() / 1000;

  wxColor col_pv = m_pv_text->GetBackgroundColour();
  wxColor col_charger = m_charger_text->GetBackgroundColour();

  if (inverter < 2)
  {
    // change only if different
    if (col_pv != wxColour(127, 127, 127))
      m_pv_text->SetBackgroundColour(wxColour(127, 127, 127));
  }
  else if (inverter < 4)
  {
    if (col_pv != wxColour(255, 255, 255))
      m_pv_text->SetBackgroundColour(wxColour(255, 255, 255));      
  }
  else if (inverter < 6)
  {
    if (col_pv != wxColour(255, 255, 224))
      m_pv_text->SetBackgroundColour(wxColour(255, 255, 224));
  }
  else 
  {
    if (col_pv != wxColour(255, 255, 0))
      m_pv_text->SetBackgroundColour(wxColour(255, 255, 0));
  }

  if (m == mix_mode)
  {
    if (inverter < 2)
    {
      if (col_charger != wxColour(255, 0, 0))
      m_charger_text->SetBackgroundColour(wxColour(255, 0, 0));
    }
    else if (inverter < 4)
    {
      if (col_charger != wxColour(255, 127, 0))
      m_charger_text->SetBackgroundColour(wxColour(255, 127, 0));
    }
    else if (inverter > 4)
    {
      if (col_charger != wxColour(0, 255, 0))
      m_charger_text->SetBackgroundColour(wxColour(0, 255, 0));
    }
  }
  else
  {
    if (inverter > 4)
    {
      // sun mode, charging > 4 kW green energy 
      if (col_charger != wxColour(0, 255, 0))
      m_charger_text->SetBackgroundColour(wxColour(0, 255, 0));
    }
    else
    {
      // sun mode, charging stopped 
      if (col_charger != wxColour(127, 127, 127))
      m_charger_text->SetBackgroundColour(wxColour(127, 127, 127));
    }
  }

  sprintf(text, "inverter in: %.02f kW", inverter);
  m_pv_text->SetValue(text);  

  if (m == mix_mode)
  {
    const float grid = inverter > 4 ? 0 : 4 - inverter;
    sprintf(text, "charger out: %.02f kW (%.02f kW from grid)", 4.0, grid);
  }
  else
  {
    const float out = inverter > 4 ? 4 : 0;
    sprintf(text, "charger out: %.02f kW", out);
  }

  m_charger_text->SetValue(text);
}
