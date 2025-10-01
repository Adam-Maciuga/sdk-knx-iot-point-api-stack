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

#include "oc_core_res.h"
#include "oc_api.h"
#include "messaging/coap/oc_coap.h"
#include "oc_discovery.h"
#include "oc_rep.h"
#include "oc_knx.h"
#include "oc_knx_dev.h"
#include "oc_knx_fb.h"
#include "oc_knx_fp.h"
#include "oc_knx_p.h"
#include "oc_knx_swu.h"
#include "oc_knx_sec.h"
#include "oc_knx_sub.h"
#include "port/oc_assert.h"
#include <stdarg.h>
#include "port/oc_storage.h"


static bool core_resources_initialized = false;
static oc_device_info_t oc_device_info;
static oc_platform_info_t oc_platform_info; // platform provider

void oc_core_init(void)
{
	// Core resources are already statically defined as const structures
	// Only call shutdown to clean up any previous state
	if (core_resources_initialized) {
		oc_core_shutdown();
	}
	core_resources_initialized = true;
}

static void oc_core_free_device_info_properties(oc_device_info_t* oc_device_info_item)
{
	if (oc_device_info_item)
	{
		// KNX
    oc_free_string(&oc_device_info_item->serialnumber);
		oc_free_string(&oc_device_info_item->hwt);
		oc_free_string(&oc_device_info_item->model);
		oc_free_string(&oc_device_info_item->hostname);
	}
}

void oc_core_shutdown(void)
{
	size_t i;
	oc_free_string(&oc_platform_info.mfg_name);
	
	// Clean up device info properties
	oc_core_free_device_info_properties(&oc_device_info);
	
	// Free KNX table resources first
	oc_free_knx_table_resources();
	core_resources_initialized = false;
}

int oc_core_set_device_fwv(int major, int minor, int patch)
{
	oc_device_info.fwv.major = major;
	oc_device_info.fwv.minor = minor;
	oc_device_info.fwv.patch = patch;
	return 0;
}

int oc_core_set_device_hwv(int major, int minor, int patch)
{
	oc_device_info.hwv.major = major;
	oc_device_info.hwv.minor = minor;
	oc_device_info.hwv.patch = patch;
	return 0;
}

int oc_core_set_device_apv(int major, int minor, int patch)
{
	oc_device_info.ap.major = major;
	oc_device_info.ap.minor = minor;
	oc_device_info.ap.patch = patch;
	return 0;
}

int oc_core_set_device_mid(uint32_t mid)
{
	oc_device_info.mid = mid;
	return 0;
}

int oc_core_set_and_store_device_ia(uint16_t ia)
{
  oc_device_info.ia = ia;
  oc_storage_write(KNX_STORAGE_IA, (uint8_t*)&ia, sizeof(ia));

  return 0;
}

int oc_core_set_device_hwt(const char* hardware_type)
{
	oc_free_string(&oc_device_info.hwt);
  oc_new_string(&oc_device_info.hwt, hardware_type, strlen(hardware_type));

	return 0;
}

int oc_core_set_device_model(const char* model)
{
	oc_free_string(&oc_device_info.model);
	oc_new_string(&oc_device_info.model, model, strlen(model));

	return 0;
}

int oc_core_set_device_hostname(const char* host_name)
{
	oc_free_string(&oc_device_info.hostname);
	oc_new_string(&oc_device_info.hostname, host_name, strlen(host_name));

	return 0;
}

uint64_t oc_core_get_device_iid()
{
	return oc_device_info.iid;
}

int oc_core_set_and_store_device_iid(uint64_t iid)
{
  oc_device_info.iid = iid;
  oc_storage_write(KNX_STORAGE_IID, (uint8_t*)&iid, sizeof(iid));

  return 0;
}

int oc_core_set_and_store_device_application_version(int major, int minor, int patch)
{
	oc_device_info.ap.major = major;
  oc_device_info.ap.minor = minor;
  oc_device_info.ap.patch = patch;

	oc_storage_write(KNX_STORAGE_AP_MAJOR, (uint8_t*)&major, sizeof(major));
  oc_storage_write(KNX_STORAGE_AP_MINOR, (uint8_t*)&minor, sizeof(minor));
  oc_storage_write(KNX_STORAGE_AP_PATCH, (uint8_t*)&patch, sizeof(patch));

  return 0;
}

