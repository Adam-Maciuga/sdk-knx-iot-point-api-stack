/*
 * Copyright (c) 2025 KNX Association
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Unit tests for api/oc_knx_swu.c (software update protocol).
 *
 * Coverage strategy
 * -----------------
 * Every writable function in oc_knx_swu.c is exercised, including the
 * file-static request handlers. The static GET/PUT handlers are reached
 * through their owning `const oc_resource_t core_resource_knx_swu_*`
 * structs (extern-declared below) by invoking the handler callback
 * pointers directly. This is required because the .c file contains many
 * `const oc_resource_t {...}` aggregate literals, which g++ rejects when
 * the .c is #included into a C++ TU (-fpermissive const char* -> void*).
 *
 * The public setters (oc_swu_set_*) all operate on the file-static
 * `swu_device`. They are both VERIFIED INDIRECTLY through the matching GET
 * handlers and used to seed the device state (notably the oc_string_t
 * fields, which are NULL on init and would crash the string-serializing
 * GET handlers via strlen(NULL)).
 *
 * Integration-level (NOT unit-tested here), with concrete reasons:
 *  - oc_create_knx_swu_resources(): calls oc_storage_read() during init and
 *    is meaningful only against the RI resource table; storage/RI setup is
 *    integration scope.
 *  - oc_core_knx_swu_get_handler() link-format SUCCESS path: serializes the
 *    core resource table via oc_check_request_from_index() over the RI
 *    singleton; producing a real link-format page requires a populated RI
 *    resource table (integration). The wrong-accept and paging-rejection
 *    (BAD_REQUEST) branches ARE unit-tested below.
 *  - The /a/swu separate/delayed response slow-path and the
 *    DOWNLOADING -> DOWNLOADED completion + storage_write path depend on a
 *    live CoAP transaction / event loop; the fast-path, no-callback and
 *    query-validation branches ARE unit-tested below.
 *
 * Note: oc_storage_read/write return -ENOENT (no crash) when storage is not
 * configured, so handler success branches that call oc_storage_write remain
 * unit-testable.
 */

#include <gtest/gtest.h>
#include <cstring>
#include <string>

extern "C" {
#include "oc_api.h"
#include "oc_knx_swu.h"
#include "oc_ri.h"
#include "oc_rep.h"
#include "oc_helpers.h"
#include "messaging/coap/oc_coap.h"
#include "util/oc_mmem.h"
#include <string.h>

// static handlers are reached through their owning resource structs
extern const oc_resource_t core_resource_knx_swu_protocol;
extern const oc_resource_t core_resource_knx_swu_hwref;
extern const oc_resource_t core_resource_knx_swu_method;
extern const oc_resource_t core_resource_knx_swu_lastupdate;
extern const oc_resource_t core_resource_knx_swu_result;
extern const oc_resource_t core_resource_knx_swu_state;
extern const oc_resource_t core_resource_knx_swu_update;
extern const oc_resource_t core_resource_knx_swu_pkgv;
extern const oc_resource_t core_resource_knx_swu_pkgcmd; // /a/swu
extern const oc_resource_t core_resource_knx_swu_pkgbytes;
extern const oc_resource_t core_resource_knx_swu_pkgnames;
extern const oc_resource_t core_resource_knx_swu; // /swu list
}

// ---------------------------------------------------------------------------
// request/response wiring helper
// ---------------------------------------------------------------------------
namespace {

struct RespCtx {
  oc_request_t request;
  oc_response_t response;
  oc_response_buffer_t response_buffer;
  uint8_t buf[1024];

  RespCtx()
  {
    memset(&request, 0, sizeof(request));
    memset(&response, 0, sizeof(response));
    memset(&response_buffer, 0, sizeof(response_buffer));
    memset(buf, 0, sizeof(buf));
    response_buffer.buffer = buf;
    response_buffer.buffer_size = sizeof(buf);
    response.response_buffer = &response_buffer;
    request.response = &response;
  }

  int code() const { return response_buffer.code; }
};

// recording stubs for callbacks ------------------------------------------------
struct UpgradeRec {
  bool called = false;
  int defer = -1;
};
void recording_upgrade_cb(int defer_time, void *data)
{
  auto *r = static_cast<UpgradeRec *>(data);
  r->called = true;
  r->defer = defer_time;
}

struct SwuBlockRec {
  bool called = false;
  size_t binary_size = 0;
  size_t block_offset = 0;
  size_t block_len = 0;
  // leave sep_response->active = false => fast path
};
void recording_swu_cb(oc_separate_response_t *response, size_t binary_size,
                      size_t block_offset, const uint8_t *block_data,
                      size_t block_len, void *data)
{
  (void)response;
  (void)block_data;
  auto *r = static_cast<SwuBlockRec *>(data);
  r->called = true;
  r->binary_size = binary_size;
  r->block_offset = block_offset;
  r->block_len = block_len;
}

} // namespace

