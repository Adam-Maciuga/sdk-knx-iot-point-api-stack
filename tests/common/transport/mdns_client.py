"""
Minimal mDNS query/response client for the EITT DnsMessage playback.

EITT models multicast-DNS discovery as `DnsMessage` sequence items rather
than CoAP telegrams. Each pair is a query the tool sends (`CWay="IN"`,
`Type="dnssd"`/`"mdns"`) followed by the record the DUT must answer with
(`CWay="OUT"`, `Type="dnsrecord"`) — see the "mDNS discovery" section of
tests/docs/eitt-xml-interpreter.md.

This module provides just enough of a resolver for that: send a query for
one record type, capture the DUT's multicast answer, and hand back the
parsed records. `test_discovery_mdns.py` keeps its own richer
snapshot/announcement tooling for the hand-written state-sync tests; the
wire-format primitives here are deliberately the same shape so the two
stay easy to compare.

**Failure policy: fail closed.** Every parse path here either produces a
value it can vouch for or raises `MdnsParseError`; nothing returns a
silently truncated name, an empty TXT dict that was really an overrun, or
a defaulted record type for an XML value this module does not know. That
is the contract python-zeroconf's `DNSIncoming` enforces with
`IncomingDecodeError` (`_protocol/incoming.py`), and it exists for the
same reason: a short name or a dropped key reads downstream as "the
device answered wrong" instead of "the packet was bad".

The listener keeps that policy compatible with a shared multicast group:
`MdnsListener` catches `MdnsParseError` per datagram and skips the packet
(finding M2), so strictness inside the parser never costs a good result
set collected alongside a foreign one.
"""

from __future__ import annotations

import os
import errno
import socket
import struct
import threading
import time
from dataclasses import dataclass, field
from typing import Optional

MDNS_MULTICAST_ADDR = "ff02::fb"
MDNS_PORT = 5353

DNS_TYPE_PTR = 12
DNS_TYPE_TXT = 16
DNS_TYPE_AAAA = 28
DNS_TYPE_SRV = 33
DNS_CLASS_IN = 1

# RFC 6762 §18.2: the top bit of the 16-bit FLAGS word is QR. A response
# has it set; a query does not. Known-answer suppression (§7.1) puts real
# answer records into QUERY packets, so a parser that ignores QR reads
# another host's records as if the DUT had answered them.
DNS_FLAG_QR = 0x8000

# RFC 6762 §5.4: the top bit of a QUESTION's QCLASS is the "unicast
# response requested" (QU) bit — the same bit position a RESOURCE
# RECORD's class uses for cache-flush (§10.2).
DNS_QCLASS_UNICAST = 0x8000

# RFC 6762 §10 recommended TTLs, used by `check_record_ttls`.
RFC6762_HOST_TTL = 120          # A/AAAA/SRV — records unique to one host
RFC6762_SHARED_TTL = 4500       # PTR/TXT — 75 minutes

# What this DUT actually sends. `port/mdns/mdns.h` defines
# MDNS_DEFAULT_TTL as 60 for BOTH classes of record, with the comment
# "RFC 6762 section 10 suggests 120 s for host records and 75 min for
# others; 60 s keeps stale entries short-lived". The device is therefore a
# deliberate, documented deviation from §10, and a TTL check wired into
# the match path would fail every live 5.1 step. `check_record_ttls` is a
# REPORTING helper for that reason — see its docstring.
KNX_DEVICE_TTL = 60

_TYPE_BY_NAME = {
    "PTR": DNS_TYPE_PTR,
    "TXT": DNS_TYPE_TXT,
    "AAAA": DNS_TYPE_AAAA,
    "SRV": DNS_TYPE_SRV,
}
_NAME_BY_TYPE = {v: k for k, v in _TYPE_BY_NAME.items()}

# RFC 1035 §2.3.4 / §4.1.4: a label is at most 63 octets (the two high
# bits of the length byte are the compression-pointer marker) and a whole
# name at most 255 octets.
_MAX_LABEL_LEN = 63
_MAX_NAME_LEN = 255

# The name `MdnsListener._await_join` probes with (finding M18). `.invalid`
# is reserved by RFC 6761 §6.4 precisely so that nothing can own it, so no
# DUT and no neighbour on the group has anything to answer — and the probe
# is a QUERY, which the listener's own QR check (M12) discards on sight.
_JOIN_PROBE_NAME = "_knx-join-probe.invalid."

_DEBUG = os.environ.get("KNX_TEST_DEBUG_MDNS") == "1"


