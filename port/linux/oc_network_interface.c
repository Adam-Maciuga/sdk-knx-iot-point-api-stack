/*
// Copyright (c) 2025 KNX Association
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

#include <sys/types.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <string.h>
#include <stdio.h>

#include "port/oc_network_interface.h"
#include "oc_log.h"

static uint32_t g_interface_filter = 0; // 0 = all interfaces

int 
oc_network_enumerate_interfaces(oc_network_interface_info_t *interfaces, int max_interfaces)
{
  if (!interfaces || max_interfaces <= 0) {
    return 0;
  }

  struct ifaddrs *ifaddr_list = NULL;
  struct ifaddrs *ifa = NULL;
  int count = 0;

  if (getifaddrs(&ifaddr_list) < 0) {
    OC_ERR("getifaddrs failed");
    return 0;
  }

  // Iterate through interfaces
  for (ifa = ifaddr_list; ifa && count < max_interfaces; ifa = ifa->ifa_next) {
    if (!ifa->ifa_name || !ifa->ifa_addr) {
      continue;
    }

    // Skip loopback
    if (ifa->ifa_flags & IFF_LOOPBACK) {
      continue;
    }

    // Skip if not up
    if (!(ifa->ifa_flags & IFF_UP)) {
      continue;
    }

    // Check if we already have this interface
    unsigned int if_index = if_nametoindex(ifa->ifa_name);
    bool found = false;
    for (int i = 0; i < count; i++) {
      if (interfaces[i].if_index == if_index) {
        found = true;
        // Update address family flags
        if (ifa->ifa_addr->sa_family == AF_INET6) {
          interfaces[i].has_ipv6 = true;
        }
#ifdef OC_IPV4
        else if (ifa->ifa_addr->sa_family == AF_INET) {
          interfaces[i].has_ipv4 = true;
        }
#endif
        break;
      }
    }

    if (found) {
      continue;
    }

    // Add new interface
    oc_network_interface_info_t *info = &interfaces[count];
    memset(info, 0, sizeof(oc_network_interface_info_t));

    strncpy(info->name, ifa->ifa_name, OC_NETWORK_IF_NAME_MAX - 1);
    info->if_index = if_index;
    info->is_up = (ifa->ifa_flags & IFF_UP);

    if (ifa->ifa_addr->sa_family == AF_INET6) {
      info->has_ipv6 = true;
    }
#ifdef OC_IPV4
    else if (ifa->ifa_addr->sa_family == AF_INET) {
      info->has_ipv4 = true;
    }
#endif

    count++;
  }

  freeifaddrs(ifaddr_list);
  return count;
}

bool 
oc_network_set_interface_filter(uint32_t if_index)
{
  g_interface_filter = if_index;
  OC_INF("Network interface filter set to: %u (0=all)", if_index);
  return true;
}

uint32_t 
oc_network_get_interface_filter(void)
{
  return g_interface_filter;
}
