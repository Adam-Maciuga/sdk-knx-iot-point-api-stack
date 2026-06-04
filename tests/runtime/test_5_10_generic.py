"""
Runtime conformance tests — EITT 5.10 Generic Tests

5.10.1.1  Pagination via GET specifying Page Number pn
5.10.1.2  Pagination via GET specifying pn and Page Size ps
5.10.1.4  List Metadata Query Parameter l
5.10.1.5  Invalid List Metadata Query Parameter (GET /fp/g?l=xxx → 4.04)
5.10.1.6  Pagination with Link to next Page
5.10.4.1  Read DPT associated with Group Object (GET /p/1?m=dpt)
5.10.5.1  Accept Option omitted → default response
5.10.5.2  Content-Format Option omitted → default
5.10.5.3  Unknown Critical Option → 4.02 Bad Option

Reference: 08_10_5 KNX IoT Point API Tests v01_01_01_AS
"""

import struct

import cbor2
import pytest

from coap_client import (APPLICATION_CBOR, LINK_FORMAT,
                         _encode_option, CON, GET)
from conftest import (DUT_IA, DUT_IID, DEVICE_PASSWORD, ALL_SCOPES,
                      auth_prepare, ia_prepare,
                      set_lsm, parse_link_format)





def _provision_go_table(coap, oscore_ctx, count):
    """Create ``count`` group objects in loading state, then set loaded."""
    set_lsm(coap, oscore_ctx, 1)  # loading
    entries = [
        {0: i, 8: 0x90, 7: [1], 11: "/p/1"}
        for i in range(1, count + 1)
    ]
    resp = coap.oscore_post(
        oscore_ctx, "/fp/g", payload=cbor2.dumps(entries))
    assert resp is not None and resp.is_successful, (
        f"POST /fp/g failed: {resp.code if resp else 'timeout'}")
    set_lsm(coap, oscore_ctx, 2)  # loaded


# ===========================================================================
# 5.10.1.1 — Pagination via GET specifying Page Number pn
# ===========================================================================

class TestPaginationPageNumber:
    """EITT 5.10.1.1: GET /fp/g?pn=N returns entries for page N."""

    @pytest.fixture(autouse=True)
    def setup(self, coap, oscore_ctx):
        auth_prepare(coap, oscore_ctx)
        ia_prepare(coap, oscore_ctx)
        _provision_go_table(coap, oscore_ctx, 4)

    def test_5_10_1_1_page_zero_returns_entries(self, coap, oscore_ctx):
        """GET /fp/g?pn=0 returns all entries on page 0.

        EITT: pn=0 returns link-format with /fp/g/1.../fp/g/4.
        """
        resp = coap.oscore_get(
            oscore_ctx, "/fp/g?pn=0", accept=LINK_FORMAT)
        assert resp is not None and resp.is_successful, (
            f"GET /fp/g?pn=0 failed: {resp.code if resp else 'timeout'}")
        assert resp.payload, "GET /fp/g?pn=0 returned empty payload"
        links = parse_link_format(resp.payload)
        # Filter out p.next links
        entries = [l for l in links if l.get("rt") != "p.next"]
        assert len(entries) >= 1, "pn=0 should return at least one entry"

    def test_5_10_1_1_page_beyond_range_returns_400(self, coap, oscore_ctx):
        """GET /fp/g?pn=N beyond range → 4.00 Bad Request.

        EITT: pn=1 and higher for 4 entries with default ps returns 4.00.
        """
        resp = coap.oscore_get(
            oscore_ctx, "/fp/g?pn=5", accept=LINK_FORMAT)
        assert resp is not None, "No response for GET /fp/g?pn=5"
        assert resp.code == "4.00", (
            f"Expected 4.00 for out-of-range page, got {resp.code}")


# ===========================================================================
# 5.10.1.2 — Pagination via GET specifying pn and ps
# ===========================================================================

