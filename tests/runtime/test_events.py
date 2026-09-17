"""Unit tests for the structured telemetry bus (GUI plan phase 0).

These run WITHOUT a DUT — no Docker, no veth, no device. They drive
SequenceRunner with a fake CoAP client, so they belong to the fast local
tier rather than the conformance suite. The point is to pin down the three
properties the rest of the suite depends on:

  1. no sink => no behaviour change and no record construction
  2. a raising sink cannot fail a run
  3. the emitted records actually describe what went on the wire
"""

from __future__ import annotations

import os

import pytest

from eitt.events import (EventBus, NullBus, RecordingSink, RX, StepEvent, TX,
                         Telegram)
from eitt.model import CoAPMessage, parse_eitt_xml
from eitt.provisioning import ProvisioningEngine
from eitt.runner import PreparationRegistry, SequenceRunner
from eitt.variables import VariableTable
from transport.coap_client import ACK, CoapResponse

# Resolved against THIS file, not the cwd: the Docker harness invokes
# pytest from the repo root, where a bare "data/..." does not exist.
TEMPLATE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "data", "KnxConformanceTestTemplate-PointAPI.xml")


class FakeCoap:
    """Answers every request 2.05 with an empty CBOR map, and records what
    it was asked to send so a test can assert the runner still drives the
    transport identically when telemetry is on."""

    host = "fd00::1"
    port = 5683

    def __init__(self):
        self.sent: list[tuple[str, str]] = []

    def _reply(self):
        return CoapResponse(code_class=2, code_detail=5, payload=b"\xa0",
                            content_format="application/cbor", msg_type=ACK,
                            mid=7, token=b"\x0a", options={},
                            source_addr=("fd00::1", 5683, 0, 0))

    def get(self, path, **kw):
        self.sent.append(("GET", path))
        return self._reply()

    def post(self, path, **kw):
        self.sent.append(("POST", path))
        return self._reply()

    def put(self, path, **kw):
        self.sent.append(("PUT", path))
        return self._reply()

    def delete(self, path, **kw):
        self.sent.append(("DELETE", path))
        return self._reply()

    def multicast_get(self, path, **kw):
        self.sent.append(("MGET", path))
        return [self._reply()]


@pytest.fixture(scope="module")
def project():
    return parse_eitt_xml(TEMPLATE)


@pytest.fixture(scope="module")
def coap_case(project):
    """The first TestCase that issues at least two plain (non-OSCORE)
    requests, so the TX/RX emit sites are actually reached."""
    for coll, _suite, tc in project.iter_test_cases():
        requests = [s for s in tc.sequence
                    if isinstance(s, CoAPMessage) and s.activate
                    and not s.is_response and not s.sec_context]
        if len(requests) >= 2:
            return getattr(coll, "name", str(coll)), tc
    pytest.skip("no plain-CoAP TestCase in the template")


def _make_runner(project, collection, coap, sink=None):
    variables = VariableTable.for_collection(project, collection)
    return SequenceRunner(coap=coap, project=project, variables=variables,
                          provisioning=ProvisioningEngine(variables),
                          preparations=PreparationRegistry(project),
                          sink=sink, sleep=lambda _s: None)


# -- the bus itself --------------------------------------------------------

def test_nullbus_is_inert():
    bus = NullBus()
    assert bus.active is False
    bus.telegram(Telegram(direction=TX))
    bus.step(StepEvent(index=0, kind="Comment", ok=True))
    assert bus.errors == []


def test_sink_exception_cannot_escape():
    """A telemetry consumer is an observer. If it throws, the conformance
    run must continue — a GUI crash must never turn a passing test red."""

    class Exploding:
        def on_telegram(self, _t):
            raise RuntimeError("boom")

    bus = EventBus(Exploding())
    bus.telegram(Telegram(direction=TX))          # must not raise
    assert len(bus.errors) == 1
    assert isinstance(bus.errors[0][1], RuntimeError)


def test_error_list_is_capped_but_counted():
    """A sink that throws once usually throws on every event, and a full
    193-TestCase run emits thousands. Retaining an exception object (and its
    traceback) for each would be a slow leak, but the TOTAL still has to be
    reported or "3 errors" and "40,000 errors" look identical."""

    class AlwaysFails:
        def on_telegram(self, _t):
            raise RuntimeError("boom")

    bus = EventBus(AlwaysFails())
    for _ in range(EventBus.MAX_ERRORS + 25):
        bus.telegram(Telegram(direction=TX))

    assert len(bus.errors) == EventBus.MAX_ERRORS
    assert bus.error_count == EventBus.MAX_ERRORS + 25


def test_partial_sink_is_allowed():
    """A sink defining only on_telegram must not blow up on step/case."""

    class OnlyTelegrams:
        def __init__(self):
            self.seen = []

        def on_telegram(self, t):
            self.seen.append(t)

    sink = OnlyTelegrams()
    bus = EventBus(sink)
    bus.step(StepEvent(index=0, kind="Comment", ok=True))
    bus.telegram(Telegram(direction=RX))
    assert len(sink.seen) == 1
    assert bus.errors == []


# -- integration with the runner -------------------------------------------