class MdnsParseError(ValueError):
    """A datagram (or one record inside it) is malformed on the wire.

    Deliberately a `ValueError` subclass: `MdnsListener` already treats
    `ValueError` as "foreign/garbage packet, skip it and keep the records
    collected so far" (finding M2), so raising this from deep inside the
    parser cannot destroy a good result set. Callers that drive the parser
    directly — the harness tests, and any future strict path — see the
    explicit failure instead of a truncated value.
    """


class MdnsInterfaceError(OSError):
    """A configured interface, bind or group join did not come up.

    Raised instead of degrading to the wildcard index, which is finding
    M14: a typo'd `DEVICE_IFACE` used to collapse the join list to `[0]`
    and send on whatever route the kernel picked, and every 5.1 step then
    reported "received nothing" with no hint that the name was bad.
    """


def _dbg(*args, **kwargs):
    """Print an [mdns] debug line only when KNX_TEST_DEBUG_MDNS=1."""
    if _DEBUG:
        print(*args, **kwargs)


@dataclass
class DnsRecord:
    """One parsed resource record from a DUT mDNS answer."""
    rtype: str                  # "PTR" | "SRV" | "AAAA" | "TXT"
    name: str                   # owner name, trailing dot stripped
    target: str = ""            # PTR rdata / SRV host / AAAA address
    port: int = 0               # SRV only
    ttl: int = 0                # 0 = "goodbye" (RFC 6762 §10.1)
    txt: dict = field(default_factory=dict)


def encode_dns_name(name: str) -> bytes:
    """Encode a dotted name in DNS wire format."""
    out = b""
    for label in name.rstrip(".").split("."):
        if label:
            out += bytes([len(label)]) + label.encode("ascii")
    return out + b"\x00"


def decode_dns_name(data: bytes, offset: int,
                    end: Optional[int] = None) -> tuple[str, int]:
    """Decode a DNS wire-format name, following compression pointers.

    Returns (name_with_trailing_dot, offset_just_past_the_name).

    `end`, when given, bounds where the name's own IN-LINE labels may live
    — used to honour RDLENGTH as a record boundary (finding M10).
    Compression pointers may still target anywhere in `data`, which is the
    point of compression; only the in-line part is bounded.

    Raises `MdnsParseError` on every malformed input instead of returning
    the labels gathered so far (finding M9). The old code `break`'d out of
    a pointer loop, an out-of-bounds pointer, a label running off the end
    and a missing root label alike, each yielding a plausible SHORT name —
    so the record then failed to match downstream and the step blamed the
    device: a false fail with a misleading cause.
    (`decode_dns_name(bytes([3, 0x61]), 0)` used to return the EMPTY name;
    it now says the label claims 3 octets with 1 present.)
    python-zeroconf raises `IncomingDecodeError` on exactly these paths
    (`_protocol/incoming.py::_decode_labels_at_offset`).
    """
    limit = len(data) if end is None else min(end, len(data))
    labels: list[str] = []
    seen: set[int] = set()
    total = 0
    jumped = False
    ret = offset
    while True:
        if offset >= len(data):
            raise MdnsParseError(
                f"DNS name at offset {offset} runs past the end of the "
                f"{len(data)}-byte packet")
        if offset in seen:
            raise MdnsParseError(
                f"DNS name compression pointer loop: offset {offset} "
                f"visited twice (chain {sorted(seen)})")
        seen.add(offset)
        length = data[offset]
        if length == 0:
            if not jumped:
                ret = offset + 1
            break
        if (length & 0xC0) == 0xC0:
            if offset + 1 >= len(data):
                raise MdnsParseError(
                    f"truncated DNS compression pointer at offset "
                    f"{offset}: 1 of its 2 bytes is missing")
            ptr = ((length & 0x3F) << 8) | data[offset + 1]
            if ptr >= len(data):
                raise MdnsParseError(
                    f"DNS compression pointer at offset {offset} targets "
                    f"{ptr}, past the end of the {len(data)}-byte packet")
            if not jumped:
                ret = offset + 2
            jumped = True
            offset = ptr
            continue
        if (length & 0xC0) != 0:
            # 0b01 / 0b10 are reserved label types; nothing legitimate
            # emits them (RFC 6891 retired the only one ever defined).
            raise MdnsParseError(
                f"reserved DNS label type 0x{length & 0xC0:02x} at offset "
                f"{offset}")
        if length > _MAX_LABEL_LEN:
            raise MdnsParseError(
                f"DNS label at offset {offset} is {length} octets, over "
                f"the {_MAX_LABEL_LEN}-octet limit (RFC 1035 §2.3.4)")
        offset += 1
        if offset + length > len(data):
            raise MdnsParseError(
                f"DNS label at offset {offset} claims {length} octets but "
                f"only {len(data) - offset} remain in the packet")
        if not jumped and offset + length > limit:
            raise MdnsParseError(
                f"DNS label at offset {offset} claims {length} octets but "
                f"only {max(0, limit - offset)} remain before the record "
                f"boundary at {limit}")
        total += length + 1
        if total > _MAX_NAME_LEN:
            raise MdnsParseError(
                f"DNS name exceeds the {_MAX_NAME_LEN}-octet wire limit "
                f"(RFC 1035 §2.3.4) at offset {offset}")
        labels.append(data[offset:offset + length].decode("ascii",
                                                          errors="replace"))
        offset += length
        if not jumped:
            ret = offset
    return ".".join(labels) + ".", ret


