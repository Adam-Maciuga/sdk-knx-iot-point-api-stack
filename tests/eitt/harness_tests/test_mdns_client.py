"""Tests for the mDNS/DNS-SD test transport (`transport.mdns_client`).

No DUT, no Docker, no veth: every test here either drives the wire-format
parsers over hand-built `bytes`, or exercises the listener/sender against
a mocked socket. They pin down the "fail closed" contract the module
documents — a malformed packet must produce a named failure, never a
plausible-looking truncated value — which is what findings M9, M11-M19 of
tests/docs/eitt-interpreter-findings.md are about.

Packet-building helpers live at the top so each test reads as "this is the
exact datagram, this is what the parser must say about it".
"""

from __future__ import annotations

import socket
import struct
import threading
import time

import pytest

from transport import mdns_client
from transport.mdns_client import (DNS_CLASS_IN, DNS_FLAG_QR,
                                   DNS_QCLASS_UNICAST, DNS_TYPE_AAAA,
                                   DNS_TYPE_PTR, DNS_TYPE_SRV, DNS_TYPE_TXT,
                                   DnsRecord, MdnsInterfaceError,
                                   MdnsListener, MdnsParseError,
                                   RFC6762_HOST_TTL, RFC6762_SHARED_TTL,
                                   check_record_ttls, decode_dns_name,
                                   dns_type_code, encode_dns_name,
                                   parse_records, parse_txt_rdata)


# --------------------------------------------------------------------------
# packet builders
# --------------------------------------------------------------------------

def _header(*, qr: bool = True, qd: int = 0, an: int = 0, ns: int = 0,
            ar: int = 0) -> bytes:
    """A 12-byte DNS header. `qr=True` marks it a RESPONSE (RFC 6762 §18.2)."""
    return struct.pack("!HHHHHH", 0, DNS_FLAG_QR if qr else 0, qd, an, ns, ar)


def _rr(name: str, rtype: int, rdata: bytes, ttl: int = 60) -> bytes:
    """One resource record with a correctly declared RDLENGTH."""
    return (encode_dns_name(name)
            + struct.pack("!HHIH", rtype, DNS_CLASS_IN, ttl, len(rdata))
            + rdata)


def _txt_rdata(*strings: bytes) -> bytes:
    """TXT RDATA: each string prefixed with its own length byte."""
    return b"".join(bytes([len(s)]) + s for s in strings)


def _srv_rdata(port: int, host: str) -> bytes:
    return struct.pack("!HHH", 0, 0, port) + encode_dns_name(host)


def _answer_packet(*records: bytes) -> bytes:
    return _header(qr=True, an=len(records)) + b"".join(records)


# --------------------------------------------------------------------------
# M9 — compression pointers and name decoding must fail loudly
# --------------------------------------------------------------------------

class TestM9NameDecoding:
    """decode_dns_name used to `break` out of every malformed input and
    return the labels gathered so far — a plausible SHORT name with no
    error signal, which downstream simply failed to match and blamed the
    device."""

    def test_wellformed_name_roundtrips(self):
        data = encode_dns_name("_knx._udp.local.")
        assert decode_dns_name(data, 0) == ("_knx._udp.local.", len(data))

    def test_compression_pointer_is_followed(self):
        # "local." at offset 0, then "_knx._udp" + a pointer back to it.
        base = encode_dns_name("local.")
        packet = base + b"\x04_knx\x04_udp" + struct.pack("!H", 0xC000)
        name, end = decode_dns_name(packet, len(base))
        assert name == "_knx._udp.local."
        assert end == len(packet)

    def test_pointer_loop_raises_instead_of_truncating(self):
        # A pointer to itself: the `seen` set terminated this already, but
        # by returning "" — the caller could not tell it from an empty name.
        packet = struct.pack("!H", 0xC000)
        with pytest.raises(MdnsParseError, match="pointer loop"):
            decode_dns_name(packet, 0)

    def test_mutual_pointer_loop_raises(self):
        # 0 -> 2 -> 0. Verified non-hanging before; now also non-silent.
        packet = struct.pack("!H", 0xC002) + struct.pack("!H", 0xC000)
        with pytest.raises(MdnsParseError, match="pointer loop"):
            decode_dns_name(packet, 0)

    def test_out_of_bounds_pointer_raises(self):
        packet = struct.pack("!H", 0xC0FF)
        with pytest.raises(MdnsParseError, match="past the end"):
            decode_dns_name(packet, 0)

    def test_truncated_pointer_raises(self):
        with pytest.raises(MdnsParseError, match="truncated DNS compression"):
            decode_dns_name(b"\xc0", 0)

    def test_label_running_off_the_end_raises(self):
        # The tracker's literal example: this returned the EMPTY name.
        with pytest.raises(MdnsParseError, match="claims 3 octets"):
            decode_dns_name(bytes([3, 0x61]), 0)

    def test_missing_root_label_raises(self):
        # A name with no terminating 0 byte used to stop at end-of-buffer
        # and hand back what it had.
        with pytest.raises(MdnsParseError, match="runs past the end"):
            decode_dns_name(b"\x05local", 0)

    def test_reserved_label_type_raises(self):
        with pytest.raises(MdnsParseError, match="reserved DNS label type"):
            decode_dns_name(b"\x80\x00", 0)

    def test_oversized_name_raises(self):
        # 6 labels of 63 octets is 384 > 255.
        packet = (b"\x3f" + b"a" * 63) * 6 + b"\x00"
        with pytest.raises(MdnsParseError, match="255-octet wire limit"):
            decode_dns_name(packet, 0)

    def test_record_boundary_bounds_inline_labels(self):
        # M10's half: an in-line label may not reach past `end`, even when
        # the bytes that follow happen to be readable.
        packet = encode_dns_name("instance.local.") + b"junkjunkjunk"
        with pytest.raises(MdnsParseError, match="record boundary"):
            decode_dns_name(packet, 0, end=4)

    def test_listener_skips_a_packet_with_a_bad_name(self):
        # MdnsParseError is a ValueError, so M2's per-packet guard still
        # absorbs it: one bad datagram must not destroy a good result set.
        good = _answer_packet(_rr("_knx._udp.local.", DNS_TYPE_PTR,
                                  encode_dns_name("inst._knx._udp.local.")))
        bad = _header(qr=True, an=1) + struct.pack("!H", 0xC000)
        assert _drive_listener([bad, good])[0] == [
            ("PTR", "_knx._udp.local", "inst._knx._udp.local")]


