/*
 * Headless KNX IoT test server for runtime (network-level) conformance tests.
 *
 * Boots the full stack with real networking (no mocks), registers EITT-style
 * datapoints (LSAB FB 417 + LSSB FB 421), and runs the event loop.
 *
 * Designed to be started as a subprocess by the Python test suite.
 * Prints "RUNTIME_TEST_SERVER_READY" to stdout when the stack is initialized
 * and ready to accept CoAP requests.
 *
 * Stop with SIGINT or SIGTERM (or Ctrl-C).
 *
 * Configuration matches the EITT test template defaults:
 *   Serial number : 00fa10020800
 *   Hardware type  : Linux
 *   Model          : KNX Certification
 *   Manufacturer ID: 667
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "oc_api.h"
#include "oc_core_res.h"
#include "oc_knx_dev.h"
#include "oc_knx_fp.h"
#include "oc_knx_client.h"
#include "oc_knx_sec.h"
#include "oc_knx_swu.h"
#include "oc_test_control.h"
#include "port/oc_clock.h"
#include "port/oc_connectivity.h"
#include "port/oc_network_interface.h"
#include "port/oc_storage.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define SLEEP_MS(ms) Sleep(ms)
#else
#include <unistd.h>
#include <pthread.h>
#define SLEEP_MS(ms) usleep((ms) * 1000)
#endif

/* ── Shutdown flag ─────────────────────────────────────────────────────── */
static volatile int g_quit = 0;

static void handle_signal(int sig)
{
  (void)sig;
  g_quit = 1;
}

/* ── Event-loop synchronization ────────────────────────────────────────── */
#ifdef _WIN32
static CRITICAL_SECTION g_cs;
static CONDITION_VARIABLE g_cv;
static volatile int g_signaled = 0;

static void signal_event_loop(void)
{
  EnterCriticalSection(&g_cs);
  g_signaled = 1;
  WakeConditionVariable(&g_cv);
  LeaveCriticalSection(&g_cs);
}

static void wait_for_event(oc_clock_time_t ticks)
{
  EnterCriticalSection(&g_cs);
  if (!g_signaled) {
    DWORD ms;
    if (ticks > 0) {
      oc_clock_time_t now = oc_clock_time();
      ms = (now < ticks) ? (DWORD)((ticks - now) * 1000 / OC_CLOCK_SECOND) : 0;
    } else {
      ms = 100;
    }
    SleepConditionVariableCS(&g_cv, &g_cs, ms);
  }
  g_signaled = 0;
  LeaveCriticalSection(&g_cs);
}
#else
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;
static volatile int g_signaled = 0;

static void signal_event_loop(void)
{
  pthread_mutex_lock(&g_mutex);
  g_signaled = 1;
  pthread_cond_signal(&g_cond);
  pthread_mutex_unlock(&g_mutex);
}

static void wait_for_event(oc_clock_time_t next_event)
{
  pthread_mutex_lock(&g_mutex);
  if (!g_signaled) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long ms;
    if (next_event > 0) {
      oc_clock_time_t now = oc_clock_time();
      ms = (now < next_event) ? (long)((next_event - now) * 1000 / OC_CLOCK_SECOND) : 0;
    } else {
      ms = 100; /* no pending timers, poll every 100 ms */
    }
    ts.tv_sec += ms / 1000;
    ts.tv_nsec += (ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
      ts.tv_sec++;
      ts.tv_nsec -= 1000000000L;
    }
    pthread_cond_timedwait(&g_cond, &g_mutex, &ts);
  }
  g_signaled = 0;
  pthread_mutex_unlock(&g_mutex);
}
#endif

/* ── Datapoint values ──────────────────────────────────────────────────── */
static bool g_dp1 = false; /* LSAB SOO /p/1 */
static bool g_dp2 = false; /* LSAB IOO /p/2 */
static bool g_dp3 = false; /* LSSB SOO /p/3 */
static bool g_dp4 = false; /* LSSB IOO /p/4 */
static int  g_param = 0;   /* Test param  /p/p1 */

