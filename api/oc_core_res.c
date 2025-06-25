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
#ifdef OC_IOT_ROUTER
#include "oc_knx_gm.h"
#endif 

#include "port/oc_assert.h"
#include <stdarg.h>

#include "oc_storage.h"

#ifdef OC_DYNAMIC_ALLOCATION
#include "oc_endpoint.h"
#include <stdlib.h>
OC_LIST(core_resource_list);
static oc_resource_t* core_resources = NULL;
static oc_device_info_t* oc_device_info = NULL;
#else  
 // TODO fix this for static allocation, this is not used at the moment..
static oc_resource_t core_resources[1 + OCF_D * (OC_MAX_NUM_DEVICES - 1)];
static oc_device_info_t oc_device_info[OC_MAX_NUM_DEVICES];
#endif 

static oc_platform_info_t oc_platform_info; // platform provider     
static size_t device_count = 0;             // holds the current number of allocated devices 

void oc_core_init(void)
{
	oc_core_shutdown();

	#ifdef OC_DYNAMIC_ALLOCATION
	core_resources = (oc_resource_t*) calloc(1, sizeof(oc_resource_t));
	if (!core_resources)
	{
		printf("COULD NOT ALLOCATE CORE RESOURCE\n\n\n\n\n");
		oc_abort("Insufficient stack memory to allocate device resources");
	}

	oc_device_info = NULL;
	#endif 
}

static void oc_core_free_device_info_properties(oc_device_info_t* oc_device_info_item)
{
	if (oc_device_info_item)
	{
		// KNX
		oc_free_string(&(oc_device_info_item->serialnumber));
		oc_free_string(&(oc_device_info_item->hwt));
		oc_free_string(&(oc_device_info_item->model));
		oc_free_string(&(oc_device_info_item->hostname));
	}
}

void oc_core_shutdown(void)
{
	size_t i;
	oc_free_string(&(oc_platform_info.mfg_name));

	#ifdef OC_DYNAMIC_ALLOCATION
	if (oc_device_info)
	{
		#endif 
		for (i = 0; i < device_count; ++i)
		{
			oc_device_info_t* oc_device_info_item = &oc_device_info[i];
			oc_core_free_device_info_properties(oc_device_info_item);
		}

		oc_free_knx_table_resources();
		

		#ifdef OC_DYNAMIC_ALLOCATION
		free(oc_device_info);
		oc_device_info = NULL;
	}
	#endif

	#ifdef OC_DYNAMIC_ALLOCATION
	if (core_resources)
	{
		#endif 
		size_t max_resource =
			1 + (WELLKNOWNCORE * (device_count ? device_count - 1 : 0));
		for (i = 0; i < max_resource; ++i)
		{
			oc_resource_t* core_resource = &core_resources[i];
			oc_ri_free_resource_properties(core_resource);
		}
		#ifdef OC_DYNAMIC_ALLOCATION
		free(core_resources);
		core_resources = NULL;
	}
	#endif 
	device_count = 0;
}

