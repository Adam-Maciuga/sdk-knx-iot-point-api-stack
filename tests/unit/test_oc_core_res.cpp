/*
 * Unit tests for the device hostname API in api/oc_core_res.c
 * (declared in include/oc_core_res.h).
 *
 * Covers:
 *   oc_core_set_device_hostname          — stores a hostname string on the device
 *   oc_core_read_and_set_device_hostname — reads hostname from storage, falling
 *                                          back to the "knx-<serial>" default
 *
 * Requirements:
 *   oc_mmem_init() — the hostname/serial are oc_string_t (allocated strings).
 *   Storage is intentionally left UNCONFIGURED so oc_storage_read() returns an
 *   error and the default-hostname fallback path is exercised deterministically.
 *
 * Note: oc_core_get_device_info() returns a pointer to the static oc_device_info,
 *       so we can read back what the setters wrote without full stack init.
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "oc_core_res.h"
#include "oc_endpoint.h" /* HNAME_SIZE, SERIAL_NUM_SIZE */
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

  static void set_serial(const char *sn)
  {
    oc_device_info_t *dev = oc_core_get_device_info();
    oc_free_string(&dev->serialnumber);
    /* the production code always copies SERIAL_NUM_SIZE chars */
    oc_new_string(&dev->serialnumber, sn, SERIAL_NUM_SIZE);
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

/* ──────────────────────── oc_core_read_and_set_device_hostname ────────────── */

TEST_F(DeviceHostname, ReadAndSetUsesDefaultWhenStorageUnavailable)
{
  /* Storage is not configured → oc_storage_read() returns an error → the
   * device must fall back to the spec-defined "knx-<serial>" host name. */
  set_serial("00fa10020700");

  EXPECT_EQ(oc_core_read_and_set_device_hostname(), 0);
  EXPECT_STREQ(oc_string(oc_core_get_device_info()->iot_hostname),
               "knx-00fa10020700");
}

TEST_F(DeviceHostname, ReadAndSetDefaultFitsHnameBuffer)
{
  /* The default "knx-" + 12-char serial + NUL must be exactly HNAME_SIZE. */
  set_serial("aabbccddeeff");
  ASSERT_EQ(oc_core_read_and_set_device_hostname(), 0);

  const char *hn = oc_string(oc_core_get_device_info()->iot_hostname);
  EXPECT_STREQ(hn, "knx-aabbccddeeff");
  EXPECT_LT(strlen(hn) + 1, (size_t)HNAME_SIZE + 1); /* fits in HNAME_SIZE */
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
  EXPECT_EQ(oc_core_set_device_fwv(1, 2, 3), 0);
  const oc_device_info_t *d = oc_core_get_device_info();
  EXPECT_EQ(d->fwv.major, 1);
  EXPECT_EQ(d->fwv.minor, 2);
  EXPECT_EQ(d->fwv.patch, 3);
}

TEST_F(DeviceInfo, SetHardwareVersion)
{
  EXPECT_EQ(oc_core_set_device_hwv(4, 5, 6), 0);
  const oc_device_info_t *d = oc_core_get_device_info();
  EXPECT_EQ(d->hwv.major, 4);
  EXPECT_EQ(d->hwv.minor, 5);
  EXPECT_EQ(d->hwv.patch, 6);
}

TEST_F(DeviceInfo, SetApplicationVersion)
{
  EXPECT_EQ(oc_core_set_device_apv(7, 8, 9), 0);
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
  EXPECT_STREQ(oc_string(oc_core_get_device_info()->hwt), "MYHW");
}

TEST_F(DeviceInfo, SetModel)
{
  EXPECT_EQ(oc_core_set_device_model("model-x"), 0);
  EXPECT_STREQ(oc_string(oc_core_get_device_info()->iot_model), "model-x");
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
  EXPECT_EQ(oc_core_set_and_store_device_application_version(2, 1, 0), 0);
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
