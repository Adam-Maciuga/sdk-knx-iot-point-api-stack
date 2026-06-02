"""
Runtime conformance tests -- 5.1 Multicast Discovery

Replicates EITT certification tests for multicast CoAP discovery.
Requires the DUT to be bound to an interface that supports IPv6 multicast.

Spec test IDs covered:
  5.1.1.1:  Multicast Discovery via /.well-known/core (site-local scope)
  5.1.1.3:  Multicast Discovery with filters rt, if & ep (5 steps)
  5.1.1.3b: Invalid multicast query → no response
  5.1.1.3c: Invalid multicast interface filter → no response
  5.1.1.3d: Invalid multicast resource type filter → no response
  5.1.1.3e: Multicast interface wildcard → 2.05
  5.1.1.5:  Multicast PM query, device in PM → 2.05
  5.1.1.5b: Multicast PM query, device not in PM → no response
  5.1.1.6:  Multicast PM+ep, correct SN, in PM → 2.05
  5.1.1.6b: Multicast PM+ep, not in PM → no response
  5.1.1.6c: Multicast PM+ep, wrong SN, in PM → no response
  5.1.1.9a: Multicast IA filter, correct IID/IA → 2.05
  5.1.1.9b: Multicast IA filter, wrong IID → no response
  5.1.1.9c: Multicast IA filter, wrong IA → no response

Reference: 08_10_5 KNX IoT Point API Tests v01_01_01_AS
EITT Project: EittProject.xml PlanID 699f219c.699f219b
"""

import os
import re

import cbor2
import pytest

from coap_client import LINK_FORMAT
from conftest import DUT_IA_HEX, DUT_IID_HEX, DUT_SERIAL_NUMBER, MC_SCOPE

# Multicast tests need a real interface; skip if not configured
DEVICE_IFACE = os.environ.get("DEVICE_IFACE")

# Standard multicast collect timeout (EITT uses 5.5s TimeToNext)
MCAST_TIMEOUT = 5.0

# Timeout for negative tests (EITT: "Wait for 5 seconds")
MCAST_TIMEOUT_NEGATIVE = 5.0

pytestmark = pytest.mark.skipif(
    DEVICE_IFACE is None,
    reason="Multicast tests require DEVICE_IFACE env var (e.g. veth-test)"
)


# ===========================================================================
#  5.1.1.1 -- Multicast Discovery via /.well-known/core
# ===========================================================================

class TestMulticastDiscovery:
    """5.1.1.1: NON GET coap://{multicast}/.well-known/core

    EITT sends NON GET with Accept: application/link-format at
    site-local scope (ff05::fd). Expects NON 2.05 Content with:
      <>;ep="knx://sn.{sn} knx://ia.{iid}.{ia}"
    """

    def test_5_1_1_1_site_local_discovery(self, coap):
        """5.1.1.1 step 3: Site-local multicast /.well-known/core returns
        2.05 with ep containing serial number and ia."""
        responses = coap.multicast_get(
            ".well-known/core",
            scope=MC_SCOPE, interface=DEVICE_IFACE,
            accept=LINK_FORMAT,
            collect_timeout=MCAST_TIMEOUT)
        assert len(responses) >= 1, (
            "Multicast site-local discovery should get at least one response")

        resp = responses[0]
        assert resp.is_successful, f"Expected 2.05, got {resp.code}"
        assert resp.content_format == LINK_FORMAT, (
            f"Expected link-format, got {resp.content_format}")

        text = resp.payload.decode("utf-8", errors="replace")
        # EITT validates: <>;ep="knx://sn.00fa10020800 knx://ia.{iid}.{ia}"
        assert f"knx://sn.{DUT_SERIAL_NUMBER}" in text, (
            f"Response should contain serial number, got: {text[:200]}")
        assert "knx://ia." in text, (
            f"Response should contain ia, got: {text[:200]}")

    def test_5_1_1_1_second_request(self, coap):
        """5.1.1.1 step 4: Second site-local multicast GET → any 2.05."""
        responses = coap.multicast_get(
            ".well-known/core",
            scope=MC_SCOPE, interface=DEVICE_IFACE,
            accept=LINK_FORMAT,
            collect_timeout=MCAST_TIMEOUT)
        assert len(responses) >= 1, (
            "Second multicast discovery should also get a response")
        assert responses[0].is_successful, (
            f"Expected 2.05, got {responses[0].code}")