int oc_core_set_and_store_device_fid(uint64_t fid)
{
	oc_device_info.fid = fid;
  oc_storage_write(KNX_STORAGE_FID, (uint8_t*)&fid, sizeof(fid));

	return 0;
}

oc_device_info_t* oc_core_set_device(char* name, char* version, char* base, char* serialnumber, oc_core_set_device_cb_t set_device_cb, void* data)
{
	(void) data;

	memset(&oc_device_info, 0, sizeof(oc_device_info_t));
	oc_device_info.ia = 0xffff;

	// ensure that the serial number is in lower case
	// it changes the original, but it must be anyhow lower case...
	oc_charstream_convert_to_lower(serialnumber);

	oc_new_string(&oc_device_info.serialnumber, serialnumber, strlen(serialnumber));
	oc_device_info.set_device_cb = set_device_cb;

	oc_create_knx_fp_resources();
	oc_create_knx_sec_resources();
	oc_create_knx_swu_resources();

	oc_device_info.data = data;

	if (oc_connectivity_init() < 0)
	{
		oc_abort("error initializing connectivity for device");
	}
	
	oc_init_oscore_from_storage(true);

	return &oc_device_info;
}

oc_platform_info_t* oc_core_init_platform(const char* mfg_name, oc_core_init_platform_cb_t init_cb, void* data)
{
	if (oc_platform_info.mfg_name.size > 0)
	{ // already initialized, size derived from base type 'oc_string_t'
		return &oc_platform_info;
	}

	oc_new_string(&oc_platform_info.mfg_name, mfg_name, strlen(mfg_name));
	oc_platform_info.init_platform_cb = init_cb;
	oc_platform_info.data = data;

	return &oc_platform_info;
}

void oc_check_uri(const char* uri)
{
  // break if uri does not start with '/' 
  oc_assert(uri[0] == '/');
}


oc_device_info_t* oc_core_get_device_info(void)
{
	return &oc_device_info;
}

oc_platform_info_t* oc_core_get_platform_info(void)
{
	return &oc_platform_info;
}


oc_resource_t* oc_core_get_resource_by_index(int index)
{
	// Ensure resources are initialized
	if (!core_resources_initialized) {
		oc_core_init();
	}

	// Traverse const linked list
	extern const oc_resource_t core_resource_dev_sn; // Start of the chain
	const oc_resource_t* res = &core_resource_dev_sn;
	int current_index = 0;

	// Walk the linked list to find the resource at the specified index
	while (res && current_index < index) {
		res = res->next;
		current_index++;
	}

	// Return the resource if found and valid (has a URI)
	if (res && res->uri.size > 0) {
		// Note: We're casting away const here because the interface expects non-const
		// The const resources should not be modified through this pointer
		return (oc_resource_t*)res;
	}

	return NULL;
}

bool oc_check_request_query_value_on_urn_knx(oc_request_t* request)
{
	char* value = NULL;
	char* key;

	size_t value_len;
	size_t key_len;

	oc_init_query_iterator();
	while (oc_iterate_query(request, &key, &key_len, &value, &value_len) > -1)
	{
		if (strncmp(value, "urn:knx", 7) == 0)
		{ // urn:knx is present ...
			return true;
		}
	}
	return false;
}

