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


//needs to be undefined so wxwidgets will not use precompiled headers when compiling with msvc
#undef WX_PRECOMP

#include <wx/cmdline.h>
#include <wx/wxprec.h>
#include <wx/wx.h>
#include <wx/display.h>

#include "api/oc_knx_dev.h"
#include "api/oc_knx_fp.h"
#include "api/oc_knx_sec.h"
#include "apps/knx_iot_virtual.h"
#include "oc_knx_client.h"
#include "port/dns-sd.h"

enum : uint16_t
{
  RESET = wxID_HIGHEST + 1, // ID for reset button in the menu
  RESET_TABLE = RESET + 1, // ID for clear table button in the menu
  IA_TEXT = RESET_TABLE + 1, // ID for internal address text
  IID_TEXT = IA_TEXT + 1, // ID for installation id text
  PM_TEXT = IID_TEXT + 1, // ID for programming mode text
  LS_TEXT = PM_TEXT + 1, // ID for load status text
  HOSTNAME_TEXT = LS_TEXT + 1, // ID for hostname text
  GOT_TABLE_ID = HOSTNAME_TEXT + 1, // ID for the Group object window
  PUB_TABLE_ID = GOT_TABLE_ID + 1, // ID for the publisher table window
  REC_TABLE_ID = PUB_TABLE_ID + 1, // ID for the recipient table window
  PARAMETER_LIST_ID = REC_TABLE_ID + 1, // ID for the parameter window
  AT_TABLE_ID = PARAMETER_LIST_ID + 1, // ID for the auth/at window
  CHECK_GA_DISPLAY = AT_TABLE_ID + 1, // ga display check
  CHECK_IID_DISPLAY = CHECK_GA_DISPLAY + 1, // iid display check
  CHECK_GRPID_DISPLAY = CHECK_IID_DISPLAY + 1, // grpid display check
  CHECK_SLEEPY = CHECK_GRPID_DISPLAY + 1, // sleepy check
  CHECK_PM = CHECK_SLEEPY + 1, // programming mode check in menu bar

  LSSB_0_SOO = CHECK_PM + 1, 
  LSSB_0_IOO = CHECK_PM + 2,
  LSSB_1_SOO = CHECK_PM + 3,
  LSSB_1_IOO = CHECK_PM + 4,

  LIST_ALL = LSSB_1_IOO + 1
};

extern lsxb_channel_t lsab[NUM_CHANNELS];

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
  closeButton->Bind(wxEVT_BUTTON, &CustomDialog::on_close, this);
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

void CustomDialog::on_close(wxCommandEvent& event)
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
  MyFrame(const char* serial_number);

private:
  void OnListAll(wxCommandEvent& event);
  void OnProgrammingMode(wxCommandEvent& event);
  void OnSleepyMode(wxCommandEvent& event);
  void OnReset(wxCommandEvent& event);
  void OnClearTables(wxCommandEvent& event);
  void OnExit(wxCommandEvent& event);
  void OnAbout(wxCommandEvent& event);
  void OnTimer(wxTimerEvent& event);
  void OnPressed_LSSB_0_SOO(wxCommandEvent& event); // trigger by hand a switch on/off request
  void OnPressed_LSSB_1_SOO(wxCommandEvent& event); // trigger by hand a switch on/off request

  void updateCheckBoxesFromLiveIOOData();
  void updateDeviceData();
  void add_bool_to_text(bool on_off, char* text);
  void int2text(int value, char* text);
  void int2gatext(uint32_t value, char* text, bool as_ets = false);
  void int2grpidtext(uint64_t value, char* text, bool as_ets);
  void int2scopetext(uint32_t value, char* text);
  void double2text(double value, char* text);

  wxString dumpGroupObjectTable();
  wxString dumpPublisherTable();
  wxString dumpRecipientTable();
  wxString dumpParameterList();
  wxString dumpAuthTable();

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

  // channel 0
  wxCheckBox *m_LSSB_0_IOO, *m_LSSB_1_IOO;
  wxButton *m_LSSB_0_SOO, *m_LSSB_1_SOO;

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

  // call in c-code
  app_initialize_stack();

  wxString serial_number;
  if (g_cmd->Found("s", &serial_number))
  {
  }

  MyFrame* frame = new MyFrame(const_cast<char*>((serial_number.c_str()).AsChar()));

  frame->Fit();
  frame->Show(true);
  return true;
}