# --------------------------------------------------------------------------
# M11 — TXT parsing
# --------------------------------------------------------------------------

class TestM11TxtParsing:
    """An overrun used to truncate to `{}` (indistinguishable from an
    empty TXT), repeated keys were last-wins where RFC 6763 §6.4 says
    first, and valueless keys were dropped."""

    def test_single_key_value(self):
        assert parse_txt_rdata(_txt_rdata(b"SP=30")) == {"SP": "30"}

    def test_empty_rdata_is_empty_dict(self):
        assert parse_txt_rdata(b"") == {}

    def test_single_zero_length_string_is_empty_dict(self):
        # RFC 6763 §6.1: the "no properties" encoding.
        assert parse_txt_rdata(b"\x00") == {}

    def test_overrun_raises_instead_of_emptying(self):
        # Declares 10 octets, supplies 4. Old behaviour: {} .
        with pytest.raises(MdnsParseError, match="claims 10 octets"):
            parse_txt_rdata(b"\x0aSP=3")

    def test_repeated_key_keeps_the_first(self):
        # §6.4: "MUST silently ignore all but the first".
        assert parse_txt_rdata(_txt_rdata(b"SP=30", b"SP=90")) == {"SP": "30"}

    def test_valueless_key_is_kept_as_none(self):
        assert parse_txt_rdata(_txt_rdata(b"boolkey")) == {"boolkey": None}

    def test_empty_value_is_kept_as_empty_string(self):
        # "k=" is a key with an empty value, distinct from a valueless key.
        assert parse_txt_rdata(_txt_rdata(b"k=")) == {"k": ""}

    def test_value_containing_equals_is_not_split_twice(self):
        assert parse_txt_rdata(_txt_rdata(b"k=a=b")) == {"k": "a=b"}

    def test_parse_records_surfaces_the_txt_dict(self):
        pkt = _answer_packet(_rr("inst._knx._udp.local.", DNS_TYPE_TXT,
                                 _txt_rdata(b"SP=30")))
        recs = parse_records(pkt)
        assert [(r.rtype, r.txt) for r in recs] == [("TXT", {"SP": "30"})]

    def test_parse_records_raises_on_an_overrun_txt(self):
        # RDLENGTH is honest; the string length inside it is not.
        pkt = _answer_packet(_rr("inst._knx._udp.local.", DNS_TYPE_TXT,
                                 b"\x0aSP=3"))
        with pytest.raises(MdnsParseError, match="TXT string"):
            parse_records(pkt)


# --------------------------------------------------------------------------
# M12 — the QR bit
# --------------------------------------------------------------------------

class TestM12QueryResponseBit:
    """RFC 6762 §7.1 known-answer suppression puts real answer records in
    QUERY packets. The parser discarded FLAGS, so another device's records
    could satisfy an assertion about the DUT."""

    def _kas_query(self) -> bytes:
        """A foreign known-answer-suppression query: QR=0, one question and
        one genuine PTR answer — the exact shape the tracker verified."""
        question = (encode_dns_name("_knx._udp.local.")
                    + struct.pack("!HH", DNS_TYPE_PTR, DNS_CLASS_IN))
        answer = _rr("_knx._udp.local.", DNS_TYPE_PTR,
                     encode_dns_name("inst._knx._udp.local."), ttl=4500)
        return _header(qr=False, qd=1, an=1) + question + answer

    def test_a_query_is_still_parseable_when_not_required_to_be_a_response(self):
        # The default stays lenient so a caller decoding a captured packet
        # of either kind keeps working.
        recs = parse_records(self._kas_query())
        assert [(r.rtype, r.name, r.ttl) for r in recs] == [
            ("PTR", "_knx._udp.local", 4500)]

    def test_require_response_rejects_a_foreign_query(self):
        with pytest.raises(MdnsParseError, match="is a QUERY"):
            parse_records(self._kas_query(), require_response=True)

    def test_require_response_accepts_a_real_response(self):
        pkt = _answer_packet(_rr("_knx._udp.local.", DNS_TYPE_PTR,
                                 encode_dns_name("inst._knx._udp.local.")))
        assert len(parse_records(pkt, require_response=True)) == 1

    def test_listener_does_not_adopt_records_from_a_foreign_query(self):
        records, _ = _drive_listener([self._kas_query()])
        assert records == []


