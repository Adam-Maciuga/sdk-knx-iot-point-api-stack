/*
 * Unit tests for the device hostname API in api/oc_core_res.c
 * (declared in include/oc_core_res.h).
 *
 * Covers:
 *   oc_core_set_device_hostname — stores a hostname label on the device,
 *                                 rejects NULL, names over HNAME_MAX_SIZE and names with a '.'
 *
 * Requirements:
 *   oc_mmem_init() — the hostname is an oc_string_t (allocated string).
 *   Storage is intentionally left UNCONFIGURED, setters that store are no-ops.
 *
 * Note: oc_core_get_device_info() returns a pointer to the static oc_device_info,
 *       so we can read back what the setters wrote without full stack init.
 */

#include <gtest/gtest.h>
#include <cstring>
#include <string>

extern "C" {
#include "oc_core_res.h"
#include "oc_endpoint.h"
#include "oc_helpers.h"
#include "oc_ri.h"
#include "util/oc_mmem.h"
}

class DeviceHostname : public ::testing::Test {
protected:
  void SetUp() override
  {
    oc_mmem_init();
  }

};

/* ───────────────────────────── oc_core_set_device_hostname ───────────────── */

TEST_F(DeviceHostname, SetStoresValue)
{
  EXPECT_EQ(oc_core_set_device_hostname("my-host"), 0);
  EXPECT_STREQ(oc_string(oc_core_get_device_info()->iot_hostname), "my-host");
}

TEST_F(DeviceHostname, SetOverwritesPrevious)
{
  oc_core_set_device_hostname("first-host");
  oc_core_set_device_hostname("second-host");
  EXPECT_STREQ(oc_string(oc_core_get_device_info()->iot_hostname), "second-host");
}

TEST_F(DeviceHostname, SetEmptyString)
{
  EXPECT_EQ(oc_core_set_device_hostname(""), 0);
  EXPECT_STREQ(oc_string(oc_core_get_device_info()->iot_hostname), "");
}

TEST_F(DeviceHostname, SetNullRejectedKeepsPrevious)
{
  ASSERT_EQ(oc_core_set_device_hostname("kept-host"), 0);
  EXPECT_EQ(oc_core_set_device_hostname(nullptr), -1);
  EXPECT_STREQ(oc_string(oc_core_get_device_info()->iot_hostname), "kept-host");
}

TEST_F(DeviceHostname, SetWithDomainRejectedKeepsPrevious)
{
  /* the host name is a single DNS label, the domain '.local' is appended where needed */
  ASSERT_EQ(oc_core_set_device_hostname("knx-00fa10020800"), 0);
  EXPECT_EQ(oc_core_set_device_hostname("knx-00fa10020800.local"), -1);
  EXPECT_STREQ(oc_string(oc_core_get_device_info()->iot_hostname), "knx-00fa10020800");
}

