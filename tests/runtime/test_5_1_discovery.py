"""
Runtime conformance tests -- 5.1 Unicast Discovery

Replicates EITT certification tests for unicast CoAP discovery.
All discovery requests are unencrypted (no OSCORE), per spec 4.2.4.

Spec test IDs covered:
  5.1.1.2:  Unicast Discovery via /.well-known/core
  5.1.1.4:  Unicast Discovery with filters rt, if & ep (5 steps)
  5.1.1.4b: Invalid unicast query → 4.04
  5.1.1.4c: Invalid unicast interface filter → 4.04
  5.1.1.4d: Invalid unicast resource type filter → 4.04
  5.1.1.4e: Unicast interface wildcard → 2.05

  5.1.1.900: Supplementary, not an official EITT clause (local extras are
             numbered from 900 upward): pagination of /.well-known/core
             when the ?rt= filter pulls the application functional
             blocks into the result set, forcing multiple pages.

Reference: 08_10_5 KNX IoT Point API Tests v01_01_01_AS
EITT Project: EittProject.xml PlanID 699f219c.699f219b
"""

import re

import pytest

from coap_client import LINK_FORMAT
from conftest import parse_link_format

# Device constants matching EITT configuration
DUT_SERIAL_NUMBER = "00fa10020800"

# Default timeout for CoAP requests
COAP_TIMEOUT = 5.0


# ===========================================================================
#  5.1.1.2 -- Unicast Discovery via /.well-known/core
# ===========================================================================

class TestUnicastDiscovery:
    """5.1.1.2: CON GET coap://{unicast}/.well-known/core

    EITT sends CON GET with Accept: application/link-format.
    Expects ACK 2.05 Content with link-format payload containing:
      - /dev  with ct=40
      - /auth with ct=40
      - /swu  with ct=40
      - /k    with ct=60 (or "60 50" or "50 60")
      - /f/*  with rt="urn:knx:fb.417" and ct=40
      - /f/*  with rt="urn:knx:fb.421" and ct=40
      - optional /ap with ct=40
    """

    def test_5_1_1_2_unicast_discovery(self, coap):
        """5.1.1.2: Unicast GET /.well-known/core returns 2.05 with
        required resources in link-format."""
        resp = coap.get(".well-known/core", accept=LINK_FORMAT,
                        timeout=COAP_TIMEOUT)
        assert resp is not None, "No response from DUT"
        assert resp.is_successful, f"Expected 2.05 Content, got {resp.code}"
        assert resp.content_format == LINK_FORMAT, (
            f"Expected Content-Format 40 (link-format), "
            f"got {resp.content_format}")
        assert len(resp.payload) > 0, "Payload should not be empty"

        text = resp.payload.decode("utf-8", errors="replace")

        # EITT regex patterns (from EittProject.xml TelNo=10)
        # Each line of the response is checked against these patterns.
        # 'all:' patterns must match at least one line.
        # 'opt:' patterns are optional.
        eitt_required = [
            r'(?=.*;ct=40)(?=.*="urn:knx:)<.*/dev>',
            r'(?=.*;ct=40)(?=.*="urn:knx:)<.*/auth>',
            r'(?=.*;ct=40)(?=.*="urn:knx:)<.*/swu>',
            r'(?=.*;ct=((?:60 50)|(?:50 60)|(?:60)))(?=.*="urn:knx:)<.*/k>',
            r'(?=.*;rt="urn:knx:fb\.417")(?=.*;ct=40)<.*/f/.*>',
            r'(?=.*;rt="urn:knx:fb\.421")(?=.*;ct=40)<.*/f/.*>',
        ]

        # Split payload into individual link entries
        lines = [line.strip() for line in text.split(",") if line.strip()]

        for pattern in eitt_required:
            matched = any(re.search(pattern, line) for line in lines)
            assert matched, (
                f"EITT pattern not matched: {pattern}\n"
                f"Response lines:\n" +
                "\n".join(f"  {l}" for l in lines))


# ===========================================================================
#  5.1.1.4 -- Unicast Discovery with filters rt, if & ep
# ===========================================================================

