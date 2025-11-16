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


static const wxCmdLineEntryDesc g_cmdLineDesc[] = {
  {wxCMD_LINE_OPTION, "s", "serialnumber", "serial number", wxCMD_LINE_VAL_STRING}, {wxCMD_LINE_NONE}};
wxCmdLineParser* g_cmd;

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

  oc_device_info_t* device = oc_core_get_device_info();
  char* sn = oc_string(device->serialnumber);

  hbox->Add(closeButton, 1, wxLEFT, 5);
  vbox->Add(panel, 1);
  vbox->Add(hbox, 0, wxALIGN_CENTER | wxTOP | wxBOTTOM, 10);

  SetSizerAndFit(vbox);
  Centre();
  ShowModal();
  Destroy();
}

class MyFrame : public wxFrame
{
public:
  MyFrame(const char* serial_number);

private:
  void OnUsage(wxCommandEvent& event);

  void OnListAll(wxCommandEvent& event);
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


  void OnClick_mode_button(wxCommandEvent& event);

  void ProcessUpdateFromBus();
  void updateDeviceData();

  wxMenu* m_menuFile;
  wxTimer m_timer;

  // sleepy information
  int m_sleep_counter = 0;
  int m_sleep_milliseconds = 20000;

  int m_lsm = LSM_S_UNLOADED;


  int m_mode = -1;
  int m_pv = -1;
  int m_charger = -1;
  char* m_link; // either the sn of the PV or the Charger device

  // non static device properties
  wxTextCtrl* m_ia_text; // text control for internal address
  wxTextCtrl* m_iid_text; // text control for installation id
  wxTextCtrl* m_pm_text; // text control for programming mode
  wxTextCtrl* m_ls_text; // text control for load state
  wxTextCtrl* m_hn_text; // text control for host name

  wxButton* m_mode_button;

