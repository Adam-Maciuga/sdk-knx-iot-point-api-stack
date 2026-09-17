"""Fail-closed behaviour of the EITT interpreter: F6, F9, F16, M7/M8.

No DUT, no Docker, no network. Every test either parses a small XML
snippet through `interpreter.model` or drives a `SequenceRunner` function
with a fake, the same shape the other harness tests use.

The single property under test throughout is that an input the
interpreter cannot honour produces a NAMED failure rather than a quiet
default — a silently-dropped modifier turns an adversarial TestCase into
a positive one that the device passes by behaving correctly.
"""

from __future__ import annotations

import os
import xml.etree.ElementTree as ET

import pytest

from interpreter import runner as R
from interpreter.model import CoAPMessage, DnsMessage, parse_eitt_xml
from interpreter.provisioning import ProvisioningEngine
from interpreter.runner import (DEFAULT_RESPONSE_TIMEOUT, MalformedXmlValue,
                                PreparationRegistry, SequenceRunner,
                                _content_format_code, _parse_time_to_next,
                                _resolve_crit_option, _response_timeout,
                                _safe_int, _unsupported_attributes)
from interpreter.variables import VariableTable
from transport.coap_client import (_parse_oscore_option,
                                   _parse_oscore_option_full)

TEMPLATE = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "data", "KnxConformanceTestTemplate-PointAPI.xml")

NS = 'xmlns="http://knx.org/xml/eitt/4.3"'


def _message(attrs: str, body: str = "") -> CoAPMessage:
    """Parse one <CoAPMessage> out of a minimal document."""
    el = ET.fromstring(f"<CoAPMessage {NS} {attrs}>{body}</CoAPMessage>")
    return CoAPMessage.from_xml(el)


def _dns(attrs: str) -> DnsMessage:
    el = ET.fromstring(f"<DnsMessage {NS} {attrs}/>")
    return DnsMessage.from_xml(el)


class FakeCoap:
    """Enough of CoapClient for the paths these tests reach."""
    host = "fd00::1"
    port = 5683

    def __init__(self):
        self.sent = []

    def drain_socket(self, timeout=0.1):
        pass


@pytest.fixture(scope="module")
def project():
    return parse_eitt_xml(TEMPLATE)


def _runner(project, coap=None) -> SequenceRunner:
    collection = project.collections[0].name
    variables = VariableTable.for_collection(project, collection)
    return SequenceRunner(coap=coap or FakeCoap(), project=project,
                          variables=variables,
                          provisioning=ProvisioningEngine(variables),
                          preparations=PreparationRegistry(project),
                          sleep=lambda _s: None)


# ==========================================================================
# F6 — attributes parsed into the model but never evaluated
# ==========================================================================

def test_f6_plain_row_is_clean():
    """The gate must be silent on the shape 3000+ template rows have, or
    it would fail the whole suite rather than the rows it is about."""
    msg = _message('ID="1" CWay="IN" Code="GET" Path="dev/sn" Type="CON"')
    assert _unsupported_attributes(msg) == []


def test_f6_seq_num_method_zero_is_inert():
    """All 74 uses in the template carry "0" — the UI enum's "not set"
    index. Failing those would fail 35 active TestCases."""
    for value in ("0", "not set", ""):
        msg = _message(f'ID="1" CWay="IN" Code="GET" SeqNumMethod="{value}"')
        assert _unsupported_attributes(msg) == [], value


def test_f6_unknown_seq_num_method_fails_and_names_the_value():
    msg = _message('ID="1" CWay="IN" Code="GET" SeqNumMethod="3"')
    reasons = _unsupported_attributes(msg)
    assert len(reasons) == 1
    assert "SeqNumMethod" in reasons[0] and "'3'" in reasons[0]


def test_f6_cycles_star_is_accepted():
    """5.4.1.15's only use. "Unlimited" is satisfied by matching the
    first occurrence, which is what the runner already does."""
    msg = _message('ID="1" CWay="OUT" Code="GET" Cycles="*"')
    assert _unsupported_attributes(msg) == []


