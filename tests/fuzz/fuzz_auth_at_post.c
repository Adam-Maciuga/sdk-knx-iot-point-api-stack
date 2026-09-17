/*
 * libFuzzer harness for oc_core_auth_at_post_handler() (api/oc_knx_sec.c).
 *
 * POST /auth/at is the credential-provisioning endpoint: it parses an
 * attacker-controlled CBOR body describing OSCORE recipient/sender IDs,
 * profile, scope and master secret, and installs a new access-token/OSCORE
 * context entry - the largest hand-written parser on the security resource,
 * reachable during pairing before mutual authentication completes.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "oc_ri.h"
#include "oc_rep.h"
#include "oc_buffer_settings.h"
#include "messaging/coap/oc_coap.h"
#include "port/oc_storage.h"
#include "psa/crypto.h"

extern const oc_resource_t core_resource_knx_auth_at;

int
LLVMFuzzerInitialize(int *argc, char ***argv)
{
  (void)argc;
  (void)argv;
  psa_crypto_init();
  oc_storage_config("fuzz_auth_at_post_storage");
  return 0;
}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  if (size > oc_get_max_app_data_size()) {
    return 0;
  }

  oc_rep_t *rep = NULL;
  oc_parse_rep(data, (int)size, &rep);

  oc_response_buffer_t response_buffer;
  memset(&response_buffer, 0, sizeof(response_buffer));
  oc_response_t response;
  memset(&response, 0, sizeof(response));
  response.response_buffer = &response_buffer;

  oc_request_t request;
  memset(&request, 0, sizeof(request));
  request.resource = &core_resource_knx_auth_at;
  request.response = &response;
  request.accept = APPLICATION_CBOR;
  request.request_payload = rep;

  core_resource_knx_auth_at.post_handler.cb(&request, OC_IF_SEC, NULL);

  oc_free_rep(rep);
  return 0;
}