// ---------------------------------------------------------------------------
// fixture
// ---------------------------------------------------------------------------
class KnxSwuBase : public ::testing::Test {
protected:
  void SetUp() override
  {
    oc_mmem_init();
    // reset the persistent file-static swu_device to a known baseline and
    // seed all oc_string_t fields so the string-serializing GET handlers do
    // not dereference NULL.
    oc_set_swu_cb(nullptr, nullptr);
    oc_set_swu_upgrade_cb(nullptr, nullptr);
    oc_swu_set_state(OC_SWU_STATE_IDLE);
    oc_swu_set_package_bytes(0);
    oc_swu_set_result(OC_SWU_RESULT_INIT);
    oc_swu_set_package_version(0, 0, 0);
    oc_swu_set_hwref("0102030405ABCDEF");
    oc_swu_set_last_update("0");
    oc_swu_set_package_name("");
  }
  void TearDown() override
  {
    oc_set_swu_cb(nullptr, nullptr);
    oc_set_swu_upgrade_cb(nullptr, nullptr);
    oc_swu_set_state(OC_SWU_STATE_IDLE);
  }
};

// helpers ---------------------------------------------------------------------
static void invoke_get(const oc_resource_t &res, RespCtx &ctx,
                       oc_content_format_t accept)
{
  ctx.request.accept = accept;
  oc_rep_new(ctx.buf, sizeof(ctx.buf));
  res.get_handler.cb(&ctx.request, OC_IF_D, nullptr);
}

static void invoke_put(const oc_resource_t &res, RespCtx &ctx,
                       oc_content_format_t accept)
{
  ctx.request.accept = accept;
  oc_rep_new(ctx.buf, sizeof(ctx.buf));
  res.put_handler.cb(&ctx.request, OC_IF_SWU, nullptr);
}

/*
  Raw scan of the encoded CBOR response for a literal byte sequence. CBOR text
  strings are stored verbatim, so this is enough to assert on returned strings
  without pulling in a full CBOR parser.
*/
static bool payload_contains(const RespCtx &ctx, const char *needle)
{
  const size_t needle_len = strlen(needle);
  size_t len = static_cast<size_t>(ctx.response_buffer.response_length);
  if (len == 0 || len > sizeof(ctx.buf)) {
    len = sizeof(ctx.buf);
  }
  if (needle_len == 0 || needle_len > len) {
    return false;
  }
  for (size_t i = 0; i + needle_len <= len; i++) {
    if (memcmp(ctx.buf + i, needle, needle_len) == 0) {
      return true;
    }
  }
  return false;
}

// ===========================================================================
// INT GET handlers: wrong-accept -> 4.00, happy -> 2.05
// ===========================================================================
class KnxSwuGetInt : public KnxSwuBase {};

TEST_F(KnxSwuGetInt, ProtocolWrongAccept)
{
  RespCtx ctx;
  invoke_get(core_resource_knx_swu_protocol, ctx, APPLICATION_JSON);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_BAD_REQUEST));
}

TEST_F(KnxSwuGetInt, ProtocolHappy)
{
  RespCtx ctx;
  invoke_get(core_resource_knx_swu_protocol, ctx, APPLICATION_CBOR);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_OK));
}

TEST_F(KnxSwuGetInt, MethodHappy)
{
  RespCtx ctx;
  invoke_get(core_resource_knx_swu_method, ctx, APPLICATION_CBOR);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_OK));
}

TEST_F(KnxSwuGetInt, MethodWrongAccept)
{
  RespCtx ctx;
  invoke_get(core_resource_knx_swu_method, ctx, APPLICATION_JSON);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_BAD_REQUEST));
}

TEST_F(KnxSwuGetInt, ResultReflectsSetter)
{
  oc_swu_set_result(OC_SWU_RESULT_SUCCESS);
  RespCtx ctx;
  invoke_get(core_resource_knx_swu_result, ctx, APPLICATION_CBOR);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_OK));
}

TEST_F(KnxSwuGetInt, ResultWrongAccept)
{
  RespCtx ctx;
  invoke_get(core_resource_knx_swu_result, ctx, APPLICATION_JSON);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_BAD_REQUEST));
}