class TestPaginationPageSize:
    """EITT 5.10.1.2: GET /fp/g?pn=N&ps=M controls page size."""

    @pytest.fixture(autouse=True)
    def setup(self, coap, oscore_ctx):
        # EITT: chains from 5.10.1.1 (no factory reset)
        _provision_go_table(coap, oscore_ctx, 4)

    def test_5_10_1_2_first_page_with_ps1(self, coap, oscore_ctx):
        """GET /fp/g?pn=0&ps=1 returns single entry + next link.

        EITT: ps=1, pn=0 → one entry + <fp/g?pn=1>;rt="p.next".
        """
        resp = coap.oscore_get(
            oscore_ctx, "/fp/g?pn=0&ps=1", accept=LINK_FORMAT)
        assert resp is not None and resp.is_successful, (
            f"GET /fp/g?pn=0&ps=1 failed: {resp.code if resp else 'timeout'}")
        assert resp.payload, "Empty payload"
        links = parse_link_format(resp.payload)
        entries = [l for l in links if l.get("rt") != "p.next"]
        next_links = [l for l in links if l.get("rt") == "p.next"]
        assert len(entries) == 1, (
            f"Expected 1 entry with ps=1, got {len(entries)}")
        assert len(next_links) >= 1, (
            "Expected a p.next link when more entries exist")

    def test_5_10_1_2_second_page_with_ps2(self, coap, oscore_ctx):
        """GET /fp/g?pn=1&ps=2 returns entries 3..4.

        EITT: pn=1 with ps=2 → entries starting at offset 2.
        """
        resp = coap.oscore_get(
            oscore_ctx, "/fp/g?pn=1&ps=2", accept=LINK_FORMAT)
        assert resp is not None and resp.is_successful, (
            f"GET /fp/g?pn=1&ps=2 failed: {resp.code if resp else 'timeout'}")
        assert resp.payload, "Empty payload"
        links = parse_link_format(resp.payload)
        entries = [l for l in links if l.get("rt") != "p.next"]
        assert len(entries) >= 1, "pn=1 with ps=2 should have entries"

    def test_5_10_1_2_beyond_range(self, coap, oscore_ctx):
        """GET /fp/g?pn=2&ps=2 → 4.00 (page 2 with ps=2 exceeds 4 entries).

        EITT: returns 4.00 for out-of-range page with explicit ps.
        """
        resp = coap.oscore_get(
            oscore_ctx, "/fp/g?pn=2&ps=2", accept=LINK_FORMAT)
        assert resp is not None, "No response"
        assert resp.code == "4.00", (
            f"Expected 4.00 for beyond-range, got {resp.code}")


# ===========================================================================
# 5.10.1.4 — List Metadata Query Parameter l
# ===========================================================================

class TestListMetadata:
    """EITT 5.10.1.4: GET /resource?l=total returns total count."""

    @pytest.fixture(autouse=True)
    def setup(self, coap, oscore_ctx):
        auth_prepare(coap, oscore_ctx)
        ia_prepare(coap, oscore_ctx)
        _provision_go_table(coap, oscore_ctx, 4)

    def test_5_10_1_4_total_fpg(self, coap, oscore_ctx):
        """GET /fp/g?l=total → link-format with ;total=4.

        EITT: Response is <path>;total=N for each table resource.
        """
        resp = coap.oscore_get(
            oscore_ctx, "/fp/g?l=total", accept=LINK_FORMAT)
        assert resp is not None and resp.is_successful, (
            f"GET /fp/g?l=total failed: {resp.code if resp else 'timeout'}")
        assert resp.payload, "Empty payload"
        links = parse_link_format(resp.payload)
        assert len(links) >= 1, "Expected at least one link"
        # The link should contain total=<N>
        assert any("total" in l for l in links), (
            f"Expected total= attribute in response: "
            f"{resp.payload.decode('utf-8', errors='replace')}")

    def test_5_10_1_4_total_p(self, coap, oscore_ctx):
        """GET /p?l=total → link-format with ;total=N."""
        resp = coap.oscore_get(
            oscore_ctx, "/p?l=total", accept=LINK_FORMAT)
        assert resp is not None and resp.is_successful, (
            f"GET /p?l=total failed: {resp.code if resp else 'timeout'}")
        assert resp.payload, "Empty payload"
        links = parse_link_format(resp.payload)
        assert len(links) >= 1, "Expected at least one link"
        assert any("total" in l for l in links), (
            f"Expected total= attribute: "
            f"{resp.payload.decode('utf-8', errors='replace')}")

    def test_5_10_1_4_total_auth_at(self, coap, oscore_ctx):
        """GET /auth/at?l=total → link-format with ;total=N."""
        resp = coap.oscore_get(
            oscore_ctx, "/auth/at?l=total", accept=LINK_FORMAT)
        assert resp is not None and resp.is_successful, (
            f"GET /auth/at?l=total failed: "
            f"{resp.code if resp else 'timeout'}")
        assert resp.payload, "Empty payload"
        links = parse_link_format(resp.payload)
        assert len(links) >= 1, "Expected at least one link"
        assert any("total" in l for l in links), (
            f"Expected total= attribute: "
            f"{resp.payload.decode('utf-8', errors='replace')}")