class TestUnicastDiscoveryFilters:
    """5.1.1.4: CON GET /.well-known/core with query filters.

    EITT tests 5 steps, each with a different filter.
    """

    def test_5_1_1_4_step1_rt_known_value(self, coap):
        """5.1.1.4 step 1: ?rt=urn:knx:dpa.417.61 → 2.05 with matching rt."""
        resp = coap.get(".well-known/core?rt=urn:knx:dpa.417.61",
                        accept=LINK_FORMAT, timeout=COAP_TIMEOUT)
        assert resp is not None, "No response from DUT"
        assert resp.is_successful, f"Expected 2.05, got {resp.code}"
        assert resp.content_format == LINK_FORMAT

        # EITT validation: all lines must contain rt=":dpa.417.61"
        text = resp.payload.decode("utf-8", errors="replace")
        lines = [l.strip() for l in text.split(",") if l.strip()]
        for line in lines:
            assert re.search(r'rt=":dpa\.417\.61', line), (
                f"All entries must match rt filter, got: {line}")

    def test_5_1_1_4_step2_rt_wildcard(self, coap):
        """5.1.1.4 step 2: ?rt=urn:knx:dpa.417.* → 2.05 with rt entries."""
        resp = coap.get(".well-known/core?rt=urn:knx:dpa.417.*",
                        accept=LINK_FORMAT, timeout=COAP_TIMEOUT)
        assert resp is not None, "No response from DUT"
        assert resp.is_successful, f"Expected 2.05, got {resp.code}"
        assert resp.content_format == LINK_FORMAT

        # EITT validation: all lines must have ;rt=
        text = resp.payload.decode("utf-8", errors="replace")
        lines = [l.strip() for l in text.split(",") if l.strip()]
        for line in lines:
            assert re.search(r';rt=', line), (
                f"Wildcard rt filter should return entries with rt, got: {line}")

    def test_5_1_1_4_step3_if_known_value(self, coap):
        """5.1.1.4 step 3: ?if=urn:knx:if.o → 2.05 with matching if."""
        resp = coap.get(".well-known/core?if=urn:knx:if.o",
                        accept=LINK_FORMAT, timeout=COAP_TIMEOUT)
        assert resp is not None, "No response from DUT"
        assert resp.is_successful, f"Expected 2.05, got {resp.code}"
        assert resp.content_format == LINK_FORMAT

        # EITT validation: all lines must contain if=":if.o"
        text = resp.payload.decode("utf-8", errors="replace")
        lines = [l.strip() for l in text.split(",") if l.strip()]
        for line in lines:
            assert re.search(r'if=":if\.o', line), (
                f"All entries must match if filter, got: {line}")

    def test_5_1_1_4_step4_ep_serial_number(self, coap):
        """5.1.1.4 step 4: ?ep=knx://sn.{sn} → 2.05 with ep containing
        sn and ia."""
        resp = coap.get(
            f".well-known/core?ep=knx://sn.{DUT_SERIAL_NUMBER}",
            accept=LINK_FORMAT, timeout=COAP_TIMEOUT)
        assert resp is not None, "No response from DUT"
        assert resp.is_successful, f"Expected 2.05, got {resp.code}"
        assert resp.content_format == LINK_FORMAT

        # EITT validation: response contains ep with sn and ia
        text = resp.payload.decode("utf-8", errors="replace")
        assert f"knx://sn.{DUT_SERIAL_NUMBER}" in text, (
            f"Response should contain serial number in ep, got: {text[:200]}")
        assert "knx://ia." in text, (
            f"Response should contain ia in ep, got: {text[:200]}")

    def test_5_1_1_4_step5_ep_sn_wildcard(self, coap):
        """5.1.1.4 step 5: ?ep=knx://sn.* → 2.05 with ep containing
        sn and ia."""
        resp = coap.get(".well-known/core?ep=knx://sn.*",
                        accept=LINK_FORMAT, timeout=COAP_TIMEOUT)
        assert resp is not None, "No response from DUT"
        assert resp.is_successful, f"Expected 2.05, got {resp.code}"
        assert resp.content_format == LINK_FORMAT

        # EITT validation: response contains ep with sn and ia
        text = resp.payload.decode("utf-8", errors="replace")
        assert f"knx://sn.{DUT_SERIAL_NUMBER}" in text, (
            f"Response should contain serial number in ep, got: {text[:200]}")
        assert "knx://ia." in text, (
            f"Response should contain ia in ep, got: {text[:200]}")


