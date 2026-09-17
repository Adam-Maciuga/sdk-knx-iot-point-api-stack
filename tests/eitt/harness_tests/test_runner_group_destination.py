"""Group-destination routing: an EITT `IP=` alias that resolves to an
IPv6 multicast address must go out as a GROUP message, not unicast.

No DUT, no Docker, no network — the runner tests drive a `SequenceRunner`
with a fake CoAP client, and the client tests drive `oscore_request`
against a fake socket.

The bug this file pins down: the template addresses 200 active IN rows to
`IP="BDUT-Multi"`, an alias NEW PROJECT SETUP provisions with
'@@[pahdev"BDUT-Multi;ipaddress=[ff35:30:fd11:9988:7766:0:8000:1]:5683"'
— the DUT's KNX group multicast address. The runner stored the alias and
then ignored it, unicasting every one of those rows to the DUT. The
device accepts a unicast /k write too, so nothing failed; but it raises
the 4.01 Echo challenge under a fresh random OSCORE ID Context ONLY for a
message that arrived on a multicast endpoint (messaging/coap/engine.c,
ECHO_CAUSED_BY_MC_SRC), which is exactly what 5.3.17.3's
`CheckRxKid="yes" RxKid="random"` rows assert on.
"""

from __future__ import annotations

import os
import socket
import struct
import xml.etree.ElementTree as ET

import pytest

from interpreter.model import CoAPMessage, parse_eitt_xml
from interpreter.provisioning import ProvisioningEngine
from interpreter.runner import (PreparationRegistry, SequenceRunner,
                                UnknownDeviceAlias,
                                _resolve_group_destination)
from interpreter.variables import VariableTable
from transport import coap_client as CC
from transport.coap_client import CON, NON, CoapClient, CoapResponse

TEMPLATE = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "data", "KnxConformanceTestTemplate-PointAPI.xml")

NS = 'xmlns="http://knx.org/xml/eitt/4.3"'

# The one provisioning line in NEW PROJECT SETUP, verbatim.
BDUT_MULTI_PROVISION = (
    '@@[pahdev"BDUT-Multi;'
    'ipaddress=[ff35:30:fd11:9988:7766:0:8000:1]:5683"')
GROUP_ADDR = "ff35:30:fd11:9988:7766:0:8000:1"
GROUP_PORT = 5683

DUT_HOST = "fd11:9988:7766::1"


def _message(attrs: str, body: str = "") -> CoAPMessage:
    el = ET.fromstring(f"<CoAPMessage {NS} {attrs}>{body}</CoAPMessage>")
    return CoAPMessage.from_xml(el)


class FakeOscoreContext:
    """Just enough of an OscoreContext for the runner to treat the row as
    secured; the runner never inspects it, it only passes it through."""


class RecordingCoap:
    """Records every request the runner makes, with its destination."""

    host = DUT_HOST
    port = 5683

    def __init__(self, response=None):
        self.calls = []
        self.response = response

    # -- secured path ---------------------------------------------------
    def oscore_request(self, oscore_ctx, method_code, path, **kw):
        self.calls.append(("oscore", method_code, path, kw))
        return self.response

    def complete_oscore_block2(self, resp, *a, **kw):
        self.calls.append(("block2", a, kw))
        return resp

    # -- unsecured path -------------------------------------------------
    def _plain(self, name):
        def call(path, **kw):
            self.calls.append((name, path, kw))
            return self.response
        return call

    def __getattr__(self, name):
        if name in ("get", "post", "put", "delete"):
            return self._plain(name)
        raise AttributeError(name)

    def multicast_get(self, path, **kw):
        self.calls.append(("multicast_get", path, kw))
        return [self.response] if self.response is not None else []

    def drain_socket(self, timeout=0.1):
        pass


@pytest.fixture(scope="module")
def project():
    return parse_eitt_xml(TEMPLATE)