# --------------------------------------------------------------------------
# M13 — the QU bit
# --------------------------------------------------------------------------

class TestM13UnicastBit:
    """QCLASS was always 0x0001, so every query was a QM query and the
    responder could apply the §6 delay, §6.3 rate limiting and §7.1
    suppression — the root cause of the 6 s timeout and the retry loop."""

    @staticmethod
    def _qclass(packet: bytes) -> int:
        return struct.unpack("!H", packet[-2:])[0]

    def test_qm_query_by_default(self):
        pkt = mdns_client._build_query("_knx._udp.local.", DNS_TYPE_PTR)
        assert self._qclass(pkt) == DNS_CLASS_IN

    def test_qu_bit_is_set_when_asked(self):
        pkt = mdns_client._build_query("_knx._udp.local.", DNS_TYPE_PTR,
                                       unicast=True)
        assert self._qclass(pkt) == DNS_CLASS_IN | DNS_QCLASS_UNICAST
        # The DUT masks 0x8000 off before its IN/ANY check
        # (mdns.h::mdns_socket_listen), so the class still reads as IN.
        assert self._qclass(pkt) & ~DNS_QCLASS_UNICAST == DNS_CLASS_IN

    def test_query_header_still_declares_exactly_one_question(self):
        pkt = mdns_client._build_query("_knx._udp.local.", DNS_TYPE_PTR,
                                       unicast=True)
        _id, flags, qd, an, ns, ar = struct.unpack("!HHHHHH", pkt[:12])
        assert (flags, qd, an, ns, ar) == (0, 1, 0, 0, 0)

    def test_send_query_passes_the_flag_through(self, monkeypatch):
        sent: list[bytes] = []
        monkeypatch.setattr(mdns_client.socket, "socket",
                            lambda *a, **k: _FakeSendSocket(sent))
        mdns_client.send_query("_knx._udp.local.", "PTR", interface="0",
                               unicast=True)
        assert self._qclass(sent[0]) == DNS_CLASS_IN | DNS_QCLASS_UNICAST

    def test_repeat_query_sets_qu_on_the_first_query_only(self, monkeypatch):
        """The retries stay QM so the answer is still multicast to the
        group if the DUT prefers that."""
        qclasses: list[int] = []

        def _fake_send(qname, rtype, interface=None, unicast=False):
            qclasses.append(DNS_CLASS_IN
                            | (DNS_QCLASS_UNICAST if unicast else 0))

        monkeypatch.setattr(mdns_client, "send_query", _fake_send)
        listener = _StubListener(timeout=0.3)
        MdnsListener.repeat_query(listener, "_knx._udp.local.", "PTR",
                                  every=0.05)
        time.sleep(0.35)
        assert len(qclasses) >= 2
        assert qclasses[0] == DNS_CLASS_IN | DNS_QCLASS_UNICAST
        assert set(qclasses[1:]) == {DNS_CLASS_IN}


# --------------------------------------------------------------------------
# M14 — interface lookup must not degrade silently
# --------------------------------------------------------------------------