# ===========================================================================
#  5.1.1.3 -- Multicast Discovery with filters rt, if & ep
# ===========================================================================

class TestMulticastDiscoveryFilters:
    """5.1.1.3: NON GET multicast /.well-known/core with query filters.

    EITT tests 5 steps, each with a different filter.
    """

    def test_5_1_1_3_step1_rt_known_value(self, coap):
        """5.1.1.3 step 1: ?rt=urn:knx:dpa.417.61 → 2.05 with matching rt."""
        responses = coap.multicast_get(
            ".well-known/core?rt=urn:knx:dpa.417.61",
            scope=MC_SCOPE, interface=DEVICE_IFACE,
            accept=LINK_FORMAT,
            collect_timeout=MCAST_TIMEOUT)
        assert len(responses) >= 1, "No response to rt filter"
        resp = responses[0]
        assert resp.is_successful, f"Expected 2.05, got {resp.code}"

        # EITT validation: all lines must contain rt=":dpa.417.61"
        text = resp.payload.decode("utf-8", errors="replace")
        lines = [l.strip() for l in text.split(",") if l.strip()]
        for line in lines:
            assert re.search(r'rt=":dpa\.417\.61', line), (
                f"All entries must match rt filter, got: {line}")

    def test_5_1_1_3_step2_rt_wildcard(self, coap):
        """5.1.1.3 step 2: ?rt=* → 2.05 with entries having rt."""
        responses = coap.multicast_get(
            ".well-known/core?rt=*",
            scope=MC_SCOPE, interface=DEVICE_IFACE,
            accept=LINK_FORMAT,
            collect_timeout=MCAST_TIMEOUT)
        assert len(responses) >= 1, "No response to rt wildcard"
        resp = responses[0]
        assert resp.is_successful, f"Expected 2.05, got {resp.code}"

        # EITT validation: all lines must have ;rt=
        text = resp.payload.decode("utf-8", errors="replace")
        lines = [l.strip() for l in text.split(",") if l.strip()]
        for line in lines:
            assert re.search(r';rt=', line), (
                f"Wildcard rt should return entries with rt, got: {line}")

    def test_5_1_1_3_step3_if_known_value(self, coap):
        """5.1.1.3 step 3: ?if=urn:knx:if.o → 2.05 with matching if."""
        responses = coap.multicast_get(
            ".well-known/core?if=urn:knx:if.o",
            scope=MC_SCOPE, interface=DEVICE_IFACE,
            accept=LINK_FORMAT,
            collect_timeout=MCAST_TIMEOUT)
        assert len(responses) >= 1, "No response to if filter"
        resp = responses[0]
        assert resp.is_successful, f"Expected 2.05, got {resp.code}"

        # EITT validation: all lines must contain if=":if.o"
        text = resp.payload.decode("utf-8", errors="replace")
        lines = [l.strip() for l in text.split(",") if l.strip()]
        for line in lines:
            assert re.search(r'if=":if\.o', line), (
                f"All entries must match if filter, got: {line}")

    def test_5_1_1_3_step4_ep_serial_number(self, coap):
        """5.1.1.3 step 4: ?ep=knx://sn.{sn} → 2.05 with ep."""
        responses = coap.multicast_get(
            f".well-known/core?ep=knx://sn.{DUT_SERIAL_NUMBER}",
            scope=MC_SCOPE, interface=DEVICE_IFACE,
            accept=LINK_FORMAT,
            collect_timeout=MCAST_TIMEOUT)
        assert len(responses) >= 1, "No response to ep serial filter"
        resp = responses[0]
        assert resp.is_successful, f"Expected 2.05, got {resp.code}"

        # EITT validation: response contains ep with sn and ia
        text = resp.payload.decode("utf-8", errors="replace")
        assert f"knx://sn.{DUT_SERIAL_NUMBER}" in text, (
            f"Response should contain serial number, got: {text[:200]}")
        assert "knx://ia." in text, (
            f"Response should contain ia, got: {text[:200]}")

    def test_5_1_1_3_step5_ep_sn_wildcard(self, coap):
        """5.1.1.3 step 5: ?ep=knx://sn.* → 2.05 with ep."""
        responses = coap.multicast_get(
            ".well-known/core?ep=knx://sn.*",
            scope=MC_SCOPE, interface=DEVICE_IFACE,
            accept=LINK_FORMAT,
            collect_timeout=MCAST_TIMEOUT)
        assert len(responses) >= 1, "No response to ep wildcard"
        resp = responses[0]
        assert resp.is_successful, f"Expected 2.05, got {resp.code}"

        text = resp.payload.decode("utf-8", errors="replace")
        assert f"knx://sn.{DUT_SERIAL_NUMBER}" in text, (
            f"Response should contain serial number, got: {text[:200]}")
        assert "knx://ia." in text, (
            f"Response should contain ia, got: {text[:200]}")