  wxTextCtrl* m_pv_text; // text control for pv
  wxTextCtrl* m_charger_text; // text control for charger
};
MyFrame::MyFrame(const char* serial_number) : wxFrame(nullptr, wxID_ANY, "CEM app")
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
  m_mode_button->Bind(wxEVT_BUTTON, &MyFrame::OnClick_mode_button, this);
  
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

  // --- Apply sizer to the frame ---
  this->SetSizerAndFit(vbox);

  // serial number
  if (strlen(serial_number) > 1)
  {
    // sn was set by command line 
    //app_set_serial_number(serial_number);
  }

  // call in c-code 
  app_initialize_stack("knx_iot_virtual_cem");

  constexpr int width_size = 180; // size of the knx info widgets
  char text[500]; 

  // serial number 
  strcpy(text, "Serial Number : ");
  oc_device_info_t* device = oc_core_get_device_info();
  strcat(text, oc_string(device->serialnumber));

  // start the 1ms interval timer for UI updates and stack polls
  m_timer.Bind(wxEVT_TIMER, &MyFrame::OnTimer, this);
  m_timer.Start(1, wxTIMER_CONTINUOUS); 

  // TODO: add an input field to make this dynamic
  //CEM_init_tables("00fa10020b00");
  //CEM_init_tables("00fa10020d00");
  // CEM_init_tables("000000000000");
  device->lsm_s = LSM_S_LOADED;
  
  // this is the config for phase 2
  //CEM_init_auth_table();
  //device->lsm_s = LSM_S_UNLOADED;
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
void MyFrame::OnSleepyMode(wxCommandEvent& event)
{
  SetStatusText("Changing sleepy mode");

  bool my_sleepy = false;
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
void MyFrame::updateDeviceData()
{
  oc_device_info_t* device = oc_core_get_device_info();
 
  // this is the handling for phase 2
  if (m_lsm != device->lsm_s)
  {
    m_lsm = device->lsm_s;

    if (m_lsm == LSM_S_UNLOADED)
    {
      //oc_knx_device_storage_reset(RESET_TO_DEFAULT_WO_IA);
    }
    if (m_lsm == LSM_S_LOADING)
    {
      //oc_knx_device_storage_reset(RESET_TO_DEFAULT_WO_IA);
      //CEM_init_tables();
      //device->lsm_s = LSM_S_LOADED;
    }
    if (m_lsm == LSM_S_LOADED)
    {
      //device->lsm_s = LSM_S_LOADED;
    }
  }

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

void MyFrame::OnClick_mode_button(wxCommandEvent& event)
{
  char text[200];
  cem_mode_t m = retrieve_cem_mode();

  if (m == sun_mode)
  {
    m = mix_mode;

    m_mode_button->SetLabel("mix mode");
  }
  else
  {
    m = sun_mode;

    m_mode_button->SetLabel("sun mode");
  }

  set_cem_mode(m);

  cem_process_inverter_input();

  this->ProcessUpdateFromBus();

  /*
  int pv = get_cem_inverter_value() / 1000;
  int charger = get_cem_charger_value() / 1000;

  sprintf(text, "in: %d kW", pv);
  m_pv_text->SetValue(text);

  if (m == 1 && pv < 4)
  {
    int grid = 4 - pv;

    sprintf(text, "out: %d kW (%d kW from grid)", charger, grid);
  }
  else
  {
    sprintf(text, "out: %d kW", charger);
  }

  m_charger_text->SetValue(text);
  */
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
  all << util_dumpDeviceIDs()        << "\n\n"
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
void MyFrame::OnGroupObjectTable(wxCommandEvent& event)
{
  int device_index = 0;
  char text[1024 * 5];
  char line[200];
  char windowtext[200];
  bool ga_conversion = true;

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
        util_int2gatext(entry->ga[i], line, ga_conversion);
      }
      strcat(line, " ]");
      strcat(text, line);
      strcat(text, "\n"); // break to next entry 
    }
    
  }
  strcpy(windowtext, "Group Object Table for sn: ");
  strcat(windowtext, oc_string(device->serialnumber));
  CustomDialog(windowtext, text, 520, 300);
  //SetStatusText("List Group Object Table");
}
void MyFrame::OnPublisherTable(wxCommandEvent& event)
{
  int device_index = 0;
  char text[1024 * 5];
  char line[200];
  char windowtext[200];
  bool ga_conversion = true;
  bool grpid_conversion = true;
  bool iid_conversion = true;

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
        util_int2grpidtext(entry->iid, line, iid_conversion);
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
        util_int2grpidtext(entry->grpid, line, grpid_conversion);
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
          util_int2gatext(entry->ga[i], line, ga_conversion);
        }
        strcat(line, " ]");
        strcat(text, line);
      }
      strcat(text, "\n"); // break to next entry 
    }
  }
  strcpy(windowtext, "Publisher Table");
  //strcat(windowtext, oc_string(device->serialnumber));
  CustomDialog(windowtext, text, 520, 300);
  //SetStatusText("List Publisher Table");
}
void MyFrame::OnRecipientTable(wxCommandEvent& event)
{
  int device_index = 0;
  char text[1024 * 5];
  char line[200];
  char windowtext[200];
  bool ga_conversion = true;
  bool grpid_conversion = true;
  bool iid_conversion = true;

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
        util_int2grpidtext(entry->iid, line, iid_conversion);
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
        util_int2grpidtext(entry->grpid, line, grpid_conversion);
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
          util_int2gatext(entry->ga[i], line, ga_conversion);
        }
        strcat(line, " ]");
        strcat(text, line);
      }
      strcat(text, "\n"); // break to next entry 
    }
  }
  strcpy(windowtext, "Recipient Table");
  //strcat(windowtext, oc_string(device->serialnumber));
  CustomDialog(windowtext, text, 520, 300);
  //SetStatusText("List Recipient Table");
}
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
  CustomDialog(windowtext, text, 520, 300);
  SetStatusText("List Parameters and their current set values");
}
void MyFrame::OnAuthTable(wxCommandEvent& event)
{
  int device_index = 0;
  char text[1024 * 10];
  char line[500];
  bool ga_conversion = true;
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
              util_int2gatext(my_entry->ga[i], text, ga_conversion);
            }
            sprintf(line, " ]\n");
            strcat(text, line);
          }
          else
          {
            sprintf(line, "  scope : ");
            util_int2scopetext(my_entry->scope, line);
            strcat(text, line);
            strcat(text, "\n");
          }
        }
      }
    }
  }

  strcpy(windowtext, "Authentication Table");
  //strcat(windowtext, oc_string(device->serialnumber));
  CustomDialog(windowtext, text, 520, 300);
  //SetStatusText("List security entries");
}
void MyFrame::OnAbout(wxCommandEvent& event)
{
  constexpr char text[] = "(c) KNX Association, 2025-09";
  CustomDialog("About", text, 520, 300);
}
void MyFrame::OnTimer(wxTimerEvent& event)
{
  bool do_poll = true;

  const bool sleepy = false;

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
  this->ProcessUpdateFromBus();
  this->updateDeviceData();
}
void MyFrame::ProcessUpdateFromBus()
{
  // the actual processing of bus events is in this case done via put_PV() + process_pv() in knx_iot_virtual.c

  char text[200];
  
  cem_mode_t m = retrieve_cem_mode();
  int pv = get_cem_inverter_value() / 1000;
  int charger = get_cem_charger_value() / 1000;

  wxColor col_pv = m_pv_text->GetBackgroundColour();
  wxColor col_charger = m_charger_text->GetBackgroundColour();


    if (pv == 0 || pv == 1)
    {
      if (col_pv != wxColour(127, 127, 127))
        m_pv_text->SetBackgroundColour(wxColour(127, 127, 127));
    }
    if (pv == 2 || pv == 3)
    {
      if (col_pv != wxColour(255, 255, 255))
        m_pv_text->SetBackgroundColour(wxColour(255, 255, 255));      
    }
    if (pv == 4 || pv == 5 || pv == 6)
    {
      if (col_pv != wxColour(255, 255, 224))
        m_pv_text->SetBackgroundColour(wxColour(255, 255, 224));
    }
    if (pv >= 7)
    {
      if (col_pv != wxColour(255, 255, 0))
        m_pv_text->SetBackgroundColour(wxColour(255, 255, 0));
    }

    if (m == mix_mode)
    {
      if (pv == 0 || pv == 1)
      {
        if (col_charger != wxColour(255, 0, 0))
        m_charger_text->SetBackgroundColour(wxColour(255, 0, 0));
      }
      if (pv == 2 || pv == 3)
      {
        if (col_charger != wxColour(255, 127, 0))
        m_charger_text->SetBackgroundColour(wxColour(255, 127, 0));
      }
      if (pv >= 4)
      {
        if (col_charger != wxColour(0, 255, 0))
        m_charger_text->SetBackgroundColour(wxColour(0, 255, 0));
      }
    }
    else
    {
      if (pv >= 4)
      {
        if (col_charger != wxColour(0, 255, 0))
        m_charger_text->SetBackgroundColour(wxColour(0, 255, 0));
      }
      else
      {
        if (col_charger != wxColour(127, 127, 127))
        m_charger_text->SetBackgroundColour(wxColour(127, 127, 127));
      }
    }

/*
  if (m_pv != pv)
  {
    m_pv = pv;

    if (pv == 0 || pv == 1)
    {
      m_pv_text->SetBackgroundColour(wxColour(127, 127, 127));
    }
    if (pv == 2 || pv == 3)
    {
      m_pv_text->SetBackgroundColour(wxColour(255, 255, 0));
    }
    if (pv >= 4)
    {
      m_pv_text->SetBackgroundColour(wxColour(255, 255, 255));
    }

    if (m == MIX_MODE)
    {
      if (pv == 0 || pv == 1)
      {
        m_charger_text->SetBackgroundColour(wxColour(255, 0, 0));
      }
      if (pv == 2 || pv == 3)
      {
        m_charger_text->SetBackgroundColour(wxColour(255, 127, 0));
      }
      if (pv >= 4)
      {
        m_charger_text->SetBackgroundColour(wxColour(0, 255, 0));
      }
    }
    else
    {
        if (pv >= 4)
        {
            m_charger_text->SetBackgroundColour(wxColour(0, 255, 0));
        }
        else
        {
          m_charger_text->SetBackgroundColour(wxColour(127, 127, 127));
        }
    }     
  }
  */

  sprintf(text, "in: %d kW", pv);
  m_pv_text->SetValue(text);  

  if (m == 1 && pv < 4)
  {
    int grid = 4 - pv;

    sprintf(text, "out: %d kW (%d kW from grid)", charger, grid);
  }
  else
  {
    sprintf(text, "out: %d kW", charger);
  }

  m_charger_text->SetValue(text);
}

class MyApp : public wxApp
{
public:
  virtual bool OnInit();
};
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

  // frame->Fit();
  frame->SetSize(350, 200);
  frame->Show(true);


  return true;
}
#ifdef USE_CONSOLE
  wxIMPLEMENT_APP_CONSOLE(MyApp);
#else
  wxIMPLEMENT_APP(MyApp);
#endif