/* ── Datapoint handlers ────────────────────────────────────────────────── */
static void get_bool_dp(oc_request_t *request,
                        oc_interface_mask_t iface_mask, void *user_data)
{
  (void)iface_mask;
  bool *val = (bool *)user_data;
  oc_rep_begin_root_object();
  if (oc_query_value_exists(request, "m") != -1) {
    char *m_value;
    oc_get_query_value(request, "m", &m_value);
    if (strncmp(m_value, "dpt", 3) == 0 || strncmp(m_value, "*", 1) == 0) {
      oc_rep_text_set_text_string(root, dpt, oc_string(request->resource->dpt));
    }
  } else {
    oc_rep_i_set_boolean(root, 1, *val);
  }
  oc_rep_end_root_object();
  oc_prepare_cbor_response(request, OC_STATUS_OK);
}

static void put_bool_dp(oc_request_t *request,
                        oc_interface_mask_t iface_mask, void *user_data)
{
  (void)iface_mask;
  bool *val = (bool *)user_data;
  oc_rep_t *rep = request->request_payload;
  while (rep) {
    if (rep->type == OC_REP_BOOL && rep->iname == 1) {
      *val = rep->value.boolean;
    }
    rep = rep->next;
  }
  oc_prepare_cbor_response(request, OC_STATUS_CHANGED);
}

static void get_int_dp(oc_request_t *request,
                       oc_interface_mask_t iface_mask, void *user_data)
{
  (void)iface_mask;
  int *val = (int *)user_data;
  oc_rep_begin_root_object();
  if (oc_query_value_exists(request, "m") != -1) {
    char *m_value;
    int m_len = oc_get_query_value(request, "m", &m_value);
    oc_string_t sn = oc_core_get_device_info()->serialnumber;
    const char *sn_str = oc_string(sn);
    const char *uri_str = oc_string(request->resource->uri);
    if (m_len == 1 && m_value[0] == '*') {
      /* Full metadata */
      char id_buf[128];
      snprintf(id_buf, sizeof(id_buf), "knx://sn:%s%s", sn_str, uri_str);
      oc_rep_i_set_text_string(root, 0, id_buf);
      oc_rep_i_set_int(root, 1, *val);
      oc_rep_text_set_text_string(root, dpt,
                                  oc_string(request->resource->dpt));
      oc_rep_text_set_text_string(root, href, uri_str);
    } else if (m_len >= 2 && strncmp(m_value, "id", 2) == 0) {
      char id_buf[128];
      snprintf(id_buf, sizeof(id_buf), "knx://sn:%s%s", sn_str, uri_str);
      oc_rep_i_set_text_string(root, 0, id_buf);
    } else if (m_len >= 3 && strncmp(m_value, "dpt", 3) == 0) {
      oc_rep_text_set_text_string(root, dpt,
                                  oc_string(request->resource->dpt));
    } else if (m_len >= 5 && strncmp(m_value, "value", 5) == 0) {
      oc_rep_i_set_int(root, 1, *val);
    } else {
      oc_rep_i_set_int(root, 1, *val);
    }
  } else {
    oc_rep_i_set_int(root, 1, *val);
  }
  oc_rep_end_root_object();
  oc_prepare_cbor_response(request, OC_STATUS_OK);
}

static void put_int_dp(oc_request_t *request,
                       oc_interface_mask_t iface_mask, void *user_data)
{
  (void)iface_mask;
  int *val = (int *)user_data;
  oc_rep_t *rep = request->request_payload;
  while (rep) {
    if (rep->type == OC_REP_INT && rep->iname == 1) {
      *val = (int)rep->value.integer;
    }
    rep = rep->next;
  }
  oc_prepare_cbor_response(request, OC_STATUS_CHANGED);
}

/* ── Stack callbacks ───────────────────────────────────────────────────── */

