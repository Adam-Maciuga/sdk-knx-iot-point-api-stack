"""
Runtime conformance tests -- 5.1 Discovery Extended

Tests requiring device state changes (programming mode, group objects).
Replicates EITT certification tests for PM and GA discovery.

Spec test IDs covered:
  5.1.1.5c: Unicast PM query, device in PM → 2.05 with ep
  5.1.1.5d: Unicast PM query, device not in PM → 4.04
  5.1.1.6d: Unicast ?if=urn:knx:if.pm&ep=knx://sn.{sn}, PM on → 2.05
  5.1.1.6e: Unicast PM+ep, PM off → 4.04
  5.1.1.6f: Unicast PM+ep, wrong SN → 4.04
  5.1.1.7:  Multicast GA, single GO → 2.05
  5.1.1.7b: Multicast GA, multiple GO → 2.05
  5.1.1.7c: Multicast GA, no match → no response
  5.1.1.7d: Multicast GA, multi-GA per GO → 2.05 (2 steps)
  5.1.1.7e: Unicast GA, single GO → 2.05
  5.1.1.7f: Unicast GA, multiple GO → 2.05
  5.1.1.7g: Unicast GA, no match → 4.04
  5.1.1.7h: Unicast GA, multi-GA per GO → 2.05 (2 steps)
  5.1.1.8:  Unicast ?d=urn:knx:g.s.* wildcard → 4.00

Reference: 08_10_5 KNX IoT Point API Tests v01_01_01_AS
EITT Project: EittProject.xml PlanID 699f219b.699f219b
"""

import os
import re

import cbor2
import pytest

from coap_client import LINK_FORMAT
from conftest import DUT_IA_HEX, DUT_IID_HEX, DUT_SERIAL_NUMBER, MC_SCOPE

# Multicast interface (same as multicast test file)
DEVICE_IFACE = os.environ.get("DEVICE_IFACE")

COAP_TIMEOUT = 5.0
MCAST_TIMEOUT = 5.0
MCAST_TIMEOUT_NEGATIVE = 5.0


# ===========================================================================
#  5.1.1.5c/d -- Unicast PM discovery
# ===========================================================================

