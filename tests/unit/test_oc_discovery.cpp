/*
 * Unit tests for the CoAP/KNX discovery helpers (api/oc_discovery.c,
 * declared in include/oc_discovery.h + oc_client_state.h).
 *
 * Covers the unit-testable public helpers:
 *   oc_add_resource_to_response_payload — frame one resource as an RFC-6690
 *                                         link-format record (<uri>;rt=..;if=..;ct=..)
 *   oc_check_request_from_resource      — discoverability + rt/if query filter +
 *                                         paging skip, then frame the record
  oc_check_request_from_index         — same, but resolve the resource from the
 *                                         core-resource table by index
 *
 * Requirements:
 *   - oc_mmem_init() for the oc_string_array_t resource types.
 *   - The framing helpers write through the global rep encoder, so the fixture
 *     calls oc_rep_new(buf, size) before each test; the written bytes are read
 *     back from buf for the first *response_length bytes.
 *
 * INTEGRATION (documented, not unit-tested here):
 *   - oc_well_known_core_discovery_handler (the GET handler on
 *     core_resource_well_known_core) and the static helpers it drives
 *     (oc_process_core_resources, oc_process_application_resources, frame_sn)
 *     need a fully bootstrapped device: the core-resource table, the app
 *     resource list, a stored serial number / internal address / installation
 *     id, and MTU/paging state. They are exercised by the runtime conformance
 *     suite (.well-known/core discovery) rather than as unit tests.
 *   - oc_check_request_from_index's match path needs the core-resource table
 *     populated (oc_core_get_core_resource_by_index); only its NULL/guard path
 *     is unit-tested here.
 */

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>

extern "C" {
#include "oc_api.h"
#include "oc_discovery.h"
#include "oc_client_state.h"
#include "oc_ri.h"
#include "oc_rep.h"
#include "oc_helpers.h"
#include "oc_endpoint.h"
#include "util/oc_mmem.h"
}

/* ───────────────────────────────── fixture ───────────────────────────────── */

class DiscoveryBase : public ::testing::Test {
protected:
  uint8_t buf[2048];

  void SetUp() override {
    oc_mmem_init();
    memset(buf, 0, sizeof(buf));
    oc_rep_new(buf, sizeof(buf));
  }

  /* current encoded content as a std::string of the first n bytes */
  std::string framed(size_t n) {
    return std::string(reinterpret_cast<const char *>(buf), n);
  }
};

/* ────────────────────── oc_add_resource_to_response_payload ───────────────── */

TEST_F(DiscoveryBase, AddPayloadNullResourceReturnsFalse)
{
  size_t len = 0;
  EXPECT_FALSE(oc_add_resource_to_response_payload(nullptr, &len, false));
  EXPECT_EQ(len, 0u);
}

TEST_F(DiscoveryBase, AddPayloadEmptyUriReturnsFalse)
{
  oc_resource_t r;
  memset(&r, 0, sizeof(r));
  size_t len = 0;
  EXPECT_FALSE(oc_add_resource_to_response_payload(&r, &len, false));
  EXPECT_EQ(len, 0u);
}

TEST_F(DiscoveryBase, AddPayloadUriOnlyFramesAngleBrackets)
{
  oc_resource_t r;
  memset(&r, 0, sizeof(r));
  oc_new_string(&r.uri, "/p/0", strlen("/p/0"));

  size_t len = 0;
  EXPECT_TRUE(oc_add_resource_to_response_payload(&r, &len, false));
  EXPECT_GT(len, 0u);
  EXPECT_NE(framed(len).find("</p/0>;"), std::string::npos);

  oc_free_string(&r.uri);
}

TEST_F(DiscoveryBase, AddPayloadInsertsLeadingCommaWhenBufferNonEmpty)
{
  oc_resource_t r;
  memset(&r, 0, sizeof(r));
  oc_new_string(&r.uri, "/p/1", strlen("/p/1"));

  size_t len = 5; /* pretend a previous record was already written */
  EXPECT_TRUE(oc_add_resource_to_response_payload(&r, &len, false));
  /* the very first byte written in this call must be the separator comma */
  EXPECT_EQ(buf[0], ',');

  oc_free_string(&r.uri);
}