class TestM14InterfaceLookup:
    """`_iface_index` swallowed OSError and returned 0, so a typo'd
    DEVICE_IFACE collapsed the join list to the wildcard alone — the exact
    single-interface configuration the historical silent-empty bug was
    about."""

    def test_no_interface_configured_is_the_wildcard(self, monkeypatch):
        monkeypatch.delenv("DEVICE_IFACE", raising=False)
        assert mdns_client._iface_index() == 0

    def test_numeric_interface_is_used_as_is(self):
        assert mdns_client._iface_index("7") == 7

    def test_env_var_is_honoured(self, monkeypatch):
        monkeypatch.setenv("DEVICE_IFACE", "9")
        assert mdns_client._iface_index() == 9

    def test_unknown_interface_name_raises(self, monkeypatch):
        def _boom(name):
            raise OSError("no such interface")

        monkeypatch.setattr(mdns_client.socket, "if_nametoindex", _boom)
        with pytest.raises(MdnsInterfaceError, match="unknown network interface"):
            mdns_client._iface_index("veth-typo0")

    def test_the_message_names_the_env_var_and_the_consequence(self, monkeypatch):
        def _boom(name):
            raise OSError("no such interface")

        monkeypatch.setattr(mdns_client.socket, "if_nametoindex", _boom)
        with pytest.raises(MdnsInterfaceError) as excinfo:
            mdns_client._iface_index("veth-typo0")
        text = str(excinfo.value)
        assert "DEVICE_IFACE" in text and "received nothing" in text

    def test_listener_construction_propagates_the_lookup_failure(self, monkeypatch):
        def _boom(name):
            raise OSError("no such interface")

        monkeypatch.setattr(mdns_client.socket, "if_nametoindex", _boom)
        with pytest.raises(MdnsInterfaceError):
            MdnsListener(interface="veth-typo0", timeout=0.2)

    def test_wildcard_only_join_is_a_failure_when_an_iface_was_named(self):
        """Joining index 0 but NOT the configured interface is the degraded
        configuration, so it must raise rather than proceed."""
        # It surfaces from the CONSTRUCTOR, not from a later records()
        # call — that is M17's half of the same story.
        with pytest.raises(MdnsInterfaceError, match="wildcard index"):
            _make_listener_that_joins(joinable={0}, iface_idx=5)

    def test_wildcard_only_join_is_fine_when_no_iface_was_named(self):
        listener = _make_listener_that_joins(joinable={0}, iface_idx=0)
        assert listener.records() == []

    # The first GitLab run of eitt-gcc after M14 failed all eight 5.1.2
    # cases with "joined ff02::fb on the wildcard index only; the
    # configured interface 3 failed - [Errno 98] Address already in use".
    # In that container the wildcard join resolves to veth-test, the one
    # interface with a multicast route, so the explicit join on 3 that
    # FOLLOWED was refused as a duplicate membership - a membership that
    # exists. Named first, wildcard second, and EADDRINUSE after a
    # successful join counts as joined.

    def test_named_interface_is_joined_before_the_wildcard(self):
        listener, fake = _make_listener_that_joins(
            joinable={0, 3}, iface_idx=3, return_fake=True)
        assert fake.joins[:2] == [3, 0]
        assert listener.records() == []

    def test_wildcard_already_a_member_after_the_named_join_is_fine(self):
        listener, fake = _make_listener_that_joins(
            joinable={3}, busy={0}, iface_idx=3, return_fake=True)
        assert fake.joins[:2] == [3, 0]
        assert listener.records() == []

    def test_named_interface_already_in_use_with_nothing_joined_raises(self):
        with pytest.raises(MdnsInterfaceError):
            _make_listener_that_joins(joinable=set(), busy={3}, iface_idx=3)


# --------------------------------------------------------------------------
# M15 — IPV6_MULTICAST_IF
# --------------------------------------------------------------------------

class TestM15MulticastInterfaceOption:
    """`if idx:` skipped the setsockopt when no scope index was known, so
    on a multi-homed host the kernel picked the route for ff02::fb itself
    — frequently not the test interface."""

    def _send(self, monkeypatch, interface):
        fake = _FakeSendSocket([])
        monkeypatch.setattr(mdns_client.socket, "socket",
                            lambda *a, **k: fake)
        mdns_client.send_query("_knx._udp.local.", "PTR",
                               interface=interface)
        return fake

    def test_multicast_if_is_set_for_a_named_index(self, monkeypatch):
        fake = self._send(monkeypatch, "5")
        assert fake.opts[(socket.IPPROTO_IPV6,
                          socket.IPV6_MULTICAST_IF)] == struct.pack("I", 5)

    def test_multicast_if_is_set_even_for_index_zero(self, monkeypatch):
        # The regression: previously skipped entirely.
        fake = self._send(monkeypatch, "0")
        assert fake.opts[(socket.IPPROTO_IPV6,
                          socket.IPV6_MULTICAST_IF)] == struct.pack("I", 0)

    def test_sendto_scope_id_agrees_with_the_sockopt(self, monkeypatch):
        fake = self._send(monkeypatch, "5")
        _data, addr = fake.sent_to[0]
        assert addr == (mdns_client.MDNS_MULTICAST_ADDR,
                        mdns_client.MDNS_PORT, 0, 5)

    def test_hop_limit_is_still_255(self, monkeypatch):
        fake = self._send(monkeypatch, "5")
        assert fake.opts[(socket.IPPROTO_IPV6,
                          socket.IPV6_MULTICAST_HOPS)] == 255

    def test_socket_is_closed_even_when_sendto_fails(self, monkeypatch):
        fake = _FakeSendSocket([], fail_send=True)
        monkeypatch.setattr(mdns_client.socket, "socket",
                            lambda *a, **k: fake)
        with pytest.raises(OSError):
            mdns_client.send_query("_knx._udp.local.", "PTR", interface="1")
        assert fake.closed


# --------------------------------------------------------------------------
# M16 — unknown RecordType
# --------------------------------------------------------------------------