int oc_frame_interfaces_mask_in_response(oc_interface_mask_t iface_mask, bool truncate)
{

	// </point-path-example1>;rt= ":dpa.352.51";if= ":if.i";ct = 50 60

	// start with quote "
	oc_rep_encode_raw((uint8_t*) "\"", 1);

	// used to check if more than a starting <"> was framed
	int total_size = 1;

	if (iface_mask & OC_IF_I)
	{
		if (!truncate)
		{
			// add urn 
			oc_rep_encode_raw((uint8_t*) "urn:knx", 7);
			total_size += 7;
		}
		oc_rep_encode_raw((uint8_t*) ":if.i", 5);
		total_size += 5;
	}
	if (iface_mask & OC_IF_O)
	{
		if (total_size > 1)
		{
			oc_rep_encode_raw((uint8_t*) " ", 1);
			total_size += 1;
		}
		if (!truncate)
		{
			oc_rep_encode_raw((uint8_t*) "urn:knx", 7);
			total_size += 7;
		}
		oc_rep_encode_raw((uint8_t*) ":if.o", 5);
		total_size += 5;
	}
	if (iface_mask & OC_IF_G)
	{
		if (total_size > 1)
		{
			oc_rep_encode_raw((uint8_t*) " ", 1);
			total_size += 1;
		}
		if (!truncate)
		{
			oc_rep_encode_raw((uint8_t*) "urn:knx", 7);
			total_size += 7;
		}
		oc_rep_encode_raw((uint8_t*) ":if.g.s", 6);
		total_size += 6;
	}
	if (iface_mask & OC_IF_C)
	{
		if (total_size > 1)
		{
			oc_rep_encode_raw((uint8_t*) " ", 1);
			total_size += 1;
		}
		if (!truncate)
		{
			oc_rep_encode_raw((uint8_t*) "urn:knx", 7);
			total_size += 7;
		}
		oc_rep_encode_raw((uint8_t*) ":if.c", 5);
		total_size += 5;
	}
	if (iface_mask & OC_IF_P)
	{
		if (total_size > 1)
		{
			oc_rep_encode_raw((uint8_t*) " ", 1);
			total_size += 1;
		}
		if (!truncate)
		{
			oc_rep_encode_raw((uint8_t*) "urn:knx", 7);
			total_size += 7;
		}
		oc_rep_encode_raw((uint8_t*) ":if.p", 5);
		total_size += 5;
	}
	if (iface_mask & OC_IF_D)
	{
		if (total_size > 1)
		{
			oc_rep_encode_raw((uint8_t*) " ", 1);
			total_size += 1;
		}
		if (!truncate)
		{
			oc_rep_encode_raw((uint8_t*) "urn:knx", 7);
			total_size += 7;
		}
		oc_rep_encode_raw((uint8_t*) ":if.d", 5);
		total_size += 5;
	}
	if (iface_mask & OC_IF_A)
	{
		if (total_size > 1)
		{
			oc_rep_encode_raw((uint8_t*) " ", 1);
			total_size += 1;
		}
		if (!truncate)
		{
			oc_rep_encode_raw((uint8_t*) "urn:knx", 7);
			total_size += 7;
		}
		oc_rep_encode_raw((uint8_t*) ":if.a", 5);
		total_size += 5;
	}
	if (iface_mask & OC_IF_S)
	{
		if (total_size > 1)
		{
			oc_rep_encode_raw((uint8_t*) " ", 1);
			total_size += 1;
		}
		oc_rep_encode_raw((uint8_t*) ":if.s", 5);
		total_size += 5;
	}
	if (iface_mask & OC_IF_LI)
	{
		if (total_size > 1)
		{
			oc_rep_encode_raw((uint8_t*) " ", 1);
			total_size += 1;
		}
		if (!truncate)
		{
			oc_rep_encode_raw((uint8_t*) "urn:knx", 7);
			total_size += 7;
		}
		oc_rep_encode_raw((uint8_t*) ":if.ll", 6);
		total_size += 6;
	}
	if (iface_mask & OC_IF_B)
	{
		if (total_size > 1)
		{
			oc_rep_encode_raw((uint8_t*) " ", 1);
			total_size += 1;
		}
		if (!truncate)
		{
			oc_rep_encode_raw((uint8_t*) "urn:knx", 7);
			total_size += 7;
		}
		oc_rep_encode_raw((uint8_t*) ":if.b", 5);
		total_size += 5;
	}
	if (iface_mask & OC_IF_SEC)
	{
		if (total_size > 1)
		{
			oc_rep_encode_raw((uint8_t*) " ", 1);
			total_size += 1;
		}
		if (!truncate)
		{
			oc_rep_encode_raw((uint8_t*) "urn:knx", 7);
			total_size += 7;
		}
		oc_rep_encode_raw((uint8_t*) ":if.sec", 7);
		total_size += 7;
	}
	if (iface_mask & OC_IF_SWU)
	{
		if (total_size > 1)
		{
			oc_rep_encode_raw((uint8_t*) " ", 1);
			total_size += 1;
		}
		if (!truncate)
		{
			oc_rep_encode_raw((uint8_t*) "urn:knx", 7);
			total_size += 7;
		}
		oc_rep_encode_raw((uint8_t*) ":if.swu", 7);
		total_size += 7;
	}
	if (iface_mask & OC_IF_PM)
	{
		if (total_size > 1)
		{
			oc_rep_encode_raw((uint8_t*) " ", 1);
			total_size += 1;
		}
		if (!truncate)
		{
			oc_rep_encode_raw((uint8_t*) "urn:knx", 7);
			total_size += 7;
		}
		oc_rep_encode_raw((uint8_t*) ":if.pm", 6);
		total_size += 6;
	}

	// end with quote "
	oc_rep_encode_raw((uint8_t*) "\"", 1);
	total_size += 1;
	return total_size;
}

