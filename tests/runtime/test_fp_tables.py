"""
Runtime conformance tests — EITT 5.5 Function Point Tables

35 of the 38 tests in this file replicated a template TestCase one-for-one
and were removed with the tier split; they run in the conformance tier now.

What remains are the three cases the template does NOT have, all probing
the tables' create-vs-update discrimination on an unknown id:

5.5.1.6b  POST /fp/g with an unused id must CREATE and return 2.01
5.5.9.5   Re-POST of two existing publishers with changed GAs → 2.04
5.5.9.5b  POST /fp/p with an unused id must CREATE and return 2.01

Plus probes the template does not have (ADDITIONAL, non-EITT):

Repeated CBOR key in one table entry - a repeated key survives the parser
          as a separate rep; counting found properties let one key stand in
          for another (e.g. 'ga' twice and no 'ia'/'grpid' was created).  On
          /fp/g, /fp/p and /fp/r the last occurrence wins (as on /auth/at),
          a missing mandatory key is still 4.00; the template's mixed
          multicast/unicast POST (5.5.5.4, telegram 210) must still create
          all entries

RCP 'ia' (12) with 'at' (14) - spec 2.5.7.4.2 Table 21 marks both "C u"
          (direct unicast); the template only stores 'ia' without 'at' (its
          5.5.5.4 variant with 'at' is disabled), so the read-back of a
          unicast 'at' is checked here.  The stack does not reject 'ia'
          without 'at' or 'at' on a 'grpid' entry: the template sends both
          (see the note in oc_core_fp_r_post_handler)

Reference: 08_10_5 KNX IoT Point API Tests v01_01_01_AS
"""

import cbor2
import pytest

from transport.coap_client import APPLICATION_CBOR

from fixtures import set_lsm


# ===========================================================================
#  5.5.1 -- Group Object Table Write (POST /fp/g)
# ===========================================================================

class TestGroupObjectTableWrite:
    """EITT 5.5.1.x — Write / Update / Delete Group Object table entries."""

    @pytest.fixture(autouse=True)
    def _setup(self, coap, oscore_ctx):
        """EITT: unload + loading (no factory reset between tests)."""
        set_lsm(coap, oscore_ctx, 4)  # unload
        set_lsm(coap, oscore_ctx, 1)  # loading
        self.coap = coap
        self.ctx = oscore_ctx

    # -- 5.5.1.6b Update non-existing GO -----------------------------------
    def test_post_nonexisting_go_creates_entry(self):
        """POST with non-existing id creates new entry (2.01)."""
        resp = self.coap.oscore_post(
            self.ctx, "/fp/g",
            payload=cbor2.dumps([{0: 999, 7: [500], 8: 0x01, 11: "/p/1"}]))
        assert resp is not None and resp.code == "2.01", (
            f"Expected 2.01 (non-existing id creates), got "
            f"{resp.code if resp else 'timeout'}")


# ===========================================================================
#  5.5.9 -- Publisher Table Write (POST /fp/p)
# ===========================================================================

class TestPublisherTableWrite:
    """EITT 5.5.9.x — Write / Update / Delete Publisher table entries."""

    @pytest.fixture(autouse=True)
    def _setup(self, coap, oscore_ctx):
        # EITT: unload + loading (no factory reset between tests)
        set_lsm(coap, oscore_ctx, 4)  # unload
        set_lsm(coap, oscore_ctx, 1)  # loading
        self.coap = coap
        self.ctx = oscore_ctx

    # -- 5.5.9.5 Update multiple publishers --------------------------------
    def test_update_multiple_publishers(self):
        """Create publishers, then POST same ids → 2.04."""
        entries = [
            {0: 10, 13: 0x80000001, 7: [100]},
            {0: 11, 13: 0x80000002, 7: [101]},
        ]
        resp1 = self.coap.oscore_post(
            self.ctx, "/fp/p", payload=cbor2.dumps(entries))
        assert resp1 is not None and resp1.code == "2.01"

        updated = [
            {0: 10, 13: 0x80000001, 7: [200, 201]},
            {0: 11, 13: 0x80000002, 7: [202, 203]},
        ]
        resp2 = self.coap.oscore_post(
            self.ctx, "/fp/p", payload=cbor2.dumps(updated))
        assert resp2 is not None and resp2.code == "2.04"

    # -- 5.5.9.5b Update non-existing publisher ----------------------------
    def test_post_nonexisting_publisher_creates_entry(self):
        """POST with non-existing id creates new entry (2.01)."""
        resp = self.coap.oscore_post(
            self.ctx, "/fp/p",
            payload=cbor2.dumps([{0: 999, 13: 0x80000099, 7: [500]}]))
        assert resp is not None and resp.code == "2.01"


