/*
 * Unit tests for messaging/coap/observe.c
 *
 * Covers:
 *   coap_observe_handler     — add observer via OC_OBSERVE_REGISTER
 *   coap_remove_observer_by_client — remove by endpoint
 *   coap_remove_observer_by_token  — remove by token + endpoint
 *   coap_remove_observer_by_mid    — remove by MID + endpoint
 *   coap_remove_observer_by_resource — remove by resource pointer
 *   coap_free_all_observers        — clear everything
 *
 * Requirements:
 *   - oc_network_event_handler_mutex_init() for observer memb
 *   - Minimal oc_resource_t with uri + runtime_data for observer bookkeeping
 */

#include <gtest/gtest.h>
#include <algorithm>
#include <vector>

extern "C" {
#include "messaging/coap/observe.h"
#include "messaging/coap/coap.h"
#include "messaging/coap/oc_coap.h"
#include "messaging/coap/transactions.h"
#include "messaging/coap/constants.h"
#include "port/oc_network_events_mutex.h"
#include "oc_ri.h"
#include "api/oc_knx_sec.h"
#include "util/oc_mmem.h"
#include <stdlib.h>
#include <string.h>

/* the /k resource descriptor (api/oc_knx.c) */
extern const oc_resource_t core_resource_knx_k;

}

/* ---------------- helpers ------------------------------------------------ */

static oc_endpoint_t make_endpoint(uint16_t port)
{
  oc_endpoint_t ep;
  memset(&ep, 0, sizeof(ep));
  ep.flags = IPV6;
  ep.addr.ipv6.port = port;
  ep.addr.ipv6.address[15] = 1; /* ::1 */
  return ep;
}

/*
 * Build a minimal oc_resource_t with a valid uri and runtime_data.
 * The uri MUST start with '/' because coap_remove_observer accesses
 * oc_string(resource->uri) + 1.
 */
struct TestResource {
  oc_resource_t resource;
  oc_resource_data_t data;

  TestResource(const char *uri_path) {
    memset(&resource, 0, sizeof(resource));
    memset(&data, 0, sizeof(data));
    oc_new_string(&resource.uri, uri_path, strlen(uri_path));
    resource.runtime_data = &data;
  }
  ~TestResource() {
    oc_free_string(&resource.uri);
  }
};

/*
 * Add an observer through coap_observe_handler by crafting the
 * minimal coap_packet_t request + response.
 * The handler requires an "lt" query parameter (KNX spec 2.5.11.6).
 */
static int add_test_observer(oc_resource_t *resource, oc_endpoint_t *ep,
                             const uint8_t *token, uint8_t token_len,
                             const char *uri, size_t uri_len,
                             const char *query = "lt=86400")
{
  coap_packet_t req;
  memset(&req, 0, sizeof(req));
  req.code = COAP_GET;
  req.observe = OC_OBSERVE_REGISTER;
  SET_OPTION(&req, COAP_OPTION_OBSERVE);
  memcpy(req.token, token, token_len);
  req.token_len = token_len;
  req.uri_path = uri;
  req.uri_path_len = (uint32_t)uri_len;

  /* lt=86400 is mandatory for observe registration after implement_observe */
  req.uri_query = query;
  req.uri_query_len = (uint32_t)strlen(query);
  SET_OPTION(&req, COAP_OPTION_URI_QUERY);

  coap_packet_t res;
  memset(&res, 0, sizeof(res));
  res.code = CONTENT_2_05;

  return coap_observe_handler(&req, &res, resource, 0, ep);
}

static void observe_get_handler(oc_request_t *request, oc_interface_mask_t,
                                void *)
{
  static const uint8_t payload[] = {0xA1, 0x01, 0xF5};
  memcpy(request->response->response_buffer->buffer, payload, sizeof(payload));
  request->response->response_buffer->response_length = sizeof(payload);
  request->response->response_buffer->code = CONTENT_2_05;
  request->response->response_buffer->content_format = APPLICATION_CBOR;
}

extern "C" bool __wrap_oc_main_initialized(void) { return true; }
extern "C" void __wrap_oc_etimer_restart(struct oc_etimer *) {}
static std::vector<uint16_t> g_sent_ports; /* destination port of each sent message */

