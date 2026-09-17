"""The tracebuffer: a live log of a running sequence, in EITT's format.

Fed by the phase-0 event sink (`eitt.events`), so a GUI run and a pytest run
execute identical code — the GUI just listens. Nothing here parses a saved
trace: the pane shows the run happening NOW and stays inspectable after it
finishes, within the same session. Saving and reloading is a separate topic,
along with persisting the CTL screen and its selection.

## Format, read off the real tool

```text
>>>>> Call sequence 'Copy of AUTH PREPARATION'.
  ============== AUTHENTICATION PREPARATION START ==============
 1:  <  2/25/2026 17:37:05.3 [COAP ] CON: GET {BDUT}/.well-known/knx :
 2:  >  2/25/2026 17:37:05.3 [COAP ] ACK: 2.05 [Content] {BDUT} [Data:] {...} :
  ============== AUTHENTICATION PREPARATION END ================
<<<<< Return to 'Copy of 5.2. /.well-known/knx V40'.
286: <  2/25/2026 17:41:58.1 [COAP ] CON: PUT {BDUT}/swu/pkgname [Data:] {...} :
```

Three things that carry meaning and are easy to lose:

1. **The gutter colour.** BLUE numbers are the sequence's own telegrams;
   **GREEN means externally loaded** — traffic from a called preparation.
   The two use SEPARATE counters, so a preparation runs 1,2,3… in green
   while the outer sequence continues 286,287… in blue.
2. **Unnumbered lines are captured but not awaited** — the `pahdiscover`
   multicast probes, for instance. They are real traffic and must appear,
   but they are not part of the numbering.
3. `<` is TX (tool → device), `>` is RX. Same convention as the trace files.
"""

from __future__ import annotations

import time
from dataclasses import dataclass, field
from enum import Enum
from typing import Optional

from eitt.events import RX, CaseEvent, StepEvent, Telegram


class LineKind(Enum):
    """What a tracebuffer line is, which decides how it is drawn."""

    TELEGRAM = "telegram"
    MISSING = "missing"          # expected, but nothing arrived
    COMMENT = "comment"          # narrative from an @[t comment
    OPERATOR = "operator"        # a human was asked to act (@@!)
    STRUCTURE = "structure"      # Call sequence / Return to / START / END
    HEADER = "header"            # the run's banner
    VERDICT = "verdict"          # the closing result block


@dataclass
class TraceLine:
    """One rendered line."""

    kind: LineKind
    text: str
    #: Display number for the gutter, or None for captured-but-not-awaited
    #: traffic (which EITT leaves unnumbered).
    number: Optional[int] = None
    #: True when this telegram belongs to a called preparation rather than
    #: the sequence proper — EITT draws those numbers GREEN.
    external: bool = False
    timestamp: float = field(default_factory=time.time)
    #: The Telegram this line came from, so clicking it can drive the
    #: properties pane. None for non-telegram lines.
    telegram: Optional[Telegram] = None
    #: For a MISSING line: the template message that was expected. Lets a
    #: click still show what SHOULD have happened, which is the only useful
    #: thing to display when nothing arrived.
    expected: object = None
    ok: Optional[bool] = None
    #: Why the step failed — shown as the properties pane's "Evaluation
    #: Result". For a missing telegram this is the ONLY diagnostic there
    #: is, so it must survive from the StepEvent all the way to the pane.
    reason: str = ""


