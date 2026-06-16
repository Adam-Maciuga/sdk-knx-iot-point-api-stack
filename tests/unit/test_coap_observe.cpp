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

extern "C" {
#include "messaging/coap/observe.h"
#include "messaging/coap/coap.h"
#include "messaging/coap/constants.h"
#include "port/oc_network_events_mutex.h"
#include "oc_ri.h"
#include <string.h>

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
                             const char *uri, size_t uri_len)
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
  req.uri_query = "lt=86400";
  req.uri_query_len = 8;
  SET_OPTION(&req, COAP_OPTION_URI_QUERY);

  coap_packet_t res;
  memset(&res, 0, sizeof(res));
  res.code = CONTENT_2_05;

#ifdef OC_BLOCK_WISE
  return coap_observe_handler(&req, &res, resource, 0, ep);
#else
  return coap_observe_handler(&req, &res, resource, ep);
#endif
}

/* ---------------- fixture ------------------------------------------------ */

class CoapObserve : public ::testing::Test {
protected:
  void SetUp() override {
    oc_network_event_handler_mutex_init();
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

#ifdef OC_BLOCK_WISE
  coap_observe_handler(&req, &res, &tr.resource, 0, &ep);
#else
  coap_observe_handler(&req, &res, &tr.resource, &ep);
#endif

  EXPECT_EQ(tr.data.num_observers, 0);
}

/* ---------------- coap_notify_k_observers (KNX clause 2.5.9.1) ------------ */

TEST_F(CoapObserve, NotifyKObserversNullPayloadIsSafe)
{
  /* Guard: NULL payload must be a no-op, never dereference. */
  coap_notify_k_observers(nullptr, 16);
  SUCCEED();
}

TEST_F(CoapObserve, NotifyKObserversZeroLengthIsSafe)
{
  /* Guard: zero-length payload must be a no-op. */
  const uint8_t payload[] = {0xA0};
  coap_notify_k_observers(payload, 0);
  SUCCEED();
}

TEST_F(CoapObserve, NotifyKObserversNoObserversIsSafe)
{
  /* With no /k observers registered the iteration must complete without
   * sending or crashing. */
  const uint8_t payload[] = {0xBF, 0xFF}; /* indefinite-length CBOR map */
  coap_notify_k_observers(payload, sizeof(payload));
  SUCCEED();
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