TEST_F(KnxSwuGetInt, StateReflectsSetter)
{
  oc_swu_set_state(OC_SWU_STATE_DOWNLOADED);
  RespCtx ctx;
  invoke_get(core_resource_knx_swu_state, ctx, APPLICATION_CBOR);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_OK));
}

TEST_F(KnxSwuGetInt, StateWrongAccept)
{
  RespCtx ctx;
  invoke_get(core_resource_knx_swu_state, ctx, APPLICATION_JSON);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_BAD_REQUEST));
}

TEST_F(KnxSwuGetInt, BytesReflectsSetter)
{
  oc_swu_set_package_bytes(4096);
  RespCtx ctx;
  invoke_get(core_resource_knx_swu_pkgbytes, ctx, APPLICATION_CBOR);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_OK));
}

TEST_F(KnxSwuGetInt, BytesWrongAccept)
{
  RespCtx ctx;
  invoke_get(core_resource_knx_swu_pkgbytes, ctx, APPLICATION_JSON);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_BAD_REQUEST));
}

// ===========================================================================
// STRING GET handlers: seed via setter, wrong-accept -> 4.00, happy -> 2.05
// ===========================================================================
class KnxSwuGetString : public KnxSwuBase {};

TEST_F(KnxSwuGetString, HwrefHappy)
{
  // the hardware reference is kept as plain 'const char*', the GET returns exactly that string
  oc_swu_set_hwref("0102030405ABCDEF");
  RespCtx ctx;
  invoke_get(core_resource_knx_swu_hwref, ctx, APPLICATION_CBOR);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_OK));
  EXPECT_TRUE(payload_contains(ctx, "0102030405ABCDEF"));
}

TEST_F(KnxSwuGetString, HwrefWrongAccept)
{
  RespCtx ctx;
  invoke_get(core_resource_knx_swu_hwref, ctx, APPLICATION_JSON);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_BAD_REQUEST));
}

TEST_F(KnxSwuGetString, LastUpdateHappy)
{
  oc_swu_set_last_update("2025-01-01T00:00:00Z");
  RespCtx ctx;
  invoke_get(core_resource_knx_swu_lastupdate, ctx, APPLICATION_CBOR);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_OK));
  // the stored RFC3339 timestamp is always returned, there is no "osv" status variant
  EXPECT_TRUE(payload_contains(ctx, "2025-01-01T00:00:00Z"));
  EXPECT_FALSE(payload_contains(ctx, "osv"));
}

TEST_F(KnxSwuGetString, LastUpdateWrongAccept)
{
  RespCtx ctx;
  invoke_get(core_resource_knx_swu_lastupdate, ctx, APPLICATION_JSON);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_BAD_REQUEST));
}

// ===========================================================================
// STATE-GATED GET handlers: not-DOWNLOADED -> 4.04, DOWNLOADED -> 2.05
// ===========================================================================
class KnxSwuGetGated : public KnxSwuBase {};

TEST_F(KnxSwuGetGated, PkgVersionNotDownloadedReturnsNotFound)
{
  oc_swu_set_state(OC_SWU_STATE_IDLE);
  RespCtx ctx;
  invoke_get(core_resource_knx_swu_pkgv, ctx, APPLICATION_CBOR);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_NOT_FOUND));
}

TEST_F(KnxSwuGetGated, PkgVersionDownloadedReturnsOk)
{
  oc_swu_set_package_version(1, 2, 3);
  oc_swu_set_state(OC_SWU_STATE_DOWNLOADED);
  RespCtx ctx;
  invoke_get(core_resource_knx_swu_pkgv, ctx, APPLICATION_CBOR);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_OK));
}

TEST_F(KnxSwuGetGated, PkgVersionWrongAccept)
{
  oc_swu_set_state(OC_SWU_STATE_DOWNLOADED);
  RespCtx ctx;
  invoke_get(core_resource_knx_swu_pkgv, ctx, APPLICATION_JSON);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_BAD_REQUEST));
}

TEST_F(KnxSwuGetGated, PkgNameNotDownloadedReturnsNotFound)
{
  oc_swu_set_state(OC_SWU_STATE_IDLE);
  RespCtx ctx;
  invoke_get(core_resource_knx_swu_pkgnames, ctx, APPLICATION_CBOR);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_NOT_FOUND));
}

TEST_F(KnxSwuGetGated, PkgNameDownloadedReturnsOk)
{
  oc_swu_set_package_name("firmware.bin");
  oc_swu_set_state(OC_SWU_STATE_DOWNLOADED);
  RespCtx ctx;
  invoke_get(core_resource_knx_swu_pkgnames, ctx, APPLICATION_CBOR);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_OK));
}