# ===========================================================================
#  5.1.1.3b -- Invalid Multicast Discovery Query
# ===========================================================================

class TestInvalidMulticastQueries:
    """5.1.1.3b-d: Invalid multicast queries → DUT must NOT respond.

    Per spec, a device that receives a multicast discovery with an
    unrecognised/non-matching query MUST silently ignore it.

    EITT waits 5 seconds and verifies no response is received.
    """

    def test_5_1_1_3b_invalid_query_key(self, coap):
        """5.1.1.3b: ?invalid-coap-rd-query → no response for 5s."""
        responses = coap.multicast_get(
            ".well-known/core?invalid-coap-rd-query",
            scope=MC_SCOPE, interface=DEVICE_IFACE,
            collect_timeout=MCAST_TIMEOUT_NEGATIVE)
        assert len(responses) == 0, (
            f"Invalid multicast query should get no response, "
            f"got {len(responses)}")

    def test_5_1_1_3c_invalid_interface(self, coap):
        """5.1.1.3c: ?if=urn:knx:if.k (non-existent) → no response for 5s."""
        responses = coap.multicast_get(
            ".well-known/core?if=urn:knx:if.k",
            scope=MC_SCOPE, interface=DEVICE_IFACE,
            collect_timeout=MCAST_TIMEOUT_NEGATIVE)
        assert len(responses) == 0, (
            f"Non-existent interface multicast should get no response, "
            f"got {len(responses)}")

    def test_5_1_1_3d_invalid_resource_type(self, coap):
        """5.1.1.3d: ?rt=urn:knx:dpa.417.99 (non-existent) → no response."""
        responses = coap.multicast_get(
            ".well-known/core?rt=urn:knx:dpa.417.99",
            scope=MC_SCOPE, interface=DEVICE_IFACE,
            collect_timeout=MCAST_TIMEOUT_NEGATIVE)
        assert len(responses) == 0, (
            f"Non-existent rt multicast should get no response, "
            f"got {len(responses)}")


# ===========================================================================
#  5.1.1.3e -- Multicast Interface Wildcard
# ===========================================================================

class TestMulticastInterfaceWildcard:
    """5.1.1.3e: Multicast with ?if=* wildcard → 2.05.

    EITT validates: all response lines must have ;if= attribute
    or contain rt="p.next" (pagination).
    """

    def test_5_1_1_3e_if_wildcard(self, coap):
        """5.1.1.3e: ?if=* → at least one response with if attributes."""
        responses = coap.multicast_get(
            ".well-known/core?if=*",
            scope=MC_SCOPE, interface=DEVICE_IFACE,
            accept=LINK_FORMAT,
            collect_timeout=MCAST_TIMEOUT)
        assert len(responses) >= 1, (
            "Multicast if=* wildcard should get at least one response")

        resp = responses[0]
        assert resp.is_successful, f"Expected 2.05, got {resp.code}"

        # EITT validation: all lines must have ;if= or rt="p.next"
        text = resp.payload.decode("utf-8", errors="replace")
        lines = [l.strip() for l in text.split(",") if l.strip()]
        for line in lines:
            assert re.search(r'(;if=)|(rt="p\.next")', line), (
                f"All entries must have if= or be p.next, got: {line}")


# ===========================================================================
#  5.1.1.5/5b -- Multicast Programming Mode Discovery
# ===========================================================================

