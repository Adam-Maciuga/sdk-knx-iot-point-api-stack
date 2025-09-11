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
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <ctype.h>
#include <../oc_log.h>
#include <../dns-sd.h>

// globally needed
static pid_t avahi_pid = 0;
static char sp_text_record[16] = ""; // may be filled at runtime with sleep seconds

uint16_t knx_get_used_port(void) { return get_ip_context_for_device(0)->port; }

int knx_publish_service(char *serial_no, uint64_t iid, uint16_t ia, bool pm)
{
  // for the case if DNS_SD is disabled
  (void) serial_no;
  (void) iid;
  (void) ia;
  (void) pm;

#ifdef OC_DNS_SD

  char serial_no_subtype[64];
  char port_str[7]; // max 65535 + /0 chars 
  char serial_no_lowercase[20];
  char installation_subtype[64];
  
  if (avahi_pid != 0) 
  {
    // A previously published service advertisement is still running
    // Kill it so that you can start a new one
    kill(avahi_pid, SIGTERM);
  }

  // Start the service advertisement in a new process
  avahi_pid = fork();

  if (avahi_pid == 0) {
    // we are in the child thread - execute Avahi

    // make sure that the serial number is used in lower case
    strncpy(serial_no_lowercase, serial_no, 19);
    for (int i = 0; i < strlen(serial_no_lowercase); ++i) {
      serial_no_lowercase[i] = tolower(serial_no_lowercase[i]);
    }

    // set up the subtype for the sn
    // --subtype=_001cafe1234._sub._knx._udp
    char *serial_format_string = "--subtype=_%s._sub._knx._udp";
    snprintf(serial_no_subtype, sizeof(serial_no_subtype), 
             serial_format_string,
             serial_no_lowercase);

    // set up the subtype for the iid, ia
    // --subtype=_ia33a3-20a._sub._knx._udp
    char *installation_format_string = "--subtype=_ia%x-%x._sub._knx._udp";
    snprintf(installation_subtype, sizeof(installation_subtype),
             installation_format_string, iid, ia);

    // set up the subtype for the pm
    // --subtype=_pm._sub._knx._udp
    char *pm_subtype = "--subtype=_pm._sub._knx._udp";
    
    // set up the subtype for the port
    const uint16_t port = knx_get_used_port();
    snprintf(port_str, sizeof(port_str), "%d", port);

    int error;

    if (pm) {
      error = execlp("avahi-publish-service", "avahi-publish-service",
                     installation_subtype, // installation & ia (subtype)
                     serial_no_subtype,    // serial number (subtype)
                     pm_subtype,           // programming mode (subtype)
                     serial_no,            // service name = serial number
                     "_knx._udp",          // service type
                     port_str,             // port
                     sp_text_record,       // TXT record
                     (char *)NULL);
    } else {
      error = execlp("avahi-publish-service", "avahi-publish-service",
                     installation_subtype, // installation & ia (subtype)
                     serial_no_subtype,    // serial number (subtype)
                                           // no programming mode subtype
                     serial_no,            // service name = serial number
                     "_knx._udp",          // service type
                     port_str,             // port
                     sp_text_record,       // TXT record
                     (char *)NULL);
    }

    if (error == -1) {
      OC_ERR("Failed to execute avahi-publish-service: %s", strerror(errno));
      return -1;
    }
  } else if (avahi_pid > 0) {
    // we are in the parent thread - return successfully
    return 0;
  } else {
    // fork failed
    OC_ERR("Failed to fork Avahi advertisement process, error %s", strerror(errno));
    return -1;
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