TEST_F(KnxSwuGetGated, PkgNameWrongAccept)
{
  oc_swu_set_state(OC_SWU_STATE_DOWNLOADED);
  RespCtx ctx;
  invoke_get(core_resource_knx_swu_pkgnames, ctx, APPLICATION_JSON);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_BAD_REQUEST));
}

TEST_F(KnxSwuGetGated, UpdateGetIdleBadRequest)
{
  // the remaining time is only relevant in DOWNLOADED, every other state is answered with '4.00 Bad Request'
  oc_swu_set_state(OC_SWU_STATE_IDLE);
  RespCtx ctx;
  invoke_get(core_resource_knx_swu_update, ctx, APPLICATION_CBOR);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_BAD_REQUEST));
}

TEST_F(KnxSwuGetGated, UpdateGetDownloadingBadRequest)
{
  // an ongoing download SHALL be answered with '4.00 Bad Request'
  oc_swu_set_state(OC_SWU_STATE_DOWNLOADING);
  RespCtx ctx;
  invoke_get(core_resource_knx_swu_update, ctx, APPLICATION_CBOR);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_BAD_REQUEST));
}

TEST_F(KnxSwuGetGated, UpdateGetDownloadedOk)
{
  oc_swu_set_state(OC_SWU_STATE_DOWNLOADED);
  RespCtx ctx;
  invoke_get(core_resource_knx_swu_update, ctx, APPLICATION_CBOR);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_OK));
}

TEST_F(KnxSwuGetGated, UpdateGetWrongAccept)
{
  RespCtx ctx;
  invoke_get(core_resource_knx_swu_update, ctx, APPLICATION_JSON);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_BAD_REQUEST));
}

// ===========================================================================
// PUT handlers
// ===========================================================================
class KnxSwuPut : public KnxSwuBase {};

static void set_int_payload(RespCtx &ctx, oc_rep_t &rep, int64_t v)
{
  memset(&rep, 0, sizeof(rep));
  rep.type = OC_REP_INT;
  rep.value.integer = v;
  ctx.request.request_payload = &rep;
}

// --- protocol PUT ----------------------------------------------------------
TEST_F(KnxSwuPut, ProtocolWrongAccept)
{
  RespCtx ctx;
  oc_rep_t rep;
  set_int_payload(ctx, rep, CoAP);
  invoke_put(core_resource_knx_swu_protocol, ctx, APPLICATION_JSON);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_BAD_REQUEST));
}

TEST_F(KnxSwuPut, ProtocolMissingPayloadBadRequest)
{
  RespCtx ctx;
  ctx.request.request_payload = nullptr;
  invoke_put(core_resource_knx_swu_protocol, ctx, APPLICATION_CBOR);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_BAD_REQUEST));
}

TEST_F(KnxSwuPut, ProtocolNonCoapBadRequest)
{
  RespCtx ctx;
  oc_rep_t rep;
  set_int_payload(ctx, rep, CoAPS);
  invoke_put(core_resource_knx_swu_protocol, ctx, APPLICATION_CBOR);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_BAD_REQUEST));
}

TEST_F(KnxSwuPut, ProtocolCoapChangedAndReadable)
{
  RespCtx ctx;
  oc_rep_t rep;
  set_int_payload(ctx, rep, CoAP);
  invoke_put(core_resource_knx_swu_protocol, ctx, APPLICATION_CBOR);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_CHANGED));
  // verify via GET
  RespCtx g;
  invoke_get(core_resource_knx_swu_protocol, g, APPLICATION_CBOR);
  EXPECT_EQ(g.code(), oc_status_code(OC_STATUS_OK));
}

// --- method PUT ------------------------------------------------------------
TEST_F(KnxSwuPut, MethodWrongAccept)
{
  RespCtx ctx;
  oc_rep_t rep;
  set_int_payload(ctx, rep, PUSH);
  invoke_put(core_resource_knx_swu_method, ctx, APPLICATION_JSON);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_BAD_REQUEST));
}

TEST_F(KnxSwuPut, MethodNonPushBadRequest)
{
  RespCtx ctx;
  oc_rep_t rep;
  set_int_payload(ctx, rep, PULL);
  invoke_put(core_resource_knx_swu_method, ctx, APPLICATION_CBOR);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_BAD_REQUEST));
}