def _runner(project, coap) -> SequenceRunner:
    collection = project.collections[0].name
    variables = VariableTable.for_collection(project, collection)
    return SequenceRunner(coap=coap, project=project, variables=variables,
                          provisioning=ProvisioningEngine(variables),
                          preparations=PreparationRegistry(project),
                          sleep=lambda _s: None)


def _provision_group(runner: SequenceRunner) -> None:
    """Replay NEW PROJECT SETUP's BDUT-Multi line through the engine."""
    assert runner.provisioning.apply(BDUT_MULTI_PROVISION) is not None
    device = runner.provisioning.resolve_device("BDUT-Multi")
    assert device.ip_address == f"[{GROUP_ADDR}]:{GROUP_PORT}"


def _secure(runner: SequenceRunner, alias: str = "GroupTx") -> str:
    """Install a context under `alias` so the row takes the OSCORE path."""
    runner.provisioning.set_context(alias, FakeOscoreContext())
    return alias


# =======================================================================
# (a) BDUT-Multi goes out to the group address, as NON
# =======================================================================

def test_bdut_multi_row_is_sent_to_the_group_address(project):
    coap = RecordingCoap()
    runner = _runner(project, coap)
    _provision_group(runner)
    ctx = _secure(runner)

    msg = _message(
        f'ID="1" CWay="IN" Code="POST" Path="k" Type="NON" '
        f'IP="BDUT-Multi" SecContext="{ctx}" CF="application/cbor"',
        "<Cbor>a0</Cbor>")
    result = runner._send_request(msg)

    assert result.ok, result.reason
    kind, method_code, path, kw = coap.calls[0]
    assert kind == "oscore"
    assert kw["dest"] == (GROUP_ADDR, GROUP_PORT)
    # The row's own context — NON, POST, path and body — is unchanged by
    # the retarget; only the destination moved.
    assert kw["msg_type"] == NON
    assert method_code == 2  # POST
    assert path == "k"


def test_group_row_skips_the_block2_continuation(project):
    """A Block2 continuation GET would go unicast and be a continuation of
    a message the device only ever saw as a group telegram."""
    coap = RecordingCoap(response=CoapResponse(
        code_class=2, code_detail=4, payload=b"", content_format=None,
        msg_type=NON, mid=1, token=b"\x01"))
    runner = _runner(project, coap)
    _provision_group(runner)
    ctx = _secure(runner)
    msg = _message(
        f'ID="1" CWay="IN" Code="POST" Path="k" Type="NON" '
        f'IP="BDUT-Multi" SecContext="{ctx}"', "<Cbor>a0</Cbor>")
    runner._send_request(msg)
    assert [c[0] for c in coap.calls] == ["oscore"]


# =======================================================================
# (b) BDUT — and everything else — stays unicast
# =======================================================================

def test_bdut_row_stays_unicast(project):
    coap = RecordingCoap()
    runner = _runner(project, coap)
    _provision_group(runner)          # the alias exists but is not used
    ctx = _secure(runner)

    msg = _message(
        f'ID="1" CWay="IN" Code="GET" Path="dev/sn" Type="CON" '
        f'IP="BDUT" SecContext="{ctx}"')
    result = runner._send_request(msg)

    assert result.ok, result.reason
    kw = coap.calls[0][3]
    assert kw["dest"] is None
    assert kw["msg_type"] == CON


def test_empty_ip_attribute_stays_unicast(project):
    coap = RecordingCoap()
    runner = _runner(project, coap)
    ctx = _secure(runner)
    msg = _message(f'ID="1" CWay="IN" Code="GET" Path="dev/sn" Type="CON" '
                   f'IP="" SecContext="{ctx}"')
    assert runner._send_request(msg).ok
    assert coap.calls[0][3]["dest"] is None