class TestMulticastProgrammingModeDiscovery:
    """5.1.1.5/5b: Multicast ?if=urn:knx:if.pm filter.

    Requires OSCORE context to toggle PM on the DUT.
    """

    def test_5_1_1_5_pm_on_returns_response(self, coap, oscore_ctx):
        """5.1.1.5: PM on, multicast ?if=urn:knx:if.pm → NON 2.05 with ep."""
        # Enable programming mode
        pm_on = cbor2.dumps({1: True})
        resp_pm = coap.oscore_put(oscore_ctx, "/dev/pm", payload=pm_on)
        assert resp_pm is not None and resp_pm.is_successful, (
            "Failed to enable PM")

        try:
            responses = coap.multicast_get(
                ".well-known/core?if=urn:knx:if.pm",
                scope=MC_SCOPE, interface=DEVICE_IFACE,
                accept=LINK_FORMAT,
                collect_timeout=MCAST_TIMEOUT)
            assert len(responses) >= 1, (
                "PM multicast should get at least one response when PM is on")
            resp = responses[0]
            assert resp.is_successful, f"Expected 2.05, got {resp.code}"
            text = resp.payload.decode("utf-8", errors="replace")
            # EITT validates: <>;ep="knx://sn.{sn} knx://ia.{iid}.{ia}"
            ep_pattern = (
                rf'<>;ep="knx://sn\.{DUT_SERIAL_NUMBER} '
                rf'knx://ia\.{DUT_IID_HEX}\.{DUT_IA_HEX}"'
            )
            assert re.search(ep_pattern, text), (
                f"EITT ep= pattern not matched: {ep_pattern}\n"
                f"Response: {text[:300]}")
        finally:
            pm_off = cbor2.dumps({1: False})
            coap.oscore_put(oscore_ctx, "/dev/pm", payload=pm_off)

    def test_5_1_1_5b_pm_off_no_response(self, coap, oscore_ctx):
        """5.1.1.5b: PM off, multicast ?if=urn:knx:if.pm → no response."""
        # Ensure PM is off
        pm_off = cbor2.dumps({1: False})
        coap.oscore_put(oscore_ctx, "/dev/pm", payload=pm_off)

        responses = coap.multicast_get(
            ".well-known/core?if=urn:knx:if.pm",
            scope=MC_SCOPE, interface=DEVICE_IFACE,
            accept=LINK_FORMAT,
            collect_timeout=MCAST_TIMEOUT_NEGATIVE)
        assert len(responses) == 0, (
            f"PM multicast should get no response when PM is off, "
            f"got {len(responses)}")


# ===========================================================================
#  5.1.1.6/6b/6c -- Multicast PM+ep Discovery
# ===========================================================================

class TestMulticastPMEndpointDiscovery:
    """5.1.1.6/6b/6c: Multicast ?if=urn:knx:if.pm&ep=knx://sn.{sn}."""

    def test_5_1_1_6_pm_ep_correct_sn(self, coap, oscore_ctx):
        """5.1.1.6: PM on, correct SN → NON 2.05 with ep."""
        pm_on = cbor2.dumps({1: True})
        resp_pm = coap.oscore_put(oscore_ctx, "/dev/pm", payload=pm_on)
        assert resp_pm is not None and resp_pm.is_successful, (
            "Failed to enable PM")

        try:
            responses = coap.multicast_get(
                f".well-known/core?if=urn:knx:if.pm"
                f"&ep=knx://sn.{DUT_SERIAL_NUMBER}",
                scope=MC_SCOPE, interface=DEVICE_IFACE,
                accept=LINK_FORMAT,
                collect_timeout=MCAST_TIMEOUT)
            assert len(responses) >= 1, (
                "PM+ep multicast with correct SN should get response")
            resp = responses[0]
            assert resp.is_successful, f"Expected 2.05, got {resp.code}"
            text = resp.payload.decode("utf-8", errors="replace")
            # EITT validates: <>;ep="knx://sn.{sn} knx://ia.{iid}.{ia}"
            ep_pattern = (
                rf'<>;ep="knx://sn\.{DUT_SERIAL_NUMBER} '
                rf'knx://ia\.{DUT_IID_HEX}\.{DUT_IA_HEX}"'
            )
            assert re.search(ep_pattern, text), (
                f"EITT ep= pattern not matched: {ep_pattern}\n"
                f"Response: {text[:300]}")
        finally:
            pm_off = cbor2.dumps({1: False})
            coap.oscore_put(oscore_ctx, "/dev/pm", payload=pm_off)

    def test_5_1_1_6b_pm_off_no_response(self, coap, oscore_ctx):
        """5.1.1.6b: PM off, multicast PM+ep → no response."""
        pm_off = cbor2.dumps({1: False})
        coap.oscore_put(oscore_ctx, "/dev/pm", payload=pm_off)

        responses = coap.multicast_get(
            f".well-known/core?if=urn:knx:if.pm"
            f"&ep=knx://sn.{DUT_SERIAL_NUMBER}",
            scope=MC_SCOPE, interface=DEVICE_IFACE,
            accept=LINK_FORMAT,
            collect_timeout=MCAST_TIMEOUT_NEGATIVE)
        assert len(responses) == 0, (
            f"PM+ep multicast should get no response when PM is off, "
            f"got {len(responses)}")

    def test_5_1_1_6c_pm_on_wrong_sn(self, coap, oscore_ctx):
        """5.1.1.6c: PM on, wrong SN (001122334455) → no response."""
        pm_on = cbor2.dumps({1: True})
        resp_pm = coap.oscore_put(oscore_ctx, "/dev/pm", payload=pm_on)
        assert resp_pm is not None and resp_pm.is_successful, (
            "Failed to enable PM")

        try:
            responses = coap.multicast_get(
                ".well-known/core?if=urn:knx:if.pm"
                "&ep=knx://sn.001122334455",
                scope=MC_SCOPE, interface=DEVICE_IFACE,
                accept=LINK_FORMAT,
                collect_timeout=MCAST_TIMEOUT_NEGATIVE)
            assert len(responses) == 0, (
                f"PM+ep multicast with wrong SN should get no response, "
                f"got {len(responses)}")
        finally:
            pm_off = cbor2.dumps({1: False})
            coap.oscore_put(oscore_ctx, "/dev/pm", payload=pm_off)