class TestM16UnknownRecordType:
    """`_TYPE_BY_NAME.get(rtype.upper(), DNS_TYPE_PTR)` sent a PTR query
    for an XML RecordType this module does not know, and the following OUT
    step then failed with a message pointing at the device."""

    @pytest.mark.parametrize("name,code", [
        ("PTR", DNS_TYPE_PTR), ("SRV", DNS_TYPE_SRV),
        ("AAAA", DNS_TYPE_AAAA), ("TXT", DNS_TYPE_TXT),
        ("ptr", DNS_TYPE_PTR), (" Srv ", DNS_TYPE_SRV),
    ])
    def test_known_types_resolve(self, name, code):
        assert dns_type_code(name) == code

    @pytest.mark.parametrize("name", ["ANY", "NSEC", "PTRR", "", "  ", None])
    def test_unknown_type_raises(self, name):
        with pytest.raises(ValueError, match="unsupported mDNS RecordType"):
            dns_type_code(name)

    def test_the_message_lists_what_is_supported(self):
        with pytest.raises(ValueError) as excinfo:
            dns_type_code("ANY")
        text = str(excinfo.value)
        assert all(t in text for t in ("PTR", "SRV", "AAAA", "TXT"))

    def test_send_query_raises_before_touching_a_socket(self, monkeypatch):
        def _no_socket(*a, **k):
            raise AssertionError("a socket must not be opened for a bad type")

        monkeypatch.setattr(mdns_client.socket, "socket", _no_socket)
        with pytest.raises(ValueError, match="unsupported mDNS RecordType"):
            mdns_client.send_query("_knx._udp.local.", "ANY")

    def test_repeat_query_raises_in_the_callers_thread(self, monkeypatch):
        """Raised inside the pump thread it would die unseen and look like
        a device that never answered."""
        monkeypatch.setattr(mdns_client, "send_query",
                            lambda *a, **k: None)
        listener = _StubListener(timeout=0.2)
        with pytest.raises(ValueError, match="unsupported mDNS RecordType"):
            MdnsListener.repeat_query(listener, "_knx._udp.local.", "ANY")


# --------------------------------------------------------------------------
# M17 — a listener that failed to bind must not look ready
# --------------------------------------------------------------------------

class TestM17ReadyOnlyWhenListening:
    """`_ready` was set in the `finally`, so a listener that failed to bind
    returned looking healthy and the error surfaced only at records() —
    with repeat_query pumping into the void in between."""

    def test_bind_failure_raises_from_the_constructor(self, monkeypatch):
        monkeypatch.setattr(mdns_client.socket, "socket",
                            lambda *a, **k: _FakeListenSocket(
                                fail_bind=OSError("Address already in use")))
        with pytest.raises(OSError, match="Address already in use"):
            MdnsListener(timeout=0.3)

    def test_join_failure_on_every_interface_raises_from_the_constructor(self):
        with pytest.raises(MdnsInterfaceError, match="could not join"):
            _make_listener_that_joins(joinable=set(), iface_idx=0)

    def test_a_healthy_listener_constructs_and_reports_no_error(self):
        listener = _make_listener_that_joins(joinable={0}, iface_idx=0)
        assert listener.records() == []

    def test_socket_creation_failure_raises_from_the_constructor(self, monkeypatch):
        def _boom(*a, **k):
            raise OSError("no IPv6 support")

        monkeypatch.setattr(mdns_client.socket, "socket", _boom)
        with pytest.raises(OSError, match="no IPv6 support"):
            MdnsListener(timeout=0.3)

    def test_the_constructor_does_not_burn_the_full_wait_on_failure(self, monkeypatch):
        """M20's other half: a dead listener must fail fast, not after 2s."""
        monkeypatch.setattr(mdns_client.socket, "socket",
                            lambda *a, **k: _FakeListenSocket(
                                fail_bind=OSError("boom")))
        start = time.monotonic()
        with pytest.raises(OSError):
            MdnsListener(timeout=0.3)
        assert time.monotonic() - start < 1.5


# --------------------------------------------------------------------------
# M18 — the join barrier
# --------------------------------------------------------------------------