def test_f6_finite_cycle_count_fails():
    msg = _message('ID="1" CWay="OUT" Code="GET" Cycles="3"')
    reasons = _unsupported_attributes(msg)
    assert len(reasons) == 1
    assert "Cycles" in reasons[0] and "'3'" in reasons[0]


def test_f6_observe_fails():
    """The interpreter never sends the CoAP Observe option, so an observe
    registration would go out as a plain GET."""
    msg = _message('ID="1" CWay="IN" Code="GET" Observe="y" ObserveValue="0"')
    reasons = _unsupported_attributes(msg)
    assert len(reasons) == 1 and "Observe" in reasons[0]


def test_f6_check_rx_kid_is_no_longer_gated():
    """CheckRxKid is EVALUATED now (see the F6 CheckRxKid block below),
    so the fail-closed gate must be silent on it — a row that both gates
    AND asserts would report the attribute twice and never reach the
    assertion."""
    msg = _message('ID="1" CWay="OUT" Code="4.01" CheckRxKid="yes" '
                   'RxKid="random"')
    assert _unsupported_attributes(msg) == []


def test_f6_secure_is_deliberately_not_gated():
    """897 uses, and the obvious assertion is provably WRONG (it inverts
    the 8 negative tests). Gating it would fail 82 TestCases, so it stays
    recorded-but-unevaluated rather than guessed at."""
    msg = _message('ID="1" CWay="OUT" Code="2.05" Secure="yes"')
    assert _unsupported_attributes(msg) == []


def test_f6_deactivated_row_is_not_gated(project):
    """A row that is never played cannot fail. The activate check sits
    ahead of the gate in _run_coap_message."""
    msg = _message('ID="1" Activate="no" CWay="IN" Code="GET" Observe="y"')
    assert _runner(project)._run_coap_message(msg).ok is True


def test_f6_gate_fails_the_step_and_names_the_row(project):
    msg = _message('ID="THE-ID" CWay="IN" Code="GET" Path="k" Observe="y"')
    result = _runner(project)._run_coap_message(msg)
    assert result.ok is False
    assert "THE-ID" in result.reason
    assert "Observe" in result.reason


def test_f6_shipped_template_has_exactly_the_known_gated_rows(project):
    """The whole point of a fail-closed gate is knowing what it costs.
    If a future template revision adds an ungateable attribute, this test
    names the TestCase before a device run does."""
    hit = set()
    for _coll, _suite, tc in project.iter_test_cases():
        for item in tc.sequence:
            if isinstance(item, CoAPMessage) and item.activate:
                if _unsupported_attributes(item):
                    hit.add(tc.name)
    # Only the 5.4.2 cases remain, for Observe; they are skipped via
    # overrides.yaml, so they never reach a device. 5.3.17.3 used to be
    # here for CheckRxKid/RxKid and is now really asserted instead — see
    # the F6 CheckRxKid block below.
    assert not any(name.startswith("5.3.17.3") for name in hit), sorted(hit)
    assert all(name.startswith("5.4.2") for name in hit), sorted(hit)


# ==========================================================================
# F6 CheckRxKid — the assertion that replaced the gate
# ==========================================================================
#
# 5.3.17.3's two OUT rows are the DEVICE's 4.01 Unauthorized + Echo
# challenge answering the tool's replayed multicast group write. The
# device protects that 4.01 under a BRAND NEW sender context whose OSCORE
# ID Context is OC_ECHO_KID_CTX_LEN freshly generated random bytes
# (oc_oscore_engine.c, `unicast_echo_response_by_mc`), and the real EITT
# tool logs "SPECIAL DECODING: use received random kid_context" at exactly
# that point. So RxKid="random" asserts: a kid context was carried, it was
# non-empty, and it was NOT the sending context's own static one.


class _FakeOscoreContext:
    """Just the one attribute `_own_id_context` reads off a real
    knx_oscore.OscoreContext."""

    def __init__(self, id_context=None):
        self.id_context = id_context


