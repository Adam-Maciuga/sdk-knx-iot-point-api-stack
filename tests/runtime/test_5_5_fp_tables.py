"""
Runtime conformance tests — EITT 5.5 Function Point Tables

5.5.1.1   Write single GO with Read flag
5.5.1.2   Write single GO with Multiple Flags (Input)
5.5.1.3   Write single GO with Multiple Flags (Output)
5.5.1.4   Write multiple GOs (Input)
5.5.1.5   Write multiple GOs (Output)
5.5.1.6   Update single GO (Input)
5.5.1.6b  Invalid Update of Non-Existing Entry
5.5.1.7   Update single GO (Output)
5.5.1.8   Delete GO via POST empty element
5.5.1.9   Invalid write GO with no GAs
5.5.1.9b  Invalid update GO with no GAs
5.5.2.1   Read list of Group Object Identifiers
5.5.3.1   Read Group Object via its Identifier
5.5.4.1   Delete Group Object via its Identifier

5.5.5.1   Write single recipient (multicast, grpid)
5.5.5.3   Write single recipient (unicast, IA)
5.5.5.4   Write multiple recipients (multicast + unicast)
5.5.5.5   Update (overwrite) multiple recipients
5.5.5.5a  Update (partial) multiple recipients
5.5.5.5b  Invalid update of non-existing entry
5.5.5.6   Delete recipient via POST empty element
5.5.5.7a  Write entries with empty GA list
5.5.5.8a  Write split entry with up to 20 GAs each
5.5.6.1   Read list of recipient table entry IDs
5.5.7.1   Read single recipient entry
5.5.8.1   Delete recipient entry via identifier

5.5.9.1   Write single publisher (multicast, grpid)
5.5.9.4   Write multiple publishers
5.5.9.5   Update multiple publishers
5.5.9.5b  Invalid update non-existing publisher
5.5.9.6   Delete publisher via POST empty element
5.5.9.7a  Write publisher with empty GA list
5.5.9.8a  Write split publisher entry (20 GAs)
5.5.10.1  Read list of publisher table entry IDs
5.5.11.1  Read single publisher entry
5.5.12.1  Delete publisher entry via identifier
5.5.12.2  Invalid delete non-existing publisher

Reference: 08_10_5 KNX IoT Point API Tests v01_01_01_AS
"""

import cbor2
import pytest