class TestProgrammingModeDiscovery:
    """5.1.1.5c/d: Unicast ?if=urn:knx:if.pm discovery.
    5.1.1.6/6d-f: Unicast ?if=urn:knx:if.pm&ep=knx://sn.{sn} discovery.

    EITT enables/disables PM via separate mechanism; we use OSCORE PUT.
    """

    def test_5_1_1_5c_pm_unicast_device_in_pm(self, coap, oscore_ctx):
        """5.1.1.5c: Unicast, device in PM → 2.05 with ep info."""
        pm_payload = cbor2.dumps({1: True})
        resp_pm = coap.oscore_put(oscore_ctx, "/dev/pm", payload=pm_payload)
        assert resp_pm is not None and resp_pm.is_successful, (
            f"Failed to enable programming mode: {resp_pm}")

        try:
            resp = coap.get(".well-known/core?if=urn:knx:if.pm",
                            accept=LINK_FORMAT, timeout=COAP_TIMEOUT)
            assert resp is not None, "No response from DUT"
            assert resp.is_successful, (
                f"Unicast PM query in PM should return 2.05, got {resp.code}")
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

    def test_5_1_1_5d_pm_unicast_device_not_in_pm(self, coap, oscore_ctx):
        """5.1.1.5d: Unicast, device NOT in PM → 4.04."""
        pm_payload = cbor2.dumps({1: False})
        coap.oscore_put(oscore_ctx, "/dev/pm", payload=pm_payload)

        resp = coap.get(".well-known/core?if=urn:knx:if.pm",
                        accept=LINK_FORMAT, timeout=COAP_TIMEOUT)
        assert resp is not None, "No response from DUT"
        assert resp.is_not_found, (
            f"PM query when not in PM should return 4.04, got {resp.code}")

    def test_5_1_1_6d_pm_ep_correct_sn(self, coap, oscore_ctx):
        """5.1.1.6d: PM + correct SN → 2.05 with ep."""
        pm_payload = cbor2.dumps({1: True})
        resp_pm = coap.oscore_put(oscore_ctx, "/dev/pm", payload=pm_payload)
        assert resp_pm is not None and resp_pm.is_successful

        try:
            resp = coap.get(
                f".well-known/core?if=urn:knx:if.pm&ep=knx://sn.{DUT_SERIAL_NUMBER}",
                accept=LINK_FORMAT, timeout=COAP_TIMEOUT)
            assert resp is not None, "No response from DUT"
            assert resp.is_successful, (
                f"PM+ep with correct SN should return 2.05, got {resp.code}")
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

    def test_5_1_1_6e_pm_ep_not_in_pm(self, coap, oscore_ctx):
        """5.1.1.6e: PM+ep, device NOT in PM → 4.04."""
        pm_payload = cbor2.dumps({1: False})
        coap.oscore_put(oscore_ctx, "/dev/pm", payload=pm_payload)

        resp = coap.get(
            f".well-known/core?if=urn:knx:if.pm&ep=knx://sn.{DUT_SERIAL_NUMBER}",
            accept=LINK_FORMAT, timeout=COAP_TIMEOUT)
        assert resp is not None, (
            "No response from DUT — EITT expects ACK 4.04")
        assert resp.is_not_found, (
            f"PM+ep when PM off should return 4.04, got {resp.code}")

    def test_5_1_1_6f_pm_ep_wrong_sn(self, coap, oscore_ctx):
        """5.1.1.6f: PM+ep, wrong SN → 4.04."""
        pm_payload = cbor2.dumps({1: True})
        resp_pm = coap.oscore_put(oscore_ctx, "/dev/pm", payload=pm_payload)
        assert resp_pm is not None and resp_pm.is_successful

        try:
            resp = coap.get(
                ".well-known/core?if=urn:knx:if.pm&ep=knx://sn.001122334455",
                accept=LINK_FORMAT, timeout=COAP_TIMEOUT)
            assert resp is not None, (
                "No response from DUT — EITT expects ACK 4.04")
            assert resp.is_not_found, (
                f"PM+ep with wrong SN should return 4.04, got {resp.code}")
        finally:
            pm_off = cbor2.dumps({1: False})
            coap.oscore_put(oscore_ctx, "/dev/pm", payload=pm_off)


# ===========================================================================
#  5.1.1.7 -- Group Address Discovery (Unicast + Multicast)
# ===========================================================================

