"""Structured run telemetry: what happened on the wire, as data.

Phase 0 of the GUI plan (tests/docs/eitt-gui-plan.md). Every wire detail
the runner knows currently goes to stdout through `_dbg_*` print() calls,
so a GUI would have to scrape its own subprocess output — the exact thing
that makes EITT's own logs painful to work with. This module gives the
runner somewhere to *emit* instead of print.

Three deliberate constraints, because this code sits underneath a suite of
177 passing tests:

1. **The default sink is a no-op.** `SequenceRunner` with no `sink=`
   behaves exactly as before. Nothing here is on a hot path that can change
   a verdict.
2. **A misbehaving sink cannot fail a test.** `emit()` swallows every
   exception a sink raises (see `SinkError` handling in `EventBus`). A
   telemetry consumer is an observer; it must never be able to turn a
   passing conformance test red.
3. **Records are plain dataclasses, not live object references.** A
   `Telegram` holds the bytes and the decoded body as they were at emit
   time. The runner mutates `_last_response` and friends constantly, so
   handing a consumer a reference would give it whatever the value became,
   not what it was.

Beyond the GUI this is also what lets a run be diffed against
`data/traces/5_X.xml` automatically instead of by eye — the traces carry
the same fields (direction, code, path, EvalResult).
"""

from __future__ import annotations

import time
from dataclasses import dataclass, field
from typing import Any, Optional, Protocol, runtime_checkable

# Direction constants. These mirror the trace files' own vocabulary
# (`<AF NAK (TX)` / `>AF NAK (RX)`) rather than EITT's XML `IN`/`OUT`,
# because the XML's names are from the DEVICE's point of view and invert
# the moment you read them next to a capture. TX = tool sent it.
TX = "TX"
RX = "RX"


@dataclass
class Telegram:
    """One message on the wire, in either direction.

    `raw` is what was actually sent/received including the OSCORE wrapper
    where there is one; `body` is the decoded application payload. Both are
    kept: a GUI shows the decode, but a protocol argument is only ever
    settled by the bytes.
    """

    direction: str                      # TX or RX
    timestamp: float = field(default_factory=time.time)

    # CoAP framing
    code: str = ""                      # "GET", "2.05", "4.01" ...
    msg_type: Optional[int] = None      # CON/NON/ACK/RST, raw 2-bit value
    mid: Optional[int] = None
    token: Optional[bytes] = None
    path: str = ""
    query: str = ""

    # Payload
    raw: Optional[bytes] = None
    body: Any = None                    # decoded, or None when undecodable
    content_format: str = ""

    # Security. `secured` is whether THIS message used OSCORE — recorded
    # per-message precisely because a request and its response can differ
    # (the distinction finding F6 turns on).
    secured: bool = False
    sec_context: str = ""
    kid: Optional[bytes] = None
    piv: Optional[bytes] = None

    # Where it went / came from, as (host, port, ...) — carries the
    # multicast group address for group telegrams, which is often the only
    # way to tell one apart from a unicast reply.
    peer: Optional[tuple] = None

    # Set once the step that owns this telegram has been evaluated. Left
    # None for traffic that is captured but never matched against an
    # expectation (the trace files' TelNo=None rows).
    eval_ok: Optional[bool] = None
    eval_reason: str = ""


@dataclass
class StepEvent:
    """A single <Sequence> item finished."""

    index: int                          # position within the TestCase
    kind: str                           # "Comment" | "CoAPMessage" | "DnsMessage"
    ok: bool
    reason: str = ""
    # The XML element's GUID. This — not the display number — is what
    # anything durable keys on; EITT renumbers telegrams per generated
    # project, so `Nr` is a display artefact (see the GUI plan's
    # "Telegram numbers are dynamic" decision).
    item_id: str = ""
    activate: bool = True
    summary: str = ""                   # short human-readable description
    timestamp: float = field(default_factory=time.time)


@dataclass
class CaseEvent:
    """A TestCase started or finished."""

    name: str
    collection: str = ""
    phase: str = "start"                # "start" | "end"
    ok: Optional[bool] = None           # None while phase == "start"
    reason: str = ""
    timestamp: float = field(default_factory=time.time)


@runtime_checkable
class RunSink(Protocol):
    """What a telemetry consumer implements.

    Every method is optional in practice — `EventBus` uses getattr, so a
    sink that only cares about telegrams just defines `on_telegram`. A
    Protocol rather than an ABC so the GUI's Qt bridge doesn't have to
    inherit from anything (Qt objects already have a metaclass).
    """

    def on_telegram(self, telegram: Telegram) -> None: ...
    def on_step(self, event: StepEvent) -> None: ...
    def on_case(self, event: CaseEvent) -> None: ...


class EventBus:
    """Fans events out to zero or more sinks, and never raises.

    The isolation is the point. If a sink throws — a GUI whose window just
    closed, a file sink on a full disk — the conformance run continues and
    the verdict is unaffected. Failures are counted and exposed via
    `errors` so they are debuggable rather than invisible.
    """

    # Cap on retained sink failures. A sink that throws once usually throws
    # on every event, and a full 193-TestCase run emits thousands — an
    # unbounded list would hold a traceback-laden exception object for each.
    # The first few identify the problem; the rest are the same bug.
    MAX_ERRORS = 50

    def __init__(self, *sinks) -> None:
        self._sinks = [s for s in sinks if s is not None]
        self.errors: list[tuple[str, Exception]] = []
        # Total failures seen, including those past MAX_ERRORS — so
        # "3 errors" and "40,000 errors" stay distinguishable.
        self.error_count = 0

    def add(self, sink) -> None:
        if sink is not None:
            self._sinks.append(sink)

    @property
    def active(self) -> bool:
        """False when nothing is listening, so callers can skip building a
        record at all. Decoding a body purely to throw it away would be a
        real cost on the OSCORE path."""
        return bool(self._sinks)

    def _emit(self, method: str, payload) -> None:
        for sink in self._sinks:
            handler = getattr(sink, method, None)
            if handler is None:
                continue
            try:
                handler(payload)
            except Exception as exc:  # noqa: BLE001 - see class docstring
                self.error_count += 1
                if len(self.errors) < self.MAX_ERRORS:
                    self.errors.append((method, exc))

    def telegram(self, telegram: Telegram) -> None:
        self._emit("on_telegram", telegram)

    def step(self, event: StepEvent) -> None:
        self._emit("on_step", event)

    def case(self, event: CaseEvent) -> None:
        self._emit("on_case", event)


class NullBus(EventBus):
    """The default. `active` is False and every emit is a no-op, so the
    runner's telemetry calls cost one attribute lookup when unused."""

    def __init__(self) -> None:
        super().__init__()

    def _emit(self, method: str, payload) -> None:
        return


class RecordingSink:
    """Keeps everything in memory. For tests, and for the GUI's initial
    load before it swaps in a live model."""

    def __init__(self) -> None:
        self.telegrams: list[Telegram] = []
        self.steps: list[StepEvent] = []
        self.cases: list[CaseEvent] = []

    def on_telegram(self, telegram: Telegram) -> None:
        self.telegrams.append(telegram)

    def on_step(self, event: StepEvent) -> None:
        self.steps.append(event)

    def on_case(self, event: CaseEvent) -> None:
        self.cases.append(event)
