/*
-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
 Copyright (c) 2024-2025 KNX Association
-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
*/

#include "ctype.h"

#include "oc_api.h"
#include "knx_iot_virtual.h"

bool app_is_secure(void)
{
  // may produce a warning if OC_OSCORE is not specified ...
  // but here it is integral part of CMake
  return OC_OSCORE ? true : false;
}


void app_str_to_upper(char* str)
{
  while (*str != '\0')
  {
    *str = (char)toupper(*str);
    str++;
  }
}

int32_t app_get_channel_and_point(const channel_t* channel, const void* user_data)
{
  oc_string_t href_caller;
  oc_string_t href_resource;

  oc_new_string(&href_caller, user_data, strlen(user_data));

  // scan channels
  for (uint16_t c = 0; c < NUM_CHANNELS; c++)
  {
    // scan points
    for (uint16_t p = 0; p < NUM_POINTS; p++)
    {
      oc_new_string(&href_resource, channel[c].point[p].href, strlen(channel[c].point[p].href));

      if (oc_url_cmp(href_caller, href_resource) == 0)
      {
        // hit
        oc_free_string(&href_caller);
        oc_free_string(&href_resource);

        // construct index, examples
        // p/1 --> channel 0 / datapoint 1 = 0
        // p/2 --> channel 0 / datapoint 2 = 1
        // p/3 --> channel 1 / datapoint 1 = 2^16 + 1
        return c << 16 | p;
      }

      oc_free_string(&href_resource);
    }
  }
  return -1;
}

static oc_event_callback_retval_t send_delayed_response(void* context)
{
  oc_separate_response_t* response = context;

  if (response->active)
  {
    oc_set_separate_response_buffer(response);
    oc_send_separate_response(response, OC_STATUS_CHANGED);
    PRINT("Delayed response sent");
  }
  else
  {
    PRINT("Delayed response NOT active");
  }

  return OC_EVENT_DONE;
}

void swu_cb(oc_separate_response_t* response, size_t binary_size, size_t offset, uint8_t* payload, size_t len, void* data)
{
  (void)binary_size;
  (void)data;

  char filename[] = "./downloaded.bin";
  PRINT("swu_cb %s block=%d size=%d ", filename, (int)offset, (int)len);

  FILE* write_ptr = fopen("downloaded_bin", "ab");
  const size_t n = fwrite(payload, sizeof(*payload), len, write_ptr);
  const size_t r = fclose(write_ptr);
  PRINT("written data: %llu, operation ok (=0): %llu", n, r);

  oc_set_delayed_callback(response, &send_delayed_response, 0);
}

