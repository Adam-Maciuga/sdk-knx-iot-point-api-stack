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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <string.h>

#include "port/oc_network_interface.h"
#include "oc_log.h"

static uint32_t g_interface_filter = 0; // 0 = all interfaces

int 
oc_network_enumerate_interfaces(oc_network_interface_info_t *interfaces, int max_interfaces)
{
  if (!interfaces || max_interfaces <= 0) {
    return 0;
  }

  ULONG family = AF_INET6;
  IP_ADAPTER_ADDRESSES *adapter_list = NULL;
  IP_ADAPTER_ADDRESSES *adapter = NULL;
  ULONG out_buf_len = 15000;
  int count = 0;

#ifdef OC_IPV4
  family = AF_UNSPEC;
#endif

  // Allocate buffer for adapter addresses
  adapter_list = (IP_ADAPTER_ADDRESSES *)malloc(out_buf_len);
  if (!adapter_list) {
    OC_ERR("Failed to allocate memory for adapter list");
    return 0;
  }

  DWORD ret = GetAdaptersAddresses(
    family,
    GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_SKIP_ANYCAST | 
    GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
    NULL,
    adapter_list,
    &out_buf_len
  );

  if (ret == ERROR_BUFFER_OVERFLOW) {
    free(adapter_list);
    adapter_list = (IP_ADAPTER_ADDRESSES *)malloc(out_buf_len);
    if (!adapter_list) {
      OC_ERR("Failed to allocate memory for adapter list");
      return 0;
    }
    ret = GetAdaptersAddresses(
      family,
      GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_SKIP_ANYCAST | 
      GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
      NULL,
      adapter_list,
      &out_buf_len
    );
  }

  if (ret != NO_ERROR) {
    OC_ERR("GetAdaptersAddresses failed with error: %d", ret);
    free(adapter_list);
    return 0;
  }

  // Iterate through adapters
  for (adapter = adapter_list; adapter && count < max_interfaces; adapter = adapter->Next) {
    // Skip loopback
    if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
      continue;
    }

    oc_network_interface_info_t *info = &interfaces[count];
    memset(info, 0, sizeof(oc_network_interface_info_t));

    // Copy friendly name (convert from wide char to narrow char)
    if (adapter->FriendlyName) {
      WideCharToMultiByte(CP_UTF8, 0, adapter->FriendlyName, -1, 
                          info->name, OC_NETWORK_IF_NAME_MAX - 1, NULL, NULL);
    } else if (adapter->Description) {
      WideCharToMultiByte(CP_UTF8, 0, adapter->Description, -1, 
                          info->name, OC_NETWORK_IF_NAME_MAX - 1, NULL, NULL);
    } else {
      snprintf(info->name, OC_NETWORK_IF_NAME_MAX, "Interface %lu", adapter->IfIndex);
    }

    info->if_index = adapter->IfIndex;
    info->is_up = (adapter->OperStatus == IfOperStatusUp);

    // Check for IPv6/IPv4 addresses
    IP_ADAPTER_UNICAST_ADDRESS *addr;
    for (addr = adapter->FirstUnicastAddress; addr; addr = addr->Next) {
      if (addr->Address.lpSockaddr->sa_family == AF_INET6) {
        info->has_ipv6 = true;
      }
#ifdef OC_IPV4
      else if (addr->Address.lpSockaddr->sa_family == AF_INET) {
        info->has_ipv4 = true;
      }
#endif
    }

    count++;
  }

  free(adapter_list);
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