/* ── Test control callbacks (app-specific, used by oc_test_control) ──── */
static void test_set_dp(const char *path, bool value, bool has_value)
{
  if (has_value) {
    if (strcmp(path, "/p/1") == 0) g_dp1 = value;
    else if (strcmp(path, "/p/2") == 0) g_dp2 = value;
    else if (strcmp(path, "/p/3") == 0) g_dp3 = value;
    else if (strcmp(path, "/p/4") == 0) g_dp4 = value;
  } else {
    if (strcmp(path, "/p/1") == 0) g_dp1 = !g_dp1;
    else if (strcmp(path, "/p/2") == 0) g_dp2 = !g_dp2;
    else if (strcmp(path, "/p/3") == 0) g_dp3 = !g_dp3;
    else if (strcmp(path, "/p/4") == 0) g_dp4 = !g_dp4;
  }
}

static void test_reset_dp(void)
{
  g_dp1 = false;
  g_dp2 = false;
  g_dp3 = false;
  g_dp4 = false;
  g_param = 0;
}

/* ── SWU (Software Update) callbacks ───────────────────────────────────── */

/* Track download progress */
static size_t swu_total_size = 0;
static size_t swu_received = 0;

/* Called by stack for each block received at /a/swu */
static void swu_cb(oc_separate_response_t* response, size_t binary_size,
                    size_t block_offset, const uint8_t* block_data,
                    size_t block_len, void* data)
{
  (void)data;
  (void)response;
  (void)block_offset;

  if (binary_size > 0 && swu_total_size == 0) {
    swu_total_size = binary_size;
    swu_received = 0;
    printf("[swu] Starting download, total size: %zu bytes\n", swu_total_size);
    fflush(stdout);
  }

  /* Write block to file (append mode) */
  FILE* f = fopen("downloaded_bin", "ab");
  if (f) {
    fwrite(block_data, 1, block_len, f);
    fclose(f);
  }

  swu_received += block_len;
  printf("[swu] Received %zu / %zu bytes\n", swu_received, swu_total_size);
  fflush(stdout);

  /* Detect download completion — set package metadata */
  if (swu_total_size > 0 && swu_received >= swu_total_size) {
    printf("[swu] Download complete, setting package version 0.0.2\n");
    fflush(stdout);
    oc_swu_set_package_version(0, 0, 2);
    oc_swu_set_package_name("firmware.bin");
    swu_total_size = 0;
    swu_received = 0;
  }
}

/* Called after simulated upgrade delay */
static oc_event_callback_retval_t swu_upgrade_complete_cb(void* data)
{
  (void)data;
  printf("[swu] Upgrade complete, setting FWV to 0.0.2\n");
  fflush(stdout);

  /* Verify downloaded file exists */
  FILE* f = fopen("downloaded_bin", "rb");
  if (!f) {
    oc_swu_set_state(OC_SWU_STATE_IDLE);
    oc_swu_set_result(OC_SWU_RESULT_ERR_ICF);
    return OC_EVENT_DONE;
  }
  fclose(f);

  /* Update firmware version to 0.0.2 */
  oc_core_set_device_fwv(0, 0, 2);

  /* Cleanup */
  remove("downloaded_bin");
  oc_swu_set_package_name("");
  oc_swu_set_package_version(0, 0, 0);
  oc_swu_set_package_bytes(0);

  /* Complete state machine transition */
  oc_swu_set_state(OC_SWU_STATE_IDLE);
  oc_swu_set_result(OC_SWU_RESULT_SUCCESS);
  oc_swu_set_result(OC_SWU_RESULT_INIT);

  return OC_EVENT_DONE;
}

/* Called by stack when PUT /swu/update is received */
static void swu_upgrade_cb(int defer_time, void* data)
{
  (void)data;
  printf("[swu] Upgrade triggered, defer_time=%d\n", defer_time);
  fflush(stdout);
  oc_set_delayed_callback(NULL, swu_upgrade_complete_cb, 2);
}

