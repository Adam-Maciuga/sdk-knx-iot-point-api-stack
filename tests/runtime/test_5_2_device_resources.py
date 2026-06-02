"""
Runtime conformance tests — EITT 5.2.5 / 5.2.6 / 5.2.7

5.2.5.1  Read list of device data property-ids (GET /dev link-format)
5.2.6.1  Read specific device data for various property-ids
5.2.7.1  Write specific device data for various property-ids
5.2.7.1b Invalid PUT on read-only resource for various dev property-ids
"""

import re

import cbor2
import pytest

from coap_client import APPLICATION_CBOR, LINK_FORMAT


# ===========================================================================
#  5.2.5.1 — GET /dev (link-format listing)
# ===========================================================================

class TestDevResourceList:
    """5.2.5.1: Read list of device data property-ids."""

    # Mandatory /dev sub-resources per EITT
    MANDATORY_RESOURCES = [
        "dev/sn", "dev/fwv", "dev/hwt", "dev/model",
        "dev/sna", "dev/da", "dev/hname", "dev/ipv6",
        "dev/fid", "dev/iid", "dev/port", "dev/mport",
        "dev/pm", "dev/mid",
    ]

    def test_5_2_5_1_dev_link_format(self, coap, oscore_ctx):
        """GET /dev → 2.05 with link-format listing all sub-resources.

        EITT expects application/link-format response containing
        mandatory /dev sub-resources with rt= and ct= attributes.
        """
        resp = coap.oscore_get(oscore_ctx, "/dev",
                               accept=LINK_FORMAT)
        assert resp is not None, "GET /dev timed out"
        assert resp.is_successful, f"GET /dev failed: {resp.code}"
        body = resp.payload
        if isinstance(body, bytes):
            body = body.decode("utf-8", errors="replace")

        for res in self.MANDATORY_RESOURCES:
            assert res in body, (
                f"Missing mandatory resource '{res}' in /dev link-format: "
                f"{body[:200]}")

        # EITT validates rt= on specific resources
        # NOTE: EITT regex uses dot (e.g. ":dpa.0.11") but the regex dot
        # matches any char, so the stack's colon form (":dpa:0.11") also
        # passes EITT. We accept both forms here to match EITT behavior.
        # See: spec Table 16 uses colon in rt&dpt column but dot in
        # link-format examples. Stack currently uses colon for dev/sn
        # and dev/mid. Filed as known discrepancy.
        lines = [l.strip() for l in body.split(",") if l.strip()]
        eitt_rt_checks = {
            "dev/sn": r":dpa[.:]0\.11",
            "dev/fwv": r":dpa[.:]0\.25",
            "dev/model": r":dpa[.:]0\.15",
            "dev/sna": r":dpa[.:]0\.57",
            "dev/da": r":dpa[.:]0\.58",
            "dev/pm": r":dpa[.:]0\.54",
            "dev/mid": r":dpa[.:]0\.12",
        }
        for resource, rt_pattern in eitt_rt_checks.items():
            matching_lines = [l for l in lines if resource in l]
            if matching_lines:
                assert any(re.search(r'rt="' + rt_pattern + r'"', l)
                           for l in matching_lines), (
                    f"EITT: {resource} must have rt matching {rt_pattern}. "
                    f"Got: {matching_lines}")

        # EITT 'all:' rule: every line must have ct=(60 50|50 60|60)
        for line in lines:
            assert re.search(
                r'ct=((?:60 50)|(?:50 60)|(?:60)|'
                r'(?:"60 50")|(?:"50 60"))', line), (
                f"EITT: every /dev entry must have ct=60. Got: {line}")


# ===========================================================================
#  5.2.6.1 — GET specific /dev/* resources
# ===========================================================================