def parse_txt_rdata(rdata: bytes) -> dict:
    """Parse TXT RDATA into a property dict (RFC 6763 §6).

    Three behaviours the old inline loop got wrong (finding M11), each
    matched here against python-zeroconf's
    `ServiceInfo._unpack_text_into_properties`:

    * **An overrun raises.** A string whose declared length runs past the
      RDATA used to be truncated by Python slicing, fail the `=` test and
      be dropped — yielding `{}`, indistinguishable from a genuinely empty
      TXT record.
    * **First key wins.** RFC 6763 §6.4: a client "MUST silently ignore
      all but the first" occurrence of a repeated key. The old dict
      assignment was last-wins.
    * **Valueless (boolean) keys are kept**, stored as `None`, instead of
      being dropped for having no `=`. §6.4 makes them legal.

    Empty RDATA and a single zero-length string both mean "no properties"
    (§6.1) and give `{}`.
    """
    props: dict = {}
    i = 0
    while i < len(rdata):
        n = rdata[i]
        i += 1
        if i + n > len(rdata):
            raise MdnsParseError(
                f"TXT string at offset {i} claims {n} octets but only "
                f"{len(rdata) - i} remain in the {len(rdata)}-byte RDATA")
        item = rdata[i:i + n]
        i += n
        if not item:
            continue                    # zero-length string, skip (§6.1)
        key, sep, value = item.partition(b"=")
        name = key.decode("ascii", "replace")
        if name in props:
            continue                    # §6.4: the first occurrence wins
        props[name] = value.decode("ascii", "replace") if sep else None
    return props


def parse_records(data: bytes, *, require_response: bool = False,
                  strict: bool = False) -> list[DnsRecord]:
    """Parse every answer/authority/additional record in an mDNS packet.

    `require_response`: reject a packet whose QR bit says QUERY (finding
    M12). RFC 6762 §7.1 known-answer suppression carries genuine answer
    records inside query packets, so without this check another device's
    records satisfy an assertion about the DUT — verified: a foreign KAS
    query parsed as `PTR _knx._udp.local -> inst._knx._udp.local
    ttl=4500`. `MdnsListener` passes it; the default stays False so a
    caller decoding a captured packet of either kind still works.

    `strict`: raise instead of stopping at the first record that does not
    fit the datagram. The default keeps the historical lenient behaviour
    at the RECORD-LIST level — a packet that simply ends early yields the
    records read so far, which is what M1's fix established — while every
    VALUE that is produced is now vouched for.
    """
    if len(data) < 12:
        if strict:
            raise MdnsParseError(
                f"mDNS packet is {len(data)} bytes, shorter than the "
                "12-byte DNS header")
        return []
    _id, flags, qd, an, ns, ar = struct.unpack("!HHHHHH", data[:12])
    if require_response and not (flags & DNS_FLAG_QR):
        raise MdnsParseError(
            f"packet is a QUERY (flags=0x{flags:04x}, QR=0), not a "
            f"response: its {an + ns + ar} record(s) are RFC 6762 §7.1 "
            "known-answer suppression and belong to whoever sent them, "
            "not to the DUT")
    off = 12
    for _ in range(qd):                       # skip the question section
        _, off = decode_dns_name(data, off)
        off += 4
    out: list[DnsRecord] = []
    for _ in range(an + ns + ar):
        if off >= len(data):
            break
        name, off = decode_dns_name(data, off)
        if off + 10 > len(data):
            if strict:
                raise MdnsParseError(
                    f"record header at offset {off} is truncated: 10 bytes "
                    f"needed, {len(data) - off} present")
            break
        rtype, _rclass, ttl, rdlen = struct.unpack("!HHIH", data[off:off + 10])
        off += 10
        rdata = data[off:off + rdlen]
        # RDLENGTH is what the sender CLAIMS; `rdata` is what actually
        # remains. A record whose declared length overruns the datagram is
        # malformed — stop rather than decode a short value as if it were
        # whole. (Checking rdlen instead of len(rdata) is how a truncated
        # AAAA used to reach inet_ntop and raise ValueError, which then
        # propagated out of the listener thread and discarded every record
        # already captured — a good discovery turned into an exception.)
        if len(rdata) != rdlen:
            if strict:
                raise MdnsParseError(
                    f"record at offset {off} declares RDLENGTH {rdlen} but "
                    f"only {len(rdata)} bytes remain in the packet")
            break
        rec_name = name.rstrip(".")
        # `off + rdlen` bounds the in-line labels of a PTR/SRV target to
        # its own record (finding M10); pointers may still reach back into
        # the packet, which is what compression is for.
        rec_end = off + rdlen
        if rtype == DNS_TYPE_PTR:
            target, _ = decode_dns_name(data, off, end=rec_end)
            out.append(DnsRecord("PTR", rec_name, target.rstrip("."),
                                 ttl=ttl))
        elif rtype == DNS_TYPE_SRV and rdlen >= 6:
            _prio, _weight, port = struct.unpack("!HHH", rdata[:6])
            host, _ = decode_dns_name(data, off + 6, end=rec_end)
            out.append(DnsRecord("SRV", rec_name, host.rstrip("."),
                                 port=port, ttl=ttl))
        elif rtype == DNS_TYPE_AAAA and len(rdata) == 16:
            addr = socket.inet_ntop(socket.AF_INET6, rdata)
            out.append(DnsRecord("AAAA", rec_name, addr, ttl=ttl))
        elif rtype == DNS_TYPE_TXT:
            out.append(DnsRecord("TXT", rec_name, ttl=ttl,
                                 txt=parse_txt_rdata(rdata)))
        off += rdlen
    return out