# ===========================================================================
#  Recipient Table - unicast 'ia' (12) with 'at' (14) (ADDITIONAL)
# ===========================================================================

RCP_IA = 0x110F
RCP_AT = "rcp-unicast-token"


class TestRecipientTableUnicastAccessToken:
    """POST /fp/r with a direct unicast entry ('ia' + 'at', spec 2.5.7.4.2
    Table 21, both "C u") is stored and reads back with 'at' under key 14."""

    @pytest.fixture(autouse=True)
    def _setup(self, coap, oscore_ctx):
        set_lsm(coap, oscore_ctx, 4)  # unload
        set_lsm(coap, oscore_ctx, 1)  # loading
        self.coap = coap
        self.ctx = oscore_ctx

    def test_ia_with_at_is_stored_and_read_back(self):
        resp = self.coap.oscore_post(self.ctx, "/fp/r", payload=cbor2.dumps([{0: 31, 7: [500], 12: RCP_IA, 14: RCP_AT}]))
        assert resp is not None, "POST /fp/r timed out"
        assert resp.code == "2.01", f"ia + at: expected 2.01, got {resp.code}"

        resp = self.coap.oscore_get(self.ctx, "/fp/r/31", accept=APPLICATION_CBOR)
        assert resp is not None, "GET /fp/r/31 timed out"
        assert resp.code == "2.05", f"GET /fp/r/31: expected 2.05, got {resp.code}"
        entry = cbor2.loads(resp.payload)
        assert entry.get(12) == RCP_IA, f"read-back ia: expected {RCP_IA}, got {entry}"
        assert entry.get(14) == RCP_AT, f"read-back at (14): expected '{RCP_AT}', got {entry}"


# ===========================================================================
#  Repeated CBOR key in one table entry (ADDITIONAL)
# ===========================================================================

class RawCbor(bytes):
    """A value already encoded as CBOR, written as it is (e.g. a half float)."""


def _cbor_entries(*entries):
    """CBOR array of maps given as (key, value) pair lists, so a key can repeat
    (a Python dict cannot hold a repeated key)."""
    out = bytes([0x80 + len(entries)])
    for pairs in entries:
        out += bytes([0xA0 + len(pairs)])
        for key, value in pairs:
            out += cbor2.dumps(key) + (bytes(value) if isinstance(value, RawCbor) else cbor2.dumps(value))
    return out


TBL_GRPID = 0x80000001
TBL_IA = 0x2202


