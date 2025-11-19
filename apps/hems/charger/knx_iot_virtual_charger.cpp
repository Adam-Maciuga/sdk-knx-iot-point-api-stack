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

// needs to be undefined so wx widgets will not use precompiled headers when compiling with msvc
#undef WX_PRECOMP

#include <wx/wxprec.h>
#include <wx/wx.h>
#include <wx/cmdline.h>
#include <wx/scrolbar.h>
#include "wx/config.h" // for native wxConfig
#include "wx/dnd.h" // drag and drop for the playlist
#include "wx/filedlg.h" // for opening files from OpenFile
#include "wx/filename.h" // For wxFileName::GetName()
#include "wx/listctrl.h" // for wxListCtrl
#include "wx/mediactrl.h" // for wxMediaCtrl
#include "wx/notebook.h" // for wxNotebook and putting movies in pages
#include "wx/sizer.h" // for positioning controls/wxBoxSizer
#include "wx/slider.h" // for a slider for seeking within media
#include "wx/textdlg.h" // for getting user text from OpenURL/Debug
#include "wx/timer.h" // timer for updating status bar
#include "wx/vector.h"
#include <wx/stdpaths.h>
#include <wx/artprov.h>
#include <wx/mstream.h>
#include <wx/image.h>
#include <wx/bitmap.h>

#include "api/oc_knx_dev.h"
#include "api/oc_knx_fp.h"
#include "api/oc_knx_sec.h"
#include "oc_knx_client.h"
#include "port/dns-sd.h"
#include "apps/hems/knx_iot_virtual_ems.h"
#include "apps/hems/icons/charger_ico.h"

#include <wx/clipbrd.h>


class FlowAnimation : public wxPanel
{
public:
  FlowAnimation(wxWindow* parent) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(100, 25))
  {
    SetBackgroundColour(wxColour(0, 0, 0));
    Bind(wxEVT_PAINT, &FlowAnimation::OnPaint, this);

    m_timer.Bind(wxEVT_TIMER, &FlowAnimation::OnTimer, this);
    m_timer.Start(m_speed); // Start by default
  }

  // --- Control methods ---

  void Start()
  {
    if (!m_timer.IsRunning())
      m_timer.Start(m_speed);
  }

  void Stop()
  {
    if (m_timer.IsRunning())
      m_timer.Stop();
    Refresh();
  }

  void SetColor(const wxColour& c)
  {
    m_color = c;
    Refresh();
  }

  

private:
  wxTimer m_timer;
  int offset = 0;
  int m_speed = 50; // milliseconds per frame
  wxColour m_color = wxColour(0, 220, 0); // green

  void OnTimer(wxTimerEvent&)
  {
    offset = (offset + 5) % 200;
    Refresh();
  }

  void OnPaint(wxPaintEvent&)
  {
    wxPaintDC dc(this);
    wxSize sz = GetClientSize();

    // background
    dc.SetBrush(wxBrush(wxColour(0, 60, 0)));
    dc.DrawRectangle(0, 0, sz.x, sz.y);

    // animated bright bars
    for (int i = 0; i < sz.x; i += 20)
    {
      int x = (i + offset) % sz.x;
      dc.GradientFillLinear(wxRect(x, 0, 10, sz.y), m_color, wxColour(0, 80, 0), wxSOUTH);
    }
  }
};

class CustomDialog : public wxDialog
{
public:
  CustomDialog(const wxString&, const wxString&, int, int);

private:
  void on_close(wxCommandEvent& event);
};

void CustomDialog::on_close(wxCommandEvent& event) { this->Destroy(); }

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

  oc_device_info_t* device = oc_core_get_device_info();
  char* sn = oc_string(device->serialnumber);

  wxButton* copyButton = new wxButton(this, wxID_ANY, wxT("Copy Certificate"));
  copyButton->Bind(wxEVT_BUTTON,
                   [this, sn](wxCommandEvent&)
                   {
                     if (wxTheClipboard->Open())
                     {
                       wxTheClipboard->SetData(new wxTextDataObject(sn));
                       wxTheClipboard->Close();
                       // wxMessageBox("Serial number copied to clipboard!", "Copied", wxOK | wxICON_INFORMATION);
                     }
                   });
  hbox->Add(copyButton, 0, wxLEFT, 5);  

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
 
  void ProcessUpdateFromCem();
  void updateDeviceData();

  wxMenu* m_menuFile;
  wxTimer m_timer;

  float m_chargeRate = -1;

  // non static device properties
  wxTextCtrl* m_charger_text; // text control for charger

  FlowAnimation* m_flow = nullptr;
};