def expected_ttl(rtype: str) -> int:
    """The RFC 6762 §10 recommended TTL for a record type: 120 s for
    records unique to one host (A/AAAA/SRV), 75 minutes for shared ones
    (PTR/TXT)."""
    return (RFC6762_HOST_TTL if (rtype or "").upper() in ("AAAA", "A", "SRV")
            else RFC6762_SHARED_TTL)


def check_record_ttls(records: list[DnsRecord],
                      allow_device_ttl: bool = True) -> list[str]:
    """Check announce TTLs against RFC 6762 §10 and return the complaints.

    Finding M19: nothing validated TTLs anywhere, and `traces/5_1.xml`
    carries `TTL="0"` on every row including live ones, so the reference
    traces do not discriminate either — TTL correctness was untested end
    to end.

    **This is a reporting helper, not a gate.** It is deliberately NOT
    called from the runner's match path, for two independent reasons:

    * TTL=0 is a GOODBYE (§10.1) and is the CORRECT answer for half of
      5.1.2.5a — see finding M5, which proposed TTL-based matching and was
      disproved by the reference trace. Goodbyes are skipped here.
    * This DUT sends 60 s for every record on purpose
      (`port/mdns/mdns.h`: `MDNS_DEFAULT_TTL 60`, whose comment names
      §10's 120/4500 and picks 60 anyway). `allow_device_ttl` accepts that
      documented deviation; pass False to have it reported as well.

    Returns one human-readable line per deviating record, each naming the
    value §10 expects — an empty list means everything checked out.
    """
    problems: list[str] = []
    for rec in records:
        if rec.ttl == 0:
            continue                    # goodbye (§10.1), not an announce
        want = expected_ttl(rec.rtype)
        if rec.ttl == want:
            continue
        is_device_ttl = rec.ttl == KNX_DEVICE_TTL
        if allow_device_ttl and is_device_ttl:
            continue
        kind = "host" if want == RFC6762_HOST_TTL else "shared"
        note = ("; this DUT deliberately sends "
                f"{KNX_DEVICE_TTL}s — see MDNS_DEFAULT_TTL in "
                "port/mdns/mdns.h" if is_device_ttl else "")
        problems.append(
            f"{rec.rtype} {rec.name!r} has ttl={rec.ttl}s, but RFC 6762 "
            f"§10 expects {want}s for {kind} records{note}")
    return problems


def dns_type_code(rtype: str) -> int:
    """Map an XML `RecordType` to its DNS TYPE code, or raise.

    Finding M16: this used to be `.get(rtype.upper(), DNS_TYPE_PTR)`, so
    an `ANY`, an `NSEC` or a plain typo silently sent a **PTR** query and
    the following OUT step failed with a message pointing at the device.
    That contradicts the runner's stated "deliberately loud, never a
    silent no-op" policy, so an unknown type is now a `ValueError` naming
    what is supported.
    """
    key = (rtype or "").strip().upper()
    try:
        return _TYPE_BY_NAME[key]
    except KeyError:
        raise ValueError(
            f"unsupported mDNS RecordType {rtype!r}: this transport speaks "
            f"{', '.join(sorted(_TYPE_BY_NAME))}. No query can be sent for "
            "it — add the type here rather than sending a PTR and letting "
            "the step fail as if the device had answered wrong.") from None