_MCAST_TX_ID_CTX = b"\x10\x2a\x3b\x4c\x5d\x6e\x7f"
_RANDOM_KID_CTX = bytes(range(10))       # 10 bytes, as the device sends


def _rx_kid_runner(project, *, received, own=_MCAST_TX_ID_CTX):
    """A runner primed as if a 4.01 NON had just been received under the
    MulticastTx context, carrying `received` as its outer kid context."""
    run = _runner(project)
    run.provisioning.set_context("MulticastTx", _FakeOscoreContext(own))
    run._last_response = b""
    run._last_response_code = (4, 1)
    run._last_response_type = R.NON
    run._last_response_kid_context = received
    return run


_RX_KID_ROW = ('ID="RXKID-ROW" CWay="OUT" Type="NON" Code="4.01" '
               'Secure="yes" SeqNoFn="asinrequest" Echo="sync" '
               'CheckRxKid="yes" RxKid="{rx}" SecContext="MulticastTx"')


def test_check_rx_kid_random_context_passes(project):
    """The shape the device really produces: a fresh 10-byte ID Context
    that is not the group's provisioned one."""
    run = _rx_kid_runner(project, received=_RANDOM_KID_CTX)
    result = run._expect_response(_message(_RX_KID_ROW.format(rx="random")))
    assert result.ok is True, result.reason


def test_check_rx_kid_missing_context_fails(project):
    """A 4.01 with no ID Context at all is the UNICAST echo branch
    (oc_oscore_engine.c, `unicast_echo_response_by_uc`), which never
    generates one — i.e. the device did not take the multicast path the
    TestCase is about."""
    run = _rx_kid_runner(project, received=None)
    result = run._expect_response(_message(_RX_KID_ROW.format(rx="random")))
    assert result.ok is False
    assert "CheckRxKid" in result.reason
    assert "no kid context" in result.reason


def test_check_rx_kid_empty_context_fails(project):
    """Present-but-empty is the same failure as absent: an empty ID
    Context cannot be a randomly generated one."""
    run = _rx_kid_runner(project, received=b"")
    result = run._expect_response(_message(_RX_KID_ROW.format(rx="random")))
    assert result.ok is False
    assert "no kid context" in result.reason


def test_check_rx_kid_static_context_fails_and_names_it(project):
    """The device echoing the group's OWN provisioned ID Context back is
    precisely the "not random" case, and the reason has to show the value
    so a wire capture can be matched against it."""
    run = _rx_kid_runner(project, received=_MCAST_TX_ID_CTX)
    result = run._expect_response(_message(_RX_KID_ROW.format(rx="random")))
    assert result.ok is False
    assert "static context" in result.reason
    assert _MCAST_TX_ID_CTX.hex() in result.reason
    assert "MulticastTx" in result.reason


def test_check_rx_kid_unknown_rx_kid_value_fails(project):
    """Fail-closed, exactly as the gate was: a value this interpreter has
    never seen must not be passed by an assertion nobody made."""
    run = _rx_kid_runner(project, received=_RANDOM_KID_CTX)
    result = run._expect_response(
        _message(_RX_KID_ROW.format(rx="something-else")))
    assert result.ok is False
    assert "RxKid" in result.reason and "unsupported" in result.reason
    assert "something-else" in result.reason


def test_check_rx_kid_without_the_flag_is_not_asserted(project):
    """CheckRxKid="no"/absent means the row says nothing about the kid
    context, so a missing one must not fail it."""
    run = _rx_kid_runner(project, received=None)
    msg = _message('ID="1" CWay="OUT" Type="NON" Code="4.01" '
                   'SecContext="MulticastTx"')
    assert run._expect_response(msg).ok is True


def test_check_rx_kid_runs_after_the_code_assertion(project):
    """Ordering matters: a device that answered 2.04 instead of 4.01 has
    to be reported as the wrong CODE, not as a missing kid context."""
    run = _rx_kid_runner(project, received=None)
    run._last_response_code = (2, 4)
    result = run._expect_response(_message(_RX_KID_ROW.format(rx="random")))
    assert result.ok is False
    assert "Message code differs" in result.reason


