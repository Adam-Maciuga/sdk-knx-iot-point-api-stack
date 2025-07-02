/*
// Copyright (c) 2016 Intel Corporation
// Copyright (c) 2021 Cascoda Ltd
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
	 * @brief callback for initializing the platform
	 *
	 */
	typedef void (*oc_core_init_platform_cb_t)(void* data);

	/**
	 * @brief callback for adding a device
	 *
	 */
	typedef void (*oc_core_add_device_cb_t)(void* data);

	/**
	 * @brief platform information
	 *
	 */
	typedef struct oc_platform_info_t
	{
		oc_string_t mfg_name;                         /**< manufacturer name */
		oc_core_init_platform_cb_t init_platform_cb;  /**< callback function */
		void* data;                                   /**< user data for the callback function */
	} oc_platform_info_t;

	/**
	 * @brief version information
	 * e.g. [major, minor, patch]
	 */
	typedef struct oc_knx_version_info_t
	{
		int major; /**< major version number */
		int minor; /**< minor version number */
		int patch; /**< patch version number */
	} oc_knx_version_info_t;

	/**
	 * @brief device information
	 *
	 * This structure contains
	 */
	typedef struct oc_device_info_t
	{
		oc_string_t serialnumber;                 /**< knx serial number */
		oc_knx_version_info_t hwv;                // knx hardware version as U5U5U6 TODO 3 x 16 bit and check 0... xxx
		oc_knx_version_info_t fwv;                // fwv firmware version as U5U5U6 TODO 3 x 16 bit and check 0... xxx
		oc_knx_version_info_t ap;                 // fwv application version as U16U16U8  TODO 3 x 16 bit and check 0... xxx
		oc_string_t hwt;                          /**< knx hardware type, should not be larger than 6 chars */
		oc_string_t model;                        /**< knx model */
		oc_string_t hostname;                     /**< knx host name */
		uint32_t mid;                             /**< knx manufacturer id */
		uint64_t fid;                             /**< knx fabric id */
		uint16_t ia;                              /**< 16-bit knx individual address */
		uint64_t iid;                             /**< 40-bit knx installation id (checked in 'runtime test' */
		bool pm;                                  /**< knx programming mode */
		oc_lsm_state_t lsm_s;                     /**< knx lsm states */
		oc_core_add_device_cb_t add_device_cb;    /**< callback when device is changed */
		void* data;                               /**< user data */
	} oc_device_info_t;

	/**
	 * @brief initialize the core functionality
	 *
	 */
	void oc_core_init(void);

	/**
	 * @brief shutdown the core functionality
	 *
	 */
	void oc_core_shutdown(void);

	/**
	 * @brief initialize the platform
	 *
	 * @param mfg_name the manufacturer name
	 * @param init_cb the callback
	 * @param data  the user data
	 * @return oc_platform_info_t* the platform information
	 */
	oc_platform_info_t* oc_core_init_platform(const char* mfg_name,
																						oc_core_init_platform_cb_t init_cb,
																						void* data);

	/**
	 * @brief Add device to the platform
	 *
	 * @param name the name of the device
	 * @param version the version of the KNX spec
	 * @param base the base url
	 * @param serialnumber the serial number of the device
	 * @param add_device_cb device callback
	 * @param data the supplied user data
	 * @return oc_device_info_t* the device structure
	 */
	oc_device_info_t* oc_core_add_device(char* name, char* version, char* base,
																			 char* serialnumber,
																			 oc_core_add_device_cb_t add_device_cb,
																			 void* data);

	/**
	 * @brief set the firmware version
	 *
	 * @param device_index the device index
	 * @param major the xxx number of xxx.yyy.zzz
	 * @param minor the yyy number of xxx.yyy.zz
	 * @param patch the zzz number of xxx.yyy.zzz
	 * @return int error status, 0 = OK
	 */
	int oc_core_set_device_fwv(size_t device_index, int major, int minor, int patch);

	/**
	 * @brief sets the hardware version number
	 *
	 * @param device_index the device index
	 * @param major the xxx number of xxx.yyy.zzz
	 * @param minor the yyy number of xxx.yyy.zz
	 * @param patch the zzz number of xxx.yyy.zzz
	 * @return int  error status, 0 = OK
	 */
	int oc_core_set_device_hwv(size_t device_index, int major, int minor, int patch);
	/**
	 * @brief sets the application version number
	 *
	 * @param device_index the device index
	 * @param major the xxx number of xxx.yyy.zzz
	 * @param minor the yyy number of xxx.yyy.zz
	 * @param patch the zzz number of xxx.yyy.zzz
	 * @return int  error status, 0 = OK
	 */
	int oc_core_set_device_apv(size_t device_index, int major, int minor, int patch);

	/**
	 * @brief sets the manufacturer id
	 *
	 * @param device_index the device index
	 * @param mid the manufacturer id
	 * @return int error status, 0 = OK
	 */
	int oc_core_set_device_mid(size_t device_index, uint32_t mid);

	/**
	 * @brief sets and stores the individual address
	 *
	 * @param device_index the device index
	 * @param ia the individual address
	 * @return int error status, 0 = OK
	 */
	int oc_core_set_and_store_device_ia(size_t device_index, uint16_t ia);

	/**
	 * @brief sets the hardware type (string)
	 * input string should not be larger than 6
	 *
	 * @param device_index the device index
	 * @param hardware_type the hardware type
	 * @return int error status, 0 = OK
	 */
	int oc_core_set_device_hwt(size_t device_index, const char* hardware_type);

	/**
	 * @brief sets the model (string)
	 *
	 * @param device_index the device index
	 * @param model the device model
	 * @return int error status, 0 = OK
	 */
	int oc_core_set_device_model(size_t device_index, const char* model);

	/**
	 * @brief sets the host name (string)
	 *
	 * @param device_index the device index
	 * @param host_name the host name
	 * @return int error status, 0 = OK
	 */
	int oc_core_set_device_hostname(size_t device_index, const char* host_name);

	/**
	 * @brief sets the installation identifier (iid) and store it
	 *
	 * @param device_index the device index
	 * @param iid the KNX installation id
	 * @return int error status, 0 = OK
	 */
	int oc_core_set_and_store_device_iid(size_t device_index, uint64_t iid);

	/**
	 * @brief sets the fabric identifier (fid)
	 *
	 * @param device_index the device index
	 * @param fid the fabric id
	 * @return int error status, 0 = OK
	 */
	int oc_core_set_and_store_device_fid(size_t device_index, uint64_t fid);

	/**
	 * @brief sets the installation identifier (iid) (unsigned int)
	 *
	 * @param device_index the device index
	 * @return The KNX installation id
	 */
	uint64_t oc_core_get_device_iid(size_t device_index);

	/**
   * @brief sets the application version identifier
   *
   * @param device_index the device index
   * @param major major version
   * @param minor minor version
   * @param patch patch version 
   * @return The KNX installation id
   */
	int oc_core_set_and_store_device_application_version(size_t device_index, int major, int minor, int patch);

	/**
	 * @brief retrieve the amount of devices
	 *
	 * @return size_t the amount of devices
	 */
	size_t oc_core_get_num_devices(void);

	/**
	 * @brief retrieve the device info from the device index
	 *
	 * @param device the device index
	 * @return oc_device_info_t* the device info
	 */
	oc_device_info_t* oc_core_get_device_info(size_t device);

	/**
	 * @brief retrieve the platform information
	 *
	 * @return oc_platform_info_t* the platform information
	 */
	oc_platform_info_t* oc_core_get_platform_info(void);

	/**
	 * @brief retrieve the resource by type (e.g. index) on a specific device
	 *
	 * @note  Accessing for device 0 a specific resource needs to travers
	 *        the list of predefined core resource pointers.
	 *
	 * @param index the index of the resource
	 * @return oc_resource_t* the resource handle
	 */
	oc_resource_t* oc_core_get_resource_by_index(int index);

	/**
	 * @brief Ensure that the given URI starts with a forward slash '/'.
	 *
	 * @param uri the URI to check
	 */
	void oc_check_uri(const char* uri);

	/**
	 * @brief populate core resource
	 * mainly used for creation of core resources
	 *
	 * @param core_resource_index the resource index
	 * @param device_index the device index
	 * @param uri the URI for the resource
	 * @param content_type0 the (first) content type that will be listed as ct in link-format responses (mandatory)
	 * @param content_type1 the (second) content type that will be listed as ct in link-format responses (optional)
	 * @param properties the properties (as mask)
	 * @param get_cb get callback function
	 * @param put_cb put callback function
	 * @param post_cb post callback function
	 * @param delete_cb delete callback function
	 * @param num_resource_types amount of resource types, listed as variable
	 * arguments after this argument
	 * @param ... Resource types, passed as zero-terminated strings. In order
	 * to save memory, the maximum length of each resource type is 32 bytes.
	 */
	void oc_core_populate_resource(int core_resource_index,
																 size_t device_index,
																 char* uri,
																 oc_content_format_t content_type0,
																 oc_content_format_t content_type1,
																 int properties,
																 oc_request_callback_t get_cb,
																 oc_request_callback_t put_cb,
																 oc_request_callback_t post_cb,
																 oc_request_callback_t delete_cb,
																 int num_resource_types,
																 ...);

	/**
	 * @brief bind a dpt to a (already created) core resource
	 *
	 * @param core_resource_index the resource index
	 * @param device_index the device index
	 * @param dpt the DPT value of the resource
	 */
	void oc_core_bind_dpt_resource(int core_resource_index, size_t device_index, const char* dpt);

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
   * - key 'rt' is present and the value from the request is a '*' wildcard (at least one type must be assigned to a resource)
   * - key 'rt' is NOT present
   * @return false
   * - key 'if' is present and resource interface type DO NOT match the value from the request
   *
   * @note according to RFC 6690 a value may contain more than one string to search for (separated by spaces) 
   *
   */
  bool oc_check_resource_by_rt(oc_resource_t* resource, oc_request_t* request);

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
  bool oc_check_resource_by_if(oc_resource_t* resource, oc_request_t* request);

	/**
	 * @brief frame the interface mask in the response, as string in the uri
	 * example: full tag if= ":if.i" this function frames ":if.i" (truncated)
	 * or "urn:knx:if.i"
	 *
	 * @param iface_mask The interface masks to frame
	 * @param truncated 1 = do not frame "urn:knx" in the payload
	 * @return int 0 = success
	 */
	int oc_frame_interfaces_mask_in_response(oc_interface_mask_t iface_mask, bool truncated);

	#ifdef __cplusplus
}
#endif

#endif 
