"""Tests for the live tracebuffer.

The format is EITT's, read off screenshots of the real tool. The part that
carries meaning — and is easy to lose — is the number gutter: blue for the
sequence's own telegrams, GREEN for externally loaded ones (from a called
preparation), and blank for traffic captured but never awaited. Blue and
green run SEPARATE counters.

No Qt needed for any of this; `gui.trace` is deliberately widget-free.
"""

from __future__ import annotations

import pytest

from eitt.events import RX, TX, CaseEvent, StepEvent, Telegram
from gui.trace import LineKind, TraceBuffer, format_telegram


def _telegram(**kwargs):
    kwargs.setdefault("direction", TX)
    kwargs.setdefault("code", "GET")
    kwargs.setdefault("msg_type", 0)
    return Telegram(**kwargs)


def _run_with_preparation():
    """The shape from the screenshots: an outer sequence that calls a
    preparation partway through."""
    buffer = TraceBuffer()
    buffer.on_case(CaseEvent(name="Copy of 5.2 V40", phase="start"))
    buffer.on_telegram(_telegram(path="/.well-known/knx"))
    buffer.on_telegram(_telegram(direction=RX, code="2.05", msg_type=2))

    buffer.on_case(CaseEvent(name="AUTH PREPARATION", phase="start"))
    buffer.on_telegram(_telegram(code="POST", path="/auth/at"))
    buffer.on_telegram(_telegram(direction=RX, code="2.01", msg_type=2))
    buffer.on_case(CaseEvent(name="AUTH PREPARATION", phase="end", ok=True))

    buffer.on_telegram(_telegram(code="PUT", path="/swu/pkgname"))
    buffer.on_case(CaseEvent(name="Copy of 5.2 V40", phase="end", ok=True))
    return buffer


def _telegrams(buffer):
    return [ln for ln in buffer.lines if ln.kind is LineKind.TELEGRAM]


# -- numbering -------------------------------------------------------------

def test_preparation_telegrams_are_external_and_counted_separately():
    """A preparation numbers 1,2,3… in GREEN while the outer sequence
    continues in blue — the single most load-bearing detail of the gutter."""
    lines = _telegrams(_run_with_preparation())

    assert [(ln.number, ln.external) for ln in lines] == [
        (1, False),     # outer
        (2, False),
        (1, True),      # preparation restarts, green
        (2, True),
        (3, False),     # outer resumes where it left off
    ]


def test_nested_preparations_stay_external():
    """A preparation may itself call one; everything below the outermost
    case is external, not just the first level down."""
    buffer = TraceBuffer()
    buffer.on_case(CaseEvent(name="outer", phase="start"))
    buffer.on_case(CaseEvent(name="prep", phase="start"))
    buffer.on_case(CaseEvent(name="inner prep", phase="start"))
    buffer.on_telegram(_telegram())
    assert _telegrams(buffer)[0].external is True


def test_unnumbered_traffic_is_shown_but_not_counted():
    """The pahdiscover probes are captured but never awaited. They must
    appear — they are real traffic — without taking a number."""
    buffer = TraceBuffer()
    buffer.on_case(CaseEvent(name="case", phase="start"))
    buffer.add_unnumbered(_telegram(path="/.well-known/core"))
    buffer.on_telegram(_telegram(path="/.well-known/knx"))

    lines = _telegrams(buffer)
    assert lines[0].number is None
    assert lines[1].number == 1


# -- structure -------------------------------------------------------------

def test_call_and_return_bracket_a_preparation():
    texts = [ln.text for ln in _run_with_preparation().lines
             if ln.kind is LineKind.STRUCTURE]
    assert any(t.startswith(">>>>> Call sequence") for t in texts)
    assert any("START" in t for t in texts)
    assert any("END" in t for t in texts)
    assert any(t.startswith("<<<<<") for t in texts)