static int app_init(void)
{
  /* Same values as EITT test template */
  oc_core_set_device("00fa10020800", "RuntimeTestServer");
  oc_core_set_device_hwv(0, 0, 1);
  oc_core_set_device_fwv(0, 0, 1);
  oc_core_set_device_hwt("Linux");
  oc_core_set_device_model("KNX Certification");
  oc_core_set_device_mid(667);

  /* Set manufacturing date for /swu/lastupdate (same as EITT template) */
  oc_swu_set_last_update("2020-04-12T23:20:50.52Z");
  /* Set hardware reference for /swu/hwref (same as EITT template) */
  oc_swu_set_hwref("0102030405ABCDEF");
  return 0;
}

static void register_resources(void)
{
  oc_resource_t *res;

  /* ── LSAB (FB 417, instance 1) ── */

  /* /p/1 — SOO (Switch On/Off) control */
  res = oc_new_resource("/p/1", 1);
  oc_resource_bind_resource_type(res, "urn:knx:dpa.417.61");
  oc_resource_bind_dpt(res, ":dpt.switch");
  oc_resource_bind_content_type(res, APPLICATION_CBOR, CONTENT_NONE);
  oc_resource_set_functional_block_data(res, 417, 1, 2);
  oc_resource_set_properties(res, OC_DISCOVERABLE);
  oc_resource_set_request_handler(res, COAP_GET, get_bool_dp, &g_dp1,
                                  OC_ACL_I, OC_IF_I);
  oc_resource_set_request_handler(res, COAP_PUT, put_bool_dp, &g_dp1,
                                  OC_ACL_I, OC_IF_I);
  oc_add_resource(res);

  /* /p/2 — IOO (Indicate On/Off) status — output interface (matches EITT) */
  res = oc_new_resource("/p/2", 1);
  oc_resource_bind_resource_type(res, "urn:knx:dpa.417.62");
  oc_resource_bind_dpt(res, ":dpt.switch");
  oc_resource_bind_content_type(res, APPLICATION_CBOR, CONTENT_NONE);
  oc_resource_set_functional_block_data(res, 417, 1, 2);
  oc_resource_set_properties(res, OC_DISCOVERABLE);
  oc_resource_set_request_handler(res, COAP_GET, get_bool_dp, &g_dp2,
                                  OC_ACL_O, OC_IF_O);
  oc_add_resource(res);

  /* ── LSSB (FB 421, instance 1) ── */

  /* /p/3 — SOO control — output interface for LSSB (matches EITT) */
  res = oc_new_resource("/p/3", 1);
  oc_resource_bind_resource_type(res, "urn:knx:dpa.421.61");
  oc_resource_bind_dpt(res, ":dpt.switch");
  oc_resource_bind_content_type(res, APPLICATION_CBOR, CONTENT_NONE);
  oc_resource_set_functional_block_data(res, 421, 1, 2);
  oc_resource_set_properties(res, OC_DISCOVERABLE);
  oc_resource_set_request_handler(res, COAP_GET, get_bool_dp, &g_dp3,
                                  OC_ACL_O, OC_IF_O);
  oc_add_resource(res);

  /* /p/4 — IOO status */
  res = oc_new_resource("/p/4", 1);
  oc_resource_bind_resource_type(res, "urn:knx:dpa.421.62");
  oc_resource_bind_dpt(res, ":dpt.switch");
  oc_resource_bind_content_type(res, APPLICATION_CBOR, CONTENT_NONE);
  oc_resource_set_functional_block_data(res, 421, 1, 2);
  oc_resource_set_properties(res, OC_DISCOVERABLE);
  oc_resource_set_request_handler(res, COAP_GET, get_bool_dp, &g_dp4,
                                  OC_ACL_I, OC_IF_I);
  oc_resource_set_request_handler(res, COAP_PUT, put_bool_dp, &g_dp4,
                                  OC_ACL_I, OC_IF_I);
  oc_add_resource(res);

  /* ── Test parameter (FB 65500, instance 0) ── */
  res = oc_new_resource("/p/p1", 1);
  oc_resource_bind_resource_type(res, "urn:knx:dpa.65500.201");
  oc_resource_bind_dpt(res, ":dpt.propDataType");
  oc_resource_bind_content_type(res, APPLICATION_CBOR, CONTENT_NONE);
  oc_resource_set_properties(res, OC_DISCOVERABLE | OC_WRITE_AFFECTS_FP);
  oc_resource_set_request_handler(res, COAP_GET, get_int_dp, &g_param,
                                  OC_ACL_D, OC_IF_D);
  oc_resource_set_request_handler(res, COAP_PUT, put_int_dp, &g_param,
                                  OC_ACL_P, OC_IF_P);
  oc_add_resource(res);

  /* ── Test control endpoints (reusable module — oc_test_control.c) ── */
  oc_test_control_register(test_set_dp, test_reset_dp);
}