from coap_client import APPLICATION_CBOR, LINK_FORMAT
from conftest import (DUT_IA, DUT_IID, DEVICE_PASSWORD, ALL_SCOPES,
                      set_lsm, parse_link_format)





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

    # -- 5.5.1.1 Write single GO with Read flag ----------------------------
    def test_5_5_1_1_write_single_go_read_flag(self):
        """POST single GO entry with read cflag, expect 2.01, read back."""
        go_entry = [{0: 1, 7: [2305, 2401], 8: 1, 11: "/p/1"}]  # cflag=1 (read)
        resp = self.coap.oscore_post(
            self.ctx, "/fp/g", payload=cbor2.dumps(go_entry))
        assert resp is not None and resp.code == "2.01", (
            f"Expected 2.01 Created, got {resp.code if resp else 'timeout'}")

        # Read back
        resp2 = self.coap.oscore_get(
            self.ctx, "/fp/g/1", accept=APPLICATION_CBOR)
        assert resp2 is not None and resp2.code == "2.05"
        data = cbor2.loads(resp2.payload)
        assert data[0] == 1      # id
        assert data[7] == [2305, 2401]  # ga
        assert data[8] == 1      # cflag (read)
        assert data[11] == "/p/1" # href

    # -- 5.5.1.2 Write single GO with Multiple Flags (Input) ---------------
    def test_5_5_1_2_write_single_go_input_flags(self):
        """POST single GO with input flags (update+read = 0x10+0x01 = 0x11)."""
        go_entry = [{0: 2, 7: [100], 8: 0x11, 11: "/p/1"}]
        resp = self.coap.oscore_post(
            self.ctx, "/fp/g", payload=cbor2.dumps(go_entry))
        assert resp is not None and resp.code == "2.01"

        resp2 = self.coap.oscore_get(
            self.ctx, "/fp/g/2", accept=APPLICATION_CBOR)
        assert resp2 is not None and resp2.code == "2.05"
        data = cbor2.loads(resp2.payload)
        assert data[8] == 0x11

    # -- 5.5.1.3 Write single GO with Multiple Flags (Output) --------------
    def test_5_5_1_3_write_single_go_output_flags(self):
        """POST single GO with output flags (transmit+acknowledge = 0x08+0x40)."""
        go_entry = [{0: 3, 7: [200], 8: 0x48, 11: "/p/2"}]
        resp = self.coap.oscore_post(
            self.ctx, "/fp/g", payload=cbor2.dumps(go_entry))
        assert resp is not None and resp.code == "2.01"

        resp2 = self.coap.oscore_get(
            self.ctx, "/fp/g/3", accept=APPLICATION_CBOR)
        assert resp2 is not None and resp2.code == "2.05"
        data = cbor2.loads(resp2.payload)
        assert data[8] == 0x48

    # -- 5.5.1.4 Write multiple GOs (Input) --------------------------------
    def test_5_5_1_4_write_multiple_gos_input(self):
        """POST multiple GO entries at once, all with input flags."""
        entries = [
            {0: 10, 7: [100, 101], 8: 0x11, 11: "/p/1"},
            {0: 11, 7: [102, 103], 8: 0x11, 11: "/p/2"},
        ]
        resp = self.coap.oscore_post(
            self.ctx, "/fp/g", payload=cbor2.dumps(entries))
        assert resp is not None and resp.code == "2.01"

        # Both entries readable
        for go_id in [10, 11]:
            resp2 = self.coap.oscore_get(
                self.ctx, f"/fp/g/{go_id}", accept=APPLICATION_CBOR)
            assert resp2 is not None and resp2.code == "2.05", (
                f"GET /fp/g/{go_id} failed: {resp2.code if resp2 else 'timeout'}")

    # -- 5.5.1.5 Write multiple GOs (Output) -------------------------------
    def test_5_5_1_5_write_multiple_gos_output(self):
        """POST multiple GO entries with output flags."""
        entries = [
            {0: 20, 7: [200], 8: 0x48, 11: "/p/1"},
            {0: 21, 7: [201], 8: 0x48, 11: "/p/2"},
        ]
        resp = self.coap.oscore_post(
            self.ctx, "/fp/g", payload=cbor2.dumps(entries))
        assert resp is not None and resp.code == "2.01"

        for go_id in [20, 21]:
            resp2 = self.coap.oscore_get(
                self.ctx, f"/fp/g/{go_id}", accept=APPLICATION_CBOR)
            assert resp2 is not None and resp2.code == "2.05"

    # -- 5.5.1.6 Update single GO (Input) ----------------------------------
    def test_5_5_1_6_update_single_go(self):
        """Create GO, then POST same id with different data → 2.04."""
        # Create first
        resp1 = self.coap.oscore_post(
            self.ctx, "/fp/g",
            payload=cbor2.dumps([{0: 30, 7: [300], 8: 0x11, 11: "/p/1"}]))
        assert resp1 is not None and resp1.code == "2.01"

        # Update with different GA and href
        resp2 = self.coap.oscore_post(
            self.ctx, "/fp/g",
            payload=cbor2.dumps([{0: 30, 7: [301, 302], 8: 0x11, 11: "/p/2"}]))
        assert resp2 is not None and resp2.code == "2.04", (
            f"Expected 2.04 Changed, got {resp2.code if resp2 else 'timeout'}")

        # Read back and verify update
        resp3 = self.coap.oscore_get(
            self.ctx, "/fp/g/30", accept=APPLICATION_CBOR)
        assert resp3 is not None and resp3.code == "2.05"
        data = cbor2.loads(resp3.payload)
        assert data[7] == [301, 302]
        assert data[11] == "/p/2"

    # -- 5.5.1.6b Update non-existing GO -----------------------------------
    def test_5_5_1_6b_update_nonexisting_go(self):
        """POST with non-existing id creates new entry (2.01)."""
        resp = self.coap.oscore_post(
            self.ctx, "/fp/g",
            payload=cbor2.dumps([{0: 999, 7: [500], 8: 0x01, 11: "/p/1"}]))
        assert resp is not None and resp.code == "2.01", (
            f"Expected 2.01 (non-existing id creates), got "
            f"{resp.code if resp else 'timeout'}")

    # -- 5.5.1.7 Update single GO (Output) ---------------------------------
    def test_5_5_1_7_update_single_go_output(self):
        """Create GO with output flags, then update → 2.04."""
        self.coap.oscore_post(
            self.ctx, "/fp/g",
            payload=cbor2.dumps([{0: 31, 7: [400], 8: 0x48, 11: "/p/1"}]))

        resp = self.coap.oscore_post(
            self.ctx, "/fp/g",
            payload=cbor2.dumps([{0: 31, 7: [401, 402], 8: 0x48, 11: "/p/2"}]))
        assert resp is not None and resp.code == "2.04"

    # -- 5.5.1.8 Delete GO via POST empty element --------------------------
    def test_5_5_1_8_delete_go_via_post(self):
        """Create GO, POST with only id → 2.04, then GET → 4.04."""
        # Create
        self.coap.oscore_post(
            self.ctx, "/fp/g",
            payload=cbor2.dumps([{0: 40, 7: [500], 8: 0x01, 11: "/p/1"}]))

        # Delete via POST with only id
        resp = self.coap.oscore_post(
            self.ctx, "/fp/g",
            payload=cbor2.dumps([{0: 40}]))
        assert resp is not None and resp.code == "2.04", (
            f"Expected 2.04 (delete), got {resp.code if resp else 'timeout'}")

        # Verify list doesn't contain deleted id
        resp2 = self.coap.oscore_get(
            self.ctx, "/fp/g", accept=LINK_FORMAT)
        assert resp2 is not None and resp2.code == "2.05"
        links = parse_link_format(resp2.payload)
        hrefs = [l["href"] for l in links]
        assert "/fp/g/40" not in hrefs, "Deleted GO still in list"

        # GET individual entry → 4.04
        resp3 = self.coap.oscore_get(
            self.ctx, "/fp/g/40", accept=APPLICATION_CBOR)
        assert resp3 is not None and resp3.code == "4.04"

    # -- 5.5.1.9 Invalid write GO with no GAs ------------------------------
    def test_5_5_1_9_invalid_write_go_empty_ga(self):
        """POST GO with empty GA list → 4.00."""
        resp = self.coap.oscore_post(
            self.ctx, "/fp/g",
            payload=cbor2.dumps([{0: 50, 7: [], 8: 0x01, 11: "/p/1"}]))
        assert resp is not None and resp.code == "4.00", (
            f"Expected 4.00 for empty GA, got {resp.code if resp else 'timeout'}")

        # Verify not created
        resp2 = self.coap.oscore_get(
            self.ctx, "/fp/g/50", accept=APPLICATION_CBOR)
        assert resp2 is not None and resp2.code == "4.04"

    # -- 5.5.1.9b Invalid update GO with no GAs ----------------------------
    def test_5_5_1_9b_invalid_update_go_empty_ga(self):
        """Create GO, then update with empty GA → 4.00."""
        # Create first
        self.coap.oscore_post(
            self.ctx, "/fp/g",
            payload=cbor2.dumps([{0: 51, 7: [600], 8: 0x01, 11: "/p/1"}]))

        # Attempt update with empty GA
        resp = self.coap.oscore_post(
            self.ctx, "/fp/g",
            payload=cbor2.dumps([{0: 51, 7: [], 8: 0x01, 11: "/p/1"}]))
        assert resp is not None and resp.code == "4.00", (
            f"Expected 4.00 for empty GA update, got "
            f"{resp.code if resp else 'timeout'}")


