"""Exporting a tracebuffer.

Three formats, each for a different job:

- **text** — what the pane shows, verbatim. For pasting into a bug report
  or diffing two runs with ordinary tools.
- **EITT XML** — the same shape EITT writes (`data/traces/*.xml`). Our own
  tooling already parses those, so an exported run can be compared against
  a recorded certification run with the scripts that exist.
- **JSON** — every field of every record, including the ones neither of the
  above carries (raw bytes, kid/PIV, decoded bodies). For a script.

Qt-free, so the formatting is testable headlessly and reusable from a CLI.

## On matching EITT's XML

Fidelity is deliberate but partial, and the gaps are honest rather than
guessed:

- `RxTime` is EITT's Windows FILETIME split into two hex halves. We emit
  the same shape from the record's own timestamp.
- `PlanSource`, `RcvStatus`, `SeqNumMethod` are EITT's internal
  bookkeeping. We write what we can justify and omit what we would be
  inventing — a plausible-looking wrong value is worse than an absent one.
- `Token` and `RawContent` are space-separated uppercase hex, as EITT
  writes them.
"""

from __future__ import annotations

import json
import time
import xml.etree.ElementTree as ET
from xml.dom import minidom

from eitt.events import RX

from .trace import LineKind, TraceBuffer

# Windows FILETIME epoch (1601-01-01) to Unix epoch, in seconds.
_FILETIME_EPOCH_OFFSET = 11644473600


def to_text(buffer: TraceBuffer) -> str:
    """The pane's content as plain text, gutter included."""
    lines = []
    for line in buffer.lines:
        gutter = "" if line.number is None else f"{line.number}:"
        marker = ""
        if line.number is not None:
            # The blue/green distinction is load-bearing and invisible in
            # plain text, so name it.
            marker = "[ext] " if line.external else ""
        lines.append(f"{gutter:>6} {marker}{line.text}")
    return "\n".join(lines) + "\n"


def to_json(buffer: TraceBuffer) -> str:
    """Every field of every record — for a script, not for reading."""
    records = []
    for line in buffer.lines:
        record = {
            "kind": line.kind.value,
            "text": line.text,
            "number": line.number,
            "external": line.external,
            "timestamp": line.timestamp,
        }
        if line.ok is not None:
            record["ok"] = line.ok
        if line.reason:
            record["reason"] = line.reason

        telegram = line.telegram
        if telegram is not None:
            record["telegram"] = {
                "direction": telegram.direction,
                "code": telegram.code,
                "msg_type": telegram.msg_type,
                "mid": telegram.mid,
                "token": telegram.token.hex() if telegram.token else None,
                "path": telegram.path,
                "content_format": telegram.content_format,
                "secured": telegram.secured,
                "sec_context": telegram.sec_context,
                "peer": list(telegram.peer) if telegram.peer else None,
                "raw": telegram.raw.hex() if telegram.raw else None,
                # `default=str` below handles a body holding bytes, which
                # json cannot serialise on its own.
                "body": telegram.body,
                "eval_ok": telegram.eval_ok,
                "eval_reason": telegram.eval_reason,
            }
        records.append(record)

    return json.dumps({
        "exported": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "lines": records,
    }, indent=2, default=str)