extern "C" void __wrap_coap_send_message(oc_message_t *message)
{
  g_sent_ports.push_back(message->endpoint.addr.ipv6.port);
  oc_message_unref(message);
}

/* ---------------- fixture ------------------------------------------------ */

class CoapObserve : public ::testing::Test {
protected:
  void SetUp() override {
    oc_network_event_handler_mutex_init();
    g_sent_ports.clear();
  }
  void TearDown() override {
    coap_free_all_observers();
    oc_network_event_handler_mutex_destroy();
  }
};

/* ---------------- tests -------------------------------------------------- */

TEST_F(CoapObserve, AddObserverViaHandler)
{
  TestResource tr("/test");
  oc_endpoint_t ep = make_endpoint(5683);
  uint8_t token[] = {0xAA, 0xBB};

  int dup = add_test_observer(&tr.resource, &ep, token, 2, "test", 4);

  /* dup == 0 means first observer (no previous duplicate removed) */
  EXPECT_EQ(dup, 0);
  EXPECT_EQ(tr.data.num_observers, 1);

  coap_free_all_observers();
}

TEST_F(CoapObserve, DuplicateObserverReplacesExisting)
{
  TestResource tr("/test");
  oc_endpoint_t ep = make_endpoint(5683);
  uint8_t token1[] = {0x01};
  uint8_t token2[] = {0x02};

  add_test_observer(&tr.resource, &ep, token1, 1, "test", 4);
  EXPECT_EQ(tr.data.num_observers, 1);

  /* Same endpoint + same URI + same iface → replaces the old one */
  int dup = add_test_observer(&tr.resource, &ep, token2, 1, "test", 4);
  EXPECT_EQ(dup, 1); /* 1 duplicate was removed */
  EXPECT_EQ(tr.data.num_observers, 1);

  coap_free_all_observers();
}

TEST_F(CoapObserve, RemoveByClient)
{
  TestResource tr("/test");
  oc_endpoint_t ep = make_endpoint(5683);
  uint8_t token[] = {0x10};

  add_test_observer(&tr.resource, &ep, token, 1, "test", 4);
  EXPECT_EQ(tr.data.num_observers, 1);

  int removed = coap_remove_observer_by_client(&ep);
  EXPECT_EQ(removed, 1);
  EXPECT_EQ(tr.data.num_observers, 0);
}

TEST_F(CoapObserve, RemoveByClientNoMatch)
{
  TestResource tr("/test");
  oc_endpoint_t ep1 = make_endpoint(5683);
  oc_endpoint_t ep2 = make_endpoint(5684);
  uint8_t token[] = {0x20};

  add_test_observer(&tr.resource, &ep1, token, 1, "test", 4);

  int removed = coap_remove_observer_by_client(&ep2);
  EXPECT_EQ(removed, 0);
  EXPECT_EQ(tr.data.num_observers, 1);

  coap_free_all_observers();
}

TEST_F(CoapObserve, RemoveByToken)
{
  TestResource tr("/test");
  oc_endpoint_t ep = make_endpoint(5683);
  uint8_t token[] = {0xDE, 0xAD};

  add_test_observer(&tr.resource, &ep, token, 2, "test", 4);
  EXPECT_EQ(tr.data.num_observers, 1);

  int removed = coap_remove_observer_by_token(&ep, token, 2);
  EXPECT_EQ(removed, 1);
  EXPECT_EQ(tr.data.num_observers, 0);
}

TEST_F(CoapObserve, RemoveByTokenNoMatch)
{
  TestResource tr("/test");
  oc_endpoint_t ep = make_endpoint(5683);
  uint8_t token[] = {0xBE, 0xEF};
  uint8_t wrong[] = {0xFF, 0xFF};

  add_test_observer(&tr.resource, &ep, token, 2, "test", 4);

  int removed = coap_remove_observer_by_token(&ep, wrong, 2);
  EXPECT_EQ(removed, 0);
  EXPECT_EQ(tr.data.num_observers, 1);

  coap_free_all_observers();
}