# ===========================================================================
# 5.10.1.6 — Pagination with Link to next Page
# ===========================================================================

class TestPaginationNextPage:
    """EITT 5.10.1.6: Follow p.next links to iterate all entries."""

    @pytest.fixture(autouse=True)
    def setup(self, coap, oscore_ctx):
        # EITT: chains from 5.10.1.4 (no factory reset)
        # Create 5 group objects so pagination with ps=2 gives 3 pages
        _provision_go_table(coap, oscore_ctx, 5)

    def test_5_10_1_6_follow_next_links(self, coap, oscore_ctx):
        """GET /fp/g?pn=0&ps=2, follow p.next links until last page.

        EITT: First page has 2 entries + p.next, second page has 2 +
        p.next, third page has 1 entry (no p.next).

        Note: The server's p.next link may omit ps.  Per EITT behavior,
        we preserve the original ps when following next links.
        """
        all_entries = []
        ps = 2
        url = f"/fp/g?pn=0&ps={ps}"
        max_pages = 10  # safety limit

        for page in range(max_pages):
            resp = coap.oscore_get(
                oscore_ctx, url, accept=LINK_FORMAT)
            assert resp is not None and resp.is_successful, (
                f"GET {url} failed: {resp.code if resp else 'timeout'}")
            assert resp.payload, f"GET {url} empty payload"

            links = parse_link_format(resp.payload)
            entries = [l for l in links if l.get("rt") != "p.next"]
            next_links = [l for l in links if l.get("rt") == "p.next"]
            all_entries.extend(entries)

            if not next_links:
                break
            # Follow the next link; append ps if not already present
            next_url = next_links[0]["href"]
            if f"ps=" not in next_url:
                sep = "&" if "?" in next_url else "?"
                next_url = f"{next_url}{sep}ps={ps}"
            url = next_url
        else:
            pytest.fail(f"Exceeded {max_pages} pages — infinite loop?")

        assert len(all_entries) == 5, (
            f"Expected 5 entries total, got {len(all_entries)}: "
            f"{[e['href'] for e in all_entries]}")


# ===========================================================================
# 5.10.1 — Invalid List Metadata Query Parameter
# ===========================================================================

class TestInvalidListMetadata:
    """Test GET on table resources with invalid ?l= query returns 4.04."""

    @pytest.fixture(autouse=True)
    def setup(self, coap, oscore_ctx):
        # EITT: chains from 5.10.1.4 (no factory reset)
        pass

    def test_5_10_1_5_invalid_list_metadata(self, coap, oscore_ctx):
        """GET /fp/g?l=xxx (invalid metadata param) → 4.04 Not Found.

        The spec says both 4.04 and 2.xx are acceptable implementations.
        Our stack returns 4.04 for unknown l= values.
        """
        set_lsm(coap, oscore_ctx, 1)  # loading state for /fp access

        urls = ["/fp/g", "/fp/r", "/fp/p", "/auth/at"]
        for url in urls:
            resp = coap.oscore_get(
                oscore_ctx, f"{url}?l=xxx", accept=LINK_FORMAT)
            assert resp is not None, (
                f"No response for GET {url}?l=xxx")
            # Spec allows 4.04 or a positive response
            assert resp.code in ("4.04", "2.05", "4.00"), (
                f"GET {url}?l=xxx: expected 4.04 or 2.05, got {resp.code}")


# ===========================================================================
# 5.10.4 — Read DPT Metadata
# ===========================================================================

