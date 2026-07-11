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

#include <stdarg.h>
#include <stdlib.h>
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
#include "port/oc_storage.h"

static oc_device_info_t oc_device_info;	// common device 0 data pointer - cannot be NULL

int oc_core_set_and_store_device_fwv(oc_knx_version_info_t* version)
{
	oc_device_info.fwv = *version;
  oc_storage_write(KNX_STORAGE_FW_VER, (uint8_t*)version, sizeof(oc_knx_version_info_t));
	
	return 0;
}

int oc_core_set_device_hwv(const oc_knx_version_info_t* version)
{
	oc_device_info.hwv = *version;
	return 0;
}

int oc_core_set_device_mid(uint32_t mid)
{
	oc_device_info.mid = mid;
	return 0;
}

bool oc_core_set_and_store_device_ia(int64_t ia)
{
  // max IA number as 16 bit, 0 = in KNX not allowed but here accepted (specification does not limit it)
  #define MAX_IA (0xFFFF)

  if (ia >= 0 && ia <= MAX_IA)
  {
    oc_device_info.ia = (uint16_t)ia;
    oc_storage_write(KNX_STORAGE_IA, (uint8_t*)&oc_device_info.ia, sizeof(oc_device_info.ia));

    return true;
  }
  return false;
}

int oc_core_set_device_hwt(const char* hardware_type)
{
	oc_free_string(&oc_device_info.hwt);
  oc_new_string(&oc_device_info.hwt, hardware_type, strlen(hardware_type));

	return 0;
}

int oc_core_set_device_model(const char* model)
{
	oc_free_string(&oc_device_info.iot_model);
  oc_new_string(&oc_device_info.iot_model, model, strlen(model));

	return 0;
}

int oc_core_set_device_hostname(const char* host_name)
{
	oc_free_string(&oc_device_info.iot_hostname);
  oc_new_string(&oc_device_info.iot_hostname, host_name, strlen(host_name));

	return 0;
}

int oc_core_read_and_set_device_hostname(void)
{

  const oc_device_info_t* const device = oc_core_get_device_info();

	// to have a fixed '\0' at the end of the (128 byte) buffer
	#define MAX_HNAME_BUFFER_SIZE 129
  
  // set default hostname as 'knx-' + serial number (12 x char + /0)  = 17, such as "knx-00fa10020700"
  char hname[MAX_HNAME_BUFFER_SIZE] = ""; 
  (void)snprintf(hname, HNAME_SIZE, HNAME_TYPE, oc_string(device->serialnumber));

  // read host name from storage (on error = the default host name from above is used, otherwise stored host name)
  oc_storage_read(KNX_STORAGE_HOSTNAME, (uint8_t*)&hname, MAX_HNAME_BUFFER_SIZE  - 1);
  oc_core_set_device_hostname(hname);

  return 0;
}

uint64_t oc_core_get_device_iid(void)
{
	return oc_device_info.iid;
}

bool oc_core_set_and_store_device_iid(int64_t iid)
{
  // max IID number as 40 bit, 0 = allowed (specification does not limit it)
  #define MAX_IID (0xFFFFFFFFFF)
  
  if (iid >= 0 && iid <= MAX_IID)
  {
    oc_device_info.iid = iid;
    oc_storage_write(KNX_STORAGE_IID, (uint8_t*)&iid, sizeof(iid));
    return true;
  }
  return false;
}

int oc_core_set_and_store_device_application_version(oc_knx_version_info_t* version)
{
	oc_device_info.apv = *version;
  oc_storage_write(KNX_STORAGE_AP_VER, (uint8_t*)version, sizeof(oc_knx_version_info_t));
  return 0;
}

bool oc_core_set_and_store_device_fid(int64_t fid)
{
  // max FID number as 40 bit, 0 = allowed (specification does not limit it)
  #define MAX_FID (0xFFFFFFFFFF)

  if (fid >= 0 && fid <= MAX_FID)
  {
    oc_device_info.fid = fid;
    oc_storage_write(KNX_STORAGE_FID, (uint8_t*)&fid, sizeof(fid));

    return true;
  }
  return false;
}

void oc_core_set_device(const char* serialnumber, const char* app_friendly_name)
{

	// release ALL strings (e.g. after a device restart/ reset) -> device content is wiped below
  oc_free_string(&oc_device_info.serialnumber);
  oc_free_string(&oc_device_info.hwt);
  oc_free_string(&oc_device_info.iot_model);
  oc_free_string(&oc_device_info.iot_hostname);
  oc_free_string(&oc_device_info.app_friendly_name);

	// clear old device context 
  memset(&oc_device_info, 0, sizeof(oc_device_info_t));

	// caller MUST ensure that the hand-over serial number is in ASCII lower case and 12 chars long 
  oc_new_string(&oc_device_info.serialnumber, serialnumber, SERIAL_NUM_SIZE);

	// device application  friendly name
	oc_new_string(&oc_device_info.app_friendly_name, app_friendly_name, strlen(app_friendly_name));
	
	// init tables
	oc_create_knx_table_resources();
	oc_create_knx_sec_resources();
	oc_create_knx_swu_resources();

	/*
	  init connectivity ip addresses
	  - SHOULD use the default unicast port as specified in clause 2.6.3.1 with COAP_DEFAULT_PORT = 5683
	  - NOTE: the optional put /dev/port is not implemented, the chosen port is used all the time
		- NOTE: if the port below is defined with '0' the OS assign an ephemeral port (useful for virtual apps on the same machine)
	*/
  oc_connectivity_set_port(KNX_UNICAST_PORT);
	if (oc_connectivity_init() < 0)
	{
		oc_abort("error initializing connectivity for device");
	}
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


const oc_resource_t* oc_core_get_core_resource_by_index(int index)
{
	// check index first, first resource index starts with 0 
	if (index < 0 || index >= OC_NUM_CORE_RESOURCES) 
	{
		return NULL;
	}

	// start of the chain
	extern const oc_resource_t core_resource_dev_sn; 
	const oc_resource_t* resource = &core_resource_dev_sn;

	/*
	 * walk the linked list to the specified index, less readable, optimized, often used
	 * - ptr cannot be NULL since all (57) resources are linked in code
	 * - index 0 = dev_sn,
	 * - index 1 = dev_hwv,
	 * - ...
	 * - index 57 = well-known 
	 *
	 */
	while (index--) 
    resource = resource->next;

	return resource;
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
			const char* wildcard = (char*)memchr(request_rt_ptr, '*', request_rt_ptr_len);

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

			  OC_DBG("rt type '%s'", resource_type_ptr);

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

            OC_DBG("if type '%s'", resource_if_ptr);

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
