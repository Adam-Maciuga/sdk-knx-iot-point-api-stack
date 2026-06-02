"""
Runtime conformance tests — EITT 5.6 Application Program

5.6.1.1  LSM startLoading transition  (POST /a/lsm {2:1} → state 2)
5.6.1.2  LSM loadComplete transition   (POST /a/lsm {2:2} → state 1)
5.6.1.3  LSM unload transition         (POST /a/lsm {2:4} → state 0)
5.6.1.4  LSM state persists over reboot
5.6.2.1  GET /a/lsm returns current status
5.6.3.1  GET /ap → link-format with /a/lsm and /ap/pv
5.6.4.1  Write and Read /ap/pv (program version)

Reference: 08_10_5 KNX IoT Point API Tests v01_01_01_AS
"""

import time

import cbor2
import pytest

from coap_client import APPLICATION_CBOR, LINK_FORMAT
from conftest import (DUT_IA, DUT_IID, DEVICE_PASSWORD, ALL_SCOPES,
                      set_lsm, parse_link_format)


def _get_lsm(coap, oscore_ctx):
    """Read current LSM state (key 3)."""
    resp = coap.oscore_get(oscore_ctx, "/a/lsm")
    assert resp is not None and resp.is_successful, (
        f"GET /a/lsm failed: {resp.code if resp else 'timeout'}")
    data = cbor2.loads(resp.payload)
    return data.get(3)



# ===========================================================================
# 5.6.1 — LSM State Transitions
# ===========================================================================

class TestLsmTransitions:
    """Test POST /a/lsm with startLoading / loadComplete / unload commands."""

    @pytest.fixture(autouse=True)
    def setup(self, coap, oscore_ctx):
        # EITT: no factory reset between 5.6 tests
        pass

    # ---- 5.6.1.1 startLoading ----

    def test_5_6_1_1_start_loading(self, coap, oscore_ctx):
        """POST {2:1} (startLoading) → 2.04, state becomes 2 (loading)."""
        # Step 1: POST startLoading
        resp = coap.oscore_post(
            oscore_ctx, "/a/lsm", payload=cbor2.dumps({2: 1}))
        assert resp is not None, "No response to POST /a/lsm cmd=1"
        assert resp.code == "2.04", f"Expected 2.04, got {resp.code}"

        # Response payload mirrors back the new state
        data = cbor2.loads(resp.payload)
        assert data.get(3) == 2, (
            f"POST response should report state=2 (loading), got {data}")

        # Step 2: Verify via GET
        status = _get_lsm(coap, oscore_ctx)
        assert status == 2, (
            f"GET /a/lsm should return 2 (loading), got {status}")

    # ---- 5.6.1.2 loadComplete ----

    def test_5_6_1_2_load_complete(self, coap, oscore_ctx):
        """POST {2:2} (loadComplete) after startLoading → state 1 (loaded)."""
        # Setup: go to loading state first
        set_lsm(coap, oscore_ctx, 1)  # startLoading

        # Step 1: POST loadComplete
        resp = coap.oscore_post(
            oscore_ctx, "/a/lsm", payload=cbor2.dumps({2: 2}))
        assert resp is not None, "No response to POST /a/lsm cmd=2"
        assert resp.code == "2.04", f"Expected 2.04, got {resp.code}"

        data = cbor2.loads(resp.payload)
        assert data.get(3) == 1, (
            f"POST response should report state=1 (loaded), got {data}")

        # Step 2: Verify via GET
        status = _get_lsm(coap, oscore_ctx)
        assert status == 1, (
            f"GET /a/lsm should return 1 (loaded), got {status}")

    # ---- 5.6.1.3 unload ----

    def test_5_6_1_3_unload(self, coap, oscore_ctx):
        """POST {2:4} (unload) after loaded → state 0 (unloaded)."""
        # Setup: go to loaded state
        set_lsm(coap, oscore_ctx, 1)  # startLoading
        set_lsm(coap, oscore_ctx, 2)  # loadComplete → loaded

        # Step 1: POST unload
        resp = coap.oscore_post(
            oscore_ctx, "/a/lsm", payload=cbor2.dumps({2: 4}))
        assert resp is not None, "No response to POST /a/lsm cmd=4"
        assert resp.code == "2.04", f"Expected 2.04, got {resp.code}"

        data = cbor2.loads(resp.payload)
        assert data.get(3) == 0, (
            f"POST response should report state=0 (unloaded), got {data}")

        # Step 2: Verify via GET
        status = _get_lsm(coap, oscore_ctx)
        assert status == 0, (
            f"GET /a/lsm should return 0 (unloaded), got {status}")

    # ---- 5.6.1.4 LSM persists over reboot ----

    def test_5_6_1_4_lsm_persist_loading(self, coap, oscore_ctx):
        """LSM loading state (2) persists over reboot."""
        set_lsm(coap, oscore_ctx, 1)  # startLoading → state=2
        status = _get_lsm(coap, oscore_ctx)
        assert status == 2, f"Pre-reboot: expected 2, got {status}"

        # Restart device
        resp = coap.oscore_post(
            oscore_ctx, "/test/restart", payload=cbor2.dumps({}))
        assert resp is not None and resp.is_successful
        time.sleep(3)
        coap.drain_socket(timeout=0.5)

        # Re-establish OSCORE (restart clears PASE, but AT persists)
        status = _get_lsm(coap, oscore_ctx)
        assert status == 2, (
            f"After reboot, LSM should still be 2 (loading), got {status}")

    def test_5_6_1_4_lsm_persist_loaded(self, coap, oscore_ctx):
        """LSM loaded state (1) persists over reboot."""
        set_lsm(coap, oscore_ctx, 1)  # startLoading
        set_lsm(coap, oscore_ctx, 2)  # loadComplete → loaded
        status = _get_lsm(coap, oscore_ctx)
        assert status == 1, f"Pre-reboot: expected 1, got {status}"

        resp = coap.oscore_post(
            oscore_ctx, "/test/restart", payload=cbor2.dumps({}))
        assert resp is not None and resp.is_successful
        time.sleep(3)
        coap.drain_socket(timeout=0.5)

        status = _get_lsm(coap, oscore_ctx)
        assert status == 1, (
            f"After reboot, LSM should still be 1 (loaded), got {status}")

    def test_5_6_1_4_lsm_persist_unloaded(self, coap, oscore_ctx):
        """LSM unloaded state (0) persists over reboot."""
        # EITT Step 3: explicit unload (previous test left device loaded)
        set_lsm(coap, oscore_ctx, 4)
        status = _get_lsm(coap, oscore_ctx)
        assert status == 0, f"Pre-reboot: expected 0, got {status}"

        resp = coap.oscore_post(
            oscore_ctx, "/test/restart", payload=cbor2.dumps({}))
        assert resp is not None and resp.is_successful
        time.sleep(3)
        coap.drain_socket(timeout=0.5)

        status = _get_lsm(coap, oscore_ctx)
        assert status == 0, (
            f"After reboot, LSM should still be 0 (unloaded), got {status}")