def test_check_rx_kid_template_rows_are_the_known_two(project):
    """The census the gate used to provide. If a template revision adds a
    CheckRxKid row elsewhere — or with an RxKid other than "random" — this
    names it here rather than against a device."""
    rows = []
    for _coll, _suite, tc in project.iter_test_cases():
        for item in tc.sequence:
            if (isinstance(item, CoAPMessage) and item.activate
                    and item.check_rx_kid):
                rows.append((tc.name, item.rx_kid, item.sec_context))
    assert len(rows) == 2, rows
    assert all(name.startswith("5.3.17.3") for name, _, _ in rows), rows
    assert all(rx == "random" for _, rx, _ in rows), rows
    assert all(ctx == "MulticastTx" for _, _, ctx in rows), rows


# ==========================================================================
# The client-side OSCORE option parser CheckRxKid rests on
# ==========================================================================

def test_oscore_option_parser_extracts_the_kid_context():
    """Hand-built option: flags 0x19 = kid context present (0x10) + kid
    present (0x08) + PIV length 1, then the PIV, then the length-prefixed
    kid context, then the kid running to the end."""
    piv = b"\x22"
    kid_ctx = bytes(range(10))
    kid = b"\xaa\xbb\xcc"
    value = bytes([0x19]) + piv + bytes([len(kid_ctx)]) + kid_ctx + kid

    assert _parse_oscore_option_full(value) == (kid, piv, kid_ctx)
    # The two-field helper every other caller uses keeps its contract.
    assert _parse_oscore_option(value) == (kid, piv)


def test_oscore_option_parser_without_a_kid_context():
    """Flags 0x09 = kid, PIV length 1, no kid context — an ordinary
    protected request, and what the unicast 4.01 echo branch sends."""
    value = bytes([0x09]) + b"\x05" + b"\xaa\xbb"
    assert _parse_oscore_option_full(value) == (b"\xaa\xbb", b"\x05", None)


def test_oscore_option_parser_empty_kid_context_is_empty_not_none():
    """A zero-length kid context is a thing the flags byte can claim, and
    it must stay distinguishable from "no kid context field at all": the
    CheckRxKid assertion rejects both, but on the wire they are different
    messages."""
    value = bytes([0x11]) + b"\x01" + bytes([0])
    kid, piv, ctx = _parse_oscore_option_full(value)
    assert ctx == b"" and ctx is not None
    assert piv == b"\x01" and kid is None


def test_oscore_option_parser_truncated_option_does_not_raise():
    """These bytes come off the wire, including datagrams meant for other
    contexts; a short option must yield None rather than an IndexError
    that reads as a device fault."""
    assert _parse_oscore_option_full(b"") == (None, None, None)
    # Flags claim a kid context but the option ends after the PIV.
    assert _parse_oscore_option_full(bytes([0x11]) + b"\x07") == (
        None, b"\x07", None)


# ==========================================================================
# F9 — malformed XML values must abort, not degrade
# ==========================================================================

def test_f9_safe_int_still_accepts_blank_as_zero():
    assert _safe_int("") == 0
    assert _safe_int(None) == 0
    assert _safe_int(" 42 ") == 42


def test_f9_safe_int_raises_and_names_attribute_and_value():
    with pytest.raises(MalformedXmlValue) as exc:
        _safe_int("twelve", attribute="Port")
    assert "Port" in str(exc.value) and "twelve" in str(exc.value)


def test_f9_content_format_names_are_still_resolved():
    assert _content_format_code("application/cbor") == 60
    assert _content_format_code("application/link-format") == 40
    assert _content_format_code("60") == 60
    assert _content_format_code("") is None
    assert _content_format_code(None) is None


@pytest.mark.parametrize("value", ["a", "r"])
def test_f9_corrupt_accepted_cf_raises(value):
    """The shipped template really does carry AcceptedCF="a"/"r" — single
    letters of `application/...`. Dropping the Accept option silently is
    what made those rows assert less than they say."""
    with pytest.raises(MalformedXmlValue) as exc:
        _content_format_code(value, attribute="AcceptedCF")
    assert "AcceptedCF" in str(exc.value)
    assert repr(value) in str(exc.value)