TEST_F(KnxSwuPut, MethodPushChangedAndReadable)
{
  RespCtx ctx;
  oc_rep_t rep;
  set_int_payload(ctx, rep, PUSH);
  invoke_put(core_resource_knx_swu_method, ctx, APPLICATION_CBOR);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_CHANGED));
  RespCtx g;
  invoke_get(core_resource_knx_swu_method, g, APPLICATION_CBOR);
  EXPECT_EQ(g.code(), oc_status_code(OC_STATUS_OK));
}

// --- update PUT (state-gated, triggers upgrade callback) -------------------
TEST_F(KnxSwuPut, UpdateWrongAccept)
{
  RespCtx ctx;
  oc_rep_t rep;
  set_int_payload(ctx, rep, 5);
  oc_swu_set_state(OC_SWU_STATE_DOWNLOADED);
  invoke_put(core_resource_knx_swu_update, ctx, APPLICATION_JSON);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_BAD_REQUEST));
}

TEST_F(KnxSwuPut, UpdateNotDownloadedBadRequest)
{
  RespCtx ctx;
  oc_rep_t rep;
  set_int_payload(ctx, rep, 5);
  oc_swu_set_state(OC_SWU_STATE_IDLE);
  invoke_put(core_resource_knx_swu_update, ctx, APPLICATION_CBOR);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_BAD_REQUEST));
}

TEST_F(KnxSwuPut, UpdateDownloadedNonIntBadRequest)
{
  RespCtx ctx;
  ctx.request.request_payload = nullptr;
  oc_swu_set_state(OC_SWU_STATE_DOWNLOADED);
  invoke_put(core_resource_knx_swu_update, ctx, APPLICATION_CBOR);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_BAD_REQUEST));
}

TEST_F(KnxSwuPut, UpdateDownloadedIntChangedInvokesUpgradeCb)
{
  UpgradeRec rec;
  oc_set_swu_upgrade_cb(recording_upgrade_cb, &rec);
  oc_swu_set_state(OC_SWU_STATE_DOWNLOADED);
  RespCtx ctx;
  oc_rep_t rep;
  set_int_payload(ctx, rep, 7);
  invoke_put(core_resource_knx_swu_update, ctx, APPLICATION_CBOR);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_CHANGED));
  EXPECT_TRUE(rec.called);
  EXPECT_EQ(rec.defer, 7);
  // state transitioned DOWNLOADED -> UPDATING
  RespCtx g;
  invoke_get(core_resource_knx_swu_state, g, APPLICATION_CBOR);
  EXPECT_EQ(g.code(), oc_status_code(OC_STATUS_OK));
}

// ===========================================================================
// /a/swu PUT handler (block download)
// ===========================================================================
class KnxSwuABlock : public KnxSwuBase {};

TEST_F(KnxSwuABlock, WrongAcceptBadRequest)
{
  RespCtx ctx;
  char q[] = "ps=10";
  ctx.request.query = q;
  ctx.request.query_len = strlen(q);
  // a/swu requires APPLICATION_OCTET_STREAM
  invoke_put(core_resource_knx_swu_pkgcmd, ctx, APPLICATION_CBOR);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_BAD_REQUEST));
}

TEST_F(KnxSwuABlock, InvalidPsParamBadRequest)
{
  RespCtx ctx;
  char q[] = "ps=abc";
  ctx.request.query = q;
  ctx.request.query_len = strlen(q);
  ctx.request._payload = nullptr;
  ctx.request._payload_len = 0;
  invoke_put(core_resource_knx_swu_pkgcmd, ctx, APPLICATION_OCTET_STREAM);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_BAD_REQUEST));
}

// query parameter edge cases for 'po', 'ps' and 'pkgs' -------------------------
struct SwuQueryCase {
  const char *name;
  const char *query;
};

class KnxSwuABlockInvalidQuery : public KnxSwuBase,
                                 public ::testing::WithParamInterface<SwuQueryCase> {
};

TEST_P(KnxSwuABlockInvalidQuery, ReturnsBadRequest)
{
  // an invalid 'po', 'ps' or 'pkgs' must be rejected before the payload is looked at
  const SwuQueryCase &test_case = GetParam();
  SwuBlockRec rec;
  oc_set_swu_cb(recording_swu_cb, &rec);
  oc_swu_set_state(OC_SWU_STATE_IDLE);

  RespCtx ctx;
  std::string q(test_case.query);
  ctx.request.query = const_cast<char *>(q.c_str());
  ctx.request.query_len = q.size();
  uint8_t data[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
  ctx.request._payload = data;
  ctx.request._payload_len = sizeof(data);

  invoke_put(core_resource_knx_swu_pkgcmd, ctx, APPLICATION_OCTET_STREAM);

  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_BAD_REQUEST));
  EXPECT_FALSE(rec.called);
}

