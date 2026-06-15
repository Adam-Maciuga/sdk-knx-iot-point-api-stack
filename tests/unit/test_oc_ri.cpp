/*
 * Unit tests for api/oc_ri.c and api/oc_knx.c — pure lookup functions
 *
 * Covers:
 *   oc_ri.c:  oc_status_code, oc_count_total_interfaces_in_mask,
 *             oc_count_total_scopes_in_mask, get_interface_string_full_urn,
 *             get_oc_status_code_from_coap_code,
 *             oc_ri_get_scope_mask, oc_put_all_access_scope_names_from_a_mask_in_string_array,
 *             oc_put_all_interface_short_urns_from_a_mask_in_string_array,
 *             oc_frame_interfaces_mask_in_response, oc_print_acl_scopes,
 *             oc_ri_new_request_from_inbound_request, oc_accept_header_is_ok,
 *             oc_ri_get_query_nth_key_value, oc_ri_get_query_value,
 *             oc_ri_query_nth_key_exists, oc_ri_query_exists,
 *             oc_ri_alloc_resource, oc_ri_alloc_resource_data
 *   oc_knx.c: oc_core_get_lsm_state_as_string, oc_core_get_lsm_event_as_string
 *
 * All are pure table lookups / bit-counting / string parsing / stack-struct
 * helpers with no bootstrapped device, network or running process required.
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "oc_ri.h"
#include "oc_knx.h"
#include "oc_helpers.h"
#include "oc_rep.h"
#include "oc_client_state.h"
#include "port/oc_random.h"
#include "messaging/coap/oc_coap.h"
#include "util/oc_mmem.h"

}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_status_code  (oc_status_t → CoAP code)
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(StatusCode, OK)
{
  /* OC_STATUS_OK → 2.05 Content = 69 (2*32 + 5) */
  EXPECT_EQ(oc_status_code(OC_STATUS_OK), CONTENT_2_05);
}

TEST(StatusCode, Created)
{
  EXPECT_EQ(oc_status_code(OC_STATUS_CREATED), CREATED_2_01);
}

TEST(StatusCode, Changed)
{
  EXPECT_EQ(oc_status_code(OC_STATUS_CHANGED), CHANGED_2_04);
}

TEST(StatusCode, Deleted)
{
  EXPECT_EQ(oc_status_code(OC_STATUS_DELETED), DELETED_2_02);
}

TEST(StatusCode, BadRequest)
{
  EXPECT_EQ(oc_status_code(OC_STATUS_BAD_REQUEST), BAD_REQUEST_4_00);
}

TEST(StatusCode, Unauthorized)
{
  EXPECT_EQ(oc_status_code(OC_STATUS_UNAUTHORIZED), UNAUTHORIZED_4_01);
}

TEST(StatusCode, NotFound)
{
  EXPECT_EQ(oc_status_code(OC_STATUS_NOT_FOUND), NOT_FOUND_4_04);
}

TEST(StatusCode, MethodNotAllowed)
{
  EXPECT_EQ(oc_status_code(OC_STATUS_METHOD_NOT_ALLOWED), METHOD_NOT_ALLOWED_4_05);
}