def test_default_runner_has_no_active_sink(project, coap_case):
    """The property the whole suite rests on: constructing a runner the way
    test_eitt.py does leaves telemetry off, so no record is ever built."""
    collection, _tc = coap_case
    runner = _make_runner(project, collection, FakeCoap())
    assert isinstance(runner.events, NullBus)
    assert runner.events.active is False


def test_telemetry_does_not_change_transport(project, coap_case):
    """Same case, run with and without a sink, must drive the CoAP client
    identically. This is the regression guard for phase 0 being additive."""
    collection, tc = coap_case

    silent = FakeCoap()
    quiet_result = _make_runner(project, collection, silent).run(tc)

    noisy = FakeCoap()
    observed = _make_runner(project, collection, noisy,
                            sink=RecordingSink()).run(tc)

    assert silent.sent == noisy.sent
    assert quiet_result.ok == observed.ok
    assert len(quiet_result.steps) == len(observed.steps)


def test_emits_tx_rx_pair_with_wire_detail(project, coap_case):
    collection, tc = coap_case
    sink = RecordingSink()
    runner = _make_runner(project, collection, FakeCoap(), sink=sink)
    runner.run(tc)

    assert runner.events.errors == []
    assert sink.telegrams, "expected at least one telegram"

    # `@@[pahdiscover` sweeps the multicast scopes before the sequence
    # proper, so skip `.well-known/core` — this test is about the pairing
    # of a scripted request with its response.
    def _own(direction):
        return next(t for t in sink.telegrams
                    if t.direction == direction
                    and not t.path.endswith(".well-known/core"))

    tx = _own(TX)
    rx = _own(RX)

    # The request carries its method and the path it was actually sent to
    # (post-substitution), not the raw #VAR from the XML.
    assert tx.code in ("GET", "POST", "PUT", "DELETE")
    assert tx.path and "#" not in tx.path

    # The response is labelled with the path it ANSWERS, not the next
    # request's — the off-by-one _last_request_path ordering guards against.
    assert rx.code == "2.05"
    assert rx.path == tx.path
    assert rx.body == {}          # decoded from the b"\xa0" empty CBOR map


def test_emits_step_and_case_events(project, coap_case):
    collection, tc = coap_case
    sink = RecordingSink()
    _make_runner(project, collection, FakeCoap(), sink=sink).run(tc)

    assert len(sink.steps) > 0
    assert all(s.kind in ("Comment", "CoAPMessage", "DnsMessage")
               for s in sink.steps)
    # Indices are the position within the sequence and must be ascending
    # within one case (they restart for a nested @# preparation).
    assert [s.index for s in sink.steps] == sorted(s.index for s in sink.steps)

    # A start/end pair brackets the run, and `end` carries the verdict.
    assert sink.cases[0].phase == "start"
    assert sink.cases[0].ok is None
    assert sink.cases[-1].phase == "end"
    assert sink.cases[-1].ok is not None


def test_link_format_body_is_not_cbor_decoded(project, coap_case):
    """A response's OWN Content-Format decides the decode. Running a
    link-format body through the CBOR decoder just because most bodies are
    CBOR would show garbage (or None) for every discovery response."""
    collection, tc = coap_case
    link = b'</p/1>;rt="urn:knx:dpa.417.61";if="if.s"'

    class LinkFormatCoap(FakeCoap):
        def _reply(self):
            return CoapResponse(code_class=2, code_detail=5, payload=link,
                                content_format="application/link-format",
                                msg_type=ACK, mid=7, token=b"\x0a",
                                options={}, source_addr=None)

    sink = RecordingSink()
    _make_runner(project, collection, LinkFormatCoap(), sink=sink).run(tc)

    # Skip `.well-known/core`: `@@[pahdiscover` sweeps the multicast scopes
    # before the sequence proper, and those replies are discovery traffic
    # rather than the response this test is about.
    received = [t for t in sink.telegrams
                if t.direction == RX and t.code
                and not t.path.endswith(".well-known/core")]
    assert received, "expected a decoded response"
    assert received[0].content_format == "application/link-format"
    assert received[0].body == link.decode()


def test_timeout_is_emitted_as_an_rx_record(project, coap_case):
    """"Nothing arrived" is an observation, not an absence. A tracebuffer
    that silently omitted it would show a request with no answer and no
    explanation."""
    collection, tc = coap_case

    class SilentCoap(FakeCoap):
        def get(self, path, **kw):
            self.sent.append(("GET", path))
            return None

        def post(self, path, **kw):
            self.sent.append(("POST", path))
            return None

    sink = RecordingSink()
    _make_runner(project, collection, SilentCoap(), sink=sink).run(tc)

    timeouts = [t for t in sink.telegrams
                if t.direction == RX and not t.code]
    assert timeouts, "a timed-out request should still emit an RX record"
    # The placeholder reason set at emit time is REPLACED by the real
    # verdict once the OUT step judges the (absent) response, so match on
    # the fact rather than on either wording.
    assert timeouts[0].eval_ok is not True
    assert "timed out" in timeouts[0].eval_reason.lower() \
        or "timeout" in timeouts[0].eval_reason.lower()