def to_eitt_xml(buffer: TraceBuffer, plan_source: str = "") -> str:
    """EITT's own trace format, so our tooling can read an exported run.

    `plan_source` names the project the run came from; EITT puts the copy's
    name there (e.g. "Copy of 5.2. /.well-known/knx V40").
    """
    root = ET.Element("EITT_Project", DocVersion="1")
    trace = ET.SubElement(root, "TraceBuffer")

    for line in buffer.lines:
        if line.kind in (LineKind.HEADER, LineKind.VERDICT,
                         LineKind.STRUCTURE, LineKind.OPERATOR):
            # Type 1 is EITT's start/end banner, 16 an informational line.
            ET.SubElement(trace, "TraceInfo",
                          Type="1" if line.kind is LineKind.HEADER else "16",
                          Info=line.text)
        elif line.kind is LineKind.COMMENT:
            ET.SubElement(trace, "Comment", Text=line.text)
        elif line.kind is LineKind.MISSING:
            # No telegram to describe, so record it as an evaluated
            # NOTHING — an exported trace that silently omitted a missing
            # telegram would read as a clean run.
            ET.SubElement(trace, "TraceInfo", Type="16",
                          Info=f"{line.text}"
                               + (f"  [{line.reason}]" if line.reason
                                  else ""))
        elif line.telegram is not None:
            _append_telegram(trace, line, plan_source)

    raw = ET.tostring(root, encoding="unicode")
    # Pretty-print: these get read by humans and diffed against the
    # recorded traces, which are indented.
    return minidom.parseString(raw).toprettyxml(indent="\t")


def _append_telegram(parent, line, plan_source: str) -> None:
    telegram = line.telegram
    attributes = {
        "Active": "Y",
        "CWay": "OUT" if telegram.direction == RX else "IN",
        "Type": _MSG_TYPE_NAMES.get(telegram.msg_type, ""),
        "Method": telegram.code or "",
        "Path": telegram.path or "",
        "RxTime": _filetime(telegram.timestamp),
    }
    if plan_source:
        attributes["PlanSource"] = plan_source
    if telegram.content_format:
        attributes["ContentFormat"] = telegram.content_format
    if telegram.mid is not None:
        attributes["Mid"] = str(telegram.mid)
    if telegram.token:
        attributes["Token"] = _hex(telegram.token)
    if telegram.peer:
        attributes["DestinationAddress"] = _address(telegram.peer)
    if telegram.secured:
        attributes["ExpSecured"] = "Y"
    if telegram.raw:
        attributes["RawContent"] = _hex(telegram.raw)
    # EvalResult is EITT's verdict column, and the reason it is worth
    # exporting at all: a trace without verdicts cannot be reviewed.
    if telegram.eval_ok is True:
        attributes["EvalResult"] = "OK"
    elif telegram.eval_ok is False:
        attributes["EvalResult"] = telegram.eval_reason or "FAILED"

    ET.SubElement(parent, "CoAPMessage", **attributes)


def _hex(data: bytes) -> str:
    """`39 33 33 37` — space-separated uppercase, as EITT writes it."""
    return " ".join(f"{b:02X}" for b in data)


def _address(peer) -> str:
    host = peer[0]
    return f"[{host}]:{peer[1]}" if len(peer) > 1 else f"[{host}]"


def _filetime(when: float) -> str:
    """EITT's `RxTime`: a Windows FILETIME as two hex halves.

    100-nanosecond ticks since 1601-01-01. Reproduced so an exported trace
    sorts and diffs against a recorded one rather than needing a special
    case at the other end.
    """
    ticks = int((when + _FILETIME_EPOCH_OFFSET) * 10_000_000)
    return f"{ticks >> 32:X}.{ticks & 0xFFFFFFFF:08X}"


_MSG_TYPE_NAMES = {0: "CON", 1: "NON", 2: "ACK", 3: "RST"}


#: Extension -> (label, writer). Drives the GUI's save dialog.
FORMATS = {
    ".txt": ("Text", to_text),
    ".xml": ("EITT trace XML", to_eitt_xml),
    ".json": ("JSON", to_json),
}


def export(buffer: TraceBuffer, path: str, plan_source: str = "") -> None:
    """Write `buffer` to `path`, choosing the format from its extension."""
    lowered = path.lower()
    if lowered.endswith(".xml"):
        content = to_eitt_xml(buffer, plan_source)
    elif lowered.endswith(".json"):
        content = to_json(buffer)
    else:
        content = to_text(buffer)
    # UTF-8 explicitly: the default on Windows is the ANSI codepage, which
    # mangles the box-drawing and arrows the text format uses.
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(content)
