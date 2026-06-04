"""
Runtime conformance tests — EITT 5.2.1 / 5.2.2.3 / 5.2.3

5.2.1.1  Reading API version and base path
5.2.2.3  Restarting device
5.2.3.1  Setting Installation ID and Individual Address
5.2.3.1b Invalid GET request to /.well-known/knx/ia fails
5.2.3.2  Setting the Fabric ID alongside IA and IID
"""

import time

import cbor2
import pytest

from coap_client import APPLICATION_CBOR


# Original IA/IID set by conftest (for restoration after IA tests)
ORIGINAL_IA = 0x1101
ORIGINAL_IID = 0x1199887766


# ===========================================================================
#  5.2.1 — GET /.well-known/knx
# ===========================================================================

class TestApiVersion:
    """5.2.1.1: Reading API version and base path."""

    def test_5_2_1_1_api_version(self, coap, oscore_ctx):
        """GET /.well-known/knx → 2.05 with api version 1.0.0.

        EITT sends this unsecured, but our stack may require OSCORE.
        Try unsecured first; fall back to OSCORE.
        """
        # Try unsecured first (per EITT spec)
        resp = coap.get("/.well-known/knx", accept=APPLICATION_CBOR, timeout=5)
        if resp is None or not resp.is_successful:
            # Fall back to OSCORE (our stack may require auth)
            resp = coap.oscore_get(oscore_ctx, "/.well-known/knx")

        assert resp is not None, "GET /.well-known/knx timed out"
        assert resp.is_successful, (
            f"GET /.well-known/knx failed: {resp.code}")
        data = cbor2.loads(resp.payload)
        assert "api" in data, f"Missing 'api' key in response: {data}"
        api = data["api"]
        assert isinstance(api, dict), f"'api' must be a map, got {type(api)}"
        assert "version" in api, f"Missing 'version' in api: {api}"
        assert api["version"] == "1.0.0", (
            f"Expected API version '1.0.0', got '{api['version']}'")


# ===========================================================================
#  5.2.2.3 — Restarting device
# ===========================================================================

class TestRestart:
    """5.2.2.3: POST /.well-known/knx with restart command.

    After restart:
    - Programming mode resets to false
    - PASE token cleared (SPAKE2+ can be redone)
    - OSCORE contexts (ATs in storage) persist
    """

    def test_5_2_2_3_restart_pm_resets(self, coap, oscore_ctx):
        """Restart resets PM to false and OSCORE still works.

        EITT steps:
        1. Enable PM
        2. POST /.well-known/knx {2: "restart"} (NON)
        3. Wait for restart
        4. Verify PM is false (OSCORE still works)
        """
        # Step 1: Enable PM
        pm_on = cbor2.dumps({1: True})
        resp = coap.oscore_put(oscore_ctx, "/dev/pm", payload=pm_on)
        assert resp is not None and resp.is_successful, (
            f"PUT /dev/pm failed: {resp and resp.code}")

        # Verify PM is on
        resp = coap.oscore_get(oscore_ctx, "/dev/pm")
        assert resp is not None and resp.is_successful
        data = cbor2.loads(resp.payload)
        assert data.get(1) is True, "PM should be True before restart"

        # Step 2: Send restart via POST /.well-known/knx
        # EITT sends NON-confirmable — the server sends no response
        restart_payload = cbor2.dumps({2: "restart"})
        resp = coap.oscore_post(oscore_ctx, "/.well-known/knx",
                                payload=restart_payload, timeout=3)
        # Restart: server may not send a response (ignores request)

        # Step 3: Wait for restart to complete
        time.sleep(1.0)

        # Step 4: Verify PM reset and OSCORE still works
        resp = coap.oscore_get(oscore_ctx, "/dev/pm")
        assert resp is not None, "GET /dev/pm after restart timed out"
        assert resp.is_successful, (
            f"OSCORE should still work after restart, got {resp.code}")
        data = cbor2.loads(resp.payload)
        assert data.get(1) is False, (
            f"PM should be False after restart, got {data.get(1)}")


# ===========================================================================
#  5.2.3 — POST /.well-known/knx/ia
# ===========================================================================