TEST_F(DeviceHostname, SetMaxLengthAcceptedOneMoreRejected)
{
  /* mDNS/SRP build the announced name in HNAME_MAX_SIZE based buffers */
  const std::string max_name(HNAME_MAX_SIZE, 'a');
  EXPECT_EQ(oc_core_set_device_hostname(max_name.c_str()), 0);
  EXPECT_STREQ(oc_string(oc_core_get_device_info()->iot_hostname), max_name.c_str());

  const std::string too_long(HNAME_MAX_SIZE + 1, 'b');
  EXPECT_EQ(oc_core_set_device_hostname(too_long.c_str()), -1);
  EXPECT_STREQ(oc_string(oc_core_get_device_info()->iot_hostname), max_name.c_str());
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Device-info setters/getters — write the static oc_device_info and (where
 * applicable) attempt a storage write that is a no-op when storage is not
 * configured. We assert the in-memory device-info reflects the change.
 * ═══════════════════════════════════════════════════════════════════════════ */

class DeviceInfo : public ::testing::Test {
protected:
  void SetUp() override { oc_mmem_init(); }
};

TEST_F(DeviceInfo, SetFirmwareVersion)
{
  oc_knx_version_info_t version = {1, 2, 3};
  EXPECT_EQ(oc_core_set_and_store_device_fwv(&version), 0);
  const oc_device_info_t *d = oc_core_get_device_info();
  EXPECT_EQ(d->fwv.major, 1);
  EXPECT_EQ(d->fwv.minor, 2);
  EXPECT_EQ(d->fwv.patch, 3);
}

TEST_F(DeviceInfo, SetHardwareVersion)
{
  // hwv is stored by reference, the application owns the version struct
  static const oc_knx_version_info_t version = {4, 5, 6};
  EXPECT_EQ(oc_core_set_device_hwv(&version), 0);
  const oc_device_info_t *d = oc_core_get_device_info();
  ASSERT_EQ(d->hwv, &version);
  EXPECT_EQ(d->hwv->major, 4);
  EXPECT_EQ(d->hwv->minor, 5);
  EXPECT_EQ(d->hwv->patch, 6);
}

TEST_F(DeviceInfo, SetHardwareVersionKeepsReferenceToCallerData)
{
  static oc_knx_version_info_t version = {1, 1, 1};
  EXPECT_EQ(oc_core_set_device_hwv(&version), 0);

  // changing the caller-owned struct is visible through the device info
  version.major = 7;
  version.minor = 8;
  version.patch = 9;

  const oc_device_info_t *d = oc_core_get_device_info();
  EXPECT_EQ(d->hwv->major, 7);
  EXPECT_EQ(d->hwv->minor, 8);
  EXPECT_EQ(d->hwv->patch, 9);
}

TEST_F(DeviceInfo, SetApplicationVersion)
{
  oc_knx_version_info_t version = {7, 8, 9};
  EXPECT_EQ(oc_core_set_and_store_device_application_version(&version), 0);
  const oc_device_info_t *d = oc_core_get_device_info();
  EXPECT_EQ(d->apv.major, 7);
  EXPECT_EQ(d->apv.minor, 8);
  EXPECT_EQ(d->apv.patch, 9);
}

TEST_F(DeviceInfo, SetManufacturerId)
{
  EXPECT_EQ(oc_core_set_device_mid(0xCAFEu), 0);
  EXPECT_EQ(oc_core_get_device_info()->mid, 0xCAFEu);
}

TEST_F(DeviceInfo, SetHardwareType)
{
  EXPECT_EQ(oc_core_set_device_hwt("MYHW"), 0);
  EXPECT_STREQ(oc_core_get_device_info()->hwt, "MYHW");
}

TEST_F(DeviceInfo, SetSerialNumber)
{
  oc_core_set_device_sn("aabbccddeeff");
  EXPECT_STREQ(oc_core_get_device_info()->serialnumber, "aabbccddeeff");
}

TEST_F(DeviceInfo, SetModel)
{
  EXPECT_EQ(oc_core_set_device_order_number("order-number-x"), 0);
  EXPECT_STREQ(oc_core_get_device_info()->order_number, "order-number-x");
}

TEST_F(DeviceInfo, ManufacturingDateDefaultsToUnixEpoch)
{
  // only valid as long as the application did not set a date yet
  EXPECT_STREQ(oc_core_get_device_info()->mfg_date, "1970-01-01T00:00:00Z");
}

TEST_F(DeviceInfo, SetManufacturingDate)
{
  EXPECT_EQ(oc_core_set_device_mfd("2020-04-12T23:20:50.52Z"), 0);
  EXPECT_STREQ(oc_core_get_device_info()->mfg_date, "2020-04-12T23:20:50.52Z");
}

TEST_F(DeviceInfo, SetManufacturingDateOverwritesPrevious)
{
  EXPECT_EQ(oc_core_set_device_mfd("2020-04-12T23:20:50.52Z"), 0);
  EXPECT_EQ(oc_core_set_device_mfd("2024-01-31T10:00:00Z"), 0);
  EXPECT_STREQ(oc_core_get_device_info()->mfg_date, "2024-01-31T10:00:00Z");
}

TEST_F(DeviceInfo, SetAndStoreIaInRange)
{
  EXPECT_TRUE(oc_core_set_and_store_device_ia(0x1234));
  EXPECT_EQ(oc_core_get_device_info()->ia, 0x1234);
}

TEST_F(DeviceInfo, SetAndStoreIaRejectsNegative)
{
  EXPECT_FALSE(oc_core_set_and_store_device_ia(-1));
}

TEST_F(DeviceInfo, SetAndStoreIaRejectsTooLarge)
{
  EXPECT_FALSE(oc_core_set_and_store_device_ia(0x10000));
}

TEST_F(DeviceInfo, SetAndStoreIidInRange)
{
  EXPECT_TRUE(oc_core_set_and_store_device_iid(0xABCDEF));
  EXPECT_EQ(oc_core_get_device_iid(), 0xABCDEFu);
}

TEST_F(DeviceInfo, SetAndStoreIidRejectsNegative)
{
  EXPECT_FALSE(oc_core_set_and_store_device_iid(-5));
}

TEST_F(DeviceInfo, SetAndStoreApplicationVersion)
{
  oc_knx_version_info_t version = {2, 1, 0};
  EXPECT_EQ(oc_core_set_and_store_device_application_version(&version), 0);
  const oc_device_info_t *d = oc_core_get_device_info();
  EXPECT_EQ(d->apv.major, 2);
  EXPECT_EQ(d->apv.minor, 1);
  EXPECT_EQ(d->apv.patch, 0);
}

TEST_F(DeviceInfo, SetAndStoreFidInRange)
{
  EXPECT_TRUE(oc_core_set_and_store_device_fid(0x99));
  EXPECT_EQ(oc_core_get_device_info()->fid, 0x99u);
}

TEST_F(DeviceInfo, SetAndStoreFidRejectsNegative)
{
  EXPECT_FALSE(oc_core_set_and_store_device_fid(-1));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_core_get_core_resource_by_index — bounds checking
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(CoreResourceByIndex, NegativeIndexReturnsNull)
{
  EXPECT_EQ(oc_core_get_core_resource_by_index(-1), nullptr);
}

TEST(CoreResourceByIndex, OutOfRangeIndexReturnsNull)
{
  EXPECT_EQ(oc_core_get_core_resource_by_index(OC_NUM_CORE_RESOURCES), nullptr);
}

TEST(CoreResourceByIndex, FirstIndexReturnsResource)
{
  const oc_resource_t *r = oc_core_get_core_resource_by_index(0);
  ASSERT_NE(r, nullptr);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_check_request_query_value_on_urn_knx
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(CheckUrnKnxQuery, MatchesWhenValueStartsWithUrnKnx)
{
  oc_request_t req{};
  req.query = (char *)"rt=urn:knx:dpa.201.1";
  req.query_len = strlen(req.query);
  EXPECT_TRUE(oc_check_request_query_value_on_urn_knx(&req));
}

TEST(CheckUrnKnxQuery, NoMatchWhenAbsent)
{
  oc_request_t req{};
  req.query = (char *)"pn=0&ps=5";
  req.query_len = strlen(req.query);
  EXPECT_FALSE(oc_check_request_query_value_on_urn_knx(&req));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_check_resource_by_rt — resource type matching with/without wildcards
 * ═══════════════════════════════════════════════════════════════════════════ */

class CheckResourceByRt : public ::testing::Test {
protected:
  oc_resource_t resource{};
  void SetUp() override
  {
    oc_mmem_init();
    oc_new_string_array(&resource.types, 1);
    oc_string_array_add_item(resource.types, "urn:knx:dpa.201.61");
  }
  void TearDown() override { oc_free_string_array(&resource.types); }
};

TEST_F(CheckResourceByRt, ExactMatch)
{
  oc_request_t req{};
  req.query = (char *)"rt=urn:knx:dpa.201.61";
  req.query_len = strlen(req.query);
  EXPECT_TRUE(oc_check_resource_by_rt(&resource, &req));
}

TEST_F(CheckResourceByRt, WildcardMatch)
{
  oc_request_t req{};
  req.query = (char *)"rt=urn:knx:dpa.201.*";
  req.query_len = strlen(req.query);
  EXPECT_TRUE(oc_check_resource_by_rt(&resource, &req));
}

TEST_F(CheckResourceByRt, NoMatchDifferentType)
{
  oc_request_t req{};
  req.query = (char *)"rt=urn:knx:dpa.999.1";
  req.query_len = strlen(req.query);
  EXPECT_FALSE(oc_check_resource_by_rt(&resource, &req));
}

TEST_F(CheckResourceByRt, NoRtKeyMatchesByDefault)
{
  oc_request_t req{};
  req.query = (char *)"if=urn:knx:if.i";
  req.query_len = strlen(req.query);
  EXPECT_TRUE(oc_check_resource_by_rt(&resource, &req));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_check_resource_by_if — interface matching with/without wildcards
 * ═══════════════════════════════════════════════════════════════════════════ */

static void dummy_handler(oc_request_t *, oc_interface_mask_t, void *) {}

class CheckResourceByIf : public ::testing::Test {
protected:
  oc_resource_t resource{};
  void SetUp() override
  {
    /* a PUT handler exposing the if.i (logical input) interface */
    resource.put_handler.cb = dummy_handler;
    resource.put_handler.interface_mask = OC_IF_I;
  }
};

TEST_F(CheckResourceByIf, ExactMatch)
{
  oc_request_t req{};
  req.query = (char *)"if=urn:knx:if.i";
  req.query_len = strlen(req.query);
  EXPECT_TRUE(oc_check_resource_by_if(&resource, &req));
}

TEST_F(CheckResourceByIf, WildcardMatch)
{
  oc_request_t req{};
  req.query = (char *)"if=urn:knx:if.*";
  req.query_len = strlen(req.query);
  EXPECT_TRUE(oc_check_resource_by_if(&resource, &req));
}

TEST_F(CheckResourceByIf, NoMatchDifferentInterface)
{
  oc_request_t req{};
  req.query = (char *)"if=urn:knx:if.o";
  req.query_len = strlen(req.query);
  EXPECT_FALSE(oc_check_resource_by_if(&resource, &req));
}

TEST_F(CheckResourceByIf, NoIfKeyMatchesByDefault)
{
  oc_request_t req{};
  req.query = (char *)"rt=urn:knx:dpa.201.1";
  req.query_len = strlen(req.query);
  EXPECT_TRUE(oc_check_resource_by_if(&resource, &req));
}