def test_f9_corrupt_accepted_cf_fails_the_step_naming_the_row(project):
    msg = _message('ID="ROW-15" CWay="IN" Code="GET" Path="fp/g/1" '
                   'AcceptedCF="r"')
    result = _runner(project)._run_item(msg)
    assert result.ok is False
    assert "ROW-15" in result.reason
    assert "AcceptedCF" in result.reason and "'r'" in result.reason


def test_f9_crit_option_number_must_parse():
    """5.10.5.3 exists to prove an unknown CRITICAL option draws 4.02.
    Dropping the option sends a clean request the device answers 2.05,
    and the test then fails blaming the device."""
    good = _message('ID="1" CWay="IN" Code="GET" CritOptNo="65123" '
                    'CritOptValue="ABCD"')
    assert _resolve_crit_option(good) == (65123, b"\xab\xcd")
    bad = _message('ID="1" CWay="IN" Code="GET" CritOptNo="sixty" '
                   'CritOptValue="ABCD"')
    with pytest.raises(MalformedXmlValue) as exc:
        _resolve_crit_option(bad)
    assert "CritOptNo" in str(exc.value)


def test_f9_time_to_next_forms_still_parse():
    assert _parse_time_to_next("00:00:02.5") == pytest.approx(2.5)
    assert _parse_time_to_next("500") == pytest.approx(0.5)
    assert _parse_time_to_next("") == 0.0
    assert _parse_time_to_next(None) == 0.0


def test_f9_time_to_next_keeps_unresolved_var_as_zero():
    """An unresolved #VAR is already reported BY NAME through
    VariableTable.unresolved (F5/F13). Raising here would replace that
    precise report with a vaguer one."""
    assert _parse_time_to_next("#GENERAL_TIME_TO_NEXT") == 0.0


def test_f9_time_to_next_raises_on_real_garbage():
    with pytest.raises(MalformedXmlValue) as exc:
        _parse_time_to_next("soon", attribute="TimeToNext")
    assert "TimeToNext" in str(exc.value) and "soon" in str(exc.value)


def test_f9_invalid_auth_tag_must_be_hex(project):
    """5.3.18.1's whole point. A typo here turned a negative test into a
    positive one the device passes by working correctly."""
    run = _runner(project)
    ok = _message('ID="1" CWay="IN" Code="POST" InvalidAuthTag="AABB"')
    _, _, tag, _ = run._resolve_oscore_overrides(ok, object())
    assert tag == b"\xaa\xbb"

    bad = _message('ID="TAG-ROW" CWay="IN" Code="POST" InvalidAuthTag="zz"')
    with pytest.raises(MalformedXmlValue) as exc:
        run._resolve_oscore_overrides(bad, object())
    assert "TAG-ROW" in str(exc.value) and "InvalidAuthTag" in str(exc.value)


def test_f9_fixed_seq_no_must_be_an_integer(project):
    run = _runner(project)
    ok = _message('ID="1" CWay="IN" Code="POST" SeqNoFn="fix" SeqNo="7"')
    override, _, _, _ = run._resolve_oscore_overrides(ok, object())
    assert override == 7

    bad = _message('ID="1" CWay="IN" Code="POST" SeqNoFn="fix" SeqNo="x7"')
    with pytest.raises(MalformedXmlValue) as exc:
        run._resolve_oscore_overrides(bad, object())
    assert "SeqNo" in str(exc.value)


def test_f9_echo_value_must_be_hex(project):
    run = _runner(project)
    ok = _message('ID="1" CWay="IN" Code="POST" Echo="fix" EchoValue="FF"')
    assert run._resolve_echo(ok) == b"\xff"

    bad = _message('ID="ECHO-ROW" CWay="IN" Code="POST" Echo="fix" '
                   'EchoValue="GG"')
    with pytest.raises(MalformedXmlValue) as exc:
        run._resolve_echo(bad)
    assert "ECHO-ROW" in str(exc.value) and "EchoValue" in str(exc.value)


