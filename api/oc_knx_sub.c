/*
// Copyright (c) 2023 Cascoda Ltd
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

#include "oc_knx_sub.h"
#include "oc_helpers.h"
#include "oc_ri.h"
#include "oc_core_res.h"
#include "oc_api.h"

static void oc_core_sub_delete_handler(oc_request_t* request, oc_interface_mask_t iface_mask, void* data)
{

	(void) iface_mask;
	(void) data;
	oc_prepare_no_format_response_no_payload(request, OC_STATUS_DELETED);
}

OC_CORE_CREATE_CONST_RESOURCE_LINKED(sub, knx_a_sen, 0, "/sub",
																		 APPLICATION_LINK_FORMAT, CONTENT_NONE,
																		 OC_DISCOVERABLE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 NULL, OC_ACL_NONE, OC_IF_NONE,
																		 oc_core_sub_delete_handler, OC_ACL_P, OC_IF_P,
																		 NULL, OC_SIZE_ZERO());

void
oc_create_sub_resource(int resource_idx, size_t device_index)
{
	OC_DBG("create /sub resources");

	if (device_index == 0)
	{
		OC_DBG("device 0: KNX device resources created statically");
		return;
	}

	oc_core_populate_resource(resource_idx, device_index, "/sub",
														APPLICATION_CBOR,CONTENT_NONE, 
														OC_DISCOVERABLE, 0, 0, 0, oc_core_sub_delete_handler,
														0);
}