# ===========================================================================
# 5.6.2 — Read LSM Status
# ===========================================================================

class TestLsmRead:
    """Test GET /a/lsm returns correct status values."""

    @pytest.fixture(autouse=True)
    def setup(self, coap, oscore_ctx):
        # EITT: no factory reset between 5.6 tests
        pass

    def test_5_6_2_1_read_lsm_all_states(self, coap, oscore_ctx):
        """GET /a/lsm returns {3: <state>} for each LSM state."""
        # State 0 (unloaded) — default after factory reset
        status = _get_lsm(coap, oscore_ctx)
        assert status == 0, f"Expected state 0 (unloaded), got {status}"

        # Transition to loading (2)
        set_lsm(coap, oscore_ctx, 1)
        status = _get_lsm(coap, oscore_ctx)
        assert status == 2, f"Expected state 2 (loading), got {status}"

        # Transition to loaded (1)
        set_lsm(coap, oscore_ctx, 2)
        status = _get_lsm(coap, oscore_ctx)
        assert status == 1, f"Expected state 1 (loaded), got {status}"

        # Transition to unloaded (0)
        set_lsm(coap, oscore_ctx, 4)
        status = _get_lsm(coap, oscore_ctx)
        assert status == 0, f"Expected state 0 (unloaded), got {status}"


# ===========================================================================
# 5.6.3 — Application Program Resource Listing
# ===========================================================================

class TestAppProgramList:
    """Test GET /ap returns link-format with /a/lsm and /ap/pv."""

    @pytest.fixture(autouse=True)
    def setup(self, coap, oscore_ctx):
        # EITT: no factory reset between 5.6 tests
        pass

    def test_5_6_3_1_read_ap_list(self, coap, oscore_ctx):
        """GET /ap returns link-format listing /a/lsm and /ap/pv."""
        resp = coap.oscore_get(
            oscore_ctx, "/ap", accept=LINK_FORMAT)
        assert resp is not None and resp.is_successful, (
            f"GET /ap failed: {resp.code if resp else 'timeout'}")
        assert resp.payload, "GET /ap returned empty payload"

        links = parse_link_format(resp.payload)
        hrefs = [l["href"] for l in links]

        # /a/lsm must be present with ct=60 (application/cbor)
        assert "/a/lsm" in hrefs, (
            f"/a/lsm not found in response: {hrefs}")
        lsm_link = next(l for l in links if l["href"] == "/a/lsm")
        assert lsm_link.get("ct") == "60", (
            f"/a/lsm should have ct=60, got {lsm_link.get('ct')}")

        # /ap/pv must be present with rt containing dpa.3.13 and ct=60
        assert "/ap/pv" in hrefs, (
            f"/ap/pv not found in response: {hrefs}")
        pv_link = next(l for l in links if l["href"] == "/ap/pv")
        assert pv_link.get("ct") == "60", (
            f"/ap/pv should have ct=60, got {pv_link.get('ct')}")
        rt_val = pv_link.get("rt", "")
        assert "dpa.3.13" in rt_val, (
            f"/ap/pv rt should contain 'dpa.3.13', got '{rt_val}'")


# ===========================================================================
# 5.6.4 — Program Version
# ===========================================================================

class TestProgramVersion:
    """Test read/write of /ap/pv (program version)."""

    @pytest.fixture(autouse=True)
    def setup(self, coap, oscore_ctx):
        # EITT: no factory reset between 5.6 tests
        pass

    def test_5_6_4_1_write_read_pv(self, coap, oscore_ctx):
        """PUT /ap/pv with [major, minor, patch], read back via GET."""
        # Must be in loading state to write
        set_lsm(coap, oscore_ctx, 1)

        version = [1, 2, 3]
        payload = cbor2.dumps({1: version})
        resp = coap.oscore_put(
            oscore_ctx, "/ap/pv", payload=payload)
        assert resp is not None and resp.is_successful, (
            f"PUT /ap/pv failed: {resp.code if resp else 'timeout'}")

        # Read back
        resp = coap.oscore_get(oscore_ctx, "/ap/pv")
        assert resp is not None and resp.is_successful, (
            f"GET /ap/pv failed: {resp.code if resp else 'timeout'}")
        data = cbor2.loads(resp.payload)
        assert data.get(1) == version, (
            f"Expected version {version}, got {data.get(1)}")
