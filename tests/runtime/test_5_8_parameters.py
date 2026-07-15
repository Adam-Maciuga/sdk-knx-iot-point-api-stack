"""
Runtime conformance tests — EITT 5.8 Parameters & Diagnostics

5.8.1.1  Write single parameter via POST (POST /p)
5.8.2.1  Read list of parameter/diagnostic paths (GET /p)
5.8.3.1  Read single parameter/diagnostic property (GET /p/p1)
5.8.3.2  Read metadata for parameter/diagnostic property (GET /p/p1?m=...)
5.8.4.1  Write single parameter via PUT (PUT /p/p1)

Reference: 08_10_5 KNX IoT Point API Tests v01_01_01_AS
"""

import cbor2
import pytest

from coap_client import APPLICATION_CBOR, LINK_FORMAT
from conftest import (DEVICE_PASSWORD, ALL_SCOPES, DUT_SERIAL_NUMBER,
                      set_lsm, parse_link_format)





# ===========================================================================
# 5.8.1 — Write Single Parameter via POST
# ===========================================================================

class TestParameterPostWrite:
    """EITT 5.8.1.1: POST /p with [{href, value}] to write a parameter."""

    @pytest.fixture(autouse=True)
    def setup(self, coap, oscore_ctx):
        # EITT: no factory reset between 5.8 tests
        set_lsm(coap, oscore_ctx, 1)  # loading

    def test_5_8_1_1_write_parameter_via_post(self, coap, oscore_ctx):
        """POST /p with [{value: 1, href: '/p/p1'}] → 2.04, read back.

        EITT trace: POST /p with CBOR array using integer keys
        (1=value, 11=href), device responds 2.04.
        Step 2: GET /p/p1 verifies {1: 1}.
        """
        entry = [{1: 1, 11: "/p/p1"}]
        resp = coap.oscore_post(
            oscore_ctx, "/p", payload=cbor2.dumps(entry))
        assert resp is not None and resp.is_successful, (
            f"POST /p failed: {resp.code if resp else 'timeout'}")

        # Step 2: read back to verify
        resp2 = coap.oscore_get(oscore_ctx, "/p/p1")
        assert resp2 is not None and resp2.is_successful, (
            f"GET /p/p1 failed: {resp2.code if resp2 else 'timeout'}")
        data = cbor2.loads(resp2.payload)
        assert data.get(1) == 1, (
            f"Expected value=1, got {data.get(1)}")


# ===========================================================================
# 5.8.2 — Read List of Parameter Paths
# ===========================================================================

class TestParameterList:
    """Test GET /p returns link-format listing all datapoint paths."""

    @pytest.fixture(autouse=True)
    def setup(self, coap, oscore_ctx):
        # EITT: no factory reset between 5.8 tests
        pass

    def test_5_8_2_1_read_parameter_list(self, coap, oscore_ctx):
        """GET /p returns link-format listing including /p/p1 (parameter).

        DUT has /p/1, /p/2, /p/3, /p/4 (bool datapoints) plus
        /p/p1 (int parameter).  All should appear in link-format.
        """
        resp = coap.oscore_get(
            oscore_ctx, "/p", accept=LINK_FORMAT)
        assert resp is not None and resp.is_successful, (
            f"GET /p failed: {resp.code if resp else 'timeout'}")
        assert resp.payload, "GET /p returned empty payload"

        links = parse_link_format(resp.payload)
        hrefs = [l["href"] for l in links]

        # Expect at least 5 resources (/p/1..4 + /p/p1)
        assert len(links) >= 5, (
            f"Expected ≥5 datapoint paths, got {len(links)}: {hrefs}")

        # /p/p1 (parameter) should be in the list
        assert any("/p/p1" in h for h in hrefs), (
            f"/p/p1 not found in parameter list: {hrefs}")

        # Each should have ct=60 (CBOR)
        for link in links:
            assert link.get("ct") == "60", (
                f"{link['href']} should have ct=60, got {link.get('ct')}")


# ===========================================================================
# 5.8.3 — Read Single Parameter
# ===========================================================================

