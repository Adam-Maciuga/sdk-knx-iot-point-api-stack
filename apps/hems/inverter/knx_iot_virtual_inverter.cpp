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

#include <wx/scrolbar.h>
#include <wx/wxprec.h>
#include <wx/wx.h>

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
#include "apps/hems/knx_iot_virtual_ems.h"
#include "oc_knx_client.h"
#include "port/dns-sd.h"
#include "apps/hems/icons/pv_ico.h"

#include <wx/clipbrd.h>

// IDs for the controls and the menu commands
enum
{
  // Menu event IDs
  wxID_LOOP = 1,
  wxID_OPENFILESAMEPAGE,
  wxID_OPENFILENEWPAGE,
  wxID_OPENURLSAMEPAGE,
  wxID_OPENURLNEWPAGE,
  wxID_CLOSECURRENTPAGE,
  wxID_PLAY,
  wxID_PAUSE,
  wxID_NEXT,
  wxID_PREV,
  wxID_SELECTBACKEND,
  wxID_SHOWINTERFACE,
  wxID_SLIDER,
  wxID_PBSLIDER,
  wxID_VOLSLIDER,
  wxID_NOTEBOOK,
  wxID_MEDIACTRL,
  wxID_BUTTONNEXT,
  wxID_BUTTONPREV,
  wxID_BUTTONSTOP,
  wxID_BUTTONPLAY,
  wxID_BUTTONVD,
  wxID_BUTTONVU,
  wxID_LISTCTRL,
  wxID_GAUGE
};

enum : uint16_t
{
  RESET = 0x0000,
  RESET_TABLE = 0x0001,
  IA_TEXT = 0x0002,
  IID_TEXT = 0x0003,
  PM_TEXT = 0x0004,
  LS_TEXT = 0x0005,
  HOSTNAME_TEXT = 0x0006,
  GOT_TABLE_ID = 0x0007,
  PUB_TABLE_ID = 0x0008,
  REC_TABLE_ID = 0x0009,
  PARAMETER_LIST_ID = 0x000a,
  AT_TABLE_ID = 0x000b,
  CHECK_PM = 0x0010,
  DEVICE_USAGE = 0x0011,
  LIST_ALL = 0x0012
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

  void OnThumbReleased_PV_slider(wxCommandEvent& event);
  void OnSlider_PV_slider(wxCommandEvent& event);

  void updateDeviceData();

  wxMenu* m_menuFile;
  wxTimer m_timer;
  wxSlider* m_PV_slider;
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
  app_initialize_stack("knx_iot_virtual_inverter");

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

  // slider
  wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
  wxStaticText* label = new wxStaticText(this, wxID_ANY, "Present DC Power (0..10 kW)");
  mainSizer->Add(label, 0, wxALL, 5);
  m_PV_slider = new wxSlider(this, wxID_SLIDER, 0, 0, 10, wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
  mainSizer->Add(m_PV_slider, 0, wxEXPAND | wxALL, 5);
  SetSizerAndFit(mainSizer);

  // slider events
  m_PV_slider->Bind(wxEVT_SCROLL_CHANGED, &MyFrame::OnSlider_PV_slider, this);
  m_PV_slider->Enable(true);
  m_PV_slider->SetValue(0);

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

void MyFrame::OnUsage(wxCommandEvent& event)
{
  wxString usage;
  usage << "Usage:" << "\n"
        << "- Simulates a solar inverter providing power data." << "\n"
        << "- Enables adjustment of the solar production value (0-10 kW) for testing." << "\n";

  CustomDialog("Inverter Usage", usage, 480, 130);
}

void MyFrame::OnListAll(wxCommandEvent& event)
{
  const oc_device_info_t* const device = oc_core_get_device_info();

  bool ga_conversion = true;
  bool grpid_conversion = true;
  bool iid_conversion = true;

  wxString all;
  all << util_dumpDeviceIDs()        << "\n\n"
      << util_dumpLsmState()   << "\n\n"
      << util_dumpGroupObjectTable(ga_conversion) << "\n\n"
      << util_dumpPublisherTable(ga_conversion, grpid_conversion, iid_conversion)   << "\n\n"
      << util_dumpRecipientTable(ga_conversion, grpid_conversion, iid_conversion)   << "\n\n"
      << util_dumpParameterList()    << "\n\n"
      << util_dumpAuthTable(ga_conversion);

  wxString title;
  title.Printf("Inverter - Device & Tables - %s", oc_string(device->serialnumber));

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
 * does a oc_main_poll to give a tick to the stack
 * takes into account if the device is sleepy
 * e.g. then it only does an poll each 20 seconds
 * @param event triggered by a timer
 */
void MyFrame::OnTimer(wxTimerEvent& event)
{
  // stack polling 
  (void)oc_main_poll();

  // update possible events
  this->updateDeviceData();
}

void MyFrame::OnSlider_PV_slider(wxCommandEvent& event)
{
  /*
    if (PV_retrieve_link() != 0)
  {
  }
  else
  {
    SetStatusText("not linked");
  }
    */

  // get url
  char* url = app_retrieve_href_from_inverter();

  // get the slider value
  const int val = m_PV_slider->GetValue();

  set_inverter_value(static_cast<float>(val * 1000));

  oc_send_s_mode_mc_or_uc_message(SENDER_SCOPE, url, "w");

  // show in status bar
  char statusBarText[100];

  // Present DC power of all solar panels connected to this PV control
  (void)sprintf(statusBarText, "Present DC power = %d kW", val);
  SetStatusText(statusBarText);

}