TEST_F(CoapObserve, RemoveByMid)
{
  TestResource tr("/test");
  oc_endpoint_t ep = make_endpoint(5683);
  uint8_t token[] = {0x30};

  add_test_observer(&tr.resource, &ep, token, 1, "test", 4);

  /* Observer's last_mid is set to 0 by add_observer, so query with 0 */
  int removed = coap_remove_observer_by_mid(&ep, 0);
  EXPECT_EQ(removed, 1);
  EXPECT_EQ(tr.data.num_observers, 0);
}

TEST_F(CoapObserve, RemoveByResource)
{
  TestResource tr1("/res1");
  TestResource tr2("/res2");
  oc_endpoint_t ep = make_endpoint(5683);
  uint8_t t1[] = {0x40};
  uint8_t t2[] = {0x50};

  add_test_observer(&tr1.resource, &ep, t1, 1, "res1", 4);
  add_test_observer(&tr2.resource, &ep, t2, 1, "res2", 4);

  EXPECT_EQ(tr1.data.num_observers, 1);
  EXPECT_EQ(tr2.data.num_observers, 1);

  int removed = coap_remove_observer_by_resource(&tr1.resource);
  EXPECT_EQ(removed, 1);
  EXPECT_EQ(tr1.data.num_observers, 0);
  /* tr2 should be untouched */
  EXPECT_EQ(tr2.data.num_observers, 1);

  coap_free_all_observers();
}

TEST_F(CoapObserve, FreeAllObservers)
{
  TestResource tr("/test");
  oc_endpoint_t ep1 = make_endpoint(5683);
  oc_endpoint_t ep2 = make_endpoint(5684);
  uint8_t t1[] = {0x60};
  uint8_t t2[] = {0x70};

  add_test_observer(&tr.resource, &ep1, t1, 1, "test", 4);
  add_test_observer(&tr.resource, &ep2, t2, 1, "test", 4);
  EXPECT_EQ(tr.data.num_observers, 2);

  coap_free_all_observers();
  EXPECT_EQ(tr.data.num_observers, 0);
}

TEST_F(CoapObserve, MultipleObserversSameResource)
{
  TestResource tr("/test");
  oc_endpoint_t ep1 = make_endpoint(5683);
  oc_endpoint_t ep2 = make_endpoint(5684);
  oc_endpoint_t ep3 = make_endpoint(5685);
  uint8_t t1[] = {0x80};
  uint8_t t2[] = {0x90};
  uint8_t t3[] = {0xA0};

  add_test_observer(&tr.resource, &ep1, t1, 1, "test", 4);
  add_test_observer(&tr.resource, &ep2, t2, 1, "test", 4);
  add_test_observer(&tr.resource, &ep3, t3, 1, "test", 4);
  EXPECT_EQ(tr.data.num_observers, 3);

  /* Remove middle one */
  coap_remove_observer_by_client(&ep2);
  EXPECT_EQ(tr.data.num_observers, 2);

  /* Remove remaining */
  coap_free_all_observers();
  EXPECT_EQ(tr.data.num_observers, 0);
}

TEST_F(CoapObserve, DeregisterViaHandler)
{
  TestResource tr("/test");
  oc_endpoint_t ep = make_endpoint(5683);
  uint8_t token[] = {0xB0, 0xB1};

  add_test_observer(&tr.resource, &ep, token, 2, "test", 4);
  EXPECT_EQ(tr.data.num_observers, 1);

  /* Send deregister through coap_observe_handler */
  coap_packet_t req;
  memset(&req, 0, sizeof(req));
  req.code = COAP_GET;
  req.observe = OC_OBSERVE_DEREGISTER;
  SET_OPTION(&req, COAP_OPTION_OBSERVE);
  memcpy(req.token, token, 2);
  req.token_len = 2;
  req.uri_path = "test";
  req.uri_path_len = 4;

  coap_packet_t res;
  memset(&res, 0, sizeof(res));
  res.code = CONTENT_2_05;

  coap_observe_handler(&req, &res, &tr.resource, 0, &ep);

  EXPECT_EQ(tr.data.num_observers, 0);
}