# ===========================================================================
#  5.5.2 -- Read List of Group Object Identifiers (GET /fp/g link-format)
# ===========================================================================

class TestGroupObjectTableReadList:
    """EITT 5.5.2.1 — Read list of GO identifiers in link-format."""

    @pytest.fixture(autouse=True)
    def _setup(self, coap, oscore_ctx):
        # EITT: unload + loading (no factory reset between tests)
        set_lsm(coap, oscore_ctx, 4)  # unload
        set_lsm(coap, oscore_ctx, 1)  # loading
        self.coap = coap
        self.ctx = oscore_ctx

    def test_5_5_2_1_read_go_list(self):
        """Install GOs, GET /fp/g Accept: link-format → list with ct=60."""
        entries = [
            {0: 13, 7: [2305, 2401], 8: 0x01, 11: "/p/1"},
            {0: 14, 7: [2306], 8: 0x48, 11: "/p/2"},
        ]
        self.coap.oscore_post(
            self.ctx, "/fp/g", payload=cbor2.dumps(entries))

        resp = self.coap.oscore_get(
            self.ctx, "/fp/g", accept=LINK_FORMAT)
        assert resp is not None and resp.code == "2.05"
        links = parse_link_format(resp.payload)
        hrefs = [l["href"] for l in links]
        assert "/fp/g/13" in hrefs, f"GO 13 not in list: {hrefs}"
        assert "/fp/g/14" in hrefs, f"GO 14 not in list: {hrefs}"

        # All links should have ct=60 (CBOR)
        for link in links:
            if link["href"].startswith("/fp/g/"):
                assert link.get("ct") == "60", (
                    f"Expected ct=60 for {link['href']}, got {link.get('ct')}")


# ===========================================================================
#  5.5.3 -- Read Single Group Object (GET /fp/g/{id})
# ===========================================================================

class TestGroupObjectTableReadSingle:
    """EITT 5.5.3.1 — Read GO entry by its identifier."""

    @pytest.fixture(autouse=True)
    def _setup(self, coap, oscore_ctx):
        # EITT: unload + loading (no factory reset between tests)
        set_lsm(coap, oscore_ctx, 4)  # unload
        set_lsm(coap, oscore_ctx, 1)  # loading
        self.coap = coap
        self.ctx = oscore_ctx

    def test_5_5_3_1_read_single_go(self):
        """Install GO, GET /fp/g/{id} → CBOR with matching values."""
        go_entry = [{0: 13, 7: [2305, 2401], 8: 0xD8, 11: "/p/1"}]
        self.coap.oscore_post(
            self.ctx, "/fp/g", payload=cbor2.dumps(go_entry))

        resp = self.coap.oscore_get(
            self.ctx, "/fp/g/13", accept=APPLICATION_CBOR)
        assert resp is not None and resp.code == "2.05"

        data = cbor2.loads(resp.payload)
        assert data[0] == 13
        assert data[7] == [2305, 2401]
        assert data[8] == 0xD8
        assert data[11] == "/p/1"