def _iface_index(interface: Optional[str] = None) -> int:
    """Resolve an interface name or numeric index to a scope index.

    Finding M14: `OSError` from `if_nametoindex` used to be swallowed and
    0 returned, so a typo'd `DEVICE_IFACE` collapsed the join list to the
    wildcard alone — the single-interface configuration the historical
    silent-empty bug was about — with no warning at all. An unresolvable
    name is now an `MdnsInterfaceError`. An ABSENT setting still means 0,
    which is the legitimate "no interface configured, use the wildcard"
    case.
    """
    iface = interface or os.environ.get("DEVICE_IFACE")
    if not iface:
        return 0
    try:
        return int(iface)
    except ValueError:
        pass
    if not hasattr(socket, "if_nametoindex"):
        raise MdnsInterfaceError(
            f"cannot resolve interface {iface!r}: socket.if_nametoindex is "
            "unavailable on this platform — set DEVICE_IFACE to a numeric "
            "scope index instead")
    try:
        return socket.if_nametoindex(iface)
    except OSError as exc:
        raise MdnsInterfaceError(
            f"unknown network interface {iface!r} "
            f"({type(exc).__name__}: {exc}) — check DEVICE_IFACE. Falling "
            f"back to the wildcard index would send queries out whatever "
            f"route the kernel picks for {MDNS_MULTICAST_ADDR}, and every "
            "discovery step would report 'received nothing'.") from exc


def _build_query(qname: str, qtype: int, unicast: bool = False) -> bytes:
    """Build a one-question mDNS query packet.

    `unicast` sets the QU bit in QCLASS (RFC 6762 §5.4) — finding M13.
    """
    header = struct.pack("!HHHHHH", 0, 0, 1, 0, 0, 0)
    qclass = DNS_CLASS_IN | (DNS_QCLASS_UNICAST if unicast else 0)
    return header + encode_dns_name(qname) + struct.pack("!HH", qtype,
                                                         qclass)