def test_discovery_scope_aliases_keep_the_plain_multicast_path(project):
    """`_plain_multicast_scope` owns the three well-known scope aliases;
    the new resolver must not claim them."""
    coap = RecordingCoap()
    runner = _runner(project, coap)
    msg = _message('ID="1" CWay="IN" Code="GET" Path=".well-known/core" '
                   'Type="CON" IP="Multicast Site-Local" SecContext=""')
    assert runner._send_request(msg).ok
    assert coap.calls[0][0] == "multicast_get"

    for alias in ("Multicast Link-Local", "Multicast Realm-Local",
                  "Multicast Site-Local"):
        assert _resolve_group_destination(runner.provisioning, alias) is None


def test_invalid_ipaddress_placeholder_stays_unicast(project):
    """5.1.2.x sets 'ipaddress=invalid' on purpose before rediscovering;
    the alias is known, it just has no usable literal right now."""
    runner = _runner(project, RecordingCoap())
    runner.provisioning.apply('@@[pahdev"BDUT;ipaddress=invalid"')
    assert _resolve_group_destination(runner.provisioning, "BDUT") is None
    runner.provisioning.apply('@@[pahdev"Other;ipaddress=invalid"')
    assert _resolve_group_destination(runner.provisioning, "Other") is None


def test_unicast_literal_alias_stays_unicast(project):
    runner = _runner(project, RecordingCoap())
    runner.provisioning.apply(
        '@@[pahdev"Peer;ipaddress=[fd11:9988:7766::2]:5683"')
    assert _resolve_group_destination(runner.provisioning, "Peer") is None


# =======================================================================
# (c) an unknown alias fails closed, naming the alias
# =======================================================================

def test_unknown_alias_fails_closed_with_the_alias_named(project):
    coap = RecordingCoap()
    runner = _runner(project, coap)
    ctx = _secure(runner)
    msg = _message(f'ID="7" CWay="IN" Code="POST" Path="k" Type="NON" '
                   f'IP="BDUT-Multi" SecContext="{ctx}"')
    # deliberately NOT provisioned
    result = runner._send_request(msg)

    assert not result.ok
    assert "BDUT-Multi" in result.reason
    assert coap.calls == []          # nothing went on the wire


def test_resolver_raises_for_an_unknown_alias(project):
    runner = _runner(project, RecordingCoap())
    with pytest.raises(UnknownDeviceAlias) as exc:
        _resolve_group_destination(runner.provisioning, "NoSuchDevice")
    assert "NoSuchDevice" in str(exc.value)


# =======================================================================
# (d) the adversarial knobs survive the group path
# =======================================================================

def test_adversarial_knobs_are_forwarded_on_the_group_path(project):
    """SeqNo/InvalidAuthTag/RndKid/Echo are the whole point of 5.3.17.x
    and 5.3.19.5 — a retarget that dropped them would turn those negative
    TestCases into positive ones the device passes by behaving well."""
    coap = RecordingCoap()
    runner = _runner(project, coap)
    _provision_group(runner)
    ctx = _secure(runner)
    runner._last_echo = b"\xca\xfe"

    # SeqNoOffset and SeqNoFn="fix" are mutually exclusive in
    # `_resolve_oscore_overrides` (offset wins), so each gets its own row —
    # 5.3.17.3 walks the window with SeqNoOffset, 5.3.18.1 pins a fixed
    # SeqNo.
    offset_row = _message(
        f'ID="1" CWay="IN" Code="POST" Path="k" Type="NON" '
        f'IP="BDUT-Multi" SecContext="{ctx}" '
        f'SeqNoOffset="-3" InvalidAuthTag="AA BB" RndKid="yes" '
        f'Echo="sync" AckChkTimeOut="2.5"', "<Cbor>a0</Cbor>")
    assert runner._send_request(offset_row).ok

    kw = coap.calls[0][3]
    assert kw["dest"] == (GROUP_ADDR, GROUP_PORT)
    assert kw["seq_no_offset"] == -3
    assert kw["invalid_auth_tag"] == b"\xaa\xbb"
    assert kw["kid_override"] == b"\xde\xad\xbe\xef"
    assert kw["echo"] == b"\xca\xfe"
    assert kw["timeout"] == 2.5

    fixed_row = _message(
        f'ID="2" CWay="IN" Code="POST" Path="k" Type="NON" '
        f'IP="BDUT-Multi" SecContext="{ctx}" '
        f'SeqNoFn="fix" SeqNo="42" Echo="fix" EchoValue="0102"',
        "<Cbor>a0</Cbor>")
    assert runner._send_request(fixed_row).ok

    kw = coap.calls[1][3]
    assert kw["dest"] == (GROUP_ADDR, GROUP_PORT)
    assert kw["seq_no_override"] == 42
    assert kw["seq_no_offset"] is None
    assert kw["echo"] == b"\x01\x02"


