"""
The sequence player: walks one TestCase's Sequence and drives
coap_client.CoapClient / knx_oscore.OscoreContext accordingly.

This is the piece that ties every other eitt module together:
  - model: the parsed TestCase.sequence (Comment/CoAPMessage/DnsMessage).
  - variables: #VAR substitution + the shared variable table.
  - provisioning: @@[pah...] comment side effects (devices, multicast
    groups, OSCORE contexts, toggles).
  - content_match: expected-response validation.
  - hooks: EvalFn/EvalResponseFn/PatchXxxFn callbacks.
  - resource_schema: JSON<->CBOR translation per resource path.
  - overrides: manual-interaction substitution / skip / xfail.

What this module deliberately does NOT attempt (documented limitations,
not silent gaps — each raises or skips loudly rather than guessing):
  - DnsMessage playback (mDNS discovery) — the existing hand-written
    test_5_1_discovery_mdns.py already covers this well; wiring EITT's
    DnsMessage sequence items to zeroconf is future work.
  - CheckRxKid and SeqNoFn="asinrequest" outside the 4.01-challenge step:
    their general semantics are still unconfirmed, so they are accepted
    but not acted on. (SeqNoFn/SeqNoOffset/InvalidAuthTag/RndKid ARE
    implemented — see _resolve_oscore_overrides and
    knx_oscore.protect_request/protect_echo_challenge.)
  - The "@#"TestCaseName"" inline test-case reference and "@>w"...;
    unlimited"" loop-invocation comment forms are handled by
    PreparationRegistry / LoopRunner below, not inline in run_sequence.
"""

from __future__ import annotations

import json
import os
import re
import time
from contextlib import contextmanager
from dataclasses import dataclass, field
from typing import Callable, Optional

from transport.coap_client import ACK, CON, NON, RST
from .content_match import match_content
from .hooks import HookContext, get_hook
from .model import CoAPMessage, Comment, DnsMessage, EittProject, TestCase
from .provisioning import ProvisioningEngine, parse_comment_command
from .resource_schema import AT_ENTRY_SCHEMA, get_schema
from .spake_flow import (SPAKE_PATH, SpakeStepFailure, is_spake_message,
                        run_spake_handshake)
from .variables import VariableTable

CBOR_CONTENT_FORMATS = {"application/cbor", "60"}
LINK_FORMAT_CONTENT_FORMATS = {"application/link-format", "40"}

_DEBUG_GROUP = os.environ.get("KNX_TEST_DEBUG_GROUP") == "1"


def _dbg_group(*args, **kwargs):
    """Print a [group] debug line only when KNX_TEST_DEBUG_GROUP=1 —
    multicast group listener arming/capture, and sensor triggering."""
    if _DEBUG_GROUP:
        print(*args, **kwargs)

_INLINE_CASE_RE = re.compile(r'^@#"([^"]+)"')

# `@[w"<time>"` (toolbar countdown) / `@@[w"<time>"` (modal dialog the
# operator dismisses). Group 1 captures which form, group 2 the timespan.
_WAIT_RE = re.compile(r'^(@@?)\[w"([^"]*)"')

# How long to pause for an operator-dismissed `@@[w` modal. The human
# clicks OK after a couple of seconds; what they are really waiting on is
# the device finishing its work (runtime_test_server simulates the
# firmware upgrade with a 2s delayed callback), so give it a small margin
# over that rather than the declared ceiling.
_MODAL_WAIT_SECONDS = 3.0

# How long to let the device settle after a `@@!`-driven control call
# (/test/restart re-inits the stack, /test/factory-reset erases storage).
# The prompt's own "once the device is ready" is what the operator's click
# was really waiting on.
_MANUAL_ACTION_SETTLE_SECONDS = 3.0

# How long an mDNS step waits for the DUT's answer. Deliberately NOT
# scaled by RUNTIME_TEST_TIME_SCALE: this is a real network wait for a
# real responder, not one of the XML's pacing delays.
#
# An mDNS responder deliberately delays its reply to avoid collisions
# (RFC 6762 §6 — up to 500ms for a shared record, and this device was
# measured answering ~2.6s after the query). 3s was not enough: the
# listener closed just before the answer landed and every step reported
# "received nothing" while the device log showed a perfectly good
# send_query_response.
_MDNS_TIMEOUT = 6.0

# How long to wait for a DUT-originated UNICAST s-mode telegram. Deliberately
# larger than the scripted TimeToNext — see _expect_unicast_telegram.
_UNICAST_TELEGRAM_TIMEOUT = 20.0

# How often to re-send an mDNS query while the listener is open (see
# _run_dns_message for why one shot is not enough).
_MDNS_RETRY_INTERVAL = 0.75


def _safe_int(value) -> int:
    try:
        return int(str(value).strip())
    except (TypeError, ValueError):
        return 0
_LOOP_INVOKE_RE = re.compile(r'^@>w"([^;"]+);([^"]*)"')

# `@@!<text>` — EITT's "display comment in toolbar, suspend evaluation and
# beep" (a modal the operator dismisses). The single-`@` form `@!<text>` is
# informational only ("display comment in toolbar and beep"), so it is NOT
# matched here and stays a no-op.
#
# Bare `@@<text>` is the SAME "display and suspend evaluation" modal, minus
# the beep, so it routes through the identical manual_actions mechanism.
# `!` is optional here; `[` is excluded because `@@[<cmd>` is the
# provisioning-command form, not an operator prompt. (Both are also ordered
# ahead of this in _run_comment — the exclusion keeps the pattern correct on
# its own rather than relying on match order.)
_MANUAL_PROMPT_RE = re.compile(r'^@@(?!\[)!?\s*(.+)$', re.S)

# Display-only comment commands: shown to the operator, no side effect.
#   @[t<text>   narrative text (2463 uses)
#   @[<Text>    the same with the 't' omitted by the author — 18 uses, all
#               plain prose ("Preparation: ...", "Recipient Multicast GRPID
#               (id=5) ..."), several appearing verbatim elsewhere WITH the
#               't'. Matched as "@[ + a letter", which cannot collide with
#               the real @[ subcommand `@[w"..."` because that is claimed by
#               _WAIT_RE first and always carries its quoted timespan.
#   @!<text>    display + beep, informational only
# Anything else starting with '@' is treated as an unimplemented command and
# fails the step rather than passing silently — see F7/F8.
_DISPLAY_ONLY_RE = re.compile(r'^(@\[[a-zA-Z]|@!)', re.S)


class UnsupportedFeature(Exception):
    """Raised when a sequence item uses XML semantics this interpreter
    doesn't implement yet — deliberately loud, never a silent no-op."""


@dataclass
class StepResult:
    ok: bool
    reason: str = ""
    item: object = None


@dataclass
class SequenceRunResult:
    ok: bool
    steps: list[StepResult] = field(default_factory=list)
    failure: Optional[StepResult] = None


def _response_as_json_text(payload: bytes, path: str,
                           content_format: str = "application/cbor") -> str:
    """Render a response payload as the JSON text an EvalFn/EvalResponseFn
    hook expects (the ported hooks all do `json.loads(response)` and
    then dict-index into it, exactly like the original EITT script —
    EITT itself hands hooks already-humanized JSON text, never raw
    CBOR bytes). Byte-string field values (e.g. an AT entry's raw
    'ms'/'id') aren't JSON-serializable, so they're hex-encoded as
    "h'<hex>'" strings — the same convention the XML's own <Content>
    text uses (see resource_schema.expand_hex_literals) — rather than
    crashing json.dumps() on a bytes value no hook in this XML actually
    needs to read.
    """
    decoded = _decode_body(payload, content_format, path)
    if decoded is None:
        return ""

    def _default(obj):
        if isinstance(obj, bytes):
            return f"h'{obj.hex()}'"
        raise TypeError(f"Object of type {type(obj)} is not JSON serializable")

    return json.dumps(decoded, default=_default)


_CF_CODES = {
    "text/plain": 0,
    "application/link-format": 40,
    "application/octet-stream": 42,
    "application/json": 50,
    "application/cbor": 60,
}


def _content_format_code(cf: Optional[str]) -> Optional[int]:
    """Map a Content-Format/AcceptedCF name to its CoAP numeric code.

    EITT's `AcceptedCF` becomes the request's CoAP Accept option (17). It
    is set on 1469 of the template's messages and deliberately absent on
    the rest, so it is not decorative: a device may legitimately choose a
    different representation without it.

    Accepts an already-numeric string too ("60"), since the trace files
    and some Field defaults use the code rather than the name.
    """
    if not cf:
        return None
    key = cf.strip().lower()
    if key in _CF_CODES:
        return _CF_CODES[key]
    try:
        return int(key)
    except ValueError:
        return None


def _response_code_tuple(code: Optional[str]) -> Optional[tuple]:
    """Parse a CoAP response code string ("2.05") into (class, detail)."""
    m = re.match(r"^\s*(\d)\.(\d{1,2})\s*$", code or "")
    if not m:
        return None
    return int(m.group(1)), int(m.group(2))


def _expected_response_codes(code: Optional[str]) -> list[tuple]:
    """Every response code an `OUT` step accepts, as (class, detail) pairs.

    EITT's `Code` attribute on a response row is usually a single code
    ("2.05"), but 36 active rows in this template list SEVERAL, comma
    separated — "4.00,4.04", "4.01,4.05", "4.00,4.01,4.03,4.05". That is
    the template author saying "any of these is acceptable here", typically
    because the spec permits more than one rejection code. Each alternative
    is parsed independently; anything unparseable is dropped, and a row that
    yields NO parseable code means "do not assert the code" (that is how a
    method-named `Code` on a DUT-originated telegram, e.g. "POST", behaves).
    """
    return [t for t in (_response_code_tuple(part)
                        for part in (code or "").split(","))
            if t is not None]


_MSG_TYPE_NAMES = {CON: "CON", NON: "NON", ACK: "ACK", RST: "RST"}

# EITT's `Type` spelled back to the CoAP wire value, for asserting what the
# DUT actually sent on an OUT row. Case-insensitive on purpose: the template
# contains one "ACk" typo, and _coap_msg_type already absorbs it on the send
# side. Anything unrecognised yields None = "do not assert the type", so a
# future spelling degrades to today's behaviour rather than inventing a
# failure.
_MSG_TYPE_BY_NAME = {"con": CON, "non": NON, "ack": ACK, "rst": RST}


def _coap_msg_type_expected(type_str: Optional[str]) -> Optional[int]:
    """The CoAP message type an OUT row demands, or None if unspecified."""
    return _MSG_TYPE_BY_NAME.get((type_str or "").strip().lower())


def _format_codes(codes: list) -> str:
    """Render expected (class, detail) pairs the way EITT words it, e.g.
    "4.00" or "4.00,4.04"."""
    return ",".join(f"{c}.{d:02d}" for c, d in codes)


def _is_method_code(code: Optional[str]) -> bool:
    """True when `Code` names a METHOD (GET/POST/PUT/DELETE) rather than a
    response code. On an `OUT` step that marks a telegram the DUT ORIGINATES
    instead of a reply to match against the last request's response."""
    return (code or "").strip().upper() in ("GET", "POST", "PUT", "DELETE")


def _coap_msg_type(type_str: Optional[str]) -> int:
    """Map EITT's `Type` attribute to a CoAP message type.

    CON (confirmable) vs NON (non-confirmable) is a real wire difference —
    a CON obliges the peer to ACK — so it is honoured per telegram rather
    than assumed. The template uses CON:1322 / NON:365 / ACK:1324 on
    active messages (ACK values sit on OUT/response rows, which describe
    what we RECEIVE and so never reach this function), plus one 'ACk'
    typo that the case-insensitive compare below absorbs.
    """
    t = (type_str or "").strip().upper()
    if t == "NON":
        return NON
    return CON


_MC_SCOPE_ALIASES = {
    "multicast link-local": 2,
    "multicast realm-local": 3,
    "multicast site-local": 5,
}


def _plain_multicast_scope(ip: Optional[str]) -> Optional[int]:
    """The IPv6 multicast scope an `IP="..."` attribute names, or None when
    it is an ordinary (unicast) device alias.

    EITT's device list contains three well-known scope entries —
    "Multicast Link-Local"/"Realm-Local"/"Site-Local" -> ff02/ff03/ff05::fd
    (the same table `_group_address_for` documents). A request addressed to
    one of them is a REAL multicast, and 5.1.1.1 "Multicast Discovery via
    /.well-known/core" is precisely the test that it works: the DUT answers
    a unicast GET too, so unicasting this step would pass while never
    exercising the multicast path the TestCase is named after.
    """
    return _MC_SCOPE_ALIASES.get((ip or "").strip().lower())


def _resolve_crit_option(msg) -> Optional[tuple]:
    """Build the (number, value) outer CoAP option for EITT's
    CritOptNo/CritOptValue pair, or None.

    Used by 5.10.5.3 ("If unknown Critical Option is used then Bad Option
    response is returned"): a GET carrying unknown CRITICAL option 65123
    with value 'ABCD' must be answered 4.02 (RFC 7252 §5.4.1). Without
    this the request goes out clean and the device answers 2.05, so the
    test would fail for the wrong reason.
    """
    if not msg.crit_opt_no:
        return None
    try:
        num = int(str(msg.crit_opt_no).strip())
    except ValueError:
        return None
    raw = (msg.crit_opt_value or "").strip()
    try:
        val = bytes.fromhex(raw.replace(" ", "")) if raw else b""
    except ValueError:
        val = raw.encode()
    return num, val