TEST_F(CoapObserve, HandlerRejectsMissingObserveOptionAndBadCodes)
{
  TestResource tr("/test");
  oc_endpoint_t ep = make_endpoint(5683);
  coap_packet_t request;
  coap_packet_t response;
  memset(&request, 0, sizeof(request));
  memset(&response, 0, sizeof(response));
  request.code = COAP_GET;
  response.code = CONTENT_2_05;
  request.token_len = 1;
  request.token[0] = 1;
  request.uri_path = "test";
  request.uri_path_len = 4;
  EXPECT_EQ(coap_observe_handler(&request, &response, &tr.resource, 0, &ep), -1);

  SET_OPTION(&request, COAP_OPTION_OBSERVE);
  request.observe = OC_OBSERVE_REGISTER;
  response.code = BAD_REQUEST_4_00;
  EXPECT_EQ(coap_observe_handler(&request, &response, &tr.resource, 0, &ep), -1);
  EXPECT_EQ(tr.data.num_observers, 0);
}

TEST_F(CoapObserve, RemovalByMidNoMatchReturnsZero)
{
  oc_endpoint_t ep = make_endpoint(5683);
  EXPECT_EQ(coap_remove_observer_by_mid(&ep, 1234), 0);
}

TEST_F(CoapObserve, RegistrationRejectsMissingInvalidAndOverlongLifetime)
{
  TestResource tr("/test");
  oc_endpoint_t ep = make_endpoint(5683);
  uint8_t token[] = {0xC0};

  EXPECT_EQ(add_test_observer(&tr.resource, &ep, token, 1, "test", 4, ""), -2);
  EXPECT_EQ(tr.data.num_observers, 0);
  EXPECT_EQ(add_test_observer(&tr.resource, &ep, token, 1, "test", 4,
                              "lt=nope"), -2);
  EXPECT_EQ(tr.data.num_observers, 0);
  EXPECT_EQ(add_test_observer(&tr.resource, &ep, token, 1, "test", 4,
                              "lt=86401"), -2);
  EXPECT_EQ(tr.data.num_observers, 0);
}

TEST_F(CoapObserve, RegistrationAcceptsNonTrue)
{
  TestResource tr("/test");
  oc_endpoint_t ep = make_endpoint(5683);
  uint8_t token[] = {0xC1};

  EXPECT_EQ(add_test_observer(&tr.resource, &ep, token, 1, "test", 4,
                              "lt=60&non=true"), 0);
  EXPECT_EQ(tr.data.num_observers, 1);
  coap_free_all_observers();
}

TEST_F(CoapObserve, NotifyObserverCanFilterByEndpoint)
{
  TestResource tr("/test");
  tr.resource.get_handler.cb = observe_get_handler;
  oc_endpoint_t ep1 = make_endpoint(5683);
  oc_endpoint_t ep2 = make_endpoint(5684);
  uint8_t token[] = {0xC3};
  ASSERT_EQ(add_test_observer(&tr.resource, &ep1, token, 1, "test", 4), 0);

  EXPECT_EQ(coap_notify_observers(&tr.resource, nullptr, &ep2), 1);
  coap_free_all_observers();
}

TEST_F(CoapObserve, NotifyObserverBuildsAndSendsNotification)
{
  TestResource tr("/test");
  tr.resource.get_handler.cb = observe_get_handler;
  oc_endpoint_t ep = make_endpoint(5683);
  uint8_t token[] = {0xC4};
  ASSERT_EQ(add_test_observer(&tr.resource, &ep, token, 1, "test", 4), 0);

  EXPECT_EQ(coap_notify_observers(&tr.resource, nullptr, nullptr), 1);
  coap_free_all_observers();
}

/* ---------------- coap_notify_k_observers (KNX clause 2.5.9.1) ------------ */

TEST_F(CoapObserve, NotifyKObserversNullPayloadIsSafe)
{
  /* Guard: NULL payload must be a no-op, never dereference. */
  coap_notify_k_observers(nullptr, 16, 1);
  SUCCEED();
}

TEST_F(CoapObserve, NotifyKObserversZeroLengthIsSafe)
{
  /* Guard: zero-length payload must be a no-op. */
  const uint8_t payload[] = {0xA0};
  coap_notify_k_observers(payload, 0, 1);
  SUCCEED();
}