class TraceBuffer:
    """Accumulates trace lines from run events.

    A plain sink (`eitt.events.RunSink`): the runner calls `on_telegram` /
    `on_step` / `on_case`, and this turns them into lines. Deliberately
    Qt-free so the formatting can be tested headlessly and reused by a
    future text export.

    NOT thread-safe by design. The runner lives on a worker thread and Qt
    widgets must only be touched from the GUI thread, so the Qt bridge
    marshals events across with a signal and calls this on the GUI side.
    Adding a lock here would invite someone to skip that.
    """

    def __init__(self) -> None:
        self.lines: list[TraceLine] = []
        # Separate counters, per the green/blue split above.
        self._sequence_number = 0
        self._external_number = 0
        # Depth of nested TestCases. run() emits a start/end pair for the
        # outer case AND for every @#"..." preparation it calls, so depth
        # > 1 is exactly "inside a preparation" — which is what makes a
        # telegram external.
        self._depth = 0
        self._telegrams = 0
        self._failures = 0

    # -- sink interface ----------------------------------------------------

    def on_case(self, event: CaseEvent) -> None:
        if event.phase == "start":
            self._depth += 1
            if self._depth > 1:
                # Entering a preparation: EITT announces the call and
                # restarts the green counter for it.
                self._external_number = 0
                self._structure(f">>>>> Call sequence '{event.name}'.")
                self._structure(f"  {'=' * 14} {event.name.upper()} "
                                f"START {'=' * 14}")
            else:
                self._header(f'*** Start: "{event.name}"  '
                             f'{_stamp(event.timestamp)}')
            return

        self._depth = max(0, self._depth - 1)
        if self._depth >= 1:
            self._structure(f"  {'=' * 14} {event.name.upper()} "
                            f"END {'=' * 14}")
            self._structure("<<<<< Return.")
        else:
            self.add_verdict(event.name)

    def on_step(self, event: StepEvent) -> None:
        # A failed OUT step is the "expected but never arrived" case: the
        # runner emits no telegram because none came back, so without this
        # the trace would simply stop, which reads as "still running".
        if event.kind in ("CoAPMessage", "DnsMessage") and not event.ok:
            if _is_timeout(event.reason):
                self._add_missing_from_step(event)
            return

        # Only narrative comments reach the tracebuffer — `@[t` is literally
        # "Append to trace buffer" in EITT's own command scheme. Telegram
        # steps arrive via on_telegram with far more detail.
        if event.kind != "Comment" or not event.summary:
            return
        text = event.summary
        if text.startswith("@[t"):
            text = text[3:].strip()
        elif text.startswith("@"):
            return          # a provisioning/wait command, not narrative
        if text:
            self._comment(text)

    def on_telegram(self, telegram: Telegram) -> None:
        self._telegrams += 1
        # NOTE: eval_ok is stamped by the OUT step that judges this
        # telegram, which runs AFTER it is emitted. So the count cannot be
        # taken here — `_failed_count()` reads the records at verdict time
        # instead, when every verdict is in.
        external = self._depth > 1
        if external:
            self._external_number += 1
            number = self._external_number
        else:
            self._sequence_number += 1
            number = self._sequence_number

        self.lines.append(TraceLine(
            kind=LineKind.TELEGRAM, text=format_telegram(telegram),
            number=number, external=external, telegram=telegram,
            ok=telegram.eval_ok, timestamp=telegram.timestamp))

    # -- explicit additions -------------------------------------------------

    def _add_missing_from_step(self, event: StepEvent) -> None:
        """Render a timed-out step as a NOT RECEIVED row.

        Built from the StepEvent's summary because the sink carries no
        reference back to the template object — `summary` is already
        "OUT 2.05 /p/1", which is exactly what was expected.
        """
        self._telegrams += 1
        self._failures += 1
        external = self._depth > 1
        if external:
            self._external_number += 1
            number = self._external_number
        else:
            self._sequence_number += 1
            number = self._sequence_number
        self.lines.append(TraceLine(
            kind=LineKind.MISSING, number=number, external=external,
            ok=False, reason=event.reason,
            text=f"!  NOT RECEIVED: expected {event.summary}"))

    def add_missing(self, expected, reason: str = "") -> None:
        """A telegram the sequence expected that never arrived.

        Rendered as a full RED row rather than left out. An absent line is
        indistinguishable from a step that has not run yet, which is
        exactly the wrong impression when a device has gone silent — the
        most common real failure.

        `expected` is the template's CoAPMessage/DnsMessage, so clicking
        the row can still show what SHOULD have happened.
        """
        self._telegrams += 1
        self._failures += 1
        external = self._depth > 1
        if external:
            self._external_number += 1
            number = self._external_number
        else:
            self._sequence_number += 1
            number = self._sequence_number

        code = getattr(expected, "code", "") or ""
        path = getattr(expected, "path", "") or ""
        detail = f" — {reason}" if reason else ""
        self.lines.append(TraceLine(
            kind=LineKind.MISSING,
            text=f"!  NOT RECEIVED: expected {code} {path}{detail}".rstrip(),
            number=number, external=external, expected=expected, ok=False,
            reason=reason))

    def add_operator_action(self, prompt: str) -> None:
        """Record that a human was asked to do something.

        Part of what happened, and load-bearing: without it a trace shows
        the device changing state between two telegrams for no visible
        reason. EITT logs these the same way.
        """
        self.lines.append(TraceLine(
            kind=LineKind.OPERATOR,
            text=f"[operator] {' '.join(prompt.split())}"))

    def add_unnumbered(self, telegram: Telegram) -> None:
        """Traffic that was captured but never awaited — the pahdiscover
        probes. Real, shown, but outside the numbering."""
        self._telegrams += 1
        self.lines.append(TraceLine(
            kind=LineKind.TELEGRAM, text=format_telegram(telegram),
            number=None, telegram=telegram, timestamp=telegram.timestamp))

    def _failed_count(self) -> int:
        """Failures, read at verdict time.

        Counted from the records rather than tallied on arrival: a
        telegram's `eval_ok` is stamped by the OUT step that judges it,
        which happens after it was emitted. Tallying on arrival reported
        "0 telegrams failed" over a screen full of 4.03s.
        """
        failed = 0
        for line in self.lines:
            if line.kind is LineKind.MISSING:
                failed += 1
            elif (line.kind is LineKind.TELEGRAM and line.telegram is not None
                    and line.telegram.eval_ok is False):
                failed += 1
        return failed

    def add_verdict(self, name: str) -> None:
        self.lines.append(TraceLine(
            kind=LineKind.VERDICT,
            text=(f"* Conformance test result: {self._telegrams} telegrams "
                  f"found (0 AAP). {self._failed_count()} telegrams "
                  f"failed")))
        self.lines.append(TraceLine(
            kind=LineKind.VERDICT,
            text=f'*** End: "{name}"  {_stamp(time.time())}'))

    def clear(self) -> None:
        self.__init__()

    # -- internals ---------------------------------------------------------

    def _structure(self, text: str) -> None:
        self.lines.append(TraceLine(kind=LineKind.STRUCTURE, text=text))

    def _comment(self, text: str) -> None:
        self.lines.append(TraceLine(kind=LineKind.COMMENT, text=text))

    def _header(self, text: str) -> None:
        self.lines.append(TraceLine(kind=LineKind.HEADER, text=text))

    def __len__(self) -> int:
        return len(self.lines)