def _decode_body(payload: bytes, content_format: Optional[str], path: str):
    """Best-effort decode of a response payload into a Python value for
    Content matching, given the message's declared/expected CF."""
    if not payload:
        return None
    # Imported up here, not inside the branch, so CBORDecodeError is bound
    # for the except clause on every path through this function.
    import cbor2

    cf = (content_format or "").lower()
    try:
        if cf in LINK_FORMAT_CONTENT_FORMATS:
            return payload.decode("utf-8", errors="replace")
        if cf in CBOR_CONTENT_FORMATS or not cf:
            decoded = cbor2.loads(payload)
            # get_schema() never raises: it falls back to _DEFAULT_SCHEMA
            # for an unmapped path, mirroring EITT's own Default="yes"
            # key-map table. (There used to be an `except KeyError` here
            # documenting a raise that does not happen — dead code.)
            return get_schema(path).decode(decoded)
        return payload.decode("utf-8", errors="replace")
    except (cbor2.CBORDecodeError, UnicodeDecodeError, ValueError):
        # A body we genuinely cannot decode -> None, which Content matching
        # reports as a mismatch. Deliberately NARROW: a TypeError or
        # AttributeError from a bug inside resource_schema.decode() must NOT
        # be laundered into "the device sent something undecodable" — that
        # is precisely how a real defect would hide as a content mismatch.
        return None


class PreparationRegistry:
    """Resolves the "@#"TestCaseName"" inline-reference and
    "@>w"TestCaseName;unlimited"" loop-invocation comment forms to the
    actual TestCase objects they name, so the runner can inline/repeat
    them without the caller having to pre-flatten the document.

    EVERY "@#"..."" reference in the real v40 XML actually reads
    '@#"Copy of AUTH PREPARATION"' / '@#"Copy of IA PREPARATION"' /
    '@#"Copy of TABLE PREPARATION"' — the "Copy of " prefix reflects
    the real EITT workflow (per the XML's own CreationMessage: "the
    sequences 'AUTH PREPARATION', 'IA PREPARATION' and 'TABLE
    PREPARATION' must be generated before you run any tests", i.e. a
    human/tool duplicates the template TestCase and the duplicate gets
    that prefix). This template XML has no such duplicate TestCase to
    literally match, since it's the template, not an instantiated
    project — so resolve() strips a leading "Copy of " and retries
    against the literal template names as a fallback.
    """

    _COPY_PREFIX = "Copy of "

    def __init__(self, project: EittProject):
        self._by_name: dict[str, TestCase] = {}
        for _coll, _suite, tc in project.iter_test_cases():
            # Multiple TestCases can legitimately share a Name across
            # different collections (e.g. per-suite "Copy of ..." case
            # instances the tester duplicates from a template) — keep
            # the FIRST one found; a runner that needs a specific
            # collection's instance should look it up via
            # EittProject.iter_test_cases() directly instead.
            self._by_name.setdefault(tc.name, tc)

    def resolve(self, name: str) -> Optional[TestCase]:
        found = self._by_name.get(name)
        if found is not None:
            return found
        if name.startswith(self._COPY_PREFIX):
            return self._by_name.get(name[len(self._COPY_PREFIX):])
        return None