# ===========================================================================
#  5.1.1.4b -- Invalid Unicast Discovery Query
# ===========================================================================

class TestInvalidUnicastQueries:
    """5.1.1.4b-e: Invalid unicast discovery queries → 4.04 Not Found.

    EITT uses specific query values (not generic "invalid" strings).
    """

    def test_5_1_1_4b_invalid_query_key(self, coap):
        """5.1.1.4b: ?invalid-coap-rd-query → ACK 4.04."""
        resp = coap.get(".well-known/core?invalid-coap-rd-query",
                        accept=LINK_FORMAT, timeout=COAP_TIMEOUT)
        assert resp is not None, "No response from DUT"
        assert resp.is_not_found, (
            f"Invalid query should return 4.04, got {resp.code}")

    def test_5_1_1_4c_invalid_interface(self, coap):
        """5.1.1.4c: ?if=urn:knx:if.k (non-existent interface) → ACK 4.04."""
        resp = coap.get(".well-known/core?if=urn:knx:if.k",
                        accept=LINK_FORMAT, timeout=COAP_TIMEOUT)
        assert resp is not None, "No response from DUT"
        assert resp.is_not_found, (
            f"Non-existent interface should return 4.04, got {resp.code}")

    def test_5_1_1_4d_invalid_resource_type(self, coap):
        """5.1.1.4d: ?rt=urn:knx:dpa.417.99 (non-existent DPA) → ACK 4.04."""
        resp = coap.get(".well-known/core?rt=urn:knx:dpa.417.99",
                        accept=LINK_FORMAT, timeout=COAP_TIMEOUT)
        assert resp is not None, "No response from DUT"
        assert resp.is_not_found, (
            f"Non-existent resource type should return 4.04, got {resp.code}")

    def test_5_1_1_4e_if_wildcard(self, coap):
        """5.1.1.4e: ?if=* (interface wildcard) → ACK 2.05 with links."""
        resp = coap.get(".well-known/core?if=*",
                        accept=LINK_FORMAT, timeout=COAP_TIMEOUT)
        assert resp is not None, "No response from DUT"
        assert resp.is_successful, (
            f"Interface wildcard should return 2.05, got {resp.code}")
        assert resp.content_format == LINK_FORMAT

        # EITT validation: all lines must have ;if= or rt="p.next"
        text = resp.payload.decode("utf-8", errors="replace")
        lines = [l.strip() for l in text.split(",") if l.strip()]
        for line in lines:
            assert re.search(r'(;if=)|(rt="p\.next")', line), (
                f"All entries must have if= attribute or be p.next, "
                f"got: {line}")


# ===========================================================================
#  5.1.1.900 -- Supplementary: Discovery Pagination over Application FBs
# ===========================================================================

