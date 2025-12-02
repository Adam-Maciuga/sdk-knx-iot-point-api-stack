/*
// Copyright (c) 2016 Intel Corporation
// Copyright (c) 2021-2023 Cascoda Ltd.
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

#include "messaging/coap/coap.h"
#ifdef OC_TCP
#include "messaging/coap/coap_signal.h"
#endif 
#include "oc_api.h"

#ifdef OC_CLIENT

coap_packet_t request[1];
oc_client_cb_t *client_cb;

//#define OC_BLOCK_WISE_REQUEST

#ifdef OC_BLOCK_WISE_REQUEST
static oc_blockwise_state_t *request_buffer = NULL;
#endif 

#ifdef OC_OSCORE
// a static pointer, to allocate/ release an outgoing mc message (used like a 2-state state machine)
oc_message_t *multicast_update = NULL; 
#endif

#ifdef OC_OSCORE

bool oc_do_multicast_update(void)
{
  const int payload_size = oc_rep_get_encoded_payload_size();

  if (payload_size) 
  {
    // multicast_update is initialized
    coap_set_payload(request, multicast_update->data + COAP_MAX_HEADER_SIZE, payload_size);
  }
  else 
  {
    // here it may jump with a NULL ptr to the error handling but this is checked there
    oc_message_unref(multicast_update);
    multicast_update = NULL;
    return false;
  }

  // is still the inner header ...
  coap_set_header_content_format(request, APPLICATION_CBOR);

  multicast_update->length =  coap_serialize_message(request, multicast_update->data);

  if (multicast_update->length > 0) 
  {
    OC_INF("sent multicast message - OK");
    oc_send_message(multicast_update);
  }
  else 
  {
    OC_WRN("sent multicast message - ERROR");
    oc_message_unref(multicast_update);
    multicast_update = NULL;
    return false;
  }

  #ifdef OC_IPV4
  oc_message_t *multicast_update4 = oc_internal_allocate_outgoing_message();
  if (multicast_update4) {
    oc_make_ipv4_endpoint(mcast4, IPV4 | MULTICAST | SECURED, 5683, 0xe0, 0x00,
                          0x01, 0xbb);

    memcpy(&multicast_update4->endpoint, &mcast4, sizeof(oc_endpoint_t));

    multicast_update4->length = multicast_update->length;
    memcpy(multicast_update4->data, multicast_update->data,
           multicast_update->length);

    oc_send_message(multicast_update4);
  }
  #endif 

  multicast_update = NULL;
  return true;
}


bool oc_init_multicast_update(oc_endpoint_t* mcast, const char *uri)
{
  multicast_update = oc_internal_allocate_outgoing_message();

  if (!multicast_update) 
  {
    return false;
  }

  memcpy(&multicast_update->endpoint, mcast, sizeof(oc_endpoint_t));
  oc_rep_new(multicast_update->data + COAP_MAX_HEADER_SIZE, OC_BLOCK_SIZE);

  coap_udp_init_message(request, COAP_TYPE_NON, OC_POST, coap_get_next_mid());

  // still the inner message
  coap_set_header_accept(request, APPLICATION_CBOR);

  // set here fix 8 byte token len
  request->token_len = 8; 
  const uint32_t a = oc_random_value(); memcpy(request->token + 0, &a, sizeof(a));
  const uint32_t b = oc_random_value(); memcpy(request->token + 4, &b, sizeof(b));

  coap_set_header_uri_path(request, uri, strlen(uri));

  return true;
}
#endif 

void
oc_free_server_endpoints(oc_endpoint_t *endpoint)
{
  while (endpoint) 
  {
    // tmp copy, will be released next ...
    oc_endpoint_t* next = endpoint->next;
    oc_free_endpoint(endpoint);
    endpoint = next;
  }
}

bool oc_get_response_payload_raw(oc_client_response_t *response,
                            const uint8_t **payload, size_t *size,
                            oc_content_format_t *content_format)
{
  if (!response || !payload || !size || !content_format) {
    return false;
  }
  if (response->_payload && response->_payload_len > 0) {
    *content_format = response->content_format;
    *payload = response->_payload;
    *size = response->_payload_len;
    return true;
  }
  return false;
}


















#ifdef OC_TCP
oc_event_callback_retval_t
oc_remove_ping_handler(void *data)
{
  oc_client_cb_t *cb = (oc_client_cb_t *)data;

  oc_client_response_t timeout_response;
  timeout_response.code = OC_PING_TIMEOUT;
  timeout_response.endpoint = &cb->endpoint;
  timeout_response.user_data = cb->user_data;
  cb->handler.response(&timeout_response);

  return oc_ri_remove_client_cb(cb);
}

bool
oc_send_ping(bool custody, oc_endpoint_t *endpoint, uint16_t timeout_seconds,
             oc_response_handler_t handler, void *user_data)
{
  oc_client_handler_t client_handler = {
    .response = handler,
    .discovery = NULL,
    .discovery_all = NULL,
  };

  oc_client_cb_t *cb = oc_ri_alloc_client_cb(
    "/ping", endpoint, 0, NULL, client_handler, LOW_QOS, user_data);
  if (!cb)
    return false;

  if (!coap_send_ping_message(endpoint, custody ? 1 : 0, cb->token,
                              cb->token_len)) {
    oc_ri_remove_client_cb(cb);
    return false;
  }

  oc_set_delayed_callback(cb, oc_remove_ping_handler, timeout_seconds);
  return true;
}
#endif 

// -----------------------------------------------------------------------------



void oc_close_session(oc_endpoint_t *endpoint)
{
  if (endpoint->flags & SECURED) 
  {
    #ifdef OC_SECURITY
    oc_tls_close_connection(endpoint);
    #endif 
  }
  else if (endpoint->flags & TCP) 
  {
    #ifdef OC_TCP
    oc_connectivity_end_session(endpoint);
    #endif
  }
}

// -----------------------------------------------------------------------------

int oc_lf_number_of_entries(const char *payload, int payload_len)
{
  int nr_entries = 0;
  if (payload == NULL) {
    return nr_entries;
  }
  if (payload_len < 5) {
    return nr_entries;
  }

  // multiple lines
  for (int i = 0; i < payload_len; i++) 
  {
    if (payload[i] == ',')
    {
      nr_entries++;
    }
  }
  if (nr_entries > 0) {
    // add the last entry, that does not have the continuation character.
    nr_entries++;
  }

  if (nr_entries == 0) {
    // only 1 line
    if (payload[0] == '<') {
      nr_entries = 1;
    }
  }

  return nr_entries;
}

static int
oc_lf_get_line(const char *payload, int payload_len, int entry,
               const char **line, int *line_len)
{
  int nr_entries = 0;
  int i;
  if (payload == NULL) {
    return nr_entries;
  }
  if (payload_len < 5) {
    return nr_entries;
  }

  int begin_line_index = 0;
  int end_line_index = 0;
  bool begin_set = false;
  bool end_set = false;

  // find begin
  for (i = 0; i < payload_len - 1; i++) {
    if (entry == nr_entries) {
      if (begin_set == false) {
        begin_line_index = i;
        begin_set = true;
      }
    }
    if (entry + 1 == nr_entries) {
      if (end_set == false) {
        end_line_index = i;
        end_set = true;
      }
    }
    if (payload[i] == ',') {
      nr_entries++;
    }
  }
  if (end_line_index == 0) {
    end_line_index = payload_len;
  }

  if (payload[begin_line_index] == '\n') {
    begin_line_index++;
  }
  // remove the trailing comma, if it exists.
  if (payload[end_line_index - 1] == ',') {
    end_line_index--;
  }
  int line_tot = end_line_index - begin_line_index;

  *line = &payload[begin_line_index];
  *line_len = line_tot;

  return 1;
}

int
oc_lf_get_entry_uri(const char *payload, int payload_len, int entry,
                    const char **uri, int *uri_len)
{
  const char *line = NULL;
  int line_len = 0;
  int begin_uri = 0;
  int end_uri = 0;

  oc_lf_get_line(payload, payload_len, entry, &line, &line_len);

  for (int i = 0; i < line_len; i++) {
    if (line[i] == '<') {
      begin_uri = i + 1;
    }
    if (line[i] == '>') {
      end_uri = i;
      break;
    }
  }

  *uri = &line[begin_uri];
  *uri_len = end_uri - begin_uri;

  return 1;
}

int
oc_lf_get_entry_param(const char *payload, int payload_len, int entry,
                      const char *param, const char **p_out, int *p_len)
{
  const char *line = NULL;
  int line_len = 0;
  int i;
  int begin_param = 0;
  int end_param = 0;
  int found = 0;

  oc_lf_get_line(payload, payload_len, entry, &line, &line_len);

  // <coap://[fe80::8d4c:632a:c5e7:ae09]:60054/p/a>;rt="urn:knx:dpa.352.51";if=if.a;ct=60
  int param_len = (int)strlen(param);
  for (i = 0; i < line_len - param_len - 1; i++) {
    if (line[i] == ';') {
      if (strncmp(&line[i + 1], param, param_len) == 0) {
        begin_param = i + 1;
        found = 1;
        break;
      }
    }
  }
  if (found == 1) {
    for (i = begin_param + 1; i < line_len - 1; i++) {
      if (line[i] == ';') {
        end_param = i;
        break;
      }
    }
    if (end_param == 0) {
      end_param = line_len;
    }

    // remove the "param=" part from the return value.
    *p_out = &line[begin_param + param_len + 1];
    *p_len = end_param - (begin_param + param_len + 1);

  } else {
    *p_out = line;
    *p_len = line_len;
  }

  return found;
}

#endif 