class SequenceRunner:
    """Runs one TestCase's Sequence against a live DUT."""

    def __init__(self, coap, project: EittProject, variables: VariableTable,
                provisioning: ProvisioningEngine,
                preparations: PreparationRegistry,
                hook_ctx: Optional[HookContext] = None,
                sleep: Callable[[float], None] = time.sleep,
                time_scale: Optional[float] = None,
                overrides=None):
        self.coap = coap
        self.project = project
        self.variables = variables
        self.provisioning = provisioning
        self.preparations = preparations
        # Needed for `@@!` operator prompts, which are mapped to DUT control
        # endpoints by TEXT (see _run_manual_prompt). An empty table means
        # every `@@!` fails loudly rather than passing silently.
        if overrides is None:
            from .overrides import OverrideTable
            overrides = OverrideTable({})
        self.overrides = overrides
        self.hook_ctx = hook_ctx or HookContext(variables=variables)
        self.sleep = sleep
        # TimeToNext values in the XML are tuned for pacing against real
        # KNX hardware over a real network; over the Docker/veth loopback
        # the device answers in microseconds and needs none of it. Scale
        # every inter-telegram wait by RUNTIME_TEST_TIME_SCALE (default
        # 1.0 = faithful) so the harness can run the same sequences ~20x
        # faster without editing the (spec-faithful) XML. See
        # run-in-docker.ps1, which sets it to a small value by default.
        if time_scale is None:
            try:
                time_scale = float(
                    os.environ.get("RUNTIME_TEST_TIME_SCALE", "1.0"))
            except ValueError:
                time_scale = 1.0
        self.time_scale = max(0.0, time_scale)
        self._last_response: Optional[bytes] = None
        # (class, detail) of the last response, e.g. (2, 5) — or None when
        # nothing was received at all. Kept SEPARATE from the payload
        # because "timed out" and "answered with an empty body" are very
        # different things and an `OUT` step must be able to tell them
        # apart: a dead device otherwise satisfies every step whose
        # expected <Content> is empty.
        self._last_response_code: Optional[tuple] = None
        # CoAP message type of the last response (CON/NON/ACK/RST as the
        # raw 2-bit wire value), or None when nothing was received. Kept
        # for the same reason as the code: on the OSCORE path the outer
        # code is ALWAYS 2.04, so type is one of only two outer-layer
        # facts an OUT step can still assert.
        self._last_response_type: Optional[int] = None
        # Whether the last REQUEST was sent OSCORE-protected. Recorded for
        # a future `Secure` assertion (F6) but NOT yet sufficient for one:
        # `Secure` on an OUT row describes the RESPONSE, and the two differ
        # exactly where it matters. See the F6 entry in
        # tests/docs/eitt-interpreter-findings.md.
        self._last_request_secured: Optional[bool] = None
        self._last_planned: Optional[str] = None
        self._last_request_path: str = ""
        # Most recent CoAP Echo (option 252) challenge value the DUT sent
        # back, captured so a later Echo="sync" telegram can echo it (the
        # group-message replay-synchronisation flow, EITT 5.3.17.3/.4/.5).
        self._last_echo: Optional[bytes] = None
        # Listener armed before a request that provokes a DUT telegram on a
        # multicast group address, consumed by the following OUT step.
        self._group_capture = None
        # True while sending a request whose 4.01 Echo challenge the script
        # handles explicitly (so the client must not auto-retry it).
        self._expect_echo_challenge: bool = False
        # The last group telegram the DUT sent us, kept so a following
        # tool-sent 4.01 challenge can quote its kid/PIV/token.
        self._last_group_telegram = None
        # Listener armed for a request the DUT sends TO US, and the request
        # once captured — a following UseLastRxToken step answers it.
        self._dut_request_capture = None
        self._last_rx_request = None
        # Read armed BEFORE the discovery answer that provokes a
        # DUT-originated unicast telegram, consumed by the following OUT step
        # (see _arm_unicast_for_next).
        self._unicast_capture = None
        # The sequence currently being run, so a step can look ahead at what
        # the next one expects.
        self._sequence: list = []
        # Source address of the most recent multicast-discovery reply
        # (`GET /.well-known/core` to a scope alias). Kept as the CoAP
        # counterpart to the mDNS-resolved address, and as evidence the
        # discovery round-trip really happened.
        self._last_discovered_addr = None
        # Records from the DUT's most recent mDNS answer. One query draws
        # the device's whole record set, and the OUT steps that follow
        # consume from this cache rather than each querying again.
        self._dns_records: list = []
        # Listener armed before a request whose side effect is an
        # unsolicited mDNS announcement (see _arm_unsolicited_dns).
        self._dns_listener = None
        # The service name of the most recent mDNS query, for the
        # rqcheck comparison (see _match_dns_record).
        self._dns_query_name = ""
        # Set by run() when the current OUT step is a DUT-originated request
        # (an `OUT <method>` whose following step answers with
        # UseLastRxToken) rather than a reply we are matching.
        self._pending_dut_request: bool = False
        # href of the group object written with the TRANSMISSION cflag —
        # the datapoint /test/trigger must poke to make the DUT originate
        # a group telegram (see _trigger_sensor).
        self._transmit_href: Optional[str] = None

    # -- top-level entry point -------------------------------------------

    def run(self, test_case: TestCase) -> SequenceRunResult:
        # `@@[pahsnc"off"` regions are scoped WITHIN a TestCase (every one
        # in the template is closed by a matching `"on"`), but a failing
        # step returns early and would leak the "off" into every later
        # TestCase in the collection — which shares this engine — silently
        # disabling automatic Echo-retry for all of them. Re-assert the
        # default here so one failure cannot corrupt its successors.
        self.provisioning.seq_no_check_enabled = True
        # run() RECURSES: an @#"..." inline preparation reference calls it
        # again with the preparation's own sequence. Save and restore, or the
        # outer TestCase's steps would look themselves up in whichever
        # preparation ran last and never find themselves — which silently
        # disabled the unicast arming (see _arm_unicast_for_next).
        outer_sequence = self._sequence
        outer_unicast = self._unicast_capture

        # Drop the cached mDNS answer at the TestCase boundary.
        #
        # `_dns_records` is filled by an IN query step and read by the
        # following OUT step. It was cleared ONLY inside
        # _arm_unsolicited_dns, so a set cached in TestCase N survived into
        # N+1 — and the unsolicited branch (`if not self._dns_records`)
        # then never listened at all, matching an announcement step out of
        # the STALE answer. That is a pass without the device having
        # announced anything. `_dns_query_name` goes with it, or the
        # rqcheck pairing compares against a query from an earlier
        # TestCase. See finding M3.
        #
        # Only at the OUTER level: run() recurses for an @#"..." inline
        # preparation, and clearing on the inner call would discard an
        # answer the enclosing TestCase legitimately cached before it.
        # `_sequence` starts as [] and is non-empty only while a TestCase is
        # already running, so falsiness is the outermost-call test.
        if not outer_sequence:
            self._dns_records = []
            self._dns_query_name = ""

        self._sequence = test_case.sequence
        self._unicast_capture = None
        try:
            return self._run_sequence(test_case)
        finally:
            self._sequence = outer_sequence
            self._unicast_capture = outer_unicast

    def _run_sequence(self, test_case: TestCase) -> SequenceRunResult:
        steps: list[StepResult] = []
        sequence = test_case.sequence
        i = 0
        while i < len(sequence):
            item = sequence[i]

            if (isinstance(item, CoAPMessage) and item.activate
                    and is_spake_message(item.path) and item.is_request
                    and self._is_real_spake_exchange(sequence, i)):
                consumed, result = self._run_spake_exchange(sequence, i)
                steps.append(result)
                if not result.ok:
                    return SequenceRunResult(ok=False, steps=steps,
                                            failure=result)
                i += consumed
                continue

            # Both the group listener and the "don't auto-answer the Echo
            # challenge" flag depend on what the NEXT step expects, and
            # both have to be set before the request goes out.
            self._prepare_for_request(sequence, i)

            # An `OUT <method>` whose next active step answers with
            # UseLastRxToken is the DUT querying US, not a reply to match.
            self._pending_dut_request = (
                isinstance(item, CoAPMessage) and item.activate
                and item.is_response and bool(item.code)
                and _response_code_tuple(item.code) is None
                and self._answers_with_last_rx_token(sequence, i))

            result = self._run_item(item)

            # EITT "alternative response": a DEACTIVATED (Activate="no")
            # OUT message immediately following an active OUT expectation
            # is an accepted alternative shape for the same response —
            # e.g. 5.3.4.1's primary expectation demands /auth/at
            # advertise dual ct="60 40", while its Activate="no" sibling
            # accepts plain ct=40; the recorded certification run
            # (traces/5_3.xml TelNo 48/50, EvalResult "OK") passed a
            # device that sends single ct=40, proving EITT falls back to
            # the alternative rather than failing. Mirror that: on a
            # failed active OUT match, try each directly-following
            # inactive OUT message before giving up.
            if (not result.ok and isinstance(item, CoAPMessage)
                    and item.activate and item.is_response):
                j = i + 1
                while j < len(sequence):
                    alt = sequence[j]
                    if (isinstance(alt, CoAPMessage) and alt.is_response
                            and not alt.activate):
                        alt_result = self._expect_response(alt)
                        if alt_result.ok:
                            result = alt_result
                            break
                        j += 1
                        continue
                    break

            steps.append(result)
            if not result.ok:
                return SequenceRunResult(ok=False, steps=steps, failure=result)
            i += 1
        return SequenceRunResult(ok=True, steps=steps)

    def _is_real_spake_exchange(self, sequence: list, start: int) -> bool:
        """True when the spake POST at `sequence[start]` is genuinely the
        start of a multi-step handshake that needs the real crypto client
        (spake_flow.py) — as opposed to an isolated single-shot probe
        (e.g. "SPAKE2+ across device reset types" deliberately POSTing a
        bare step-1 request while the device is NOT in default config
        state, with a fixed/literal rnd and no <TOOL.*> placeholder
        anywhere) that the generic per-message _send_request/
        _expect_response path already handles correctly on its own.

        The distinguishing signal: a real handshake's OWN immediately-
        following OUT response is a success (2.xx) — only then does EITT
        actually continue to steps 2/3, which is what the XML's <Content>
        placeholders (<TOOL.SHARE_P>/<TOOL.CONFIRM_P>) presuppose. An
        isolated probe's OUT response is the error code being tested
        (4.00/etc.), so there is no step 2/3 to drive at all — this
        message should just be sent and matched like any other."""
        j = start + 1
        while j < len(sequence) and isinstance(sequence[j], Comment):
            j += 1
        if j >= len(sequence) or not isinstance(sequence[j], CoAPMessage):
            return False
        response = sequence[j]
        if not (response.activate and response.is_response):
            return False
        return response.code.startswith("2.")

    def _run_spake_exchange(self, sequence: list, start: int
                            ) -> tuple[int, StepResult]:
        """Consume the full SPAKE2+ 3-request/response exchange starting
        at `sequence[start]`, driving it through the real crypto client
        (spake_flow.py) instead of per-message dispatch — the XML's own
        <Content> for these messages is illustrative only, holding
        EITT-computed placeholders like <TOOL.SHARE_P> that nothing but
        EITT's own PAKE math can fill in.

        Comments interleaved between the 3 request/response pairs (e.g.
        the real AUTH PREPARATION's '@@[pahctx"Temporary Key;
        senderid=TmpTok"' between pair 2 and pair 3) are applied via the
        normal comment path AS ENCOUNTERED, in their original relative
        order, but the handshake itself only actually runs once all 3
        pairs have been located — a pahctx naming this exchange's
        sender_id and no mastersecret is finalized retroactively via
        ProvisioningEngine.pending_spake_result once the shared key is
        known (see provisioning.py's _apply_pahctx).

        Returns (number_of_sequence_items_consumed, StepResult).
        """
        i = start
        request_count = 0
        first_req: Optional[CoAPMessage] = None
        requests: list[CoAPMessage] = []
        responses: list[CoAPMessage] = []
        deferred_comments: list[Comment] = []

        # Stop once the 3rd request's OWN response has been collected too
        # (len(responses) >= request_count), not the instant the 3rd
        # request itself is seen — otherwise the loop exits one item too
        # early and leaves step 3's "OUT" pairing message uncollected,
        # which _run_spake_exchange needs below to match a deliberately-
        # wrong confirmP's rejection response against.
        while i < len(sequence) and (
                request_count < 3 or len(responses) < request_count):
            item = sequence[i]
            if isinstance(item, Comment):
                deferred_comments.append(item)
                i += 1
                continue
            if isinstance(item, CoAPMessage) and item.activate:
                if item.is_request:
                    if first_req is None:
                        first_req = item
                    requests.append(item)
                    request_count += 1
                    i += 1
                    continue
                if item.is_response:
                    responses.append(item)
                    i += 1  # the OUT pairing message — nothing to send here
                    continue
            i += 1  # inactive/unexpected item — skip past it

        if first_req is None:
            return i - start, StepResult(
                ok=False, item=sequence[start],
                reason="SPAKE2+ exchange starting here has no request "
                "messages to drive")

        sender_id = "TmpTok"
        # The password to authenticate with is whatever the most recent
        # '@@[pahdev"<alias>;password=..."' command set for the device
        # alias this request targets (e.g. a deliberately wrong password
        # for a "SPAKE2+ fails with incorrect password" TestCase) — NOT
        # always the device's real password. Fall back to the global
        # BDUT_DEVICE_PASSWORD field only when no such override exists
        # (the common case: most TestCases never call pahdev with an
        # explicit password and just want the real one).
        device = self.provisioning.resolve_device(first_req.ip)
        password = (device.password if device is not None else None)
        if password is None:
            password = self.variables.get("BDUT_DEVICE_PASSWORD")
        if first_req.content:
            try:
                content = json.loads(self.variables.substitute(first_req.content))
                sender_id = content.get("id", sender_id)
            except ValueError:
                pass
        if password is None:
            return i - start, StepResult(
                ok=False, item=first_req,
                reason="BDUT_DEVICE_PASSWORD not set in variable table — "
                "cannot run SPAKE2+ handshake")

        # The step-3 request's <Content> normally holds EITT's own
        # "<TOOL.CONFIRM_P>" placeholder, meaning "compute the real
        # confirmP" — but a "sending an incorrect confirmP fails"
        # TestCase instead writes a literal (fixed garbage, or #FIELD)
        # hex value there, meaning "send exactly this wrong bytestring
        # instead of the mathematically correct one". Detect that case
        # generically by resolving the field and checking whether the
        # placeholder is absent, rather than special-casing this one
        # TestCase's name/ID.
        bad_confirm_p: Optional[bytes] = None
        if request_count >= 3 and requests[2].content:
            step3_content = self.variables.substitute(requests[2].content)
            if "<TOOL.CONFIRM_P>" not in step3_content:
                try:
                    parsed = json.loads(step3_content)
                    raw = parsed.get("confirmP", "")
                    m = re.match(r"^h'([0-9A-Fa-f ]+)'$", raw.strip())
                    if m:
                        bad_confirm_p = bytes.fromhex(
                            m.group(1).replace(" ", ""))
                except (ValueError, AttributeError):
                    pass

        try:
            # Only drive as many steps as this TestCase actually
            # Activate="yes"s (request_count) — e.g. a wrong-password
            # TestCase activates just steps 1-2 and never wants a real
            # confirmP sent, so it must not run to completion.
            spake_result = run_spake_handshake(
                self.coap, password, sender_id, max_steps=request_count,
                override_confirm_p=bad_confirm_p)
        except AssertionError as e:
            return i - start, StepResult(ok=False, item=first_req, reason=str(e))

        # A step came back non-2.xx (e.g. "SPAKE2+ across device reset
        # types" deliberately POSTs step 1 while the device isn't in
        # default config state, or a "sending an incorrect confirmP
        # fails" TestCase's step 3 was rejected) — there is no shared
        # key, only the device's raw response. Match it against the
        # XML's own expected-response CoAPMessage for THIS step (its
        # "OUT" pairing message) exactly like any other request/response
        # pair, via the same _expect_response path every non-SPAKE2+
        # message uses, instead of assuming the exchange must succeed.
        if isinstance(spake_result, SpakeStepFailure):
            resp = spake_result.response
            step_idx = spake_result.step - 1
            self._remember_response(resp)
            self._last_planned = requests[step_idx].content
            self._last_request_path = SPAKE_PATH
            expect_result = self._expect_response(responses[step_idx])
            if not expect_result.ok:
                return i - start, expect_result
            for comment in deferred_comments:
                comment_result = self._run_comment(comment)
                if not comment_result.ok:
                    return i - start, comment_result
            return i - start, StepResult(ok=True, item=first_req)

        # Now that the shared key is known (a full 3-step exchange only —
        # a partial 1-2 step run has no shared key and nothing for a
        # trailing pahctx to consume), apply every comment that was
        # interleaved between the request/response pairs, in original
        # order — this is what lets a pahctx mid-exchange (as in the
        # real AUTH PREPARATION) resolve against pending_spake_result.
        if spake_result is not None:
            self.provisioning.pending_spake_result = spake_result
        for comment in deferred_comments:
            comment_result = self._run_comment(comment)
            if not comment_result.ok:
                return i - start, comment_result
        # A pahctx AFTER the exchange (not just mid-exchange) should
        # still be able to consume the result, so leave it set — the
        # one-shot consumption happens inside _apply_pahctx itself.

        return i - start, StepResult(ok=True, item=first_req)

    # -- per-item dispatch -------------------------------------------------

    def _run_item(self, item) -> StepResult:
        if isinstance(item, Comment):
            return self._run_comment(item)
        if isinstance(item, CoAPMessage):
            return self._run_coap_message(item)
        if isinstance(item, DnsMessage):
            return self._run_dns_message(item)
        return StepResult(ok=False, item=item,
                          reason=f"Unknown sequence item type {type(item)}")

    def _run_comment(self, comment: Comment) -> StepResult:
        if not comment.activate:
            return StepResult(ok=True, item=comment)
        text = comment.text

        # Wait commands, two forms with DIFFERENT semantics — check before
        # the generic "@@[" provisioning branch, which would otherwise
        # swallow @@[w:
        #
        #   @[w"<time>"   toolbar countdown. An unattended delay: EITT
        #                 really does sit out the full timespan, so we
        #                 sleep it (scaled by RUNTIME_TEST_TIME_SCALE like
        #                 every other wait).
        #   @@[w"<time>"  MODAL DIALOG. The operator clicks OK to continue,
        #                 typically after a couple of seconds; the declared
        #                 time is only the upper bound before it closes
        #                 itself. Sleeping the full value here would be
        #                 wrong (5.2.9.1 declares 00:02:00 twice) — this is
        #                 a manual-interaction step, so we pause only long
        #                 enough for the device-side work to finish, which
        #                 is what the human's click is really waiting on.
        m = _WAIT_RE.match(text)
        if m:
            return self._run_wait_command(comment, m.group(1), m.group(2))

        if text.startswith("@@["):
            try:
                self.provisioning.apply(text)
            except KeyError as e:
                return StepResult(ok=False, item=comment, reason=str(e))
            return StepResult(ok=True, item=comment)

        # `@@!<text>` / bare `@@<text>` — "display comment in toolbar,
        # SUSPEND EVALUATION" (`!` only adds a beep): a modal the operator
        # dismisses after doing something PHYSICAL. Checked before the
        # plain-comment fall-through, which is where these used to land
        # silently — and after the `@@[` branch above, since that prefix is
        # a provisioning command rather than an operator prompt.
        m = _MANUAL_PROMPT_RE.match(text)
        if m:
            return self._run_manual_prompt(comment, m.group(1))

        m = _INLINE_CASE_RE.match(text)
        if m:
            return self._run_inline_reference(comment, m.group(1))

        m = _LOOP_INVOKE_RE.match(text)
        if m:
            return self._run_loop_invocation(comment, m.group(1), m.group(2))

        # Display-only forms: no side effect, always a no-op pass.
        #
        #   @[t<text>  narrative text in the toolbar (2463 uses)
        #   @[<Text>   the same thing with the 't' omitted by the author —
        #              18 uses, all plain English prose ("Preparation: ...",
        #              "Check that the device rest was successful ...",
        #              "Recipient Multicast GRPID (id=5) ..."), several of
        #              which appear verbatim elsewhere WITH the 't'.
        #   @!<text>   display + beep, informational only
        #
        # Anything else is an @-command this interpreter does not implement.
        # Passing it silently is how a behavioural command gets mistaken for
        # a no-op (see F7: bare `@@` sat in this fall-through and two real
        # 5.4.1.8 steps never ran), so fail loudly and name the text.
        if not text.startswith("@") or _DISPLAY_ONLY_RE.match(text):
            return StepResult(ok=True, item=comment)

        return StepResult(
            ok=False, item=comment,
            reason=f"unrecognised @-command {text[:60]!r} — this interpreter "
            f"implements @[t/@[<text>/@! (display only), @[w & @@[w (wait), "
            f"@@[ (provisioning), @@ & @@! (operator prompt), @# (inline "
            f"reference) and @>w (loop). Treating an unknown command as a "
            f"no-op would let the TestCase pass without performing it; look "
            f"the command up in EITT's comment-command scheme and implement "
            f"it, or extend _DISPLAY_ONLY_RE if it is display-only.")

    def _run_manual_prompt(self, comment: Comment,
                           prompt: str) -> StepResult:
        """Act on one `@@!<text>` operator prompt.

        The TEXT decides what to do — `@@!` alone only means "a human does
        something" — so the mapping lives in `overrides.yaml`'s
        `manual_actions:` list, matched by regex, first match wins.

        An UNMAPPED prompt fails the step. That is deliberate: these steps
        gate on a physical action, so quietly continuing would let the test
        "pass" having never exercised what it exists to test. 5.3.1.3 was
        doing exactly that — `@@!` was ignored outright, so its
        SPAKE2+-after-power-cycle step ran against a device still in the
        previous state and the two hardest reset types went untested.
        """
        prompt = prompt.strip()
        action = self.overrides.manual_action(prompt)
        if action is None:
            return StepResult(
                ok=False, item=comment,
                reason=f"unmapped @@! operator prompt {prompt!r} — this step "
                f"needs a physical action. Add a 'manual_actions:' entry in "
                f"eitt/overrides.yaml mapping it to a /test/... endpoint (or "
                f"to no endpoint if it is a human observation only).")
        if not action.endpoint:
            _dbg_group(f"[manual] {prompt!r} -> observation only "
                       f"({action.reason})")
            self._apply_wait(comment)
            return StepResult(ok=True, item=comment)

        ctx = self.provisioning.resolve_context("TokenTest")
        _dbg_group(f"[manual] {prompt!r} -> POST {action.endpoint} "
                   f"({action.reason})")
        try:
            import cbor2
            payload = cbor2.dumps(action.params) if action.params else b""
            if ctx is not None:
                self.coap.oscore_request(ctx, 2, action.endpoint,
                                         payload=payload, timeout=5.0)
            else:
                # The control endpoints are unsecured when no context has
                # been provisioned yet (conftest uses plain POSTs for them).
                self.coap.post(action.endpoint, payload=payload, timeout=5.0)
        except Exception as exc:                     # noqa: BLE001
            return StepResult(
                ok=False, item=comment,
                reason=f"@@! {prompt!r}: POST {action.endpoint} failed: {exc}")
        # A factory reset wipes the device's AT table and OSCORE state, so
        # every context we have materialized is now keyed on an entry that no
        # longer exists device-side. Drop them, exactly as the CoAP
        # `{"cmd":"reset","value":2}` path does — without this the whole rest
        # of the collection fails (measured: 44 passed -> 11) because it keeps
        # using a dead "TokenTest". The template re-provisions right after
        # (5.3.1.3 item 46 is `@#"Copy of AUTH PREPARATION"`).
        if "factory-reset" in action.endpoint:
            self.provisioning.contexts.clear()
            _dbg_group("[manual] cleared materialized OSCORE contexts after "
                       "the factory reset")
        # The device needs to finish restarting/erasing before the next
        # telegram; the prompt's own "once the device is ready" is what the
        # operator's click was waiting on.
        time.sleep(_MANUAL_ACTION_SETTLE_SECONDS * (self.time_scale or 1.0))
        self.coap.drain_socket(timeout=0.2)
        return StepResult(ok=True, item=comment)

    def _run_wait_command(self, comment: Comment, form: str,
                          value: str) -> StepResult:
        """Handle EITT's `@[w"<time>"` / `@@[w"<time>"` wait commands.

        Previously neither was implemented: `@@[w` fell into the generic
        "@@[" provisioning branch and was silently dropped, so 5.2.9.1
        read /swu/state the instant after triggering the update and saw
        UPGRADING(3) where the script expects IDLE(0).

        `form` is "@" (toolbar countdown) or "@@" (modal dialog); see the
        block comment at the call site for why they are treated
        differently.
        """
        seconds = _parse_time_to_next(self.variables.substitute(value))
        if form == "@@":
            # Operator-paced modal: the declared time is a ceiling, not a
            # delay. Wait a short fixed moment for whatever the device is
            # doing (the runtime_test_server simulates its upgrade with a
            # 2s delayed callback), rather than the declared value.
            seconds = min(seconds, _MODAL_WAIT_SECONDS)
        else:
            seconds *= self.time_scale
        if seconds > 0:
            self.sleep(seconds)
        return StepResult(ok=True, item=comment)

    def _run_inline_reference(self, comment: Comment, name: str) -> StepResult:
        target = self.preparations.resolve(name)
        if target is None:
            return StepResult(
                ok=False, item=comment,
                reason=f'@#"{name}" references an unknown TestCase — it '
                "must be generated/present in this XML per the "
                "CreationMessage instructions")
        sub_result = self.run(target)
        if not sub_result.ok:
            return StepResult(
                ok=False, item=comment,
                reason=f'inline reference @#"{name}" failed: '
                f"{sub_result.failure.reason if sub_result.failure else ''}")
        return StepResult(ok=True, item=comment)

    def _run_loop_invocation(self, comment: Comment, name: str,
                             loop_arg: str) -> StepResult:
        """"@>w"TestCaseName;unlimited"" — repeat the named TestCase,
        reading/writing sequence_loop_count.<name> / sequence_loop_limit.
        <name> exactly as the ported PatchContentFn hooks expect (see
        tests/docs/eitt-xml-interpreter.md §7)."""
        target = self.preparations.resolve(name)
        if target is None:
            return StepResult(
                ok=False, item=comment,
                reason=f'@>w"{name};{loop_arg}" references an unknown '
                "TestCase — it must be generated per the CreationMessage "
                "instructions")

        count_key = f"sequence_loop_count.{name}"
        limit_key = f"sequence_loop_limit.{name}"
        iteration = 1
        self.variables.set(count_key, str(iteration))

        max_iterations = 100_000  # guard against a runaway/missing limit
        while True:
            sub_result = self.run(target)
            if not sub_result.ok:
                return StepResult(
                    ok=False, item=comment,
                    reason=f'loop invocation "{name}" iteration '
                    f"{iteration} failed: "
                    f"{sub_result.failure.reason if sub_result.failure else ''}")

            limit_str = self.variables.get(limit_key)
            if limit_str is None:
                # No PatchContentFn/PatchParameterFn on this loop's
                # messages ever set a limit — treat as a single pass.
                break
            if iteration >= int(limit_str):
                break
            iteration += 1
            self.variables.set(count_key, str(iteration))
            if iteration > max_iterations:
                return StepResult(
                    ok=False, item=comment,
                    reason=f'loop invocation "{name}" exceeded '
                    f"{max_iterations} iterations without reaching its "
                    f"loop limit — likely a PatchContentFn/PatchParameterFn "
                    "bug or missing sequence_loop_limit write")
        return StepResult(ok=True, item=comment)

    # -- CoAPMessage ----------------------------------------------------

    # -- DnsMessage (mDNS discovery) --------------------------------------

    def _arm_unsolicited_dns(self, sequence: list, index: int) -> None:
        """Start listening for an mDNS announcement the DUT will push in
        reaction to the request at `index`, if a `DnsMessage` OUT with no
        query of its own follows it.

        The scan skips Comments AND the request's own paired response: the
        shape in 5.1.2.5a/b is `PUT dev/pm` -> `OUT 2.04` -> `DNS OUT PTR`,
        so stopping at the first CoAPMessage would always find the 2.04 and
        never arm. Arming has to happen BEFORE the request goes out, because
        the device re-announces from inside the PUT handler
        (oc_knx_dev.c:558 -> knx_dns_sd_update_service) — listening only
        once the OUT step is reached catches the tail of the announcement
        (in practice just the AAAA record) and never the subtype PTR the
        step is asserting on.
        """
        from transport import mdns_client
        for cand in sequence[index + 1:]:
            if isinstance(cand, Comment):
                continue
            if isinstance(cand, CoAPMessage):
                # The paired response to the request being prepared; the
                # DnsMessage we want may still come after it. Any FURTHER
                # request means the announcement (if any) belongs to that
                # one instead, so stop.
                if cand.activate and cand.is_request:
                    return
                continue
            if isinstance(cand, DnsMessage) and cand.activate:
                if cand.cway == "OUT":
                    self._dns_records = []
                    # await_live: the device goodbyes the OLD advertisement
                    # before announcing the new one, and it is the ANNOUNCE
                    # that carries the state this step asserts on.
                    self._dns_listener = mdns_client.MdnsListener(
                        timeout=_MDNS_TIMEOUT, await_live=True)
                    _dbg_group("[mdns] armed for an unsolicited "
                               f"{cand.record_type} announcement")
            return

    def _run_dns_message(self, msg: DnsMessage) -> StepResult:
        """Play one mDNS step.

        EITT models discovery as query/answer PAIRS of DnsMessage items:
          IN  Type="dnssd"|"mdns"  — the query the TOOL sends
          OUT Type="dnsrecord"     — the record the DUT must answer with

        The IN step arms a listener and sends the query; the OUT step
        matches what came back and runs any EvalFn. An OUT with no
        preceding IN (5.1.2.5a/b) is an UNSOLICITED announcement the DUT
        emits after a state change, so it only listens.
        """
        if not msg.activate:
            return StepResult(ok=True, item=msg)

        from transport import mdns_client

        service = self.variables.substitute(msg.service) or ""
        if msg.cway == "IN":
            # A query. The DUT answers with its WHOLE record set, not just
            # the record the next step happens to name, so cache everything
            # and let the following OUT steps consume from that cache.
            # traces/5_1.xml TelNo 121 shows exactly this: one PTR query for
            # _knx._udp.local draws PTR + SRV + AAAA + every subtype PTR
            # back, and the scripted OUT step (TelNo 122) matches
            # _00fa10020800._sub._knx._udp.local out of that single answer.
            #
            # The query is also REPEATED while the listener is open: an mDNS
            # responder delays its reply (RFC 6762 §6), and a preceding
            # factory reset makes this device stop and re-open its listen
            # socket, so a one-shot query can land in a dead window and be
            # lost with no sign of trouble on either side.
            listener = mdns_client.MdnsListener(timeout=_MDNS_TIMEOUT)
            listener.repeat_query(service, msg.record_type,
                                  every=_MDNS_RETRY_INTERVAL)
            self._dns_records = listener.records()
            self._dns_query_name = service
            _dbg_group(f"[mdns] query {msg.record_type} {service!r} -> "
                       f"cached {len(self._dns_records)} records")
            self._apply_wait(msg)
            return StepResult(ok=True, item=msg)

        # OUT — match against the cached answer. With nothing cached the DUT
        # is announcing unprompted (5.1.2.5a/b, after a state change), so
        # listen for that now.
        if not self._dns_records:
            # Prefer a listener armed BEFORE the request that provokes the
            # announcement (see _arm_unsolicited_dns); fall back to
            # listening now for a genuinely spontaneous one.
            #
            # The fallback needs await_live for exactly the same reason the
            # armed one does: a state change emits the GOODBYE for the old
            # advertisement first and the announce after it, so a listener
            # that stops at the first packet keeps only TTL=0 records and
            # never sees the announce this step is waiting for. It was set
            # only on the armed path, so every route that failed to arm
            # (several early returns in _arm_unsolicited_dns) fell back to
            # the historical bug. See finding M6.
            listener = self._dns_listener or mdns_client.MdnsListener(
                timeout=_MDNS_TIMEOUT, await_live=True)
            self._dns_listener = None
            self._dns_records = listener.records()
            _dbg_group(f"[mdns] unsolicited listen -> "
                       f"{len(self._dns_records)} records")
        return self._match_dns_record(msg, self._dns_records, service)

    def _match_dns_record(self, msg: DnsMessage, records: list,
                          service: str) -> StepResult:
        """Match one expected `Type="dnsrecord"` OUT step against what the
        DUT actually answered, then run its EvalFn if it has one."""
        want_type = (msg.record_type or "").upper()
        host = self.variables.substitute(msg.host) or ""
        candidates = [r for r in records if r.rtype == want_type]
        if not candidates:
            seen = ", ".join(sorted({r.rtype for r in records})) or "nothing"
            return StepResult(
                ok=False, item=msg,
                reason=f"expected an mDNS {want_type} record for "
                f"{service!r} but received {seen}")

        # EITT matches the record's OWNER name against Service, and (for
        # PTR/SRV) its rdata against Host. Names on the wire carry a
        # trailing dot the XML sometimes includes and sometimes doesn't,
        # so compare normalised.
        def norm(v: str) -> str:
            return (v or "").rstrip(".").lower()

        # `@@[MDNS"rqcheck=off"` relaxes the requirement that the answered
        # service equals the one that was QUERIED — which is what lets
        # 5.1.2.x query _knx._udp.local and then match a subtype PTR out
        # of the same answer. The scripted OUT step's own Service is still
        # matched either way; rqcheck governs the query/answer pairing,
        # not this record-level check.
        rqcheck = self.provisioning.mdns_request_check_enabled
        if rqcheck and self._dns_query_name and service:
            if norm(service) != norm(self._dns_query_name):
                return StepResult(
                    ok=False, item=msg,
                    reason=f"mDNS request-check is ON and the expected "
                    f"service {service!r} differs from the queried "
                    f"{self._dns_query_name!r} — the script must set "
                    f'@@[MDNS"rqcheck=off" to match across services')

        # TTL is deliberately NOT part of the match — see finding M5, which
        # proposed the opposite and was disproved by the reference trace.
        #
        # The idea was that a ttl=0 record is a GOODBYE (RFC 6762 §10.1)
        # and so cannot satisfy a step asserting the record EXISTS. That
        # reasoning fails on 5.1.2.5a, which uses the SAME step shape for
        # both directions of a state change: Step 1 sets pm=false and then
        # expects a `_pm._sub` PTR — there the goodbye IS the correct
        # answer — while Step 2 sets pm=true and expects the announce.
        #
        # Decisive evidence: in traces/5_1.xml EVERY DnsMessage row carries
        # TTL="0", including the ones EITT evaluated "OK". EITT does not
        # discriminate on TTL here at all; the attribute is a display
        # default, not an observed wire value. Preferring live records made
        # 5.1.2.5a fail.
        match = None
        for rec in candidates:
            if service and norm(rec.name) != norm(service):
                continue
            # host="0.0.0.0" is EITT's "any address" placeholder on AAAA
            # steps, and an empty/0 Host means "don't care".
            if host and host != "0.0.0.0" and norm(rec.target) != norm(host):
                continue
            match = rec
            break
        if match is None:
            got = "; ".join(f"{r.rtype} {r.name!r}->{r.target!r}"
                            for r in candidates[:4])
            return StepResult(
                ok=False, item=msg,
                reason=f"no mDNS {want_type} record matched service="
                f"{service!r} host={host!r}; received: {got}")

        _dbg_group(f"[mdns] matched {match.rtype} {match.name!r} -> "
                   f"{match.target!r} port={match.port}")

        if msg.eval_fn:
            hook = get_hook(msg.eval_fn)
            payload = json.dumps({"dns": {
                "planned": {"service": service, "port": _safe_int(msg.port)},
                "received": {"type": match.rtype, "service": match.name,
                             "host": match.target, "port": match.port},
            }})
            if not hook(self.hook_ctx, payload):
                return StepResult(
                    ok=False, item=msg,
                    reason=f"EvalFn '{msg.eval_fn}' failed: "
                    f"{self.hook_ctx.last_error}")

        self._apply_wait(msg)
        return StepResult(ok=True, item=msg)

    def _run_coap_message(self, msg: CoAPMessage) -> StepResult:
        if not msg.activate:
            return StepResult(ok=True, item=msg)

        if msg.is_request:
            # An 'IN' step whose Code is a response code is not a request
            # at all: it is the tool answering a telegram the DUT sent us.
            # 5.3.17.6 is the only such case in 5.3 — the tool challenges
            # the DUT's sensor telegram with 4.01+Echo.
            if (msg.code or "").strip() == "4.01":
                return self._send_echo_challenge(msg)
            if msg.use_last_rx_token:
                return self._answer_last_rx_request(msg)
            return self._send_request(msg)
        return self._expect_response(msg)

    def _remember_response(self, resp) -> None:
        """Record the last response's payload, its code AND its message type.

        All three are needed by `_expect_response`: the payload for content
        matching, the code because most negative TestCases assert nothing
        else, and the type because 1324 of the template's 1457 active
        response rows specify one. `None` for code/type means "no response
        arrived", which is deliberately distinguishable from an empty
        payload.
        """
        self._last_response = resp.payload if resp else None
        self._last_response_code = (
            (resp.code_class, resp.code_detail) if resp is not None else None)
        self._last_response_type = (
            resp.msg_type if resp is not None else None)


    def _resolve_path_and_query(self, msg: CoAPMessage) -> str:
        path = self.variables.substitute(msg.path) or ""
        query_parts = []
        for p in msg.url_parameters:
            key = self.variables.substitute(p.key)
            value = self.variables.substitute(p.value)
            if msg.patch_parameter_fn:
                continue  # PatchParameterFn replaces the whole query string
            query_parts.append(f"{key}={value}")
        if msg.patch_parameter_fn:
            hook = get_hook(msg.patch_parameter_fn)
            patched = hook(self.hook_ctx, "&".join(query_parts))
            if patched is False:
                raise RuntimeError(
                    f"PatchParameterFn '{msg.patch_parameter_fn}' failed: "
                    f"{self.hook_ctx.last_error}")
            return path + (patched or "")
        if query_parts:
            return path + "?" + "&".join(query_parts)
        return path

    def _resolve_body(self, msg: CoAPMessage, path: str) -> bytes:
        if msg.patch_content_fn:
            hook = get_hook(msg.patch_content_fn)
            patched = hook(self.hook_ctx, msg.content or "")
            if patched is False:
                raise RuntimeError(
                    f"PatchContentFn '{msg.patch_content_fn}' failed: "
                    f"{self.hook_ctx.last_error}")
            # swu_patch_content returns a space-separated hex string.
            return bytes.fromhex(patched.replace(" ", "")) if patched else b""

        # A <Cbor> element carries the body as PRE-ENCODED CBOR hex, used
        # where the JSON-ish <Content> mini-language cannot express the exact
        # wire bytes the step needs. 5.10.5.2 is the heavy user (14 of the 16
        # occurrences in this template): its whole point is "Content-Format
        # omitted", so it hand-rolls each payload rather than letting the
        # CF-driven encoder pick one.
        #
        # This must be checked BEFORE the `not msg.content` bail-out below:
        # such a message usually has an EMPTY <Content>, so without this the
        # request went out with a zero-length body and the DUT quietly did
        # nothing — which is how 5.10.5.2's `POST /k` group write silently
        # failed to set /p/1 and the later read saw False instead of true.
        if msg.cbor:
            return bytes.fromhex(
                self.variables.substitute(msg.cbor).replace(" ", "").strip())

        if not msg.content:
            return b""
        text = self.variables.substitute(msg.content)
        if text.strip() == "*":
            return b""

        cf = (msg.cf or "").lower()
        if cf in CBOR_CONTENT_FORMATS:
            import cbor2
            try:
                obj = json.loads(text)
            except ValueError as e:
                raise ValueError(
                    f"CoAPMessage {msg.id}: <Content> is not valid JSON "
                    f"and CF is application/cbor: {e}") from e

            if path.strip("/").rstrip("/") == "auth/at":
                # /auth/at has no flat field-name schema of its own.
                # CONFIRMED against real EITT trace logs
                # (tests/runtime/data/traces/5_3.xml, TelNo 53/61 for
                # the single-entry case, TelNo 89 for the 2-entry case):
                # the wire envelope is ALWAYS a bare CBOR array of raw
                # entry maps — "81 BF 08...FF" (array of 1) for a
                # single-entry write, "82 BF 08...FF BF 08...FF" (array
                # of 2) for a multi-entry write — NEVER a {0: entry}
                # map wrapper, regardless of entry count. This
                # contradicts conftest.py's own oscore_ctx/auth_prepare
                # fixtures, which send {0: at_inner} for a single entry
                # and happen to also be accepted by the stack — but the
                # real EITT-recorded wire format is the array form, so
                # that's what this interpreter reproduces.
                if isinstance(obj, list):
                    return cbor2.dumps(
                        [AT_ENTRY_SCHEMA.encode(entry) for entry in obj])
                return cbor2.dumps([AT_ENTRY_SCHEMA.encode(obj)])

            schema = get_schema(path)
            return cbor2.dumps(schema.encode(obj))
        return text.encode("utf-8")

    def _resolve_oscore_overrides(self, msg: CoAPMessage, oscore_ctx):
        """Translate a request message's OSCORE adversarial attributes
        into the (seq_no_override, seq_no_offset, invalid_auth_tag,
        kid_override) tuple CoapClient.oscore_request expects. Returns all
        None for an ordinary request.

        - SeqNoOffset=N → seq_no_offset=N: the send still consumes/advances
          the context's sequence counter (as EITT's does), transmitting
          the consumed number shifted by N. The replay-window negative
          tests (5.3.17.x) walk N across the window edges (+31 ahead /
          -1 just-inside / -32/-35/-64 outside) to exercise accept / replay
          / reject. See OscoreContext.protect_request for why this
          (relative, counter-advancing) is the EITT-faithful model rather
          than a fixed absolute PIV.
        - SeqNoFn="fix" + SeqNo=V → seq_no_override=V: pin the transmitted
          sequence number to exactly V WITHOUT advancing the counter
          (5.3.18.1 fixes it so each InvalidAuthTag attempt is
          reproducible). SeqNoFn "automatic"/"asinrequest"/"not set" need
          no override — normal auto-increment / echo-driven behavior
          already matches.
        - InvalidAuthTag=<hex> → corrupt the AEAD tag with these bytes
          (5.3.18.1 integrity negative test).
        - RndKid="yes" → send a random kid the device has no context for
          (5.3.17.6). A fixed non-existent kid keeps the test reproducible.
        """
        seq_no_override = None
        seq_no_offset = None
        if msg.seq_no_offset is not None:
            seq_no_offset = msg.seq_no_offset
        elif (msg.seq_no_fn or "").lower() == "fix" and msg.seq_no is not None:
            try:
                seq_no_override = int(self.variables.substitute(msg.seq_no))
            except (ValueError, TypeError):
                seq_no_override = None

        invalid_tag = None
        if msg.invalid_auth_tag:
            resolved = self.variables.substitute(msg.invalid_auth_tag)
            try:
                invalid_tag = bytes.fromhex(resolved.replace(" ", ""))
            except ValueError:
                invalid_tag = None

        kid_override = b"\xde\xad\xbe\xef" if msg.rnd_kid else None

        return seq_no_override, seq_no_offset, invalid_tag, kid_override

    def _resolve_echo(self, msg: CoAPMessage) -> Optional[bytes]:
        """Return the CoAP Echo (option 252) value to send with this
        request, or None. EITT's Echo attribute:
        - "sync"  → echo back the challenge the DUT sent in a PRIOR
          response (self._last_echo), captured by _send_request. This is
          the group-message replay-synchronisation handshake: a first
          group POST gets a 4.01 + Echo challenge, and the follow-up
          telegram (Echo="sync") re-sends carrying that echo so the
          device can bind it to the now-synchronised sequence number.
        - "fix"   → send the literal EchoValue attribute verbatim.
        - "not set"/absent → no Echo option.
        """
        echo_attr = (msg.echo or "").lower()
        if echo_attr == "sync":
            return self._last_echo
        if echo_attr == "fix" and msg.echo_value:
            try:
                return bytes.fromhex(
                    self.variables.substitute(msg.echo_value).replace(" ", ""))
            except ValueError:
                return None
        return None

    def _send_request(self, msg: CoAPMessage) -> StepResult:
        path = self._resolve_path_and_query(msg)
        try:
            body = self._resolve_body(msg, path)
        except KeyError as e:
            return StepResult(
                ok=False, item=msg,
                reason=f"could not encode request body for '{path}': {e}")

        # PatchAddressFn retargets THIS request at an address the script
        # discovered at runtime rather than the configured one. 5.1.2.x
        # uses it to prove mDNS discovery actually worked: the sequence
        # first invalidates the cached address
        # (@@[pahdev"BDUT;ipaddress=invalid"), then resolves SRV+AAAA into
        # mdns_port/mdns_ip, and patch_address_5_1_2_1 hands back
        # "[ip]:port" for the closing GET /.well-known/core.
        with self._patched_address(msg) as addr_error:
            if addr_error:
                return StepResult(ok=False, item=msg, reason=addr_error)
            return self._send_request_inner(msg, path, body)

    @contextmanager
    def _patched_address(self, msg: CoAPMessage):
        """Point the CoAP client at this message's PatchAddressFn address
        for the duration of one request, then restore it."""
        if not msg.patch_address_fn:
            yield None
            return
        hook = get_hook(msg.patch_address_fn)
        patched = hook(self.hook_ctx, self.variables.substitute(msg.ip) or "")
        if not patched or patched is True:
            yield (f"PatchAddressFn '{msg.patch_address_fn}' failed: "
                   f"{self.hook_ctx.last_error}")
            return
        m = re.match(r"^\[([^\]]+)\](?::(\d+))?$", str(patched).strip())
        if not m:
            yield (f"PatchAddressFn '{msg.patch_address_fn}' returned "
                   f"{patched!r}, expected '[<ipv6>]:<port>'")
            return
        host, port = m.group(1), m.group(2)
        if not host:
            yield (f"PatchAddressFn '{msg.patch_address_fn}' returned no "
                   f"address ({patched!r}) — did the SRV/AAAA steps run?")
            return
        old_host, old_port = self.coap.host, self.coap.port
        self.coap.host = host
        if port:
            self.coap.port = int(port)
        _dbg_group(f"[mdns] request retargeted to [{self.coap.host}]:"
                   f"{self.coap.port} (was [{old_host}]:{old_port})")
        try:
            yield None
        finally:
            self.coap.host, self.coap.port = old_host, old_port

    def _send_request_inner(self, msg: CoAPMessage, path: str,
                            body: bytes) -> StepResult:

        oscore_ctx = self.provisioning.resolve_context(msg.sec_context)
        method_map = {"GET": 1, "POST": 2, "PUT": 3, "DELETE": 4}
        method_code = method_map.get(msg.code)
        if method_code is None:
            raise UnsupportedFeature(
                f"CoAPMessage {msg.id} has an unrecognized request Code "
                f"'{msg.code}'")

        timeout = msg.ack_chk_timeout
        # Which path this REQUEST took. See F6 — not the same thing as the
        # OUT row's `Secure`, which describes the response.
        self._last_request_secured = oscore_ctx is not None
        if oscore_ctx is not None:
            seq_no_override, seq_no_offset, invalid_tag, kid_override = \
                self._resolve_oscore_overrides(msg, oscore_ctx)
            echo = self._resolve_echo(msg)
            resp = self.coap.oscore_request(
                oscore_ctx, method_code, path, payload=body, timeout=timeout,
                accept=_content_format_code(msg.accepted_cf),
                # The request's OWN Content-Format (option 12). Omitting it
                # made every payload look like CBOR to the device, which
                # then CBOR-parsed raw firmware bytes at /a/swu and
                # answered 4.00 for any block that did not happen to be
                # valid CBOR (oc_ri.c:1083 only parses when the format IS
                # CBOR/OSCORE). 5.2.9.1 uploads
                # CF="application/octet-stream".
                content_format=_content_format_code(msg.cf),
                seq_no_override=seq_no_override,
                seq_no_offset=seq_no_offset,
                invalid_auth_tag=invalid_tag,
                kid_override=kid_override,
                echo=echo,
                expect_echo_challenge=self._expect_echo_challenge,
                msg_type=_coap_msg_type(msg.type),
                crit_option=_resolve_crit_option(msg))
        else:
            scope = _plain_multicast_scope(msg.ip)
            if scope is not None:
                # A real multicast, not a unicast to the known DEVICE_HOST —
                # see _plain_multicast_scope. The device joins the CoAP
                # multicast groups on the STANDARD port 5683 even though its
                # unicast port is ephemeral (ipadapter.c
                # "Adding all CoAP nodes"), so the scope address + default
                # port is what reaches it; the reply comes back from its
                # unicast port, which `source_addr` carries.
                responses = self.coap.multicast_get(
                    path, scope=scope,
                    accept=_content_format_code(msg.accepted_cf),
                    collect_timeout=timeout or _MDNS_TIMEOUT,
                    stop_after_first=True)
                resp = responses[0] if responses else None
                if resp is not None and resp.source_addr:
                    self._last_discovered_addr = resp.source_addr
            else:
                method = {"GET": self.coap.get, "POST": self.coap.post,
                          "PUT": self.coap.put,
                          "DELETE": self.coap.delete}[msg.code]
                kwargs = {"timeout": timeout} if timeout else {}
                if msg.code in ("POST", "PUT"):
                    resp = method(path, payload=body, **kwargs)
                else:
                    resp = method(path, **kwargs)

        self._remember_response(resp)
        self._last_planned = msg.content
        self._last_request_path = path
        if resp is not None and getattr(resp, "echo_value", None):
            self._last_echo = resp.echo_value
        self._remember_transmit_href(path, msg)

        if (resp is not None and resp.is_successful
                and path.strip("/").rstrip("/") == ".well-known/knx"):
            self._invalidate_contexts_on_device_reset(msg, body)

        if msg.eval_fn:
            hook = get_hook(msg.eval_fn)
            response_json = _response_as_json_text(
                resp.payload if resp else b"", path, msg.cf or "application/cbor")
            ok = hook(self.hook_ctx, msg.content or "", response_json)
            if not ok:
                return StepResult(
                    ok=False, item=msg,
                    reason=f"EvalFn '{msg.eval_fn}' failed: "
                    f"{self.hook_ctx.last_error}")

        self._apply_wait(msg)
        return StepResult(ok=True, item=msg)

    def _invalidate_contexts_on_device_reset(self, msg: CoAPMessage,
                                             body: bytes) -> None:
        """A successful {"cmd": "reset", "value": 2} (RESET_TO_DEFAULT_STATE)
        to .well-known/knx wipes the device's ENTIRE AT table and OSCORE
        replay state (oc_knx_dev.c: reset_mode==2 calls
        oc_delete_at_table()). Every OSCORE context this runner has
        materialized (e.g. AUTH PREPARATION's "TokenTest") is keyed on an
        AT entry that no longer exists device-side afterward, and even
        when the same sender_id/mastersecret gets re-provisioned (as the
        XML always does — a full reset is invariably followed by another
        "@#Copy of ... PREPARATION"), the device's replay window is back
        at 0 too, so OUR client-side context must be dropped and rebuilt
        from scratch rather than keep incrementing its now-stale 'ssn' —
        otherwise the next request reuses/exceeds a partial IV the device
        no longer recognizes as fresh.

        Deliberately NOT triggered by a bare {"cmd": "restart"} or a
        {"cmd": "reset", "value": 7} (RESET_TO_DEFAULT_WO_IA): per
        oc_knx_dev.c, a plain restart explicitly does "DON'T delete at
        table" (only the transient PASE/"TmpTok" token is removed), and
        value=7 calls oc_delete_at_table_except_sec_scope_entries() —
        both leave an if.sec-scoped token like "TokenTest" (used by e.g.
        5.3.1.3's own restart/reset-7/reset-2 sequence) fully valid
        device-side, so dropping it here would be wrong: the very next
        request through it would suddenly look unprovisioned/unsecured
        even though the device still has it."""
        try:
            import cbor2
            decoded = cbor2.loads(body) if body else {}
        except Exception:
            return
        if not isinstance(decoded, dict):
            return
        if decoded.get(2) == "reset" and decoded.get(1) == 2:
            self.provisioning.contexts.clear()

    def _prepare_for_request(self, sequence: list, index: int) -> None:
        """Set up whatever the step at `index` needs that depends on what
        the script expects to come BACK — both pieces of state have to be
        decided before the request goes out:

        - `_expect_echo_challenge`: the script — not the tool — owns the
          4.01+Echo challenge/retry handshake for this request, so the
          client's automatic Echo-retry must stand down (see
          oscore_request). Two independent signals set it; see
          `_script_owns_echo_handshake`.
        - `_group_capture`: the next active OUT is a telegram on a joined
          multicast group's RX context, so that group's listener has to
          be bound already — the DUT answers a group read from inside its
          own request handler.

        Only the immediately-following active OUT matters: EITT pairs one
        request with one expectation, and 5.3.19.5's 100 read/response
        pairs are exactly that pairing repeated.
        """
        item = sequence[index]
        if not (isinstance(item, CoAPMessage) and item.activate
                and item.is_request):
            # Leave any armed capture alone — only a request can arm one,
            # and the OUT step that follows must still find it.
            return
        self._group_capture = None
        self._expect_echo_challenge = False

        # An UNSOLICITED mDNS announcement (a `DnsMessage` OUT with no
        # query of its own) is pushed by the DUT in reaction to the state
        # change this very request makes — 5.1.2.5a/b toggle
        # PUT /dev/pm and expect the _pm subtype PTR to be re-announced.
        # Arm the listener before the request goes out, or the
        # announcement is already gone by the time the OUT step runs.
        # NOTE: do NOT arm the unicast telegram read here. It was tried, and
        # arming ahead of every request breaks more than it fixes (12 passed
        # instead of 14: 5.4.1.11 and 5.4.1.15 regressed) because the armed
        # read OWNS the shared client socket and swallows the ordinary
        # request/response traffic that follows. 5.4.1.4 needs a different
        # answer — see the note in _expect_unicast_telegram.
        self._arm_unsolicited_dns(sequence, index)

        nxt = None
        for cand in sequence[index + 1:]:
            if isinstance(cand, CoAPMessage):
                if cand.activate:
                    nxt = cand
                break
        self._expect_echo_challenge = self._script_owns_echo_handshake(nxt)
        if nxt is None or not nxt.is_response:
            return
        # The step's own SecContext when it names one; otherwise the joined
        # group's binding. 5.4.1.9's telegram step carries `SecContext=""`
        # (`POST .well-known/knx {"cmd":"restart"}` -> `OUT POST` with no
        # context), yet the telegram IS protected under MulticastRx — the
        # group inherits `context=MulticastRx` from 5.4.1.3 and the trace
        # records TelNo 185 with that context. Without this the listener is
        # never armed and the init read, which the device sends 0.4s after
        # the restart, is gone before the OUT step looks for it.
        group = self.provisioning.group_for_rx_context(nxt.sec_context)
        if group is None and not nxt.sec_context and _is_method_code(nxt.code):
            group = self.provisioning.joined_group_with_context()
        if group is None:
            return
        ctx_alias = nxt.sec_context or group.context_alias or ""
        oscore_ctx = self.provisioning.resolve_context(ctx_alias)
        addr = self._group_address_for(group)
        _dbg_group(f"[group] arming listener on {addr} for "
                   f"{ctx_alias!r} ctx={oscore_ctx is not None}")
        if oscore_ctx is None or addr is None:
            return
        # Honour the step's own budget, not a flat 5s: 5.4.1.9's telegram
        # follows a device RESTART and its TimeToNext is
        # #RESTART_TIME_TO_NEXT (20s), because the device has to come all the
        # way back up before it can send the init read.
        # substitute() first: TimeToNext is almost always a #VAR here
        # (1489 #GENERAL_TIME_TO_NEXT / 71 #FACTORY_RESET_TIME_TO_NEXT /
        # 10 #RESTART_TIME_TO_NEXT in the template), and the raw token
        # parses to 0.0, which silently collapsed this whole expression to
        # the 5.0 fallback — including for the 20s restart case the comment
        # above says this exists to honour. See finding F13.
        timeout = (nxt.ack_chk_timeout
                   or _parse_time_to_next(
                       self.variables.substitute(nxt.time_to_next))
                   or 5.0)
        self._group_capture = self.coap.arm_group_listener(
            oscore_ctx, addr, timeout=max(timeout, 5.0))

    def _script_owns_echo_handshake(
            self, nxt: Optional[CoAPMessage]) -> bool:
        """True when the SCRIPT, not the tool, drives the 4.01+Echo
        challenge/retry for the request about to be sent — so
        `oscore_request`'s automatic Echo-retry must stand down.

        Two independent signals, either of which is sufficient:

        1. `@@[pahsnc"off"` — EITT's own "switch automatic CoAP sequence
           number synchronisation off". Every `off` region in the template
           (5.3.17.4/.5/.6/.7/.8, 5.4.1.6) then spells the handshake out as
           explicit steps: request → `4.01` OUT carrying `Echo="sync"` →
           replayed request, also `Echo="sync"`. If the tool auto-retried
           inside the first send, the 4.01 step would find nothing to match
           and the replay step would be a duplicate. `pahsnc"on"` (the
           default, and what closes each region) restores auto-handling.

        2. The next active OUT is itself a `4.01`. Kept alongside the flag
           rather than replaced by it: an `off` region is scoped to a whole
           span of steps, but requests INSIDE that span which are not part
           of the handshake still need the retry suppressed only where the
           script models the challenge — and, conversely, adversarial
           one-shot steps outside any `off` region (5.3.17.1's replays)
           rely on this lookahead alone. Neither signal subsumes the other.
        """
        if not self.provisioning.seq_no_check_enabled:
            return True
        return (nxt is not None and nxt.is_response
                and (nxt.code or "").strip() == "4.01")

    @staticmethod
    def _answers_with_last_rx_token(sequence: list, index: int) -> bool:
        """True when the next active CoAPMessage after `index` answers with
        EITT's UseLastRxToken — the marker that the step AT `index` is a
        request the DUT sent to us rather than a telegram to match."""
        for cand in sequence[index + 1:]:
            if isinstance(cand, CoAPMessage) and cand.activate:
                return bool(cand.is_request and cand.use_last_rx_token)
        return False

    def _arm_dut_request_listener(self, msg: CoAPMessage):
        """Bind a listener for an unsecured request the DUT will send to us,
        returning the handle (or None when no scope is joined).

        The DUT multicasts its `GET /.well-known/core` to a well-known
        scope address, so we listen on the joined scope groups (EITT's
        `Multicast Link-Local`/`Realm-Local`/`Site-Local` aliases, which
        5.4.1.4 and 5.4.1.15 join for exactly this). Link-local first: that
        is the scope the device actually answers on over the veth pair.
        """
        addrs = []
        for grp in self.provisioning.multicast_groups.values():
            if not grp.joined:
                continue
            addr = self._group_address_for(grp)
            if addr and addr not in addrs:
                try:
                    scope = int(grp.scope) if grp.scope else 5
                except (TypeError, ValueError):
                    scope = 5
                addrs.append((scope, addr))
        if not addrs:
            return None
        addrs.sort()
        _, addr = addrs[0]
        _dbg_group(f"[group] arming plain listener on {addr} for a "
                   f"DUT-originated {msg.code} /{msg.path}")
        return self.coap.arm_plain_listener(
            addr, timeout=msg.ack_chk_timeout or 5.0,
            path=msg.path or None)

    def _group_address_for(self, group) -> Optional[str]:
        """The IPv6 group address a MulticastGroup denotes, or None when it
        can't be resolved.

        Two shapes, matching EITT's own Multicast Groups dialog:
        - a KNX group (groupid + installationid + scope), e.g. MCastGrp01
          -> ff35:30:fd11:9988:7766:0:8000:1
        - a bare well-known scope with no group/installation id, i.e. the
          "Multicast Link-Local" / "Realm-Local" / "Site-Local" aliases
          5.4 joins so the tool can receive the DUT's discovery queries
          -> ff02::fd / ff03::fd / ff05::fd (EITT's dialog lists exactly
          these three addresses).
        """
        from transport.coap_client import (knx_group_multicast_address,
                                 KNX_MULTICAST_ADDRESSES)
        try:
            scope = int(group.scope) if group.scope else 5
        except (TypeError, ValueError):
            return None
        # A "00:0000:0000" installation id / "0000:0000" group id is EITT's
        # way of saying "not a KNX group, just the well-known scope address".
        def _empty(v):
            return v is None or not str(v).replace(":", "").strip("0")
        if _empty(group.group_id) and _empty(group.installation_id):
            return KNX_MULTICAST_ADDRESSES.get(scope)
        try:
            grpid = int(str(group.group_id).replace(":", ""), 16)
            iid = int(str(group.installation_id).replace(":", ""), 16)
        except (TypeError, ValueError):
            return None
        return knx_group_multicast_address(grpid, iid, scope)

    def _expect_dut_request(self, msg: CoAPMessage) -> StepResult:
        """Wait for a request the DUT sends TO US on a joined multicast
        scope, and stash it so the following step can answer it.

        5.4's unicast group messaging needs this: before the DUT can send a
        unicast s-mode telegram it has to learn our individual address, so
        it multicasts `GET /.well-known/core` and waits for someone to
        claim the IA its recipient table names. The script models that
        query as an `OUT GET` (the DUT is the sender) followed by an
        `IN 2.05 UseLastRxToken=yes` (our answer) — see 5.4.1.4 items
        52/53 and 5.4.1.15 items 49/50, which is why those TestCases join
        `Multicast Link-Local`/`Realm-Local`/`Site-Local` first.
        """
        capture = self._dut_request_capture
        self._dut_request_capture = None
        if capture is None:
            # Nothing armed it, because nothing in the script provokes this
            # query: 5.4.1.15's preceding step is the comment "Trigger
            # device manually". Self-arm and then trigger, the same shape
            # as the unsolicited group telegram in _expect_group_telegram.
            capture = self._arm_dut_request_listener(msg)
            if capture is None:
                return StepResult(
                    ok=False, item=msg,
                    reason=f"expected the DUT to send {msg.code} "
                    f"/{msg.path} to us, but no multicast scope is joined "
                    f"to listen on")
            self._trigger_sensor(None, msg.ack_chk_timeout or 5.0)
        got = capture.result()
        if got is None:
            return StepResult(
                ok=False, item=msg,
                reason=f"expected the DUT to send {msg.code} /{msg.path} "
                f"to us, but nothing arrived")
        self._last_rx_request = got
        _dbg_group(f"[group] captured DUT request token={got.token.hex()} "
                   f"from {got.source_addr}")
        self._apply_wait(msg)
        return StepResult(ok=True, item=msg)

    def _answer_last_rx_request(self, msg: CoAPMessage) -> StepResult:
        """Answer the request the DUT last sent us, quoting its CoAP token
        (EITT's `UseLastRxToken`).

        The body is the script's own `<Content>` — for the discovery query
        a link-format `<>;ep="knx://sn.… knx://ia.…"` string naming the
        serial number and individual address the tool claims. Unprotected:
        `.well-known/core` discovery is unsecured, and these steps carry no
        SecContext.
        """
        got = self._last_rx_request
        if got is None or not got.source_addr:
            return StepResult(
                ok=False, item=msg,
                reason="UseLastRxToken: no previously received DUT request "
                "to answer")
        body = self.variables.substitute(msg.content or "").encode()
        code = _response_code_tuple(msg.code)
        if code is None:
            return StepResult(
                ok=False, item=msg,
                reason=f"UseLastRxToken: unrecognized response Code "
                f"{msg.code!r}")
        # ARM BEFORE ANSWERING. This reply is what unblocks the DUT: it has
        # been waiting to learn our address, and it sends the unicast s-mode
        # telegram as soon as it has one. Measured, it lands in the window
        # between this send and the NEXT step starting its own read — and
        # `Wait="yes"` on this step means _apply_wait sleeps right through
        # that window, so the telegram is gone before the following
        # `OUT CON POST /k` ever looks. Same arm-then-send ordering the group
        # listener needs, for the same reason.
        if self._unicast_capture is None:
            self._arm_unicast_for_next(msg)
        self.coap.send_plain_response(
            code, body, got.token, got.source_addr,
            content_format=_content_format_code(msg.cf),
            msg_type=_coap_msg_type(msg.type))
        self._apply_wait(msg)
        return StepResult(ok=True, item=msg)

    def _arm_unicast_for_next(self, msg: CoAPMessage) -> None:
        """Start reading for a DUT-originated unicast telegram now, if the
        step after this one expects it (an `OUT` whose Code is a method and
        whose SecContext is not a joined group's)."""
        seq = self._sequence or []
        idx = next((k for k, it in enumerate(seq) if it is msg), None)
        if idx is None:
            _dbg_group("[group] unicast arm: answer step not found in the "
                       "current sequence")
            return
        self._arm_unicast_ahead(seq, idx)

    def _arm_unicast_ahead(self, seq: list, idx: int) -> None:
        """Arm the unicast read if the next active step is a DUT-originated
        unicast telegram. Idempotent — a capture already armed is left alone."""
        if self._unicast_capture is not None:
            return
        for cand in seq[idx + 1:]:
            if isinstance(cand, Comment):
                continue
            if not isinstance(cand, CoAPMessage):
                return
            # Skip DEACTIVATED steps: 5.4.1.4's discovery pair (items 52/53)
            # is Activate="no" because the device already knows our address,
            # and the telegram we want sits right after it at item 54.
            if not cand.activate:
                continue
            # Skip the ACK/response the request at `idx` pairs with — the
            # telegram may still come after it. A further REQUEST means any
            # telegram belongs to that one instead, so stop.
            if _response_code_tuple(cand.code) is not None:
                continue
            if cand.is_request:
                return
            if not (cand.is_response and _is_method_code(cand.code)):
                _dbg_group(f"[group] unicast arm: next step is {cand.code!r}, "
                           "not a DUT-originated telegram")
                return
            if self.provisioning.group_for_rx_context(cand.sec_context):
                _dbg_group("[group] unicast arm: next step is on a joined "
                           "group, leaving it to the group listener")
                return
            oscore_ctx = (self.provisioning.resolve_context(cand.sec_context)
                          or self.provisioning.any_unicast_context())
            if oscore_ctx is None:
                _dbg_group(f"[group] unicast arm: no context for "
                           f"{cand.sec_context!r}")
                return
            _dbg_group("[group] arming unicast telegram read before the "
                       "discovery answer")
            self._unicast_capture = self.coap.recv_unicast_telegram(
                oscore_ctx, timeout=_UNICAST_TELEGRAM_TIMEOUT,
                path=cand.path or None)
            return

    def _send_echo_challenge(self, msg: CoAPMessage) -> StepResult:
        """Answer the DUT's group telegram with a 4.01+Echo challenge, then
        arm a listener for the retransmission the DUT owes us.

        This is EITT 5.3.17.6's "Sending Device Responds to Echo Option
        Challenge": having received the sensor telegram, the tool plays
        challenger, and the DUT must resend the same telegram with an
        incremented Partial IV (traces/5_3.xml TelNo 761/762/763 — PIV A0,
        challenge, PIV A1).
        """
        telegram = self._last_group_telegram
        if telegram is None or telegram.oscore_kid is None:
            return StepResult(
                ok=False, item=msg,
                reason="cannot send a 4.01 Echo challenge: no preceding "
                "group telegram to challenge")
        oscore_ctx = self.provisioning.resolve_context(msg.sec_context)
        if oscore_ctx is None:
            return StepResult(
                ok=False, item=msg,
                reason=f"no OSCORE context '{msg.sec_context}' for the "
                f"4.01 Echo challenge")

        # Our own challenge value; the DUT must quote it back. EITT's
        # Echo="sync" on this step means "carry an Echo option", and the
        # device only checks the length/freshness bookkeeping, so any
        # 8-byte value works (the device's own challenges are timestamps).
        echo = self._last_echo or os.urandom(8)

        group = self.provisioning.group_for_rx_context(msg.sec_context)
        timeout = msg.ack_chk_timeout or 5.0
        # Arm BEFORE challenging: the DUT retransmits as soon as it
        # processes the challenge.
        if group is not None:
            addr = self._group_address_for(group)
            if addr is not None:
                self._group_capture = self.coap.arm_group_listener(
                    oscore_ctx, addr, timeout=timeout)

        self.coap.send_echo_challenge(
            oscore_ctx, telegram.oscore_kid, telegram.oscore_piv or b"\x00",
            echo, telegram.token)
        self._last_echo = echo
        self._apply_wait(msg)
        return StepResult(ok=True, item=msg)

    def _remember_transmit_href(self, path: str, msg: CoAPMessage) -> None:
        """Record the href of an fp/g group-object entry written with the
        TRANSMISSION cflag (bit 6, OC_CFLAG_TRANSMISSION in
        api/oc_knx_fp.h: "can transmit on self triggered read/write
        actions"). That datapoint — /p/3 in 5.3.17.6, whose entry is the
        only one written with cflag=64 — is the one _trigger_sensor pokes.
        Entries without that flag (cflag=16 write-only, 24 read+write) are
        deliberately ignored: poking those would change a value the test
        is about to assert on.
        """
        if path.strip("/").rstrip("/") != "fp/g" or not msg.content:
            return
        try:
            entries = json.loads(self.variables.substitute(msg.content))
        except (ValueError, TypeError):
            return
        if isinstance(entries, dict):
            entries = [entries]
        if not isinstance(entries, list):
            return
        for entry in entries:
            if not isinstance(entry, dict):
                continue
            href = entry.get("href")
            cflag = entry.get("cflag")
            if href and isinstance(cflag, int) and cflag & (1 << 6):
                self._transmit_href = href if href.startswith("/") \
                    else "/" + href

    def _trigger_sensor(self, group, timeout: float) -> None:
        """Make the DUT originate a group telegram by poking the datapoint
        behind its transmitting group object.

        EITT's script says "Trigger sensor on the device to send s-mode
        message" — on certification hardware a human presses the sensor.
        The stack exposes /test/trigger for exactly this
        (oc_test_control.c: "same as EITT GUI 'SOO press me'"), which is
        what overrides.py's action="trigger" and
        test_5_4_group_comm.py's _trigger_sensor already use.
        """
        trigger_ctx = self.provisioning.resolve_context("TokenTest")
        if trigger_ctx is None:
            return
        import cbor2
        href = self._transmit_href or "/p/3"
        try:
            self.coap.oscore_request(
                trigger_ctx, 2, "/test/trigger",
                payload=cbor2.dumps({11: href}), timeout=timeout)
        except Exception as exc:
            _dbg_group(f"[group] /test/trigger failed: {exc}")

    def _expect_group_telegram(self, msg: CoAPMessage, group
                               ) -> StepResult:
        """Match a telegram the DUT sent to the MULTICAST GROUP address.

        Two shapes, distinguished by whether a capture is already armed:

        - SOLICITED (5.3.19.5): a group read (`/k` with `st: r`) we sent
          is answered by the DUT with an s-mode group RESPONSE (`st: a`)
          addressed to the group, not a unicast reply — device log,
          oc_knx_client.c:225 "Sending s-mode multicast" to
          coap://[ff35:0030:fd11:9988:7766:0000:8000:0001]:5683. The DUT
          answers from inside its own request handler, so
          _prepare_for_request armed the listener before we sent.

        - UNSOLICITED (5.3.17.6): an OUT step with NO preceding request at
          all, i.e. the sensor-press telegram. Nothing armed a listener,
          so arm one here and then trigger the sensor.

        EITT's recorded trace labels both DestinationAddress="BDUT"
        because it logs from its own receive perspective; its LocaIP /
        LocaMCast="Y" fields (traces/5_3.xml TelNo 761) and the device log
        are what actually show the group address.
        """
        # The step's own SecContext when it has one, otherwise the context
        # the joined GROUP is bound to — 5.4.1.7/.9 leave `SecContext=""` on
        # a telegram that is nonetheless protected under the group's RX
        # context (see joined_group_with_context).
        ctx_alias = msg.sec_context or group.context_alias or ""
        capture = self._group_capture
        self._group_capture = None
        if capture is None:
            oscore_ctx = self.provisioning.resolve_context(ctx_alias)
            addr = self._group_address_for(group)
            if oscore_ctx is None or addr is None:
                return StepResult(
                    ok=False, item=msg,
                    reason=f"cannot listen for a group telegram on "
                    f"'{ctx_alias}': no context or group address")
            timeout = msg.ack_chk_timeout or 5.0
            capture = self.coap.arm_group_listener(oscore_ctx, addr,
                                                  timeout=timeout)
            self._trigger_sensor(group, timeout)
        resp = capture.result()
        if resp is None:
            return StepResult(
                ok=False, item=msg,
                reason=f"expected a group telegram from the DUT on the "
                f"'{ctx_alias}' context's group address but none "
                f"arrived")

        # A DUT-originated telegram is a REQUEST (its `Code` is a method
        # name), so there is no response code to assert — clear it rather
        # than leaving the previous step's code to be re-checked.
        self._last_response = resp.payload
        self._last_response_code = None
        self._last_response_type = None
        self._last_request_path = msg.path or "k"
        if getattr(resp, "echo_value", None):
            self._last_echo = resp.echo_value
        # Keep the wire details of the DUT's telegram: a following tool-sent
        # 4.01 challenge has to quote its kid, PIV and token back verbatim
        # (see _send_echo_challenge).
        self._last_group_telegram = resp
        decoded = _decode_body(resp.payload, msg.cf, self._last_request_path)
        _dbg_group(f"[group] captured code={resp.code_class}."
                   f"{resp.code_detail:02d} payload={resp.payload.hex()} "
                   f"decoded={decoded!r}")
        result = match_content(msg.content, resp.payload,
                               decoded_body=decoded, variables=self.variables)
        if not result.ok:
            return StepResult(ok=False, item=msg, reason=result.reason)
        self._apply_wait(msg)
        return StepResult(ok=True, item=msg)

    def _expect_unicast_telegram(self, msg: CoAPMessage) -> StepResult:
        """Match an s-mode telegram the DUT sent us by UNICAST rather than to
        a group address (EITT 5.4.1.4 and 5.4.1.15).

        The DUT has to learn our IPv6 address before it can do this: it
        multicasts `GET /.well-known/core?ep=knx://ia.<iid>.<ia>`, we answer
        it (the `OUT <method>` + `IN ... UseLastRxToken` pair, already
        handled by _expect_dut_request/_answer_last_rx_request), and the
        telegram follows as a unicast CON POST /k. traces/5_4_1.xml records
        the whole chain: TelNo 321 (query) -> 322 (our answer) -> 323 (the
        telegram, CON, dst=BDUT, `LocaIP` = the tool's own address:port).

        The context is the step's own SecContext. EITT relabels these
        telegrams — 5.4.1.4's step says `MessagingTestUnicast02` while the
        trace records `MessagingTestUnicast01` — but the two aliases are
        mirror views of ONE OSCORE pair (`01.sender_key == 02.recipient_key`,
        shared common_iv), so decrypting under either works. Where the step
        carries no context at all (5.4.1.15's, which the trace also records
        with an empty SecContext), fall back to whatever unicast context the
        collection has provisioned.
        """
        # Prefer the read armed before the discovery answer — the telegram
        # arrives in that window, so starting one only now would miss it
        # (see _arm_unicast_for_next).
        #
        # 5.4.1.4 has NO discovery pair to arm on — its items 52/53 are
        # Activate="no" because the device already knows our address from an
        # earlier TestCase — so the DUT answers our group READ (`st: r`,
        # item 50) with a unicast `st: a` telegram from inside its own request
        # handler, in flight while we are still matching item 50's ACK. That
        # one is caught by CoapClient's `_pending_requests` buffer instead:
        # _recv_oscore_response now keeps an unsolicited REQUEST it would
        # otherwise have discarded as a stale token, and this listener drains
        # that buffer before touching the socket.
        #
        # Arming ahead of every request in _prepare_for_request does NOT work
        # as an alternative: the armed read owns the shared client socket and
        # swallows ordinary request/response traffic, taking 5.4.1 from 14
        # passed down to 12 (5.4.1.11 and 5.4.1.15 regress). Don't retry it.
        listener = self._unicast_capture
        self._unicast_capture = None
        if listener is None:
            ctx_alias = msg.sec_context or ""
            oscore_ctx = self.provisioning.resolve_context(ctx_alias)
            if oscore_ctx is None:
                oscore_ctx = self.provisioning.any_unicast_context()
            if oscore_ctx is None:
                return StepResult(
                    ok=False, item=msg,
                    reason=f"expected a unicast telegram from the DUT but no "
                    f"OSCORE context is available to decrypt it "
                    f"(step SecContext={ctx_alias!r})")
            # Be generous with the wait. The scripted TimeToNext is 5s, but
            # the DUT has to finish its own address-resolution handshake
            # first, and measured end-to-end that took ~7.5s from our
            # discovery reply — the device itself allows 25s for that
            # exchange (oc_knx_client.c
            # DISCOVERY_RESPONSE_MESSAGE_TIMEOUT_SECONDS), so the script's 5s
            # describes the gap to the NEXT step, not how long the telegram
            # may take to appear.
            # substitute() before parsing — a raw "#VAR" yields 0.0 and
            # collapses this to the 5.0 fallback (finding F13).
            scripted = msg.ack_chk_timeout or _parse_time_to_next(
                self.variables.substitute(msg.time_to_next)) or 5.0
            listener = self.coap.recv_unicast_telegram(
                oscore_ctx, timeout=max(scripted, _UNICAST_TELEGRAM_TIMEOUT),
                path=msg.path or None)
        resp = listener.result()
        if resp is None:
            seen = getattr(listener, "_seen", 0)
            skipped = getattr(listener, "_skipped", 0)
            return StepResult(
                ok=False, item=msg,
                reason="expected a unicast s-mode telegram from the DUT but "
                f"none arrived ({seen} datagram(s) seen on our socket, "
                f"{skipped} did not decrypt under the bound context)")

        self._last_group_telegram = resp
        # A DUT-originated telegram is a REQUEST (its `Code` is a method
        # name), so there is no response code to assert — clear it rather
        # than leaving the previous step's code to be re-checked.
        self._last_response = resp.payload
        self._last_response_code = None
        self._last_response_type = None
        self._last_request_path = msg.path or "k"
        decoded = _decode_body(resp.payload, msg.cf, self._last_request_path)
        _dbg_group(f"[group] unicast telegram code={resp.code_class}."
                   f"{resp.code_detail:02d} payload={resp.payload.hex()} "
                   f"decoded={decoded!r}")
        result = match_content(msg.content, resp.payload,
                               decoded_body=decoded, variables=self.variables)
        if not result.ok:
            return StepResult(ok=False, item=msg, reason=result.reason)
        self._apply_wait(msg)
        return StepResult(ok=True, item=msg)

    def _expect_response(self, msg: CoAPMessage) -> StepResult:
        """An 'OUT' CoAPMessage describes what the previous request's
        response must look like — matched against the response captured
        by the immediately preceding _send_request call, since EITT
        always pairs one IN request with one OUT expectation.

        Two exceptions:
        - when the SecContext is a joined multicast group's RX context, the
          DUT answered on the GROUP address instead and the telegram was
          captured by a listener armed before the request (see
          _expect_group_telegram);
        - when this OUT step is a REQUEST the DUT sends to US, flagged by
          `self._pending_dut_request` (set by run() when it sees the
          `OUT <method>` + `IN ... UseLastRxToken="yes"` pair). The
          discriminator has to be that pairing, NOT "Code is a method":
          103 of section 5.3's OUT steps are `OUT POST` group telegrams
          (5.3.19.5's 100 responses, 5.3.17.6's 3), and routing on the code
          alone hijacked every one of them."""
        if self._pending_dut_request:
            self._pending_dut_request = False
            return self._expect_dut_request(msg)

        group = self.provisioning.group_for_rx_context(msg.sec_context)
        if group is None and not msg.sec_context and _is_method_code(msg.code):
            # A DUT-ORIGINATED telegram whose step carries no SecContext of
            # its own (5.4.1.7's triggered sensor write, 5.4.1.9's init-flag
            # read on startup). `Code` being a METHOD rather than a 2.xx/4.xx
            # is what makes it a telegram and not a response — the same
            # discriminator _expect_response documents above — so this can
            # only divert steps that were never matchable against
            # _last_response anyway. The context comes from the joined
            # group's binding; see joined_group_with_context.
            group = self.provisioning.joined_group_with_context()
        if group is not None:
            return self._expect_group_telegram(msg, group)

        if _is_method_code(msg.code):
            # A DUT-originated telegram that is NOT on a group address — the
            # DUT sends it to us by UNICAST (5.4.1.4's group-read answer,
            # 5.4.1.15's triggered write). No joined group matched above, and
            # `Code` is a method, so there is no prior response this could be
            # matched against; see _expect_unicast_telegram.
            return self._expect_unicast_telegram(msg)

        # --- assert the RESPONSE CODE before looking at the body ---------
        # This has to happen here, not inside match_content: 176 of the
        # active OUT steps expect a 4.xx/5.xx and EVERY one of them has an
        # empty or wildcard <Content>, so the code is their only assertion.
        # Checking it also distinguishes "device answered with an empty
        # body" from "device never answered" — both reach this point as
        # b"", and without the code a timed-out request satisfies every
        # empty-<Content> step.
        expected_codes = _expected_response_codes(msg.code)
        if expected_codes:
            actual = self._last_response_code
            if actual is None:
                # "No response" has two very different causes, and saying
                # "timed out" for both sends you hunting a device bug when
                # the fault is our own correlation logic (T10). If any
                # datagram DID arrive and we rejected it, name it.
                discards = getattr(self.coap, "_last_discards", None) or []
                if discards:
                    detail = ("no USABLE response: "
                              f"{len(discards)} datagram(s) arrived but were "
                              f"discarded [" + "; ".join(discards[:4])
                              + ("; ..." if len(discards) > 4 else "") + "]")
                else:
                    detail = "no response was received (request timed out)"
                return StepResult(
                    ok=False, item=msg,
                    reason=f"expected response code "
                    f"{_format_codes(expected_codes)} but {detail}")
            if actual not in expected_codes:
                # A code the certification body accepts in place of the
                # planned one (see overrides.yaml accepted_response_codes)
                # is not a failure — the reference runs record exactly
                # these substitutions and were accepted.
                actual_str = f"{actual[0]}.{actual[1]:02d}"
                accepted = self.overrides.accepted_response_codes
                if not any(actual_str in accepted.get(
                        f"{c}.{d:02d}", ()) for c, d in expected_codes):
                    return StepResult(
                        ok=False, item=msg,
                        reason=f"Message code differs, planned: "
                        f"{_format_codes(expected_codes)}, received: "
                        f"{actual_str}")

        # --- assert the RESPONSE TYPE (ACK/NON/CON) ----------------------
        # 1324 of the 1457 active OUT rows say Type="ACK" — a piggybacked
        # response (RFC 7252 5.2.1) — plus 130 NON and 2 CON, and the traces
        # confirm the DUT really answers that way. Without this a device
        # replying NON where the script demands a piggybacked ACK passes:
        # an ACK also discharges the request's reliability obligation, a NON
        # does not. EITT asserts type and code in ONE evaluation — see
        # traces/5_4_1.xml: "Message code differs, planned: 4.00, received:
        # 4.03  Message type differs, planned: NON, received: ACK" — so the
        # wording below deliberately mirrors it. See finding F12.
        expected_type = _coap_msg_type_expected(msg.type)
        actual_type = self._last_response_type
        if (expected_type is not None and actual_type is not None
                and actual_type != expected_type
                # A planned ACK satisfied by a CON is the SEPARATE-response
                # pattern (RFC 7252 §5.2.2): the device could not answer
                # within the ACK timeout, so it sent an empty ACK first and
                # the real response later as its own CON — which our client
                # handles correctly (empty ACK skipped, CON taken and
                # ACKed). That is a legitimate, spec-permitted way to answer
                # a CON request; the template writes "ACK" because that is
                # what the reference device happened to do, not because the
                # separate form is a defect. Observed once, on 5.3.1.2b's
                # deliberately-wrong confirmP, where the device does the
                # expensive SPAKE2+ verification before rejecting.
                and not (expected_type == ACK and actual_type == CON)):
            return StepResult(
                ok=False, item=msg,
                reason=f"Message type differs, planned: "
                f"{_MSG_TYPE_NAMES.get(expected_type, expected_type)}, "
                f"received: "
                f"{_MSG_TYPE_NAMES.get(actual_type, actual_type)}")

        response_payload = self._last_response or b""
        # An 'OUT' message has no Path of its own — the schema lookup
        # keys on the immediately preceding request's path instead.
        decoded = _decode_body(response_payload, msg.cf,
                               self._last_request_path)

        if msg.eval_response_fn:
            hook = get_hook(msg.eval_response_fn)
            response_json = _response_as_json_text(
                response_payload, self._last_request_path,
                msg.cf or "application/cbor")
            ok = hook(self.hook_ctx, self._last_planned or "", response_json)
            if not ok:
                return StepResult(
                    ok=False, item=msg,
                    reason=f"EvalResponseFn '{msg.eval_response_fn}' failed: "
                    f"{self.hook_ctx.last_error}")
            self._apply_wait(msg)
            return StepResult(ok=True, item=msg)

        result = match_content(msg.content, response_payload,
                               decoded_body=decoded, variables=self.variables)
        if not result.ok:
            return StepResult(ok=False, item=msg, reason=result.reason)

        self._apply_wait(msg)
        return StepResult(ok=True, item=msg)

    def _apply_wait(self, msg: CoAPMessage) -> None:
        """Sleep this message's TimeToNext before moving on — but only when
        its `Wait` attribute says to.

        `Wait="no"` (527 of the template's 3105 CoAPMessages) means the
        TimeToNext is a TIMEOUT, not a delay: continue as soon as the
        expected telegram arrives instead of sitting out the budget. The
        recorded certification run proves it — 5.3.17.6's three group-telegram
        steps each declare `TimeToNext="00:02:00"` (six minutes in total),
        yet the wall-clock gaps in traces/5_3.xml (TelNo 761-764, whose
        TimeToNext field is a CLOCK reading, not a duration) are 0.1s, 0.1s
        and 0.1s. Those two minutes are EITT's allowance for a human to walk
        over and press the sensor, released the moment the telegram lands.

        Honouring this is worth more than it looks: at TimeScale 0.05 those
        three steps alone were 18s of that test's 25s, and by construction
        we have ALREADY received the telegram by the time we get here.
        """
        # Comments carry neither Wait nor TimeToNext — an operator prompt's
        # pause is expressed as the separate `@[w"..."` step that follows it
        # (5.4.1.8 item 9). _run_manual_prompt calls this for an
        # observation-only prompt, so accept and ignore those rather than
        # AttributeError on Comment.wait.
        if not getattr(msg, "wait", None):
            return
        ttn = self.variables.substitute(msg.time_to_next)
        seconds = _parse_time_to_next(ttn) * self.time_scale
        if seconds:
            self.sleep(seconds)


_TTN_RE = re.compile(r"^(\d+):(\d+):(\d+(?:\.\d+)?)$")


def _parse_time_to_next(value: Optional[str]) -> float:
    """Parse a TimeToNext value: either 'HH:MM:SS.f' or a millisecond
    integer left over from an unresolved #VAR (NumberField defaults for
    TimeToNextTelegram fields are plain milliseconds, e.g. "1000")."""
    if not value:
        return 0.0
    m = _TTN_RE.match(value)
    if m:
        h, mi, s = m.groups()
        return int(h) * 3600 + int(mi) * 60 + float(s)
    try:
        return float(value) / 1000.0
    except ValueError:
        return 0.0