class TestDiscoveryPaginationManyFunctionalBlocks:
    """5.1.1.900: Not an EITT-numbered test case -- local extra tests are
    numbered from 900 upward to avoid colliding with official EITT clause
    numbers.

    api/oc_discovery.c only folds the device's application resources
    (functional blocks, datapoints, etc.) into the /.well-known/core
    `total` count when a filter (rt= or if=) is present on the request
    (see the `rt_len > 0 || if_len > 0` branch ahead of the pagination
    check). ?rt=* is the filter that matches every resource, so it is
    the way to make discovery enumerate "many" application functional
    blocks and exercise the p.next continuation mechanism (EITT
    5.10.1.6 covers the same mechanism against /fp/g).

    The server's p.next link only ever carries the next `pn` value
    (see add_next_page_indicator in api/oc_knx_helpers.c) -- rt and ps
    are silently dropped, so a conforming client must remember its own
    query and merely substitute pn. We do the same here.
    """

    @staticmethod
    def _next_pn(next_href):
        m = re.search(r'[?&]pn=(\d+)', next_href)
        assert m, f"p.next link missing pn=: {next_href}"
        return int(m.group(1))

    def test_5_1_1_900_pagination_follows_next_links_over_all_fbs(self, coap):
        """5.1.1.900: GET /.well-known/core?rt=*&ps=1 must paginate through every
        matching entry (core resources + app resources/FBs) via p.next,
        and a single request sized to that exact total must return the
        same set of resources with no further p.next."""
        ps = 1
        pn = 0
        collected = []
        max_pages = 200  # safety limit against an infinite p.next loop

        for _ in range(max_pages):
            resp = coap.get(
                f".well-known/core?rt=*&pn={pn}&ps={ps}",
                accept=LINK_FORMAT, timeout=COAP_TIMEOUT)
            assert resp is not None and resp.is_successful, (
                f"GET .well-known/core?rt=*&pn={pn}&ps={ps} failed: "
                f"{resp.code if resp else 'timeout'}")
            assert resp.content_format == LINK_FORMAT
            assert resp.payload, "Discovery page returned empty payload"

            links = parse_link_format(resp.payload)
            entries = [l for l in links if l.get("rt") != "p.next"]
            next_links = [l for l in links if l.get("rt") == "p.next"]
            assert len(entries) == 1, (
                f"Expected exactly 1 entry per page with ps=1, "
                f"got {len(entries)} on pn={pn}: {entries}")
            collected.append(entries[0]["href"])

            if not next_links:
                break
            pn = self._next_pn(next_links[0]["href"])
        else:
            pytest.fail(
                f"Exceeded {max_pages} pages while paginating "
                f"/.well-known/core?rt=* -- infinite p.next loop?")

        total = len(collected)
        assert total > 5, (
            "Expected more entries than the fixed core resource set "
            f"once ?rt=* pulls in application resources/FBs, got {total}: "
            f"{collected}")
        assert len(set(collected)) == total, (
            f"Paginated entries should be unique, got duplicates: "
            f"{collected}")

        # Cross-check: a single page sized to the discovered total must
        # return exactly the same resources with no next-page link.
        resp = coap.get(
            f".well-known/core?rt=*&pn=0&ps={total}",
            accept=LINK_FORMAT, timeout=COAP_TIMEOUT)
        assert resp is not None and resp.is_successful, (
            f"GET .well-known/core?rt=*&pn=0&ps={total} failed: "
            f"{resp.code if resp else 'timeout'}")
        links = parse_link_format(resp.payload)
        entries = [l for l in links if l.get("rt") != "p.next"]
        next_links = [l for l in links if l.get("rt") == "p.next"]
        assert not next_links, (
            f"A page sized to the exact total should omit p.next, "
            f"got: {next_links}")
        single_page_hrefs = {e["href"] for e in entries}
        collected_hrefs = set(collected)
        assert single_page_hrefs == collected_hrefs, (
            "Single-page listing differs from the paginated collection.\n"
            f"Missing: {collected_hrefs - single_page_hrefs}\n"
            f"Extra: {single_page_hrefs - collected_hrefs}")

    def test_5_1_1_900_negative_pn_returns_400(self, coap):
        """5.1.1.900: GET /.well-known/core?rt=*&pn=-1 -> 4.00 Bad Request.

        api/oc_discovery.c line 487 explicitly rejects query_pn < 0
        (`first_entry >= total || query_ps < 1 || query_pn < 0`).
        """
        resp = coap.get(
            ".well-known/core?rt=*&pn=-1", accept=LINK_FORMAT,
            timeout=COAP_TIMEOUT)
        assert resp is not None, "No response for GET .well-known/core?rt=*&pn=-1"
        assert resp.code == "4.00", (
            f"Expected 4.00 for negative pn, got {resp.code}")

    def test_5_1_1_900_negative_ps_returns_400(self, coap):
        """5.1.1.900: GET /.well-known/core?rt=*&pn=0&ps=-1 -> 4.00 Bad Request.

        api/oc_discovery.c line 487 rejects query_ps < 1, which covers
        both ps=0 and negative ps values.
        """
        resp = coap.get(
            ".well-known/core?rt=*&pn=0&ps=-1", accept=LINK_FORMAT,
            timeout=COAP_TIMEOUT)
        assert resp is not None, "No response for GET .well-known/core?rt=*&pn=0&ps=-1"
        assert resp.code == "4.00", (
            f"Expected 4.00 for negative ps, got {resp.code}")

    def test_5_1_1_900_ps_zero_returns_400(self, coap):
        """5.1.1.900: GET /.well-known/core?rt=*&pn=0&ps=0 -> 4.00 Bad Request.

        Regression guard: ps=0 must still be rejected after the line 487
        update from `query_ps == 0` to `query_ps < 1`.
        """
        resp = coap.get(
            ".well-known/core?rt=*&pn=0&ps=0", accept=LINK_FORMAT,
            timeout=COAP_TIMEOUT)
        assert resp is not None, "No response for GET .well-known/core?rt=*&pn=0&ps=0"
        assert resp.code == "4.00", (
            f"Expected 4.00 for ps=0, got {resp.code}")

    def test_5_1_1_900_negative_pn_and_ps_returns_400(self, coap):
        """5.1.1.900: GET /.well-known/core?rt=*&pn=-1&ps=-1 -> 4.00 Bad Request.

        Both invalid conditions combined must still yield a single,
        well-defined 4.00 response (not a crash/hang from the
        `first_entry = pn * ps` computation going positive again when
        both operands are negative).
        """
        resp = coap.get(
            ".well-known/core?rt=*&pn=-1&ps=-1", accept=LINK_FORMAT,
            timeout=COAP_TIMEOUT)
        assert resp is not None, "No response for GET .well-known/core?rt=*&pn=-1&ps=-1"
        assert resp.code == "4.00", (
            f"Expected 4.00 for negative pn and ps, got {resp.code}")

    def test_5_1_1_900_non_numeric_pn_returns_400(self, coap):
        """5.1.1.900: GET /.well-known/core?rt=*&pn=abc -> 4.00 Bad Request.

        evaluate_query_px() previously used atoi(), which converted any
        non-numeric value to 0 and silently served page 0. The strtol()
        endptr validation must reject it instead.
        """
        resp = coap.get(
            ".well-known/core?rt=*&pn=abc", accept=LINK_FORMAT,
            timeout=COAP_TIMEOUT)
        assert resp is not None, "No response for GET .well-known/core?rt=*&pn=abc"
        assert resp.code == "4.00", (
            f"Expected 4.00 for non-numeric pn, got {resp.code}")

    def test_5_1_1_900_trailing_garbage_ps_returns_400(self, coap):
        """5.1.1.900: GET /.well-known/core?rt=*&pn=0&ps=2abc -> 4.00 Bad Request.

        strtol() converts '2abc' to 2 without setting errno; only the
        endptr comparison detects the trailing characters.
        """
        resp = coap.get(
            ".well-known/core?rt=*&pn=0&ps=2abc", accept=LINK_FORMAT,
            timeout=COAP_TIMEOUT)
        assert resp is not None, "No response for GET .well-known/core?rt=*&pn=0&ps=2abc"
        assert resp.code == "4.00", (
            f"Expected 4.00 for trailing garbage in ps, got {resp.code}")

    def test_5_1_1_900_out_of_int_range_pn_returns_400(self, coap):
        """5.1.1.900: GET /.well-known/core?rt=*&pn=2147483648 -> 4.00 Bad Request.

        INT_MAX + 1. On a 64-bit target `long` is wider than `int`, so
        strtol() reports no ERANGE; only the explicit `temp > INT_MAX`
        check prevents the truncation into an int.
        """
        resp = coap.get(
            ".well-known/core?rt=*&pn=2147483648", accept=LINK_FORMAT,
            timeout=COAP_TIMEOUT)
        assert resp is not None, "No response for GET .well-known/core?rt=*&pn=2147483648"
        assert resp.code == "4.00", (
            f"Expected 4.00 for out-of-int-range pn, got {resp.code}")

    def test_5_1_1_900_overflow_pn_ps_returns_400(self, coap):
        """5.1.1.900: GET /.well-known/core?rt=*&pn=100000&ps=100000 -> 4.00.

        Regression guard for the signed multiplication overflow in
        `first_entry = pn * ps`: unguarded it wraps to a negative value
        that passes the `first_entry >= total` range check.
        """
        resp = coap.get(
            ".well-known/core?rt=*&pn=100000&ps=100000", accept=LINK_FORMAT,
            timeout=COAP_TIMEOUT)
        assert resp is not None, "No response for GET .well-known/core?rt=*&pn=100000&ps=100000"
        assert resp.code == "4.00", (
            f"Expected 4.00 for overflowing pn*ps, got {resp.code}")