class TestDevResourceRead:
    """5.2.6.1: Read specific device data for various property-ids.

    Values must match runtime_test_server.c configuration.
    """

    def test_5_2_6_1_serial_number(self, coap, oscore_ctx):
        """GET /dev/sn → serial number 00fa10020800."""
        resp = coap.oscore_get(oscore_ctx, "/dev/sn")
        assert resp is not None and resp.is_successful, (
            f"GET /dev/sn failed: {resp and resp.code}")
        data = cbor2.loads(resp.payload)
        assert data.get(1) == "00fa10020800", (
            f"Serial number mismatch: {data.get(1)}")

    def test_5_2_6_1_firmware_version(self, coap, oscore_ctx):
        """GET /dev/fwv → firmware version [0, 0, 1]."""
        resp = coap.oscore_get(oscore_ctx, "/dev/fwv")
        assert resp is not None and resp.is_successful
        data = cbor2.loads(resp.payload)
        fwv = data.get(1)
        assert isinstance(fwv, list) and len(fwv) == 3, (
            f"FWV must be [major, minor, patch], got {fwv}")
        assert fwv == [0, 0, 1], (
            f"FWV should be [0, 0, 1], got {fwv}")

    def test_5_2_6_1_hardware_type(self, coap, oscore_ctx):
        """GET /dev/hwt → hardware type string."""
        resp = coap.oscore_get(oscore_ctx, "/dev/hwt")
        assert resp is not None and resp.is_successful
        data = cbor2.loads(resp.payload)
        assert isinstance(data.get(1), str), (
            f"HWT must be a string, got {type(data.get(1))}")

    def test_5_2_6_1_model(self, coap, oscore_ctx):
        """GET /dev/model → 'KNX Certification'."""
        resp = coap.oscore_get(oscore_ctx, "/dev/model")
        assert resp is not None and resp.is_successful
        data = cbor2.loads(resp.payload)
        assert data.get(1) == "KNX Certification", (
            f"Model mismatch: {data.get(1)}")

    def test_5_2_6_1_sna(self, coap, oscore_ctx):
        """GET /dev/sna → subnet address 17 (0x1101 >> 8 = 0x11 = 17)."""
        resp = coap.oscore_get(oscore_ctx, "/dev/sna")
        assert resp is not None and resp.is_successful, (
            f"GET /dev/sna failed: {resp and resp.code}")
        data = cbor2.loads(resp.payload)
        assert data.get(1) == 17, (
            f"SNA should be 17, got {data.get(1)}")

    def test_5_2_6_1_da(self, coap, oscore_ctx):
        """GET /dev/da → device address 1 (0x1101 & 0xFF = 0x01 = 1)."""
        resp = coap.oscore_get(oscore_ctx, "/dev/da")
        assert resp is not None and resp.is_successful, (
            f"GET /dev/da failed: {resp and resp.code}")
        data = cbor2.loads(resp.payload)
        assert data.get(1) == 1, (
            f"DA should be 1, got {data.get(1)}")

    def test_5_2_6_1_iid(self, coap, oscore_ctx):
        """GET /dev/iid → installation ID."""
        resp = coap.oscore_get(oscore_ctx, "/dev/iid")
        assert resp is not None and resp.is_successful
        data = cbor2.loads(resp.payload)
        assert isinstance(data.get(1), int), (
            f"IID must be int, got {type(data.get(1))}")

    def test_5_2_6_1_pm(self, coap, oscore_ctx):
        """GET /dev/pm → programming mode boolean."""
        resp = coap.oscore_get(oscore_ctx, "/dev/pm")
        assert resp is not None and resp.is_successful
        data = cbor2.loads(resp.payload)
        assert isinstance(data.get(1), bool), (
            f"PM must be boolean, got {type(data.get(1))}")

    def test_5_2_6_1_mid(self, coap, oscore_ctx):
        """GET /dev/mid → manufacturer ID 667."""
        resp = coap.oscore_get(oscore_ctx, "/dev/mid")
        assert resp is not None and resp.is_successful
        data = cbor2.loads(resp.payload)
        assert data.get(1) == 667, f"MID mismatch: {data.get(1)}"

    def test_5_2_6_1_port(self, coap, oscore_ctx):
        """GET /dev/port → CoAP port integer."""
        resp = coap.oscore_get(oscore_ctx, "/dev/port")
        assert resp is not None and resp.is_successful
        data = cbor2.loads(resp.payload)
        assert isinstance(data.get(1), int), (
            f"Port must be int, got {type(data.get(1))}")
        assert 1024 <= data[1] <= 65535, f"Port out of range: {data[1]}"

    def test_5_2_6_1_ipv6(self, coap, oscore_ctx):
        """GET /dev/ipv6 → 16-byte IPv6 address."""
        resp = coap.oscore_get(oscore_ctx, "/dev/ipv6")
        assert resp is not None and resp.is_successful
        data = cbor2.loads(resp.payload)
        ipv6 = data.get(1)
        assert isinstance(ipv6, bytes), (
            f"IPv6 must be bytes, got {type(ipv6)}")
        assert len(ipv6) == 16, (
            f"IPv6 must be 16 bytes, got {len(ipv6)}")

    def test_5_2_6_1_hname(self, coap, oscore_ctx):
        """GET /dev/hname → hostname 'knx-00fa10020800'."""
        resp = coap.oscore_get(oscore_ctx, "/dev/hname")
        assert resp is not None and resp.is_successful
        data = cbor2.loads(resp.payload)
        assert data.get(1) == "knx-00fa10020800", (
            f"Hostname should be 'knx-00fa10020800', got {data.get(1)}")

    def test_5_2_6_1_fid(self, coap, oscore_ctx):
        """GET /dev/fid → fabric ID integer."""
        resp = coap.oscore_get(oscore_ctx, "/dev/fid")
        assert resp is not None and resp.is_successful
        data = cbor2.loads(resp.payload)
        assert isinstance(data.get(1), int), (
            f"FID must be int, got {type(data.get(1))}")


