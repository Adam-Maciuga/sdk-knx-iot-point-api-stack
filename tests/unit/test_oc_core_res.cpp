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