def test_run_is_bracketed_by_header_and_verdict():
    buffer = _run_with_preparation()
    assert buffer.lines[0].kind is LineKind.HEADER
    assert buffer.lines[0].text.startswith("*** Start:")

    verdicts = [ln.text for ln in buffer.lines
                if ln.kind is LineKind.VERDICT]
    assert "5 telegrams found" in verdicts[0]
    assert "0 telegrams failed" in verdicts[0]
    assert verdicts[-1].startswith("*** End:")


def test_failed_telegrams_are_counted_in_the_verdict():
    buffer = TraceBuffer()
    buffer.on_case(CaseEvent(name="case", phase="start"))
    buffer.on_telegram(_telegram(eval_ok=True))
    buffer.on_telegram(_telegram(eval_ok=False, eval_reason="no match"))
    buffer.on_case(CaseEvent(name="case", phase="end", ok=False))

    verdict = next(ln.text for ln in buffer.lines
                   if ln.kind is LineKind.VERDICT)
    assert "2 telegrams found" in verdict
    assert "1 telegrams failed" in verdict


# -- expected but never arrived --------------------------------------------

def test_timed_out_step_becomes_a_missing_row():
    """An absent line reads as "not run yet". A device that went silent —
    the most common real failure — has to be visible as a failure."""
    buffer = TraceBuffer()
    buffer.on_case(CaseEvent(name="case", phase="start"))
    buffer.on_step(StepEvent(index=3, kind="CoAPMessage", ok=False,
                             reason="no response received within 5.0s",
                             summary="OUT 2.05 /p/1"))

    missing = [ln for ln in buffer.lines if ln.kind is LineKind.MISSING]
    assert len(missing) == 1
    assert "NOT RECEIVED" in missing[0].text
    assert missing[0].ok is False
    # The assessment must survive to the properties pane's Evaluation
    # Result — for a missing telegram it is the only diagnostic there is.
    assert missing[0].reason == "no response received within 5.0s"


def test_content_mismatch_is_not_a_missing_row():
    """Something DID arrive, it just did not match. That is a conformance
    finding; silence is a dead device or a wrong address. Conflating them
    would send the operator looking in the wrong place."""
    buffer = TraceBuffer()
    buffer.on_case(CaseEvent(name="case", phase="start"))
    buffer.on_step(StepEvent(index=3, kind="CoAPMessage", ok=False,
                             reason="content mismatch: expected 'x' got 'y'",
                             summary="OUT 2.05 /p/1"))
    assert not [ln for ln in buffer.lines if ln.kind is LineKind.MISSING]


def test_missing_rows_are_numbered_and_counted_as_failures():
    buffer = TraceBuffer()
    buffer.on_case(CaseEvent(name="case", phase="start"))
    buffer.on_telegram(_telegram())
    buffer.on_step(StepEvent(index=1, kind="CoAPMessage", ok=False,
                             reason="timed out", summary="OUT 2.05 /p/1"))
    buffer.on_case(CaseEvent(name="case", phase="end", ok=False))

    missing = next(ln for ln in buffer.lines
                   if ln.kind is LineKind.MISSING)
    assert missing.number == 2          # shares the sequence's numbering

    verdict = next(ln.text for ln in buffer.lines
                   if ln.kind is LineKind.VERDICT)
    assert "2 telegrams found" in verdict
    assert "1 telegrams failed" in verdict


def test_missing_row_inside_a_preparation_is_external():
    buffer = TraceBuffer()
    buffer.on_case(CaseEvent(name="outer", phase="start"))
    buffer.on_case(CaseEvent(name="AUTH PREPARATION", phase="start"))
    buffer.on_step(StepEvent(index=0, kind="CoAPMessage", ok=False,
                             reason="no response", summary="OUT 2.05 /p/1"))
    missing = next(ln for ln in buffer.lines
                   if ln.kind is LineKind.MISSING)
    assert missing.external is True