bool oc_check_resource_by_rt(const oc_resource_t* resource, oc_request_t* request)
{
	// pre-assumption that 'rt' key is not part of request
  bool match = true, more_query_params; 
	char* request_rt_ptr = NULL;
	int request_rt_ptr_len = -1;

  oc_init_query_iterator();
	do
	{
		more_query_params =	oc_iterate_query_get_values(request, "rt", &request_rt_ptr, &request_rt_ptr_len);

		// a value must be present  
		if (request_rt_ptr_len > 0)
		{
		  // key 'rt' is part of query, check on wildcard rt=* or rt=urn:knx:dpa.201.* 
			const char* wildcard = memchr(request_rt_ptr, '*', request_rt_ptr_len);

		  if (wildcard)
			{
				// cut value string len to compare with, such as for
				// - rt=urn:knx:dpa.201.* from size 17 -> size 16
        // - rt=* from size 1 -> size 0
		    request_rt_ptr_len = (int) (wildcard - request_rt_ptr);
			}

			// default assumption key 'if' is part of query, but value will be not found 
			match = false;

			// 0...n rt types (acc. specification it can be more than one assigned)
			for (int i = 0; i < (int)oc_string_array_get_allocated_size(resource->types); i++)
			{
				// get the 'rt' string and len, contains the full specified name such as urn:knx:dpa.201.50
			  const int resource_type_len =	oc_string_array_get_item_size(resource->types, i);
				const char* resource_type_ptr =	oc_string_array_get_item(resource->types, i);

			  PRINT("rt type '%s'", resource_type_ptr);

			  if (wildcard)
				{
					// on wildcard scan number of chars up to position of wildcard only
			    if (strncmp(request_rt_ptr, resource_type_ptr, request_rt_ptr_len) == 0)
					{
						// value from request is contained in resource type
						// - urn:knx:dpa.201.* matches, urn:knx:dpa.201.51/52/xxx scanning 16 chars
            // - * matches all, scanning 0 chars 
			      return true;
					}
				}
        else
        {
          // on NO wildcard scan number of all chars, take care that request string value
          // has same len as resource len, otherwise you compare 'urn:knx:dpa' (11)
          // with 'urn:knx:dpa.201.50' (18) that always matches
          if (strncmp(request_rt_ptr, resource_type_ptr, request_rt_ptr_len) == 0 && request_rt_ptr_len == resource_type_len)
          {
            // value from request matches to resource type and len also matches
            return true;
          }
        }
			}
		}
	}
	while (more_query_params);
	return match;
}

bool oc_check_resource_by_if(const oc_resource_t* resource, oc_request_t* request)
{
  // pre-assumption that 'if' key is not part of request
  bool match = true, more_query_params;
	char* request_if_ptr = NULL;
	int request_if_ptr_len = -1;

	oc_init_query_iterator();
	do
	{
		more_query_params = oc_iterate_query_get_values(request, "if", &request_if_ptr, &request_if_ptr_len);

		// a value must be present  
		if (request_if_ptr_len > 0)
		{
		  // key 'if' is part of query, check on wildcard if=* or if=urn:knx:if.s*
			const char* wildcard = memchr(request_if_ptr, '*', request_if_ptr_len);

		  if (wildcard)
			{
        // cut value string len to compare with, such as for
        // - if=urn:knx:if.s* from size 13 -> size 12
        // - if=* from size 1 -> size 0
        request_if_ptr_len = (int)(wildcard - request_if_ptr);
			}

			// default assumption key 'if' is part of query, but value will be not found 
			match = false;

			 // get if's from resource
      oc_interface_mask_t interfaces = OC_IF_NONE;

			if (oc_resource_get_all_interfaces_for_a_resource(resource, &interfaces))
			{
				// 0...n if types (acc. specification it can be more than one assigned per resource method) 
				for (int i = 0; i <= MAX_INTERFACE_BIT; i++, interfaces >>= 1)
        {
					// only if interface is set on a resource check on a match with request, e.g;
					// request = urn:knx:if.o, resource = urn:knx:if.p and urn:knx:if.o -> one mach at i = 2
          // 32-bit if.p + if.i = 0b00000000 00000000 00010000 00100100
					if (interfaces & 1)
					{
					  // get the 'if' string and len, contains the full specified URN such as 'urn:knx:if.ll'
            const char* resource_if_ptr = get_interface_string_full_urn(i);
            const int resource_if_ptr_len = (int)strlen(resource_if_ptr);

            PRINT("if type '%s'", resource_if_ptr);

            if (wildcard)
            {
              // on wildcard scan number of chars up to position of wildcard only
              if (strncmp(request_if_ptr, resource_if_ptr, request_if_ptr_len) == 0)
              {
                // value from request is contained in resource type
                // - urn:knx:if.s* matches urn:knx:if.s/sec, scanning 12 chars
                // - * matches all, scanning 0 chars
                return true;
              }
            }
            else
            {
              // on NO wildcard scan number of all chars, take care that request string value
              // has same len as resource len, otherwise you compare 'urn:knx:if' (10)
              // with 'urn:knx:if.ll' (13) that always matches
              if (strncmp(request_if_ptr, resource_if_ptr, request_if_ptr_len) == 0 && request_if_ptr_len == resource_if_ptr_len)
              {
                // value from request matches to resource type and len also matches
                return true;
              }
            }
					}
        }
			}
		}
	}
	while (more_query_params);
	return match;
}
