/*
// Copyright (c) 2022 Cascoda Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/
#include "ipadapter.h"
#include <process.h>
#include <string.h>
#include <Windows.h>
#include <inttypes.h>
#include <../oc_log.h>
#include <../dns-sd.h>

// globally needed
intptr_t process_handle = 0;
static char sp_text_record[16] = ""; // may be filled at runtime with sleep seconds

uint16_t knx_get_used_port(void) { return get_ip_context_for_device()->port; }

int knx_publish_service(char* serial_no, uint64_t iid, uint16_t ia, bool pm)
{
  // for the case if DNS_SD is disabled
  (void) serial_no;
  (void) iid;
  (void) ia;
  (void) pm;

#ifdef OC_DNS_SD

  char subtypes[64];
  char port_str[7]; // max 65535 + /0 chars 

  // if already present, kill first
  if (process_handle != 0)
  {
    TerminateProcess((HANDLE) process_handle, 0);
  }

  // stringify port
  const uint16_t port = knx_get_used_port();
  (void)snprintf(port_str, sizeof(port_str), "%d", port);

  // stringify prog mode
  char* pm_subtype = pm ? ",_pm" : "";

  // stringify subtypes
  (void) snprintf(subtypes, 63, "_knx._udp,_ia%" PRIx64 "-%x%s,_%s", iid, ia, pm_subtype, serial_no);

  // creates/executes a new process (may need to install a dns client)
  // Use CreateProcess with conditional window visibility
  STARTUPINFOA si = {0};           // Initialize all fields to 0
  PROCESS_INFORMATION pi = {0};    // Initialize all fields to 0
  
  si.cb = sizeof(si);              // Required: tell Windows the structure size
  
  // Build command line: dns-sd -R <Name> <Type> <Domain> <Port> [<TXT>...]
  char cmdline[512];
  (void)snprintf(cmdline, sizeof(cmdline), "dns-sd -R \"%s\" \"%s\" \"local\" \"%s\" \"%s\"", 
                 serial_no, subtypes, port_str, sp_text_record);
  
  // Set creation flags based on console preference
  #ifndef USE_CONSOLE
    DWORD creation_flags = CREATE_NO_WINDOW;    // Hide console window
  #else
    DWORD creation_flags = 0;                   // Show console window
  #endif
    
  // Create process with appropriate window visibility
  if (CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, creation_flags, NULL, NULL, &si, &pi)) {
    process_handle = (intptr_t)pi.hProcess;
    CloseHandle(pi.hThread); // Don't need thread handle
  } else {
    process_handle = 0;
  }
  #endif 

  return 0;
}

void knx_service_sleep_period(int sp)
{
  if (sp)
    // string includes "SP=xx"
    (void)sprintf(sp_text_record, "SP=%d", sp);
  else
    // empty the string (maybe it was set to SP=xxx before) 
    memset(sp_text_record, 0, sizeof(sp_text_record));
    
}
