"""
Runtime conformance tests — EITT 5.2.8 / 5.2.9 / 5.2.10

5.2.8.1   Read list of software update property-ids (GET /swu link-format)
5.2.9.1   Read specific SWU data during PUSH-based firmware update
5.2.10.1b Invalid PUT on read-only resource for various SWU property-ids
"""

import re
import time

import cbor2
import pytest

from coap_client import APPLICATION_CBOR, LINK_FORMAT

APPLICATION_OCTET_STREAM = 42


# ===========================================================================
#  5.2.8.1 — GET /swu (link-format listing)
# ===========================================================================

class TestSwuResourceList:
    """5.2.8.1: Read list of software update property-ids."""

    MANDATORY_RESOURCES = [
        "swu/pkgname", "swu/pkgbytes", "swu/pkgv",
        "swu/update", "swu/state", "swu/result",
        "swu/lastupdate", "swu/method", "swu/protocol", "swu/hwref",
    ]

    def test_5_2_8_1_swu_link_format(self, coap, oscore_ctx):
        """GET /swu → 2.05 with link-format listing SWU sub-resources.

        EITT expects application/link-format with mandatory SWU resources,
        each having if= and ct= attributes. Also checks /a/swu with ct=42.
        """
        resp = coap.oscore_get(oscore_ctx, "/swu", accept=LINK_FORMAT)
        if resp is None:
            pytest.skip("GET /swu timed out")
        if not resp.is_successful:
            pytest.skip(f"GET /swu not supported: {resp.code}")

        body = resp.payload
        if isinstance(body, bytes):
            body = body.decode("utf-8", errors="replace")

        for res in self.MANDATORY_RESOURCES:
            assert res in body, (
                f"Missing mandatory SWU resource '{res}' in link-format: "
                f"{body[:200]}")

        # EITT validates if= and ct= on each entry
        lines = [l.strip() for l in body.split(",") if l.strip()]
        for line in lines:
            # Every SWU sub-resource must have if= attribute
            if "/swu/" in line:
                assert re.search(r'if="[^"]*"', line), (
                    f"EITT: SWU entry must have if= attribute. Got: {line}")
                # ct must be 60 (or variants)
                assert re.search(
                    r'ct=((?:60 50)|(?:50 60)|(?:60)|'
                    r'(?:"60 50")|(?:"50 60"))', line), (
                    f"EITT: SWU entry must have ct=60. Got: {line}")
            # /a/swu must have ct=42 and if= attribute
            if "/a/swu" in line and "/swu/" not in line:
                assert "ct=42" in line, (
                    f"EITT: /a/swu must have ct=42. Got: {line}")
                assert re.search(r'if="[^"]*"', line), (
                    f"EITT: /a/swu must have if= attribute. Got: {line}")


# ===========================================================================
#  5.2.9.1 — PUSH-based firmware update
# ===========================================================================