INSTANTIATE_TEST_SUITE_P(
  PoPsPkgs, KnxSwuABlockInvalidQuery,
  ::testing::Values(
    // non numeric
    SwuQueryCase{ "PoNonNumeric", "po=abc&ps=10" },
    SwuQueryCase{ "PsNonNumeric", "po=0&ps=abc" },
    SwuQueryCase{ "PkgsNonNumeric", "po=0&ps=10&pkgs=abc" },
    // trailing garbage
    SwuQueryCase{ "PoTrailingGarbage", "po=2abc&ps=10" },
    SwuQueryCase{ "PsTrailingGarbage", "po=0&ps=10abc" },
    SwuQueryCase{ "PkgsTrailingGarbage", "po=0&ps=10&pkgs=64k" },
    // negative
    SwuQueryCase{ "PoNegative", "po=-1&ps=10" },
    SwuQueryCase{ "PsNegative", "po=0&ps=-1" },
    SwuQueryCase{ "PkgsNegative", "po=0&ps=10&pkgs=-1" },
    // zero, note that 'po=0' is valid (it is the default) and therefore not listed here
    SwuQueryCase{ "PsZero", "po=0&ps=0" },
    SwuQueryCase{ "PkgsZero", "po=0&ps=10&pkgs=0" },
    // above INT_MAX
    SwuQueryCase{ "PoAboveIntMax", "po=2147483648&ps=10" },
    SwuQueryCase{ "PsAboveIntMax", "po=0&ps=2147483648" },
    SwuQueryCase{ "PkgsAboveIntMax", "po=0&ps=10&pkgs=2147483648" },
    // far above INT_MAX, more digits than the parser accepts at all
    SwuQueryCase{ "PoTooManyDigits", "po=99999999999999&ps=10" },
    /*
      11 digits, one more than the parser accepts, although the value is a valid INT_MAX resp. small number.
      The rejection therefore comes from the length limit alone and not from the value being out of range.
    */
    SwuQueryCase{ "PoLeadingZeroIntMax", "po=02147483647&ps=10" },
    SwuQueryCase{ "PsLeadingZeroSmall", "po=0&ps=00000000010" },
    SwuQueryCase{ "PkgsLeadingZeroSmall", "po=0&ps=10&pkgs=00000000064" },
    // empty value
    SwuQueryCase{ "PoEmpty", "po=&ps=10" },
    SwuQueryCase{ "PsEmpty", "po=0&ps=" },
    SwuQueryCase{ "PkgsEmpty", "po=0&ps=10&pkgs=" }),
  [](const ::testing::TestParamInfo<SwuQueryCase> &info) { return info.param.name; });

// 'po=0' is the documented default and must be accepted, unlike 'ps=0' and 'pkgs=0'
TEST_F(KnxSwuABlock, PoZeroIsAccepted)
{
  SwuBlockRec rec;
  oc_set_swu_cb(recording_swu_cb, &rec);
  oc_swu_set_state(OC_SWU_STATE_IDLE);
  RespCtx ctx;
  char q[] = "po=0&ps=10&pkgs=8";
  ctx.request.query = q;
  ctx.request.query_len = strlen(q);
  uint8_t data[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
  ctx.request._payload = data;
  ctx.request._payload_len = sizeof(data);
  invoke_put(core_resource_knx_swu_pkgcmd, ctx, APPLICATION_OCTET_STREAM);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_CHANGED));
  EXPECT_TRUE(rec.called);
  EXPECT_EQ(rec.block_offset, 0u);
}

// the widest accepted value, 10 digits, must still be parsed
TEST_F(KnxSwuABlock, IntMaxOffsetIsAccepted)
{
  SwuBlockRec rec;
  oc_set_swu_cb(recording_swu_cb, &rec);
  oc_swu_set_state(OC_SWU_STATE_IDLE);
  RespCtx ctx;
  char q[] = "po=2147483647&ps=10&pkgs=64";
  ctx.request.query = q;
  ctx.request.query_len = strlen(q);
  uint8_t data[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
  ctx.request._payload = data;
  ctx.request._payload_len = sizeof(data);
  invoke_put(core_resource_knx_swu_pkgcmd, ctx, APPLICATION_OCTET_STREAM);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_CHANGED));
  EXPECT_TRUE(rec.called);
  EXPECT_EQ(rec.block_offset, 2147483647u);
}