class TestTableRepeatedKey:
    """A repeated key in one entry of POST /fp/g, /fp/p, /fp/r: the last
    occurrence wins; the mandatory check counts which keys are present, not
    how often, so a repeated 'ga' cannot stand in for 'ia'/'grpid'. Unicast
    and multicast entries side by side in one POST (template 5.5.5.4,
    telegram 210) still create every entry."""

    @pytest.fixture(autouse=True)
    def _setup(self, coap, oscore_ctx):
        set_lsm(coap, oscore_ctx, 4)  # unload
        set_lsm(coap, oscore_ctx, 1)  # loading
        self.coap = coap
        self.ctx = oscore_ctx

    def _post(self, path, payload):
        resp = self.coap.oscore_post(self.ctx, path, payload=payload)
        assert resp is not None, f"POST {path} timed out"
        return resp

    def _get_code(self, path):
        resp = self.coap.oscore_get(self.ctx, path, accept=APPLICATION_CBOR)
        assert resp is not None, f"GET {path} timed out"
        return resp.code

    def _get_entry(self, path):
        resp = self.coap.oscore_get(self.ctx, path, accept=APPLICATION_CBOR)
        assert resp is not None, f"GET {path} timed out"
        assert resp.code == "2.05", f"GET {path}: expected 2.05, got {resp.code}"
        return cbor2.loads(resp.payload)

    @pytest.mark.parametrize("path", ["/fp/r", "/fp/p"])
    def test_mixed_multicast_unicast_entries_are_created(self, path):
        # template 5.5.5.4, telegram 210
        payload = _cbor_entries(
            [(0, 2), (13, TBL_GRPID), (7, [2204, 2205, 2206])],
            [(0, 3), (13, TBL_GRPID + 1), (7, [2207, 2208])],
            [(0, 4), (12, TBL_IA), (7, [2209, 2210])])
        resp = self._post(path, payload)
        assert resp.code == "2.01", f"POST {path} mixed entries: expected 2.01, got {resp.code}"
        for rid in (2, 3, 4):
            assert self._get_code(f"{path}/{rid}") == "2.05", f"{path}/{rid} was not stored"

    @pytest.mark.parametrize("path", ["/fp/r", "/fp/p"])
    def test_repeated_ga_without_target_is_rejected(self, path):
        resp = self._post(path, _cbor_entries([(0, 41), (7, [500]), (7, [501])]))
        assert resp.code == "4.00", f"POST {path} ga twice, no ia/grpid: expected 4.00, got {resp.code}"
        assert self._get_code(f"{path}/41") == "4.04", f"{path}/41 was stored anyway"

    @pytest.mark.parametrize("path", ["/fp/r", "/fp/p"])
    def test_repeated_ga_with_grpid_last_wins(self, path):
        resp = self._post(path, _cbor_entries([(0, 42), (7, [500]), (7, [501]), (13, TBL_GRPID)]))
        assert resp.code == "2.01", f"POST {path} ga twice: expected 2.01, got {resp.code}"
        entry = self._get_entry(f"{path}/42")
        assert entry.get(7) == [501], f"{path}/42 ga: expected the last one [501], got {entry}"

    def test_rcp_repeated_at_last_wins(self):
        resp = self._post("/fp/r", _cbor_entries([(0, 45), (7, [500]), (12, TBL_IA), (14, "first"), (14, "last")]))
        assert resp.code == "2.01", f"POST /fp/r at twice: expected 2.01, got {resp.code}"
        entry = self._get_entry("/fp/r/45")
        assert entry.get(14) == "last", f"/fp/r/45 at: expected the last one 'last', got {entry}"

    @pytest.mark.parametrize("path", ["/fp/r", "/fp/p"])
    def test_ia_with_grpid_is_rejected(self, path):
        resp = self._post(path, _cbor_entries([(0, 43), (7, [500]), (12, TBL_IA), (13, TBL_GRPID)]))
        assert resp.code == "4.00", f"POST {path} ia + grpid: expected 4.00, got {resp.code}"
        assert self._get_code(f"{path}/43") == "4.04", f"{path}/43 was stored anyway"

    def test_go_repeated_ga_last_wins(self):
        resp = self._post("/fp/g", _cbor_entries([(0, 44), (7, [500]), (7, [501]), (8, 0x01), (11, "/p/1")]))
        assert resp.code == "2.01", f"POST /fp/g ga twice: expected 2.01, got {resp.code}"
        entry = self._get_entry("/fp/g/44")
        assert entry.get(7) == [501], f"/fp/g/44 ga: expected the last one [501], got {entry}"


# ===========================================================================
#  Value of an unsupported CBOR type in a table entry (ADDITIONAL)
# ===========================================================================

