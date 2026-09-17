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
"""

from __future__ import annotations

import os
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

_TYPE_BY_NAME = {
    "PTR": DNS_TYPE_PTR,
    "TXT": DNS_TYPE_TXT,
    "AAAA": DNS_TYPE_AAAA,
    "SRV": DNS_TYPE_SRV,
}
_NAME_BY_TYPE = {v: k for k, v in _TYPE_BY_NAME.items()}

_DEBUG = os.environ.get("KNX_TEST_DEBUG_MDNS") == "1"


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


def decode_dns_name(data: bytes, offset: int) -> tuple[str, int]:
    """Decode a DNS wire-format name, following compression pointers.

    Returns (name_with_trailing_dot, offset_just_past_the_name).
    """
    labels: list[str] = []
    seen: set[int] = set()
    jumped = False
    ret = offset
    while True:
        if offset >= len(data) or offset in seen:
            break
        seen.add(offset)
        length = data[offset]
        if length == 0:
            if not jumped:
                ret = offset + 1
            break
        if (length & 0xC0) == 0xC0:
            if offset + 1 >= len(data):
                break
            ptr = ((length & 0x3F) << 8) | data[offset + 1]
            if not jumped:
                ret = offset + 2
            jumped = True
            offset = ptr
            continue
        offset += 1
        if offset + length > len(data):
            break
        labels.append(data[offset:offset + length].decode("ascii",
                                                          errors="replace"))
        offset += length
        if not jumped:
            ret = offset
    return ".".join(labels) + ".", ret


def parse_records(data: bytes) -> list[DnsRecord]:
    """Parse every answer/authority/additional record in an mDNS packet."""
    if len(data) < 12:
        return []
    _, _, qd, an, ns, ar = struct.unpack("!HHHHHH", data[:12])
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
            break
        rec_name = name.rstrip(".")
        if rtype == DNS_TYPE_PTR:
            target, _ = decode_dns_name(data, off)
            out.append(DnsRecord("PTR", rec_name, target.rstrip("."),
                                 ttl=ttl))
        elif rtype == DNS_TYPE_SRV and rdlen >= 6:
            _prio, _weight, port = struct.unpack("!HHH", rdata[:6])
            host, _ = decode_dns_name(data, off + 6)
            out.append(DnsRecord("SRV", rec_name, host.rstrip("."),
                                 port=port, ttl=ttl))
        elif rtype == DNS_TYPE_AAAA and len(rdata) == 16:
            addr = socket.inet_ntop(socket.AF_INET6, rdata)
            out.append(DnsRecord("AAAA", rec_name, addr, ttl=ttl))
        elif rtype == DNS_TYPE_TXT:
            props: dict = {}
            i = 0
            while i < len(rdata):
                n = rdata[i]
                i += 1
                item = rdata[i:i + n]
                i += n
                if b"=" in item:
                    k, v = item.split(b"=", 1)
                    props[k.decode("ascii", "replace")] = v.decode("ascii",
                                                                   "replace")
            out.append(DnsRecord("TXT", rec_name, ttl=ttl, txt=props))
        off += rdlen
    return out


def _iface_index(interface: Optional[str] = None) -> int:
    iface = interface or os.environ.get("DEVICE_IFACE")
    if not iface:
        return 0
    try:
        return int(iface)
    except ValueError:
        try:
            return socket.if_nametoindex(iface)
        except (OSError, AttributeError):
            return 0


def _build_query(qname: str, qtype: int) -> bytes:
    header = struct.pack("!HHHHHH", 0, 0, 1, 0, 0, 0)
    return header + encode_dns_name(qname) + struct.pack("!HH", qtype,
                                                         DNS_CLASS_IN)


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
        the device precedes with a goodbye for the old state."""
        self._timeout = timeout
        self._interface = interface
        self._await_live = await_live
        self._records: list[DnsRecord] = []
        self._error: Optional[BaseException] = None
        self._ready = threading.Event()
        self._iface_idx = _iface_index(interface)
        self._thread = threading.Thread(target=self._listen, daemon=True)
        self._thread.start()
        self._ready.wait(timeout=2.0)
        time.sleep(0.15)          # let the bind/join settle

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
            joined = []
            for idx in dict.fromkeys([0, self._iface_idx]):
                try:
                    mreq = socket.inet_pton(socket.AF_INET6,
                                            MDNS_MULTICAST_ADDR)
                    mreq += struct.pack("I", idx)
                    sock.setsockopt(socket.IPPROTO_IPV6,
                                    socket.IPV6_JOIN_GROUP, mreq)
                    joined.append(idx)
                except OSError as exc:
                    _dbg(f"[mdns] join on iface {idx} failed: {exc}")
            if not joined:
                raise OSError(
                    f"could not join {MDNS_MULTICAST_ADDR} on any interface "
                    f"(tried 0 and {self._iface_idx})")
            sock.settimeout(0.5)
            _dbg(f"[mdns] listening on [{MDNS_MULTICAST_ADDR}]:{MDNS_PORT} "
                 f"ifaces={joined}")
            self._ready.set()
            deadline = time.monotonic() + self._timeout
            while time.monotonic() < deadline:
                try:
                    data, _addr = sock.recvfrom(9000)
                except socket.timeout:
                    continue
                except OSError:
                    break
                try:
                    parsed = parse_records(data)
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
        """
        def _pump():
            deadline = time.monotonic() + self._timeout
            while time.monotonic() < deadline and not self._records:
                send_query(qname, rtype, interface=self._interface)
                time.sleep(every)

        threading.Thread(target=_pump, daemon=True).start()

    def records(self) -> list[DnsRecord]:
        """Join the listener and return everything captured.

        Raises whatever killed the listener thread, so a bind/join problem
        surfaces as itself instead of as an empty result set.
        """
        self._thread.join(timeout=self._timeout + 2)
        if self._error is not None:
            raise self._error
        return list(self._records)


def send_query(qname: str, rtype: str,
               interface: Optional[str] = None) -> None:
    """Send one mDNS query to the link-local multicast group."""
    qtype = _TYPE_BY_NAME.get(rtype.upper(), DNS_TYPE_PTR)
    idx = _iface_index(interface)
    sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM,
                         socket.IPPROTO_UDP)
    try:
        sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_MULTICAST_HOPS, 255)
        if idx:
            sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_MULTICAST_IF,
                            struct.pack("I", idx))
        _dbg(f"[mdns] tx query {rtype} {qname!r} iface_idx={idx}")
        sock.sendto(_build_query(qname, qtype),
                    (MDNS_MULTICAST_ADDR, MDNS_PORT, 0, idx))
    finally:
        sock.close()


def query(qname: str, rtype: str, interface: Optional[str] = None,
          timeout: float = 3.0) -> list[DnsRecord]:
    """Arm a listener, send the query, and return the captured records."""
    listener = MdnsListener(interface=interface, timeout=timeout)
    send_query(qname, rtype, interface=interface)
    return listener.records()