def test_expect_echo_challenge_is_forwarded_on_the_group_path(project):
    coap = RecordingCoap()
    runner = _runner(project, coap)
    _provision_group(runner)
    ctx = _secure(runner)
    runner._expect_echo_challenge = True
    msg = _message(f'ID="1" CWay="IN" Code="POST" Path="k" Type="NON" '
                   f'IP="BDUT-Multi" SecContext="{ctx}"', "<Cbor>a0</Cbor>")
    assert runner._send_request(msg).ok
    kw = coap.calls[0][3]
    assert kw["expect_echo_challenge"] is True
    assert kw["dest"] == (GROUP_ADDR, GROUP_PORT)


def test_group_response_is_remembered_for_the_following_out_rows(project):
    """The multicast reply is still "the response": `_remember_response`
    must see it, and the Echo it carries must reach an Echo="sync"
    follow-up."""
    resp = CoapResponse(
        code_class=4, code_detail=1, payload=b"", content_format=None,
        msg_type=NON, mid=9, token=b"\x07",
        oscore_kid_context=b"\x11\x22\x33")
    resp.echo_value = b"\xbe\xef"
    coap = RecordingCoap(response=resp)
    runner = _runner(project, coap)
    _provision_group(runner)
    ctx = _secure(runner)
    msg = _message(f'ID="1" CWay="IN" Code="POST" Path="k" Type="NON" '
                   f'IP="BDUT-Multi" SecContext="{ctx}"', "<Cbor>a0</Cbor>")
    assert runner._send_request(msg).ok

    assert runner._last_response_code == (4, 1)
    assert runner._last_response_type == NON
    assert runner._last_response_kid_context == b"\x11\x22\x33"
    assert runner._last_echo == b"\xbe\xef"


# =======================================================================
# The template itself: exactly which rows this moves
# =======================================================================

def test_template_group_rows_all_resolve_to_the_group_address(project):
    """Every active IN row the template addresses to BDUT-Multi — and no
    other alias — must resolve to the group endpoint."""
    variables = VariableTable.for_collection(
        project, project.collections[0].name)
    engine = ProvisioningEngine(variables)
    engine.apply(BDUT_MULTI_PROVISION)

    root = ET.parse(TEMPLATE).getroot()
    seen: dict[str, int] = {}

    def walk(el):
        ip = el.get("IP")
        if ip is not None and el.get("CWay") == "IN":
            seen[ip] = seen.get(ip, 0) + 1
        for child in el:
            walk(child)

    walk(root)
    assert seen["BDUT-Multi"] == 200
    assert _resolve_group_destination(engine, "BDUT-Multi") == (
        GROUP_ADDR, GROUP_PORT)
    for alias in seen:
        if alias == "BDUT-Multi":
            continue
        assert _resolve_group_destination(engine, alias) is None, alias


# =======================================================================
# (e) client level: oscore_request(dest=<group>) builds a NON and picks
#     the DUT's reply out of several
# =======================================================================