# ===========================================================================
#  5.5.4 -- Delete Single Group Object (DELETE /fp/g/{id})
# ===========================================================================

class TestGroupObjectTableDelete:
    """EITT 5.5.4.1 — Delete GO entry via DELETE method."""

    @pytest.fixture(autouse=True)
    def _setup(self, coap, oscore_ctx):
        # EITT: unload + loading (no factory reset between tests)
        set_lsm(coap, oscore_ctx, 4)  # unload
        set_lsm(coap, oscore_ctx, 1)  # loading
        self.coap = coap
        self.ctx = oscore_ctx

    def test_5_5_4_1_delete_single_go(self):
        """Install GO, DELETE /fp/g/{id} → 2.02, GET → 4.04."""
        self.coap.oscore_post(
            self.ctx, "/fp/g",
            payload=cbor2.dumps([{0: 13, 7: [2305], 8: 0x01, 11: "/p/1"}]))

        resp = self.coap.oscore_delete(self.ctx, "/fp/g/13")
        assert resp is not None and resp.code == "2.02", (
            f"Expected 2.02 Deleted, got {resp.code if resp else 'timeout'}")

        resp2 = self.coap.oscore_get(
            self.ctx, "/fp/g/13", accept=APPLICATION_CBOR)
        assert resp2 is not None and resp2.code == "4.04"


# ===========================================================================
#  5.5.5 -- Recipient Table Write (POST /fp/r)
# ===========================================================================

