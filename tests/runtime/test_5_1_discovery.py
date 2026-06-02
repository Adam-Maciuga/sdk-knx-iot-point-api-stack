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

Reference: 08_10_5 KNX IoT Point API Tests v01_01_01_AS
EITT Project: EittProject.xml PlanID 699f219c.699f219b
"""

import re

import pytest

from coap_client import LINK_FORMAT

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