#ifdef USE_CONSOLE
  wxIMPLEMENT_APP_CONSOLE(MyApp);
#else
  wxIMPLEMENT_APP(MyApp);
#endif

bool MyApp::OnInit()
{
  // call in c-code
  app_initialize_stack("knx_iot_virtual_charger");

  wxInitAllImageHandlers(); 

  MyFrame* frame = new MyFrame();

  frame->SetSize(350, 150);
  frame->Show(true);
  return true;
}

/**
 * @brief Construct a new My Frame:: My Frame object
 *
 */
MyFrame::MyFrame() : wxFrame(nullptr, wxID_ANY, "Charger")
{
  wxMemoryInputStream iconStream(charger_ico, charger_ico_len);
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

  // charger flow
  wxColor wxBackColor = this->GetBackgroundColour(); 

  wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL);
  wxBoxSizer* hbox1 = new wxBoxSizer(wxHORIZONTAL);

  m_charger_text = new wxTextCtrl(this, LS_TEXT, "test", wxDefaultPosition, wxSize(100, 25), wxBORDER_NONE);
  m_charger_text->SetBackgroundColour(wxBackColor);

  hbox1->Add(m_charger_text, 1, wxEXPAND); // stretches horizontally
    
  wxBoxSizer* hbox2 = new wxBoxSizer(wxHORIZONTAL);
  m_flow = new FlowAnimation(this);
  m_flow->SetColor(wxColour(0xff, 0, 0));
  hbox2->Add(m_flow, 1, wxEXPAND); // stretches horizontally
  
  vbox->Add(hbox1, 0, wxEXPAND | wxALL, 10);
  vbox->Add(hbox2, 0, wxEXPAND | wxALL, 10);

  this->SetSizerAndFit(vbox);

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

void MyFrame::OnUsage(wxCommandEvent& event)
{
  wxString usage;
  usage << "Usage:" << "\n"
        << "- Represents an Charger." << "\n"
        << "- Receives and applies the charging power from the CEM" << "\n";

  CustomDialog("Charger Usage", usage, 480, 130);
}

void MyFrame::OnListAll(wxCommandEvent& event)
{
  const oc_device_info_t* const device = oc_core_get_device_info();

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
  title.Printf("Charger - Device & Tables - %s", oc_string(device->serialnumber));

  CustomDialog(title, all, 520, 400);
  SetStatusText("List Device & All Tables");
}

/**
 * @brief shows static info about the application
 *
 * @param event command triggered by a menu button
 */
void MyFrame::OnAbout(wxCommandEvent& event)
{
  constexpr char text[] = "(c) KNX Association, 2025-09";
  CustomDialog("About", text, 520, 300);
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
  // stack polling 
  (void)oc_main_poll();

  // data polling
  ProcessUpdateFromCem();

  // update possible user events
  this->updateDeviceData();
}

void MyFrame::ProcessUpdateFromCem()
{
  // get charger value in kW
  const float charge_rate = get_charger_value() / 1000;  

  #define FLOAT_PRECISION (0.00001)

  // simple (incomplete) float compare to identify a value change  
  const bool is_different = 
    charge_rate < m_chargeRate || 
    charge_rate > m_chargeRate; 

  if (is_different)
  {
    m_chargeRate = charge_rate;

    // close to '0' -> stop
    if (charge_rate < FLOAT_PRECISION)
      m_flow->Stop();
    else
      m_flow->Start();

    // show in bar
    char barText[100];

    (void)sprintf(barText, "Present DC charging powers = %.02f kW", charge_rate);
    m_charger_text->SetValue(barText);
  }
}