TEST_F(CoapObserve, NotifyKObserversNoObserversIsSafe)
{
  /* With no /k observers registered the iteration must complete without
   * sending or crashing. */
  const uint8_t payload[] = {0xBF, 0xFF}; /* indefinite-length CBOR map */
  coap_notify_k_observers(payload, sizeof(payload), 1);
  SUCCEED();
}

/*
  /k events only to subscribers whose access token covers the group address (spec 3.5.4.1, 2.6.9.3):
  - scope if.g.s (OC_ACL_G)       -> all group addresses
  - scope <ga> list (OC_ACL_GA)   -> only the listed group addresses
  - no token on the observer      -> nothing
*/
TEST_F(CoapObserve, NotifyKObserversOnlyToSubscribersWithTheGroupAddressInScope)
{
  uint32_t ga_1[] = {1};
  uint32_t ga_2[] = {2};

  oc_auth_at_t at_all;
  memset(&at_all, 0, sizeof(at_all));
  at_all.scope = OC_ACL_G;

  oc_auth_at_t at_ga1;
  memset(&at_ga1, 0, sizeof(at_ga1));
  at_ga1.scope = OC_ACL_GA;
  at_ga1.ga = ga_1;
  at_ga1.ga_len = 1;

  oc_auth_at_t at_ga2;
  memset(&at_ga2, 0, sizeof(at_ga2));
  at_ga2.scope = OC_ACL_GA;
  at_ga2.ga = ga_2;
  at_ga2.ga_len = 1;

  oc_endpoint_t ep_all = make_endpoint(5001);
  ep_all.access_token.inbound = &at_all;
  oc_endpoint_t ep_ga1 = make_endpoint(5002);
  ep_ga1.access_token.inbound = &at_ga1;
  oc_endpoint_t ep_ga2 = make_endpoint(5003);
  ep_ga2.access_token.inbound = &at_ga2;
  oc_endpoint_t ep_none = make_endpoint(5004);

  oc_resource_t *k = (oc_resource_t *)&core_resource_knx_k;
  const uint8_t t1[] = {0x01}, t2[] = {0x02}, t3[] = {0x03}, t4[] = {0x04};
  ASSERT_EQ(add_test_observer(k, &ep_all, t1, 1, "k", 1), 0);
  ASSERT_EQ(add_test_observer(k, &ep_ga1, t2, 1, "k", 1), 0);
  ASSERT_EQ(add_test_observer(k, &ep_ga2, t3, 1, "k", 1), 0);
  ASSERT_EQ(add_test_observer(k, &ep_none, t4, 1, "k", 1), 0);

  const uint8_t payload[] = {0xBF, 0x04, 0x01, 0xFF};
  coap_notify_k_observers(payload, sizeof(payload), 1);

  std::vector<uint16_t> sent = g_sent_ports;
  std::sort(sent.begin(), sent.end());
  EXPECT_EQ(sent, (std::vector<uint16_t>{5001, 5002}));

  coap_free_all_transactions();
}

/*
  coap_remove_observers_by_access_token removes every observer subscribed with that access token (any client, any
  port), and only those
*/
TEST_F(CoapObserve, RemoveObserversByAccessTokenRemovesOnlyThatTokensObservers)
{
  oc_auth_at_t at_a;
  memset(&at_a, 0, sizeof(at_a));
  oc_auth_at_t at_b;
  memset(&at_b, 0, sizeof(at_b));

  oc_endpoint_t ep_a1 = make_endpoint(5201);
  ep_a1.access_token.inbound = &at_a;
  oc_endpoint_t ep_a2 = make_endpoint(5202);
  ep_a2.access_token.inbound = &at_a;
  oc_endpoint_t ep_b = make_endpoint(5203);
  ep_b.access_token.inbound = &at_b;

  oc_resource_t *k = (oc_resource_t *)&core_resource_knx_k;
  const uint8_t t1[] = {0x11}, t2[] = {0x12}, t3[] = {0x13};
  ASSERT_EQ(add_test_observer(k, &ep_a1, t1, 1, "k", 1), 0);
  ASSERT_EQ(add_test_observer(k, &ep_a2, t2, 1, "k", 1), 0);
  ASSERT_EQ(add_test_observer(k, &ep_b, t3, 1, "k", 1), 0);
  ASSERT_EQ(k->runtime_data->num_observers, 3);

  EXPECT_EQ(coap_remove_observers_by_access_token(&at_a), 2);
  EXPECT_EQ(k->runtime_data->num_observers, 1);

  /* the remaining observer is the one of token B */
  uint32_t ga[] = {1};
  at_b.scope = OC_ACL_GA;
  at_b.ga = ga;
  at_b.ga_len = 1;
  const uint8_t payload[] = {0xBF, 0x04, 0x01, 0xFF};
  coap_notify_k_observers(payload, sizeof(payload), 1);
  EXPECT_EQ(g_sent_ports, (std::vector<uint16_t>{5203}));

  EXPECT_EQ(coap_remove_observers_by_access_token(nullptr), 0);
  coap_free_all_transactions();
}