# ===========================================================================
#  5.2.7.1 — Write specific device data
# ===========================================================================

class TestDevResourceWrite:
    """5.2.7.1: Write specific device data for various property-ids.

    EITT only actively tests PUT /dev/pm (Active='Y').
    """

    def test_5_2_7_1_write_pm(self, coap, oscore_ctx):
        """PUT /dev/pm {1: true} → 2.04, then verify GET returns true.

        EITT steps:
        1. PUT /dev/pm {1: true} → 2.04
        2. GET /dev/pm → {1: true}
        """
        try:
            # Step 1: PUT PM to true
            payload = cbor2.dumps({1: True})
            resp = coap.oscore_put(oscore_ctx, "/dev/pm", payload=payload)
            assert resp is not None, "PUT /dev/pm timed out"
            assert resp.is_successful, f"PUT /dev/pm failed: {resp.code}"

            # Step 2: Verify
            resp = coap.oscore_get(oscore_ctx, "/dev/pm")
            assert resp is not None and resp.is_successful
            data = cbor2.loads(resp.payload)
            assert data.get(1) is True, (
                f"PM should be True after PUT, got {data.get(1)}")
        finally:
            # Restore PM to false
            restore = cbor2.dumps({1: False})
            coap.oscore_put(oscore_ctx, "/dev/pm", payload=restore)


# ===========================================================================
#  5.2.7.1b — Invalid PUT on read-only /dev resources
# ===========================================================================

class TestDevReadOnlyPutRejected:
    """5.2.7.1b: PUT to read-only /dev/* resources must fail.

    EITT step 1: PUT with invalid payload → 4.xx
    EITT step 2: GET → verify value unchanged
    """

    # Read-only device resources and their expected types
    READONLY_RESOURCES = [
        ("dev/sn", {1: "001caffe1234"}),
        ("dev/mid", {1: 25}),
        ("dev/hwv", {1: [1, 0, 1]}),
        ("dev/fwv", {1: [1, 0, 1]}),
        ("dev/hwt", {1: "012345ABCDEFG"}),
        ("dev/model", {1: "ABC123"}),
        ("dev/sna", {1: 69}),
        ("dev/da", {1: 6}),
        ("dev/mport", {1: 55555}),
    ]

    @pytest.mark.parametrize("path,payload_data",
                             READONLY_RESOURCES,
                             ids=[r[0] for r in READONLY_RESOURCES])
    def test_5_2_7_1b_put_readonly_rejected(self, coap, oscore_ctx,
                                            path, payload_data):
        """PUT /{path} → 4.xx (read-only resource must reject write)."""
        # Read original value first
        resp_orig = coap.oscore_get(oscore_ctx, f"/{path}")
        orig_payload = resp_orig.payload if resp_orig and resp_orig.is_successful else None

        # Attempt PUT
        payload = cbor2.dumps(payload_data)
        resp = coap.oscore_put(oscore_ctx, f"/{path}", payload=payload)
        assert resp is not None, f"PUT /{path} timed out"
        assert not resp.is_successful, (
            f"PUT /{path} (read-only) should fail, got {resp.code}")
        assert resp.code_class == 4, (
            f"Expected 4.xx error for PUT /{path}, got {resp.code}")

        # Verify unchanged
        if orig_payload is not None:
            resp_check = coap.oscore_get(oscore_ctx, f"/{path}")
            assert resp_check is not None and resp_check.is_successful
            assert resp_check.payload == orig_payload, (
                f"/{path} value should not change after rejected PUT")
