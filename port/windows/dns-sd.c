/*
// Copyright (c) 2022 Cascoda Ltd.
// Copyright (c) 2024-2025 KNX Association
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
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <inttypes.h>
#include <ctype.h>
#include <../oc_log.h>
#include <../dns-sd.h>
#include "../../api/oc_knx_fp.h"
#include "../../util/oc_memb.h"
#include "../../util/oc_list.h"
#include "../../util/oc_etimer.h"
#include "../../util/oc_process.h"
#include "port/oc_clock.h"

// External function declarations
extern uint64_t oc_core_get_device_iid(void);
extern int oc_core_get_group_table_size(void);
extern oc_group_table_t* oc_core_get_group_table_entry(int index);

// IPv6 resolution request structure for non-blocking operation
typedef struct ipv6_resolution_request {
  struct ipv6_resolution_request* next;
  uint16_t ia;
  int recipient_index;
  int step; // 0=browse, 1=lookup, 2=resolve
  char instance_name[128];
  char hostname[256];
  char output_buffer[8192];
  size_t output_buffer_len;
  HANDLE process_handle;
  HANDLE read_pipe;
  oc_clock_time_t start_time;
  oc_clock_time_t step_start_time;
} ipv6_resolution_request_t;

// Memory pool for resolution requests
OC_MEMB(ipv6_resolution_requests, ipv6_resolution_request_t, 10);
OC_LIST(active_resolution_requests);

// IPv6 resolution process
OC_PROCESS(ipv6_resolution_process, "IPv6 Resolution Process");
static struct oc_etimer resolution_check_timer;

// Forward declarations for parsing functions
static int parse_instance_name(const char* output, char* instance_name, size_t max_len);
static int parse_hostname(const char* output, char* hostname, size_t max_len);
static int parse_ipv6_addresses(const char* output, char* ipv6_addr_out, size_t max_len);
int parse_ipv6_string_to_bytes(const char* ipv6_str, uint8_t ipv6_bytes[16]);

// Forward declarations for async resolution
static int knx_start_ipv6_resolution_async(int recipient_index);
static void knx_process_ipv6_resolution_requests(void);
static void knx_start_resolution_step(ipv6_resolution_request_t* req);
static void knx_handle_resolution_step_complete(ipv6_resolution_request_t* req);
static void knx_complete_ipv6_resolution_request(ipv6_resolution_request_t* req, int result);

// globally needed
intptr_t process_handle = 0;
static char sp_text_record[16] = ""; // may be filled at runtime with sleep seconds
static HANDLE job_handle = NULL;    

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
  
  // build command line: dns-sd -R <Name> <Type> <Domain> <Port> [<TXT>...]
  char cmdline[512];
  (void)snprintf(cmdline, sizeof(cmdline), 
                 "dns-sd -R \"%s\" \"%s\" \"local\" \"%s\" \"%s\"", 
                 serial_no, subtypes, port_str, sp_text_record);
  
  // set creation flags based on console preference
  #ifndef USE_CONSOLE
    DWORD creation_flags = CREATE_NO_WINDOW;    // Hide console window
  #else
    DWORD creation_flags = 0;                   // Show console window
  #endif
    
  // create process with appropriate window visibility
  if (CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, creation_flags, NULL, NULL, &si, &pi))
  {
    // initialize job object once and set auto-kill flag
    if (!job_handle) 
    {
      job_handle = CreateJobObject(NULL, NULL);
      if (job_handle) 
      {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {0};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job_handle, JobObjectExtendedLimitInformation,
                                &jeli, sizeof(jeli));
      }
    }

    // assign new process to the job (if available)
    if (job_handle)
      AssignProcessToJobObject(job_handle, pi.hProcess);

    process_handle = (intptr_t)pi.hProcess;
    CloseHandle(pi.hThread); // don't need thread handle
  }
  else 
  {
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

// Parse instance name from dns-sd -B output
static int parse_instance_name(const char* output, char* instance_name, size_t max_len)
{
  const char* line = output;

  while (line && *line) {
    if (strstr(line, "Add") != NULL) {
      const char* service_pos = strstr(line, "_knx._udp.");
      if (service_pos) {
        service_pos += strlen("_knx._udp.");
        while (*service_pos && (*service_pos == ' ' || *service_pos == '\t' || *service_pos == '.')) {
          service_pos++;
        }

        size_t i = 0;
        while (*service_pos && *service_pos != ' ' && *service_pos != '\t' &&
               *service_pos != '\r' && *service_pos != '\n' && i < max_len - 1) {
          instance_name[i++] = *service_pos++;
        }
        instance_name[i] = '\0';

        if (i > 0 && strcmp(instance_name, "local") != 0) {
          return 0;
        }
      }
    }
    line = strchr(line, '\n');
    if (line) line++;
  }
  return -1;
}

// Parse hostname from dns-sd -L output
static int parse_hostname(const char* output, char* hostname, size_t max_len)
{
  const char* line = output;

  while (line && *line) {
    const char* reached_at = strstr(line, "can be reached at");
    if (reached_at) {
      reached_at += strlen("can be reached at");
      while (*reached_at && (*reached_at == ' ' || *reached_at == '\t')) {
        reached_at++;
      }

      size_t i = 0;
      while (*reached_at && *reached_at != ':' && *reached_at != ' ' &&
             *reached_at != '\t' && *reached_at != '\r' && *reached_at != '\n' &&
             i < max_len - 1) {
        hostname[i++] = *reached_at++;
      }
      hostname[i] = '\0';

      if (i > 0) {
        return 0;
      }
    }
    line = strchr(line, '\n');
    if (line) line++;
  }
  return -1;
}

// Parse IPv6 addresses from dns-sd -G output and select the best one
static int parse_ipv6_addresses(const char* output, char* ipv6_addr_out, size_t max_len)
{
  const char* line = output;
  char best_addr[64] = {0};
  int best_priority = 0;
  BOOL found_any = FALSE;

  ipv6_addr_out[0] = '\0';

  while (line && *line) {
    if (strstr(line, "Add") != NULL) {
      const char* hostname_pos = strstr(line, ".local.");
      if (hostname_pos) {
        const char* ip_start = hostname_pos + strlen(".local.");
        while (*ip_start && (*ip_start == ' ' || *ip_start == '\t')) {
          ip_start++;
        }

        char ipv6_addr[64] = {0};
        size_t i = 0;
        while (*ip_start && (isxdigit(*ip_start) || *ip_start == ':' ||
               *ip_start == '%' || *ip_start == '<' || *ip_start == '>' ||
               *ip_start == '_' || isalpha(*ip_start)) &&
               *ip_start != ' ' && *ip_start != '\t' && i < sizeof(ipv6_addr) - 1) {
          ipv6_addr[i++] = *ip_start++;
        }
        ipv6_addr[i] = '\0';

        if (i > 0 && strchr(ipv6_addr, ':')) {
          int priority = 1; // Default priority
          // Check for global unicast (2000::/3)
          if ((ipv6_addr[0] == '2' || ipv6_addr[0] == '3')) {
            priority = 3;
          }
          // Check for unique local (fc00::/7, fd00::/8)
          else if (strncmp(ipv6_addr, "fc", 2) == 0 || strncmp(ipv6_addr, "fd", 2) == 0 ||
                   strncmp(ipv6_addr, "FC", 2) == 0 || strncmp(ipv6_addr, "FD", 2) == 0) {
            priority = 2;
          }

          OC_DBG("Found IPv6 address: %s (Priority: %d)", ipv6_addr, priority);

          if (!found_any || priority > best_priority) {
            strncpy(best_addr, ipv6_addr, sizeof(best_addr) - 1);
            best_addr[sizeof(best_addr) - 1] = '\0';
            best_priority = priority;
            found_any = TRUE;

            if (priority == 3) { // Global address - stop searching
              break;
            }
          }
        }
      }
    }
    line = strchr(line, '\n');
    if (line) line++;
  }

  if (found_any) {
    strncpy(ipv6_addr_out, best_addr, max_len - 1);
    ipv6_addr_out[max_len - 1] = '\0';
    return 0;
  }

  return -1;
}

// Convert IPv6 string to 16-byte array
int parse_ipv6_string_to_bytes(const char* ipv6_str, uint8_t ipv6_bytes[16])
{
  // Remove any interface identifier (% and after)
  char clean_str[64];
  strncpy(clean_str, ipv6_str, sizeof(clean_str) - 1);
  clean_str[sizeof(clean_str) - 1] = '\0';
  
  char* percent_pos = strchr(clean_str, '%');
  if (percent_pos) {
    *percent_pos = '\0';
  }

  // Use inet_pton for conversion
  struct sockaddr_in6 sa;
  int result = inet_pton(AF_INET6, clean_str, &(sa.sin6_addr));
  if (result == 1) {
    memcpy(ipv6_bytes, &(sa.sin6_addr), 16);
    return 0;
  }

  OC_ERR("Failed to parse IPv6 address: %s", clean_str);
  return -1;
}

// Main IPv6 resolution function implementation moved to replace the stub

// Function to resolve IPv6 for an Individual Address
// Looks up IA in recipient table, checks if already resolved, otherwise starts non-blocking resolution
int knx_resolve_recipient_ipv6_address(uint32_t ia)
{
  #ifdef OC_DNS_SD

  extern int oc_core_get_recipient_table_size(void);
  extern oc_group_table_t* oc_core_get_recipient_table_entry(int index);

  if (ia == 0) {
    OC_ERR("Invalid IA: 0");
    return -1;
  }

  // Find the recipient table entry with this IA
  int recipient_index = -1;
  int table_size = oc_core_get_recipient_table_size();

  for (int i = 0; i < table_size; i++) {
    oc_group_table_t* entry = oc_core_get_recipient_table_entry(i);
    if (entry && entry->ia == ia) {
      recipient_index = i;

      // Check current resolution status
      switch (entry->ipadd.init_status) {
        case OC_IP_STATUS_RESOLVED:
          // Already resolved - return success
          OC_DBG("IPv6 already resolved for IA 0x%04x", (uint16_t)ia);
          return 0;

        case OC_IP_STATUS_RESOLVING:
          // Resolution already in progress - return success (non-blocking)
          OC_DBG("IPv6 resolution already in progress for IA 0x%04x", (uint16_t)ia);
          return 0;

        case OC_IP_STATUS_UNRESOLVED:
        case OC_IP_STATUS_FAILED:
        case OC_IP_STATUS_EXPIRED:
          // Need to start resolution
          OC_DBG("Starting IPv6 resolution for IA 0x%04x", (uint16_t)ia);
          return knx_start_ipv6_resolution_async(recipient_index);

        default:
          OC_ERR("Unknown IPv6 status for IA 0x%04x", (uint16_t)ia);
          return -1;
      }
    }
  }

  // IA not found in recipient table - cannot resolve
  OC_ERR("IA 0x%04x not found in recipient table - cannot resolve", (uint16_t)ia);
  return -1;

  #else
  (void)ia;
  return -1; // DNS-SD disabled
  #endif
}

// Function to resolve IPv6 addresses for all recipient table entries (async)
int knx_resolve_all_recipient_ipv6_addresses(void)
{
  #ifdef OC_DNS_SD
  
  extern int oc_core_get_recipient_table_size(void);
  extern oc_group_table_t* oc_core_get_recipient_table_entry(int index);
  
  int table_size = oc_core_get_recipient_table_size();
  int started_count = 0;
  int already_resolved_count = 0;
  
  OC_DBG("Starting batch async IPv6 resolution for %d recipient table entries", table_size);
  
  for (int i = 0; i < table_size; i++) {
    oc_group_table_t* entry = oc_core_get_recipient_table_entry(i);
    if (entry && entry->ia > 0 && entry->ga_len > 0) {
      // Only resolve entries that have valid IA and GAs
      if (entry->ipadd.init_status == OC_IP_STATUS_UNRESOLVED || 
          entry->ipadd.init_status == OC_IP_STATUS_FAILED) {
        
        if (knx_start_ipv6_resolution_async(i) == 0) {
          started_count++;
          OC_DBG("Started async IPv6 resolution for recipient table entry %d (IA: 0x%04x)", i, (uint16_t)entry->ia);
        }
      } else if (entry->ipadd.init_status == OC_IP_STATUS_RESOLVED) {
        already_resolved_count++; // Already resolved  
      }
    }
  }
  
  OC_DBG("Batch async IPv6 resolution started: %d new requests, %d already resolved", started_count, already_resolved_count);
  
  return 0; // Always return success for async operation
  
  #else
  return -1; // DNS-SD disabled
  #endif
}

// Utility function to convert IPv6 bytes back to string format
int knx_ipv6_bytes_to_string(const uint8_t ipv6_bytes[16], char* ipv6_string, size_t max_len)
{
  if (!ipv6_bytes || !ipv6_string || max_len < 46) {  // IPv6 max length is 45 chars + null terminator
    OC_ERR("Invalid parameters for IPv6 bytes to string conversion");
    return -1;
  }

  struct sockaddr_in6 sa;
  memset(&sa, 0, sizeof(sa));
  sa.sin6_family = AF_INET6;
  memcpy(&(sa.sin6_addr), ipv6_bytes, 16);
  
  const char* result = inet_ntop(AF_INET6, &(sa.sin6_addr), ipv6_string, (int)max_len);
  if (result != NULL) {
    OC_DBG("IPv6 bytes to string conversion successful: %s", ipv6_string);
    return 0;
  } else {
    // Fallback: format as 8 groups of 16-bit hex, no zero-compression.
    // Example: abcd:1234:... (raw groups)
    // Ensure buffer is large enough (we already checked max_len >= 46).
    int written = snprintf(
      ipv6_string,
      max_len,
      "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
      ipv6_bytes[0], ipv6_bytes[1], ipv6_bytes[2], ipv6_bytes[3],
      ipv6_bytes[4], ipv6_bytes[5], ipv6_bytes[6], ipv6_bytes[7],
      ipv6_bytes[8], ipv6_bytes[9], ipv6_bytes[10], ipv6_bytes[11],
      ipv6_bytes[12], ipv6_bytes[13], ipv6_bytes[14], ipv6_bytes[15]
    );
    if (written > 0 && (size_t)written < max_len) {
      OC_DBG("inet_ntop failed; used raw IPv6 fallback: %s", ipv6_string);
      return 0;
    }
    OC_ERR("IPv6 conversion failed and fallback formatting overflowed");
    return -1;
  }
}

// Check IPv6 resolution status for a recipient table entry
oc_ip_status_t knx_get_recipient_ipv6_status(int recipient_index)
{
  extern oc_group_table_t* oc_core_get_recipient_table_entry(int index);
  
  oc_group_table_t* entry = oc_core_get_recipient_table_entry(recipient_index);
  if (!entry) {
    return OC_IP_STATUS_FAILED;
  }
  
  return entry->ipadd.init_status;
}

// Get IPv6 address from recipient table entry as string
int knx_get_recipient_ipv6_string(int recipient_index, char* ipv6_string, size_t max_len)
{
  extern oc_group_table_t* oc_core_get_recipient_table_entry(int index);
  
  oc_group_table_t* entry = oc_core_get_recipient_table_entry(recipient_index);
  if (!entry || entry->ipadd.init_status != OC_IP_STATUS_RESOLVED) {
    return -1;
  }
  
  return knx_ipv6_bytes_to_string(entry->ipadd.ipv6, ipv6_string, max_len);
}

// Reset IPv6 resolution status for a recipient table entry (force re-resolution)
int knx_reset_recipient_ipv6_status(int recipient_index)
{
  extern oc_group_table_t* oc_core_get_recipient_table_entry(int index);
  
  oc_group_table_t* entry = oc_core_get_recipient_table_entry(recipient_index);
  if (!entry) {
    return -1;
  }
  
  entry->ipadd.init_status = OC_IP_STATUS_UNRESOLVED;
  memset(entry->ipadd.ipv6, 0, 16);
  
  OC_DBG("Reset IPv6 status for recipient IA 0x%04x", (uint16_t)entry->ia);
  return 0;
}

// Non-blocking IPv6 resolution process thread
OC_PROCESS_THREAD(ipv6_resolution_process, ev, data)
{
  OC_PROCESS_BEGIN();

  // Initialize on first run
  static bool initialized = false;
  if (!initialized) {
    oc_memb_init(&ipv6_resolution_requests);
    oc_list_init(active_resolution_requests);
    initialized = true;
  }

  while (1) {
    OC_PROCESS_WAIT_EVENT();

    if (ev == OC_PROCESS_EVENT_TIMER && data == &resolution_check_timer) {
      // Timer fired - process active resolution requests
      knx_process_ipv6_resolution_requests();

      // Restart timer if there are still active requests
      if (oc_list_length(active_resolution_requests) > 0) {
        oc_etimer_set(&resolution_check_timer, OC_CLOCK_SECOND / 10); // Check every 100ms
      }
    } else if (ev == OC_PROCESS_EVENT_POLL) {
      // Process was polled - handle active requests
      knx_process_ipv6_resolution_requests();

      // Set up timer for continuous processing if there are active requests and timer is not already running
      if (oc_list_length(active_resolution_requests) > 0) {
        if (oc_etimer_expired(&resolution_check_timer)) {
          oc_etimer_set(&resolution_check_timer, OC_CLOCK_SECOND / 10); // Check every 100ms
        }
      }
    }
  }

  OC_PROCESS_END();
}

// Start IPv6 resolution for a recipient
static int knx_start_ipv6_resolution_async(int recipient_index)
{
  #ifdef OC_DNS_SD

  extern oc_group_table_t* oc_core_get_recipient_table_entry(int index);

  // Ensure process is started
  static bool process_started = false;
  if (!process_started) {
    oc_process_start(&ipv6_resolution_process, NULL);
    process_started = true;
    OC_DBG("Started IPv6 resolution process");
  }

  oc_group_table_t* entry = oc_core_get_recipient_table_entry(recipient_index);
  if (!entry || entry->ia <= 0) {
    return -1;
  }

  // Check if already resolved or in progress
  if (entry->ipadd.init_status == OC_IP_STATUS_RESOLVED ||
      entry->ipadd.init_status == OC_IP_STATUS_RESOLVING) {
    return 0;
  }

  // Allocate resolution request
  ipv6_resolution_request_t* req = oc_memb_alloc(&ipv6_resolution_requests);
  if (!req) {
    OC_ERR("No memory for IPv6 resolution request");
    return -1;
  }

  // Initialize request
  req->ia = (uint16_t)entry->ia;
  req->recipient_index = recipient_index;
  req->step = 0; // Start with browse
  req->instance_name[0] = '\0';
  req->hostname[0] = '\0';
  req->output_buffer[0] = '\0';
  req->output_buffer_len = 0;
  req->process_handle = NULL;
  req->read_pipe = NULL;
  req->start_time = oc_clock_time();
  req->step_start_time = oc_clock_time();

  // Set status to initializing
  entry->ipadd.init_status = OC_IP_STATUS_RESOLVING;

  // Add to active requests
  oc_list_add(active_resolution_requests, req);

  // Trigger the resolution process to run
  oc_process_poll(&ipv6_resolution_process);

  OC_DBG("Started async IPv6 resolution for IA 0x%04x (recipient %d)", req->ia, recipient_index);
  return 0;

  #else
  (void)recipient_index;
  return -1;
  #endif
}

// Process IPv6 resolution requests (called by timer)
static void knx_process_ipv6_resolution_requests(void)
{
  #ifdef OC_DNS_SD

  ipv6_resolution_request_t* req = oc_list_head(active_resolution_requests);
  ipv6_resolution_request_t* next;

  while (req) {
    next = req->next;

    // Check for timeout (30 seconds)
    if (oc_clock_time() - req->start_time > 30 * OC_CLOCK_SECOND) {
      OC_ERR("IPv6 resolution timeout for IA 0x%04x", req->ia);
      knx_complete_ipv6_resolution_request(req, -1);
      req = next;
      continue;
    }

    // Check if current step process is still running
    if (req->process_handle) {
      // Read available data from pipe
      if (req->read_pipe) {
        DWORD bytes_available = 0;
        if (PeekNamedPipe(req->read_pipe, NULL, 0, NULL, &bytes_available, NULL) && bytes_available > 0) {
          char temp_buffer[4096];
          DWORD bytes_read = 0;
          if (ReadFile(req->read_pipe, temp_buffer, sizeof(temp_buffer) - 1, &bytes_read, NULL) && bytes_read > 0) {
            temp_buffer[bytes_read] = '\0';
            // Append to output buffer
            size_t remaining = sizeof(req->output_buffer) - req->output_buffer_len - 1;
            if (remaining > 0) {
              strncat(req->output_buffer, temp_buffer, remaining);
              req->output_buffer_len += bytes_read;
              if (req->output_buffer_len >= sizeof(req->output_buffer) - 1) {
                req->output_buffer_len = sizeof(req->output_buffer) - 1;
              }
            }
          }
        }
      }

      // Check if process has finished
      DWORD exit_code;
      if (GetExitCodeProcess(req->process_handle, &exit_code)) {
        if (exit_code == STILL_ACTIVE) {
          // Check for step timeout (5 seconds per step)
          if (oc_clock_time() - req->step_start_time > 5 * OC_CLOCK_SECOND) {
            // Check if we got useful data
            if (req->output_buffer_len > 0 &&
                (strstr(req->output_buffer, "Add") || strstr(req->output_buffer, "can be reached"))) {
              // We have data, process it
              TerminateProcess(req->process_handle, 0);
              knx_handle_resolution_step_complete(req);
            } else {
              // Timeout without data
              OC_ERR("Step %d timeout for IA 0x%04x", req->step, req->ia);
              knx_complete_ipv6_resolution_request(req, -1);
            }
          }
          // Still running, continue to next request
          req = next;
          continue;
        } else {
          // Process finished, read any remaining data and move to next step
          if (req->read_pipe) {
            // Read remaining data
            char temp_buffer[4096];
            DWORD bytes_read = 0;
            while (ReadFile(req->read_pipe, temp_buffer, sizeof(temp_buffer) - 1, &bytes_read, NULL) && bytes_read > 0) {
              temp_buffer[bytes_read] = '\0';
              size_t remaining = sizeof(req->output_buffer) - req->output_buffer_len - 1;
              if (remaining > 0) {
                strncat(req->output_buffer, temp_buffer, remaining);
                req->output_buffer_len += bytes_read;
                if (req->output_buffer_len >= sizeof(req->output_buffer) - 1) {
                  req->output_buffer_len = sizeof(req->output_buffer) - 1;
                  break;
                }
              }
            }
          }
          knx_handle_resolution_step_complete(req);
        }
      } else {
        // Error checking process, fail this request
        OC_ERR("Error checking resolution process for IA 0x%04x", req->ia);
        knx_complete_ipv6_resolution_request(req, -1);
      }
    } else {
      // Start next resolution step
      knx_start_resolution_step(req);
    }

    req = next;
  }

  #endif
}

// Start a resolution step for a request
static void knx_start_resolution_step(ipv6_resolution_request_t* req)
{
  #ifdef OC_DNS_SD

  char cmdline[512];
  uint64_t iid = oc_core_get_device_iid();

  switch (req->step) {
    case 0: // Browse step
      snprintf(cmdline, sizeof(cmdline), "dns-sd -B _ia%" PRIx64 "-%x._knx._udp local", iid, req->ia);
      break;
    case 1: // Lookup step
      snprintf(cmdline, sizeof(cmdline), "dns-sd -L \"%s\" _knx._udp local", req->instance_name);
      break;
    case 2: // Resolve step
      snprintf(cmdline, sizeof(cmdline), "dns-sd -G v6 %s", req->hostname);
      break;
    default:
      OC_ERR("Invalid resolution step %d for IA 0x%04x", req->step, req->ia);
      knx_complete_ipv6_resolution_request(req, -1);
      return;
  }

  OC_DBG("Starting resolution step %d for IA 0x%04x: %s", req->step, req->ia, cmdline);

  // Create pipe for capturing output
  HANDLE hWritePipe;
  SECURITY_ATTRIBUTES sa = {0};
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = NULL;

  if (!CreatePipe(&req->read_pipe, &hWritePipe, &sa, 0)) {
    OC_ERR("Failed to create pipe for dns-sd");
    knx_complete_ipv6_resolution_request(req, -1);
    return;
  }

  // Ensure read handle is not inherited
  SetHandleInformation(req->read_pipe, HANDLE_FLAG_INHERIT, 0);

  // Start the DNS-SD process asynchronously with output redirection
  STARTUPINFOA si = {0};
  PROCESS_INFORMATION pi = {0};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.hStdOutput = hWritePipe;
  si.hStdError = hWritePipe;
  si.wShowWindow = SW_HIDE;

  char cmdline_copy[512];
  strncpy(cmdline_copy, cmdline, sizeof(cmdline_copy) - 1);
  cmdline_copy[sizeof(cmdline_copy) - 1] = '\0';

  if (CreateProcessA(NULL, cmdline_copy, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
    req->process_handle = pi.hProcess;
    CloseHandle(pi.hThread);
    CloseHandle(hWritePipe); // Close write end in parent process
    req->output_buffer[0] = '\0';
    req->output_buffer_len = 0;
    req->step_start_time = oc_clock_time();
  } else {
    OC_ERR("Failed to start DNS-SD process for IA 0x%04x step %d", req->ia, req->step);
    CloseHandle(req->read_pipe);
    CloseHandle(hWritePipe);
    req->read_pipe = NULL;
    knx_complete_ipv6_resolution_request(req, -1);
  }

  #endif
}

// Handle completion of a resolution step
static void knx_handle_resolution_step_complete(ipv6_resolution_request_t* req)
{
  #ifdef OC_DNS_SD

  extern oc_group_table_t* oc_core_get_recipient_table_entry(int index);

  // Clean up process handle
  if (req->process_handle) {
    CloseHandle(req->process_handle);
    req->process_handle = NULL;
  }

  // Clean up pipe
  if (req->read_pipe) {
    CloseHandle(req->read_pipe);
    req->read_pipe = NULL;
  }

  OC_DBG("Step %d complete for IA 0x%04x, output length: %zu", req->step, req->ia, req->output_buffer_len);

  // Parse the output based on the current step
  int parse_result = -1;

  switch (req->step) {
    case 0: // Browse step - parse instance name
      parse_result = parse_instance_name(req->output_buffer, req->instance_name, sizeof(req->instance_name));
      if (parse_result == 0) {
        OC_DBG("Parsed instance name: %s", req->instance_name);
      } else {
        OC_ERR("Failed to parse instance name from browse output for IA 0x%04x", req->ia);
      }
      break;

    case 1: // Lookup step - parse hostname
      parse_result = parse_hostname(req->output_buffer, req->hostname, sizeof(req->hostname));
      if (parse_result == 0) {
        OC_DBG("Parsed hostname: %s", req->hostname);
      } else {
        OC_ERR("Failed to parse hostname from lookup output for IA 0x%04x", req->ia);
      }
      break;

    case 2: // Resolve step - parse IPv6 address
      {
        char ipv6_string[64] = {0};
        parse_result = parse_ipv6_addresses(req->output_buffer, ipv6_string, sizeof(ipv6_string));
        if (parse_result == 0) {
          OC_DBG("Parsed IPv6 address: %s", ipv6_string);

          // Convert to bytes and store in recipient table
          oc_group_table_t* entry = oc_core_get_recipient_table_entry(req->recipient_index);
          if (entry) {
            if (parse_ipv6_string_to_bytes(ipv6_string, entry->ipadd.ipv6) == 0) {
              // Successfully resolved and stored
              knx_complete_ipv6_resolution_request(req, 0);
              return;
            } else {
              OC_ERR("Failed to convert IPv6 string to bytes for IA 0x%04x", req->ia);
              parse_result = -1;
            }
          }
        } else {
          OC_ERR("Failed to parse IPv6 addresses from resolve output for IA 0x%04x", req->ia);
        }
      }
      break;

    default:
      OC_ERR("Invalid resolution step %d for IA 0x%04x", req->step, req->ia);
      knx_complete_ipv6_resolution_request(req, -1);
      return;
  }

  // Check if parsing failed
  if (parse_result != 0) {
    OC_ERR("Failed to complete step %d for IA 0x%04x", req->step, req->ia);
    knx_complete_ipv6_resolution_request(req, -1);
    return;
  }

  // Move to next step
  req->step++;

  if (req->step >= 3) {
    // All steps completed successfully (should have been handled in step 2)
    OC_ERR("Unexpected: reached step 3 without completion for IA 0x%04x", req->ia);
    knx_complete_ipv6_resolution_request(req, -1);
  } else {
    // Continue with next step - will be started on next timer tick
    req->process_handle = NULL;
  }

  #endif
}

// Complete an IPv6 resolution request
static void knx_complete_ipv6_resolution_request(ipv6_resolution_request_t* req, int result)
{
  extern oc_group_table_t* oc_core_get_recipient_table_entry(int index);

  oc_group_table_t* entry = oc_core_get_recipient_table_entry(req->recipient_index);

  if (entry) {
    if (result == 0) {
      // Resolution successful - IPv6 data should already be set in entry
      entry->ipadd.init_status = OC_IP_STATUS_RESOLVED;
      OC_DBG("Async IPv6 resolution completed successfully for IA 0x%04x", req->ia);
    } else {
      // Resolution failed
      entry->ipadd.init_status = OC_IP_STATUS_FAILED;
      memset(entry->ipadd.ipv6, 0, 16);
      OC_ERR("Async IPv6 resolution failed for IA 0x%04x", req->ia);
    }
  }

  // Clean up
  if (req->process_handle) {
    TerminateProcess(req->process_handle, 0);
    CloseHandle(req->process_handle);
    req->process_handle = NULL;
  }

  if (req->read_pipe) {
    CloseHandle(req->read_pipe);
    req->read_pipe = NULL;
  }

  oc_list_remove(active_resolution_requests, req);
  oc_memb_free(&ipv6_resolution_requests, req);
}

// Print IPv6 resolution status for all recipient table entries (debugging)
void knx_print_recipient_ipv6_status_table(void)
{
  extern int oc_core_get_recipient_table_size(void);
  extern oc_group_table_t* oc_core_get_recipient_table_entry(int index);
  
  int table_size = oc_core_get_recipient_table_size();
  
  OC_DBG("=== Recipient Table IPv6 Status ===");
  
  for (int i = 0; i < table_size; i++) {
    oc_group_table_t* entry = oc_core_get_recipient_table_entry(i);
    if (entry && entry->ia > 0 && entry->ga_len > 0) {
      const char* status_str;
      switch (entry->ipadd.init_status) {
        case OC_IP_STATUS_UNRESOLVED: status_str = "UNRESOLVED"; break;
        case OC_IP_STATUS_RESOLVING: status_str = "RESOLVING"; break;
        case OC_IP_STATUS_RESOLVED: status_str = "RESOLVED"; break;
        case OC_IP_STATUS_FAILED: status_str = "FAILED"; break;
        case OC_IP_STATUS_EXPIRED: status_str = "EXPIRED"; break;
        default: status_str = "UNKNOWN"; break;
      }
      
      if (entry->ipadd.init_status == OC_IP_STATUS_RESOLVED) {
        char ipv6_str[INET6_ADDRSTRLEN];
        if (knx_ipv6_bytes_to_string(entry->ipadd.ipv6, ipv6_str, sizeof(ipv6_str)) == 0) {
          OC_DBG("[%d] IA: 0x%04x, Status: %s, IPv6: %s", 
                 i, (uint16_t)entry->ia, status_str, ipv6_str);
        } else {
          OC_DBG("[%d] IA: 0x%04x, Status: %s, IPv6: <conversion failed>", 
                 i, (uint16_t)entry->ia, status_str);
        }
      } else {
        OC_DBG("[%d] IA: 0x%04x, Status: %s", i, (uint16_t)entry->ia, status_str);
      }
    }
  }
  
  OC_DBG("=== End of Recipient Table ===");
}
