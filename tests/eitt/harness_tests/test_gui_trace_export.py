"""Tests for exporting a tracebuffer.

No Qt — the exporters are deliberately widget-free so they can be tested
headlessly and reused from a script.

The property that matters most: the EITT XML output must parse with the
SAME reader that reads `data/traces/*.xml`. An export nobody can open is
worse than no export, because the failure only shows up when someone needs
the file.
"""

from __future__ import annotations

import json
import xml.etree.ElementTree as ET

import pytest

from interpreter.events import RX, TX, CaseEvent, StepEvent, Telegram
from gui.trace import TraceBuffer
from gui.trace_export import export, to_eitt_xml, to_json, to_text


@pytest.fixture
def buffer():
    """A run with everything that renders differently: a passing pair, a
    preparation, a failed telegram and a missing one."""
    buf = TraceBuffer()
    buf.on_case(CaseEvent(name="Copy of 5.2 V40", phase="start"))
    buf.on_step(StepEvent(index=0, kind="Comment", ok=True,
                          summary="@[t5.2.1.1 Reading API version"))
    buf.on_telegram(Telegram(direction=TX, code="GET", msg_type=0,
                             path=".well-known/knx", mid=1234,
                             token=b"\x93\x37\xf7\xaf"))
    ok = Telegram(direction=RX, code="2.05", msg_type=2, mid=1234,
                  token=b"\x93\x37\xf7\xaf", raw=b"\xa1\x63api",
                  body={"api": {"version": "1.0.0"}},
                  content_format="60", peer=("fe80::1", 53806))
    ok.eval_ok = True
    buf.on_telegram(ok)

    buf.on_case(CaseEvent(name="AUTH PREPARATION", phase="start"))
    bad = Telegram(direction=RX, code="4.03", msg_type=2, mid=9)
    bad.eval_ok = False
    bad.eval_reason = "Message code differs, planned: 2.04, received: 4.03"
    buf.on_telegram(bad)
    buf.on_case(CaseEvent(name="AUTH PREPARATION", phase="end", ok=False))

    buf.on_step(StepEvent(index=5, kind="CoAPMessage", ok=False,
                          reason="no response received",
                          summary="OUT 2.05 /p/1"))
    buf.on_case(CaseEvent(name="Copy of 5.2 V40", phase="end", ok=False))
    return buf


# -- text ------------------------------------------------------------------

def test_text_keeps_the_gutter_numbers(buffer):
    text = to_text(buffer)
    assert "    1: <" in text
    assert "*** Start:" in text
    assert "* Conformance test result:" in text


def test_text_names_external_telegrams(buffer):
    """Blue vs green is invisible in plain text, so the distinction has to
    be spelled out or an exported trace loses whose telegram is whose."""
    assert "[ext]" in to_text(buffer)


# -- EITT XML --------------------------------------------------------------

def test_xml_parses_as_an_eitt_project(buffer):
    root = ET.fromstring(to_eitt_xml(buffer, "Copy of 5.2 V40"))
    assert root.tag == "EITT_Project"
    assert root.find("TraceBuffer") is not None


def test_xml_carries_the_telegrams_and_their_verdicts(buffer):
    """EvalResult is the reason exporting is worth anything: a trace
    without verdicts cannot be reviewed."""
    root = ET.fromstring(to_eitt_xml(buffer, "Copy of 5.2 V40"))
    messages = root.findall(".//CoAPMessage")
    assert len(messages) == 3

    verdicts = [m.get("EvalResult") for m in messages]
    assert "OK" in verdicts
    assert any(v and "Message code differs" in v for v in verdicts)


def test_xml_uses_eitts_hex_convention(buffer):
    """Space-separated uppercase, as EITT writes Token and RawContent."""
    root = ET.fromstring(to_eitt_xml(buffer, "plan"))
    message = root.findall(".//CoAPMessage")[0]
    assert message.get("Token") == "93 37 F7 AF"


def test_xml_direction_uses_eitts_cway(buffer):
    """EITT names direction from the DEVICE's point of view: a response the
    tool RECEIVES is CWay="OUT"."""
    root = ET.fromstring(to_eitt_xml(buffer, "plan"))
    messages = root.findall(".//CoAPMessage")
    assert messages[0].get("CWay") == "IN"     # tool -> device
    assert messages[1].get("CWay") == "OUT"    # device -> tool


def test_xml_records_a_missing_telegram(buffer):
    """An export that silently omitted a missing telegram would read as a
    clean run — the opposite of the truth."""
    xml = to_eitt_xml(buffer, "plan")
    assert "NOT RECEIVED" in xml
    assert "no response received" in xml


def test_xml_rxtime_is_a_filetime_pair(buffer):
    root = ET.fromstring(to_eitt_xml(buffer, "plan"))
    rx_time = root.findall(".//CoAPMessage")[0].get("RxTime")
    high, low = rx_time.split(".")
    assert int(high, 16) > 0
    assert len(low) == 8            # zero-padded low half, as EITT writes


# -- JSON ------------------------------------------------------------------

def test_json_is_valid_and_holds_the_wire_detail(buffer):
    payload = json.loads(to_json(buffer))
    telegrams = [ln["telegram"] for ln in payload["lines"]
                 if "telegram" in ln]
    assert len(telegrams) == 3

    received = telegrams[1]
    assert received["raw"] == "a163617069"
    assert received["eval_ok"] is True
    assert received["body"] == {"api": {"version": "1.0.0"}}


def test_json_survives_a_body_holding_bytes():
    """CBOR decodes to bytes for some fields, which json cannot serialise
    on its own — an export must not blow up on a real payload."""
    buf = TraceBuffer()
    buf.on_case(CaseEvent(name="c", phase="start"))
    buf.on_telegram(Telegram(direction=RX, code="2.05", msg_type=2,
                             body={"ms": b"\xaf\xfe"}))
    payload = json.loads(to_json(buf))          # must not raise
    assert payload["lines"]


# -- dispatch --------------------------------------------------------------

@pytest.mark.parametrize("suffix,marker", [
    (".txt", "*** Start:"),
    (".xml", "<EITT_Project"),
    (".json", '"lines"'),
])
def test_export_picks_the_format_from_the_extension(buffer, tmp_path,
                                                    suffix, marker):
    path = tmp_path / f"trace{suffix}"
    export(buffer, str(path), "plan")
    assert marker in path.read_text(encoding="utf-8")


def test_export_writes_utf8(buffer, tmp_path):
    """The default on Windows is the ANSI codepage, which mangles the
    arrows and rules the text format uses."""
    path = tmp_path / "trace.txt"
    export(buffer, str(path), "plan")
    # Would raise UnicodeDecodeError if it had been written as cp1252.
    assert path.read_text(encoding="utf-8")


def test_unknown_extension_falls_back_to_text(buffer, tmp_path):
    path = tmp_path / "trace.log"
    export(buffer, str(path), "plan")
    assert "*** Start:" in path.read_text(encoding="utf-8")
