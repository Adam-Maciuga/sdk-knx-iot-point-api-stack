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

  void SetSpeed(int ms)
  { // smaller = faster
    m_speed = std::max(10, ms);
    if (m_timer.IsRunning())
      m_timer.Start(m_speed);
  }

  void SetColor(const wxColour& c)
  {
    m_color = c;
    Refresh();
  }

  void SetFlowEnabled(bool enabled)
  {
    if (enabled)
      Start();
    else
      Stop();
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
  wxString dumpDeviceIDs();
  wxString dumpDeviceSettings();
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
 
  void ProcessUpdateFromBus();
  void updateDeviceData();
  void add_bool_to_text(bool on_off, char* text);
  void int2text(int value, char* text);
  void int2gatext(uint32_t value, char* text, bool as_ets = false);
  void int2grpidtext(uint64_t value, char* text, bool as_ets);
  void int2scopetext(uint32_t value, char* text);
  void double2text(double value, char* text);

  wxMenu* m_menuFile;
  wxTimer m_timer;

  // sleepy information
  int m_sleep_counter = 0;
  int m_sleep_milliseconds = 20000;

  int m_lsm = LSM_S_UNLOADED;

  int m_chargeRate = -1;

  // non static device properties
  wxTextCtrl* m_charger_text; // text control for charger

  FlowAnimation* m_flow = nullptr;
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
  

  wxColor wxBackColor = this->GetBackgroundColour(); 

  // Create a vertical box sizer for the whole section
  wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL);

  wxBoxSizer* hbox1 = new wxBoxSizer(wxHORIZONTAL);
  m_charger_text = new wxTextCtrl(this, LS_TEXT, "out: 0 kW", wxDefaultPosition, wxSize(100, 25), wxBORDER_NONE);
  m_charger_text->SetBackgroundColour(wxBackColor);
  hbox1->Add(m_charger_text, 1, wxEXPAND); // stretches horizontally
    
  wxBoxSizer* hbox2 = new wxBoxSizer(wxHORIZONTAL);
  m_flow = new FlowAnimation(this);
  m_flow->SetColor(wxColour(0xff, 0, 0));
  hbox2->Add(m_flow, 1, wxEXPAND); // stretches horizontally
  
  vbox->Add(hbox1, 0, wxEXPAND | wxALL, 10);
  vbox->Add(hbox2, 0, wxEXPAND | wxALL, 10);

  this->SetSizerAndFit(vbox);

  constexpr int width_size = 180; // size of the knx info widgets
  char text[500]; 

  // serial number 
  strcpy(text, "Serial Number : ");
  oc_device_info_t* device = oc_core_get_device_info();
  strcat(text, oc_string(device->serialnumber));

  // start the 1ms interval timer for UI updates and stack polls
  m_timer.Bind(wxEVT_TIMER, &MyFrame::OnTimer, this);
  m_timer.Start(1, wxTIMER_CONTINUOUS);

  oc_knx_device_storage_reset(RESET_TO_DEFAULT_STATE);
  // Charger_init_auth_table();
  device->lsm_s = LSM_S_UNLOADED;
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

  if (m_lsm != device->lsm_s)
  {
    m_lsm = device->lsm_s;

    if (m_lsm == LSM_S_LOADING)
    {
      oc_knx_device_storage_reset(RESET_TO_DEFAULT_STATE);
      //oc_knx_device_storage_reset(RESET_TO_DEFAULT_WO_IA);
      // Charger_init_tables_QR("00fa10020c00");
      device->lsm_s = LSM_S_LOADED;
    }
  }
}