class TestParameterRead:
    """Test GET /p/p1 returns CBOR with parameter value."""

    @pytest.fixture(autouse=True)
    def setup(self, coap, oscore_ctx):
        # EITT: no factory reset between 5.8 tests
        pass

    def test_5_8_3_1_read_parameter(self, coap, oscore_ctx):
        """GET /p/p1 returns {1: <int_value>}."""
        resp = coap.oscore_get(oscore_ctx, "/p/p1")
        assert resp is not None and resp.is_successful, (
            f"GET /p/p1 failed: {resp.code if resp else 'timeout'}")
        assert resp.payload, "GET /p/p1 returned empty payload"

        data = cbor2.loads(resp.payload)
        assert 1 in data, (
            f"Response should contain key 1, got {data}")
        assert isinstance(data[1], int), (
            f"Parameter value should be int, got {type(data[1])}")


# ===========================================================================
# 5.8.3.2 — Read Metadata for Parameter
# ===========================================================================

class TestParameterMetadata:
    """EITT 5.8.3.2: GET /p/p1?m=... returns metadata fields."""

    @pytest.fixture(autouse=True)
    def setup(self, coap, oscore_ctx):
        # EITT: no factory reset between 5.8 tests
        pass

    def test_5_8_3_2_read_all_metadata(self, coap, oscore_ctx):
        """GET /p/p1?m=* returns full metadata (id, value, dpt, href, if, rt).

        EITT trace step 1: GET /p/p1?m=* → 2.05 with id, value, dpt,
        href, if, rt.
        """
        resp = coap.oscore_get(oscore_ctx, "/p/p1?m=*")
        assert resp is not None and resp.is_successful, (
            f"GET /p/p1?m=* failed: {resp.code if resp else 'timeout'}")
        assert resp.payload, "GET /p/p1?m=* returned empty payload"
        data = cbor2.loads(resp.payload)

        # Must have "id" (key 0 or string "id")
        has_id = (0 in data) or ("id" in data)
        assert has_id, f"Missing 'id' in metadata: {data}"

        # Must have "href" (key "href" or 11)
        has_href = ("href" in data) or (11 in data)
        assert has_href, f"Missing 'href' in metadata: {data}"

    def test_5_8_3_2_read_metadata_id(self, coap, oscore_ctx):
        """GET /p/p1?m=id returns only the id field.

        EITT trace step 2: GET /p/p1?m=id → {id: 'knx://sn:.../p/p1'}.
        """
        resp = coap.oscore_get(oscore_ctx, "/p/p1?m=id")
        assert resp is not None and resp.is_successful, (
            f"GET /p/p1?m=id failed: {resp.code if resp else 'timeout'}")
        assert resp.payload, "GET /p/p1?m=id returned empty payload"
        data = cbor2.loads(resp.payload)
        expected = {0: f"knx://sn:{DUT_SERIAL_NUMBER}/p/p1"}
        assert data == expected, (
            f"Expected only the integer-keyed id {expected}, got {data}")

    def test_5_8_3_2_read_metadata_value(self, coap, oscore_ctx):
        """GET /p/p1?m=value returns only the value field.

        EITT trace step 3: GET /p/p1?m=value → {value: <int>}.
        """
        resp = coap.oscore_get(oscore_ctx, "/p/p1?m=value")
        assert resp is not None and resp.is_successful, (
            f"GET /p/p1?m=value failed: {resp.code if resp else 'timeout'}")
        assert resp.payload, "GET /p/p1?m=value returned empty payload"
        data = cbor2.loads(resp.payload)
        assert (1 in data) or ("value" in data), (
            f"Response should contain 'value': {data}")


# ===========================================================================
# 5.8.4 — Write Single Parameter
# ===========================================================================

class TestParameterWrite:
    """Test PUT /p/p1 updates the parameter value."""

    @pytest.fixture(autouse=True)
    def setup(self, coap, oscore_ctx):
        # EITT: no factory reset between 5.8 tests
        pass

    def test_5_8_4_1_write_parameter(self, coap, oscore_ctx):
        """PUT /p/p1 with {1: 42} → 2.04, read back verifies value."""
        test_value = 42
        payload = cbor2.dumps({1: test_value})
        resp = coap.oscore_put(
            oscore_ctx, "/p/p1", payload=payload)
        assert resp is not None and resp.is_successful, (
            f"PUT /p/p1 failed: {resp.code if resp else 'timeout'}")

        # Read back to verify
        resp = coap.oscore_get(oscore_ctx, "/p/p1")
        assert resp is not None and resp.is_successful, (
            f"GET /p/p1 after PUT failed: {resp.code if resp else 'timeout'}")
        data = cbor2.loads(resp.payload)
        assert data.get(1) == test_value, (
            f"Expected {test_value}, got {data.get(1)}")