# ===========================================================================
#  5.1.1.9a/9b/9c -- Multicast IA Discovery
# ===========================================================================

class TestMulticastIADiscovery:
    """5.1.1.9a/9b/9c: Multicast ?ep=knx://ia.{iid}.{ia} filter.

    IID and IA are set in conftest to match EITT configuration:
      IID = 0x1199887766 → hex "1199887766"
      IA  = 0x1101       → hex "1101"
    """

    # Correct values matching conftest/EITT
    CORRECT_IID_HEX = "1199887766"
    CORRECT_IA_HEX = "1101"

    # Wrong values matching EITT
    WRONG_IID_HEX = "23556165ba"
    WRONG_IA_HEX = "110a"

    def test_5_1_1_9a_correct_iid_ia(self, coap, oscore_ctx):
        """5.1.1.9a: Multicast ep=knx://ia.{iid}.{ia} → NON 2.05 with ep."""
        responses = coap.multicast_get(
            f".well-known/core?ep=knx://ia."
            f"{self.CORRECT_IID_HEX}.{self.CORRECT_IA_HEX}",
            scope=MC_SCOPE, interface=DEVICE_IFACE,
            accept=LINK_FORMAT,
            collect_timeout=MCAST_TIMEOUT)
        assert len(responses) >= 1, (
            "Correct IID/IA multicast should get at least one response")
        resp = responses[0]
        assert resp.is_successful, f"Expected 2.05, got {resp.code}"
        text = resp.payload.decode("utf-8", errors="replace")
        # EITT validates: <>;ep="knx://sn.{sn} knx://ia.{iid}.{ia}"
        ep_pattern = (
            rf'<>;ep="knx://sn\.{DUT_SERIAL_NUMBER} '
            rf'knx://ia\.{self.CORRECT_IID_HEX}\.{self.CORRECT_IA_HEX}"'
        )
        assert re.search(ep_pattern, text), (
            f"EITT ep= pattern not matched: {ep_pattern}\n"
            f"Response: {text[:300]}")

    def test_5_1_1_9b_wrong_iid(self, coap, oscore_ctx):
        """5.1.1.9b: Multicast with wrong IID → no response (5s wait)."""
        responses = coap.multicast_get(
            f".well-known/core?ep=knx://ia."
            f"{self.WRONG_IID_HEX}.{self.CORRECT_IA_HEX}",
            scope=MC_SCOPE, interface=DEVICE_IFACE,
            accept=LINK_FORMAT,
            collect_timeout=MCAST_TIMEOUT_NEGATIVE)
        assert len(responses) == 0, (
            f"Wrong IID multicast should get no response, "
            f"got {len(responses)}")

    def test_5_1_1_9c_wrong_ia(self, coap, oscore_ctx):
        """5.1.1.9c: Multicast with wrong IA → no response (5s wait)."""
        responses = coap.multicast_get(
            f".well-known/core?ep=knx://ia."
            f"{self.CORRECT_IID_HEX}.{self.WRONG_IA_HEX}",
            scope=MC_SCOPE, interface=DEVICE_IFACE,
            accept=LINK_FORMAT,
            collect_timeout=MCAST_TIMEOUT_NEGATIVE)
        assert len(responses) == 0, (
            f"Wrong IA multicast should get no response, "
            f"got {len(responses)}")