class TestIaAssignment:
    """5.2.3: Individual Address assignment via POST /.well-known/knx/ia."""

    def _restore_ia(self, coap, oscore_ctx):
        """Restore IA/IID to conftest values."""
        payload = cbor2.dumps({12: ORIGINAL_IA, 26: ORIGINAL_IID})
        coap.oscore_post(oscore_ctx, "/.well-known/knx/ia", payload=payload)

    def test_5_2_3_1_set_ia_and_iid(self, coap, oscore_ctx):
        """5.2.3.1: Setting Installation ID and Individual Address.

        EITT steps:
        1. POST /.well-known/knx/ia {12: <ia>, 26: <iid>} → 2.04
        2. GET /dev/iid → verify IID
        3. GET /dev/sna → verify SNA (IA >> 8)
        4. GET /dev/da → verify DA (IA & 0xFF)
        """
        test_ia = 0x1203   # SNA=0x12=18, DA=0x03=3
        test_iid = 0x1122334455  # 73588229205

        try:
            # Step 1: POST IA and IID
            payload = cbor2.dumps({12: test_ia, 26: test_iid})
            resp = coap.oscore_post(oscore_ctx, "/.well-known/knx/ia",
                                    payload=payload, timeout=5)
            assert resp is not None, "POST /.well-known/knx/ia timed out"
            assert resp.is_successful, (
                f"POST /.well-known/knx/ia failed: {resp.code}")

            # Step 2: Verify IID
            resp = coap.oscore_get(oscore_ctx, "/dev/iid")
            assert resp is not None and resp.is_successful
            data = cbor2.loads(resp.payload)
            assert data.get(1) == test_iid, (
                f"IID mismatch: expected {test_iid}, got {data.get(1)}")

            # Step 3: Verify SNA
            resp = coap.oscore_get(oscore_ctx, "/dev/sna")
            assert resp is not None and resp.is_successful
            data = cbor2.loads(resp.payload)
            expected_sna = (test_ia >> 8) & 0xFF
            assert data.get(1) == expected_sna, (
                f"SNA mismatch: expected {expected_sna}, got {data.get(1)}")

            # Step 4: Verify DA
            resp = coap.oscore_get(oscore_ctx, "/dev/da")
            assert resp is not None and resp.is_successful
            data = cbor2.loads(resp.payload)
            expected_da = test_ia & 0xFF
            assert data.get(1) == expected_da, (
                f"DA mismatch: expected {expected_da}, got {data.get(1)}")
        finally:
            self._restore_ia(coap, oscore_ctx)

    def test_5_2_3_1b_get_ia_rejected(self, coap, oscore_ctx):
        """5.2.3.1b: GET /.well-known/knx/ia → 4.05 Method Not Allowed."""
        resp = coap.oscore_get(oscore_ctx, "/.well-known/knx/ia")
        assert resp is not None, "GET /.well-known/knx/ia timed out"
        assert not resp.is_successful, (
            f"GET /.well-known/knx/ia should fail, got {resp.code}")
        assert resp.code == "4.05", (
            f"Expected 4.05 Method Not Allowed, got {resp.code}")

    def test_5_2_3_2_set_ia_iid_and_fid(self, coap, oscore_ctx):
        """5.2.3.2: Setting Fabric ID alongside IA and IID.

        EITT steps:
        1. POST /.well-known/knx/ia {12: <ia>, 26: <iid>, 25: <fid>} → 2.04
        2. GET /dev/iid → verify IID
        3. GET /dev/sna → verify SNA
        4. GET /dev/da → verify DA
        5. GET /dev/fid → verify FID
        """
        test_ia = 0x4506   # SNA=0x45=69, DA=0x06=6
        test_iid = 0x5544332211  # 366216421905
        test_fid = 0x9988776655  # 659419522645

        try:
            # Step 1: POST IA, IID, and FID
            payload = cbor2.dumps({12: test_ia, 26: test_iid, 25: test_fid})
            resp = coap.oscore_post(oscore_ctx, "/.well-known/knx/ia",
                                    payload=payload, timeout=5)
            assert resp is not None, "POST /.well-known/knx/ia timed out"
            assert resp.is_successful, (
                f"POST /.well-known/knx/ia failed: {resp.code}")

            # Step 2: Verify IID
            resp = coap.oscore_get(oscore_ctx, "/dev/iid")
            assert resp is not None and resp.is_successful
            data = cbor2.loads(resp.payload)
            assert data.get(1) == test_iid, (
                f"IID mismatch: expected {test_iid}, got {data.get(1)}")

            # Step 3: Verify SNA
            resp = coap.oscore_get(oscore_ctx, "/dev/sna")
            assert resp is not None and resp.is_successful
            data = cbor2.loads(resp.payload)
            expected_sna = (test_ia >> 8) & 0xFF
            assert data.get(1) == expected_sna, (
                f"SNA mismatch: expected {expected_sna}, got {data.get(1)}")

            # Step 4: Verify DA
            resp = coap.oscore_get(oscore_ctx, "/dev/da")
            assert resp is not None and resp.is_successful
            data = cbor2.loads(resp.payload)
            expected_da = test_ia & 0xFF
            assert data.get(1) == expected_da, (
                f"DA mismatch: expected {expected_da}, got {data.get(1)}")

            # Step 5: Verify FID
            resp = coap.oscore_get(oscore_ctx, "/dev/fid")
            assert resp is not None and resp.is_successful
            data = cbor2.loads(resp.payload)
            assert data.get(1) == test_fid, (
                f"FID mismatch: expected {test_fid}, got {data.get(1)}")
        finally:
            self._restore_ia(coap, oscore_ctx)