class TestRecipientTableWrite:
    """EITT 5.5.5.x — Write / Update / Delete Recipient table entries."""

    @pytest.fixture(autouse=True)
    def _setup(self, coap, oscore_ctx):
        # EITT: unload + loading (no factory reset between tests)
        set_lsm(coap, oscore_ctx, 4)  # unload
        set_lsm(coap, oscore_ctx, 1)  # loading
        self.coap = coap
        self.ctx = oscore_ctx

    # -- 5.5.5.1 Write single multicast recipient --------------------------
    def test_5_5_5_1_write_single_mc_recipient(self):
        """POST single multicast recipient (grpid, 10 GAs) → 2.01, read back."""
        entry = [{
            0: 2,
            13: 0x80000001,
            7: [2204, 2205, 2206, 1, 2, 3, 65535, 2207, 2208, 2209],
        }]
        resp = self.coap.oscore_post(
            self.ctx, "/fp/r", payload=cbor2.dumps(entry))
        assert resp is not None and resp.code == "2.01", (
            f"Expected 2.01, got {resp.code if resp else 'timeout'}")

        # Read back
        resp2 = self.coap.oscore_get(
            self.ctx, "/fp/r/2", accept=APPLICATION_CBOR)
        assert resp2 is not None and resp2.code == "2.05"
        data = cbor2.loads(resp2.payload)
        assert data[0] == 2
        assert data[13] == 0x80000001
        assert data[7] == [2204, 2205, 2206, 1, 2, 3, 65535, 2207, 2208, 2209]

    # -- 5.5.5.3 Write single unicast recipient ----------------------------
    def test_5_5_5_3_write_single_uc_recipient(self):
        """POST single unicast recipient (ia, at) → 2.01, read back."""
        entry = [{
            0: 3,
            12: 55,
            7: [2204, 2205, 2206],
            14: "test_at_id",
        }]
        resp = self.coap.oscore_post(
            self.ctx, "/fp/r", payload=cbor2.dumps(entry))
        assert resp is not None and resp.code == "2.01"

        resp2 = self.coap.oscore_get(
            self.ctx, "/fp/r/3", accept=APPLICATION_CBOR)
        assert resp2 is not None and resp2.code == "2.05"
        data = cbor2.loads(resp2.payload)
        assert data[0] == 3
        assert data[12] == 55
        assert data[7] == [2204, 2205, 2206]
        assert data[14] == "test_at_id"

    # -- 5.5.5.4 Write multiple recipients ---------------------------------
    def test_5_5_5_4_write_multiple_recipients(self):
        """POST multiple recipients (mc + mc + uc) → 2.01."""
        entries = [
            {0: 2, 13: 0x80000001, 7: [2204, 2205, 2206]},
            {0: 3, 13: 0x80000002, 7: [2207, 2208]},
            {0: 4, 12: 55, 7: [2209, 2210], 14: "test_at_id"},
        ]
        resp = self.coap.oscore_post(
            self.ctx, "/fp/r", payload=cbor2.dumps(entries))
        assert resp is not None and resp.code == "2.01"

        # All entries readable
        for rid in [2, 3, 4]:
            resp2 = self.coap.oscore_get(
                self.ctx, f"/fp/r/{rid}", accept=APPLICATION_CBOR)
            assert resp2 is not None and resp2.code == "2.05", (
                f"GET /fp/r/{rid} failed: {resp2.code if resp2 else 'timeout'}")

    # -- 5.5.5.5 Update (overwrite) multiple recipients --------------------
    def test_5_5_5_5_update_multiple_recipients(self):
        """Create recipients, then POST same ids with different data → 2.04."""
        # Create
        entries = [
            {0: 10, 13: 0x80000001, 7: [100, 101]},
            {0: 11, 13: 0x80000002, 7: [102]},
        ]
        resp1 = self.coap.oscore_post(
            self.ctx, "/fp/r", payload=cbor2.dumps(entries))
        assert resp1 is not None and resp1.code == "2.01"

        # Update
        updated = [
            {0: 10, 13: 0x80000001, 7: [200, 201, 202]},
            {0: 11, 13: 0x80000002, 7: [203, 204]},
        ]
        resp2 = self.coap.oscore_post(
            self.ctx, "/fp/r", payload=cbor2.dumps(updated))
        assert resp2 is not None and resp2.code == "2.04"

        # Verify update
        resp3 = self.coap.oscore_get(
            self.ctx, "/fp/r/10", accept=APPLICATION_CBOR)
        assert resp3 is not None and resp3.code == "2.05"
        data = cbor2.loads(resp3.payload)
        assert data[7] == [200, 201, 202]

    # -- 5.5.5.5a Update partial (single entry of multiple) ----------------
    def test_5_5_5_5a_update_partial_recipients(self):
        """Create two recipients, update only one → changed."""
        entries = [
            {0: 20, 13: 0x80000001, 7: [100]},
            {0: 21, 13: 0x80000002, 7: [101]},
        ]
        resp1 = self.coap.oscore_post(
            self.ctx, "/fp/r", payload=cbor2.dumps(entries))
        assert resp1 is not None and resp1.code == "2.01"

        # Update only id=20
        resp2 = self.coap.oscore_post(
            self.ctx, "/fp/r",
            payload=cbor2.dumps([{0: 20, 13: 0x80000001, 7: [200, 201]}]))
        assert resp2 is not None and resp2.code == "2.04"

        # Verify id=20 updated
        resp3 = self.coap.oscore_get(
            self.ctx, "/fp/r/20", accept=APPLICATION_CBOR)
        data = cbor2.loads(resp3.payload)
        assert data[7] == [200, 201]

        # Verify id=21 unchanged
        resp4 = self.coap.oscore_get(
            self.ctx, "/fp/r/21", accept=APPLICATION_CBOR)
        data2 = cbor2.loads(resp4.payload)
        assert data2[7] == [101]

    # -- 5.5.5.5b Update non-existing recipient ----------------------------
    def test_5_5_5_5b_update_nonexisting_recipient(self):
        """POST with non-existing id creates new entry (2.01)."""
        resp = self.coap.oscore_post(
            self.ctx, "/fp/r",
            payload=cbor2.dumps([{0: 999, 13: 0x80000099, 7: [500]}]))
        assert resp is not None and resp.code == "2.01", (
            f"Expected 2.01 (non-existing id creates), got "
            f"{resp.code if resp else 'timeout'}")

    # -- 5.5.5.6 Delete recipient via POST empty element -------------------
    def test_5_5_5_6_delete_recipient_via_post(self):
        """Create recipient, POST with only id → 2.04, verify deleted."""
        # Create
        self.coap.oscore_post(
            self.ctx, "/fp/r",
            payload=cbor2.dumps([{0: 2, 13: 0x80000001, 7: [100]}]))

        # Delete via POST with only id
        resp = self.coap.oscore_post(
            self.ctx, "/fp/r",
            payload=cbor2.dumps([{0: 2}]))
        assert resp is not None and resp.code == "2.04"

        # Verify deleted from list
        resp2 = self.coap.oscore_get(
            self.ctx, "/fp/r", accept=LINK_FORMAT)
        assert resp2 is not None and resp2.code == "2.05"
        links = parse_link_format(resp2.payload)
        hrefs = [l["href"] for l in links]
        assert "/fp/r/2" not in hrefs

        # Verify GET individual → 4.04
        resp3 = self.coap.oscore_get(
            self.ctx, "/fp/r/2", accept=APPLICATION_CBOR)
        assert resp3 is not None and resp3.code == "4.04"

    # -- 5.5.5.7a Write entries with empty GA list -------------------------
    def test_5_5_5_7a_write_empty_ga_list(self):
        """POST recipients with empty GA arrays → 2.01."""
        entries = [
            {0: 2, 13: 0x80000001, 7: []},
            {0: 3, 13: 0x80000002, 7: []},
            {0: 4, 12: 55, 7: [], 14: "test_at_id"},
        ]
        resp = self.coap.oscore_post(
            self.ctx, "/fp/r", payload=cbor2.dumps(entries))
        assert resp is not None and resp.code == "2.01", (
            f"Expected 2.01 for empty GA recipients, got "
            f"{resp.code if resp else 'timeout'}")

    # -- 5.5.5.8a Write split entry with 20 GAs ---------------------------
    def test_5_5_5_8a_write_large_ga_list(self):
        """POST recipient with 20 GAs → 2.01, read back all 20."""
        gas = list(range(1, 21))  # 20 group addresses
        entry = [{0: 5, 13: 0x80000001, 7: gas}]
        resp = self.coap.oscore_post(
            self.ctx, "/fp/r", payload=cbor2.dumps(entry))
        assert resp is not None and resp.code == "2.01"

        resp2 = self.coap.oscore_get(
            self.ctx, "/fp/r/5", accept=APPLICATION_CBOR)
        assert resp2 is not None and resp2.code == "2.05"
        data = cbor2.loads(resp2.payload)
        assert data[7] == gas, f"Expected {gas}, got {data[7]}"