def test_add_missing_keeps_the_expected_message():
    """Clicking the row must still show what SHOULD have happened."""
    from eitt.model import CoAPMessage

    expected = CoAPMessage(
        id="m", activate=True, time_to_next="", cway="OUT", wait=True,
        type="ACK", code="2.05", cf="", accepted_cf="", ip="", path="/p/1",
        sec_context="", secure=None, comment_attr="", content=None,
        cbor=None)

    buffer = TraceBuffer()
    buffer.on_case(CaseEvent(name="case", phase="start"))
    buffer.add_missing(expected, reason="device did not answer")

    line = next(ln for ln in buffer.lines if ln.kind is LineKind.MISSING)
    assert line.expected is expected
    assert line.reason == "device did not answer"
    assert "2.05" in line.text and "/p/1" in line.text


# -- external sequences complete before the outer one resumes ---------------

def test_preparation_block_is_contiguous():
    """A called preparation must be fully rendered before the outer
    sequence's next line — interleaved blocks would make it impossible to
    tell whose telegram is whose.

    Guaranteed by the runner: `self.run(target)` for an @#"..." reference
    is a blocking call, so its events are emitted in full before the outer
    loop advances. This pins the resulting SHAPE.
    """
    buffer = _run_with_preparation()

    kinds = [(ln.kind, ln.external) for ln in buffer.lines
             if ln.kind in (LineKind.TELEGRAM, LineKind.STRUCTURE)]
    externals = [i for i, (_k, ext) in enumerate(kinds) if ext]
    # Every external line sits in ONE unbroken run.
    assert externals == list(range(externals[0], externals[-1] + 1))


def test_outer_numbering_resumes_after_the_preparation():
    """The outer counter is untouched by the preparation — it continues
    from where it paused rather than restarting or absorbing the inner
    telegrams."""
    lines = _telegrams(_run_with_preparation())
    outer = [ln.number for ln in lines if not ln.external]
    assert outer == [1, 2, 3]


# -- comments --------------------------------------------------------------

def test_narrative_comments_reach_the_buffer():
    """`@[t` is literally "Append to trace buffer" in EITT's own command
    scheme, so those are exactly the comments that belong here."""
    buffer = TraceBuffer()
    buffer.on_step(StepEvent(index=0, kind="Comment", ok=True,
                             summary="@[t5.2.1.1 Reading API version"))
    assert [ln.text for ln in buffer.lines] == ["5.2.1.1 Reading API version"]


def test_provisioning_commands_do_not_reach_the_buffer():
    """`@@[pahctx"..."` configures the tool; it is not narrative and EITT
    does not log it as such."""
    buffer = TraceBuffer()
    buffer.on_step(StepEvent(index=0, kind="Comment", ok=True,
                             summary='@@[pahctx"TokenTest;..."'))
    buffer.on_step(StepEvent(index=1, kind="CoAPMessage", ok=True,
                             summary="IN GET /p/1"))
    assert buffer.lines == []


# -- line format -----------------------------------------------------------

def test_tx_and_rx_use_eitts_arrows():
    assert format_telegram(_telegram(direction=TX)).startswith("<")
    assert format_telegram(_telegram(direction=RX)).startswith(">")


def test_response_code_carries_its_name():
    text = format_telegram(_telegram(direction=RX, code="2.05", msg_type=2))
    assert "ACK: 2.05 [Content]" in text


def test_payload_is_rendered_as_data():
    text = format_telegram(_telegram(direction=RX, code="2.05", msg_type=2,
                                     body={"value": 1}))
    assert "[Data:]" in text
    assert "'value': 1" in text


def test_empty_body_has_no_data_section():
    assert "[Data:]" not in format_telegram(_telegram(body=None))
    assert "[Data:]" not in format_telegram(_telegram(body={}))


@pytest.mark.parametrize("msg_type,name", [(0, "CON"), (1, "NON"),
                                           (2, "ACK"), (3, "RST")])
def test_message_types_are_named(msg_type, name):
    assert f"{name}:" in format_telegram(_telegram(msg_type=msg_type))


def test_clear_resets_numbering():
    buffer = _run_with_preparation()
    assert buffer.lines
    buffer.clear()
    assert buffer.lines == []
    buffer.on_case(CaseEvent(name="again", phase="start"))
    buffer.on_telegram(_telegram())
    assert _telegrams(buffer)[0].number == 1