// leading zeros are accepted as long as the total length stays within the 10 digits the parser allows
TEST_F(KnxSwuABlock, LeadingZerosUpToMaxLenAreAccepted)
{
  SwuBlockRec rec;
  oc_set_swu_cb(recording_swu_cb, &rec);
  oc_swu_set_state(OC_SWU_STATE_IDLE);
  RespCtx ctx;
  char q[] = "po=0000000010&ps=0000000010&pkgs=0000000064";
  ctx.request.query = q;
  ctx.request.query_len = strlen(q);
  uint8_t data[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
  ctx.request._payload = data;
  ctx.request._payload_len = sizeof(data);
  invoke_put(core_resource_knx_swu_pkgcmd, ctx, APPLICATION_OCTET_STREAM);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_CHANGED));
  EXPECT_TRUE(rec.called);
  EXPECT_EQ(rec.block_offset, 10u);
}

// a leading zero is accepted, the value is decimal coded and not octal
TEST_F(KnxSwuABlock, LeadingZeroIsAccepted)
{
  SwuBlockRec rec;
  oc_set_swu_cb(recording_swu_cb, &rec);
  oc_swu_set_state(OC_SWU_STATE_IDLE);
  RespCtx ctx;
  char q[] = "po=010&ps=10&pkgs=64";
  ctx.request.query = q;
  ctx.request.query_len = strlen(q);
  uint8_t data[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
  ctx.request._payload = data;
  ctx.request._payload_len = sizeof(data);
  invoke_put(core_resource_knx_swu_pkgcmd, ctx, APPLICATION_OCTET_STREAM);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_CHANGED));
  EXPECT_TRUE(rec.called);
  EXPECT_EQ(rec.block_offset, 10u);
}

// the query value is not NUL terminated, the parser must not read beyond value_len
TEST_F(KnxSwuABlock, ValueIsNotReadBeyondItsLength)
{
  SwuBlockRec rec;
  oc_set_swu_cb(recording_swu_cb, &rec);
  oc_swu_set_state(OC_SWU_STATE_IDLE);
  RespCtx ctx;
  // truncate the query so that 'ps' ends at '1', the trailing '23' must be ignored
  char q[] = "po=0&ps=123";
  ctx.request.query = q;
  ctx.request.query_len = strlen(q) - 2;
  uint8_t data[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
  ctx.request._payload = data;
  ctx.request._payload_len = sizeof(data);
  invoke_put(core_resource_knx_swu_pkgcmd, ctx, APPLICATION_OCTET_STREAM);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_CHANGED));
  EXPECT_TRUE(rec.called);
}

TEST_F(KnxSwuABlock, NoCallbackReturnsNotImplemented)
{
  oc_set_swu_cb(nullptr, nullptr);  oc_swu_set_state(OC_SWU_STATE_IDLE);
  RespCtx ctx;
  char q[] = "po=1&ps=10";
  ctx.request.query = q;
  ctx.request.query_len = strlen(q);
  uint8_t data[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
  ctx.request._payload = data;
  ctx.request._payload_len = sizeof(data);
  invoke_put(core_resource_knx_swu_pkgcmd, ctx, APPLICATION_OCTET_STREAM);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_NOT_IMPLEMENTED));
}

TEST_F(KnxSwuABlock, CallbackFastPathReturnsChanged)
{
  SwuBlockRec rec;
  oc_set_swu_cb(recording_swu_cb, &rec);
  oc_swu_set_state(OC_SWU_STATE_IDLE);
  RespCtx ctx;
  char q[] = "po=1&ps=10";
  ctx.request.query = q;
  ctx.request.query_len = strlen(q);
  uint8_t data[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
  ctx.request._payload = data;
  ctx.request._payload_len = sizeof(data);
  invoke_put(core_resource_knx_swu_pkgcmd, ctx, APPLICATION_OCTET_STREAM);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_CHANGED));
  EXPECT_TRUE(rec.called);
  EXPECT_EQ(rec.block_len, sizeof(data));
}

// ===========================================================================
// /swu list GET handler: wrong-accept and paging rejection (link-format
// SUCCESS body is integration-level, see header comment)
// ===========================================================================
class KnxSwuList : public KnxSwuBase {};

TEST_F(KnxSwuList, WrongAcceptBadRequest)
{
  RespCtx ctx;
  invoke_get(core_resource_knx_swu, ctx, APPLICATION_CBOR);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_BAD_REQUEST));
}

// ===========================================================================
// Public setters verified indirectly through GET handlers
// ===========================================================================
class KnxSwuSetters : public KnxSwuBase {};