# ===========================================================================
#  5.5.6 -- Read List of Recipient IDs (GET /fp/r link-format)
# ===========================================================================

class TestRecipientTableReadList:
    """EITT 5.5.6.1 — Read list of recipient table entry IDs."""

    @pytest.fixture(autouse=True)
    def _setup(self, coap, oscore_ctx):
        # EITT: unload + loading (no factory reset between tests)
        set_lsm(coap, oscore_ctx, 4)  # unload
        set_lsm(coap, oscore_ctx, 1)  # loading
        self.coap = coap
        self.ctx = oscore_ctx

    def test_5_5_6_1_read_recipient_list(self):
        """Install recipients, GET /fp/r Accept: link-format."""
        entries = [
            {0: 2, 13: 0x80000001, 7: [100]},
            {0: 3, 13: 0x80000002, 7: [101]},
        ]
        self.coap.oscore_post(
            self.ctx, "/fp/r", payload=cbor2.dumps(entries))

        resp = self.coap.oscore_get(
            self.ctx, "/fp/r", accept=LINK_FORMAT)
        assert resp is not None and resp.code == "2.05"

        links = parse_link_format(resp.payload)
        hrefs = [l["href"] for l in links]
        assert "/fp/r/2" in hrefs
        assert "/fp/r/3" in hrefs


# ===========================================================================
#  5.5.7 -- Read Single Recipient (GET /fp/r/{id})
# ===========================================================================

class TestRecipientTableReadSingle:
    """EITT 5.5.7.1 — Read single recipient entry."""

    @pytest.fixture(autouse=True)
    def _setup(self, coap, oscore_ctx):
        # EITT: unload + loading (no factory reset between tests)
        set_lsm(coap, oscore_ctx, 4)  # unload
        set_lsm(coap, oscore_ctx, 1)  # loading
        self.coap = coap
        self.ctx = oscore_ctx

    def test_5_5_7_1_read_single_recipient(self):
        """Install recipient, GET /fp/r/{id} → CBOR with matching values."""
        entry = [{0: 2, 13: 0x80000001, 7: [2204, 2205, 2206]}]
        self.coap.oscore_post(
            self.ctx, "/fp/r", payload=cbor2.dumps(entry))

        resp = self.coap.oscore_get(
            self.ctx, "/fp/r/2", accept=APPLICATION_CBOR)
        assert resp is not None and resp.code == "2.05"

        data = cbor2.loads(resp.payload)
        assert data[0] == 2
        assert data[13] == 0x80000001
        assert data[7] == [2204, 2205, 2206]


# ===========================================================================
#  5.5.8 -- Delete Single Recipient (DELETE /fp/r/{id})
# ===========================================================================