size_t oc_core_get_num_devices(void)
{
	return device_count;
}

int oc_core_set_device_fwv(size_t device_index, int major, int minor, int patch)
{
	if (device_index >= oc_core_get_num_devices())
	{
		OC_ERR("device_index %d too large", (int) device_index);
		return -1;
	}
	oc_device_info[device_index].fwv.major = major;
	oc_device_info[device_index].fwv.minor = minor;
	oc_device_info[device_index].fwv.patch = patch;
	return 0;
}

int oc_core_set_device_hwv(size_t device_index, int major, int minor, int patch)
{
	if (device_index >= oc_core_get_num_devices())
	{
		OC_ERR("device_index %d too large", (int) device_index);
		return -1;
	}

	oc_device_info[device_index].hwv.major = major;
	oc_device_info[device_index].hwv.minor = minor;
	oc_device_info[device_index].hwv.patch = patch;
	return 0;
}

int oc_core_set_device_apv(size_t device_index, int major, int minor, int patch)
{
	if (device_index >= oc_core_get_num_devices())
	{
		OC_ERR("device_index %d too large", (int) device_index);
		return -1;
	}

	oc_device_info[device_index].ap.major = major;
	oc_device_info[device_index].ap.minor = minor;
	oc_device_info[device_index].ap.patch = patch;
	return 0;
}

int oc_core_set_device_mid(size_t device_index, uint32_t mid)
{
	if (device_index >= oc_core_get_num_devices())
	{
		OC_ERR("device_index %d too large", (int) device_index);
		return -1;
	}
	oc_device_info[device_index].mid = mid;
	return 0;
}

int oc_core_set_and_store_device_ia(size_t device_index, uint16_t ia)
{
	if (device_index >= oc_core_get_num_devices())
  {
    OC_ERR("device_index %d too large", (int)device_index);
    return -1;
  }

  oc_device_info[device_index].ia = ia;
  oc_storage_write(KNX_STORAGE_IA, (uint8_t*)&ia, sizeof(ia));

  return 0;
}

int oc_core_set_device_hwt(const size_t device_index, const char* hardware_type)
{
	if (device_index >= oc_core_get_num_devices())
	{
		OC_ERR("device_index %llu too large", device_index);
		return -1;
	}
	const size_t hwt_len = strlen(hardware_type);

	oc_free_string(&oc_device_info[device_index].hwt);
	oc_new_string(&oc_device_info[device_index].hwt, hardware_type, hwt_len);

	return 0;
}

int oc_core_set_device_model(const size_t device_index, const char* model)
{
	if (device_index >= oc_core_get_num_devices())
	{
		OC_ERR("device_index %d too large", (int) device_index);
		return -1;
	}
	oc_free_string(&oc_device_info[device_index].model);
	oc_new_string(&oc_device_info[device_index].model, model, strlen(model));

	return 0;
}

int oc_core_set_device_hostname(const size_t device_index, const char* host_name)
{
	if (device_index >= oc_core_get_num_devices())
	{
		OC_ERR("device_index %d too large", (int) device_index);
		return -1;
	}
	oc_free_string(&oc_device_info[device_index].hostname);
	oc_new_string(&oc_device_info[device_index].hostname, host_name, strlen(host_name));

	return 0;
}

uint64_t oc_core_get_device_iid(const size_t device_index)
{
	if (device_index >= oc_core_get_num_devices())
	{
		OC_ERR("device_index %d too large", (int) device_index);
		return -1;
	}

	return oc_device_info[device_index].iid;
}