TEST_F(DiscoveryBase, AddPayloadFramesResourceTypeUnTruncated)
{
  oc_resource_t r;
  memset(&r, 0, sizeof(r));
  oc_new_string(&r.uri, "/p/2", strlen("/p/2"));
  oc_new_string_array(&r.types, 1);
  oc_string_array_add_item(r.types, "urn:knx:dpa.353");

  size_t len = 0;
  EXPECT_TRUE(oc_add_resource_to_response_payload(&r, &len, false));
  std::string s = framed(len);
  EXPECT_NE(s.find("rt=\"urn:knx:dpa.353\""), std::string::npos);

  oc_free_string(&r.uri);
  oc_free_string_array(&r.types);
}

TEST_F(DiscoveryBase, AddPayloadTruncateStripsUrnKnxPrefix)
{
  oc_resource_t r;
  memset(&r, 0, sizeof(r));
  oc_new_string(&r.uri, "/p/3", strlen("/p/3"));
  oc_new_string_array(&r.types, 1);
  oc_string_array_add_item(r.types, "urn:knx:dpa.353");

  size_t len = 0;
  EXPECT_TRUE(oc_add_resource_to_response_payload(&r, &len, true));
  std::string s = framed(len);
  /* "urn:knx" (7 chars) stripped -> ":dpa.353" remains */
  EXPECT_NE(s.find("rt=\":dpa.353\""), std::string::npos);
  EXPECT_EQ(s.find("urn:knx"), std::string::npos);

  oc_free_string(&r.uri);
  oc_free_string_array(&r.types);
}

TEST_F(DiscoveryBase, AddPayloadFramesSingleContentType)
{
  oc_resource_t r;
  memset(&r, 0, sizeof(r));
  oc_new_string(&r.uri, "/p/4", strlen("/p/4"));
  r.content_type[0] = APPLICATION_CBOR;
  r.content_type[1] = CONTENT_NONE;

  size_t len = 0;
  EXPECT_TRUE(oc_add_resource_to_response_payload(&r, &len, false));
  std::string s = framed(len);
  /* single content type is framed without quotes: ct=<n> */
  EXPECT_NE(s.find("ct="), std::string::npos);
  EXPECT_EQ(s.find("ct=\""), std::string::npos);

  oc_free_string(&r.uri);
}

TEST_F(DiscoveryBase, AddPayloadFramesDoubleContentType)
{
  oc_resource_t r;
  memset(&r, 0, sizeof(r));
  oc_new_string(&r.uri, "/p/5", strlen("/p/5"));
  r.content_type[0] = APPLICATION_CBOR;
  r.content_type[1] = APPLICATION_LINK_FORMAT;

  size_t len = 0;
  EXPECT_TRUE(oc_add_resource_to_response_payload(&r, &len, false));
  std::string s = framed(len);
  /* two content types are framed inside quotes: ct="<n> <m>" */
  EXPECT_NE(s.find("ct=\""), std::string::npos);

  oc_free_string(&r.uri);
}

/* ────────────────────────── oc_check_request_from_resource ────────────────── */

static void
set_query(oc_request_t *req, const char *q)
{
  req->query = (char *)q;
  req->query_len = q ? strlen(q) : 0;
}

TEST_F(DiscoveryBase, CheckRequestNullResourceReturnsFalse)
{
  oc_request_t req;
  memset(&req, 0, sizeof(req));
  size_t len = 0;
  int skipped = 0;
  EXPECT_FALSE(oc_check_request_from_resource(nullptr, &req, &len, &skipped, 0, false));
}

TEST_F(DiscoveryBase, CheckRequestNonDiscoverableReturnsFalse)
{
  oc_resource_t r;
  memset(&r, 0, sizeof(r));
  oc_new_string(&r.uri, "/p/6", strlen("/p/6"));
  r.properties = (oc_resource_properties_t)0; /* not OC_DISCOVERABLE */

  oc_request_t req;
  memset(&req, 0, sizeof(req));
  set_query(&req, nullptr);

  size_t len = 0;
  int skipped = 0;
  EXPECT_FALSE(oc_check_request_from_resource(&r, &req, &len, &skipped, 0, false));

  oc_free_string(&r.uri);
}