class TestRecipientTableDelete:
    """EITT 5.5.8.1 — Delete recipient entry via DELETE method."""

    @pytest.fixture(autouse=True)
    def _setup(self, coap, oscore_ctx):
        # EITT: unload + loading (no factory reset between tests)
        set_lsm(coap, oscore_ctx, 4)  # unload
        set_lsm(coap, oscore_ctx, 1)  # loading
        self.coap = coap
        self.ctx = oscore_ctx

    def test_5_5_8_1_delete_single_recipient(self):
        """Install recipient, DELETE /fp/r/{id} → 2.02, GET → 4.04."""
        self.coap.oscore_post(
            self.ctx, "/fp/r",
            payload=cbor2.dumps([{0: 2, 13: 0x80000001, 7: [100]}]))

        resp = self.coap.oscore_delete(self.ctx, "/fp/r/2")
        assert resp is not None and resp.code == "2.02"

        resp2 = self.coap.oscore_get(
            self.ctx, "/fp/r/2", accept=APPLICATION_CBOR)
        assert resp2 is not None and resp2.code == "4.04"


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

    # -- 5.5.9.1 Write single multicast publisher --------------------------
    def test_5_5_9_1_write_single_mc_publisher(self):
        """POST single multicast publisher (grpid, 10 GAs) → 2.01, read back."""
        entry = [{
            0: 2,
            13: 0x80000001,
            7: [2204, 2205, 2206, 1, 2, 3, 65535, 2207, 2208, 2209],
        }]
        resp = self.coap.oscore_post(
            self.ctx, "/fp/p", payload=cbor2.dumps(entry))
        assert resp is not None and resp.code == "2.01"

        resp2 = self.coap.oscore_get(
            self.ctx, "/fp/p/2", accept=APPLICATION_CBOR)
        assert resp2 is not None and resp2.code == "2.05"
        data = cbor2.loads(resp2.payload)
        assert data[0] == 2
        assert data[13] == 0x80000001
        assert data[7] == [2204, 2205, 2206, 1, 2, 3, 65535, 2207, 2208, 2209]

    # -- 5.5.9.3 Write single unicast publisher (IA-based) -----------------
    def test_5_5_9_3_write_single_uc_publisher(self):
        """POST single unicast publisher (ia, 3 GAs) → 2.01, read back.

        EITT 5.5.9.3: Write a single entry for an Individual Address
        based Unicast Address publisher.  Uses ``ia`` (key 12) instead of
        ``grpid`` (key 13).
        """
        entry = [{
            0: 3,
            12: 1234,
            7: [2204, 2205, 2206],
        }]
        resp = self.coap.oscore_post(
            self.ctx, "/fp/p", payload=cbor2.dumps(entry))
        assert resp is not None and resp.code == "2.01"

        resp2 = self.coap.oscore_get(
            self.ctx, "/fp/p/3", accept=APPLICATION_CBOR)
        assert resp2 is not None and resp2.code == "2.05"
        data = cbor2.loads(resp2.payload)
        assert data[0] == 3
        assert data[12] == 1234
        assert data[7] == [2204, 2205, 2206]

    # -- 5.5.9.4 Write multiple publishers ---------------------------------
    def test_5_5_9_4_write_multiple_publishers(self):
        """POST multiple publisher entries → 2.01."""
        entries = [
            {0: 2, 13: 0x80000001, 7: [100, 101]},
            {0: 3, 13: 0x80000002, 7: [102]},
            {0: 4, 12: 55, 7: [103, 104], 14: "test_at_id"},
        ]
        resp = self.coap.oscore_post(
            self.ctx, "/fp/p", payload=cbor2.dumps(entries))
        assert resp is not None and resp.code == "2.01"

        for pid in [2, 3, 4]:
            resp2 = self.coap.oscore_get(
                self.ctx, f"/fp/p/{pid}", accept=APPLICATION_CBOR)
            assert resp2 is not None and resp2.code == "2.05"

    # -- 5.5.9.5 Update multiple publishers --------------------------------
    def test_5_5_9_5_update_multiple_publishers(self):
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
    def test_5_5_9_5b_update_nonexisting_publisher(self):
        """POST with non-existing id creates new entry (2.01)."""
        resp = self.coap.oscore_post(
            self.ctx, "/fp/p",
            payload=cbor2.dumps([{0: 999, 13: 0x80000099, 7: [500]}]))
        assert resp is not None and resp.code == "2.01"

    # -- 5.5.9.6 Delete publisher via POST empty element -------------------
    def test_5_5_9_6_delete_publisher_via_post(self):
        """Create publisher, POST with only id → 2.04, verify deleted."""
        self.coap.oscore_post(
            self.ctx, "/fp/p",
            payload=cbor2.dumps([{0: 2, 13: 0x80000001, 7: [100]}]))

        resp = self.coap.oscore_post(
            self.ctx, "/fp/p",
            payload=cbor2.dumps([{0: 2}]))
        assert resp is not None and resp.code == "2.04"

        resp2 = self.coap.oscore_get(
            self.ctx, "/fp/p", accept=LINK_FORMAT)
        assert resp2 is not None and resp2.code == "2.05"
        links = parse_link_format(resp2.payload)
        hrefs = [l["href"] for l in links]
        assert "/fp/p/2" not in hrefs

        resp3 = self.coap.oscore_get(
            self.ctx, "/fp/p/2", accept=APPLICATION_CBOR)
        assert resp3 is not None and resp3.code == "4.04"

    # -- 5.5.9.7a Write publisher with empty GA list -----------------------
    def test_5_5_9_7a_write_empty_ga_list(self):
        """POST publisher with empty GA arrays → 2.01."""
        entries = [
            {0: 2, 13: 0x80000001, 7: []},
            {0: 3, 13: 0x80000002, 7: []},
        ]
        resp = self.coap.oscore_post(
            self.ctx, "/fp/p", payload=cbor2.dumps(entries))
        assert resp is not None and resp.code == "2.01"

    # -- 5.5.9.8a Write split publisher entry (20 GAs) --------------------
    def test_5_5_9_8a_write_large_ga_list(self):
        """POST publisher with 20 GAs → 2.01, read back all 20."""
        gas = list(range(1, 21))
        entry = [{0: 5, 13: 0x80000001, 7: gas}]
        resp = self.coap.oscore_post(
            self.ctx, "/fp/p", payload=cbor2.dumps(entry))
        assert resp is not None and resp.code == "2.01"

        resp2 = self.coap.oscore_get(
            self.ctx, "/fp/p/5", accept=APPLICATION_CBOR)
        assert resp2 is not None and resp2.code == "2.05"
        data = cbor2.loads(resp2.payload)
        assert data[7] == gas