class TestTableUnsupportedValueType:
    """A value of a type the table handlers do not take (here a byte string
    under an unused key) is a 4.00 and nothing is stored, on /fp/g, /fp/p and
    /fp/r alike. A byte string before 'ga' shows a handler that does not stop
    at it by a stored entry; one after 'ga' also covers the freed 'ga' array
    (memory errors show in the ASan runtime job)."""

    @pytest.fixture(autouse=True)
    def _setup(self, coap, oscore_ctx):
        set_lsm(coap, oscore_ctx, 4)  # unload
        set_lsm(coap, oscore_ctx, 1)  # loading
        self.coap = coap
        self.ctx = oscore_ctx

    @pytest.mark.parametrize("path, pairs", [
        ("/fp/g", [(0, 51), (99, b"\x01"), (7, [500]), (8, 0x01), (11, "/p/1")]),
        ("/fp/p", [(0, 51), (99, b"\x01"), (7, [500]), (13, TBL_GRPID)]),
        ("/fp/r", [(0, 51), (99, b"\x01"), (7, [500]), (13, TBL_GRPID)]),
        # after 'ga': a handler that frees 'ga' here and goes on stores a freed pointer (use-after-free, ASan job)
        ("/fp/r", [(0, 51), (7, [500]), (13, TBL_GRPID), (99, b"\x01")]),
    ], ids=["fp_g", "fp_p", "fp_r", "fp_r_after_ga"])
    def test_byte_string_value_is_rejected(self, path, pairs):
        resp = self.coap.oscore_post(self.ctx, path, payload=_cbor_entries(pairs))
        assert resp is not None, f"POST {path} timed out"
        assert resp.code == "4.00", f"POST {path} with a byte string value: expected 4.00, got {resp.code}"

        resp = self.coap.oscore_get(self.ctx, f"{path}/51", accept=APPLICATION_CBOR)
        assert resp is not None, f"GET {path}/51 timed out"
        assert resp.code == "4.04", f"{path}/51 was stored anyway ({resp.code})"


# ===========================================================================
#  Empty 'ga' only as an empty array, not as null (ADDITIONAL)
# ===========================================================================

class TestTableGaNotNull:
    """An empty 'ga' is the empty array 7: [] (template 5.5.5.7a, allowed on
    /fp/p and /fp/r). CBOR null, undefined or a value the parser does not map
    (half float) is no array: 4.00, nothing stored."""

    @pytest.fixture(autouse=True)
    def _setup(self, coap, oscore_ctx):
        set_lsm(coap, oscore_ctx, 4)  # unload
        set_lsm(coap, oscore_ctx, 1)  # loading
        self.coap = coap
        self.ctx = oscore_ctx

    def _post_get(self, path, ga_value):
        resp = self.coap.oscore_post(self.ctx, path, payload=_cbor_entries([(0, 61), (7, ga_value), (13, TBL_GRPID)]))
        assert resp is not None, f"POST {path} timed out"
        get = self.coap.oscore_get(self.ctx, f"{path}/61", accept=APPLICATION_CBOR)
        assert get is not None, f"GET {path}/61 timed out"
        return resp.code, get.code

    @pytest.mark.parametrize("path", ["/fp/p", "/fp/r"])
    def test_empty_array_ga_is_accepted(self, path):
        post, get = self._post_get(path, [])
        assert post == "2.01", f"POST {path} ga []: expected 2.01, got {post}"
        assert get == "2.05", f"{path}/61 with ga [] was not stored ({get})"

    @pytest.mark.parametrize("path", ["/fp/p", "/fp/r"])
    @pytest.mark.parametrize("ga_value", [None, RawCbor(b"\xf7"), RawCbor(b"\xf9\x3c\x00")], ids=["null", "undefined", "half_float"])
    def test_non_array_ga_is_rejected(self, path, ga_value):
        post, get = self._post_get(path, ga_value)
        assert post == "4.00", f"POST {path} ga {ga_value!r}: expected 4.00, got {post}"
        assert get == "4.04", f"{path}/61 with ga {ga_value!r} was stored anyway ({get})"