/*
  Token rotation with slot reuse: token A (<ga> [1]) is deleted, token B (if.g.s) lands in the SAME table slot.
  The subscriber of A must not get /k events with B's rights - deleting A ended its subscription.
*/
TEST_F(CoapObserve, NewTokenInTheFreedSlotDoesNotReachTheOldSubscriber)
{
  oc_mmem_init();

  oc_auth_at_t *slot = oc_get_auth_at_entry(3);
  ASSERT_NE(slot, nullptr);
  oc_new_string(&slot->id, "tokA", 4);
  slot->scope = OC_ACL_GA;
  slot->ga = (uint32_t *)malloc(sizeof(uint32_t));
  ASSERT_NE(slot->ga, nullptr);
  slot->ga[0] = 1;
  slot->ga_len = 1;

  oc_endpoint_t ep = make_endpoint(5401);
  ep.access_token.inbound = slot;
  const uint8_t t[] = {0x41};
  ASSERT_EQ(add_test_observer((oc_resource_t *)&core_resource_knx_k, &ep, t, 1, "k", 1), 0);

  /* rotation: A deleted, B (all group addresses) configured into the same slot */
  ASSERT_EQ(oc_delete_at_table_entry(slot), 0);
  oc_free_string(&slot->id);
  oc_new_string(&slot->id, "tokB", 4);
  slot->scope = OC_ACL_G;

  const uint8_t payload[] = {0xBF, 0x04, 0x01, 0xFF};
  coap_notify_k_observers(payload, sizeof(payload), 1);
  EXPECT_TRUE(g_sent_ports.empty());

  /* free the slot's strings without re-creating them (LSan) */
  oc_free_string(&slot->id);
  oc_free_string(&slot->osc_id);
  oc_free_string(&slot->osc_ms);
  oc_free_string(&slot->osc_salt);
  oc_free_string(&slot->osc_contextid);
  slot->scope = OC_ACL_NONE;
  coap_free_all_transactions();
}

/* ---------------- coap_notify_observers (/p + core res.) ------------------ */

TEST_F(CoapObserve, NotifyObserversNullResourceReturnsZero)
{
  /* Guard: a NULL resource returns 0 (nothing notified). */
  EXPECT_EQ(coap_notify_observers(nullptr, nullptr, nullptr), 0);
}

TEST_F(CoapObserve, NotifyObserversNoObserversReturnsZero)
{
  /* A resource with zero observers short-circuits and returns 0 without
   * invoking the GET handler. */
  TestResource tr("/test");
  EXPECT_EQ(tr.data.num_observers, 0);
  EXPECT_EQ(coap_notify_observers(&tr.resource, nullptr, nullptr), 0);
}

/* ---------------- get_observe_counter ------------------------------------ */

TEST_F(CoapObserve, GetObserveCounterIsAtLeastFirstNotificationValue)
{
  /* The observe counter seeds at OC_OBSERVE_NOTIFICATIONS (3) and
   * only ever increases, so it is always >= that floor. */
  EXPECT_GE(get_observe_counter(), (uint32_t)OC_OBSERVE_NOTIFICATIONS);
}

TEST_F(CoapObserve, GetObserveCounterIsStableWithoutNotifications)
{
  /* Reading the counter has no side effects: two reads return the same value. */
  uint32_t a = get_observe_counter();
  uint32_t b = get_observe_counter();
  EXPECT_EQ(a, b);
}