class MdnsListener:
    """A bound mDNS listener, started BEFORE the query that provokes an
    answer — same arm-then-send shape as CoapClient.arm_group_listener,
    and for the same reason: the DUT answers fast enough that binding
    afterwards races the response.
    """

    def __init__(self, interface: Optional[str] = None,
                 timeout: float = 3.0, await_live: bool = False):
        """`await_live`: don't stop at a goodbye-only (TTL=0) packet — keep
        reading until a live record arrives or the timeout expires. Set it
        when listening for an announcement provoked by a STATE CHANGE, which
        the device precedes with a goodbye for the old state.

        Raises whatever stopped the listener from coming up — an
        unresolvable `DEVICE_IFACE` (M14), a bind or group-join failure
        (M17) — rather than returning an object that looks healthy while
        `repeat_query` pumps queries into the void.
        """
        self._timeout = timeout
        self._interface = interface
        self._await_live = await_live
        self._records: list[DnsRecord] = []
        self._error: Optional[BaseException] = None
        self._packets = 0
        self._ready = threading.Event()
        self._listening = threading.Event()
        self._packet_seen = threading.Event()
        # Built once so the read loop can recognise our own reflected
        # probe by identity of content and keep it out of the M8 counter.
        self._join_probe = _build_query(_JOIN_PROBE_NAME, DNS_TYPE_PTR)
        # M14: resolve BEFORE starting the thread, so a bad DEVICE_IFACE
        # raises out of the constructor at the call site that configured
        # it, not inside a daemon thread nobody is watching.
        self._iface_idx = _iface_index(interface)
        self._thread = threading.Thread(target=self._listen, daemon=True)
        self._thread.start()
        self._ready.wait(timeout=2.0)
        # M17: `_ready` is also set from the `finally`, so it fires for a
        # listener that DIED — a bind or join failure used to surface only
        # at records(), by which time repeat_query had been sending into
        # nothing for the whole timeout. `_listening` is set ONLY on the
        # success path, right after the join, so the two together
        # distinguish "up" from "already over".
        if not self._listening.is_set():
            self._thread.join(timeout=1.0)
            if self._error is not None:
                raise self._error
            if not self._listening.is_set():
                raise MdnsInterfaceError(
                    "mDNS listener did not become ready within 2s and "
                    "reported no error — the listen thread is wedged "
                    "before the group join")
        # M18: the fixed sleep was a guess at MLD report propagation with
        # no feedback of any kind, and this module's history says an
        # unconfirmed join is exactly what an intermittent empty result
        # set looks like. Confirm the membership by round-tripping a probe
        # and only fall back to the sleep when that cannot be done.
        if not self._await_join():
            time.sleep(0.15)

    def _await_join(self, timeout: float = 0.5) -> bool:
        """Verify the multicast membership by round-tripping a probe.

        Returns True once a datagram — any datagram — has reached the
        listen socket, which is the only direct evidence that the join
        took effect (finding M18).

        The probe is a query for a name no responder owns
        (`_knx-join-probe.invalid.`), so a DUT or a neighbour that sees it
        answers nothing and the only copy that matters is our own,
        reflected by `IPV6_MULTICAST_LOOP`. A False return means "could
        not confirm", not "broken": a link with multicast loopback
        disabled is still a working link, so the caller falls back to the
        historical sleep rather than failing.
        """
        probe = self._join_probe
        deadline = time.monotonic() + timeout
        try:
            sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM,
                                 socket.IPPROTO_UDP)
        except OSError as exc:
            _dbg(f"[mdns] join probe socket failed: {exc}")
            return False
        try:
            sock.setsockopt(socket.IPPROTO_IPV6,
                            socket.IPV6_MULTICAST_HOPS, 1)
            try:
                sock.setsockopt(socket.IPPROTO_IPV6,
                                socket.IPV6_MULTICAST_LOOP, 1)
            except (OSError, AttributeError):
                return False
            sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_MULTICAST_IF,
                            struct.pack("I", self._iface_idx))
            while time.monotonic() < deadline:
                try:
                    sock.sendto(probe, (MDNS_MULTICAST_ADDR, MDNS_PORT, 0,
                                        self._iface_idx))
                except OSError as exc:
                    _dbg(f"[mdns] join probe send failed: {exc}")
                    return False
                if self._packet_seen.wait(timeout=0.05):
                    _dbg("[mdns] join confirmed by loopback probe")
                    return True
            _dbg("[mdns] join probe unconfirmed, falling back to the sleep")
            return False
        finally:
            sock.close()

    def _listen(self) -> None:
        # M20: socket() must be INSIDE the try. Constructing it outside meant
        # a failure here escaped _listen entirely — self._error stayed None
        # (so records() reported an innocent empty set instead of the real
        # OSError), self._ready was never set (so every construction burned
        # the full 2s wait), and the finally block referenced an unbound
        # `sock`. That is precisely the "thread died silently" case _error
        # exists to prevent.
        sock = None
        try:
            sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM,
                                 socket.IPPROTO_UDP)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            if hasattr(socket, "SO_REUSEPORT"):
                sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
            sock.bind(("", MDNS_PORT))
            # Join on the WILDCARD interface, the named one, AND every other
            # IPv6 interface on the host.
            #
            # Wildcard + named is not enough on its own: it works until the
            # DUT re-opens its own mDNS socket (which a factory reset makes
            # it do), after which its answers stop arriving on that
            # membership and every query looks unanswered — while the device
            # log happily shows send_query_response. Joining index 0 too
            # receives them again (verified: 0 records -> 100 records).
            #
            # This pair IS enough for the DUT's unsolicited announcements
            # too — they arrive on the wildcard membership even though the
            # device logs them against its own interface index. Joining
            # every interface was tried and made no difference (5.1 passes
            # 42/42 either way), so the narrower join stays: it avoids a
            # spurious "join on iface N failed: Address already in use".
            #
            # ORDER MATTERS: the named interface first, the wildcard second.
            # A wildcard join makes the kernel pick an interface by route;
            # when that interface IS the named one (GitLab CI: veth-test is
            # the only interface with a multicast route), the explicit join
            # that follows fails with EADDRINUSE - the membership exists, but
            # the join call reports it as a failure, and M14's check then
            # aborted every 5.1.2 case in CI while the Docker runs, whose
            # container has an eth0 the wildcard join landed on, never saw
            # it. Named first cannot collide with anything; EADDRINUSE on the
            # wildcard afterwards just means it resolved to the interface we
            # already joined, which is the wanted state, so it counts as
            # joined too.
            wanted = list(dict.fromkeys([self._iface_idx, 0]))
            joined = []
            failures = []
            for idx in wanted:
                try:
                    mreq = socket.inet_pton(socket.AF_INET6,
                                            MDNS_MULTICAST_ADDR)
                    mreq += struct.pack("I", idx)
                    sock.setsockopt(socket.IPPROTO_IPV6,
                                    socket.IPV6_JOIN_GROUP, mreq)
                    joined.append(idx)
                except OSError as exc:
                    if exc.errno == errno.EADDRINUSE and joined:
                        # Already a member of this group on the interface
                        # the kernel resolved this join to - the earlier
                        # join covers it.
                        _dbg(f"[mdns] join on iface {idx}: already a member "
                             f"via {joined}, fine")
                        joined.append(idx)
                        continue
                    failures.append(f"{idx}: {type(exc).__name__}: {exc}")
                    _dbg(f"[mdns] join on iface {idx} failed: {exc}")
            if not joined:
                raise MdnsInterfaceError(
                    f"could not join {MDNS_MULTICAST_ADDR} on any interface "
                    f"(tried {wanted}) — {'; '.join(failures)}")
            # M14, second half: joining ONLY the wildcard when a specific
            # interface was asked for IS the degraded single-interface
            # configuration the historical silent-empty bug was about, so
            # it is a setup failure rather than a debug line nobody reads.
            if self._iface_idx and self._iface_idx not in joined:
                raise MdnsInterfaceError(
                    f"joined {MDNS_MULTICAST_ADDR} on the wildcard index "
                    f"only; the configured interface {self._iface_idx} "
                    f"failed — {'; '.join(failures)}. A wildcard-only join "
                    "is the configuration that silently returns no records "
                    "once the DUT re-opens its mDNS socket.")
            sock.settimeout(0.5)
            _dbg(f"[mdns] listening on [{MDNS_MULTICAST_ADDR}]:{MDNS_PORT} "
                 f"ifaces={joined}")
            self._listening.set()
            self._ready.set()
            deadline = time.monotonic() + self._timeout
            while time.monotonic() < deadline:
                try:
                    data, _addr = sock.recvfrom(9000)
                except socket.timeout:
                    continue
                except OSError:
                    break
                # `_packet_seen` is the join barrier's evidence and fires
                # for ANY datagram, our own reflected probe included —
                # that is exactly what confirms the membership. The
                # `_packets` counter is M8's "did the DUT say anything"
                # signal, so the probe is excluded from it; otherwise
                # every listener would claim to have received traffic.
                self._packet_seen.set()
                if data != self._join_probe:
                    self._packets += 1
                try:
                    parsed = parse_records(data, require_response=True)
                except (struct.error, ValueError, IndexError,
                        UnicodeDecodeError) as exc:
                    # M2: a per-packet parse failure must NOT be fatal.
                    # ff02::fb is a shared group — anything else on the link
                    # (a printer, another rig, a _companion-link browser)
                    # multicasts here too and this listener does not filter
                    # by source. Letting one such packet escape to the outer
                    # handler would store it in self._error, and records()
                    # raises that BEFORE returning, so a good result set
                    # already collected would be thrown away and reported as
                    # a device failure. Skip the packet, keep the records.
                    #
                    # Narrowed rather than bare `Exception` (same reasoning as
                    # O3/T1): these four are what a malformed DATAGRAM can
                    # actually produce out of parse_records — struct.error on
                    # a short header, ValueError from inet_ntop, IndexError
                    # on an overrun, UnicodeDecodeError on a bad label. A
                    # TypeError/AttributeError means OUR parser is broken and
                    # must not be silently swallowed as "foreign traffic".
                    # (RecursionError is not in the list: decode_dns_name
                    # already breaks compression-pointer loops via `seen`.)
                    #
                    # M9/M11/M12 land here BY DESIGN: MdnsParseError is a
                    # ValueError, so a malformed name, an overrun TXT and a
                    # foreign QUERY are each skipped with a stated reason in
                    # the debug log instead of yielding a short name, an
                    # empty dict, or another device's records.
                    _dbg(f"[mdns] skipping unparseable packet "
                         f"({len(data)} bytes): {type(exc).__name__}: {exc}")
                    continue
                for rec in parsed:
                    self._records.append(rec)
                    _dbg(f"[mdns] rx {rec.rtype} {rec.name!r} -> "
                         f"{rec.target!r} port={rec.port} ttl={rec.ttl}")
                if self._records:
                    # A DUT answers a query with its whole record set in one
                    # or two packets, so stop as soon as anything arrives
                    # rather than sitting out the timeout on every step.
                    #
                    # A STATE CHANGE is different: the device sends a
                    # goodbye (TTL=0) for the old advertisement and only
                    # THEN announces the new one
                    # (dns-sd_mdns.c knx_dns_sd_update_service). Breaking on
                    # the goodbye loses the announcement that actually
                    # carries the new state — which is exactly what
                    # 5.1.2.5a Step 2 (enable pm) asserts on: the goodbye
                    # still has pm=0 and so omits the _pm._sub PTR, the
                    # announce that follows has pm=1 and includes it. So
                    # when the caller is waiting for a live record, keep
                    # reading while everything seen so far is a goodbye.
                    if not (self._await_live
                            and all(r.ttl == 0 for r in self._records)):
                        break
        except Exception as exc:            # noqa: BLE001
            # Record rather than swallow: a bind/join failure here used to
            # leave the constructor happily waiting on a thread that had
            # already died, so every step reported "received nothing" with
            # no hint why.
            self._error = exc
            _dbg(f"[mdns] listener failed: {type(exc).__name__}: {exc}")
        finally:
            self._ready.set()
            if sock is not None:
                try:
                    sock.close()
                except OSError:
                    pass

    def repeat_query(self, qname: str, rtype: str,
                     every: float = 0.75) -> None:
        """Re-send `qname`/`rtype` every `every` seconds until the listener
        stops, from a background thread.

        A single query is not reliable: an mDNS responder delays its reply
        (RFC 6762 §6), and a device that has just restarted its responder
        (e.g. after a factory reset) silently drops anything that arrives
        while its listen socket is being re-opened.

        The FIRST query sets the QU bit (finding M13). RFC 6762 §5.4 lets
        a responder answer a QU question immediately by unicast instead of
        applying the §6 20-120 ms delay, the §6.3 rate limit and §7.1
        known-answer suppression — and three identical QM queries inside
        3 s is precisely what that damping targets. The retries stay QM,
        so the answer is still multicast to the group if the DUT prefers
        that; this listener is bound to `("", 5353)` and receives either.

        Safe against this DUT specifically: `mdns.h::mdns_socket_listen`
        masks 0x8000 off the question class before its IN/ANY check, and
        `dns-sd_mdns.c::mdns_query_callback` takes `(void)rclass` and
        always answers via `mdns_query_answer_multicast`. So QU changes
        nothing about WHAT the device sends — it only removes the excuse
        for delaying it.
        """
        # M16: validate here, in the caller's thread. An unknown
        # RecordType raised inside `_pump` would die in a daemon thread
        # and look exactly like a device that never answered.
        dns_type_code(rtype)

        def _pump():
            deadline = time.monotonic() + self._timeout
            unicast = True
            while time.monotonic() < deadline and not self._records:
                send_query(qname, rtype, interface=self._interface,
                           unicast=unicast)
                unicast = False
                time.sleep(every)

        threading.Thread(target=_pump, daemon=True).start()

    def packets_received(self) -> int:
        """How many datagrams reached the socket, parseable or not.

        Finding M8: the read loop exits identically whether the deadline
        expired with zero packets or the break fired, so `records() == []`
        was equally consistent with a device failure, a join failure, a
        wrong-interface send and a packet this parser rejected. A caller
        can now tell "nothing arrived at all" from "packets arrived but
        none of them were ours", and say which in its failure message.
        (M8 itself is a runner-side finding; this is the signal it needs.)
        """
        return self._packets

    def records(self) -> list[DnsRecord]:
        """Join the listener and return everything captured.

        Raises whatever killed the listener thread, so a bind/join problem
        surfaces as itself instead of as an empty result set.
        """
        self._thread.join(timeout=self._timeout + 2)
        if self._error is not None:
            raise self._error
        return list(self._records)