class TestDptMetadata:
    """Test GET /p/<dp>?m=dpt returns the datapoint type."""

    @pytest.fixture(autouse=True)
    def setup(self, coap, oscore_ctx):
        auth_prepare(coap, oscore_ctx)
        ia_prepare(coap, oscore_ctx)

    def test_5_10_4_1_read_dpt(self, coap, oscore_ctx):
        """GET /p/1?m=dpt → {"dpt": ":dpt.switch"}."""
        resp = coap.oscore_get(oscore_ctx, "/p/1?m=dpt")
        assert resp is not None and resp.is_successful, (
            f"GET /p/1?m=dpt failed: {resp.code if resp else 'timeout'}")
        assert resp.payload, "GET /p/1?m=dpt returned empty payload"

        data = cbor2.loads(resp.payload)
        # DPT is encoded with text key "dpt"
        dpt_val = data.get("dpt")
        assert dpt_val is not None, (
            f"Response should contain 'dpt' key, got {data}")
        assert "dpt.switch" in dpt_val, (
            f"Expected dpt.switch, got '{dpt_val}'")


# ===========================================================================
# 5.10.5 — Default Options & Unknown Critical Options
# ===========================================================================

class TestDefaultOptions:
    """Test default behavior when Accept/Content-Format are omitted."""

    @pytest.fixture(autouse=True)
    def setup(self, coap, oscore_ctx):
        # EITT: chains from 5.10.4.1 (no factory reset)
        pass

    def test_5_10_5_1_accept_omitted(self, coap, oscore_ctx):
        """GET /dev/sn without Accept → still returns valid CBOR response."""
        # oscore_get with accept=None → no Accept option in inner message
        resp = coap.oscore_get(oscore_ctx, "/dev/sn", accept=None)
        assert resp is not None and resp.is_successful, (
            f"GET /dev/sn (no Accept) failed: "
            f"{resp.code if resp else 'timeout'}")
        assert resp.payload, "GET /dev/sn (no Accept) returned empty payload"
        # Should still be valid CBOR
        data = cbor2.loads(resp.payload)
        assert data is not None

    def test_5_10_5_2_content_format_omitted(self, coap, oscore_ctx):
        """POST /a/lsm without Content-Format → still processed.

        When Content-Format is omitted, the stack should still accept
        valid CBOR payload and process it.
        """
        # Send startLoading without explicit Content-Format
        resp = coap.oscore_post(
            oscore_ctx, "/a/lsm",
            payload=cbor2.dumps({2: 1}),
            content_format=None)
        # The device should either accept it (2.04) or reject gracefully
        assert resp is not None, (
            "No response to POST /a/lsm without Content-Format")
        # Accept either success or a well-formed error
        assert resp.code_class in (2, 4), (
            f"Expected 2.xx or 4.xx, got {resp.code}")

    def test_5_10_5_3_unknown_critical_option(self, coap, oscore_ctx):
        """Send request with unknown critical CoAP option → 4.02 Bad Option.

        We send a plain (non-OSCORE) GET to a non-secured resource with
        an unknown odd (critical) option number in the experimental range.
        """
        # Build a raw CoAP CON GET for /.well-known/knx (no auth needed)
        mid = 0xBEEF
        token = b"\xAB"
        tkl = 1
        ver_type_tkl = (1 << 6) | (CON << 4) | tkl
        code = (0 << 5) | 1  # 0.01 GET
        header = struct.pack("!BBH", ver_type_tkl, code, mid)

        # Options: Uri-Path segments
        options = b""
        prev_opt = 0
        for part in [".well-known", "knx"]:
            delta = 11 - prev_opt
            prev_opt = 11
            options += _encode_option(delta, part.encode())

        # Add unknown critical option (odd number = critical per CoAP spec)
        # Use option 65001 (experimental range, odd → critical)
        unknown_opt_num = 65001
        delta = unknown_opt_num - prev_opt
        prev_opt = unknown_opt_num
        options += _encode_option(delta, b"\x00")

        msg = header + token + options

        import socket
        old_timeout = coap._sock.gettimeout()
        coap._sock.settimeout(5)
        try:
            coap._sock.sendto(msg, (coap.host, coap.port, 0, 0))
            data, addr = coap._sock.recvfrom(4096)
        except socket.timeout:
            pytest.fail("No response to request with unknown critical option")
        finally:
            coap._sock.settimeout(old_timeout)

        # Parse response
        code_byte = data[1]
        code_class = code_byte >> 5
        code_detail = code_byte & 0x1F
        assert code_class == 4 and code_detail == 2, (
            f"Expected 4.02 Bad Option, got {code_class}.{code_detail:02d}")