def test_f9_shipped_template_malformed_values_are_the_known_three(project):
    """Same guard as the F6 census: the cost of failing closed has to be
    knowable from a host run, not discovered against a device."""
    hits = []
    for _coll, _suite, tc in project.iter_test_cases():
        for item in tc.sequence:
            if not isinstance(item, CoAPMessage) or not item.activate:
                continue
            for attr, value in (("CF", item.cf),
                                ("AcceptedCF", item.accepted_cf)):
                try:
                    _content_format_code(value, attribute=attr)
                except MalformedXmlValue:
                    hits.append((tc.name, item.cway, attr, value))
    assert len(hits) == 3, hits
    # Only ONE of the three is on a request row, so only one can reach a
    # parser at run time: AcceptedCF is the CoAP Accept option and an OUT
    # row never sends one.
    requests = [h for h in hits if h[1] == "IN"]
    assert len(requests) == 1
    assert requests[0][0].startswith("5.5.4.1")


# ==========================================================================
# F16 — one documented response-timeout default
# ==========================================================================

def test_f16_default_is_five_seconds_without_the_env_var(monkeypatch):
    monkeypatch.delenv("COAP_TIMEOUT", raising=False)
    assert R._default_response_timeout() == 5.0


def test_f16_default_comes_from_coap_timeout(monkeypatch):
    monkeypatch.setenv("COAP_TIMEOUT", "12.5")
    assert R._default_response_timeout() == 12.5


@pytest.mark.parametrize("value", ["", "abc", "0", "-3"])
def test_f16_unusable_env_value_falls_back(monkeypatch, value):
    """A typo'd or non-positive COAP_TIMEOUT must not silently produce a
    zero-second budget, which would report every device as silent."""
    monkeypatch.setenv("COAP_TIMEOUT", value)
    assert R._default_response_timeout() == 5.0


def test_f16_row_without_ack_chk_timeout_uses_the_default():
    msg = _message('ID="1" CWay="OUT" Code="2.05"')
    assert msg.ack_chk_timeout is None
    assert _response_timeout(msg) == DEFAULT_RESPONSE_TIMEOUT


def test_f16_row_timeout_wins_over_the_default():
    """AckChkTimeOut is EITT's "Wait for Response Timeout". It sits on 9
    template rows (10/15/20 s) and on none of the active OUT rows, so it
    changes nothing today — but a template revision that sets it is now
    honoured without a code change."""
    msg = _message('ID="1" CWay="OUT" Code="2.05" AckChkTimeOut="20"')
    assert msg.ack_chk_timeout == 20.0
    assert _response_timeout(msg) == 20.0


def test_f16_no_row_at_all_still_yields_the_default():
    assert _response_timeout(None) == DEFAULT_RESPONSE_TIMEOUT


def test_f16_no_bare_five_second_literal_survives_in_the_runner():
    """The regression this item is really about: seven separate `or 5.0`
    literals meant "the timeout" was never one decision. Raising it for
    slow hardware had to mean finding all seven."""
    import inspect
    source = inspect.getsource(R)
    body = source[source.index("class SequenceRunner"):]
    assert "or 5.0" not in body
    assert "max(timeout, 5.0)" not in body


# ==========================================================================
# M7 / M8 — an mDNS step that captured nothing
# ==========================================================================

class _Listener:
    """Stand-in for mdns_client.MdnsListener."""

    def __init__(self, records=(), packets_seen=None):
        self._records = list(records)
        if packets_seen is not None:
            self.packets_seen = packets_seen

    def repeat_query(self, *_a, **_kw):
        pass

    def records(self):
        return list(self._records)


def _patch_listener(monkeypatch, listener):
    from transport import mdns_client
    monkeypatch.setattr(mdns_client, "MdnsListener",
                        lambda *a, **kw: listener)