class TestGroupAddressDiscovery:
    """5.1.1.7/7b-7h: ?d=urn:knx:g.s.X group address filter.

    Requires group object table to match EITT configuration:
      GA 1 → /p/1 (single GO)
      GA 2 → /p/2 AND /p/3 (multiple GOs)
      GA 3 → /p/4 (first GA for multi-GA GO)
      GA 4 → /p/4 (second GA for multi-GA GO)
      GA 6 → nothing (non-existent)
    """

    @pytest.fixture(autouse=True)
    def _setup_group_objects(self, coap, oscore_ctx):
        """Configure group object table matching EITT exactly."""
        lsm_load = cbor2.dumps({2: 1})
        resp = coap.oscore_post(oscore_ctx, "/a/lsm", payload=lsm_load)
        if resp is None or not resp.is_successful:
            pytest.skip("Could not set LSM to loading state")

        # GO 1: /p/1 with GA=[1], cflag=0x90 (EITT: 144)
        go1 = cbor2.dumps({0: {0: 1, 11: "/p/1", 7: [1], 8: 0x90}})
        resp1 = coap.oscore_post(oscore_ctx, "/fp/g", payload=go1)
        if resp1 is None or not resp1.is_successful:
            pytest.skip("Could not create GO entry 1")

        # GO 2: /p/2 with GA=[2], cflag=0x48 (EITT: 72)
        go2 = cbor2.dumps({0: {0: 2, 11: "/p/2", 7: [2], 8: 0x48}})
        resp2 = coap.oscore_post(oscore_ctx, "/fp/g", payload=go2)
        if resp2 is None or not resp2.is_successful:
            pytest.skip("Could not create GO entry 2")

        # GO 3: /p/3 with GA=[2], cflag=0x48 (EITT: 72, same GA 2 as GO 2)
        go3 = cbor2.dumps({0: {0: 3, 11: "/p/3", 7: [2], 8: 0x48}})
        resp3 = coap.oscore_post(oscore_ctx, "/fp/g", payload=go3)
        if resp3 is None or not resp3.is_successful:
            pytest.skip("Could not create GO entry 3")

        # GO 4: /p/4 with GA=[3, 4], cflag=0x90 (EITT: 144, multi-GA on one GO)
        go4 = cbor2.dumps({0: {0: 4, 11: "/p/4", 7: [3, 4], 8: 0x90}})
        resp4 = coap.oscore_post(oscore_ctx, "/fp/g", payload=go4)
        if resp4 is None or not resp4.is_successful:
            pytest.skip("Could not create GO entry 4")

        lsm_loaded = cbor2.dumps({2: 2})
        coap.oscore_post(oscore_ctx, "/a/lsm", payload=lsm_loaded)

        yield

        lsm_unload = cbor2.dumps({2: 4})
        coap.oscore_post(oscore_ctx, "/a/lsm", payload=lsm_unload)

    # -- Unicast tests --

    def test_5_1_1_7e_unicast_matching_ga(self, coap):
        """5.1.1.7e: Unicast d=urn:knx:g.s.1 → 2.05 with /p/1."""
        resp = coap.get(".well-known/core?d=urn:knx:g.s.1",
                        accept=LINK_FORMAT, timeout=COAP_TIMEOUT)
        assert resp is not None, "No response from DUT"
        assert resp.is_successful, (
            f"Matching GA should return 2.05, got {resp.code}")
        text = resp.payload.decode("utf-8", errors="replace")
        # EITT validates: <.*/p/1>(?=.*rt=":dpa.417.61".*)(?=.*ct=(60 50|50 60|60).*)
        lines = [l.strip() for l in text.split(",") if l.strip()]
        matched = any(re.search(
            r'<.*/p/1>(?=.*rt=":dpa\.417\.61".*)'
            r'(?=.*ct=((?:60 50)|(?:50 60)|(?:60)).*)', line)
            for line in lines)
        assert matched, (
            f"EITT: /p/1 must have rt=:dpa.417.61 and ct=60. "
            f"Response lines: {lines}")

    def test_5_1_1_7f_unicast_multiple_go(self, coap):
        """5.1.1.7f: Unicast d=urn:knx:g.s.2 → 2.05 with /p/2 and /p/3."""
        resp = coap.get(".well-known/core?d=urn:knx:g.s.2",
                        accept=LINK_FORMAT, timeout=COAP_TIMEOUT)
        assert resp is not None, "No response from DUT"
        assert resp.is_successful, (
            f"GA 2 should return 2.05, got {resp.code}")
        text = resp.payload.decode("utf-8", errors="replace")
        lines = [l.strip() for l in text.split(",") if l.strip()]
        # EITT validates /p/2 with rt=:dpa.417.62 and /p/3 with rt=:dpa.421.61
        matched_p2 = any(re.search(
            r'<.*/p/2>(?=.*rt=":dpa\.417\.62".*)'
            r'(?=.*ct=((?:60 50)|(?:50 60)|(?:60)).*)', line)
            for line in lines)
        assert matched_p2, (
            f"EITT: /p/2 must have rt=:dpa.417.62 and ct=60. Lines: {lines}")
        matched_p3 = any(re.search(
            r'<.*/p/3>(?=.*rt=":dpa\.421\.61".*)'
            r'(?=.*ct=((?:60 50)|(?:50 60)|(?:60)).*)', line)
            for line in lines)
        assert matched_p3, (
            f"EITT: /p/3 must have rt=:dpa.421.61 and ct=60. Lines: {lines}")

    def test_5_1_1_7g_unicast_no_matching_ga(self, coap):
        """5.1.1.7g: Unicast d=urn:knx:g.s.6 → 4.04."""
        resp = coap.get(".well-known/core?d=urn:knx:g.s.6",
                        accept=LINK_FORMAT, timeout=COAP_TIMEOUT)
        assert resp is not None, "No response from DUT"
        assert resp.is_not_found, (
            f"Non-existent GA 6 should return 4.04, got {resp.code}")

    def test_5_1_1_7h_unicast_multi_ga_per_go(self, coap):
        """5.1.1.7h: Unicast, /p/4 has GA=[3,4]. Both queries → /p/4."""
        # Step 1: d=urn:knx:g.s.3 → /p/4
        resp1 = coap.get(".well-known/core?d=urn:knx:g.s.3",
                         accept=LINK_FORMAT, timeout=COAP_TIMEOUT)
        assert resp1 is not None, "No response for GA 3"
        assert resp1.is_successful, (
            f"GA 3 should return 2.05, got {resp1.code}")
        text1 = resp1.payload.decode("utf-8", errors="replace")
        lines1 = [l.strip() for l in text1.split(",") if l.strip()]
        # EITT: <.*/p/4>(?=.*rt=":dpa.421.62".*)(?=.*ct=...)
        matched1 = any(re.search(
            r'<.*/p/4>(?=.*rt=":dpa\.421\.62".*)'
            r'(?=.*ct=((?:60 50)|(?:50 60)|(?:60)).*)', line)
            for line in lines1)
        assert matched1, (
            f"EITT: GA 3 → /p/4 with rt=:dpa.421.62. Lines: {lines1}")

        # Step 2: d=urn:knx:g.s.4 → /p/4
        resp2 = coap.get(".well-known/core?d=urn:knx:g.s.4",
                         accept=LINK_FORMAT, timeout=COAP_TIMEOUT)
        assert resp2 is not None, "No response for GA 4"
        assert resp2.is_successful, (
            f"GA 4 should return 2.05, got {resp2.code}")
        text2 = resp2.payload.decode("utf-8", errors="replace")
        lines2 = [l.strip() for l in text2.split(",") if l.strip()]
        matched2 = any(re.search(
            r'<.*/p/4>(?=.*rt=":dpa\.421\.62".*)'
            r'(?=.*ct=((?:60 50)|(?:50 60)|(?:60)).*)', line)
            for line in lines2)
        assert matched2, (
            f"EITT: GA 4 → /p/4 with rt=:dpa.421.62. Lines: {lines2}")

    # -- Multicast tests --

    @pytest.mark.skipif(
        os.environ.get("DEVICE_IFACE") is None,
        reason="Multicast tests require DEVICE_IFACE")
    def test_5_1_1_7_multicast_ga_single_go(self, coap):
        """5.1.1.7: Multicast d=urn:knx:g.s.1 → NON 2.05 with /p/1."""
        responses = coap.multicast_get(
            ".well-known/core?d=urn:knx:g.s.1",
            scope=MC_SCOPE, interface=DEVICE_IFACE,
            accept=LINK_FORMAT,
            collect_timeout=MCAST_TIMEOUT)
        assert len(responses) >= 1, "No response to GA 1 multicast"
        resp = responses[0]
        assert resp.is_successful, f"Expected 2.05, got {resp.code}"
        text = resp.payload.decode("utf-8", errors="replace")
        lines = [l.strip() for l in text.split(",") if l.strip()]
        matched = any(re.search(
            r'<.*/p/1>(?=.*rt=":dpa\.417\.61".*)'
            r'(?=.*ct=((?:60 50)|(?:50 60)|(?:60)).*)', line)
            for line in lines)
        assert matched, (
            f"EITT: /p/1 must have rt=:dpa.417.61 and ct=60. Lines: {lines}")

    @pytest.mark.skipif(
        os.environ.get("DEVICE_IFACE") is None,
        reason="Multicast tests require DEVICE_IFACE")
    def test_5_1_1_7b_multicast_ga_multiple_go(self, coap):
        """5.1.1.7b: Multicast d=urn:knx:g.s.2 → NON 2.05 with /p/2,/p/3."""
        responses = coap.multicast_get(
            ".well-known/core?d=urn:knx:g.s.2",
            scope=MC_SCOPE, interface=DEVICE_IFACE,
            accept=LINK_FORMAT,
            collect_timeout=MCAST_TIMEOUT)
        assert len(responses) >= 1, "No response to GA 2 multicast"
        resp = responses[0]
        assert resp.is_successful, f"Expected 2.05, got {resp.code}"
        text = resp.payload.decode("utf-8", errors="replace")
        lines = [l.strip() for l in text.split(",") if l.strip()]
        matched_p2 = any(re.search(
            r'<.*/p/2>(?=.*rt=":dpa\.417\.62".*)'
            r'(?=.*ct=((?:60 50)|(?:50 60)|(?:60)).*)', line)
            for line in lines)
        assert matched_p2, (
            f"EITT: /p/2 must have rt=:dpa.417.62. Lines: {lines}")
        matched_p3 = any(re.search(
            r'<.*/p/3>(?=.*rt=":dpa\.421\.61".*)'
            r'(?=.*ct=((?:60 50)|(?:50 60)|(?:60)).*)', line)
            for line in lines)
        assert matched_p3, (
            f"EITT: /p/3 must have rt=:dpa.421.61. Lines: {lines}")

    @pytest.mark.skipif(
        os.environ.get("DEVICE_IFACE") is None,
        reason="Multicast tests require DEVICE_IFACE")
    def test_5_1_1_7c_multicast_ga_no_match(self, coap):
        """5.1.1.7c: Multicast d=urn:knx:g.s.6 → no response (5s wait)."""
        responses = coap.multicast_get(
            ".well-known/core?d=urn:knx:g.s.6",
            scope=MC_SCOPE, interface=DEVICE_IFACE,
            accept=LINK_FORMAT,
            collect_timeout=MCAST_TIMEOUT_NEGATIVE)
        assert len(responses) == 0, (
            f"Non-existent GA 6 multicast should get no response, "
            f"got {len(responses)}")

    @pytest.mark.skipif(
        os.environ.get("DEVICE_IFACE") is None,
        reason="Multicast tests require DEVICE_IFACE")
    def test_5_1_1_7d_multicast_multi_ga_per_go(self, coap):
        """5.1.1.7d: Multicast, /p/4 has GA=[3,4]. Both queries → /p/4."""
        # Step 1: d=urn:knx:g.s.3 → /p/4
        responses1 = coap.multicast_get(
            ".well-known/core?d=urn:knx:g.s.3",
            scope=MC_SCOPE, interface=DEVICE_IFACE,
            accept=LINK_FORMAT,
            collect_timeout=MCAST_TIMEOUT)
        assert len(responses1) >= 1, "No response to GA 3 multicast"
        resp1 = responses1[0]
        assert resp1.is_successful, f"GA 3 expected 2.05, got {resp1.code}"
        text1 = resp1.payload.decode("utf-8", errors="replace")
        lines1 = [l.strip() for l in text1.split(",") if l.strip()]
        matched1 = any(re.search(
            r'<.*/p/4>(?=.*rt=":dpa\.421\.62".*)'
            r'(?=.*ct=((?:60 50)|(?:50 60)|(?:60)).*)', line)
            for line in lines1)
        assert matched1, (
            f"EITT: GA 3 → /p/4 with rt=:dpa.421.62. Lines: {lines1}")

        # Step 2: d=urn:knx:g.s.4 → /p/4
        responses2 = coap.multicast_get(
            ".well-known/core?d=urn:knx:g.s.4",
            scope=MC_SCOPE, interface=DEVICE_IFACE,
            accept=LINK_FORMAT,
            collect_timeout=MCAST_TIMEOUT)
        assert len(responses2) >= 1, "No response to GA 4 multicast"
        resp2 = responses2[0]
        assert resp2.is_successful, f"GA 4 expected 2.05, got {resp2.code}"
        text2 = resp2.payload.decode("utf-8", errors="replace")
        lines2 = [l.strip() for l in text2.split(",") if l.strip()]
        matched2 = any(re.search(
            r'<.*/p/4>(?=.*rt=":dpa\.421\.62".*)'
            r'(?=.*ct=((?:60 50)|(?:50 60)|(?:60)).*)', line)
            for line in lines2)
        assert matched2, (
            f"EITT: GA 4 → /p/4 with rt=:dpa.421.62. Lines: {lines2}")


# ===========================================================================
#  5.1.1.8 -- Invalid GA Wildcard
# ===========================================================================

class TestGAWildcard:
    """5.1.1.8: d=urn:knx:g.s.* wildcard must fail."""

    def test_5_1_1_8_ga_wildcard_rejected(self, coap):
        """5.1.1.8: ?d=urn:knx:g.s.* → 4.00 Bad Request."""
        resp = coap.get(".well-known/core?d=urn:knx:g.s.*",
                        accept=LINK_FORMAT, timeout=COAP_TIMEOUT)
        assert resp is not None, "No response from DUT"
        assert resp.is_bad_request, (
            f"GA wildcard should return 4.00, got {resp.code}")