int oc_core_set_and_store_device_iid(size_t device_index, uint64_t iid)
{
  if (device_index >= oc_core_get_num_devices())
  {
    OC_ERR("device_index %d too large", (int)device_index);
    return -1;
  }

  oc_device_info[device_index].iid = iid;
  oc_storage_write(KNX_STORAGE_IID, (uint8_t*)&iid, sizeof(iid));

  return 0;
}

int oc_core_set_and_store_device_application_version(size_t device_index, int major, int minor, int patch)
{
  if (device_index >= oc_core_get_num_devices())
  {
    OC_ERR("device_index %d too large", (int)device_index);
    return -1;
  }

	oc_device_info[device_index].ap.major = major;
  oc_device_info[device_index].ap.minor = minor;
  oc_device_info[device_index].ap.patch = patch;

	oc_storage_write(KNX_STORAGE_AP_MAJOR, (uint8_t*)&major, sizeof(major));
  oc_storage_write(KNX_STORAGE_AP_MINOR, (uint8_t*)&minor, sizeof(minor));
  oc_storage_write(KNX_STORAGE_AP_PATCH, (uint8_t*)&patch, sizeof(patch));

  return 0;
}

int oc_core_set_and_store_device_fid(size_t device_index, uint64_t fid)
{
	if (device_index >= oc_core_get_num_devices())
	{
		OC_ERR("device_index %d too large", (int) device_index);
		return -1;
	}
	oc_device_info[device_index].fid = fid;
  oc_storage_write(KNX_STORAGE_FID, (uint8_t*)&fid, sizeof(fid));

	return 0;
}

