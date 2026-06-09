/*
 * Unit tests for the client-side API helpers (api/oc_client_api.c).
 *
 * Covers the unit-testable, pure helpers:
 *   oc_lf_number_of_entries  — count RFC-6690 link-format records in a payload
 *   oc_lf_get_entry_uri      — extract the <uri> of the Nth record
 *   oc_lf_get_entry_param    — extract a ;param=value of the Nth record
 *                              (also drives the static oc_lf_get_line)
 *   oc_get_response_payload_raw — NULL-guarded raw-payload accessor
 *   oc_free_server_endpoints — free a linked list of endpoints
 *   oc_close_session         — plain (non-secured/non-TCP) endpoint is a no-op
 *
 * INTEGRATION (documented, not unit-tested here):
 *   oc_do_s_mode_message_update / oc_do_well_known_message_update send the
 *   prepared CoAP packet via coap_send_message(), which posts to the OC message
 *   buffer process and hands the datagram to the network layer; verifying their
 *   effect requires a running process scheduler + live transport.
 *   oc_init_s_mode_message_update / oc_init_well_known_message_update are
 *   unit-tested below (smoke level): they allocate the outbound message and
 *   build the CoAP header/token/uri without touching the network. Deeper
 *   assertions on the populated packet are not possible because the message and
 *   coap_packet_t live in file-static storage with no public accessor.
 *   oc_send_ping / oc_remove_ping_handler (OC_TCP) send a CoAP ping and use the
 *   delayed-callback / client-cb registry. All require a live transport.
 *   oc_close_session's SECURED / TCP branches call into TLS / connectivity
 *   teardown (live session) and are integration-level; only the plain no-op
 *   branch is unit-tested.
 */

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>

extern "C" {
#include "oc_api.h"
#include "oc_client_state.h"
#include "oc_endpoint.h"
#include "oc_helpers.h"
#include "util/oc_mmem.h"
#include "port/oc_random.h"
#include "port/oc_network_events_mutex.h"
}

/* a representative two-record link-format payload */
static const char *LF2 =
  "<coap://[fe80::1]:5683/p/a>;rt=\"urn:knx:dpa.352.51\";if=if.a;ct=60,"
  "<coap://[fe80::1]:5683/p/b>;rt=\"urn:knx:dpa.353.52\";if=if.s;ct=50";

class ClientApiLF : public ::testing::Test {
protected:
  void SetUp() override { oc_mmem_init(); }
};

/* ───────────────────────────── oc_lf_number_of_entries ───────────────────── */

TEST_F(ClientApiLF, NumberOfEntriesNullPayloadIsZero)
{
  EXPECT_EQ(oc_lf_number_of_entries(nullptr, 10), 0);
}

TEST_F(ClientApiLF, NumberOfEntriesTooShortIsZero)
{
  EXPECT_EQ(oc_lf_number_of_entries("<a>", 3), 0);
}

TEST_F(ClientApiLF, NumberOfEntriesSingleRecord)
{
  const char *one = "<coap://[fe80::1]/p/a>;ct=60";
  EXPECT_EQ(oc_lf_number_of_entries(one, (int)strlen(one)), 1);
}

TEST_F(ClientApiLF, NumberOfEntriesTwoRecords)
{
  EXPECT_EQ(oc_lf_number_of_entries(LF2, (int)strlen(LF2)), 2);
}

/* ───────────────────────────── oc_lf_get_entry_uri ───────────────────────── */

TEST_F(ClientApiLF, GetEntryUriFirstRecord)
{
  const char *uri = nullptr;
  int uri_len = 0;
  EXPECT_EQ(oc_lf_get_entry_uri(LF2, (int)strlen(LF2), 0, &uri, &uri_len), 1);
  ASSERT_GT(uri_len, 0);
  EXPECT_EQ(std::string(uri, uri_len), "coap://[fe80::1]:5683/p/a");
}

TEST_F(ClientApiLF, GetEntryUriSecondRecord)
{
  const char *uri = nullptr;
  int uri_len = 0;
  EXPECT_EQ(oc_lf_get_entry_uri(LF2, (int)strlen(LF2), 1, &uri, &uri_len), 1);
  ASSERT_GT(uri_len, 0);
  EXPECT_EQ(std::string(uri, uri_len), "coap://[fe80::1]:5683/p/b");
}

/* ───────────────────────────── oc_lf_get_entry_param ─────────────────────── */

TEST_F(ClientApiLF, GetEntryParamRtFirstRecord)
{
  const char *p = nullptr;
  int p_len = 0;
  EXPECT_EQ(oc_lf_get_entry_param(LF2, (int)strlen(LF2), 0, "rt", &p, &p_len), 1);
  ASSERT_GT(p_len, 0);
  /* value is returned including the surrounding quotes */
  EXPECT_NE(std::string(p, p_len).find("urn:knx:dpa.352.51"), std::string::npos);
}

TEST_F(ClientApiLF, GetEntryParamIfSecondRecord)
{
  const char *p = nullptr;
  int p_len = 0;
  EXPECT_EQ(oc_lf_get_entry_param(LF2, (int)strlen(LF2), 1, "if", &p, &p_len), 1);
  ASSERT_GT(p_len, 0);
  EXPECT_EQ(std::string(p, p_len), "if.s");
}

TEST_F(ClientApiLF, GetEntryParamMissingReturnsZero)
{
  const char *p = nullptr;
  int p_len = 0;
  /* "xx" is not a parameter of the record */
  EXPECT_EQ(oc_lf_get_entry_param(LF2, (int)strlen(LF2), 0, "xx", &p, &p_len), 0);
}

/* ─────────────────────────── oc_get_response_payload_raw ──────────────────── */