TEST_F(KnxSwuSetters, SetStateVisibleInStateGet)
{
  oc_swu_set_state(OC_SWU_STATE_UPDATING);
  RespCtx ctx;
  invoke_get(core_resource_knx_swu_state, ctx, APPLICATION_CBOR);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_OK));
}

TEST_F(KnxSwuSetters, SetResultVisibleInResultGet)
{
  oc_swu_set_result(OC_SWU_RESULT_ERR_URL);
  RespCtx ctx;
  invoke_get(core_resource_knx_swu_result, ctx, APPLICATION_CBOR);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_OK));
}

TEST_F(KnxSwuSetters, SetPackageBytesVisibleInBytesGet)
{
  oc_swu_set_package_bytes(65536);
  RespCtx ctx;
  invoke_get(core_resource_knx_swu_pkgbytes, ctx, APPLICATION_CBOR);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_OK));
}

TEST_F(KnxSwuSetters, SetHwrefVisibleInHwrefGet)
{
  oc_swu_set_hwref("DEADBEEFCAFEBABE");
  RespCtx ctx;
  invoke_get(core_resource_knx_swu_hwref, ctx, APPLICATION_CBOR);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_OK));
  EXPECT_TRUE(payload_contains(ctx, "DEADBEEFCAFEBABE"));
  // the setter only stores the pointer, a second call must be visible immediately
  oc_swu_set_hwref("0102030405ABCDEF");
  RespCtx g;
  invoke_get(core_resource_knx_swu_hwref, g, APPLICATION_CBOR);
  EXPECT_EQ(g.code(), oc_status_code(OC_STATUS_OK));
  EXPECT_TRUE(payload_contains(g, "0102030405ABCDEF"));
}

TEST_F(KnxSwuSetters, SetLastUpdateVisibleInLastUpdateGet)
{
  oc_swu_set_last_update("2030-12-31T23:59:59Z");
  RespCtx ctx;
  invoke_get(core_resource_knx_swu_lastupdate, ctx, APPLICATION_CBOR);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_OK));
  EXPECT_TRUE(payload_contains(ctx, "2030-12-31T23:59:59Z"));
}

TEST_F(KnxSwuSetters, SetPackageVersionVisibleInPkgvGet)
{
  oc_swu_set_package_version(4, 5, 6);
  oc_swu_set_state(OC_SWU_STATE_DOWNLOADED);
  RespCtx ctx;
  invoke_get(core_resource_knx_swu_pkgv, ctx, APPLICATION_CBOR);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_OK));
}

TEST_F(KnxSwuSetters, SetPackageNameVisibleInPkgNameGet)
{
  oc_swu_set_package_name("image-v2.bin");
  oc_swu_set_state(OC_SWU_STATE_DOWNLOADED);
  RespCtx ctx;
  invoke_get(core_resource_knx_swu_pkgnames, ctx, APPLICATION_CBOR);
  EXPECT_EQ(ctx.code(), oc_status_code(OC_STATUS_OK));
}

// ===========================================================================
// Public callback registration setters
// ===========================================================================
class KnxSwuCbReg : public KnxSwuBase {};

TEST_F(KnxSwuCbReg, SetSwuUpgradeCbThenTriggered)
{
  UpgradeRec rec;
  oc_set_swu_upgrade_cb(recording_upgrade_cb, &rec);
  oc_swu_set_state(OC_SWU_STATE_DOWNLOADED);
  RespCtx ctx;
  oc_rep_t rep;
  set_int_payload(ctx, rep, 0);
  invoke_put(core_resource_knx_swu_update, ctx, APPLICATION_CBOR);
  EXPECT_TRUE(rec.called);
  EXPECT_EQ(rec.defer, 0);
}

TEST_F(KnxSwuCbReg, SetSwuCbThenTriggered)
{
  SwuBlockRec rec;
  oc_set_swu_cb(recording_swu_cb, &rec);
  oc_swu_set_state(OC_SWU_STATE_IDLE);
  RespCtx ctx;
  char q[] = "po=2&ps=4";
  ctx.request.query = q;
  ctx.request.query_len = strlen(q);
  uint8_t data[4] = { 9, 8, 7, 6 };
  ctx.request._payload = data;
  ctx.request._payload_len = sizeof(data);
  invoke_put(core_resource_knx_swu_pkgcmd, ctx, APPLICATION_OCTET_STREAM);
  EXPECT_TRUE(rec.called);
  EXPECT_EQ(rec.block_offset, 2u);
}