oc_device_info_t* oc_core_add_device(const char* name, const char* version, const char* base,
																		 const char* serialnumber,
																		 oc_core_add_device_cb_t add_device_cb, void* data)
{
	(void) data;
	#ifndef OC_DYNAMIC_ALLOCATION
	if (device_count == OC_MAX_NUM_DEVICES)
	{
		OC_ERR("device limit reached");
		return NULL;
	}
	#else

	// note, there is always 1 resource present, the initial one in the list
	// per device 'WELLKNOWNCORE' resources needed 
	const size_t new_num = 1 + WELLKNOWNCORE * device_count;

	// allocate new device resources
	core_resources = (oc_resource_t*) realloc(core_resources, new_num * sizeof(oc_resource_t));

	if (!core_resources)
	{
		oc_abort("Insufficient memory");
	}

	if (device_count > 0)
	{
		// clear all NEW resources (e.g. for device count 2 : 115-57 -> 58..115)
		oc_resource_t* device_resources = &core_resources[new_num - WELLKNOWNCORE];
		memset(device_resources, 0, WELLKNOWNCORE * sizeof(oc_resource_t));
	}
	else
	{
		// device count is 0 
		oc_device_info = (oc_device_info_t*) realloc(oc_device_info, sizeof(oc_device_info_t));

		if (!oc_device_info)
		{
			oc_abort("Insufficient memory");
		}

		// define extern for below usage
		extern oc_resource_t core_resource_dev_sn;

	  // clear device 0 resources
	  oc_list_add_block(core_resource_list, &core_resource_dev_sn);
	}

	oc_device_info = (oc_device_info_t*) realloc(oc_device_info, (device_count + 1) * sizeof(oc_device_info_t));

	if (!oc_device_info)
	{
		oc_abort("Insufficient memory");
	}

	memset(&oc_device_info[device_count], 0, sizeof(oc_device_info_t));
	oc_device_info[device_count].ia = 0xffff;

	#endif /* OC_DYNAMIC_ALLOCATION */

	/* Construct device resource */
	// int properties = OC_DISCOVERABLE;

	// ensure that the serial number is in lower case
	// it changes the original, but it must be anyhow lower case...
	oc_charstream_convert_to_lower(serialnumber);

	oc_new_string(&oc_device_info[device_count].serialnumber, serialnumber, strlen(serialnumber));
	oc_device_info[device_count].add_device_cb = add_device_cb;

	oc_create_discovery_resource(WELLKNOWNCORE, device_count);
	oc_create_knx_device_resources(device_count);
	oc_create_knx_resources(device_count);
	oc_create_knx_fb_resources(device_count);
	oc_create_knx_fp_resources(device_count);
	oc_create_knx_p_resources(device_count);
	oc_create_knx_sec_resources(device_count);
	oc_create_knx_swu_resources(device_count);
	oc_create_sub_resource(OC_KNX_SUB, device_count);

	#ifdef OC_IOT_ROUTER
	oc_create_knx_iot_router_resources(device_count);
	#endif 

	oc_device_info[device_count].data = data;

	if (oc_connectivity_init(device_count) < 0)
	{
		oc_abort("error initializing connectivity for device");
	}

	/* must be before the increase of device_count */
	oc_init_oscore_from_storage(true);

	device_count++;

	return &oc_device_info[device_count - 1];
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
	oc_assert(uri[0] == '/');
}

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
															 ...)
{
	oc_resource_t* r = oc_core_get_resource_by_index(core_resource_index, device_index);

	if (!r)
	{
		return;
	}

	// const are precompiled resources (device 0 or higher)
	if (r->is_const)
	{
		OC_ERR("oc_core_populate_resource: resource %d is const", core_resource_index);
		return;
	}

	// device index
	r->device = device_index;

	// uri 
	oc_check_uri(uri);
	r->uri.next = NULL;
	r->uri.ptr = uri;
	r->uri.size = strlen(uri) + 1; // include null terminator in size

	// properties
	r->properties = properties;

	// rt types, use variable arguments (stdarg.h)
	va_list rt_list;
	va_start(rt_list, num_resource_types);
	if (num_resource_types > 0)
	{
		oc_new_string_array(&r->types, num_resource_types);
		for (int i = 0; i < num_resource_types; i++)
		{
			const char* resource_type = va_arg(rt_list, const char*);
			oc_assert(strlen(resource_type) < STRING_ARRAY_ITEM_MAX_LEN);
			oc_string_array_add_item(r->types, resource_type);
		}
	}
	va_end(rt_list);

	r->content_type[0] = content_type0;
	r->content_type[1] = content_type1;

	// caller handler
	r->get_handler.cb = get_cb;
	r->put_handler.cb = put_cb;
	r->post_handler.cb = post_cb;
	r->delete_handler.cb = delete_cb;

	// scopes/ interfaces
	// TODO must be set according to 'non const' resource

}

void oc_core_bind_dpt_resource(int core_resource_index, size_t device_index, const char* dpt)
{
	const oc_resource_t* r = oc_core_get_resource_by_index(core_resource_index, device_index);
	if (!r)
	{
		return;
	}
	if (r->is_const)
	{
		OC_ERR("resource is const");
		return;
	}

	oc_resource_bind_dpt((oc_resource_t*) r, dpt);
}

oc_device_info_t* oc_core_get_device_info(size_t device)
{
	if (device >= device_count)
	{
		return NULL;
	}
	return &oc_device_info[device];
}

oc_platform_info_t* oc_core_get_platform_info(void)
{
	return &oc_platform_info;
}