/**
 * @brief Construct a new My Frame:: My Frame object
 *
 * @param serial_number
 */
MyFrame::MyFrame(const char* serial_number) : wxFrame(nullptr, wxID_ANY, "KNX virtual sensor (LSSB)")
{
  m_menuFile = new wxMenu;
  m_menuFile->Append(LIST_ALL, "List All Tables", "List all tables in one window", false);
  m_menuFile->Append(CHECK_PM, "Programming Mode", "Sets the application in programming mode", true);
  m_menuFile->Append(RESET_TABLE, "Reset (7) (Tables)", "Reset 7 (Reset to default without IA).", false);
  m_menuFile->Append(RESET, "Reset (2)(ex-factory)", "Reset 2 (Reset to default state)", false);
  m_menuFile->AppendSeparator();
  m_menuFile->Append(wxID_EXIT);

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
  menuBar->Append(m_menuFile, "&File");
  menuBar->Append(m_menuDisplay, "&Display");
  menuBar->Append(m_menuOptions, "&Options");
  menuBar->Append(menuHelp, "&Help");
  wxFrameBase::SetMenuBar(menuBar);
  wxFrameBase::CreateStatusBar();
  wxFrameBase::SetStatusText("Welcome to KNX virtual sensor!");

  Bind(wxEVT_MENU, &MyFrame::OnReset, this, RESET);
  Bind(wxEVT_MENU, &MyFrame::OnListAll, this, LIST_ALL);
  Bind(wxEVT_MENU, &MyFrame::OnClearTables, this, RESET_TABLE);
  Bind(wxEVT_MENU, &MyFrame::OnProgrammingMode, this, CHECK_PM);
  Bind(wxEVT_MENU, &MyFrame::OnSleepyMode, this, CHECK_SLEEPY);
  Bind(wxEVT_MENU, &MyFrame::OnReset, this, RESET);
  Bind(wxEVT_MENU, &MyFrame::OnAbout, this, wxID_ABOUT);
  Bind(wxEVT_MENU, &MyFrame::OnExit, this, wxID_EXIT);

  int x_width = 100; // width of the widgets
  int x_height = 25; // height of the widgets
  int max_instances = 4; // number of channels before the rest is shown
  int row;
  int column;

  // channel 0 - sensor
  {
    row = 0;
    column = 0;

    wxStaticText* ch0s =
      new wxStaticText(this, wxID_ANY, "Ch 0 | Sensor", 
                       wxPoint(10 + column * x_width, 10 + x_height * row),
                       wxSize(x_width, x_height), wxALIGN_LEFT);

    // control
    m_LSSB_0_SOO = new wxButton(this, LSSB_0_SOO, _T("SOO, press me ..."),
                                wxPoint(120 + column * x_width, 10 + x_height * row), 
                                wxSize(x_width, x_height), 0);

    m_LSSB_0_SOO->Bind(wxEVT_BUTTON, &MyFrame::OnPressed_LSSB_0_SOO, this);
    m_LSSB_0_SOO->Enable(true);

    row = 0;
    column = 1;

    // status
    m_LSSB_0_IOO = new wxCheckBox(this, LSSB_0_IOO, _T("Undefined"), 
                                  wxPoint(140 + column * x_width, 10 + x_height * row),
                                  wxSize(x_width, x_height), wxCHK_3STATE);

    m_LSSB_0_IOO->Set3StateValue(wxCHK_UNDETERMINED);
    m_LSSB_0_IOO->Enable(false);
  }

  // channel 1 - sensor
  {
    row = 1;
    column = 0;

    wxStaticText* ch1s =
      new wxStaticText(this, wxID_ANY, "Ch 1 | Sensor", wxPoint(10 + column * x_width, 10 + x_height * row),
                       wxSize(x_width, x_height), wxALIGN_LEFT);

    // control
    m_LSSB_1_SOO = new wxButton(this, LSSB_1_SOO, _T("SOO, press me ..."),
                                wxPoint(120 + column * x_width, 10 + x_height * row), wxSize(x_width, x_height), 0);

    m_LSSB_1_SOO->Bind(wxEVT_BUTTON, &MyFrame::OnPressed_LSSB_1_SOO, this);
    m_LSSB_1_SOO->Enable(true);

    row = 1;
    column = 1;

    // status
    m_LSSB_1_IOO = new wxCheckBox(this, LSSB_1_IOO, _T("Undefined"), wxPoint(140 + column * x_width, 10 + x_height * row),
                                  wxSize(x_width, x_height), wxCHK_3STATE);

    m_LSSB_1_IOO->Set3StateValue(wxCHK_UNDETERMINED);
    m_LSSB_1_IOO->Enable(false);
  }

  // serial number
  if (strlen(serial_number) > 1)
  {
    // sn was set by command line 
    app_set_serial_number(serial_number);
  }


  constexpr int width_size = 180; // size of the knx info widgets
  char text[500]; 

  // serial number 
  strcpy(text, "Serial Number : ");
  oc_device_info_t* device = oc_core_get_device_info(0);
  strcat(text, oc_string(device->serialnumber));

  wxTextCtrl* static_text0 = new wxTextCtrl(this, wxID_ANY, text, 
                                          wxPoint(10, 10 + ((max_instances + 1) * x_height)),
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
  strcpy(text, "QR Code:   KNX:S:");
  strcat(text, oc_string(device->serialnumber));
  strcat(text, ";P:");
  strcat(text, app_get_password());
  app_str_to_upper(text);

  wxTextCtrl* static_text1 = new wxTextCtrl(this, wxID_ANY, text, 
                                            wxPoint(10, 10 + ((max_instances + 2) * x_height)),
                                            wxSize(width_size * 2, x_height), 0);
  static_text1->SetEditable(false);

  // individual address, displayed data set/refreshed later
  m_ia_text = new wxTextCtrl(this, IA_TEXT, "",
                             wxPoint(10, 10 + ((max_instances + 3) * x_height)), 
                             wxSize(width_size, x_height), 0);
  m_ia_text->SetEditable(false);

  // installation id, displayed data set/refreshed later
  m_iid_text = new wxTextCtrl(this, IID_TEXT, "", 
                              wxPoint(10 + width_size, 10 + ((max_instances + 3) * x_height)),
                              wxSize(width_size, x_height), 0);
  m_iid_text->SetEditable(false);

  // programming mode, displayed data set/refreshed later
  m_pm_text = new wxTextCtrl(this, PM_TEXT, "",
                             wxPoint(10, 10 + ((max_instances + 4) * x_height)), 
                             wxSize(width_size, x_height), 0);
  m_pm_text->SetEditable(false);

  // installation id, displayed data set/refreshed later
  m_ls_text = new wxTextCtrl(this, LS_TEXT, "", 
                             wxPoint(10 + width_size, 10 + ((max_instances + 4) * 25)),
                             wxSize(width_size, 25), 0);
  m_ls_text->SetEditable(false);

  // hostname, displayed data set/refreshed later
  m_hn_text = new wxTextCtrl(this, LS_TEXT, "",
                                   wxPoint(10, 10 + ((max_instances + 5) * 25)), 
                                   wxSize(width_size, 25), 0);
  m_hn_text->SetEditable(false);

  // SPAKE 2+ pwd
  strcpy(text, app_get_password());
  wxTextCtrl* static_text2 = new wxTextCtrl(this, LS_TEXT, text, 
                                  wxPoint(10 + width_size, 10 + ((max_instances + 5) * 25)),
                                  wxSize(width_size, 25), wxTE_RICH);
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
  oc_device_info_t* device = oc_core_get_device_info(0);
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
  oc_device_info_t* device = oc_core_get_device_info(0);

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
  oc_device_info_t* device = oc_core_get_device_info(0);
  
  const uint16_t ia_a = device->ia >> 12; // area
  const uint16_t ia_l = device->ia >> 8 & 0xF; // line
  const uint16_t ia_d = device->ia & 0x00FF; // device
  (void)sprintf(text, "IA : %d.%d.%d [%d]", ia_a, ia_l, ia_d, device->ia);
  m_ia_text->SetValue(text);

  (void)sprintf(text, "LoadState : %s", oc_core_get_lsm_state_as_string(device->lsm_s));
  m_pm_text->SetValue(text);

  (void)sprintf(text, "Programming Mode : %d", device->pm);
  m_ls_text->SetValue(text);

  strcpy(text, "IID : ");
  this->int2grpidtext(device->iid, text, iid_conversion);
  m_iid_text->SetValue(text);

  (void)sprintf(text, "Hostname : %s", oc_string(device->hostname));
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
  oc_knx_device_storage_reset(0, RESET_TO_DEFAULT_WO_IA);
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
  oc_knx_device_storage_reset(0, RESET_TO_DEFAULT_STATE);
  // update the UI
  this->updateDeviceData();
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
  oc_device_info_t* device = oc_core_get_device_info(0);
  if (!device) {
    return;
  }

  wxString all;
  all << dumpGroupObjectTable() << "\n\n"
      << dumpPublisherTable()   << "\n\n"
      << dumpRecipientTable()   << "\n\n"
      << dumpParameterList()    << "\n\n"
      << dumpAuthTable();

  wxString title;
  title.Printf("LSSB - All Tables - %s", oc_string(device->serialnumber));

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
    if (oc_knx_device_in_programming_mode(0))
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

  char text[200];
  bool p;

  // update check box
  p = app_retrieve_bool_variable_from_channel(0, IOO);
  m_LSSB_0_IOO->Set3StateValue(p ? wxCHK_CHECKED : wxCHK_UNCHECKED);

  // update check box text
  strcpy(text, "IOO = ");
  this->add_bool_to_text(p, text);
  m_LSSB_0_IOO->SetLabel(text);

  // update check box
  p = app_retrieve_bool_variable_from_channel(1, IOO);
  m_LSSB_1_IOO->Set3StateValue(p ? wxCHK_CHECKED : wxCHK_UNCHECKED);

  // update check box text
  strcpy(text, "IOO = ");
  this->add_bool_to_text(p, text);
  m_LSSB_1_IOO->SetLabel(text);
  
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


// trigger by hand a switch on/off request
void MyFrame::OnPressed_LSSB_0_SOO(wxCommandEvent& event)
{
  // get url from SOO 
  char* url = app_retrieve_href_from_channel(0, SOO);
  bool p = app_retrieve_bool_variable_from_channel(0, SOO);

  // toggle value
  p = !p;

  // set value
  app_set_bool_variable_from_channel(0, SOO, p);

  // send out, multicast
  oc_issue_s_mode_with_scope_and_check_mc_or_uc(SENDER_SCOPE, url, "w");  

  // update button text
  char text[200];
  strcpy(text, "SOO = ");

  this->add_bool_to_text(p, text);
  m_LSSB_0_SOO->SetLabel(text);

  // show in status bar
  char statusBarText[100];
  (void)sprintf(statusBarText, "Switch On/Off @ '%s' pressed: %s", url, p ? "On" : "Off");
  SetStatusText(statusBarText);
}

// trigger by hand a switch on/off request
void MyFrame::OnPressed_LSSB_1_SOO(wxCommandEvent& event)
{
  // get url from IOO
  char* url = app_retrieve_href_from_channel(1, SOO);
  bool p = app_retrieve_bool_variable_from_channel(1, SOO);

  // toggle value
  p = !p;

  // set value
  app_set_bool_variable_from_channel(1, SOO, p);

  // send out, multicast
  oc_issue_s_mode_with_scope_and_check_mc_or_uc(SENDER_SCOPE, url, "w");

  // update button text
  char text[200];
  strcpy(text, "SOO = ");

  this->add_bool_to_text(p, text);
  m_LSSB_1_SOO->SetLabel(text);

  // show in status bar
  char statusBarText[100];
  (void)sprintf(statusBarText, "Switch On/Off @ '%s' pressed: %s", url, p ? "On" : "Off");
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
  wxString out("=== Group Object Table ===\n");
  char line[512];
  bool ga_conversion = m_menuDisplay->IsChecked(CHECK_GA_DISPLAY);

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
  wxString out("=== Publisher Table ===\n");
  char line[256];
  bool ga_conversion = m_menuDisplay->IsChecked(CHECK_GA_DISPLAY);
  bool grpid_conversion = m_menuDisplay->IsChecked(CHECK_GRPID_DISPLAY);
  bool iid_conversion = m_menuDisplay->IsChecked(CHECK_IID_DISPLAY);

  int total = oc_core_get_publisher_table_size();
  for (int i = 0; i < total; i++) {
    oc_group_table_t* entry = oc_core_get_publisher_table_entry(i);
    if (entry && entry->id >= 0) {
      sprintf(line, "Index %d ", i); out += line;
      sprintf(line, "  id: '%d'  ", entry->id); out += line;
      if (entry->ia >= 0) { sprintf(line, "  ia: '%d' ", entry->ia); out += line; }
      if (entry->iid >= 0) { strcpy(line, "  iid: "); this->int2grpidtext(entry->iid, line, iid_conversion); out += line; }
      if (entry->fid >= 0) { sprintf(line, "  fid: '%lld' ", entry->fid); out += line; }
      if (entry->grpid > 0) { strcpy(line, "  grpid: "); this->int2grpidtext(entry->grpid, line, grpid_conversion); out += line; }
      if (oc_string_len(entry->at) > 0) { sprintf(line, "  at: '%s' ", oc_string(entry->at)); out += line; }
      if (entry->ga_len > 0) {
        strcpy(line, "  ga : [");
        for (int j = 0; j < entry->ga_len; j++) {
          this->int2gatext(entry->ga[j], line, ga_conversion);
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
  wxString out("=== Recipient Table ===\n");
  char line[256];
  bool ga_conversion = m_menuDisplay->IsChecked(CHECK_GA_DISPLAY);
  bool grpid_conversion = m_menuDisplay->IsChecked(CHECK_GRPID_DISPLAY);
  bool iid_conversion = m_menuDisplay->IsChecked(CHECK_IID_DISPLAY);

  int total = oc_core_get_recipient_table_size();
  for (int i = 0; i < total; i++) {
    oc_group_table_t* entry = oc_core_get_recipient_table_entry(i);
    if (entry && entry->id >= 0) {
      sprintf(line, "Index %d ", i); out += line;
      sprintf(line, "  id: '%d'  ", entry->id); out += line;
      if (entry->ia >= 0) { sprintf(line, "  ia: '%d' ", entry->ia); out += line; }
      if (entry->iid >= 0) { strcpy(line, "  iid: "); this->int2grpidtext(entry->iid, line, iid_conversion); out += line; }
      if (entry->fid >= 0) { sprintf(line, "  fid: '%lld' ", entry->fid); out += line; }
      if (entry->grpid > 0) { strcpy(line, "  grpid: "); this->int2grpidtext(entry->grpid, line, grpid_conversion); out += line; }
      if (oc_string_len(entry->at) > 0) { sprintf(line, "  at: '%s' ", oc_string(entry->at)); out += line; }
      if (entry->ga_len > 0) {
        strcpy(line, "  ga : [");
        for (int j = 0; j < entry->ga_len; j++) {
          this->int2gatext(entry->ga[j], line, ga_conversion);
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
  if (url == NULL) {
    out += "no parameters in this device\n";
  }
  while (url) {
    sprintf(line, "\nIndex %02d ", index); out += line;
    sprintf(line, "  url : '%s'  ", url); out += line;
    char* name = app_get_parameter_name(index);
    if (name) { sprintf(line, "  name: '%s'  ", name); out += line; }
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
  wxString out("=== Auth/AT Table ===\n");
  char line[512];
  bool ga_conversion = m_menuDisplay->IsChecked(CHECK_GA_DISPLAY);

  int max_entries = oc_core_get_at_table_size();
  for (int i = 0; i < max_entries; i++) {
    oc_auth_at_t* entry = oc_get_auth_at_entry(i);
    if (entry && oc_string_len(entry->id)) {
      sprintf(line, "index : '%d' id = '%s' ", i, oc_string(entry->id));
      out += line;
      sprintf(line, "  profile : %d (%s)", entry->profile,
              oc_at_profile_to_string(entry->profile));
      out += line;

      if (entry->profile == OC_PROFILE_COAP_DTLS) {
        if (oc_string_len(entry->sub) > 0) {
          sprintf(line, "  sub : %s", oc_string(entry->sub)); out += line;
        }
        if (oc_string_len(entry->kid) > 0) {
          sprintf(line, "  kid : %s", oc_string(entry->kid)); out += line;
        }
      }

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
            this->int2gatext(entry->ga[j], line, ga_conversion);
          }
          strcat(line, " ]");
          out += line;
        } else {
          sprintf(line, "  scope : ");
          this->int2scopetext(entry->scope, line);
          out += line;
        }
      }
      out += "\n";
    }
  }
  return out;
}