class TestM18JoinBarrier:
    """The only barrier was `time.sleep(0.15)`, a guess at MLD report
    propagation with no verification that the join took effect."""

    def test_join_is_confirmed_by_a_returning_probe(self, monkeypatch):
        slept: list[float] = []
        monkeypatch.setattr(mdns_client.time, "sleep",
                            lambda s: slept.append(s))
        # The fake listen socket echoes the probe back, which is what a
        # working IPV6_MULTICAST_LOOP does.
        listener = _make_listener_that_joins(joinable={0}, iface_idx=0,
                                             echo_probe=True)
        assert 0.15 not in slept          # the sleep fallback was not used
        # ...and the probe is NOT counted as traffic from the DUT, or M8's
        # "did anything at all arrive" signal would always say yes.
        assert listener.packets_received() == 0

    def test_probe_is_a_query_for_a_name_no_responder_owns(self, monkeypatch):
        """A DUT that sees the probe must have nothing to answer, so the
        probe cannot pollute a real discovery."""
        sent: list[bytes] = []
        monkeypatch.setattr(mdns_client.socket, "socket",
                            lambda *a, **k: _FakeSendSocket(sent))
        listener = _StubListener(timeout=0.1)
        listener._packet_seen.set()
        assert MdnsListener._await_join(listener, timeout=0.2) is True
        name, _ = decode_dns_name(sent[0], 12)
        assert name == "_knx-join-probe.invalid."

    def test_a_confirmed_join_still_captures_the_dut_answer(self):
        """The probe must not consume or mask the answer that follows."""
        answer = _answer_packet(_rr("_knx._udp.local.", DNS_TYPE_PTR,
                                    encode_dns_name("inst._knx._udp.local.")))
        listener = _make_listener_that_joins(joinable={0}, iface_idx=0,
                                             echo_probe=True,
                                             packets=[answer])
        assert [r.name for r in listener.records()] == ["_knx._udp.local"]
        assert listener.packets_received() == 1     # the answer, not the probe

    def test_unconfirmed_join_falls_back_to_the_sleep(self, monkeypatch):
        slept: list[float] = []
        monkeypatch.setattr(mdns_client.time, "sleep",
                            lambda s: slept.append(s))
        listener = _make_listener_that_joins(joinable={0}, iface_idx=0,
                                             echo_probe=False)
        assert 0.15 in slept
        assert listener.records() == []

    def test_loopback_unsupported_falls_back_rather_than_failing(self, monkeypatch):
        """A link with multicast loopback disabled is not a broken link."""
        fake = _FakeSendSocket([], fail_opts={(socket.IPPROTO_IPV6,
                                               socket.IPV6_MULTICAST_LOOP)})
        monkeypatch.setattr(mdns_client.socket, "socket",
                            lambda *a, **k: fake)
        listener = _StubListener(timeout=0.1)
        assert MdnsListener._await_join(listener, timeout=0.2) is False
        assert fake.closed


# --------------------------------------------------------------------------
# M19 — TTL validation
# --------------------------------------------------------------------------

class TestM19TtlValidation:
    """Nothing checked announce TTLs against RFC 6762 §10, and the traces
    carry TTL=0 on every row, so TTL correctness was untested end to end."""

    def test_rfc_values_are_the_documented_ones(self):
        assert (RFC6762_HOST_TTL, RFC6762_SHARED_TTL) == (120, 4500)
        assert mdns_client.expected_ttl("AAAA") == 120
        assert mdns_client.expected_ttl("SRV") == 120
        assert mdns_client.expected_ttl("PTR") == 4500
        assert mdns_client.expected_ttl("TXT") == 4500

    def test_rfc_conformant_records_produce_no_complaint(self):
        recs = [DnsRecord("PTR", "_knx._udp.local", ttl=4500),
                DnsRecord("AAAA", "knx-x.local", ttl=120),
                DnsRecord("SRV", "inst._knx._udp.local", ttl=120),
                DnsRecord("TXT", "inst._knx._udp.local", ttl=4500)]
        assert check_record_ttls(recs) == []

    def test_goodbyes_are_never_flagged(self):
        # TTL=0 is a withdrawal (§10.1) and is the CORRECT answer for half
        # of 5.1.2.5a — see finding M5.
        recs = [DnsRecord("PTR", "_pm._sub._knx._udp.local", ttl=0)]
        assert check_record_ttls(recs) == []

    def test_this_devices_60s_ttl_is_accepted_by_default(self):
        # port/mdns/mdns.h: MDNS_DEFAULT_TTL 60, a documented deviation.
        recs = [DnsRecord("PTR", "_knx._udp.local", ttl=mdns_client.KNX_DEVICE_TTL),
                DnsRecord("AAAA", "knx-x.local", ttl=mdns_client.KNX_DEVICE_TTL)]
        assert check_record_ttls(recs) == []

    def test_the_device_deviation_can_be_reported_on_request(self):
        recs = [DnsRecord("AAAA", "knx-x.local", ttl=mdns_client.KNX_DEVICE_TTL)]
        problems = check_record_ttls(recs, allow_device_ttl=False)
        assert len(problems) == 1
        assert "MDNS_DEFAULT_TTL" in problems[0]

    def test_a_wrong_ttl_is_reported_with_the_expected_value(self):
        recs = [DnsRecord("AAAA", "knx-x.local", ttl=9999)]
        problems = check_record_ttls(recs)
        assert len(problems) == 1
        assert "ttl=9999s" in problems[0]
        assert "expects 120s" in problems[0]
        assert "host records" in problems[0]

    def test_a_wrong_shared_ttl_names_4500(self):
        problems = check_record_ttls([DnsRecord("PTR", "_knx._udp.local",
                                                ttl=7)])
        assert "expects 4500s" in problems[0]
        assert "shared records" in problems[0]

    def test_every_deviating_record_is_reported(self):
        recs = [DnsRecord("PTR", "a", ttl=1), DnsRecord("AAAA", "b", ttl=2),
                DnsRecord("SRV", "c", ttl=120)]
        assert len(check_record_ttls(recs)) == 2

    def test_ttls_are_parsed_off_the_wire_unchanged(self):
        pkt = _answer_packet(
            _rr("_knx._udp.local.", DNS_TYPE_PTR,
                encode_dns_name("inst._knx._udp.local."), ttl=4500),
            _rr("knx-x.local.", DNS_TYPE_AAAA,
                socket.inet_pton(socket.AF_INET6, "fe80::1"), ttl=120))
        assert [r.ttl for r in parse_records(pkt)] == [4500, 120]
        assert check_record_ttls(parse_records(pkt)) == []