def _is_timeout(reason: str) -> bool:
    """Whether a step failed because NOTHING arrived, as opposed to
    something arriving that did not match.

    The two look very different to an operator: a content mismatch is a
    conformance finding, silence is usually a dead device or a wrong
    address. Only the second earns the red NOT RECEIVED row.
    """
    lowered = (reason or "").lower()
    return any(marker in lowered for marker in (
        "no response", "timed out", "timeout", "received nothing",
        "nothing was received"))


def _stamp(when: float) -> str:
    """`2/25/2026 17:37:05.3` — EITT's format, tenths and all."""
    local = time.localtime(when)
    tenths = int((when % 1) * 10)
    return (f"{local.tm_mon}/{local.tm_mday}/{local.tm_year} "
            f"{local.tm_hour:02d}:{local.tm_min:02d}:{local.tm_sec:02d}"
            f".{tenths}")


def format_telegram(telegram: Telegram) -> str:
    """One telegram as EITT writes it.

        <  2/25/2026 17:37:05.3 [COAP ] CON: GET {BDUT}/.well-known/knx :
        >  2/25/2026 17:37:05.3 [COAP ] ACK: 2.05 [Content] {BDUT} [Data:] {...} :
    """
    arrow = ">" if telegram.direction == RX else "<"
    stamp = _stamp(telegram.timestamp)
    kind = "mDNS" if telegram.content_format == "mdns" else "COAP"

    parts = [f"{arrow}  {stamp} [{kind:<5}] "]
    msg_type = _MSG_TYPE_NAMES.get(telegram.msg_type, "")
    code = _code_label(telegram.code)
    parts.append(f"{msg_type}: {code} " if msg_type else f"{code} ")

    # `{BDUT}` for the configured device, a literal address for anything
    # else — that distinction is how a multicast probe is spotted.
    target = telegram.path or ""
    if telegram.direction == RX:
        parts.append("{BDUT} " if not telegram.peer else _peer(telegram.peer))
    else:
        parts.append("{BDUT}/" + target.lstrip("/") + " " if target
                     else "{BDUT} ")

    if telegram.body not in (None, "", {}):
        parts.append(f"[Data:] {telegram.body} ")
    parts.append(":")
    return "".join(parts)


def _peer(peer) -> str:
    if not peer:
        return "{BDUT} "
    host = peer[0]
    port = peer[1] if len(peer) > 1 else None
    return f"[{host}]:{port} " if port else f"[{host}] "


# CoAP message types as EITT names them.
_MSG_TYPE_NAMES = {0: "CON", 1: "NON", 2: "ACK", 3: "RST"}

_CODE_NAMES = {
    "2.01": "Created", "2.02": "Deleted", "2.03": "Valid",
    "2.04": "Changed", "2.05": "Content",
    "4.00": "Bad Request", "4.01": "Unauthorized", "4.02": "Bad Option",
    "4.03": "Forbidden", "4.04": "Not Found",
    "4.05": "Method Not Allowed", "4.06": "Not Acceptable",
    "4.08": "Request Entity Incomplete",
    "4.13": "Request Entity Too Large",
    "4.15": "Unsupported Content-Format",
    "5.00": "Internal Server Error", "5.01": "Not Implemented",
    "5.03": "Service Unavailable", "5.05": "Proxying Not Supported",
}


def _code_label(code: str) -> str:
    name = _CODE_NAMES.get((code or "").strip())
    return f"{code} [{name}]" if name else (code or "")
