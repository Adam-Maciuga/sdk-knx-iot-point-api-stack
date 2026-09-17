"""Tests for the "message_attributes:" override section (eitt/overrides.py).

No DUT and no Qt — pure load-and-apply. What matters here is that a
correction over the READ-ONLY master template lands on exactly the message it
names and nothing else, and that a correction which has gone stale (or was
mistyped) is loud rather than silent: a quietly-dropped entry would leave the
template's corrupt value in place, and since the interpreter now fails closed
on an unparseable Content-Format (finding F9) that resurfaces as a TestCase
failure with no visible connection to its cause.
"""

from __future__ import annotations

import os
import textwrap
from pathlib import Path

import pytest

from interpreter.model import parse_eitt_xml
from interpreter.overrides import OverrideTable

EITT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEMPLATE = os.path.join(EITT_DIR, "data",
                        "KnxConformanceTestTemplate-PointAPI.xml")
REAL_OVERRIDES = Path(EITT_DIR) / "interpreter" / "overrides.yaml"

# The one row the shipped template gets wrong: 5.5.4.1's first GET fp/g/1
# carries AcceptedCF="r", the tail fragment of "application/cbor".
CASE_5541 = "5.5.4.1. Delete Group Object via its Identifier"
MSG_5541 = "3FFDAF04-5D79-4762-AF20-7B7673C7054B"


@pytest.fixture(scope="module")
def template():
    return parse_eitt_xml(TEMPLATE)


def _write(tmp_path, text):
    path = tmp_path / "overrides.yaml"
    path.write_text(textwrap.dedent(text), encoding="utf-8")
    return path


def _messages(project, case, msg_id):
    """Every CoAPMessage with `msg_id` inside the TestCase named `case`."""
    out = []
    for _coll, _suite, tc in project.iter_test_cases():
        if tc.name != case:
            continue
        for item in tc.sequence:
            if getattr(item, "id", None) == msg_id and hasattr(item, "cf"):
                out.append(item)
    return out


def test_entry_changes_only_the_named_field(tmp_path):
    """The correction lands on the message it names, and leaves every other
    attribute of that same message — and every other message — untouched."""
    project = parse_eitt_xml(TEMPLATE)
    target = _messages(project, CASE_5541, MSG_5541)
    assert len(target) == 1, "the fixture message should be unique in its case"
    msg = target[0]
    assert msg.accepted_cf == "r", "template baseline changed; retarget this test"
    before = {"cf": msg.cf, "path": msg.path, "code": msg.code,
              "cway": msg.cway, "activate": msg.activate}

    other = next(m for _c, _s, tc in project.iter_test_cases()
                 for m in tc.sequence
                 if hasattr(m, "accepted_cf") and m is not msg)
    other_before = other.accepted_cf

    table = OverrideTable.load(_write(tmp_path, f"""
        message_attributes:
          - case: '{CASE_5541}'
            id: {MSG_5541}
            set:
              AcceptedCF: application/cbor
            reason: "corrupt template value"
        """))
    assert table.apply_message_attributes(project) == []

    assert msg.accepted_cf == "application/cbor"
    assert {"cf": msg.cf, "path": msg.path, "code": msg.code,
            "cway": msg.cway, "activate": msg.activate} == before
    assert other.accepted_cf == other_before


def test_cf_maps_onto_the_content_format_field(tmp_path):
    """Both spellings of the request Content-Format attribute — the XML's own
    `CF` and the trace's `ContentFormat` — reach CoAPMessage.cf."""
    project = parse_eitt_xml(TEMPLATE)
    msg = _messages(project, CASE_5541, MSG_5541)[0]

    table = OverrideTable.load(_write(tmp_path, f"""
        message_attributes:
          - case: '{CASE_5541}'
            id: {MSG_5541}
            set:
              ContentFormat: application/cbor
            reason: "check the alias maps onto cf"
        """))
    assert table.apply_message_attributes(project) == []
    assert msg.cf == "application/cbor"
    assert msg.accepted_cf == "r", "only the named attribute may change"


def test_unmatched_entry_is_reported_not_silent(tmp_path, template):
    """A stale case/id is returned for the caller to raise on — never applied
    quietly. test_eitt.py turns a non-empty result into a collection error."""
    table = OverrideTable.load(_write(tmp_path, """
        message_attributes:
          - case: 'No Such TestCase'
            id: 00000000-0000-0000-0000-000000000000
            set:
              AcceptedCF: application/cbor
            reason: "deliberately stale"
        """))
    unmatched = table.apply_message_attributes(template)
    assert len(unmatched) == 1
    assert "No Such TestCase" in unmatched[0]
    assert "no such message" in unmatched[0]