def test_m7_in_step_with_no_answer_fails(monkeypatch, project):
    """The IN branch returned ok=True unconditionally and deferred the
    verdict to the following OUT step — so an unanswered query whose OUT
    is missing or deactivated was a silent pass."""
    _patch_listener(monkeypatch, _Listener(records=()))
    msg = _dns('ID="Q1" CWay="IN" Type="dnssd" RecordType="PTR" '
               'Service="_knx._udp.local"')
    result = _runner(project)._run_dns_message(msg)
    assert result.ok is False
    assert "_knx._udp.local" in result.reason


def test_m7_in_step_with_an_answer_still_passes(monkeypatch, project):
    from transport.mdns_client import DnsRecord
    rec = DnsRecord(rtype="PTR", name="_knx._udp.local",
                    target="i._knx._udp.local", ttl=4500)
    _patch_listener(monkeypatch, _Listener(records=[rec]))
    msg = _dns('ID="Q1" CWay="IN" Type="dnssd" RecordType="PTR" '
               'Service="_knx._udp.local"')
    assert _runner(project)._run_dns_message(msg).ok is True


def test_m7_deactivated_in_step_is_untouched(monkeypatch, project):
    _patch_listener(monkeypatch, _Listener(records=()))
    msg = _dns('ID="Q1" Activate="no" CWay="IN" Type="dnssd" '
               'RecordType="PTR" Service="_knx._udp.local"')
    assert _runner(project)._run_dns_message(msg).ok is True


def test_m8_zero_packets_reads_as_a_silent_link():
    msg = _dns('ID="Q1" CWay="IN" Type="dnssd" RecordType="PTR" '
               'Service="_knx._udp.local"')
    reason = SequenceRunner._dns_empty_reason(
        msg, "_knx._udp.local", _Listener(packets_seen=0), elapsed=6.0)
    assert "no datagram arrived at all" in reason


def test_m8_packets_but_no_records_reads_differently():
    """"The deadline expired with zero packets" and "packets arrived but
    none parsed" are the same empty list downstream and point at
    completely different causes."""
    msg = _dns('ID="Q1" CWay="IN" Type="dnssd" RecordType="PTR" '
               'Service="_knx._udp.local"')
    reason = SequenceRunner._dns_empty_reason(
        msg, "_knx._udp.local", _Listener(packets_seen=4), elapsed=6.0)
    assert "4 datagram(s) arrived" in reason
    assert "no datagram arrived at all" not in reason


def test_m8_without_a_packet_counter_infers_from_the_clock():
    """`packets_seen` is a diagnostic the listener may not expose; a
    missing counter must degrade to a weaker message, never to an
    AttributeError."""
    msg = _dns('ID="Q1" CWay="IN" Type="dnssd" RecordType="PTR" '
               'Service="_knx._udp.local"')
    full = SequenceRunner._dns_empty_reason(
        msg, "_knx._udp.local", _Listener(), elapsed=R._MDNS_TIMEOUT)
    assert "no datagram arrived at all" in full

    early = SequenceRunner._dns_empty_reason(
        msg, "_knx._udp.local", _Listener(), elapsed=0.2)
    assert "stopped after 0.2s" in early


def test_m8_reason_rules_out_a_listener_failure():
    """A bind/join failure is RAISED by records(), never reported as an
    empty set — so say so, or the reader hunts a device bug for what is a
    local misconfiguration (or the other way round)."""
    msg = _dns('ID="Q1" CWay="OUT" Type="dnsrecord" RecordType="PTR" '
               'Service="_pm._sub._knx._udp.local"')
    reason = SequenceRunner._dns_empty_reason(
        msg, "_pm._sub._knx._udp.local", _Listener(packets_seen=0),
        elapsed=6.0)
    assert "bound" in reason and "joined" in reason
    assert "announcement" in reason


def test_m8_unsolicited_out_step_with_nothing_fails(monkeypatch, project):
    _patch_listener(monkeypatch, _Listener(records=()))
    msg = _dns('ID="A1" CWay="OUT" Type="dnsrecord" RecordType="PTR" '
               'Service="_pm._sub._knx._udp.local"')
    run = _runner(project)
    run._dns_records = []
    result = run._run_dns_message(msg)
    assert result.ok is False
    assert "captured nothing" in result.reason