class FakeSocket:
    """A socket whose recvfrom replays a scripted list of (data, addr)."""

    def __init__(self):
        self.inbox: list = []
        self.sent: list = []
        self.sockopts: list = []
        self._timeout = None
        self.closed = False

    def setsockopt(self, level, opt, value):
        self.sockopts.append((level, opt, value))

    def settimeout(self, t):
        self._timeout = t

    def gettimeout(self):
        return self._timeout

    def bind(self, addr):
        self.bound = addr

    def getsockname(self):
        return ("::", 44444, 0, 0)

    def sendto(self, data, addr):
        self.sent.append((data, addr))
        return len(data)

    def recvfrom(self, bufsize):
        if not self.inbox:
            raise socket.timeout()
        return self.inbox.pop(0)

    def close(self):
        self.closed = True


class PrimedGroupSocket(FakeSocket):
    """A group socket that answers its own send: our looped-back datagram,
    a stranger's, then the DUT's — all with the right token, so only the
    SOURCE ADDRESS can tell them apart."""

    def sendto(self, data, addr):
        out = FakeSocket.sendto(self, data, addr)
        tkl = data[0] & 0x0F
        token = data[4:4 + tkl]
        self.inbox.append((_oscore_datagram(token, 11),
                           ("fd11:9988:7766::99", 5683, 0, 0)))
        self.inbox.append((_oscore_datagram(token, 12),
                           (DUT_HOST, 49153, 0, 0)))
        return out


class StubOscoreCtx:
    """protect_request/unprotect_response stand-ins with no crypto."""

    def __init__(self):
        self.protect_calls: list = []
        self.unprotected: list = []

    def protect_request(self, method_code, path, payload=b"", queries=None,
                        **kw):
        self.protect_calls.append((method_code, path, payload, kw))
        # kid=b"\x01", piv=b"\x05" — a minimal well-formed OSCORE option.
        return b"\x09\x05\x01", b"CIPHER", b"\x05"

    def unprotect_response(self, oscore_opt, payload, piv):
        self.unprotected.append((oscore_opt, payload, piv))
        return 0x45, b"INNER", {}      # inner 2.05, no inner options


def _oscore_datagram(token: bytes, mid: int = 1) -> bytes:
    """A NON 2.04 carrying an OSCORE option (9) and a ciphertext."""
    header = struct.pack("!BBH", (1 << 6) | (NON << 4) | len(token),
                         (2 << 5) | 4, mid)
    option = CC._encode_option(9, b"\x09\x06\x01")
    return header + token + option + b"\xFF" + b"CIPHER"


@pytest.fixture
def fake_client(monkeypatch):
    """A CoapClient whose own socket is a FakeSocket, plus a hook that
    makes every SUBSEQUENT socket a PrimedGroupSocket (the one
    `_request_socket` builds for a group send)."""
    monkeypatch.setattr(CC.socket, "socket",
                        lambda family, type_: FakeSocket())
    client = CoapClient(host=DUT_HOST, port=5683, timeout=0.5)
    group_sockets: list = []

    def primed(family, type_):
        s = PrimedGroupSocket()
        group_sockets.append(s)
        return s

    monkeypatch.setattr(CC.socket, "socket", primed)
    return client, group_sockets


def test_oscore_request_multicast_dest_sends_non_from_a_group_socket(
        fake_client, monkeypatch):
    monkeypatch.setenv("DEVICE_IFACE", "0")
    client, group_sockets = fake_client
    ctx = StubOscoreCtx()

    resp = client.oscore_request(
        ctx, 2, "k", payload=b"BODY", msg_type=CON,
        dest=(GROUP_ADDR, GROUP_PORT), timeout=0.5)

    # The DUT's reply is the response, decrypted through the ordinary
    # OSCORE path (inner 2.05, not the outer 2.04).
    assert resp is not None
    assert (resp.code_class, resp.code_detail) == (2, 5)
    assert resp.payload == b"INNER"

    assert len(group_sockets) == 1
    sock = group_sockets[0]
    assert sock.closed                      # the group socket is disposable
    assert client._sock is not sock         # and the client's own is restored

    data, addr = sock.sent[0]
    # Sent to the GROUP, and sent NON even though msg_type said CON.
    assert addr[0] == GROUP_ADDR and addr[1] == GROUP_PORT
    assert ((data[0] >> 4) & 0x03) == NON
    # Configured like oscore_multicast_post: a multicast hop limit.
    assert any(opt == socket.IPV6_MULTICAST_HOPS
               for _lvl, opt, _v in sock.sockopts)
    # Exactly ONE datagram was decrypted — the stranger's was rejected on
    # its source address, and the rejection was recorded.
    assert len(ctx.unprotected) == 1
    assert client._last_discards
    assert "fd11:9988:7766::99" in client._last_discards[0]