/*
void MyFrame::updateDeviceData()
{
  oc_device_info_t* device = oc_core_get_device_info();

  if (m_lsm != device->lsm_s)
  {
    m_lsm = device->lsm_s;

    if (m_lsm == LSM_S_UNLOADED)
    {
      //oc_knx_device_storage_reset(RESET_TO_DEFAULT_WO_IA);
    }
    if (m_lsm == LSM_S_LOADING)
    {
      Charger_init_tables_QR("00fa10020c00");
    }
    if (m_lsm == LSM_S_LOADED)
    {
      //device->lsm_s = LSM_S_LOADED;
    }
  }
}
*/


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
        << "- Represents an EV charger." << "\n"
        << "- Receives and applies the charging request from the CEM app." << "\n";

  CustomDialog("Charger Usage", usage, 480, 130);
}

void MyFrame::OnListAll(wxCommandEvent& event)
{
  const oc_device_info_t* const device = oc_core_get_device_info();
  if (!device)
  {
    return;
  }

  wxString all;
  all << dumpDeviceIDs()        << "\n\n"
      << dumpDeviceSettings()   << "\n\n"
      << dumpGroupObjectTable() << "\n\n"
      << dumpPublisherTable()   << "\n\n"
      << dumpRecipientTable()   << "\n\n"
      << dumpParameterList()    << "\n\n"
      << dumpAuthTable();

  wxString title;
  title.Printf("Charger - Device & Tables - %s", oc_string(device->serialnumber));

  CustomDialog(title, all, 520, 400);
  SetStatusText("List Device & All Tables");
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
        this->int2gatext(entry->ga[i], line, ga_conversion);
      }
      strcat(line, " ]");
      strcat(text, line);
      strcat(text, "\n"); // break to next entry 
    }
    
  }
  strcpy(windowtext, "Group Object Table");
  //strcat(windowtext, oc_string(device->serialnumber));
  CustomDialog(windowtext, text, 520, 300);
  //SetStatusText("List Group Object Table");
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
  //strcat(windowtext, oc_string(device->serialnumber));
  CustomDialog(windowtext, text, 520, 300);
  //SetStatusText("List Publisher Table");
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
  //strcat(windowtext, oc_string(device->serialnumber));
  CustomDialog(windowtext, text, 520, 300);
  //SetStatusText("List Recipient Table");
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
  CustomDialog(windowtext, text, 520, 300);
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
  //strcat(windowtext, oc_string(device->serialnumber));
  CustomDialog(windowtext, text, 520, 300);
  //SetStatusText("List security entries");
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


/**
 * @brief update the UI e.g. check boxes in the UI
 * updates:
 * does a oc_main_poll to give a tick to the stack
 *
 * @param event triggered by a timer
 */
void MyFrame::ProcessUpdateFromBus()
{
  // the actual processing of bus events is in this case done via put_PV() + process_pv() in knx_iot_virtual.c

  char text[200];
  
  int chargeRate = get_charger_value() / 1000;  


  if (chargeRate != m_chargeRate)
  {
    m_chargeRate = chargeRate;

    if (chargeRate == 0)
      m_flow->Stop();
    else
      m_flow->Start();

    (void)sprintf(text, "out: %d kW", chargeRate);
    m_charger_text->SetValue(text);
  }

  /*

  if (snCEM != m_CEM)
  {
    m_CEM = snCEM;

    // Allocate enough space: 2 hex chars per byte + 1 for null terminator
    char str[13]; // 6 bytes → 12 hex chars + '\0'

    // Format with leading zeros
    snprintf(str, sizeof(str), "%012llx", (unsigned long long)snCEM);

    if (m_CEM == 0)
      strcpy(str, "unlinked");

    SetStatusText(str);

    Charger_init_tables_QR(str);
  }

  */    




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



/**
 * @brief returns a formatted string containing device IDs
 *
 * @return wxString containing formatted device IDs (serial number, IA, IID)
 */
wxString MyFrame::dumpDeviceIDs()
{
  oc_device_info_t* device = oc_core_get_device_info();
  if (!device)
  {
    return wxString("");
  }

  wxString out("- Device IDs:\n");
  char line[256];

  // Serial number
  sprintf(line, "  - Serial number: '%s'\n", oc_string(device->serialnumber));
  out += line;

  // Individual address
  const uint16_t ia_a = device->ia >> 12; // area
  const uint16_t ia_l = device->ia >> 8 & 0xF; // line
  const uint16_t ia_d = device->ia & 0x00FF; // device
  sprintf(line, "  - Individual address: %d.%d.%d (%04x)\n", ia_a, ia_l, ia_d, device->ia);
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
    sprintf(line, "  - Installation ID: %02x%02x:%02x%02x", byte_4, byte_3, byte_2, byte_1);
  }
  else
  {
    sprintf(line, "  - Installation ID: %02x:%02x%02x:%02x%02x", byte_5, byte_4, byte_3, byte_2, byte_1);
  }
  out += line;

  return out;
}

