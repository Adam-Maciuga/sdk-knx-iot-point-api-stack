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


// For compilers that support precompilation, includes "wx/wx.h".
#include <wx/cmdline.h>
#include <wx/scrolbar.h>
#include <wx/wxprec.h>

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

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

// main is used from here
#define NO_MAIN

#include "api/oc_knx_dev.h"
#include "api/oc_knx_fp.h"
#include "api/oc_knx_sec.h"
#include "apps/knx_iot_virtual.h"
#include "oc_knx_client.h"
#include "port/dns-sd.h"
#include "oc_oscore_context.h"

#include "knx_iot_virtual_EMS.h"
#include "icons/key_png.h"
#include "icons/pv_png.h"
#include "icons/pv_ico.h"
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
  CHECK_GA_DISPLAY = 0x000c,
  CHECK_IID_DISPLAY = 0x000d,
  CHECK_GRPID_DISPLAY = 0x000e,
  CHECK_SLEEPY = 0x000f,
  CHECK_PM = 0x0010,
  DEVICE_USAGE = 0x0011,
  DEVICE_SETTINGS = 0x0012
};


static const wxCmdLineEntryDesc g_cmdLineDesc[] = {
  {wxCMD_LINE_OPTION, "s", "serialnumber", "serial number", wxCMD_LINE_VAL_STRING}, {wxCMD_LINE_NONE}};

wxCmdLineParser* g_cmd;

class CustomDialog : public wxDialog
{
public:
  CustomDialog(const wxString& title, const wxString& text);

private:
  void on_close(wxCommandEvent& event);
};

void CustomDialog::on_close(wxCommandEvent& event) { this->Destroy(); }

CustomDialog::CustomDialog(const wxString& title, const wxString& text)
    : wxDialog(NULL, -1, title, wxDefaultPosition, wxSize(550, 300))
{
  int size_x = 520;
  int size_y = 300;
  
  wxPanel* panel = new wxPanel(this, -1);

  wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL);
  wxBoxSizer* hbox = new wxBoxSizer(wxHORIZONTAL);

  wxTextCtrl* tc = new wxTextCtrl(panel, -1, text, wxPoint(10, 10), wxSize(size_x, size_y), wxTE_MULTILINE | wxTE_READONLY);

  wxButton* closeButton = new wxButton(this, -1, wxT("Close"), wxDefaultPosition, wxDefaultSize);
  closeButton->Bind(wxEVT_BUTTON, &CustomDialog::on_close, this);

  /*

  wxButton* pasteButton = new wxButton(this, wxID_ANY, wxT("Paste Serial Number"));
  pasteButton->Bind(wxEVT_BUTTON,
                    [this, tc](wxCommandEvent&)
                    {
                      if (wxTheClipboard->Open())
                      {
                        if (wxTheClipboard->IsSupported(wxDF_TEXT))
                        {
                          wxTextDataObject data;
                          wxTheClipboard->GetData(data);
                          wxString wxStr = data.GetText();

                          char serial[16];
                          strncpy(serial, wxStr.mb_str().data(), sizeof(serial) - 1); // safe copy
                          serial[sizeof(serial) - 1] = '\0';

                          PV_init_tables_QR(serial);
                        }
                        wxTheClipboard->Close();
                      }

                      EndModal(wxID_OK);
                    });
  hbox->Add(pasteButton, 0, wxLEFT, 5);

  */

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
  MyFrame(const char* serial_number);

private:
  void OnSettings(wxCommandEvent& event);
  void OnUsage(wxCommandEvent& event);
  wxString dumpGroupObjectTable();
  wxString dumpPublisherTable();
  wxString dumpRecipientTable();
  wxString dumpParameterList();
  wxString dumpAuthTable();

  void OnGroupObjectTable(wxCommandEvent& event);
  void OnPublisherTable(wxCommandEvent& event);
  void OnRecipientTable(wxCommandEvent& event);
  void OnParameterList(wxCommandEvent& event);
  void OnAuthTable(wxCommandEvent& event);
  void OnProgrammingMode(wxCommandEvent& event);
  void OnSleepyMode(wxCommandEvent& event);
  void OnReset(wxCommandEvent& event);
  void OnClearTables(wxCommandEvent& event);
  void OnExit(wxCommandEvent& event);
  void OnAbout(wxCommandEvent& event);
  void OnTimer(wxTimerEvent& event);

  void OnThumbReleased_PV_slider(wxCommandEvent& event);
  void OnSlider_PV_slider(wxCommandEvent& event);

  void updateDeviceData();
  void add_bool_to_text(bool on_off, char* text);
  void int2text(int value, char* text);
  void int2gatext(uint32_t value, char* text, bool as_ets = false);
  void int2grpidtext(uint64_t value, char* text, bool as_ets);
  void int2scopetext(uint32_t value, char* text);
  void double2text(double value, char* text);

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

  wxSlider* m_PV_slider;
};

wxIMPLEMENT_APP(MyApp);

/**
 * @brief initialization of the application
 *
 * @return true
 * @return false
 */