def test_oscore_request_multicast_reply_from_a_non_5683_port_is_accepted(
        fake_client, monkeypatch):
    """The device answers a group message from its UNICAST port, not 5683.
    Nothing may compare the source port."""
    monkeypatch.setenv("DEVICE_IFACE", "0")
    client, group_sockets = fake_client
    resp = client.oscore_request(
        StubOscoreCtx(), 2, "k", msg_type=NON,
        dest=(GROUP_ADDR, GROUP_PORT), timeout=0.5)
    assert resp is not None
    # The reply PrimedGroupSocket injects for the DUT comes from 49153.
    assert group_sockets[0].sent


def test_oscore_request_unicast_dest_uses_the_clients_own_socket(
        fake_client):
    client, group_sockets = fake_client
    own = client._sock
    own.inbox.append((_oscore_datagram(b"", 5), (DUT_HOST, 5683, 0, 0)))
    # A unicast dest simply retargets: no new socket, no NON coercion.
    client.oscore_request(StubOscoreCtx(), 1, "dev/sn",
                          dest=("fd11:9988:7766::2", 5683), timeout=0.2)
    assert group_sockets == []
    assert client._sock is own
    assert own.sent[0][1][0] == "fd11:9988:7766::2"
    assert ((own.sent[0][0][0] >> 4) & 0x03) == CON


def test_oscore_request_without_dest_is_the_old_unicast_path(fake_client):
    client, group_sockets = fake_client
    own = client._sock
    client.oscore_request(StubOscoreCtx(), 1, "dev/sn", timeout=0.2)
    assert group_sockets == []
    assert client._sock is own
    assert own.sent[0][1][:2] == (DUT_HOST, 5683)


# =======================================================================
# The address helpers
# =======================================================================

@pytest.mark.parametrize("value,expected", [
    (f"[{GROUP_ADDR}]:5683", (GROUP_ADDR, 5683)),
    (f"[{GROUP_ADDR}]", (GROUP_ADDR, 5683)),
    (GROUP_ADDR, (GROUP_ADDR, 5683)),
    ("[fd11::2]:1234", ("fd11::2", 1234)),
    ("invalid", None),
    ("", None),
    (None, None),
    ("[fd11::2", None),
    ("[fd11::2]:notaport", None),
])
def test_parse_ipv6_endpoint(value, expected):
    assert CC.parse_ipv6_endpoint(value) == expected


@pytest.mark.parametrize("value,expected", [
    (GROUP_ADDR, True),
    (f"[{GROUP_ADDR}]", True),
    ("ff02::fd", True),
    ("ff02::fd%eth0", True),
    ("fd11:9988:7766::1", False),
    ("::1", False),
    ("not-an-address", False),
    ("", False),
    (None, False),
])
def test_is_multicast_v6(value, expected):
    assert CC._is_multicast_v6(value) is expected


def test_same_host_ignores_form_and_zone():
    assert CC._same_host("fd11:0:0:0:0:0:0:1", "fd11::1")
    assert CC._same_host("FD11::1%eth0", "fd11::1")
    assert not CC._same_host("fd11::1", "fd11::2")