TEST_F(DiscoveryBase, CheckRequestDiscoverableNoQueryFrames)
{
  oc_resource_t r;
  memset(&r, 0, sizeof(r));
  oc_new_string(&r.uri, "/p/7", strlen("/p/7"));
  r.properties = OC_DISCOVERABLE;

  oc_request_t req;
  memset(&req, 0, sizeof(req));
  set_query(&req, nullptr);

  size_t len = 0;
  int skipped = 0;
  EXPECT_TRUE(oc_check_request_from_resource(&r, &req, &len, &skipped, 0, false));
  EXPECT_GT(len, 0u);
  EXPECT_NE(framed(len).find("</p/7>;"), std::string::npos);

  oc_free_string(&r.uri);
}

TEST_F(DiscoveryBase, CheckRequestRtMismatchReturnsFalse)
{
  oc_resource_t r;
  memset(&r, 0, sizeof(r));
  oc_new_string(&r.uri, "/p/8", strlen("/p/8"));
  r.properties = OC_DISCOVERABLE;
  oc_new_string_array(&r.types, 1);
  oc_string_array_add_item(r.types, "urn:knx:dpa.353");

  oc_request_t req;
  memset(&req, 0, sizeof(req));
  set_query(&req, "rt=urn:knx:dpa.999");

  size_t len = 0;
  int skipped = 0;
  EXPECT_FALSE(oc_check_request_from_resource(&r, &req, &len, &skipped, 0, false));

  oc_free_string(&r.uri);
  oc_free_string_array(&r.types);
}

TEST_F(DiscoveryBase, CheckRequestRtMatchReturnsTrue)
{
  oc_resource_t r;
  memset(&r, 0, sizeof(r));
  oc_new_string(&r.uri, "/p/9", strlen("/p/9"));
  r.properties = OC_DISCOVERABLE;
  oc_new_string_array(&r.types, 1);
  oc_string_array_add_item(r.types, "urn:knx:dpa.353");

  oc_request_t req;
  memset(&req, 0, sizeof(req));
  set_query(&req, "rt=urn:knx:dpa.353");

  size_t len = 0;
  int skipped = 0;
  EXPECT_TRUE(oc_check_request_from_resource(&r, &req, &len, &skipped, 0, false));
  EXPECT_GT(len, 0u);

  oc_free_string(&r.uri);
  oc_free_string_array(&r.types);
}

TEST_F(DiscoveryBase, CheckRequestSkipsEntriesBelowFirstEntry)
{
  oc_resource_t r;
  memset(&r, 0, sizeof(r));
  oc_new_string(&r.uri, "/p/a", strlen("/p/a"));
  r.properties = OC_DISCOVERABLE;

  oc_request_t req;
  memset(&req, 0, sizeof(req));
  set_query(&req, nullptr);

  size_t len = 0;
  int skipped = 0;
  /* first_entry = 2: this match is below the page start, must be skipped */
  EXPECT_FALSE(oc_check_request_from_resource(&r, &req, &len, &skipped, 2, false));
  EXPECT_EQ(skipped, 1);
  EXPECT_EQ(len, 0u);

  oc_free_string(&r.uri);
}

/* ──────────────────────────── oc_check_request_from_index ─────────────────── */

TEST_F(DiscoveryBase, CheckRequestFromIndexUninitializedCoreReturnsFalse)
{
  oc_request_t req;
  memset(&req, 0, sizeof(req));
  set_query(&req, nullptr);

  size_t len = 0;
  int skipped = 0;
  /* with no device bootstrapped the core table is empty, so the resolved
   * resource is NULL and the call must safely return false. */
  EXPECT_FALSE(oc_check_request_from_index(9999, &req, &len, &skipped, 0, false));
}