bool MyApp::OnInit()
{
       
  int argc = wxAppConsole::argc;
  wxChar** argv = wxAppConsole::argv;


  g_cmd = new wxCmdLineParser(argc, argv);
  g_cmd->SetDesc(g_cmdLineDesc);
  g_cmd->Parse(true);

  wxString serial_number;
  if (g_cmd->Found("s", &serial_number))
  {
  }  
  
    wxInitAllImageHandlers();   

  MyFrame* frame = new MyFrame(const_cast<char*>((serial_number.c_str()).AsChar()));
  frame->SetSize(350, 150);

  frame->Show(true);

  return true;
}

/**
 * @brief Construct a new My Frame:: My Frame object
 *
 * @param serial_number
 */
MyFrame::MyFrame(const char* serial_number) : wxFrame(nullptr, wxID_ANY, "PV app")
{
    
   
      wxMemoryInputStream iconStream(pv_ico, pv_ico_len);
  wxImage img(iconStream, wxBITMAP_TYPE_ICO);
  wxBitmap bmp(img);
  wxIcon icon;
  icon.CopyFromBitmap(bmp);
  SetIcon(icon);


  wxToolBar* tb = CreateToolBar(wxTB_VERTICAL | wxNO_BORDER | wxTB_FLAT);


  wxMemoryInputStream stream1(key_png, key_png_len);
  wxImage img1(stream1, wxBITMAP_TYPE_PNG);
  wxBitmap bmp1(img1);
  wxMemoryInputStream stream2(pv_png, pv_png_len);
  wxImage img2(stream2, wxBITMAP_TYPE_PNG);
  wxBitmap bmp2(img2);

  tb->AddTool(DEVICE_SETTINGS, "", wxBitmapBundle::FromBitmap(bmp1), "Settings");
  tb->AddTool(DEVICE_USAGE, "", wxBitmapBundle::FromBitmap(bmp2), "Usage");


  tb->Realize();


  m_menuFile = new wxMenu;
  m_menuFile->Append(DEVICE_SETTINGS, "Device Details", "", false);
  m_menuFile->Append(GOT_TABLE_ID, "Group Object Table", "", false);
  m_menuFile->Append(PUB_TABLE_ID, "Publisher Table", "", false);
  m_menuFile->Append(REC_TABLE_ID, "Recipient Table", "", false);
  m_menuFile->Append(AT_TABLE_ID, "Authentication Table", "", false);

  // display menu
  m_menuDisplay = new wxMenu;
  m_menuDisplay->Append(CHECK_GA_DISPLAY, "GA 3-level (ETS)", "Displays as GA 3-Level or as integer",true);
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
  menuBar->Append(m_menuFile, "Config");
  //menuBar->Append(m_menuDisplay, "&Display");
  //menuBar->Append(m_menuOptions, "&Options");
  //menuBar->Append(menuHelp, "&Help");
  //wxFrameBase::SetMenuBar(menuBar);
  wxFrameBase::CreateStatusBar();
  //wxFrameBase::SetStatusText("PV");

  Bind(wxEVT_MENU, &MyFrame::OnUsage, this, DEVICE_USAGE);
  Bind(wxEVT_MENU, &MyFrame::OnSettings, this, DEVICE_SETTINGS);
  // Bind(wxEVT_MENU, &MyFrame::OnGroupObjectTable, this, GOT_TABLE_ID);
  // Bind(wxEVT_MENU, &MyFrame::OnPublisherTable, this, PUB_TABLE_ID);
  // Bind(wxEVT_MENU, &MyFrame::OnRecipientTable, this, REC_TABLE_ID);
  // Bind(wxEVT_MENU, &MyFrame::OnAuthTable, this, AT_TABLE_ID);


  wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
  wxStaticText* label = new wxStaticText(this, wxID_ANY, "Present DC Power (0..10 kW)");
  mainSizer->Add(label, 0, wxALL, 5);
  m_PV_slider = new wxSlider(this, wxID_SLIDER, 0, 0, 10, wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
  mainSizer->Add(m_PV_slider, 0, wxEXPAND | wxALL, 5);
  SetSizerAndFit(mainSizer);
  
  m_PV_slider->Bind(wxEVT_SCROLL_THUMBRELEASE, &MyFrame::OnThumbReleased_PV_slider, this);
  m_PV_slider->Bind(wxEVT_SCROLL_CHANGED, &MyFrame::OnSlider_PV_slider, this);
  m_PV_slider->Enable(true);
  m_PV_slider->SetValue(0);
  

  // serial number
  if (strlen(serial_number) > 1)
  {
    // sn was set by command line 
    //app_set_serial_number(serial_number);
  }

  // call in c-code 
  app_initialize_stack();

  // start the 1ms interval timer for UI updates and stack polls
  m_timer.Bind(wxEVT_TIMER, &MyFrame::OnTimer, this);
  m_timer.Start(1, wxTIMER_CONTINUOUS);

  //PV_init_tables();
  PV_init_tables_QR("00fa10020c00");

  oc_device_info_t* device = oc_core_get_device_info();
  device->lsm_s = LSM_S_LOADED;
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


/**
 * @brief checks/unchecks the sleepy mode
 *
 * @param event command triggered by the menu button
 */
void MyFrame::OnSleepyMode(wxCommandEvent& event)
{
  SetStatusText("Changing sleepy mode");

  bool my_sleepy = m_menuOptions->IsChecked(CHECK_SLEEPY);
  oc_device_info_t* device = oc_core_get_device_info();

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
  /*

  char text[500];
  
  bool iid_conversion = m_menuDisplay->IsChecked(CHECK_IID_DISPLAY);

  // get the device data structure
  oc_device_info_t* device = oc_core_get_device_info(0);
  
  const uint16_t ia_a = device->ia >> 12; // area
  const uint16_t ia_l = device->ia >> 8 & 0xF; // line
  const uint16_t ia_d = device->ia & 0x00FF; // device
  (void)sprintf(text, "IA : %d.%d.%d [%d]", ia_a, ia_l, ia_d, device->ia);
  m_ia_text->SetLabelText(text);

  (void)sprintf(text, "LoadState : %s", oc_core_get_lsm_state_as_string(device->lsm_s));
  m_pm_text->SetLabelText(text);

  (void)sprintf(text, "Programming Mode : %d", device->pm);
  m_ls_text->SetLabelText(text);

  strcpy(text, "IID : ");
  this->int2grpidtext(device->iid, text, iid_conversion);
  m_iid_text->SetLabelText(text);

  (void)sprintf(text, "Hostname : %s", oc_string(device->hostname));
  m_hn_text->SetLabelText(text);

  // set in menu the programming mode to what the device has
  m_menuFile->Check(CHECK_PM, device->pm);

  */
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
  int device_index = 0;
  char text[1024 * 5];
  char line[200];
  char windowtext[200];
  bool ga_conversion = m_menuDisplay->IsChecked(CHECK_GA_DISPLAY);

  strcpy(text, "");

  sprintf(line, "Device IDs:\n");
  strcat(text, line);

  oc_device_info_t* device = oc_core_get_device_info();
  if (device == NULL)
  {
    return;
  }

  sprintf(line, " - Serial number: '%s' ", oc_string(device->serialnumber));
  strcat(text, line);
  strcat(text, "\n"); // break to next entry

  const uint16_t ia_a = device->ia >> 12; // area
  const uint16_t ia_l = device->ia >> 8 & 0xF; // line
  const uint16_t ia_d = device->ia & 0x00FF; // device
  sprintf(line, " - Individual address: %d.%d.%d '%02x'", ia_a, ia_l, ia_d, device->ia);
  strcat(text, line);
  strcat(text, "\n"); // break to next entry

  uint64_t value = device->iid;

  /*
 create the multicast address from group and scope
 FF3_:FD__:____:____:(8-f)___:____
 FF35:30:<ULA-routing-prefix>::<group id>
    | 5 == scope
    | 3 == scope
 Multicast prefix: FF35:0030:  [4 bytes]
 ULA routing prefix: FD11:2222:3333::  [6 bytes + 2 empty bytes]
 Group Identifier: 8000 : 0068 [4 bytes ]
*/
  // group number to the various bytes
  uint8_t byte_1 = static_cast<uint8_t>(value);
  uint8_t byte_2 = static_cast<uint8_t>(value >> 8);
  uint8_t byte_3 = static_cast<uint8_t>(value >> 16);
  uint8_t byte_4 = static_cast<uint8_t>(value >> 24);
  uint8_t byte_5 = static_cast<uint8_t>(value >> 32);

  if (byte_5 == 0)
  {
    sprintf(line, " - Installation ID: '%02x%02x:%02x%02x'", byte_4, byte_3, byte_2, byte_1);
  }
  else
  {
    sprintf(line, " - Installation ID: '%02x:%02x%02x:%02x%02x'", byte_5, byte_4, byte_3, byte_2, byte_1);
  }
  strcat(text, line);


  wxString all;
  all << text << "\n\n"
      << "Usage:" << "\n"
      << "- Simulates a solar inverter providing power data." << "\n"
      << "- Enables adjustment of the solar production value (0–10 kW) for testing." << "\n";

  strcpy(windowtext, "Device IDs & usage");
  CustomDialog(windowtext, all);
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
void MyFrame::OnSettings(wxCommandEvent& event)
{
  oc_device_info_t* device = oc_core_get_device_info();
  if (!device)
  {
    return;
  }

  wxString all;
  all << dumpGroupObjectTable() << "\n"
      << dumpPublisherTable()
      << "\n"
      /* << dumpRecipientTable() */
      << "\n"
      /* << dumpParameterList() << "\n\n" */
      << dumpAuthTable();

  wxString title;
  title.Printf("Device settings & cryptography");

  CustomDialog(title, all);
  // SetStatusText("List All Tables");
}








/**
 * @brief shows the group object table in a window
 *
 * @param event command triggered by a menu button
 */
void MyFrame::OnGroupObjectTable(wxCommandEvent& event)
{
  int device_index = 0;
  char text[1024 * 5];
  char line[200];
  char windowtext[200];
  bool ga_conversion = m_menuDisplay->IsChecked(CHECK_GA_DISPLAY);

  strcpy(text, "");
  oc_device_info_t* device = oc_core_get_device_info();
  if (device == NULL)
  {
    return;
  }
  int total = oc_core_get_group_object_table_total_size();
  for (int index = 0; index < total; index++)
  {
    oc_group_object_table_t* entry = oc_core_get_group_object_table_entry(index);

    if (entry && entry->ga_len > 0)
    {
      sprintf(line, "Index %d ", index);
      strcat(text, line);
      sprintf(line, "  id: '%d'  ", entry->id);
      strcat(text, line);
      sprintf(line, "  url: '%s' ", oc_string(entry->href));
      strcat(text, line);
      sprintf(line, "  cflags : '%d' ", static_cast<int>(entry->cflags));
      oc_cflags_as_string(line, entry->cflags);
      strcat(text, line);
      strcpy(line, "  ga : [");
      for (int i = 0; i < entry->ga_len; i++)
      {
        this->int2gatext(entry->ga[i], line, ga_conversion);
      }
      strcat(line, " ]");
      strcat(text, line);
      strcat(text, "\n"); // break to next entry 
    }
    
  }
  strcpy(windowtext, "Group Object Table");
  CustomDialog(windowtext, text);
}

/**
 * @brief shows the Publisher table in a window
 *
 * @param event command triggered by a menu button
 */
void MyFrame::OnPublisherTable(wxCommandEvent& event)
{
  int device_index = 0;
  char text[1024 * 5];
  char line[200];
  char windowtext[200];
  bool ga_conversion = m_menuDisplay->IsChecked(CHECK_GA_DISPLAY);
  bool grpid_conversion = m_menuDisplay->IsChecked(CHECK_GRPID_DISPLAY);
  bool iid_conversion = m_menuDisplay->IsChecked(CHECK_IID_DISPLAY);

  strcpy(text, "");
  oc_device_info_t* device = oc_core_get_device_info();
  if (device == NULL)
  {
    return;
  }

  int total = oc_core_get_publisher_table_size();
  for (int index = 0; index < total; index++)
  {
    oc_group_table_t* entry = oc_core_get_publisher_table_entry(index);

    if (entry && entry->id >= 0)
    {
      sprintf(line, "Index %d ", index);
      strcat(text, line);
      sprintf(line, "  id: '%d'  ", entry->id);
      strcat(text, line);
      if (entry->ia >= 0)
      {
        sprintf(line, "  ia: '%d' ", entry->ia);
        strcat(text, line);
      }
      if (entry->iid >= 0)
      {
        strcpy(line, "  iid: ");
        this->int2grpidtext(entry->iid, line, iid_conversion);
        strcat(text, line);
      }
      if (entry->fid >= 0)
      {
        sprintf(line, "  fid: '%lld' ", entry->fid);
        strcat(text, line);
      }
      if (entry->grpid > 0)
      {
        // sprintf(line, "  grpid: '%u' ", entry->grpid);
        strcpy(line, "  grpid: ");
        this->int2grpidtext(entry->grpid, line, grpid_conversion);
        strcat(text, line);
      }
      if (oc_string_len(entry->at) > 0)
      {
        sprintf(line, "  at: '%s' ", oc_string(entry->at));
        strcat(text, line);
      }
      if (entry->ga_len > 0)
      {
        strcpy(line, "  ga : [");
        for (int i = 0; i < entry->ga_len; i++)
        {
          this->int2gatext(entry->ga[i], line, ga_conversion);
        }
        strcat(line, " ]");
        strcat(text, line);
      }
      strcat(text, "\n"); // break to next entry 
    }
  }
  strcpy(windowtext, "Publisher Table");
  CustomDialog(windowtext, text);
}

/**
 * @brief shows the Recipient table in a window
 *
 * @param event command triggered by a menu button
 */
void MyFrame::OnRecipientTable(wxCommandEvent& event)
{
  int device_index = 0;
  char text[1024 * 5];
  char line[200];
  char windowtext[200];
  bool ga_conversion = m_menuDisplay->IsChecked(CHECK_GA_DISPLAY);
  bool grpid_conversion = m_menuDisplay->IsChecked(CHECK_GRPID_DISPLAY);
  bool iid_conversion = m_menuDisplay->IsChecked(CHECK_IID_DISPLAY);

  strcpy(text, "");
  oc_device_info_t* device = oc_core_get_device_info();
  if (device == NULL)
  {
    return;
  }

  int total = oc_core_get_recipient_table_size();
  for (int index = 0; index < total; index++)
  {
    oc_group_table_t* entry = oc_core_get_recipient_table_entry(index);

    if (entry && entry->id >= 0)
    {
      sprintf(line, "Index %d ", index);
      strcat(text, line);
      sprintf(line, "  id: '%d'  ", entry->id);
      strcat(text, line);
      if (entry->ia >= 0)
      {
        sprintf(line, "  ia: '%d' ", entry->ia);
        strcat(text, line);
      }
      if (entry->iid >= 0)
      {
        strcpy(line, "  iid: ");
        this->int2grpidtext(entry->iid, line, iid_conversion);
        strcat(text, line);
      }
      if (entry->fid >= 0)
      {
        sprintf(line, "  fid: '%lld' ", entry->fid);
        strcat(text, line);
      }
      if (entry->grpid > 0)
      {
        strcpy(line, "  grpid: ");
        this->int2grpidtext(entry->grpid, line, grpid_conversion);
        strcat(text, line);
      }
      if (oc_string_len(entry->at) > 0)
      {
        sprintf(line, "  at: '%s' ", oc_string(entry->at));
        strcat(text, line);
      }
      if (entry->ga_len > 0)
      {
        strcpy(line, "  ga : [");
        for (int i = 0; i < entry->ga_len; i++)
        {
          this->int2gatext(entry->ga[i], line, ga_conversion);
        }
        strcat(line, " ]");
        strcat(text, line);
      }
      strcat(text, "\n"); // break to next entry 
    }
  }
  strcpy(windowtext, "Recipient Table");
  CustomDialog(windowtext, text);
}
/**
 * @brief shows a window containing the parameters and current values of the application
 *
 * @param event command triggered by a menu button
 */
void MyFrame::OnParameterList(wxCommandEvent& event)
{
  int device_index = 0;
  char text[1024 + (200 * 0)];
  char line[200];
  char windowtext[200];

  strcpy(text, "");

  oc_device_info_t* device = oc_core_get_device_info();
  if (device == NULL)
  {
    return;
  }

  int index = 1;
  char* url = app_get_parameter_url(index);
  if (url == NULL)
  {
    strcat(text, "no parameters in this device");
  }
  while (url)
  {
    sprintf(line, "\nIndex %02d ", index);
    strcat(text, line);
    sprintf(line, "  url : '%s'  ", url);
    strcat(text, line);
    char* name = app_get_parameter_name(index);
    if (name)
    {
      sprintf(line, "  name: '%s'  ", app_get_parameter_name(index));
      strcat(text, line);
    }


    // if (app_is_string_url(url)) {
    //   sprintf(line, "  value : '%s'  ", app_retrieve_string_variable(url));
    //   strcat(text, line);
    // }
    index++;
    url = app_get_parameter_url(index);
  }
  strcpy(windowtext, "Parameter List ");
  strcat(windowtext, oc_string(device->serialnumber));
  // wxMessageBox(text, windowtext,
  //   wxOK | wxICON_NONE);
  CustomDialog(windowtext, text);
  SetStatusText("List Parameters and their current set values");
}

/**
 * @brief shows the (loaded) auth/at table
 *
 * @param event command triggered by a menu button
 */
void MyFrame::OnAuthTable(wxCommandEvent& event)
{
  int device_index = 0;
  char text[1024 * 10];
  char line[500];
  bool ga_conversion = m_menuDisplay->IsChecked(CHECK_GA_DISPLAY);
  char windowtext[200];
  int max_entries = oc_core_get_at_table_size();
  int index = 1;

  oc_device_info_t* device = oc_core_get_device_info();
  if (device == NULL)
  {
    return;
  }

  strcpy(text, "");
  for (index = 0; index < max_entries; index++)
  {

    oc_auth_at_t* my_entry = oc_get_auth_at_entry(index);
    if (my_entry)
    {
      if (oc_string_len(my_entry->id))
      {
        sprintf(line, "index : '%d' id = '%s' ", index, oc_string(my_entry->id));
        strcat(text, line);
        sprintf(line, "  profile : %d (%s)", my_entry->profile, oc_at_profile_to_string(my_entry->profile));
        strcat(text, line);
        if (my_entry->profile == OC_PROFILE_COAP_DTLS)
        {
          if (oc_string_len(my_entry->sub) > 0)
          {
            sprintf(line, "    sub           : %s", oc_string(my_entry->sub));
            strcat(text, line);
          }
          if (oc_string_len(my_entry->kid) > 0)
          {
            sprintf(line, "  kid : %s", oc_string(my_entry->kid));
            strcat(text, line);
          }
        }
        if (my_entry->profile == OC_PROFILE_COAP_OSCORE)
        {
          if (oc_byte_string_len(my_entry->osc_id) > 0)
          {
            sprintf(line, "  osc_id [%d]: ", static_cast<int>(oc_byte_string_len(my_entry->osc_id)));
            strcat(text, line);
            char* ms = oc_string(my_entry->osc_id);
            int length = static_cast<int>(oc_byte_string_len(my_entry->osc_id));
            for (int i = 0; i < length; i++)
            {
              sprintf(line, "%02x", static_cast<unsigned char>(ms[i]));
              strcat(text, line);
            }
            sprintf(line, "");
            strcat(text, line);
          }
          
          if (oc_byte_string_len(my_entry->osc_ms) > 0)
          {
            sprintf(line, "  osc_ms [%d]: ", static_cast<int>(oc_byte_string_len(my_entry->osc_ms)));
            strcat(text, line);
            int length = static_cast<int>(oc_byte_string_len(my_entry->osc_ms));
            char* ms = oc_string(my_entry->osc_ms);
            for (int i = 0; i < length; i++)
            {
              sprintf(line, "%02x", static_cast<unsigned char>(ms[i]));
              strcat(text, line);
            }
            sprintf(line, "");
            strcat(text, line);
          }
          if (oc_byte_string_len(my_entry->osc_contextid) > 0)
          {
            sprintf(line, "  osc_contextid (o)[%d]: ", static_cast<int>(oc_byte_string_len(my_entry->osc_contextid)));
            strcat(text, line);
            char* ms = oc_string(my_entry->osc_contextid);
            int length = static_cast<int>(oc_byte_string_len(my_entry->osc_contextid));
            for (int i = 0; i < length; i++)
            {
              sprintf(line, "%02x", static_cast<unsigned char>(ms[i]));
              strcat(text, line);
            }
            sprintf(line, "");
            strcat(text, line);
          }
          
          if (my_entry->scope == OC_ACL_GA)
          {
            sprintf(line, "  osc_ga : [");
            strcat(text, line);
            for (int i = 0; i < my_entry->ga_len; i++)
            {
              this->int2gatext(my_entry->ga[i], text, ga_conversion);
            }
            sprintf(line, " ]\n");
            strcat(text, line);
          }
          else
          {
            sprintf(line, "  scope : ");
            this->int2scopetext(my_entry->scope, line);
            strcat(text, line);
            strcat(text, "\n");
          }
        }
      }
    }
  }

  strcpy(windowtext, "Authentication Table");
  CustomDialog(windowtext, text);
}

/**
 * @brief shows static info about the application
 *
 * @param event command triggered by a menu button
 */
void MyFrame::OnAbout(wxCommandEvent& event)
{
  constexpr char text[] = "(c) KNX Association, 2025-09";
  CustomDialog("About", text);
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
  this->updateDeviceData();
}

/**
 * @brief convert the boolean to text and appends it to the given text 
 *
 * @param on_off the boolean
 * @param text the text to add the boolean as text
 */
void MyFrame::add_bool_to_text(bool on_off, char* text)
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

/**
 * @brief convert the integer to text for display
 *
 * @param value the integer
 * @param text the text to add info to
 */
void MyFrame::int2text(int value, char* text)
{
  char value_text[50];

  sprintf(value_text, " %d", value);
  strcat(text, value_text);
}

/**
 * @brief convert the group address to text for display
 *
 * @param value the integer
 * @param text the text to add info to
 * @param as_ets the text as terminology as used in ets
 */
void MyFrame::int2gatext(uint32_t value, char* text, bool as_ets)
{
  char value_text[50];

  if (as_ets)
  {
    /*
    The so called Group Address structure correlates with its representation style in ETS,
    see also the relevant ETS Professional article.
    The information about the ETS Group Address representation style itself is NOT included in the Group Address.
    '3-level' = main/middle/sub
    main = D7+D6+D5+D4+D3 of the first octet (high address)
    middle = D2+D1+D0 of the first octet (high address)
    sub = the entire second octet (low address)
    ranges: main = 0..31, middle = 0..7, sub = 0..255
    */
    uint32_t ga = value;
    uint32_t ga_main = (ga >> 11);
    uint32_t ga_middle = (ga >> 8) & 0x7;
    uint32_t ga_sub = (ga & 0x000000FF);
    sprintf(value_text, " %lu/%lu/%lu", ga_main, ga_middle, ga_sub);
    strcat(text, value_text);
  }
  else
  {
    sprintf(value_text, " %lu", value);
    strcat(text, value_text);
  }
}

/**
 * @brief convert the scope to text for display
 *
 * @param value the scope
 * @param text the text to add info too
 */
void MyFrame::int2scopetext(uint32_t value, char* text)
{
  char value_text[150];

  sprintf(value_text, " [%d]", value);
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
 * @brief convert the group id to text for display
 *
 * @param value the group id
 * @param text the text to add info too
 * @param as_ets the text as terminology as used in ets
 */
void MyFrame::int2grpidtext(uint64_t value, char* text, bool as_ets)
{
  char value_text[50];

  if (as_ets)
  {
    /*
     create the multicast address from group and scope
     FF3_:FD__:____:____:(8-f)___:____
     FF35:30:<ULA-routing-prefix>::<group id>
        | 5 == scope
        | 3 == scope
     Multicast prefix: FF35:0030:  [4 bytes]
     ULA routing prefix: FD11:2222:3333::  [6 bytes + 2 empty bytes]
     Group Identifier: 8000 : 0068 [4 bytes ]
    */
    // group number to the various bytes
    uint8_t byte_1 = static_cast<uint8_t>(value);
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

/**
 * @brief convert the double (e.g. float)  to text for display
 *
 * @param value the vlue
 * @param text the text to add info too
 */
void MyFrame::double2text(double value, char* text)
{
  char new_text[200];
  sprintf(new_text, " %f", value);
  strcat(text, new_text);
}

void MyFrame::OnThumbReleased_PV_slider(wxCommandEvent& event)
{
  /*

  // get url
  char* url = PV_retrieve_href(0);
  // get the slider value
  int val = m_PV_slider->GetValue();
      
  PV_set_PV(val);          

  oc_issue_s_mode_with_scope_and_check_mc_or_uc(SENDER_SCOPE, url, "w");

  // show in status bar
  char statusBarText[100];
  //Present DC power of all solar panels connected to this PV control
  (void)sprintf(statusBarText, "Present DC power @ '%s' = %d kW", url, val);
  SetStatusText(statusBarText);

  */
}

void MyFrame::OnSlider_PV_slider(wxCommandEvent& event)
{
  // get url
  char* url = PV_retrieve_href(0);
  // get the slider value
  int val = m_PV_slider->GetValue();

  PV_set_PV(val);

  oc_issue_s_mode_with_scope_and_check_mc_or_uc(SENDER_SCOPE, url, "w");

  // show in status bar
  char statusBarText[100];
  // Present DC power of all solar panels connected to this PV control
  (void)sprintf(statusBarText, "Present DC power = %d kW", val);
  SetStatusText(statusBarText);
}


/**
 * @brief dump the Group Object Table into a string
 *
 * Iterates through all group object entries and prints:
 * - Index
 * - id
 * - url
 * - cflags (numeric and textual)
 * - group addresses (GA list)
 *
 * @return wxString containing formatted Group Object Table
 */
wxString MyFrame::dumpGroupObjectTable()
{
  wxString out("- Datapoints:\n");
  char line[512];
  bool ga_conversion = m_menuDisplay->IsChecked(CHECK_GA_DISPLAY);

  int total = oc_core_get_group_object_table_total_size();
  for (int i = 0; i < total; i++)
  {
    oc_group_object_table_t* entry = oc_core_get_group_object_table_entry(i);
    if (entry && entry->ga_len > 0)
    {
      // sprintf(line, "Index %d ", i);
      // out += line;

      // sprintf(line, "  id: '%d'  ", entry->id);
      // out += line;

      sprintf(line, "  - url: '%s' ", oc_string(entry->href));
      out += line;

      // numeric + textual cflags in ONE go
      sprintf(line, "  cflags : '%d' ", (int)entry->cflags);
      oc_cflags_as_string(line, entry->cflags);
      out += line;

      // ga list
      strcpy(line, "  ga: [");
      for (int j = 0; j < entry->ga_len; j++)
      {
        this->int2gatext(entry->ga[j], line, ga_conversion);
      }
      strcat(line, " ]");
      out += line;

      out += "\n";
    }
  }
  return out;
}

/**
 * @brief dump the Publisher Table into a string
 *
 * Iterates through all publisher table entries and prints:
 * - Index
 * - id, ia, iid, fid
 * - grpid (converted if enabled)
 * - at string
 * - group addresses (GA list)
 *
 * @return wxString containing formatted Publisher Table
 */
wxString MyFrame::dumpPublisherTable()
{
  wxString out("- Multicast:\n");
  char line[256];
  bool ga_conversion = m_menuDisplay->IsChecked(CHECK_GA_DISPLAY);
  bool grpid_conversion = m_menuDisplay->IsChecked(CHECK_GRPID_DISPLAY);
  bool iid_conversion = m_menuDisplay->IsChecked(CHECK_IID_DISPLAY);

  int total = oc_core_get_publisher_table_size();
  for (int i = 0; i < total; i++)
  {
    oc_group_table_t* entry = oc_core_get_publisher_table_entry(i);
    if (entry && entry->id >= 0)
    {
      /*
      sprintf(line, "Index %d ", i);
      out += line;
      sprintf(line, "  id: '%d'  ", entry->id);
      out += line;
      if (entry->ia >= 0)
      {
        sprintf(line, "  ia: '%d' ", entry->ia);
        out += line;
      }
      if (entry->iid >= 0)
      {
        strcpy(line, "  iid: ");
        this->int2grpidtext(entry->iid, line, iid_conversion);
        out += line;
      }
      if (entry->fid >= 0)
      {
        sprintf(line, "  fid: '%lld' ", entry->fid);
        out += line;
      }
      */
      if (entry->grpid > 0)
      {
        strcpy(line, "  - grpid: ");
        this->int2grpidtext(entry->grpid, line, grpid_conversion);
        out += line;
      }
      if (oc_string_len(entry->at) > 0)
      {
        sprintf(line, "  at: '%s' ", oc_string(entry->at));
        out += line;
      }
      if (entry->ga_len > 0)
      {
        strcpy(line, "  ga: [");
        for (int j = 0; j < entry->ga_len; j++)
        {
          this->int2gatext(entry->ga[j], line, ga_conversion);
        }
        strcat(line, " ]");
        out += line;
      }
      // out += "\n";
    }
  }
  return out;
}

/**
 * @brief dump the Recipient Table into a string
 *
 * Iterates through all recipient table entries and prints:
 * - Index
 * - id, ia, iid, fid
 * - grpid (converted if enabled)
 * - at string
 * - group addresses (GA list)
 *
 * @return wxString containing formatted Recipient Table
 */
wxString MyFrame::dumpRecipientTable()
{
  wxString out("- Recipient Table:\n");
  char line[256];
  bool ga_conversion = m_menuDisplay->IsChecked(CHECK_GA_DISPLAY);
  bool grpid_conversion = m_menuDisplay->IsChecked(CHECK_GRPID_DISPLAY);
  bool iid_conversion = m_menuDisplay->IsChecked(CHECK_IID_DISPLAY);

  int total = oc_core_get_recipient_table_size();
  for (int i = 0; i < total; i++)
  {
    oc_group_table_t* entry = oc_core_get_recipient_table_entry(i);
    if (entry && entry->id >= 0)
    {
      sprintf(line, "Index %d ", i);
      out += line;
      sprintf(line, "  id: '%d'  ", entry->id);
      out += line;
      if (entry->ia >= 0)
      {
        sprintf(line, "  ia: '%d' ", entry->ia);
        out += line;
      }
      if (entry->iid >= 0)
      {
        strcpy(line, "  iid: ");
        this->int2grpidtext(entry->iid, line, iid_conversion);
        out += line;
      }
      if (entry->fid >= 0)
      {
        sprintf(line, "  fid: '%lld' ", entry->fid);
        out += line;
      }
      if (entry->grpid > 0)
      {
        strcpy(line, "  grpid: ");
        this->int2grpidtext(entry->grpid, line, grpid_conversion);
        out += line;
      }
      if (oc_string_len(entry->at) > 0)
      {
        sprintf(line, "  at: '%s' ", oc_string(entry->at));
        out += line;
      }
      if (entry->ga_len > 0)
      {
        strcpy(line, "  ga : [");
        for (int j = 0; j < entry->ga_len; j++)
        {
          this->int2gatext(entry->ga[j], line, ga_conversion);
        }
        strcat(line, " ]");
        out += line;
      }
      // out += "\n";
    }
  }
  return out;
}

/**
 * @brief dump the Parameter List into a string
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
wxString MyFrame::dumpParameterList()
{
  wxString out("=== Parameter List ===\n");
  char line[256];

  int index = 1;
  char* url = app_get_parameter_url(index);
  if (url == NULL)
  {
    out += "no parameters in this device\n";
  }
  while (url)
  {
    sprintf(line, "\nIndex %02d ", index);
    out += line;
    sprintf(line, "  url : '%s'  ", url);
    out += line;
    char* name = app_get_parameter_name(index);
    if (name)
    {
      sprintf(line, "  name: '%s'  ", name);
      out += line;
    }
    index++;
    url = app_get_parameter_url(index);
  }
  return out;
}

/**
 * @brief dump the Auth/AT Table into a string
 *
 * Iterates through all authentication/authorization entries and prints:
 * - Index
 * - id
 * - profile
 * - For DTLS: sub, kid
 * - For OSCORE: osc_id, osc_ms, osc_contextid (hex dumps)
 * - scope or osc_ga (with GA list)
 *
 * @return wxString containing formatted Auth/AT Table
 */
wxString MyFrame::dumpAuthTable()
{
  wxString out("- OSCORE (Wireshark):\n");
  char line[512];
  bool ga_conversion = m_menuDisplay->IsChecked(CHECK_GA_DISPLAY);

  int max_entries = oc_core_get_at_table_size();
  for (int i = 0; i < max_entries; i++)
  {
    oc_auth_at_t* entry = oc_get_auth_at_entry(i);
    if (entry && oc_string_len(entry->id))
    {
      /*
      sprintf(line, "index : '%d' id = '%s' ", i, oc_string(entry->id));
      out += line;
      sprintf(line, "  profile : %d (%s)", entry->profile, oc_at_profile_to_string(entry->profile));
      out += line;

      if (entry->profile == OC_PROFILE_COAP_DTLS)
      {
        if (oc_string_len(entry->sub) > 0)
        {
          sprintf(line, "  sub : %s", oc_string(entry->sub));
          out += line;
        }
        if (oc_string_len(entry->kid) > 0)
        {
          sprintf(line, "  kid : %s", oc_string(entry->kid));
          out += line;
        }
      }
      */
      if (entry->profile == OC_PROFILE_COAP_OSCORE)
      {
        strcpy(line, "  - ga ");
        // out += line;
        this->int2gatext(entry->ga[0], line, ga_conversion);
        strcat(line, ":\n");
        out += line;

        // osc_id
        if (oc_byte_string_len(entry->osc_id) > 0)
        {
          sprintf(line, "       - osc_id [%d]: ", (int)oc_byte_string_len(entry->osc_id));
          out += line;
          char* ms = oc_string(entry->osc_id);
          for (int j = 0; j < (int)oc_byte_string_len(entry->osc_id); j++)
          {
            sprintf(line, "%02x", (unsigned char)ms[j]);
            out += line;
          }
        }
        out += "\n";
        // osc_ms
        if (oc_byte_string_len(entry->osc_ms) > 0)
        {
          sprintf(line, "       - osc_ms [%d]: ", (int)oc_byte_string_len(entry->osc_ms));
          out += line;
          char* ms = oc_string(entry->osc_ms);
          for (int j = 0; j < (int)oc_byte_string_len(entry->osc_ms); j++)
          {
            sprintf(line, "%02x", (unsigned char)ms[j]);
            out += line;
          }
        }
        out += "\n";
        // osc_contextid
        if (oc_byte_string_len(entry->osc_contextid) > 0)
        {
          sprintf(line, "       - osc_contextid [%d]: ", (int)oc_byte_string_len(entry->osc_contextid));
          out += line;
          char* ms = oc_string(entry->osc_contextid);
          for (int j = 0; j < (int)oc_byte_string_len(entry->osc_contextid); j++)
          {
            sprintf(line, "%02x", (unsigned char)ms[j]);
            out += line;
          }
        }
        /*
        if (entry->scope == OC_ACL_GA)
        {
          strcpy(line, " ga: ");
          out += line;
          for (int j = 0; j < entry->ga_len; j++)
          {
            this->int2gatext(entry->ga[j], line, ga_conversion);
          }
          strcat(line, " ");
          out += line;
        }
        else
        {
          sprintf(line, "  scope : ");
          this->int2scopetext(entry->scope, line);
          out += line;
        }
        */
      }
      out += "\n";
    }
  }
  return out;
}