# --------------------------------------------------------------------------
# regression guards for the untouched happy path
# --------------------------------------------------------------------------

class TestHappyPathUnchanged:
    """The DUT's own answer shape must still parse exactly as before, so
    the strictness added above cannot cost a passing 5.1 step."""

    def _dut_answer(self) -> bytes:
        inst = "00fa10020800._knx._udp.local."
        host = "knx-00fa10020800.local."
        return _answer_packet(
            _rr("_knx._udp.local.", DNS_TYPE_PTR, encode_dns_name(inst)),
            _rr("_00fa10020800._sub._knx._udp.local.", DNS_TYPE_PTR,
                encode_dns_name(inst)),
            _rr("_pm._sub._knx._udp.local.", DNS_TYPE_PTR,
                encode_dns_name(inst)),
            _rr(inst, DNS_TYPE_SRV, _srv_rdata(5683, host)),
            _rr(host, DNS_TYPE_AAAA,
                socket.inet_pton(socket.AF_INET6, "fe80::1")),
            _rr(inst, DNS_TYPE_TXT, _txt_rdata(b"SP=30")))

    def test_full_dut_answer_parses(self):
        recs = parse_records(self._dut_answer(), require_response=True)
        assert [r.rtype for r in recs] == ["PTR", "PTR", "PTR", "SRV",
                                           "AAAA", "TXT"]
        srv = next(r for r in recs if r.rtype == "SRV")
        assert (srv.port, srv.target) == (5683, "knx-00fa10020800.local")
        aaaa = next(r for r in recs if r.rtype == "AAAA")
        assert aaaa.target == "fe80::1"
        assert next(r for r in recs if r.rtype == "TXT").txt == {"SP": "30"}

    def test_goodbye_packet_parses_with_ttl_zero(self):
        pkt = _answer_packet(_rr("_pm._sub._knx._udp.local.", DNS_TYPE_PTR,
                                 encode_dns_name("inst._knx._udp.local."),
                                 ttl=0))
        assert [r.ttl for r in parse_records(pkt)] == [0]

    def test_short_packet_still_degrades_to_empty(self):
        assert parse_records(b"\x00\x01") == []

    def test_short_packet_raises_under_strict(self):
        with pytest.raises(MdnsParseError, match="12-byte DNS header"):
            parse_records(b"\x00\x01", strict=True)

    def test_lying_ancount_degrades_to_what_is_present(self):
        # The tracker's "checked and NOT a defect" case: ANCOUNT 65535 on a
        # short packet must not crash or hang.
        pkt = struct.pack("!HHHHHH", 0, DNS_FLAG_QR, 0, 0xFFFF, 0, 0)
        assert parse_records(pkt) == []

    def test_truncated_rdata_stops_without_raising(self):
        # M1's case: a truncated AAAA must not destroy the earlier records.
        good = _rr("_knx._udp.local.", DNS_TYPE_PTR,
                   encode_dns_name("inst._knx._udp.local."))
        truncated = (encode_dns_name("knx-x.local.")
                     + struct.pack("!HHIH", DNS_TYPE_AAAA, DNS_CLASS_IN, 60,
                                   16) + b"\x00\x01\x02")
        pkt = _header(qr=True, an=2) + good + truncated
        assert [r.rtype for r in parse_records(pkt)] == ["PTR"]

    def test_truncated_rdata_raises_under_strict(self):
        truncated = (encode_dns_name("knx-x.local.")
                     + struct.pack("!HHIH", DNS_TYPE_AAAA, DNS_CLASS_IN, 60,
                                   16) + b"\x00\x01\x02")
        with pytest.raises(MdnsParseError, match="RDLENGTH 16"):
            parse_records(_header(qr=True, an=1) + truncated, strict=True)

    def test_question_section_is_skipped(self):
        question = (encode_dns_name("_knx._udp.local.")
                    + struct.pack("!HH", DNS_TYPE_PTR, DNS_CLASS_IN))
        answer = _rr("_knx._udp.local.", DNS_TYPE_PTR,
                     encode_dns_name("inst._knx._udp.local."))
        pkt = _header(qr=True, qd=1, an=1) + question + answer
        assert [r.rtype for r in parse_records(pkt)] == ["PTR"]

    def test_packets_received_counts_every_datagram(self):
        good = _answer_packet(_rr("_knx._udp.local.", DNS_TYPE_PTR,
                                  encode_dns_name("inst._knx._udp.local.")))
        garbage = b"not a dns packet at all"
        records, count = _drive_listener([garbage, good])
        # M8's signal: both datagrams are counted even though one yielded
        # nothing, so "received nothing" can be told from "received junk".
        assert count == 2 and len(records) == 1