/* ── Password callback (required by the stack for SPAKE2+) ─────────────── */
extern const char *app_get_password(void);
const char *app_get_password(void)
{
  return "2X4W3TE0DFLLS19Y1FCH";
}

/* ── Restart callback (resets datapoints to defaults, like demos do) ──── */
static void restart_cb(void *data)
{
  (void)data;
  printf("[test-ctrl] Restart callback: resetting datapoints to defaults\n");
  fflush(stdout);
  test_reset_dp();
}

/* ── Main ──────────────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
  (void)argc;
  (void)argv;

  signal(SIGINT, handle_signal);
#ifndef _WIN32
  signal(SIGTERM, handle_signal);
#endif

#ifdef _WIN32
  InitializeCriticalSection(&g_cs);
  InitializeConditionVariable(&g_cv);
#endif

  /* Clean storage for repeatable tests */
  oc_storage_config("runtime_test_storage_00fa10020800");

  /* Remove stale firmware file from previous runs */
  remove("downloaded_bin");

  static const oc_handler_t handler = {
    .init = app_init,
    .signal_event_loop = signal_event_loop,
#ifdef OC_SERVER
    .register_resources = register_resources,
#endif
  };

  /* Register restart callback (called by oc_knx_device_restart) */
  oc_set_restart_cb(restart_cb, NULL);

  /* Register SWU callbacks (must be before oc_main_init) */
  oc_set_swu_cb(swu_cb, NULL);
  oc_set_swu_upgrade_cb(swu_upgrade_cb, NULL);

  if (oc_main_init(&handler) < 0) {
    fprintf(stderr, "ERROR: oc_main_init failed\n");
    return 1;
  }

  /* Optional: bind to a specific network interface (e.g. veth-dut in CI).
   * Set DUT_IFACE=<ifname> to restrict the stack to that interface.
   * This enables multicast testing via a veth pair without code changes. */
  const char *iface_env = getenv("DUT_IFACE");
  if (iface_env && iface_env[0]) {
    oc_network_interface_info_t ifs[16];
    int n = oc_network_enumerate_interfaces(ifs, 16);
    for (int i = 0; i < n; i++) {
      if (strcmp(ifs[i].name, iface_env) == 0) {
        printf("[test-server] Binding to interface '%s' (index %u)\n",
               ifs[i].name, (unsigned)ifs[i].if_index);
        oc_network_set_interface_filter(ifs[i].if_index);
        oc_network_refresh_endpoints();
        break;
      }
    }
  }

  /* Find the unicast port the stack bound to */
  uint16_t unicast_port = 0;
  const oc_endpoint_t *ep = oc_connectivity_get_endpoints();
  while (ep) {
    if ((ep->flags & IPV6) && !(ep->flags & MULTICAST)) {
      unicast_port = ep->addr.ipv6.port;
      break;
    }
    ep = ep->next;
  }

  /* Signal to the test runner that we are ready */
  printf("RUNTIME_TEST_SERVER_READY port=%u\n", (unsigned)unicast_port);
  fflush(stdout);

  /* Event loop */
  while (!g_quit) {
    oc_clock_time_t next = oc_main_poll();
    wait_for_event(next);
  }

  oc_main_shutdown();

#ifdef _WIN32
  DeleteCriticalSection(&g_cs);
#endif

  return 0;
}