/**
 * @brief returns device settings and load state information
 *
 * @return wxString containing device settings info
 */
wxString MyFrame::dumpDeviceSettings()
{
  oc_device_info_t* device = oc_core_get_device_info();
  if (!device)
  {
    return wxString("");
  }

  wxString out("- Device Settings:\n");

  if (device->lsm_s == LSM_S_UNLOADED)
  {
    out += "  Charger: unloaded\n";
  }
  else if (device->lsm_s == LSM_S_LOADING)
  {
    out += "  Charger: loading\n";
  }
  else if (device->lsm_s == LSM_S_LOADED)
  {
    out += "  Charger: loaded\n";
  }

  return out;
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
  bool ga_conversion = true;

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
  bool ga_conversion = true;
  bool grpid_conversion = true;
  bool iid_conversion = true;

  /*
  int total = oc_core_get_publisher_table_size();
  for (int i = 0; i < total; i++)
  {
    oc_group_table_t* entry = oc_core_get_publisher_table_entry(i);
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
    }
  }
  */

  oc_group_table_t* entry = oc_core_get_publisher_table_entry(0);

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
  out += "\n";

  //  ff32:0030:fd7d:ff7f:6c9a:0000:c285:fba0 -> ff32:0030:fd + iid + 0000 + grpid
  out += "  - address: ff32:0030:fd";

  oc_device_info_t* device = oc_core_get_device_info();
  uint64_t iid = device->iid;

  uint8_t byte_1 = static_cast<uint8_t>(iid);
  uint8_t byte_2 = static_cast<uint8_t>(iid >> 8);
  uint8_t byte_3 = static_cast<uint8_t>(iid >> 16);
  uint8_t byte_4 = static_cast<uint8_t>(iid >> 24);
  uint8_t byte_5 = static_cast<uint8_t>(iid >> 32);

  sprintf(line, "%02x:%02x%02x:%02x%02x", byte_5, byte_4, byte_3, byte_2, byte_1);

  out += line;
  out += ":0000:";

  uint64_t grpid = entry->grpid;

  byte_1 = static_cast<uint8_t>(grpid);
  byte_2 = static_cast<uint8_t>(grpid >> 8);
  byte_3 = static_cast<uint8_t>(grpid >> 16);
  byte_4 = static_cast<uint8_t>(grpid >> 24);

  sprintf(line, "%02x%02x:%02x%02x", byte_4, byte_3, byte_2, byte_1);

  out += line;

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
  bool ga_conversion = true;
  bool grpid_conversion = true;
  bool iid_conversion = true;

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
  bool ga_conversion = true;

  int max_entries = oc_core_get_at_table_size();
  for (int i = 0; i < max_entries; i++)
  {
    oc_auth_at_t* entry = oc_get_auth_at_entry(i);
    if (entry && oc_string_len(entry->id))
    {
      if (entry->profile == OC_PROFILE_COAP_OSCORE)
      {
        if (oc_byte_string_len(entry->osc_contextid) == 6)
        {
          // ga
          strcpy(line, "       - ga: [");
          this->int2gatext(entry->ga[0], line, ga_conversion);
          strcat(line, " ]");
          out += line;
          out += "\n";

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
          out += "\n";
        }
      }
      out += "\n";
    }
  }
  return out;
}