def test_index_past_the_end_is_reported(tmp_path, template):
    """An `index` beyond the occurrences present is stale in the same way as a
    bad id, and is reported rather than clamped onto the wrong message."""
    table = OverrideTable.load(_write(tmp_path, f"""
        message_attributes:
          - case: '{CASE_5541}'
            id: {MSG_5541}
            index: 7
            set:
              AcceptedCF: application/cbor
            reason: "there is only one occurrence"
        """))
    unmatched = table.apply_message_attributes(template)
    assert len(unmatched) == 1
    assert "index=7" in unmatched[0]
    assert "only 1 occurrence" in unmatched[0]


def test_index_selects_one_of_several_occurrences(tmp_path):
    """Two messages in one TestCase can share a GUID (5.10.1.2's `auth`
    alternatives literally do), so `index` picks the 0-based occurrence — the
    others must be left alone."""
    project = parse_eitt_xml(TEMPLATE)
    shared = None
    for _coll, _suite, tc in project.iter_test_cases():
        seen = {}
        for item in tc.sequence:
            if not hasattr(item, "accepted_cf"):
                continue
            seen.setdefault(item.id, []).append(item)
        for mid, items in seen.items():
            if len(items) > 1:
                shared = (tc.name, mid, items)
                break
        if shared:
            break
    assert shared, "template no longer has a duplicate message ID to test with"
    case, mid, items = shared
    before = [m.accepted_cf for m in items]

    table = OverrideTable.load(_write(tmp_path, f"""
        message_attributes:
          - case: '{case}'
            id: {mid}
            index: 1
            set:
              AcceptedCF: application/cbor
            reason: "pick the second occurrence only"
        """))
    assert table.apply_message_attributes(project) == []

    assert items[1].accepted_cf == "application/cbor"
    for i, msg in enumerate(items):
        if i != 1:
            assert msg.accepted_cf == before[i]


def test_unknown_attribute_name_raises_at_load(tmp_path):
    """A misspelled XML attribute is a load-time ValueError, not a no-op —
    otherwise the override would look present in review and change nothing."""
    with pytest.raises(ValueError, match="unknown XML attribute"):
        OverrideTable.load(_write(tmp_path, f"""
            message_attributes:
              - case: '{CASE_5541}'
                id: {MSG_5541}
                set:
                  AcceptdCF: application/cbor
                reason: "typo in the attribute name"
            """))


def test_empty_set_raises_at_load(tmp_path):
    with pytest.raises(ValueError, match="NON-EMPTY mapping"):
        OverrideTable.load(_write(tmp_path, f"""
            message_attributes:
              - case: '{CASE_5541}'
                id: {MSG_5541}
                set: {{}}
                reason: "nothing to set"
            """))


def test_missing_reason_raises_at_load(tmp_path):
    """`reason` is mandatory: the entry overrides the certified template, so
    why has to be written down where the next reader sees it."""
    with pytest.raises(ValueError, match="non-empty 'reason'"):
        OverrideTable.load(_write(tmp_path, f"""
            message_attributes:
              - case: '{CASE_5541}'
                id: {MSG_5541}
                set:
                  AcceptedCF: application/cbor
            """))


def test_real_overrides_fix_the_5_5_4_1_row():
    """End to end on the shipped files: the real overrides.yaml applied to the
    real template turns 5.5.4.1's AcceptedCF="r" into "application/cbor".

    "r" is a fragment of "application/cbor" left in the template by an editing
    accident; the certified reference run recorded every GET fp/g/1 of this
    section with the full value (data/traces/5_5.xml). Since the interpreter
    now fails closed on an unparseable Content-Format (finding F9), losing this
    entry fails the TestCase rather than silently dropping the Accept option.
    """
    project = parse_eitt_xml(TEMPLATE)
    msg = _messages(project, CASE_5541, MSG_5541)[0]
    assert msg.accepted_cf == "r"

    table = OverrideTable.load(REAL_OVERRIDES)
    assert table.apply_message_attributes(project) == []
    assert msg.accepted_cf == "application/cbor"