oc_resource_t* oc_core_get_resource_by_index(int index, size_t device)
{
	#ifndef OC_DYNAMIC_ALLOCATION
	if (type == OC_DEV_SN)
	{
		return &core_resources[0];
	}
	return &core_resources[WELLKNOWNCORE * device + type];
	#else
	if (index == OC_DEV_SN)
	{
		// returns for each device the same SN(0) from device 0
		// several device will have only one SN 
		return oc_list_head(core_resource_list);
	}
	if (device != 0)
	{
		// device > 0: need to traverse list of dynamically added core resources 
		// device 5 =  4 * WK(57) + index = index in table
		return &core_resources[WELLKNOWNCORE * (device - 1) + index];
	}

	// device = 0 : need to traverse the 'linked' list of const core resources 
	oc_resource_t* res = oc_list_head(core_resource_list);
	while (index && res)
	{
		// index > 0 (so no SN type can be searched for, see above)
		res = oc_list_item_next(res);
		index--;
	}
	// returns the nth pointer such as for type OC_KNX_SWU (48) it is the 48' pointer 
	return res;
	#endif
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

bool oc_filter_resource_by_rt(const oc_resource_t* resource, oc_request_t* request)
{
	// pre-assumption that 'rt' key is not part of request
  bool match = true, more_query_params; 
	char* rt_ptr = NULL;
	int rt_ptr_len = -1;

  oc_init_query_iterator();
	do
	{
		more_query_params =	oc_iterate_query_get_values(request, "rt", &rt_ptr, &rt_ptr_len);

		// value must be present  
		if (rt_ptr_len > 0)
		{
			// key 'rt' is part of query, check on wildcard rt=* or rt=urn:knx:dpa.201.* 
			const char* wildcard = memchr(rt_ptr, '*', rt_ptr_len);

		  if (wildcard)
			{
				// cut value string len to compare with, such as for
				// - rt=urn:knx:dpa.201.* from 17 -> 16
        // - rt=* from 17 -> 1
		    rt_ptr_len = (int) (wildcard - rt_ptr);
			}

			// default assumption key 'if' is part of query, but value will be not found 
			match = false;

			// 0 .. n types
			for (int i = 0; i < (int) oc_string_array_get_allocated_size(resource->types); i++)
			{
				const int resource_type_len =	oc_string_array_get_item_size(resource->types, i);
				const char* resource_type_ptr =	oc_string_array_get_item(resource->types, i);
				PRINT("rt type len and value '%.*s'", resource_type_len,	resource_type_ptr);

			  if (wildcard)
				{
					// on wildcard scan number of chars up to position of wildcard only
			    if (strncmp(rt_ptr, resource_type_ptr, rt_ptr_len) == 0)
					{
						// value from request is contained in resource type
						// urn:knx:dpa.201.* matches urn:knx:dpa.201.51/52/xxx scanning 16 chars 
			      return true;
					}
				}

				// on NO wildcard scan number of all chars, take care that request string value
				// has same len as resource len, otherwise you compare 'urn:knx:dpa' (11)
				// with 'urn:knx:dpa.201.50' (18) that always matches 
				if (rt_ptr_len == (int) resource_type_len &&
						strncmp(rt_ptr, resource_type_ptr, rt_ptr_len) == 0)
				{
          // value from request matches to resource type and len also matches
          // urn:knx:dpa.201.51 matches urn:knx:dpa.201.51 scanning 18 chars 
				  return true;
				}
			}
		}
	}
	while (more_query_params);
	return match;
}

bool oc_filter_resource_by_if(const oc_resource_t* resource, oc_request_t* request)
{
  // pre-assumption that 'if' key is not part of request
  bool match = true, more_query_params;
	char* if_ptr = NULL;
	int if_ptr_len = -1;

	oc_init_query_iterator();
	do
	{
		more_query_params = oc_iterate_query_get_values(request, "if", &if_ptr, &if_ptr_len);

		// value must be present  
		if (if_ptr_len > 8)
		{
      // key 'if' is part of query, check on wildcard if=* or if=urn:knx:if.s*
			const char* wildcard = memchr(if_ptr, '*', if_ptr_len);
			if (wildcard)
			{
				return true;
			}

			// default assumption key 'if' is part of query, but value will be not found 
			match = false;

			// get if's from resource 
			oc_interface_mask_t interface = OC_IF_NONE;

			if (oc_resource_get_acl_and_interface_mask(resource, request->request_method, NULL, &interface))
			{
				// get the 'if' string from the 'if' bit mask, such as 'if.ll' from OC_IF_LI
				const char* resource_interface = get_interface_string(interface);

				// the value contains urn:knx:if.xxx; +8 points to the last DOT '.' , -8 is len of all - sizeof(urn:knx:if.) 
				if (strncmp(resource_interface, if_ptr + 8, if_ptr_len - 8) == 0)
				{
					return true;
				}

			}
		}
	}
	while (more_query_params);
	return match;
}