# --------------------------------------------------------------------------
# test doubles
# --------------------------------------------------------------------------

class _FakeSendSocket:
    """Stands in for the short-lived sending socket in `send_query`."""

    def __init__(self, sent: list, fail_send: bool = False,
                 fail_opts: set | None = None):
        self.sent = sent
        self.sent_to: list = []
        self.opts: dict = {}
        self.closed = False
        self._fail_send = fail_send
        self._fail_opts = fail_opts or set()

    def setsockopt(self, level, opt, value):
        if (level, opt) in self._fail_opts:
            raise OSError("unsupported socket option")
        self.opts[(level, opt)] = value

    def sendto(self, data, addr):
        if self._fail_send:
            raise OSError("network unreachable")
        self.sent.append(data)
        self.sent_to.append((data, addr))

    def close(self):
        self.closed = True


class _FakeListenSocket:
    """Stands in for the listener's bound socket.

    `packets` are handed out by `recvfrom` in order, then it times out
    until the listener's own deadline expires. `joinable` is the set of
    scope indices whose IPV6_JOIN_GROUP succeeds.
    """

    def __init__(self, packets: list | None = None,
                 joinable: set | None = None,
                 fail_bind: OSError | None = None,
                 echo_probe: bool = False,
                 busy: set | None = None):
        self._packets = list(packets or [])
        self._joinable = {0} if joinable is None else joinable
        # Indices whose join fails with EADDRINUSE: the kernel says the
        # socket is already a member on that interface (GitLab CI, where
        # the wildcard join resolves to veth-test).
        self._busy = set(busy or ())
        self._fail_bind = fail_bind
        self._echo_probe = echo_probe
        self.closed = False
        self.joins: list[int] = []   # join attempts, in order

    def setsockopt(self, level, opt, value):
        if opt == socket.IPV6_JOIN_GROUP:
            idx = struct.unpack("I", value[16:20])[0]
            self.joins.append(idx)
            if idx in self._busy:
                import errno
                raise OSError(errno.EADDRINUSE, "Address already in use")
            if idx not in self._joinable:
                raise OSError(f"join on {idx} refused")

    def bind(self, addr):
        if self._fail_bind is not None:
            raise self._fail_bind

    def settimeout(self, value):
        pass

    def sendto(self, data, addr):
        # The same fake also stands in for the join probe's sending
        # socket; `echo_probe` decides whether the link reflects it, the
        # way IPV6_MULTICAST_LOOP does on a real one.
        return len(data)

    def recvfrom(self, size):
        if self._packets:
            return self._packets.pop(0), ("fe80::2", 5353, 0, 0)
        if self._echo_probe:
            # One reflected probe, as IPV6_MULTICAST_LOOP would deliver.
            self._echo_probe = False
            return (mdns_client._build_query(mdns_client._JOIN_PROBE_NAME,
                                             DNS_TYPE_PTR),
                    ("fe80::1", 5353, 0, 0))
        raise socket.timeout()

    def close(self):
        self.closed = True


class _StubListener:
    """The minimum state `repeat_query`/`_await_join` read off `self`,
    without binding anything."""

    def __init__(self, timeout: float):
        self._timeout = timeout
        self._interface = None
        self._records: list = []
        self._iface_idx = 0
        self._packet_seen = threading.Event()
        self._join_probe = mdns_client._build_query(
            mdns_client._JOIN_PROBE_NAME, DNS_TYPE_PTR)


def _make_listener_that_joins(joinable: set, iface_idx: int,
                              echo_probe: bool = False,
                              packets: list | None = None,
                              busy: set | None = None,
                              return_fake: bool = False):
    """Build a real MdnsListener over a fake socket with a chosen join
    outcome. Raises out of the constructor exactly as the real one does.
    `busy` indices fail their join with EADDRINUSE; `return_fake` also
    hands back the socket so a test can inspect the join order."""
    import unittest.mock as mock
    fake = _FakeListenSocket(packets=packets, joinable=joinable,
                             echo_probe=echo_probe, busy=busy)
    with mock.patch.object(mdns_client.socket, "socket",
                           side_effect=lambda *a, **k: fake), \
            mock.patch.object(mdns_client, "_iface_index",
                              return_value=iface_idx):
        listener = MdnsListener(timeout=0.4)
    return (listener, fake) if return_fake else listener


def _drive_listener(packets: list) -> tuple[list, int]:
    """Run a real MdnsListener over a fake socket that yields `packets`,
    and return (simplified records, datagram count)."""
    listener = _make_listener_that_joins(joinable={0}, iface_idx=0,
                                         packets=packets)
    recs = listener.records()
    return ([(r.rtype, r.name, r.target) for r in recs],
            listener.packets_received())
