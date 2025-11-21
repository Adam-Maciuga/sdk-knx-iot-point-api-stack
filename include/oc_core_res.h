/*
// Copyright (c) 2016 Intel Corporation
// Copyright (c) 2021 Cascoda Ltd
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
/**
	@brief KNX mandatory resources implementation.
	@file
*/
#ifndef OC_CORE_RES_H
#define OC_CORE_RES_H

#include "oc_ri.h"
#include "oc_knx.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
	#endif

	// ... intel aim for compatibility with MSVC, but it does not work for pragmas 
	#if defined _MSC_VER && !defined __INTEL_COMPILER

	#define PRAGMA_IN _Pragma("warning(disable:4090)")
	#define PRAGMA_OUT _Pragma("warning(default:4090)")

	#else

	#define PRAGMA_IN  _Pragma("GCC diagnostic push")  \
                     _Pragma("GCC diagnostic ignored \"-Wdiscarded-array-qualifiers\"")
	#define PRAGMA_OUT _Pragma("GCC diagnostic pop")

	#endif

	/**
	 * @brief version information
	 * e.g. [major, minor, patch]
	 */
	typedef struct oc_knx_version_info_t
	{
		uint16_t major;
    uint16_t minor;
    uint16_t patch;
	} oc_knx_version_info_t;

	/**
	 * @brief Device information
	 *
	 * This structure contains device data.
	 *
	 * @note
	 * - some properties maybe changed at runtime via a PUT/POST service, or as a consequence of a reset (code 2/7)
	 *   or restart, marked with (mod)
	 * - some properties are set to a fixed value at device startup and cannot be changed at all, marked with (fix)
	 
	 */
	typedef struct oc_device_info_t
	{
		oc_string_t serialnumber;     // knx serial number, binary 6 bytes, in hex 12 bytes (fix)
		oc_knx_version_info_t hwv;    // hardware ver, :dpt.version -> U5U5U6 (fix)
		oc_knx_version_info_t fwv;    // firmware ver, :dpt.version -> U5U5U6 (fix)
		oc_knx_version_info_t apv;    // application ver, :dpt.programVersion -> U16U16U8 (vendor id, device type, app. version) (mod)
		oc_string_t hwt;              // knx hardware type, should not be larger than 6 chars (fix)
		oc_string_t iot_model;        // knx model, former mask version (fix), name is specific due to vast amount of "hostname" in other code
    oc_string_t iot_hostname;			// knx host name (mod), see above
		uint32_t mid;                 // knx manufacturer id (fix)
		uint64_t fid;                 // knx fabric id (mod)(mod)
		uint16_t ia;                  // 16-bit knx individual address (mod)
		uint64_t iid;                 // 40-bit knx installation id (mod)
		bool pm;                      // knx programming mode (mod)
		oc_lsm_state_t lsm_s;         // knx lsm states (mod)
    oc_string_t app_friendly_name;// knx application 'friendly' name, currently not able to retrieve from any endpoint (fix)
	} oc_device_info_t;

	/**
	 * @brief Set device serial number, mfg name and some default data, init then device (/dev, ...) resources 
	 *
	 * @param serialnumber the serial number of the device, MUST be in ASCII lower case, MUST be exactly 12 SN chars (+ '\')
	 * @param app_friendly_name the user-friendly name of the application

	 */
  void oc_core_set_device(const char* serialnumber, const char* app_friendly_name);

	/**
	 * @brief set the firmware version
	 *
	 * @param major the xxx number of xxx.yyy.zzz
	 * @param minor the yyy number of xxx.yyy.zzz
	 * @param patch the zzz number of xxx.yyy.zzz
	 *
	 * @note according to the type definition it is a U5U5U6,
	 *       please consider the range (no active range check is implemented)
	 *
	 * @return int error status, 0 = OK
	 */
  int oc_core_set_device_fwv(uint16_t major, uint16_t minor, uint16_t patch);

	/**
	 * @brief sets the hardware version number
	 *
	 * @param major the xxx number of xxx.yyy.zzz
	 * @param minor the yyy number of xxx.yyy.zzz
	 * @param patch the zzz number of xxx.yyy.zzz
	 *
	 * @note according to the type definition it is a U5U5U6,
	 *       please consider the range (no active range check is implemented)
	 *
	 * @return int  error status, 0 = OK
	 */
  int oc_core_set_device_hwv(uint16_t major, uint16_t minor, uint16_t patch);

  /**
	 * @brief sets the application version number
	 *
	 * @param major the xxx number of xxx.yyy.zzz
	 * @param minor the yyy number of xxx.yyy.zzz
	 * @param patch the zzz number of xxx.yyy.zzz
	 *
	 * @note according to the type definition it is a U16U16U8,
	 *       please consider the range (no active range check is implemented)
	 *
	 * @return int  error status, 0 = OK
	 */
  int oc_core_set_device_apv(uint16_t major, uint16_t minor, uint16_t patch);

	/**
	 * @brief sets the manufacturer id
	 *
	 * @param mid the manufacturer id
	 * @return int error status, 0 = OK
	 */
	int oc_core_set_device_mid(uint32_t mid);

	/**
	 * @brief sets and stores the individual address
	 *
	 * @param ia the individual address
	 * @return int error status, 0 = OK
	 */
	int oc_core_set_and_store_device_ia(uint16_t ia);

	/**
	 * @brief sets the hardware type (string)
	 * input string should not be larger than 6
	 *
	 * @param hardware_type the hardware type
	 * @return int error status, 0 = OK
	 */
	int oc_core_set_device_hwt(const char* hardware_type);

	/**
	 * @brief sets the model (string)
	 *
	 * @param model the device model
	 * @return int error status, 0 = OK
	 */
	int oc_core_set_device_model(const char* model);

	/**
	 * @brief sets the host name (string)
	 *
	 * @param host_name the host name
	 * @return int error status, 0 = OK
	 */
	int oc_core_set_device_hostname(const char* host_name);

	/**
	 * @brief sets the installation identifier (iid) and store it
	 *
	 * @param iid the KNX installation id
	 * @return int error status, 0 = OK
	 */
	int oc_core_set_and_store_device_iid(uint64_t iid);

	/**
	 * @brief sets the fabric identifier (fid)
	 *
	 * @param fid the fabric id
	 * @return int error status, 0 = OK
	 */
	int oc_core_set_and_store_device_fid(uint64_t fid);

	/**
	 * @brief gets the installation identifier (iid) (unsigned int)
	 *
	 * @return The KNX installation id
	 */
	uint64_t oc_core_get_device_iid(void);

	/**
   * @brief sets the application version identifier
   *
   * @param major major version
   * @param minor minor version
   * @param patch patch version
   *
   * @note according to the type definition it is a U16U16U8 with vendor id, device type, app. version,
   *       no active range check is yet implemented for the U8 range of patch (app. version)
   *
   * @return int error status, 0 = OK
   */
	int oc_core_set_and_store_device_application_version(uint16_t major, uint16_t minor, uint16_t patch);

	/**
	 * @brief retrieve the device info for device 0
	 *
	 * @note the response can never be a NULL pointer, device 0 is a global static definition
	 *       (note that some device properties as such may be '0' (int) or NULL (strings), e.g; a not initialized serial number) 
	 *
	 * @return oc_device_info_t* the device info
	 */
	oc_device_info_t* oc_core_get_device_info(void);

	/**
	 * @brief retrieve the resource by type (e.g. index) on a specific device
	 *
	 * @note  Accessing for device 0 a specific resource needs to travers
	 *        the list of predefined core resource pointers.
	 *
	 * @param index the index of the resource
	 * @return oc_resource_t* the resource handle
	 */
	const oc_resource_t* oc_core_get_core_resource_by_index(int index);

	/**
	 * @brief Ensure that the given URI starts with a forward slash '/'.
	 *
	 * @param uri the URI to check
	 */
	void oc_check_uri(const char* uri);

	/**
	 * @brief checks for the presence of 'urn:knx' in ANY of the request query parameter value's
	 *
	 * @param request the request to scan
	 * @return true if present, false otherwise
	 */
	bool oc_check_request_query_value_on_urn_knx(oc_request_t* request);

	/**
   * @brief filter if the query parameter key 'rt' is part of the request
   *        and if the value is contained in a resource (including wildcards)
   *
   * @param resource the resource to look for
   * @param request the request to scan
   *
   * @return true
   * - key 'rt' is present and resource interface type DO match the value from the request
   * - key 'rt' is present and the value from the request is a '*' wildcard
   *   (at least one resource type must be assigned that matches the wildcard)
   * - key 'rt' is NOT present
   *
   * @return false
   * - key 'if' is present and resource interface type DO NOT match the value from the request
   *
   * @note according to RFC 6690 a value may contain more than one string to search for (separated by spaces) 
   *
   */
  bool oc_check_resource_by_rt(const oc_resource_t* resource, oc_request_t* request);

	/**
	 * @brief filter if the query parameter key 'if' is part of the request
   *        and if the value is contained in a resource (including wildcards)
	 *
	 * @param resource the resource to look for
	 * @param request the request to scan
	 *
	 * @return true
	 * - key 'if' is present and resource interface type DO match the value from the request 
	 * - key 'if' is present and the value from the request is a '*' wildcard (at least one type must be assigned to a resource)
	 * - key 'if' is NOT present
	 * @return false
	 * - key 'if' is present and resource interface type DO NOT match the value from the request
	 *
	 * @note according to RFC 6690 a value may contain more than one string to search for (separated by spaces) 
	 * 
	 */
  bool oc_check_resource_by_if(const oc_resource_t* resource, oc_request_t* request);

	#ifdef __cplusplus
}
#endif

#endif 