def send_query(qname: str, rtype: str, interface: Optional[str] = None,
               unicast: bool = False) -> None:
    """Send one mDNS query to the link-local multicast group.

    `unicast` sets the QU bit (RFC 6762 §5.4) — see
    `MdnsListener.repeat_query` for why, and why it is safe here.
    """
    qtype = dns_type_code(rtype)
    idx = _iface_index(interface)
    sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM,
                         socket.IPPROTO_UDP)
    try:
        sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_MULTICAST_HOPS, 255)
        # M15: this used to be `if idx:`, so with no scope index the
        # setsockopt was skipped and the sendto scope-id was 0 — on a
        # multi-homed host the kernel then picks the route for ff02::fb
        # itself, frequently not the test interface, and every 5.1 step
        # reports "received nothing" while the queries go out the wrong
        # NIC. Setting it unconditionally is a no-op for idx == 0 (that IS
        # "let the kernel choose") but makes the choice explicit and keeps
        # the sockopt and the sendto scope-id in agreement.
        sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_MULTICAST_IF,
                        struct.pack("I", idx))
        _dbg(f"[mdns] tx query {rtype} {qname!r} iface_idx={idx} "
             f"qu={unicast}")
        sock.sendto(_build_query(qname, qtype, unicast=unicast),
                    (MDNS_MULTICAST_ADDR, MDNS_PORT, 0, idx))
    finally:
        sock.close()


def query(qname: str, rtype: str, interface: Optional[str] = None,
          timeout: float = 3.0) -> list[DnsRecord]:
    """Arm a listener, send the query, and return the captured records."""
    listener = MdnsListener(interface=interface, timeout=timeout)
    send_query(qname, rtype, interface=interface, unicast=True)
    return listener.records()