TEST(ClientApiResponse, ResponsePayloadRawNullArgsReturnFalse)
{
  oc_client_response_t resp;
  memset(&resp, 0, sizeof(resp));
  const uint8_t *payload = nullptr;
  size_t size = 0;
  oc_content_format_t cf = CONTENT_NONE;

  EXPECT_FALSE(oc_get_response_payload_raw(nullptr, &payload, &size, &cf));
  EXPECT_FALSE(oc_get_response_payload_raw(&resp, nullptr, &size, &cf));
  EXPECT_FALSE(oc_get_response_payload_raw(&resp, &payload, nullptr, &cf));
  EXPECT_FALSE(oc_get_response_payload_raw(&resp, &payload, &size, nullptr));
}

TEST(ClientApiResponse, ResponsePayloadRawEmptyPayloadReturnsFalse)
{
  oc_client_response_t resp;
  memset(&resp, 0, sizeof(resp)); /* _payload == NULL, _payload_len == 0 */
  const uint8_t *payload = nullptr;
  size_t size = 0;
  oc_content_format_t cf = CONTENT_NONE;
  EXPECT_FALSE(oc_get_response_payload_raw(&resp, &payload, &size, &cf));
}

TEST(ClientApiResponse, ResponsePayloadRawReturnsStoredPayload)
{
  const uint8_t data[] = { 0xDE, 0xAD, 0xBE, 0xEF };
  oc_client_response_t resp;
  memset(&resp, 0, sizeof(resp));
  resp._payload = data;
  resp._payload_len = sizeof(data);
  resp.content_format = APPLICATION_CBOR;

  const uint8_t *payload = nullptr;
  size_t size = 0;
  oc_content_format_t cf = CONTENT_NONE;
  EXPECT_TRUE(oc_get_response_payload_raw(&resp, &payload, &size, &cf));
  EXPECT_EQ(payload, data);
  EXPECT_EQ(size, sizeof(data));
  EXPECT_EQ(cf, APPLICATION_CBOR);
}

/* ─────────────────────────── oc_free_server_endpoints ─────────────────────── */

TEST(ClientApiEndpoints, FreeServerEndpointsNullIsNoOp)
{
  oc_free_server_endpoints(nullptr);
  SUCCEED();
}

TEST(ClientApiEndpoints, FreeServerEndpointsFreesChain)
{
  /* build a 3-node endpoint chain and free it; ASan verifies no leak / double
   * free (oc_new_endpoint allocates, oc_free_server_endpoints walks ->next). */
  oc_endpoint_t *a = oc_new_endpoint();
  oc_endpoint_t *b = oc_new_endpoint();
  oc_endpoint_t *c = oc_new_endpoint();
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  ASSERT_NE(c, nullptr);
  a->next = b;
  b->next = c;
  c->next = nullptr;

  oc_free_server_endpoints(a);
  SUCCEED();
}

/* ─────────────────────────────── oc_close_session ─────────────────────────── */

TEST(ClientApiSession, CloseSessionPlainEndpointIsNoOp)
{
  oc_endpoint_t ep;
  memset(&ep, 0, sizeof(ep));
  ep.flags = IPV6; /* neither SECURED nor TCP */
  /* must not call into TLS / connectivity teardown -> no crash */
  oc_close_session(&ep);
  SUCCEED();
}

/* ──────────────────── oc_init_s_mode / well_known message ──────────────────
 * These build the outbound CoAP packet (header, token, uri/query) into the
 * file-static request buffer; they allocate a heap message but do NOT send it.
 * We can only assert the boolean success path and that the full body runs
 * without crashing (the populated packet is not publicly accessible).
 * ─────────────────────────────────────────────────────────────────────────── */

class ClientApiInitMsg : public ::testing::Test {
protected:
  oc_endpoint_t ep_;
  void SetUp() override
  {
    oc_network_event_handler_mutex_init(); /* oc_allocate_message lock */
    oc_random_init();                      /* token generation */
    memset(&ep_, 0, sizeof(ep_));
    ep_.flags = IPV6;
    ep_.addr.ipv6.port = 5683;
    ep_.addr.ipv6.address[15] = 1; /* ::1 */
  }
  void TearDown() override { oc_random_destroy(); }
};

TEST_F(ClientApiInitMsg, InitSModeConfirmableSucceeds)
{
  EXPECT_TRUE(oc_init_s_mode_message_update(&ep_, "/.knx", false));
}

TEST_F(ClientApiInitMsg, InitSModeNonConfirmableSucceeds)
{
  EXPECT_TRUE(oc_init_s_mode_message_update(&ep_, "/.knx", true));
}

TEST_F(ClientApiInitMsg, InitWellKnownConfirmableSucceeds)
{
  oc_client_cb_t cb;
  memset(&cb, 0, sizeof(cb));
  cb.mid = 0x1234;
  cb.token_len = COAP_TOKEN_LEN;
  for (int i = 0; i < COAP_TOKEN_LEN; i++) cb.token[i] = (uint8_t)(i + 1);

  EXPECT_TRUE(oc_init_well_known_message_update(
    &ep_, "/.well-known/core", "rt=urn:knx:if.pm", false, &cb));
}

TEST_F(ClientApiInitMsg, InitWellKnownNonConfirmableSucceeds)
{
  oc_client_cb_t cb;
  memset(&cb, 0, sizeof(cb));
  cb.mid = 0x4321;
  cb.token_len = COAP_TOKEN_LEN;
  for (int i = 0; i < COAP_TOKEN_LEN; i++) cb.token[i] = (uint8_t)(i + 1);

  EXPECT_TRUE(oc_init_well_known_message_update(
    &ep_, "/.well-known/core", "rt=urn:knx:if.pm", true, &cb));
}