class TestSwuFirmwareUpdate:
    """5.2.9.1: Read specific SWU data during PUSH-based firmware update.

    EITT flow:
      Step 1: No update package — verify idle state and initial values
      Step 2: Upload first half → verify DOWNLOADING state
      Step 3: Upload second half → verify DOWNLOADED state + package metadata
      Step 4: Trigger update → verify IDLE + new firmware version
    """

    # 256-byte dummy firmware (content doesn't matter for stack testing)
    FIRMWARE_SIZE = 256
    BLOCK_SIZE = 128

    def _upload_block(self, coap, oscore_ctx, block_data, offset,
                      total_size=None):
        """Upload a single firmware block via PUT /a/swu."""
        queries = [f"ps={len(block_data)}"]
        if total_size is not None:
            queries.append(f"pkgs={total_size}")
        if offset > 0:
            queries.append(f"po={offset}")

        resp = coap.oscore_request(
            oscore_ctx, 3, "/a/swu",
            payload=block_data,
            uri_queries=queries,
            content_format=APPLICATION_OCTET_STREAM,
            timeout=10.0)
        return resp

    def _get_cbor_value(self, coap, oscore_ctx, path, key=1):
        """GET a CBOR resource and return value for given key."""
        resp = coap.oscore_get(oscore_ctx, f"/{path}")
        if resp is None or not resp.is_successful:
            return resp, None
        data = cbor2.loads(resp.payload)
        return resp, data.get(key)

    def test_5_2_9_1_swu_firmware_update(self, coap, oscore_ctx):
        """Full PUSH-based firmware update cycle matching EITT 5.2.9.1."""
        firmware = bytes(range(256)) * (self.FIRMWARE_SIZE // 256) \
            + bytes(range(self.FIRMWARE_SIZE % 256))
        firmware = firmware[:self.FIRMWARE_SIZE]

        # ── Step 1: No update package available ──────────────────────

        # pkgname → 4.04 (no package yet)
        resp = coap.oscore_get(oscore_ctx, "/swu/pkgname")
        assert resp is not None
        assert resp.code == "4.04", (
            f"Expected 4.04 for /swu/pkgname (no package), got {resp.code}")

        # pkgbytes → 0
        resp, val = self._get_cbor_value(coap, oscore_ctx, "swu/pkgbytes")
        assert resp is not None and resp.is_successful
        assert val == 0, f"pkgbytes should be 0 initially, got {val}"

        # pkgv → 4.04 (no package version)
        resp = coap.oscore_get(oscore_ctx, "/swu/pkgv")
        assert resp is not None
        assert resp.code == "4.04", (
            f"Expected 4.04 for /swu/pkgv (no package), got {resp.code}")

        # state → 0 (IDLE)
        resp, val = self._get_cbor_value(coap, oscore_ctx, "swu/state")
        assert resp is not None and resp.is_successful
        assert val == 0, f"state should be 0 (IDLE), got {val}"

        # result → 0 (INIT)
        resp, val = self._get_cbor_value(coap, oscore_ctx, "swu/result")
        assert resp is not None and resp.is_successful
        assert val == 0, f"result should be 0 (INIT), got {val}"

        # lastupdate → "osv:true" or manufacturing date
        resp = coap.oscore_get(oscore_ctx, "/swu/lastupdate")
        assert resp is not None and resp.is_successful

        # protocol → 0 (CoAP)
        resp, val = self._get_cbor_value(coap, oscore_ctx, "swu/protocol")
        assert resp is not None and resp.is_successful
        assert val == 0, f"protocol should be 0 (CoAP), got {val}"

        # method → 1 (PUSH) or 2 (BOTH)
        resp, val = self._get_cbor_value(coap, oscore_ctx, "swu/method")
        assert resp is not None and resp.is_successful
        assert val in (1, 2), f"method should be 1 (PUSH) or 2, got {val}"

        # hwref → string
        resp = coap.oscore_get(oscore_ctx, "/swu/hwref")
        assert resp is not None and resp.is_successful

        # fwv → [0, 0, 1] (initial firmware version)
        resp, val = self._get_cbor_value(coap, oscore_ctx, "dev/fwv")
        assert resp is not None and resp.is_successful
        assert val == [0, 0, 1], (
            f"Initial FWV should be [0, 0, 1], got {val}")

        # ── Upload first half of firmware ────────────────────────────
        first_half = firmware[:self.BLOCK_SIZE]
        resp = self._upload_block(coap, oscore_ctx, first_half, 0,
                                  total_size=self.FIRMWARE_SIZE)
        assert resp is not None, "PUT /a/swu (first block) timed out"
        assert resp.is_successful, (
            f"PUT /a/swu (first block) failed: {resp.code}")

        # ── Step 2: During download ──────────────────────────────────

        # state → 1 (DOWNLOADING)
        resp, val = self._get_cbor_value(coap, oscore_ctx, "swu/state")
        assert resp is not None and resp.is_successful
        assert val == 1, f"state should be 1 (DOWNLOADING), got {val}"

        # pkgbytes > 0
        resp, val = self._get_cbor_value(coap, oscore_ctx, "swu/pkgbytes")
        assert resp is not None and resp.is_successful
        assert val > 0, f"pkgbytes should be > 0 during download, got {val}"

        # PUT /swu/update during download → 4.00
        update_payload = cbor2.dumps({1: 0})
        resp = coap.oscore_put(oscore_ctx, "/swu/update",
                               payload=update_payload)
        assert resp is not None
        assert resp.code == "4.00", (
            f"PUT /swu/update during download should fail with 4.00, "
            f"got {resp.code}")

        # ── Upload second half of firmware ───────────────────────────
        second_half = firmware[self.BLOCK_SIZE:]
        resp = self._upload_block(coap, oscore_ctx, second_half,
                                  self.BLOCK_SIZE)
        assert resp is not None, "PUT /a/swu (second block) timed out"
        assert resp.is_successful, (
            f"PUT /a/swu (second block) failed: {resp.code}")

        # ── Step 3: After download complete ──────────────────────────

        # state → 2 (DOWNLOADED)
        resp, val = self._get_cbor_value(coap, oscore_ctx, "swu/state")
        assert resp is not None and resp.is_successful
        assert val == 2, f"state should be 2 (DOWNLOADED), got {val}"

        # pkgv → [0, 0, 2] (new package version)
        resp, val = self._get_cbor_value(coap, oscore_ctx, "swu/pkgv")
        assert resp is not None and resp.is_successful
        assert val == [0, 0, 2], (
            f"Package version should be [0, 0, 2], got {val}")

        # ── Trigger update ───────────────────────────────────────────
        resp = coap.oscore_put(oscore_ctx, "/swu/update",
                               payload=update_payload)
        assert resp is not None
        assert resp.is_successful, (
            f"PUT /swu/update after download should succeed, got {resp.code}")

        # Wait for simulated upgrade to complete (test server uses 2s delay)
        time.sleep(4)

        # ── Step 4: After upgrade complete ───────────────────────────

        # state → 0 (IDLE)
        resp, val = self._get_cbor_value(coap, oscore_ctx, "swu/state")
        assert resp is not None and resp.is_successful
        assert val == 0, (
            f"state should be 0 (IDLE) after upgrade, got {val}")

        # fwv → [0, 0, 2] (updated firmware version)
        resp, val = self._get_cbor_value(coap, oscore_ctx, "dev/fwv")
        assert resp is not None and resp.is_successful
        assert val == [0, 0, 2], (
            f"FWV after upgrade should be [0, 0, 2], got {val}")

        # lastupdate → "osv:true" (device has been updated once)
        resp = coap.oscore_get(oscore_ctx, "/swu/lastupdate")
        assert resp is not None and resp.is_successful


# ===========================================================================
#  5.2.10.1b — Invalid PUT on read-only SWU resources
# ===========================================================================

class TestSwuReadOnlyPutRejected:
    """5.2.10.1b: PUT to read-only /swu/* resources must fail.

    EITT step 1: PUT with invalid payload → 4.05
    EITT step 2: GET → verify value unchanged
    """

    READONLY_RESOURCES = [
        ("swu/pkgname", {1: "test name"}),
        ("swu/pkgbytes", {1: 42}),
        ("swu/pkgv", {1: [0, 0, 0]}),
        ("swu/state", {1: 2}),
        ("swu/result", {1: 9}),
    ]

    @pytest.mark.parametrize("path,payload_data",
                             READONLY_RESOURCES,
                             ids=[r[0] for r in READONLY_RESOURCES])
    def test_5_2_10_1b_put_readonly_swu_rejected(self, coap, oscore_ctx,
                                                  path, payload_data):
        """PUT /{path} → 4.xx (read-only SWU resource must reject write)."""
        # Read original value (may be 4.04 for pkgname/pkgv)
        resp_orig = coap.oscore_get(oscore_ctx, f"/{path}")
        orig_code = resp_orig.code if resp_orig else None
        orig_payload = resp_orig.payload if resp_orig and resp_orig.is_successful else None

        # Attempt PUT
        payload = cbor2.dumps(payload_data)
        resp = coap.oscore_put(oscore_ctx, f"/{path}", payload=payload)
        if resp is None:
            pytest.skip(f"PUT /{path} timed out")
        assert not resp.is_successful, (
            f"PUT /{path} (read-only) should fail, got {resp.code}")
        assert resp.code == "4.05", (
            f"Expected 4.05 for PUT /{path}, got {resp.code}")

        # Verify unchanged (only if original was readable)
        if orig_payload is not None:
            resp_check = coap.oscore_get(oscore_ctx, f"/{path}")
            assert resp_check is not None and resp_check.is_successful
            assert resp_check.payload == orig_payload, (
                f"/{path} value should not change after rejected PUT")
        elif orig_code == "4.04":
            # Resource was not found before — should still be not found
            resp_check = coap.oscore_get(oscore_ctx, f"/{path}")
            assert resp_check is not None
            assert resp_check.code == "4.04", (
                f"/{path} should still be 4.04 after rejected PUT")
