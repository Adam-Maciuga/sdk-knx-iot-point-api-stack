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

/**
 * @file oc_network_interface.h
 * @brief Network interface enumeration and selection API
 */

#ifndef OC_NETWORK_INTERFACE_H
#define OC_NETWORK_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum length for interface name
 */
#define OC_NETWORK_IF_NAME_MAX 256

/**
 * @brief Network interface information
 */
typedef struct oc_network_interface_info_t
{
  char name[OC_NETWORK_IF_NAME_MAX];  /**< Interface friendly name */
  uint32_t if_index;                   /**< Interface index */
  bool is_up;                          /**< Interface is operational */
  bool has_ipv6;                       /**< Has IPv6 address */
  bool has_ipv4;                       /**< Has IPv4 address */
} oc_network_interface_info_t;

/**
 * @brief Enumerate available network interfaces
 *
 * @param interfaces Array to store interface information
 * @param max_interfaces Maximum number of interfaces to return
 * @return Number of interfaces found (0 if error or none found)
 */
int oc_network_enumerate_interfaces(oc_network_interface_info_t *interfaces, int max_interfaces);

/**
 * @brief Set the preferred network interface by index
 *
 * @param if_index Interface index to use (0 = use all interfaces)
 * @return true on success, false on error
 */
bool oc_network_set_interface_filter(uint32_t if_index);

/**
 * @brief Get the currently configured interface filter
 *
 * @return Interface index (0 = all interfaces)
 */
uint32_t oc_network_get_interface_filter(void);

/**
 * @brief Refresh network endpoints after interface filter change
 *
 * Rebuilds the endpoint list based on the current interface filter.
 * Should be called after oc_network_set_interface_filter() to apply changes.
 *
 * @return 0 on success, -1 on error
 */
int oc_network_refresh_endpoints(void);

#ifdef __cplusplus
}
#endif

#endif /* OC_NETWORK_INTERFACE_H */