# ===========================================================================
#  5.5.10 -- Read List of Publisher IDs (GET /fp/p link-format)
# ===========================================================================

class TestPublisherTableReadList:
    """EITT 5.5.10.1 — Read list of publisher table entry IDs."""

    @pytest.fixture(autouse=True)
    def _setup(self, coap, oscore_ctx):
        # EITT: unload + loading (no factory reset between tests)
        set_lsm(coap, oscore_ctx, 4)  # unload
        set_lsm(coap, oscore_ctx, 1)  # loading
        self.coap = coap
        self.ctx = oscore_ctx

    def test_5_5_10_1_read_publisher_list(self):
        """Install publishers, GET /fp/p Accept: link-format."""
        entries = [
            {0: 2, 13: 0x80000001, 7: [100]},
            {0: 3, 13: 0x80000002, 7: [101]},
        ]
        self.coap.oscore_post(
            self.ctx, "/fp/p", payload=cbor2.dumps(entries))

        resp = self.coap.oscore_get(
            self.ctx, "/fp/p", accept=LINK_FORMAT)
        assert resp is not None and resp.code == "2.05"

        links = parse_link_format(resp.payload)
        hrefs = [l["href"] for l in links]
        assert "/fp/p/2" in hrefs
        assert "/fp/p/3" in hrefs


# ===========================================================================
#  5.5.11 -- Read Single Publisher (GET /fp/p/{id})
# ===========================================================================

class TestPublisherTableReadSingle:
    """EITT 5.5.11.1 — Read single publisher entry."""

    @pytest.fixture(autouse=True)
    def _setup(self, coap, oscore_ctx):
        # EITT: unload + loading (no factory reset between tests)
        set_lsm(coap, oscore_ctx, 4)  # unload
        set_lsm(coap, oscore_ctx, 1)  # loading
        self.coap = coap
        self.ctx = oscore_ctx

    def test_5_5_11_1_read_single_publisher(self):
        """Install publisher, GET /fp/p/{id} → CBOR with matching values."""
        entry = [{0: 2, 13: 0x80000001, 7: [2204, 2205, 2206]}]
        self.coap.oscore_post(
            self.ctx, "/fp/p", payload=cbor2.dumps(entry))

        resp = self.coap.oscore_get(
            self.ctx, "/fp/p/2", accept=APPLICATION_CBOR)
        assert resp is not None and resp.code == "2.05"

        data = cbor2.loads(resp.payload)
        assert data[0] == 2
        assert data[13] == 0x80000001
        assert data[7] == [2204, 2205, 2206]


# ===========================================================================
#  5.5.12 -- Delete Single Publisher (DELETE /fp/p/{id})
# ===========================================================================

class TestPublisherTableDeleteSingle:
    """EITT 5.5.12.x — Delete publisher entry via DELETE method."""

    @pytest.fixture(autouse=True)
    def _setup(self, coap, oscore_ctx):
        # EITT: unload + loading (no factory reset between tests)
        set_lsm(coap, oscore_ctx, 4)  # unload
        set_lsm(coap, oscore_ctx, 1)  # loading
        self.coap = coap
        self.ctx = oscore_ctx

    def test_5_5_12_1_delete_single_publisher(self):
        """Install publisher, DELETE /fp/p/{id} → 2.02, GET → 4.04."""
        self.coap.oscore_post(
            self.ctx, "/fp/p",
            payload=cbor2.dumps([{0: 2, 13: 0x80000001, 7: [100]}]))

        resp = self.coap.oscore_delete(self.ctx, "/fp/p/2")
        assert resp is not None and resp.code == "2.02"

        resp2 = self.coap.oscore_get(
            self.ctx, "/fp/p/2", accept=APPLICATION_CBOR)
        assert resp2 is not None and resp2.code == "4.04"

    def test_5_5_12_2_delete_nonexisting_publisher(self):
        """DELETE /fp/p/{nonexistent_id} → 4.04."""
        resp = self.coap.oscore_delete(self.ctx, "/fp/p/9999")
        assert resp is not None and resp.code == "4.04", (
            f"Expected 4.04 for non-existing, got "
            f"{resp.code if resp else 'timeout'}")