TEST(StatusCode, InternalServerError)
{
  EXPECT_EQ(oc_status_code(OC_STATUS_INTERNAL_SERVER_ERROR),
            INTERNAL_SERVER_ERROR_5_00);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_count_total_interfaces_in_mask
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(CountInterfaces, None)
{
  EXPECT_EQ(oc_count_total_interfaces_in_mask(OC_IF_NONE), 0u);
}

TEST(CountInterfaces, SingleBit)
{
  EXPECT_EQ(oc_count_total_interfaces_in_mask(OC_IF_I), 1u);
  EXPECT_EQ(oc_count_total_interfaces_in_mask(OC_IF_SEC), 1u);
}

TEST(CountInterfaces, MultipleBits)
{
  auto mask = (oc_interface_mask_t)(OC_IF_I | OC_IF_O | OC_IF_C);
  EXPECT_EQ(oc_count_total_interfaces_in_mask(mask), 3u);
}

TEST(CountInterfaces, AllBits)
{
  auto mask = (oc_interface_mask_t)(
    OC_IF_I | OC_IF_O | OC_IF_G | OC_IF_C | OC_IF_P |
    OC_IF_D | OC_IF_A | OC_IF_S | OC_IF_LI | OC_IF_B |
    OC_IF_SEC | OC_IF_SWU | OC_IF_PM | OC_IF_M);
  EXPECT_EQ(oc_count_total_interfaces_in_mask(mask), 14u);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_count_total_scopes_in_mask
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(CountScopes, None)
{
  EXPECT_EQ(oc_count_total_scopes_in_mask(OC_ACL_NONE), 0u);
}

TEST(CountScopes, SingleScope)
{
  EXPECT_EQ(oc_count_total_scopes_in_mask(OC_ACL_I), 1u);
}

TEST(CountScopes, MultipleScopes)
{
  auto mask = (oc_acl_mask_t)(OC_ACL_I | OC_ACL_SEC | OC_ACL_P);
  EXPECT_EQ(oc_count_total_scopes_in_mask(mask), 3u);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * get_interface_string_full_urn
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(InterfaceStringFullUrn, IndexI)
{
  /* OC_IF_I = 1 << 1 → index 1 → "urn:knx:if.i" */
  EXPECT_STREQ(get_interface_string_full_urn(1), "urn:knx:if.i");
}

TEST(InterfaceStringFullUrn, IndexO)
{
  EXPECT_STREQ(get_interface_string_full_urn(2), "urn:knx:if.o");
}

TEST(InterfaceStringFullUrn, IndexSec)
{
  EXPECT_STREQ(get_interface_string_full_urn(11), "urn:knx:if.sec");
}

TEST(InterfaceStringFullUrn, IndexNone)
{
  EXPECT_STREQ(get_interface_string_full_urn(0), "");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_core_get_lsm_state_as_string
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(LsmStateString, Unloaded)
{
  EXPECT_STREQ(oc_core_get_lsm_state_as_string(LSM_S_UNLOADED), "unloaded");
}

TEST(LsmStateString, Loaded)
{
  EXPECT_STREQ(oc_core_get_lsm_state_as_string(LSM_S_LOADED), "loaded");
}

TEST(LsmStateString, Loading)
{
  EXPECT_STREQ(oc_core_get_lsm_state_as_string(LSM_S_LOADING), "loading");
}

TEST(LsmStateString, Unloading)
{
  EXPECT_STREQ(oc_core_get_lsm_state_as_string(LSM_S_UNLOADING), "unloading");
}

TEST(LsmStateString, LoadCompleting)
{
  EXPECT_STREQ(oc_core_get_lsm_state_as_string(LSM_S_LOADCOMPLETING),
               "load completing");
}

TEST(LsmStateString, Unknown)
{
  EXPECT_STREQ(oc_core_get_lsm_state_as_string(LSM_S_ERROR), "");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_core_get_lsm_event_as_string
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(LsmEventString, Nop)
{
  EXPECT_STREQ(oc_core_get_lsm_event_as_string(LSM_E_NOP), "nop");
}

TEST(LsmEventString, StartLoading)
{
  EXPECT_STREQ(oc_core_get_lsm_event_as_string(LSM_E_STARTLOADING),
               "start loading");
}

TEST(LsmEventString, LoadComplete)
{
  EXPECT_STREQ(oc_core_get_lsm_event_as_string(LSM_E_LOADCOMPLETE),
               "load complete");
}

TEST(LsmEventString, Unload)
{
  EXPECT_STREQ(oc_core_get_lsm_event_as_string(LSM_E_UNLOAD), "unload");
}

TEST(LsmEventString, Unknown)
{
  EXPECT_STREQ(oc_core_get_lsm_event_as_string((oc_lsm_event_t)99), "");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * get_oc_status_code_from_coap_code  (inverse of oc_status_code)
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(StatusFromCoap, ContentMapsToOk)
{
  EXPECT_EQ(get_oc_status_code_from_coap_code(CONTENT_2_05), OC_STATUS_OK);
}

TEST(StatusFromCoap, ChangedMapsToChanged)
{
  EXPECT_EQ(get_oc_status_code_from_coap_code(CHANGED_2_04), OC_STATUS_CHANGED);
}

TEST(StatusFromCoap, NotFoundMapsToNotFound)
{
  EXPECT_EQ(get_oc_status_code_from_coap_code(NOT_FOUND_4_04),
            OC_STATUS_NOT_FOUND);
}

TEST(StatusFromCoap, UnknownCodeMapsToIgnore)
{
  EXPECT_EQ(get_oc_status_code_from_coap_code(0xABCD), OC_IGNORE);
}

TEST(StatusFromCoap, RoundTripsWithStatusCode)
{
  for (int s = OC_STATUS_OK; s < OC_STATUS_SERVICE_UNAVAILABLE; ++s) {
    int coap = oc_status_code((oc_status_t)s);
    EXPECT_EQ(get_oc_status_code_from_coap_code(coap), (oc_status_t)s);
  }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_ri_get_scope_mask  (scope name → mask bit)
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(ScopeMask, IfIIsBitOne)
{
  EXPECT_EQ(oc_ri_get_scope_mask("if.i", 4), (oc_acl_mask_t)(1 << 1));
}

TEST(ScopeMask, IfAIsBitSeven)
{
  EXPECT_EQ(oc_ri_get_scope_mask("if.a", 4), (oc_acl_mask_t)(1 << 7));
}

TEST(ScopeMask, IfSwuIsBitTwelve)
{
  EXPECT_EQ(oc_ri_get_scope_mask("if.swu", 6), (oc_acl_mask_t)(1 << 12));
}

TEST(ScopeMask, UnknownScopeIsNone)
{
  EXPECT_EQ(oc_ri_get_scope_mask("if.zz", 5), OC_ACL_NONE);
}

TEST(ScopeMask, LengthMismatchIsNone)
{
  // a too-short length must not partially match "if.swu"
  EXPECT_EQ(oc_ri_get_scope_mask("if.swu", 3), OC_ACL_NONE);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * mask → string-array expanders + framing  (need oc_mmem for string arrays)
 * ═══════════════════════════════════════════════════════════════════════════ */

class RiMask : public ::testing::Test {
protected:
  void SetUp() override { oc_mmem_init(); }
};

TEST_F(RiMask, ScopeNamesSingleBit)
{
  oc_string_array_t arr;
  oc_new_string_array(&arr, 4);
  // bit 1 -> "if.i"
  oc_put_all_access_scope_names_from_a_mask_in_string_array(
    (oc_acl_mask_t)(1 << 1), arr);
  // expander writes into preallocated slots; first slot holds "if.i"
  EXPECT_STREQ(oc_string_array_get_item(arr, 0), "if.i");
  oc_free_string_array(&arr);
}

TEST_F(RiMask, ScopeNamesMultipleBits)
{
  oc_string_array_t arr;
  oc_new_string_array(&arr, 4);
  // bits 1 and 7 -> "if.i", "if.a"
  oc_put_all_access_scope_names_from_a_mask_in_string_array(
    (oc_acl_mask_t)((1 << 1) | (1 << 7)), arr);
  EXPECT_STREQ(oc_string_array_get_item(arr, 0), "if.i");
  EXPECT_STREQ(oc_string_array_get_item(arr, 1), "if.a");
  oc_free_string_array(&arr);
}

TEST_F(RiMask, InterfaceShortUrnsSingleBit)
{
  oc_string_array_t arr;
  oc_new_string_array(&arr, 4);
  // bit 1 -> ":if.i"
  oc_put_all_interface_short_urns_from_a_mask_in_string_array(
    (oc_interface_mask_t)(1 << 1), arr);
  EXPECT_STREQ(oc_string_array_get_item(arr, 0), ":if.i");
  oc_free_string_array(&arr);
}

TEST_F(RiMask, FrameInterfacesTruncatedSingle)
{
  uint8_t buf[128];
  oc_rep_new(buf, sizeof(buf));
  int n = oc_frame_interfaces_mask_in_response((oc_interface_mask_t)(1 << 1),
                                               true);
  ASSERT_EQ(n, (int)strlen(":if.i"));
  EXPECT_EQ(0, memcmp(buf, ":if.i", n));
}

TEST_F(RiMask, FrameInterfacesFullSingle)
{
  uint8_t buf[128];
  oc_rep_new(buf, sizeof(buf));
  int n = oc_frame_interfaces_mask_in_response((oc_interface_mask_t)(1 << 1),
                                               false);
  ASSERT_EQ(n, (int)strlen("urn:knx:if.i"));
  EXPECT_EQ(0, memcmp(buf, "urn:knx:if.i", n));
}

TEST_F(RiMask, FrameInterfacesTruncatedMultipleSpaceSeparated)
{
  uint8_t buf[128];
  oc_rep_new(buf, sizeof(buf));
  // bits 1 and 2 -> ":if.i :if.o"
  int n = oc_frame_interfaces_mask_in_response(
    (oc_interface_mask_t)((1 << 1) | (1 << 2)), true);
  const char *expect = ":if.i :if.o";
  ASSERT_EQ(n, (int)strlen(expect));
  EXPECT_EQ(0, memcmp(buf, expect, n));
}

TEST_F(RiMask, FrameInterfacesNoneIsEmpty)
{
  uint8_t buf[128];
  oc_rep_new(buf, sizeof(buf));
  int n = oc_frame_interfaces_mask_in_response((oc_interface_mask_t)0, true);
  EXPECT_EQ(n, 0);
}

// oc_print_acl_scopes only prints (OC_PRINT) and returns void; just confirm it
// runs without crashing over a multi-bit mask.
TEST(PrintAclScopes, RunsWithoutCrash)
{
  oc_print_acl_scopes((oc_acl_mask_t)((1 << 1) | (1 << 12)));
  SUCCEED();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_ri_new_request_from_inbound_request
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(NewRequestFromInbound, RewiresResponseAndCopiesBuffer)
{
  uint8_t backing[16];

  oc_response_buffer_t in_rb;
  memset(&in_rb, 0, sizeof(in_rb));
  in_rb.buffer = backing;
  in_rb.buffer_size = sizeof(backing);

  oc_response_t in_resp;
  memset(&in_resp, 0, sizeof(in_resp));
  in_resp.response_buffer = &in_rb;

  oc_request_t inbound;
  memset(&inbound, 0, sizeof(inbound));
  inbound.response = &in_resp;

  oc_request_t new_request;
  oc_response_buffer_t new_rb;
  oc_response_t new_resp;
  memset(&new_rb, 0, sizeof(new_rb));
  memset(&new_resp, 0, sizeof(new_resp));

  oc_ri_new_request_from_inbound_request(&new_request, &inbound, &new_rb,
                                         &new_resp);

  EXPECT_EQ(new_request.response, &new_resp);
  EXPECT_EQ(new_resp.response_buffer, &new_rb);
  EXPECT_EQ(new_resp.separate_response, nullptr);
  EXPECT_EQ(new_rb.buffer, backing);
  EXPECT_EQ(new_rb.buffer_size, sizeof(backing));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_accept_header_is_ok
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(AcceptHeader, ExactMatchIsOk)
{
  oc_request_t req;
  memset(&req, 0, sizeof(req));
  req.accept = APPLICATION_CBOR;
  EXPECT_TRUE(oc_accept_header_is_ok(&req, APPLICATION_CBOR));
}

TEST(AcceptHeader, ContentNoneAcceptsAnything)
{
  oc_request_t req;
  memset(&req, 0, sizeof(req));
  req.accept = CONTENT_NONE;
  EXPECT_TRUE(oc_accept_header_is_ok(&req, APPLICATION_CBOR));
}

TEST(AcceptHeader, NullRequestIsNotOk)
{
  // request NULL is handled inside the bad-request preparation path
  EXPECT_FALSE(oc_accept_header_is_ok(nullptr, APPLICATION_CBOR));
}

TEST(AcceptHeader, MismatchIsBadRequest)
{
  uint8_t backing[64];
  oc_response_buffer_t rb;
  memset(&rb, 0, sizeof(rb));
  rb.buffer = backing;
  rb.buffer_size = sizeof(backing);
  oc_response_t resp;
  memset(&resp, 0, sizeof(resp));
  resp.response_buffer = &rb;
  oc_request_t req;
  memset(&req, 0, sizeof(req));
  req.response = &resp;
  req.accept = APPLICATION_LINK_FORMAT;

  EXPECT_FALSE(oc_accept_header_is_ok(&req, APPLICATION_CBOR));
  EXPECT_EQ(rb.code, oc_status_code(OC_STATUS_BAD_REQUEST));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * query parsers
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(QueryValue, FirstKey)
{
  const char *q = "a=1&b=22&c=333";
  char *v = nullptr;
  int len = oc_ri_get_query_value(q, strlen(q), "a", &v);
  EXPECT_EQ(len, 1);
  ASSERT_NE(v, nullptr);
  EXPECT_EQ(0, strncmp(v, "1", 1));
}

TEST(QueryValue, MiddleKey)
{
  const char *q = "a=1&b=22&c=333";
  char *v = nullptr;
  int len = oc_ri_get_query_value(q, strlen(q), "b", &v);
  EXPECT_EQ(len, 2);
  ASSERT_NE(v, nullptr);
  EXPECT_EQ(0, strncmp(v, "22", 2));
}

TEST(QueryValue, LastKey)
{
  const char *q = "a=1&b=22&c=333";
  char *v = nullptr;
  int len = oc_ri_get_query_value(q, strlen(q), "c", &v);
  EXPECT_EQ(len, 3);
  ASSERT_NE(v, nullptr);
  EXPECT_EQ(0, strncmp(v, "333", 3));
}

TEST(QueryValue, CaseInsensitiveKey)
{
  const char *q = "a=1&Bee=22";
  char *v = nullptr;
  int len = oc_ri_get_query_value(q, strlen(q), "bee", &v);
  EXPECT_EQ(len, 2);
}

TEST(QueryValue, MissingKeyReturnsMinusOne)
{
  const char *q = "a=1&b=22";
  char *v = nullptr;
  EXPECT_EQ(oc_ri_get_query_value(q, strlen(q), "z", &v), -1);
}

TEST(QueryExists, KeyValuePairExists)
{
  const char *q = "a=1&b=22&c=333";
  EXPECT_EQ(oc_ri_query_exists(q, strlen(q), "b"), 1);
}

TEST(QueryExists, KeyOnlyFragmentExists)
{
  const char *q = "flag&a=1";
  EXPECT_EQ(oc_ri_query_exists(q, strlen(q), "flag"), 1);
}

TEST(QueryExists, MissingKeyReturnsMinusOne)
{
  const char *q = "a=1&b=22";
  EXPECT_EQ(oc_ri_query_exists(q, strlen(q), "z"), -1);
}

TEST(QueryNthKeyValue, SecondFragment)
{
  const char *q = "a=1&b=22&c=333";
  char *k = nullptr, *v = nullptr;
  size_t kl = 0, vl = 0;
  int next = oc_ri_get_query_nth_key_value(q, strlen(q), &k, &kl, &v, &vl, 2);
  EXPECT_GT(next, 0);
  ASSERT_EQ(kl, 1U);
  EXPECT_EQ(0, strncmp(k, "b", 1));
  ASSERT_EQ(vl, 2U);
  EXPECT_EQ(0, strncmp(v, "22", 2));
}

TEST(QueryNthKeyExists, ThirdFragmentKey)
{
  const char *q = "a=1&b=22&c=333";
  char *k = nullptr;
  size_t kl = 0;
  int next = oc_ri_query_nth_key_exists(q, strlen(q), &k, &kl, 3);
  EXPECT_GT(next, 0);
  ASSERT_EQ(kl, 1U);
  EXPECT_EQ(0, strncmp(k, "c", 1));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * resource allocators
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(RiAlloc, ResourceIsZeroed)
{
  oc_resource_t *r = oc_ri_alloc_resource();
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->next, nullptr);
  EXPECT_EQ(oc_string_len(r->uri), 0U);
  free(r);
}

TEST(RiAlloc, ResourceDataIsZeroed)
{
  oc_resource_data_t *d = oc_ri_alloc_resource_data();
  ASSERT_NE(d, nullptr);
  // freshly calloc'd: every byte zero
  const uint8_t *p = reinterpret_cast<const uint8_t *>(d);
  bool all_zero = true;
  for (size_t i = 0; i < sizeof(oc_resource_data_t); ++i) {
    if (p[i] != 0) {
      all_zero = false;
      break;
    }
  }
  EXPECT_TRUE(all_zero);
  free(d);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_ri_add_resource — validation gate for application resources
 * ═══════════════════════════════════════════════════════════════════════════ */

static void ri_dummy_handler(oc_request_t *, oc_interface_mask_t, void *) {}

TEST(RiAddResource, NullResourceReturnsFalse)
{
  EXPECT_FALSE(oc_ri_add_resource(nullptr));
}

TEST(RiAddResource, ConstResourceReturnsFalse)
{
  oc_resource_t *r = oc_ri_alloc_resource();
  ASSERT_NE(r, nullptr);
  r->is_const = true;
  r->get_handler.cb = ri_dummy_handler;
  EXPECT_FALSE(oc_ri_add_resource(r));
  free(r);
}

TEST(RiAddResource, NoHandlerReturnsFalse)
{
  oc_resource_t *r = oc_ri_alloc_resource();
  ASSERT_NE(r, nullptr);
  /* no get/put/post/delete handler set */
  EXPECT_FALSE(oc_ri_add_resource(r));
  free(r);
}

TEST(RiAddResource, PeriodicWithZeroPeriodReturnsFalse)
{
  oc_resource_t *r = oc_ri_alloc_resource();
  ASSERT_NE(r, nullptr);
  r->get_handler.cb = ri_dummy_handler;
  r->properties = OC_PERIODIC;
  r->observe_period_seconds = 0;
  EXPECT_FALSE(oc_ri_add_resource(r));
  free(r);
}

TEST(RiAddResource, ValidResourceIsAdded)
{
  oc_resource_t *r = oc_ri_alloc_resource();
  ASSERT_NE(r, nullptr);
  r->get_handler.cb = ri_dummy_handler;
  EXPECT_TRUE(oc_ri_add_resource(r));
  /* delete removes it from the list and frees it */
  EXPECT_TRUE(oc_ri_delete_resource(r));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_ri_delete_resource
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(RiDeleteResource, NullReturnsFalse)
{
  EXPECT_FALSE(oc_ri_delete_resource(nullptr));
}

TEST(RiDeleteResource, ConstReturnsFalse)
{
  oc_resource_t *r = oc_ri_alloc_resource();
  ASSERT_NE(r, nullptr);
  r->is_const = true;
  EXPECT_FALSE(oc_ri_delete_resource(r));
  free(r);
}

TEST(RiDeleteResource, RemovesAndFreesAddedResource)
{
  oc_resource_t *r = oc_ri_alloc_resource();
  ASSERT_NE(r, nullptr);
  r->put_handler.cb = ri_dummy_handler;
  ASSERT_TRUE(oc_ri_add_resource(r));
  /* deleting an added, non-const resource succeeds (and frees it) */
  EXPECT_TRUE(oc_ri_delete_resource(r));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * client callback list: alloc / find_by_mid / find_by_token / valid / get
 * ═══════════════════════════════════════════════════════════════════════════ */

class RiClientCb : public ::testing::Test {
protected:
  oc_endpoint_t ep{};
  void SetUp() override
  {
    oc_mmem_init();
    oc_random_init();
    memset(&ep, 0, sizeof(ep));
    ep.flags = IPV6;
    ep.addr.ipv6.port = 5683;
    ep.addr.ipv6.address[15] = 1; /* ::1 */
  }
  void TearDown() override
  {
    /* frees every cb registered on this endpoint (ref_count 0, not mc/discovery) */
    oc_ri_free_client_cbs_by_endpoint(&ep);
    oc_random_destroy();
  }
  oc_client_cb_t *make_cb(const char *uri, coap_method_t method)
  {
    oc_client_handler_t handler{};
    return oc_ri_alloc_client_cb(uri, &ep, method, nullptr, handler,
                                 HIGH_QOS, nullptr);
  }
};

TEST_F(RiClientCb, FindByMidReturnsMatch)
{
  oc_client_cb_t *cb = make_cb("/a", COAP_GET);
  ASSERT_NE(cb, nullptr);
  EXPECT_EQ(oc_ri_find_client_cb_by_mid(cb->mid), cb);
}

TEST_F(RiClientCb, FindByMidReturnsNullWhenAbsent)
{
  oc_client_cb_t *cb = make_cb("/a", COAP_GET);
  ASSERT_NE(cb, nullptr);
  /* a MID that cannot equal the allocated one */
  EXPECT_EQ(oc_ri_find_client_cb_by_mid((uint16_t)(cb->mid + 1)), nullptr);
}

TEST_F(RiClientCb, FindByTokenReturnsMatch)
{
  oc_client_cb_t *cb = make_cb("/b", COAP_POST);
  ASSERT_NE(cb, nullptr);
  EXPECT_EQ(oc_ri_find_client_cb_by_token(cb->token, cb->token_len), cb);
}

TEST_F(RiClientCb, FindByTokenReturnsNullWhenAbsent)
{
  oc_client_cb_t *cb = make_cb("/b", COAP_POST);
  ASSERT_NE(cb, nullptr);
  uint8_t bogus[COAP_TOKEN_LEN];
  memcpy(bogus, cb->token, cb->token_len);
  bogus[0] ^= 0xFF; /* guarantee a mismatch */
  EXPECT_EQ(oc_ri_find_client_cb_by_token(bogus, cb->token_len), nullptr);
}

TEST_F(RiClientCb, IsClientCbValidTrueForRegistered)
{
  oc_client_cb_t *cb = make_cb("/c", COAP_GET);
  ASSERT_NE(cb, nullptr);
  EXPECT_TRUE(oc_ri_is_client_cb_valid(cb));
}

TEST_F(RiClientCb, IsClientCbValidFalseForUnregistered)
{
  oc_client_cb_t local{};
  EXPECT_FALSE(oc_ri_is_client_cb_valid(&local));
}

TEST_F(RiClientCb, GetClientCbMatchesUriEndpointMethod)
{
  oc_client_cb_t *cb = make_cb("/d", COAP_PUT);
  ASSERT_NE(cb, nullptr);
  EXPECT_EQ(oc_ri_get_client_cb("/d", &ep, COAP_PUT), cb);
}

TEST_F(RiClientCb, GetClientCbReturnsNullForWrongMethod)
{
  oc_client_cb_t *cb = make_cb("/d", COAP_PUT);
  ASSERT_NE(cb, nullptr);
  EXPECT_EQ(oc_ri_get_client_cb("/d", &ep, COAP_GET), nullptr);
}

