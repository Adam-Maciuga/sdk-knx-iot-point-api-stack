"""
CoAP client for the runtime conformance tests, on raw UDP sockets.

WHY NOT A LIBRARY (aiocoap): conformance testing has to be able to put
DELIBERATELY NON-CONFORMANT frames on the wire — a corrupted OSCORE auth
tag (InvalidAuthTag), a replayed or offset sequence number (SeqNoOffset),
an unknown critical option, a mismatched token. A correctness-enforcing
library actively prevents exactly those, which is the point of the test.
KNX-IoT's OSCORE profile also deviates from RFC 8613 (see
tests/docs/knx-protocol-deviations.md). The original reason for dropping
aiocoap — its Context management exhausting after ~11 requests in a
shared session — still holds, but it is no longer the main one.

The client is SYNCHRONOUS but NOT stateless: it keeps a Message-ID
counter, a token counter, per-request correlation state and a buffer of
unsolicited requests from the DUT. Requests are retried (3 attempts; 1
when sending an adversarial frame, where a retry would corrupt the test).
Beyond plain request/response it also does CON/NON/ACK/RST handling,
piggybacked vs separate responses, OSCORE protect/unprotect, the echo
challenge, multicast group join/capture and observe.
"""

import os
import socket
import struct
import time
from dataclasses import dataclass, replace
from typing import Optional

# The only crypto import in this module. It is here rather than in a local
# import because it names an EXPECTED outcome on the multicast path — a
# group telegram encrypted for a different context — and catching it by
# name is what keeps `except Exception` out of the receive loop.
from cryptography.exceptions import InvalidTag

# CoAP message types
CON = 0
NON = 1
ACK = 2
RST = 3

# CoAP method codes
GET = (0, 1)
POST = (0, 2)
PUT = (0, 3)
DELETE = (0, 4)

# UDP receive buffer. Linux DISCARDS the remainder of an oversized datagram
# with no error, so an undersized buffer truncates silently — the failure
# then looks like a content mismatch. 64 KiB is the largest a UDP datagram
# can be, so nothing can be lost. (Was 4096: not reachable with the current
# template — largest observed payload is ~829 bytes and 5.2 chunks its
# firmware upload at PREP_BLOCK_SIZE=500 via ?ps=/?po= rather than RFC 7959
# blockwise — but a GET /.well-known/core from a resource-rich device is the
# realistic way to exceed it. See finding T9.)
_RECV_BUF = 65535

# Common content formats
LINK_FORMAT = 40
APPLICATION_CBOR = 60

# KNX IPv6 CoAP multicast addresses (scope -> address)
KNX_MULTICAST_ADDRESSES = {
    2: "ff02::fd",  # link-local
    3: "ff03::fd",  # realm-local
    5: "ff05::fd",  # site-local
}

# Default multicast scope — override via KNX_MULTICAST_SCOPE env var.
# 2 = link-local (Windows dev), 5 = site-local (production/CI).
_DEFAULT_MC_SCOPE = int(os.environ.get("KNX_MULTICAST_SCOPE", "5"))


# Debug print switches. Off by default so they do not spoil test output.
# Enable with KNX_TEST_DEBUG_OSCORE=1 and KNX_TEST_DEBUG_COAP=1.
_DEBUG_OSCORE = os.environ.get("KNX_TEST_DEBUG_OSCORE", "0") == "1"
_DEBUG_COAP = os.environ.get("KNX_TEST_DEBUG_COAP", "0") == "1"


def _dbg_oscore(*args, **kwargs):
    """Print an [oscore] debug line only when KNX_TEST_DEBUG_OSCORE=1."""
    if _DEBUG_OSCORE:
        print(*args, **kwargs)


def _dbg_coap(*args, **kwargs):
    """Print a [coap] debug line only when KNX_TEST_DEBUG_COAP=1."""
    if _DEBUG_COAP:
        print(*args, **kwargs)


def _parse_oscore_option(value: bytes) -> tuple[Optional[bytes],
                                                Optional[bytes]]:
    """Split an OSCORE option value into (kid, partial_iv).

    Layout per RFC 8613 §6.1 (and knx_oscore._build_oscore_option): a
    flags byte whose low 3 bits give the PIV length, bit 3 marks a kid and
    bit 4 a kid context, followed by the PIV, then a length-prefixed kid
    context, then the kid (running to the end of the option).
    """
    if not value:
        return None, None
    flags = value[0]
    pos = 1
    piv_len = flags & 0x07
    piv = value[pos:pos + piv_len] if piv_len else None
    pos += piv_len
    if flags & 0x10:                       # kid context present
        if pos >= len(value):
            return None, piv
        ctx_len = value[pos]
        pos += 1 + ctx_len
    kid = value[pos:] if flags & 0x08 else None
    return kid, piv


def knx_group_multicast_address(grpid: int, iid: int,
                                scope: int = _DEFAULT_MC_SCOPE) -> str:
    """Compute the KNX IPv6 multicast address for group communication.

    Address format (RFC 3306):
      FF3{scope}:0030:FD{IID_bytes}:0000:{grpid_bytes}

    Args:
        grpid: 32-bit group identifier (ULA style has MSB=1)
        iid: 40-bit installation identifier
        scope: IPv6 multicast scope (2=link, 3=realm, 5=site)

    Returns:
        IPv6 multicast address string.
    """
    b = bytearray(16)
    b[0] = 0xFF
    b[1] = 0x30 + scope
    b[2] = 0x00
    b[3] = 0x30
    b[4] = 0xFD
    b[5] = (iid >> 32) & 0xFF
    b[6] = (iid >> 24) & 0xFF
    b[7] = (iid >> 16) & 0xFF
    b[8] = (iid >> 8) & 0xFF
    b[9] = iid & 0xFF
    b[10] = 0x00
    b[11] = 0x00
    b[12] = (grpid >> 24) & 0xFF
    b[13] = (grpid >> 16) & 0xFF
    b[14] = (grpid >> 8) & 0xFF
    b[15] = grpid & 0xFF
    return socket.inet_ntop(socket.AF_INET6, bytes(b))


@dataclass
class CoapResponse:
    """Parsed CoAP response."""
    code_class: int
    code_detail: int
    payload: bytes
    content_format: Optional[int]
    msg_type: int
    mid: int
    token: bytes
    options: Optional[dict] = None
    source_addr: Optional[tuple] = None  # (host, port, flowinfo, scope_id)
    echo_value: Optional[bytes] = None  # CoAP Echo option (252) if present
    # Outer OSCORE option fields of the message this was decrypted from,
    # kept for group telegrams because a tool-sent 4.01 Echo challenge has
    # to quote the challenged telegram's kid and Partial IV back verbatim
    # (see CoapClient.send_echo_challenge).
    oscore_kid: Optional[bytes] = None
    oscore_piv: Optional[bytes] = None

    @property
    def code(self) -> str:
        """Return the response code as 'X.YY' string."""
        return f"{self.code_class}.{self.code_detail:02d}"

    @property
    def is_successful(self) -> bool:
        return self.code_class == 2

    @property
    def is_forbidden(self) -> bool:
        return self.code_class == 4 and self.code_detail == 3

    @property
    def is_not_found(self) -> bool:
        return self.code_class == 4 and self.code_detail == 4

    @property
    def is_method_not_allowed(self) -> bool:
        return self.code_class == 4 and self.code_detail == 5

    @property
    def is_bad_request(self) -> bool:
        return self.code_class == 4 and self.code_detail == 0


@dataclass
class ObserveSubscription:
    """Handle for an active CoAP Observe relationship.

    Tracks the token (the subscription identifier the server echoes on
    every notification), the last seen Observe sequence number, and — for
    OSCORE-protected subscriptions — the OSCORE context plus the partial IV
    of the original registration request (needed to build the AAD when
    decrypting server-initiated notifications, RFC 8613 §8.3).
    """
    path: str
    token: bytes
    oscore_ctx: object = None          # OscoreContext or None (plain)
    request_piv: Optional[bytes] = None  # PIV of the observe GET request
    last_seq: Optional[int] = None     # last Observe option value seen

    @property
    def is_oscore(self) -> bool:
        return self.oscore_ctx is not None


@dataclass
class Notification:
    """A single observe notification delivered to a subscriber."""
    response: "CoapResponse"
    seq: Optional[int]   # Observe sequence number (option 6)
    msg_type: int        # CON or NON
    mid: int


def _encode_option(delta: int, value: bytes) -> bytes:
    """Encode a single CoAP option."""
    length = len(value)

    # Extended delta encoding
    if delta < 13:
        d = delta
        d_ext = b""
    elif delta < 269:
        d = 13
        d_ext = struct.pack("B", delta - 13)
    else:
        d = 14
        d_ext = struct.pack("!H", delta - 269)

    # Extended length encoding
    if length < 13:
        l = length
        l_ext = b""
    elif length < 269:
        l = 13
        l_ext = struct.pack("B", length - 13)
    else:
        l = 14
        l_ext = struct.pack("!H", length - 269)

    return struct.pack("B", (d << 4) | l) + d_ext + l_ext + value


class CoapFormatError(ValueError):
    """A received datagram is not well-formed CoAP (RFC 7252 §3).

    Raised instead of returning half-parsed data: a caller that cannot
    trust the option set cannot trust the payload offset either, and
    silently truncating a malformed option would hand the test suite a
    plausible-looking but wrong value. Every receive loop catches this and
    skips the datagram.
    """


def _parse_options(data: bytes, offset: int,
                   repeated: Optional[dict] = None) -> tuple[dict, int]:
    """Parse CoAP options. Returns ({option_number: value}, payload_offset).

    The returned dict holds ONE value per option number (the last seen),
    which is what every caller here wants for the non-repeatable options
    they read: Content-Format (12), OSCORE (9), Observe (6), Echo (252).

    Several CoAP options are repeatable, though — Uri-Path (11) and
    Uri-Query (15) in particular, where "a/b/c" is three separate option
    instances. Pass a dict as `repeated` to collect every instance
    ({option_number: [v1, v2, ...]}); the flat return value alone would
    silently reduce that path to "c".

    Raises CoapFormatError on anything malformed rather than guessing:
    a truncated extension byte, an option whose length runs past the end
    of the datagram, or the reserved 15 nibble (RFC 7252 §3.1 — "MUST be
    processed as a message format error").
    """
    options: dict[int, bytes] = {}
    opt_num = 0
    end = len(data)

    while offset < end:
        byte = data[offset]
        if byte == 0xFF:
            # Payload marker. RFC 7252 §3: it MUST be followed by a
            # non-empty payload.
            offset += 1
            if offset >= end:
                raise CoapFormatError(
                    "payload marker (0xFF) with no payload following")
            break

        delta = (byte >> 4) & 0x0F
        length = byte & 0x0F
        offset += 1

        if delta == 15 or length == 15:
            raise CoapFormatError(
                f"reserved nibble 15 in option header at offset {offset - 1}")

        if delta == 13:
            if offset + 1 > end:
                raise CoapFormatError("truncated option delta extension")
            delta = data[offset] + 13
            offset += 1
        elif delta == 14:
            if offset + 2 > end:
                raise CoapFormatError("truncated option delta extension")
            delta = struct.unpack("!H", data[offset:offset + 2])[0] + 269
            offset += 2

        if length == 13:
            if offset + 1 > end:
                raise CoapFormatError("truncated option length extension")
            length = data[offset] + 13
            offset += 1
        elif length == 14:
            if offset + 2 > end:
                raise CoapFormatError("truncated option length extension")
            length = struct.unpack("!H", data[offset:offset + 2])[0] + 269
            offset += 2

        if offset + length > end:
            raise CoapFormatError(
                f"option {opt_num + delta} declares {length} byte(s) but only "
                f"{end - offset} remain")

        opt_num += delta
        value = data[offset:offset + length]
        options[opt_num] = value
        if repeated is not None:
            repeated.setdefault(opt_num, []).append(value)
        offset += length

    return options, offset


# ---------------------------------------------------------------------------
# Block-wise transfer (RFC 7959)
# ---------------------------------------------------------------------------
#
# The DUT paginates any link-format body that does not fit its configured
# payload buffer (KNX_PAYLOAD_SIZE).  It then answers with Block2 (option 23)
# carrying the "more" bit and, on the first block, Size2 (option 28) with the
# total body length.  A client that ignores Block2 silently sees only the
# first block, which looks exactly like a truncated payload.  See F-016.

BLOCK2_OPTION = 23
SIZE2_OPTION = 28

# RFC 7959 4.6: SZX is log2(block size) - 4, so SZX 0..6 == 16..1024 bytes.
BLOCK_SZX_MAX = 6


def _decode_block_option(value: bytes) -> tuple[int, bool, int]:
    """Decode a Block1/Block2 option value into (num, more, size)."""
    raw = int.from_bytes(value, "big") if value else 0
    num = raw >> 4
    more = bool(raw & 0x08)
    szx = raw & 0x07
    return num, more, 16 << szx


def _encode_block_option(num: int, more: bool, size: int) -> bytes:
    """Encode a Block1/Block2 option value from (num, more, size)."""
    szx = max(0, min(BLOCK_SZX_MAX, (size.bit_length() - 1) - 4))
    raw = (num << 4) | (0x08 if more else 0x00) | szx
    return raw.to_bytes((raw.bit_length() + 7) // 8, "big") if raw else b""


class CoapClient:
    """
    Minimal synchronous CoAP client over UDP/IPv6.

    Uses a single socket for all requests (same source port),
    which avoids server-side resource exhaustion.
    """

    def __init__(self, host: str = "::1", port: int = 5683,
                 timeout: float = 5.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self._mid = 1000
        self._token_counter = 0
        self._sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
        self._sock.settimeout(timeout)
        self._sock.bind(("", 0))
        # Unsolicited REQUESTS the DUT sent us that arrived while we were
        # reading a response, kept as (parsed_message, source_addr) so a later
        # step can still claim them. See _recv_oscore_response, where they used
        # to be discarded as stale, and UnicastTelegramListener, which drains
        # this before reading the socket. EITT gets this for free by keeping
        # one socket permanently open; this list is our equivalent.
        self._pending_requests: list[tuple[dict, tuple]] = []

        # Why the last _recv_response returned None: one entry per datagram
        # that arrived but was NOT usable as the response. Empty means the
        # device really was silent. See T10 in eitt-interpreter-findings.md —
        # without this a token/MID correlation bug is indistinguishable from
        # a dead device.
        self._last_discards: list[str] = []

    def close(self):
        self._sock.close()

    @property
    def source_port(self) -> int:
        """Return the local UDP port this client is bound to."""
        return self._sock.getsockname()[1]

    def _next_mid(self) -> int:
        mid = self._mid
        self._mid = (self._mid + 1) & 0xFFFF
        return mid

    def _next_token(self) -> bytes:
        """A fresh 8-byte token, unique for the life of the process.

        Was a 2-byte counter starting at 0 for EVERY new CoapClient, which
        made tokens both predictable and REPEATED across client instances:
        a delayed reply to a previous client's first request carried token
        0x0000 and would be accepted as the answer to the new client's
        first request. Correlation is token-only here (see `_recv_response`),
        so that is the whole of the protection.

        The random half makes a collision with an in-flight stale response
        negligible; the counter half guarantees no two tokens from this
        process are equal even if os.urandom repeats. RFC 7252 §5.3.1 wants
        tokens that are not trivially guessable.
        """
        self._token_counter += 1
        return (os.urandom(4)
                + struct.pack("!I", self._token_counter & 0xFFFFFFFF))

    def _build_request(self, method: tuple, uri_path: str,
                       payload: bytes = b"",
                       accept: Optional[int] = None,
                       content_format: Optional[int] = None,
                       observe: Optional[int] = None,
                       token: Optional[bytes] = None,
                       block2: Optional[bytes] = None) -> bytes:
        """Build a CoAP CON request message.

        Args:
            observe: If not None, encode the CoAP Observe option (6).
                     0 = register, 1 = deregister (RFC 7641).
            token: If provided, use this exact token instead of generating
                   a fresh one (needed to deregister/refresh an existing
                   observe relationship, which is keyed by token).
            block2: If not None, encode the CoAP Block2 option (23) to
                    request a specific block of a paginated response
                    (RFC 7959).
        """
        mid = self._next_mid()
        if token is None:
            token = self._next_token()
        tkl = len(token)

        # Split path?query
        if "?" in uri_path:
            path_part, query_part = uri_path.split("?", 1)
        else:
            path_part, query_part = uri_path, None

        # Header
        ver_type_tkl = (1 << 6) | (CON << 4) | tkl
        code = (method[0] << 5) | method[1]
        header = struct.pack("!BBH", ver_type_tkl, code, mid)

        # Options (must be in order of option number)
        options = b""
        prev_opt = 0

        # Observe (option 6) -- comes before Uri-Path (11)
        if observe is not None:
            # RFC 7641: value 0 is encoded as a zero-length option.
            obs_bytes = (b"" if observe == 0
                         else observe.to_bytes(
                             (observe.bit_length() + 7) // 8 or 1, "big"))
            options += _encode_option(6 - prev_opt, obs_bytes)
            prev_opt = 6

        # Uri-Path (option 11) -- split by /
        for part in path_part.strip("/").split("/"):
            if not part:
                continue
            delta = 11 - prev_opt
            prev_opt = 11
            options += _encode_option(delta, part.encode())

        # Content-Format (option 12)
        if content_format is not None:
            delta = 12 - prev_opt
            prev_opt = 12
            options += _encode_option(delta,
                                      struct.pack("B", content_format))

        # Uri-Query (option 15) -- split by &
        if query_part:
            for qp in query_part.split("&"):
                delta = 15 - prev_opt
                prev_opt = 15
                options += _encode_option(delta, qp.encode())

        # Accept (option 17)
        if accept is not None:
            delta = 17 - prev_opt
            prev_opt = 17
            options += _encode_option(delta, struct.pack("B", accept))

        # Block2 (option 23) -- request a specific block of the response
        if block2 is not None:
            delta = BLOCK2_OPTION - prev_opt
            prev_opt = BLOCK2_OPTION
            options += _encode_option(delta, block2)

        msg = header + token + options
        if payload:
            msg += b"\xFF" + payload

        return msg

    def drain_socket(self, timeout: float = 0.1):
        """Discard stale inbound state: the socket AND the pending-request
        buffer.

        Useful after factory reset or test failures to clear stale
        retransmissions from the DUT.

        Clearing `_pending_requests` matters as much as draining the socket.
        Entries there carry no timestamp, so a DUT-originated telegram
        buffered during TestCase N is indistinguishable from a fresh one and
        would be handed to TestCase N+5's UnicastTelegramListener as though
        it had just arrived. Callers invoke this to mean "forget anything
        the device said before now", and a telegram already lifted off the
        socket is exactly that. See finding T8.
        """
        old_timeout = self._sock.gettimeout()
        self._sock.settimeout(timeout)
        drained = 0
        try:
            while True:
                self._sock.recvfrom(_RECV_BUF)
                drained += 1
        except socket.timeout:
            pass
        finally:
            self._sock.settimeout(old_timeout)
        buffered = len(self._pending_requests)
        self._pending_requests.clear()
        if drained or buffered:
            _dbg_coap(f"[coap] drained {drained} stale message(s) and "
                      f"discarded {buffered} buffered request(s)")

    def request(self, method: tuple, path: str,
                payload: bytes = b"",
                accept: Optional[int] = None,
                content_format: Optional[int] = None,
                timeout: Optional[float] = None,
                block2: Optional[bytes] = None) -> Optional[CoapResponse]:
        """
        Send a CoAP request and wait for a response.
        Handles CoAP separate responses (empty ACK followed by CON).
        Returns CoapResponse or None on timeout.

        Sends exactly one request and returns exactly one block. Callers that
        want the whole body should use get()/oscore_get(), which reassemble
        Block2 responses.
        """
        msg = self._build_request(method, path, payload, accept,
                                  content_format, block2=block2)
        # Extract expected token and message ID from the built message
        tkl = msg[0] & 0x0F
        expected_token = msg[4:4 + tkl]
        expected_mid = struct.unpack("!H", msg[2:4])[0]

        old_timeout = self._sock.gettimeout()
        effective_timeout = timeout if timeout is not None else self.timeout
        self._sock.settimeout(effective_timeout)

        try:
            self._sock.sendto(msg, (self.host, self.port, 0, 0))
            return self._recv_response(effective_timeout, expected_token,
                                       expected_mid)
        except socket.timeout:
            return None
        finally:
            self._sock.settimeout(old_timeout)

    def _recv_response(self, timeout: float,
                       expected_token: bytes = None,
                       expected_mid: Optional[int] = None
                       ) -> Optional[CoapResponse]:
        """Receive a CoAP response, handling separate response pattern.

        Skips messages whose token doesn't match expected_token (stale
        retransmissions from DUT).

        Returning None is ambiguous by nature — it means "no response I could
        use", which covers both a genuinely silent device and a device that
        answered in a way we discarded. `self._last_discards` records which
        happened, so the failure message can tell them apart (T10).
        """
        import time
        deadline = time.monotonic() + timeout

        # Reset per receive attempt, not per datagram: the caller reads this
        # after we return None to explain WHY nothing came back.
        self._last_discards = []

        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            self._sock.settimeout(remaining)

            try:
                data, addr = self._sock.recvfrom(_RECV_BUF)
            except socket.timeout:
                return None
            except ConnectionResetError as exc:
                # Windows queues an ICMP port/host-unreachable against the
                # SOCKET and delivers it on the next recv, as WinError
                # 10054 — even though the datagram that provoked it was a
                # different, earlier send. A multicast sweep to a scope
                # with no listener is the usual source, and the error then
                # lands on an unrelated unicast request and aborts it.
                #
                # It says nothing about the request in flight (UDP has no
                # connection to reset), so drain it and keep waiting for
                # the real answer rather than failing a good exchange.
                self._last_discards.append(f"ICMP unreachable ({exc})")
                _dbg_coap(f"[coap] discarding queued ICMP error: {exc}")
                continue

            if len(data) < 4:
                self._last_discards.append(
                    f"runt datagram ({len(data)} bytes, min 4)")
                continue

            byte0 = data[0]
            msg_type = (byte0 >> 4) & 0x03
            tkl = byte0 & 0x0F
            code_byte = data[1]
            code_class = code_byte >> 5
            code_detail = code_byte & 0x1F
            mid = struct.unpack("!H", data[2:4])[0]
            token = data[4:4 + tkl]

            # Empty ACK (code 0.00) = separate response pattern.
            # The actual response will follow as a CON message.
            if msg_type == ACK and code_byte == 0:
                continue

            # Skip messages with wrong token (stale retransmissions).
            #
            # This MUST come before the CON-ACK below. A CON that is not
            # ours is very often a telegram the DUT originated and is
            # waiting on — 5.4.1 asserts on the DUT's RETRANSMISSION of an
            # unACKed telegram (see runner._send_echo_challenge). ACKing it
            # here would satisfy the device, the retransmission would never
            # come, and the test would fail with "no telegram arrived":
            # a false FAIL that only shows up when a response read happens
            # to overlap a DUT-originated telegram, i.e. as flakiness.
            if expected_token is not None and token != expected_token:
                _dbg_coap(f"[coap] skipping stale msg: token={token.hex()}"
                          f" expected={expected_token.hex()}")
                self._last_discards.append(
                    f"token mismatch (got {token.hex() or 'empty'}, "
                    f"expected {expected_token.hex()}, "
                    f"code {code_class}.{code_detail:02d})")
                continue

            # A PIGGYBACKED response (RFC 7252 §5.2.1) rides in the ACK to
            # our request and MUST carry that request's Message ID. A
            # SEPARATE response arrives later as its own CON with a fresh
            # MID, so the check applies to ACKs only — matching a CON here
            # would reject exactly the case the empty-ACK branch above set
            # up. 1324 of the template's 1457 active response rows are
            # piggybacked, so this covers the overwhelming majority.
            if (expected_mid is not None and msg_type == ACK
                    and mid != expected_mid):
                _dbg_coap(f"[coap] skipping ACK with wrong mid={mid} "
                          f"expected={expected_mid}")
                self._last_discards.append(
                    f"ACK MID mismatch (got {mid}, expected {expected_mid}, "
                    f"code {code_class}.{code_detail:02d})")
                continue

            # Our own CON response: ACK it, to the address it actually came
            # FROM. The DUT answers from an ephemeral source port, so
            # replying to (self.host, self.port) can miss it entirely — the
            # device would then retransmit, and those copies land on the
            # shared socket during a LATER exchange.
            if msg_type == CON:
                ack = struct.pack("!BBH", (1 << 6) | (ACK << 4), 0, mid)
                try:
                    self._sock.sendto(ack, addr)
                except OSError as exc:
                    # Windows rejects some source addresses as DESTINATIONS
                    # (WinError 10049): a datagram that arrived via a
                    # multicast group, or one whose scope id names no route
                    # out. Failing to ACK only makes the peer retransmit —
                    # dropping the run over it turns a cosmetic problem
                    # into a dead sequence.
                    _dbg_coap(f"[coap] could not ACK CON mid={mid} to "
                              f"{addr}: {exc}")

            offset = 4 + tkl
            try:
                options, payload_offset = _parse_options(data, offset)
            except CoapFormatError as exc:
                # Not well-formed CoAP — skip it and keep waiting rather
                # than letting one bad datagram abort the whole run.
                _dbg_coap(f"[coap] discarding malformed datagram: {exc}")
                self._last_discards.append(f"malformed options ({exc})")
                continue

            payload_data = (data[payload_offset:]
                            if payload_offset < len(data) else b"")

            # Debug: show received response
            _dbg_coap(f"[coap] recv: type={msg_type} code={code_class}.{code_detail:02d}"
                      f" mid={mid} tkl={tkl}"
                      f" token={token.hex() if token else 'none'}"
                      f" opts={sorted(options.keys())}"
                      f" payload_len={len(payload_data)}")

            # Extract Content-Format from option 12
            ct = None
            if 12 in options:
                ct_bytes = options[12]
                ct = int.from_bytes(ct_bytes, "big") if ct_bytes else 0

            return CoapResponse(
                code_class=code_class,
                code_detail=code_detail,
                payload=payload_data,
                content_format=ct,
                msg_type=msg_type,
                mid=mid,
                token=token,
                options=options,
            )

    def get(self, path: str, accept: Optional[int] = None,
            timeout: Optional[float] = None) -> Optional[CoapResponse]:
        """GET a resource, reassembling a Block2 response into one body.

        RFC 7959: when the body does not fit one datagram the DUT answers
        with Block2 (option 23) and the "more" bit set. Each further block
        is fetched by echoing Block2 with the next block number and the
        server's own SZX. The returned CoapResponse carries the complete
        concatenated payload, and its Block2 option is rewritten so that
        the "more" bit is clear, i.e. it describes the reassembled body.
        """
        resp = self.request(GET, path, accept=accept, timeout=timeout)
        return self._reassemble_block2(
            resp, lambda blk: self.request(GET, path, accept=accept,
                                           timeout=timeout, block2=blk))

    def _reassemble_block2(self, resp, fetch_block):
        """Follow Block2 until the "more" bit clears, concatenating payloads.

        fetch_block(block2_option_bytes) sends one follow-up request and
        returns its CoapResponse. Shared by the plain and OSCORE paths.
        """
        if resp is None or not resp.options or BLOCK2_OPTION not in resp.options:
            return resp

        num, more, size = _decode_block_option(resp.options[BLOCK2_OPTION])
        if not more:
            return resp

        body = bytearray(resp.payload)
        expected = resp.options.get(SIZE2_OPTION)
        total = int.from_bytes(expected, "big") if expected else None
        max_blocks = 64  # safety limit against a server that never clears "more"

        while more and num < max_blocks:
            num = num + 1
            nxt = fetch_block(_encode_block_option(num, False, size))
            if nxt is None:
                _dbg_coap(f"[coap] block2: timeout fetching block {num}")
                return None
            if not nxt.is_successful:
                _dbg_coap(f"[coap] block2: block {num} failed {nxt.code}")
                return nxt
            body += nxt.payload
            if nxt.options and BLOCK2_OPTION in nxt.options:
                num, more, size = _decode_block_option(
                    nxt.options[BLOCK2_OPTION])
            else:
                more = False
            resp = nxt

        _dbg_coap(f"[coap] block2: reassembled {len(body)} bytes"
                  f" in {num + 1} block(s) size2={total}")

        options = dict(resp.options or {})
        options[BLOCK2_OPTION] = _encode_block_option(num, False, size)
        return replace(resp, payload=bytes(body), options=options)

    def put(self, path: str, payload: bytes = b"",
            content_format: int = APPLICATION_CBOR,
            timeout: Optional[float] = None) -> Optional[CoapResponse]:
        return self.request(PUT, path, payload=payload,
                           content_format=content_format, timeout=timeout)

    def post(self, path: str, payload: bytes = b"",
             content_format: int = APPLICATION_CBOR,
             timeout: Optional[float] = None) -> Optional[CoapResponse]:
        return self.request(POST, path, payload=payload,
                           content_format=content_format, timeout=timeout)

    def delete(self, path: str,
               timeout: Optional[float] = None) -> Optional[CoapResponse]:
        return self.request(DELETE, path, timeout=timeout)

    # ------------------------------------------------------------------
    # OSCORE-protected requests
    # ------------------------------------------------------------------

    def _build_oscore_request(self, oscore_option: bytes,
                              ciphertext: bytes,
                              observe: Optional[int] = None,
                              token: Optional[bytes] = None,
                              msg_type: int = CON,
                              crit_option: Optional[tuple] = None) -> bytes:
        """Build a CoAP POST request carrying an OSCORE option.

        OSCORE outer message:
        - Type = msg_type (CON by default; EITT's `Type` attribute picks
          CON vs NON per telegram and unicast group writes use NON)
        - Code = POST (0.02)
        - Option 6 (Observe, outer/Class U) = observe, if requested
        - Option 9 (OSCORE) = oscore_option
        - `crit_option` = (number, value_bytes) for an extra OUTER option,
          used by EITT's CritOptNo/CritOptValue to send an unknown
          CRITICAL option the device must reject with 4.02 Bad Option
          (RFC 7252 §5.4.1). It has to be outer/unencrypted: the point is
          that the receiving CoAP layer sees and rejects it.
        - Payload = ciphertext (the encrypted inner message)

        The inner (encrypted) message also carries the Observe option; the
        outer copy is required so the receiving CoAP/observe layer treats
        the message as an observe (de)registration (RFC 8613 §4.1.3.5).
        """
        mid = self._next_mid()
        if token is None:
            token = self._next_token()
        tkl = len(token)

        ver_type_tkl = (1 << 6) | (msg_type << 4) | tkl
        code = (POST[0] << 5) | POST[1]  # 0.02
        header = struct.pack("!BBH", ver_type_tkl, code, mid)

        # Outer options, ascending by number: Observe (6), OSCORE (9), then
        # any critical option (its number is far higher than 9 in practice —
        # 65123 in 5.10.5.3 — but sort explicitly rather than assume).
        options = b""
        prev_opt = 0
        if observe is not None:
            obs_bytes = (b"" if observe == 0
                         else observe.to_bytes(
                             (observe.bit_length() + 7) // 8 or 1, "big"))
            options += _encode_option(6 - prev_opt, obs_bytes)
            prev_opt = 6
        outer = [(9, oscore_option)]
        if crit_option is not None:
            outer.append(crit_option)
        for num, val in sorted(outer):
            options += _encode_option(num - prev_opt, val)
            prev_opt = num

        msg = header + token + options + b"\xFF" + ciphertext
        return msg

    def oscore_request(self, oscore_ctx, method_code: int, path: str,
                       payload: bytes = b"",
                       uri_queries: list = None,
                       accept: Optional[int] = None,
                       content_format: Optional[int] = None,
                       timeout: Optional[float] = None,
                       block2: Optional[bytes] = None,
                       seq_no_override: Optional[int] = None,
                       seq_no_offset: Optional[int] = None,
                       invalid_auth_tag: Optional[bytes] = None,
                       kid_override: Optional[bytes] = None,
                       echo: Optional[bytes] = None,
                       expect_echo_challenge: bool = False,
                       msg_type: int = CON,
                       crit_option: Optional[tuple] = None) -> Optional[
                           CoapResponse]:
        """Send an OSCORE-protected CoAP request.

        Automatically handles Echo challenges (4.01 with Echo option)
        by retrying with the Echo value.

        Args:
            oscore_ctx: OscoreContext instance
            method_code: Inner CoAP code (1=GET, 2=POST, 3=PUT, 4=DELETE)
            path: URI path
            payload: Application payload (will be encrypted)
            uri_queries: List of query strings
            timeout: Socket timeout override
            seq_no_override / invalid_auth_tag / kid_override: adversarial
                replay/integrity/kid overrides forwarded to
                OscoreContext.protect_request (EITT SeqNoOffset,
                SeqNoFn="fix"+SeqNo, InvalidAuthTag, RndKid). When any is
                set the automatic Echo-retry loop is DISABLED — these are
                deliberately-malformed one-shot requests whose whole point
                is the device's rejection/challenge, so retrying (which
                would resync the SSN or resend) would defeat the test.

        Returns:
            CoapResponse with decrypted payload, or None on timeout.
            The code_class/code_detail reflect the INNER response code.
        """
        # A caller-supplied echo (EITT Echo="sync" follow-up) seeds the
        # first attempt directly — the device already sent us its
        # challenge in a PRIOR telegram's response and the XML models the
        # echo-back as its own separate message, so we don't wait for an
        # in-loop 4.01 to discover it.
        echo_value = echo
        adversarial = (seq_no_override is not None
                       or seq_no_offset is not None
                       or invalid_auth_tag is not None
                       or kid_override is not None
                       # The caller's script models the 4.01+Echo
                       # challenge as its own step and replays the
                       # telegram itself, so auto-satisfying the
                       # challenge here would skip the very behaviour
                       # under test — EITT 5.3.17.5 replays with a
                       # deliberately WRONG echo and asserts the device
                       # rejects it, which silently "passes" if this loop
                       # already completed the handshake correctly.
                       or expect_echo_challenge)
        max_retries = 1 if adversarial else 3

        for attempt in range(max_retries):
            # Split query string from path for inner OSCORE options
            if "?" in path:
                inner_path, query_part = path.split("?", 1)
                all_queries = (uri_queries or []) + query_part.split("&")
            else:
                inner_path = path
                all_queries = uri_queries

            oscore_opt, ciphertext, piv = oscore_ctx.protect_request(
                method_code, inner_path, payload, all_queries,
                echo=echo_value,
                accept=accept, content_format=content_format,
                block2=block2,
                seq_no_override=seq_no_override,
                seq_no_offset=seq_no_offset,
                invalid_auth_tag=invalid_auth_tag,
                kid_override=kid_override)

            msg = self._build_oscore_request(oscore_opt, ciphertext,
                                             msg_type=msg_type,
                                             crit_option=crit_option)
            # Extract expected token from built message
            tkl = msg[0] & 0x0F
            expected_token = msg[4:4 + tkl]

            old_timeout = self._sock.gettimeout()
            effective_timeout = timeout if timeout is not None else self.timeout
            self._sock.settimeout(effective_timeout)

            try:
                self._sock.sendto(msg, (self.host, self.port, 0, 0))
                result = self._recv_oscore_response(
                    oscore_ctx, piv, effective_timeout, expected_token)
            except socket.timeout:
                return None
            finally:
                self._sock.settimeout(old_timeout)

            if result is None:
                return None

            resp, inner_options = result

            # Surface any Echo option (252) on the response so the runner
            # can capture it for a later Echo="sync" follow-up telegram
            # (the group-message replay-sync flow, EITT 5.3.17.3/.4/.5,
            # models the echo-back as a SEPARATE message rather than an
            # in-loop retry).
            if 252 in inner_options:
                resp.echo_value = inner_options[252]

            # Check for Echo challenge (option 252) — both inner
            # (OSCORE-protected) and outer (unprotected 4.01). For
            # adversarial one-shot sends the retry loop is disabled
            # (max_retries=1), so a 4.01+Echo is returned to the caller
            # (with echo_value set above) instead of auto-retried.
            if (not adversarial and resp.code_class == 4
                    and resp.code_detail == 1 and 252 in inner_options):
                echo_value = inner_options[252]
                _dbg_oscore(f"[oscore] Echo challenge received, retrying "
                            f"(attempt {attempt + 1})")
                continue

            return resp

        return resp  # Return last response even if still 4.01

    def _recv_oscore_response(self, oscore_ctx, piv: bytes,
                              timeout: float,
                              expected_token: bytes = None) -> Optional[
                                  tuple[CoapResponse, dict]]:
        """Receive and decrypt an OSCORE response.

        Skips messages whose token doesn't match expected_token (stale
        retransmissions from DUT).

        Returns (CoapResponse, inner_options_dict) or None on timeout.
        """
        import time
        deadline = time.monotonic() + timeout

        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            self._sock.settimeout(remaining)

            try:
                data, addr = self._sock.recvfrom(_RECV_BUF)
            except socket.timeout:
                return None
            except ConnectionResetError as exc:
                # Windows queues an ICMP port/host-unreachable against the
                # SOCKET and delivers it on the next recv, as WinError
                # 10054 — even though the datagram that provoked it was a
                # different, earlier send. A multicast sweep to a scope
                # with no listener is the usual source, and the error then
                # lands on an unrelated unicast request and aborts it.
                #
                # It says nothing about the request in flight (UDP has no
                # connection to reset), so drain it and keep waiting for
                # the real answer rather than failing a good exchange.
                self._last_discards.append(f"ICMP unreachable ({exc})")
                _dbg_coap(f"[coap] discarding queued ICMP error: {exc}")
                continue

            if len(data) < 4:
                continue

            byte0 = data[0]
            msg_type = (byte0 >> 4) & 0x03
            code_byte = data[1]
            mid = struct.unpack("!H", data[2:4])[0]
            tkl = byte0 & 0x0F
            token = data[4:4 + tkl]

            # Skip empty ACKs (separate response pattern)
            if msg_type == ACK and code_byte == 0:
                continue

            # NOTE: the CON-ACK deliberately happens AFTER the token check
            # below, not here. A CON carrying someone else's token is very
            # often a telegram the DUT originated and is waiting on — 5.4.1
            # asserts on the DUT's RETRANSMISSION of an unACKed telegram.
            # ACKing it at this point would satisfy the device, the
            # retransmission would never arrive, and the test would fail
            # with "no telegram arrived".

            # Skip messages with wrong token (stale DUT retransmissions) —
            # but FIRST keep it if it is an unsolicited REQUEST the DUT sent
            # us rather than a stale reply.
            #
            # This is where 5.4.1.4's telegram used to vanish. The DUT answers
            # our group read (`st: r`) with a unicast `st: a` telegram from
            # inside its own request handler: traces/5_4_1.xml shows it 0.1s
            # after our ACK (TelNo 98 at 22:25:20.6 -> TelNo 101 at
            # 22:25:20.7) with a DIFFERENT mid and token (9456/`39 33 30 34`
            # vs 25636/`27 99 1A 0D`), so it arrives while this loop is still
            # reading and gets discarded as "stale". EITT never has this
            # problem because its socket is continuously open and the telegram
            # is simply buffered until the step that wants it looks — which is
            # why its step 54 can get away with a plain 2s budget. Buffering it
            # here gives us the same property.
            if expected_token is not None and token != expected_token:
                if (code_byte >> 5) == 0 and code_byte != 0:
                    self._pending_requests.append(
                        (self.parse_coap_message(data), addr))
                    _dbg_coap(f"[coap] buffered an unsolicited request "
                              f"(code={code_byte:#04x} mid={mid} "
                              f"token={token.hex()}) from {addr[0]}")
                    continue
                _dbg_coap(f"[coap] skipping stale oscore msg: token={token.hex()}"
                          f" expected={expected_token.hex()}")
                continue

            # Our own CON response: ACK it, to the address it actually came
            # FROM (the DUT answers from an ephemeral source port, so
            # replying to (self.host, self.port) can miss it entirely).
            if msg_type == CON:
                ack = struct.pack("!BBH", (1 << 6) | (ACK << 4), 0, mid)
                try:
                    self._sock.sendto(ack, addr)
                except OSError as exc:
                    # Windows rejects some source addresses as DESTINATIONS
                    # (WinError 10049): a datagram that arrived via a
                    # multicast group, or one whose scope id names no route
                    # out. Failing to ACK only makes the peer retransmit —
                    # dropping the run over it turns a cosmetic problem
                    # into a dead sequence.
                    _dbg_coap(f"[coap] could not ACK CON mid={mid} to "
                              f"{addr}: {exc}")

            offset = 4 + tkl
            try:
                options, payload_offset = _parse_options(data, offset)
            except CoapFormatError as exc:
                _dbg_coap(f"[coap] discarding malformed datagram: {exc}")
                continue

            outer_payload = data[payload_offset:] if payload_offset < len(
                data) else b""

            _dbg_coap(f"[coap] recv msg: type={msg_type} code={code_byte:#04x}"
                      f" mid={mid} tkl={tkl}"
                      f" token={token.hex() if token else 'none'}"
                      f" opts={sorted(options.keys())}"
                      f" payload_len={len(outer_payload)}")
            _dbg_coap(f"[coap] raw ({len(data)}B): {data.hex()}")

            # Check if response has OSCORE option (9)
            if 9 in options:
                # Decrypt the response
                resp_oscore_opt = options[9]
                inner_code, inner_payload, inner_options = (
                    oscore_ctx.unprotect_response(
                        resp_oscore_opt, outer_payload, piv))
                # Surface the OUTER (Class U) Observe option, which carries
                # the sequence number for observe registrations and
                # notifications per RFC 8613 4.1.3.5.2.  The inner Observe
                # is present but empty.
                if 6 in options:
                    inner_options[6] = options[6]
                return (CoapResponse(
                    code_class=inner_code >> 5,
                    code_detail=inner_code & 0x1F,
                    payload=inner_payload,
                    content_format=None,
                    msg_type=msg_type,
                    mid=mid,
                    token=token,
                    options=inner_options,
                ), inner_options)
            else:
                # Unprotected response (e.g. 4.01 Unauthorized)
                code_class = code_byte >> 5
                code_detail = code_byte & 0x1F
                ct = None
                if 12 in options:
                    ct_bytes = options[12]
                    ct = (int.from_bytes(ct_bytes, "big")
                          if ct_bytes else 0)
                # Pass outer options so Echo challenges are visible
                outer_opts = {}
                if 252 in options:
                    outer_opts[252] = options[252]
                return (CoapResponse(
                    code_class=code_class,
                    code_detail=code_detail,
                    payload=outer_payload,
                    content_format=ct,
                    msg_type=msg_type,
                    mid=mid,
                    token=token,
                ), outer_opts)

    def oscore_get(self, oscore_ctx, path: str,
                   accept: Optional[int] = None,
                   timeout: Optional[float] = None) -> Optional[CoapResponse]:
        """OSCORE GET, reassembling a Block2 response into one body.

        Block2 is a Class E option, so both the request option and the
        response option are carried inside the encrypted inner message.
        """
        resp = self.oscore_request(oscore_ctx, 1, path, accept=accept,
                                   timeout=timeout)
        return self._reassemble_block2(
            resp, lambda blk: self.oscore_request(
                oscore_ctx, 1, path, accept=accept, timeout=timeout,
                block2=blk))

    def complete_oscore_block2(self, resp, oscore_ctx, method_code: int,
                               path: str, payload: bytes = b"",
                               accept: Optional[int] = None,
                               content_format: Optional[int] = None,
                               timeout: Optional[float] = None,
                               msg_type: int = CON):
        """Finish a Block2 body for a response `oscore_request` already got.

        For callers that must drive `oscore_request` themselves (the EITT
        runner, which forwards the XML's adversarial knobs) and so cannot
        use `oscore_get`. Reuses `_reassemble_block2` — this is only the
        entry condition plus the follow-up request shape.

        Sends NOTHING unless the response actually carries Block2 with the
        "more" bit set. That matters beyond efficiency: every follow-up
        burns an OSCORE sender sequence number, and the 5.3.17.x
        replay-window cases assert on exact SSN arithmetic, so a body that
        fits one block must still cost exactly one message. The caller is
        responsible for not calling this after an adversarial send
        (seq_no_override / seq_no_offset / invalid_auth_tag / kid_override
        / expect_echo_challenge) — those are one-shot by design.

        The follow-up repeats the SAME method, path (queries included, they
        are part of `path` here), accept, content-format and security
        context, per RFC 7959 2.4.
        """
        if resp is None or not resp.options or BLOCK2_OPTION not in resp.options:
            return resp
        _num, more, _size = _decode_block_option(resp.options[BLOCK2_OPTION])
        if not more:
            return resp
        return self._reassemble_block2(
            resp, lambda blk: self.oscore_request(
                oscore_ctx, method_code, path, payload=payload,
                accept=accept, content_format=content_format,
                timeout=timeout, msg_type=msg_type, block2=blk))

    def oscore_put(self, oscore_ctx, path: str, payload: bytes = b"",
                   content_format: int = APPLICATION_CBOR,
                   timeout: Optional[float] = None) -> Optional[CoapResponse]:
        return self.oscore_request(oscore_ctx, 3, path, payload=payload,
                                  content_format=content_format,
                                  timeout=timeout)

    def oscore_post(self, oscore_ctx, path: str, payload: bytes = b"",
                    content_format: int = APPLICATION_CBOR,
                    timeout: Optional[float] = None) -> Optional[CoapResponse]:
        return self.oscore_request(oscore_ctx, 2, path, payload=payload,
                                  content_format=content_format,
                                  timeout=timeout)

    def oscore_delete(self, oscore_ctx, path: str,
                      timeout: Optional[float] = None) -> Optional[
                          CoapResponse]:
        return self.oscore_request(oscore_ctx, 4, path, timeout=timeout)

    @staticmethod
    def _build_observe_query(path: str, lt: Optional[int],
                             non: Optional[bool]) -> tuple[str, list[str]]:
        """Return (inner_path, query_list) with lt/non appended.

        `lt=None` omits the lifetime entirely (to exercise the missing-lt
        rejection); `lt=0` is sent literally.  `non=True` adds `non=true`.
        """
        if "?" in path:
            inner_path, qpart = path.split("?", 1)
            queries = [q for q in qpart.split("&") if q]
        else:
            inner_path, queries = path, []
        if lt is not None:
            queries.append(f"lt={lt}")
        if non:
            queries.append("non=true")
        return inner_path, queries

    def observe_register(self, path: str, lt: Optional[int] = 86400,
                         non: Optional[bool] = None,
                         accept: Optional[int] = None,
                         timeout: Optional[float] = None) -> tuple[
                             Optional[CoapResponse], "ObserveSubscription"]:
        """Register a plain (unprotected) CoAP observe on `path`.

        Returns (first_response, subscription).  The first response is the
        initial notification (2.05 with the Observe option) on success, or
        an error response (e.g. 4.00) when the registration is rejected.
        """
        inner_path, queries = self._build_observe_query(path, lt, non)
        full = inner_path + ("?" + "&".join(queries) if queries else "")
        msg = self._build_request(GET, full, observe=0)
        tkl = msg[0] & 0x0F
        token = msg[4:4 + tkl]

        eff = timeout if timeout is not None else self.timeout
        old = self._sock.gettimeout()
        self._sock.settimeout(eff)
        try:
            self._sock.sendto(msg, (self.host, self.port, 0, 0))
            resp = self._recv_response(eff, token)
        finally:
            self._sock.settimeout(old)

        sub = ObserveSubscription(path=inner_path, token=token)
        if resp is not None and resp.options and 6 in resp.options:
            sub.last_seq = (int.from_bytes(resp.options[6], "big")
                            if resp.options[6] else 0)
        return resp, sub

    def observe_register_oscore(self, oscore_ctx, path: str,
                                lt: Optional[int] = 86400,
                                non: Optional[bool] = None,
                                accept: Optional[int] = None,
                                content_format: Optional[int] = None,
                                timeout: Optional[float] = None) -> tuple[
                                    Optional[CoapResponse],
                                    "ObserveSubscription"]:
        """Register an OSCORE-protected CoAP observe on `path`.

        The Observe option is carried both inside the encrypted inner
        message and as an outer Class-U option.  The registration request's
        partial IV is stored on the subscription so later notifications can
        be decrypted (their AAD binds the request PIV, RFC 8613 §8.3).
        """
        inner_path, queries = self._build_observe_query(path, lt, non)
        oscore_opt, ciphertext, piv = oscore_ctx.protect_request(
            1, inner_path, b"", queries or None, observe=0,
            accept=accept, content_format=content_format)
        token = self._next_token()
        msg = self._build_oscore_request(oscore_opt, ciphertext,
                                         observe=0, token=token)

        eff = timeout if timeout is not None else self.timeout
        old = self._sock.gettimeout()
        self._sock.settimeout(eff)
        try:
            self._sock.sendto(msg, (self.host, self.port, 0, 0))
            result = self._recv_oscore_response(oscore_ctx, piv, eff, token)
        finally:
            self._sock.settimeout(old)

        sub = ObserveSubscription(path=inner_path, token=token,
                                  oscore_ctx=oscore_ctx, request_piv=piv)
        if result is not None:
            resp, inner_options = result
            if 6 in inner_options:
                sub.last_seq = (int.from_bytes(inner_options[6], "big")
                                if inner_options[6] else 0)
            return resp, sub
        return None, sub

    def collect_notifications(self, sub: "ObserveSubscription",
                              count: int = 1, timeout: float = 5.0,
                              ack: bool = True) -> list["Notification"]:
        """Collect up to `count` notifications for `sub` within `timeout`.

        Confirmable (CON) notifications are ACKed (unless `ack=False`, used
        to test stale-observer pruning).  OSCORE subscriptions are decrypted
        with the subscription's stored context and request PIV.  Messages
        whose token does not match the subscription are ignored.
        """
        import time
        deadline = time.monotonic() + timeout
        out: list[Notification] = []
        old = self._sock.gettimeout()
        try:
            while len(out) < count:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    break
                self._sock.settimeout(remaining)
                try:
                    data, _ = self._sock.recvfrom(_RECV_BUF)
                except socket.timeout:
                    break
                if len(data) < 4:
                    continue

                byte0 = data[0]
                msg_type = (byte0 >> 4) & 0x03
                tkl = byte0 & 0x0F
                code_byte = data[1]
                mid = struct.unpack("!H", data[2:4])[0]
                token = data[4:4 + tkl]

                if msg_type == ACK and code_byte == 0:
                    continue
                if msg_type == CON and ack:
                    ackmsg = struct.pack("!BBH", (1 << 6) | (ACK << 4), 0, mid)
                    self._sock.sendto(ackmsg, (self.host, self.port, 0, 0))
                if token != sub.token:
                    print(f"[observe] ignoring msg token={token.hex()}"
                          f" (want {sub.token.hex()})")
                    continue

                offset = 4 + tkl
                try:
                    options, payload_offset = _parse_options(data, offset)
                except CoapFormatError as exc:
                    _dbg_coap(f"[coap] discarding malformed datagram: {exc}")
                    continue
                outer_payload = (data[payload_offset:]
                                 if payload_offset < len(data) else b"")

                if sub.is_oscore:
                    if 9 not in options:
                        print("[observe] OSCORE sub got unprotected msg, skip")
                        continue
                    inner_code, inner_payload, inner_options = (
                        sub.oscore_ctx.unprotect_response(
                            options[9], outer_payload, sub.request_piv))
                    # The notification sequence number is carried in the
                    # OUTER (Class U) Observe option per RFC 8613
                    # 4.1.3.5.2; the inner Observe is present but empty
                    # (it only signals that this is a notification).
                    seq = self._observe_seq(options)
                    if seq is None:
                        seq = self._observe_seq(inner_options)
                    resp = CoapResponse(
                        code_class=inner_code >> 5,
                        code_detail=inner_code & 0x1F,
                        payload=inner_payload, content_format=None,
                        msg_type=msg_type, mid=mid, token=token,
                        options=inner_options)
                else:
                    ct = None
                    if 12 in options:
                        ct = (int.from_bytes(options[12], "big")
                              if options[12] else 0)
                    seq = self._observe_seq(options)
                    resp = CoapResponse(
                        code_class=code_byte >> 5,
                        code_detail=code_byte & 0x1F,
                        payload=outer_payload, content_format=ct,
                        msg_type=msg_type, mid=mid, token=token,
                        options=options)

                if seq is not None:
                    sub.last_seq = seq
                out.append(Notification(response=resp, seq=seq,
                                        msg_type=msg_type, mid=mid))
        finally:
            self._sock.settimeout(old)
        return out

    @staticmethod
    def _observe_seq(options: dict) -> Optional[int]:
        """Extract the Observe option (6) sequence value, or None."""
        if 6 not in options:
            return None
        return int.from_bytes(options[6], "big") if options[6] else 0

    def count_incoming_messages(self, timeout: float = 2.0,
                                ack: bool = True) -> int:
        """Count non-empty, non-ACK CoAP messages arriving within `timeout`.

        Each observe notification is one message regardless of its token or
        OSCORE protection, so this is used to detect duplicate notifications
        (e.g. a stale subscription that should have been replaced).
        Confirmable messages are ACKed so the server does not retransmit.
        """
        import time
        deadline = time.monotonic() + timeout
        count = 0
        old = self._sock.gettimeout()
        try:
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    break
                self._sock.settimeout(remaining)
                try:
                    data, _ = self._sock.recvfrom(4096)
                except socket.timeout:
                    break
                if len(data) < 4:
                    continue
                msg_type = (data[0] >> 4) & 0x03
                code_byte = data[1]
                mid = struct.unpack("!H", data[2:4])[0]
                if msg_type == ACK and code_byte == 0:
                    continue
                if msg_type == CON and ack:
                    ackmsg = struct.pack("!BBH", (1 << 6) | (ACK << 4), 0, mid)
                    self._sock.sendto(ackmsg, (self.host, self.port, 0, 0))
                count += 1
        finally:
            self._sock.settimeout(old)
        return count

    def observe_deregister(self, sub: "ObserveSubscription",
                           timeout: Optional[float] = None) -> Optional[
                               CoapResponse]:
        """Cancel a subscription via a GET with Observe=1 and the same token."""
        eff = timeout if timeout is not None else self.timeout
        old = self._sock.gettimeout()
        self._sock.settimeout(eff)
        try:
            if sub.is_oscore:
                oscore_opt, ciphertext, piv = sub.oscore_ctx.protect_request(
                    1, sub.path, b"", None, observe=1)
                msg = self._build_oscore_request(oscore_opt, ciphertext,
                                                 observe=1, token=sub.token)
                self._sock.sendto(msg, (self.host, self.port, 0, 0))
                result = self._recv_oscore_response(
                    sub.oscore_ctx, piv, eff, sub.token)
                return result[0] if result else None
            msg = self._build_request(GET, sub.path, observe=1,
                                      token=sub.token)
            self._sock.sendto(msg, (self.host, self.port, 0, 0))
            return self._recv_response(eff, sub.token)
        finally:
            self._sock.settimeout(old)

    # ------------------------------------------------------------------
    # Multicast requests
    # ------------------------------------------------------------------

    def _build_non_request(self, method: tuple, uri_path: str,
                           payload: bytes = b"",
                           accept: Optional[int] = None,
                           content_format: Optional[int] = None) -> bytes:
        """Build a CoAP NON (non-confirmable) request message."""
        mid = self._next_mid()
        token = self._next_token()
        tkl = len(token)

        if "?" in uri_path:
            path_part, query_part = uri_path.split("?", 1)
        else:
            path_part, query_part = uri_path, None

        ver_type_tkl = (1 << 6) | (NON << 4) | tkl
        code = (method[0] << 5) | method[1]
        header = struct.pack("!BBH", ver_type_tkl, code, mid)

        options = b""
        prev_opt = 0

        for part in path_part.strip("/").split("/"):
            if not part:
                continue
            delta = 11 - prev_opt
            prev_opt = 11
            options += _encode_option(delta, part.encode())

        if content_format is not None:
            delta = 12 - prev_opt
            prev_opt = 12
            options += _encode_option(delta,
                                      struct.pack("B", content_format))

        if query_part:
            for qp in query_part.split("&"):
                delta = 15 - prev_opt
                prev_opt = 15
                options += _encode_option(delta, qp.encode())

        if accept is not None:
            delta = 17 - prev_opt
            prev_opt = 17
            options += _encode_option(delta, struct.pack("B", accept))

        msg = header + token + options
        if payload:
            msg += b"\xFF" + payload

        return msg

    def send_multicast(self, method: tuple, path: str,
                       payload: bytes = b"",
                       accept: Optional[int] = None,
                       content_format: Optional[int] = None,
                       scope: int = _DEFAULT_MC_SCOPE,
                       interface: Optional[str] = None,
                       collect_timeout: float = 2.0,
                       port: int = 5683,
                       stop_after_first: bool = False,
                       ) -> list[CoapResponse]:
        """Send a NON CoAP request to a KNX multicast address and collect
        responses within collect_timeout seconds.

        Args:
            method: CoAP method (GET, POST, etc.)
            path: URI path (e.g. "/.well-known/core")
            payload: Request payload
            accept: Accept content format
            content_format: Content-Format option
            scope: IPv6 multicast scope (2=link-local, 3=realm, 5=site)
            interface: Network interface name for scope_id lookup (e.g. "veth-test").
                       If None, uses the DEVICE_IFACE env var, or 0 (OS default).
            collect_timeout: How long to listen for responses (seconds)
            port: Destination port for multicast (default 5683, the CoAP standard port)
            stop_after_first: Return as soon as the first response is received
                       instead of waiting out the full collect_timeout. Only
                       use this when the test asserts on the first/any response
                       (e.g. `len(responses) >= 1`) and is not verifying an
                       exact/multiple-responder count or the absence of
                       further responses -- callers checking for "no response"
                       must still pass the full collect_timeout with this off.

        Returns:
            List of CoapResponse from all responders (a single-element list
            when stop_after_first is set and a response arrives).
        """
        mcast_addr = KNX_MULTICAST_ADDRESSES.get(scope)
        if not mcast_addr:
            raise ValueError(f"Unknown multicast scope {scope}")

        # Resolve interface name to scope_id
        scope_id = 0
        iface = interface or os.environ.get("DEVICE_IFACE")
        if iface:
            try:
                scope_id = int(iface)
            except ValueError:
                try:
                    scope_id = socket.if_nametoindex(iface)
                except (OSError, AttributeError):
                    _dbg_coap(f"[coap] Warning: could not resolve interface '{iface}', "
                              f"using scope_id=0")
        # Allow direct scope_id override for interface iteration during
        # discovery (avoids needing a name for each interface)
        if not scope_id:
            override = os.environ.get("_DISCOVERY_SCOPE_ID")
            if override:
                scope_id = int(override)

        msg = self._build_non_request(method, path, payload, accept,
                                      content_format)

        # Create a separate socket for multicast so we don't interfere
        # with the main unicast socket's recv buffer
        mcast_sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
        mcast_sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_MULTICAST_HOPS,
                              255 if scope > 2 else 1)
        if scope_id:
            try:
                mcast_sock.setsockopt(
                    socket.IPPROTO_IPV6, socket.IPV6_MULTICAST_IF,
                    struct.pack("@I", scope_id))
            except OSError:
                scope_id = 0
        mcast_sock.settimeout(collect_timeout)
        mcast_sock.bind(("", 0))

        _dbg_coap(f"[coap] multicast {method} -> [{mcast_addr}%{scope_id}]"
                  f":{port}{path}")

        dest = (mcast_addr, port, 0, scope_id)
        mcast_sock.sendto(msg, dest)

        responses = []
        import time
        deadline = time.monotonic() + collect_timeout

        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            mcast_sock.settimeout(remaining)

            try:
                data, addr = mcast_sock.recvfrom(_RECV_BUF)
            except socket.timeout:
                break

            if len(data) < 4:
                continue

            byte0 = data[0]
            msg_type = (byte0 >> 4) & 0x03
            tkl = byte0 & 0x0F
            code_byte = data[1]
            code_class = code_byte >> 5
            code_detail = code_byte & 0x1F
            mid = struct.unpack("!H", data[2:4])[0]
            token = data[4:4 + tkl]

            offset = 4 + tkl
            try:
                options, payload_offset = _parse_options(data, offset)
            except CoapFormatError as exc:
                _dbg_coap(f"[coap] discarding malformed datagram: {exc}")
                continue

            payload_data = (data[payload_offset:]
                            if payload_offset < len(data) else b"")

            ct = None
            if 12 in options:
                ct_bytes = options[12]
                ct = int.from_bytes(ct_bytes, "big") if ct_bytes else 0

            resp = CoapResponse(
                code_class=code_class,
                code_detail=code_detail,
                payload=payload_data,
                content_format=ct,
                msg_type=msg_type,
                mid=mid,
                token=token,
                source_addr=addr,
                options=options,
            )
            _dbg_coap(f"[coap] multicast response from {addr[0]}: "
                      f"{resp.code} payload_len={len(payload_data)}")
            responses.append(resp)

            if stop_after_first:
                break

        mcast_sock.close()
        _dbg_coap(f"[coap] multicast: collected {len(responses)} response(s)")
        return responses

    def multicast_get(self, path: str, scope: int = _DEFAULT_MC_SCOPE,
                      accept: Optional[int] = None,
                      interface: Optional[str] = None,
                      collect_timeout: float = 2.0,
                      stop_after_first: bool = False,
                      ) -> list[CoapResponse]:
        """Send a multicast GET and collect responses."""
        return self.send_multicast(GET, path, accept=accept, scope=scope,
                                  interface=interface,
                                  collect_timeout=collect_timeout,
                                  stop_after_first=stop_after_first)

    def multicast_post(self, path: str, payload: bytes = b"",
                       content_format: int = APPLICATION_CBOR,
                       scope: int = _DEFAULT_MC_SCOPE,
                       interface: Optional[str] = None,
                       collect_timeout: float = 2.0,
                       stop_after_first: bool = False,
                       ) -> list[CoapResponse]:
        """Send a multicast POST and collect responses."""
        return self.send_multicast(POST, path, payload=payload,
                                  content_format=content_format, scope=scope,
                                  interface=interface,
                                  collect_timeout=collect_timeout,
                                  stop_after_first=stop_after_first)

    def oscore_multicast_post(self, oscore_ctx, path: str,
                              payload: bytes = b"",
                              content_format: int = APPLICATION_CBOR,
                              scope: int = _DEFAULT_MC_SCOPE,
                              interface: Optional[str] = None,
                              collect_timeout: float = 2.0,
                              port: int = 5683,
                              target_addr: Optional[str] = None,
                              echo: Optional[bytes] = None,
                              ) -> list[CoapResponse]:
        """Send an OSCORE-protected NON POST to a multicast address.

        Group OSCORE messages require OSCORE protection with the group
        AT's credentials (sender_id, master_secret).

        Args:
            oscore_ctx: OscoreContext for the group AT
            path: URI path (e.g. "/k")
            payload: Application payload (CBOR s-mode message)
            content_format: Content-Format for inner message
            scope: IPv6 multicast scope
            interface: Network interface for multicast
            collect_timeout: Response collection window
            port: Destination port (default 5683)
            target_addr: Custom multicast address (overrides scope lookup)
            echo: Echo option value for freshness verification

        Returns:
            List of CoapResponse from responders.
        """
        if target_addr:
            mcast_addr = target_addr
        else:
            mcast_addr = KNX_MULTICAST_ADDRESSES.get(scope)
        if not mcast_addr:
            raise ValueError(f"Unknown multicast scope {scope}")

        # Resolve interface
        scope_id = 0
        iface = interface or os.environ.get("DEVICE_IFACE")
        if iface:
            try:
                scope_id = int(iface)
            except (ValueError, TypeError):
                try:
                    scope_id = socket.if_nametoindex(iface)
                except (OSError, AttributeError):
                    pass

        # OSCORE-protect the inner request
        oscore_opt, ciphertext, piv = oscore_ctx.protect_request(
            2, path, payload, content_format=content_format,
            echo=echo)  # 2 = POST

        # Build NON outer message with OSCORE option
        mid = self._next_mid()
        token = self._next_token()
        tkl = len(token)
        ver_type_tkl = (1 << 6) | (NON << 4) | tkl
        code = (POST[0] << 5) | POST[1]
        header = struct.pack("!BBH", ver_type_tkl, code, mid)
        options = _encode_option(9, oscore_opt)
        msg = header + token + options + b"\xFF" + ciphertext

        # Send via multicast socket
        mcast_sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
        mcast_sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_MULTICAST_HOPS,
                              255 if scope > 2 else 1)
        if scope_id:
            mcast_sock.setsockopt(
                socket.IPPROTO_IPV6, socket.IPV6_MULTICAST_IF,
                struct.pack("@I", scope_id))
        mcast_sock.settimeout(collect_timeout)
        mcast_sock.bind(("", 0))

        _dbg_coap(f"[coap] oscore multicast POST -> [{mcast_addr}%{scope_id}]"
                  f":{port}{path}")

        dest = (mcast_addr, port, 0, scope_id)
        mcast_sock.sendto(msg, dest)

        responses = []
        import time
        deadline = time.monotonic() + collect_timeout

        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            mcast_sock.settimeout(remaining)

            try:
                data, addr = mcast_sock.recvfrom(4096)
            except socket.timeout:
                break

            if len(data) < 4:
                continue

            byte0 = data[0]
            msg_type = (byte0 >> 4) & 0x03
            tkl_r = byte0 & 0x0F
            code_byte = data[1]
            code_class = code_byte >> 5
            code_detail = code_byte & 0x1F
            mid_r = struct.unpack("!H", data[2:4])[0]
            token_r = data[4:4 + tkl_r]

            offset = 4 + tkl_r
            resp_options, payload_offset = _parse_options(data, offset)

            resp_payload = (data[payload_offset:]
                            if payload_offset < len(data) else b"")

            ct = None
            if 12 in resp_options:
                ct_bytes = resp_options[12]
                ct = int.from_bytes(ct_bytes, "big") if ct_bytes else 0

            resp = CoapResponse(
                code_class=code_class,
                code_detail=code_detail,
                payload=resp_payload,
                content_format=ct,
                msg_type=msg_type,
                mid=mid_r,
                token=token_r,
                options=resp_options,
                source_addr=addr,
            )
            _dbg_coap(f"[coap] oscore multicast response from {addr[0]}: "
                      f"{resp.code} payload_len={len(resp_payload)}")
            responses.append(resp)

        mcast_sock.close()
        _dbg_coap(f"[coap] oscore multicast: collected {len(responses)} "
                  f"response(s)")
        return responses

    @staticmethod
    def parse_coap_message(data: bytes) -> dict:
        """Parse a raw CoAP message into its components.

        Returns a dict with: type, code_class, code_detail, code_byte,
        mid, token, options, payload.
        """
        if len(data) < 4:
            return {}
        byte0 = data[0]
        msg_type = (byte0 >> 4) & 0x03
        tkl = byte0 & 0x0F
        code_byte = data[1]
        mid = struct.unpack("!H", data[2:4])[0]
        token = data[4:4 + tkl]

        offset = 4 + tkl
        options, payload_offset = _parse_options(data, offset)
        payload = (data[payload_offset:]
                   if payload_offset < len(data) else b"")

        return {
            "type": msg_type,
            "code_class": code_byte >> 5,
            "code_detail": code_byte & 0x1F,
            "code_byte": code_byte,
            "mid": mid,
            "token": token,
            "options": options,
            "payload": payload,
        }

    def send_echo_challenge(self, oscore_ctx, request_kid: bytes,
                            request_piv: bytes, echo: bytes,
                            token: bytes,
                            kid_context: Optional[bytes] = None) -> None:
        """Send a 4.01+Echo challenge answering a telegram the DUT sent us.

        Fire-and-forget: it is a NON response, and what the test asserts on
        is the DUT's RETRANSMISSION of its telegram, captured separately by
        a group listener. The CoAP token must be the one from the telegram
        being challenged — the device correlates the echo to its pending
        s-mode message by mid+token (oc_oscore_engine.c:170).
        """
        import os as _os
        if kid_context is None:
            # 10 random bytes: the length is what marks this as an echo
            # response to the device (OC_ECHO_KID_CTX_LEN), and EITT's
            # RndKid="yes" makes the content random.
            kid_context = _os.urandom(10)

        oscore_opt, ciphertext = oscore_ctx.protect_echo_challenge(
            request_kid, request_piv, echo, kid_context)

        mid = self._next_mid()
        tkl = len(token)
        ver_type_tkl = (1 << 6) | (NON << 4) | tkl
        # Outer code is POST for an OSCORE-protected message; the real
        # 4.01 lives in the encrypted inner message.
        code = (POST[0] << 5) | POST[1]
        msg = struct.pack("!BBH", ver_type_tkl, code, mid) + token
        msg += _encode_option(9, oscore_opt)
        msg += b"\xff" + ciphertext

        _dbg_oscore(f"[oscore] sending 4.01 Echo challenge kid="
                    f"{request_kid.hex()} piv={request_piv.hex()} "
                    f"kid_ctx={kid_context.hex()}")
        self._sock.sendto(msg, (self.host, self.port, 0, 0))

    def send_plain_response(self, code: tuple, payload: bytes,
                            token: bytes, dest: tuple,
                            content_format: Optional[int] = None,
                            msg_type: int = NON,
                            mid: Optional[int] = None) -> None:
        """Send an UNPROTECTED CoAP response to `dest`, quoting `token`.

        Needed when the tool has to answer a request the DUT addressed to
        IT rather than the other way round — EITT's `UseLastRxToken`. The
        token is what correlates the answer to the DUT's pending request,
        so it must be the token off that request, not a fresh one.

        `dest` is the (host, port, flowinfo, scope_id) tuple as returned by
        recvfrom, so the reply goes back to the exact socket that asked.
        """
        if mid is None:
            mid = self._next_mid()
        tkl = len(token)
        ver_type_tkl = (1 << 6) | (msg_type << 4) | tkl
        code_byte = (code[0] << 5) | code[1]
        msg = struct.pack("!BBH", ver_type_tkl, code_byte, mid) + token
        if content_format is not None:
            cf = content_format.to_bytes(
                (content_format.bit_length() + 7) // 8 or 1, "big")
            msg += _encode_option(12, cf)
        if payload:
            msg += b"\xFF" + payload
        _dbg_coap(f"[coap] plain response {code[0]}.{code[1]:02d} "
                  f"token={token.hex()} -> {dest[0]}:{dest[1]} "
                  f"({len(payload)}B)")
        self._sock.sendto(msg, dest)

    def send_empty_ack(self, mid: int, dest: tuple) -> None:
        """Acknowledge a CON message: an empty ACK (code 0.00, no token).

        RFC 7252 §5.2.2 — a separate ACK carries the request's Message ID
        and an empty code, and the real answer follows later (or never, for
        a telegram that needs none). Without it the DUT retransmits the
        telegram, and those copies arrive on top of whatever request runs
        next on this socket.
        """
        ver_type_tkl = (1 << 6) | (ACK << 4)
        msg = struct.pack("!BBH", ver_type_tkl, 0, mid)
        _dbg_coap(f"[coap] empty ACK mid={mid} -> {dest[0]}:{dest[1]}")
        self._sock.sendto(msg, dest)

    def arm_plain_listener(self, mcast_addr: str,
                           port: int = 5683,
                           interface: Optional[str] = None,
                           timeout: float = 5.0,
                           path: Optional[str] = None) -> "PlainListener":
        """Bind and join `mcast_addr` NOW to capture an UNPROTECTED request
        the DUT sends to us (e.g. its `GET /.well-known/core` discovery
        query), returning a handle whose result() waits for it.

        Split from collection for the same reason as arm_group_listener:
        the query is provoked by a step we are about to run, so binding
        afterwards races it. `path` (if given) filters on the request's
        Uri-Path so an unrelated multicast query is not mistaken for the
        one being waited on.
        """
        return PlainListener(self, mcast_addr, port, interface, timeout, path)

    def arm_group_listener(self, oscore_ctx, mcast_addr: str,
                           port: int = 5683,
                           interface: Optional[str] = None,
                           timeout: float = 5.0) -> "GroupListener":
        """Bind and join the group NOW, returning a handle whose result()
        waits for and decrypts the first matching telegram.

        Bind/join is split from collection because a group read is answered
        by the DUT *while it handles the request* (oc_knx_client.c's
        "Sending s-mode multicast" happens inside the /k POST handler),
        so binding after the request has already been sent races the
        response and usually loses it. Arm first, send, then collect.
        """
        return GroupListener(self, oscore_ctx, mcast_addr, port,
                             interface, timeout)

    def recv_unicast_telegram(self, oscore_ctx,
                              timeout: float = 5.0,
                              path: Optional[str] = None
                              ) -> "UnicastTelegramListener":
        """Wait for an unsolicited request the DUT sends us by UNICAST,
        returning a handle whose result() decrypts it.

        There is NOTHING TO ARM here, unlike arm_group_listener /
        arm_plain_listener which each bind and join a multicast address.
        This telegram arrives at the tool's ordinary unicast endpoint —
        `self._sock`, bound once in __init__ and used for every request —
        so it is simply a read of a socket that was already listening.

        That mirrors EITT itself: its comment-command grammar
        (`EITT_KnxCommentCommandsScheme.xml`) has NO listen/capture/receive
        command of any kind, and neither 5.4.1.4 nor 5.4.1.15 contains
        anything that could start one. EITT's CoAP endpoint is simply always
        up, and an `OUT` step means "wait for a matching telegram on it".
        (`pahmcast"...;join=true"` is about GROUP MEMBERSHIP, not about
        starting a listener.) The handle exists only to keep a timeout and
        the decrypt/ACK bookkeeping in one place.

        EITT 5.4.1.4 and 5.4.1.15 need this. The DUT does not know our IPv6
        address up front; it multicasts
        `GET /.well-known/core?ep=knx://ia.<iid>.<ia>` to resolve us
        (oc_knx_client.c knx_add_ipv6_address_coap_discovery, which then
        allows 25s for an answer), we reply with our own ep, and only THEN
        does it send the s-mode telegram as a unicast CON POST /k.
        traces/5_4_1.xml shows the full chain as TelNo 321 -> 322 -> 323.

        A CON telegram is ACKed automatically — the DUT retransmits
        otherwise, and the retransmissions confuse whatever request runs
        next on this socket.
        """
        return UnicastTelegramListener(self, oscore_ctx, timeout, path)

    def _decrypt_group_telegram(self, captured: list, oscore_ctx
                                ) -> Optional[CoapResponse]:
        """Turn raw captured multicast messages into the first one that
        decrypts under `oscore_ctx` (skipping telegrams meant for other
        contexts), or None."""
        for msg, addr in captured:
            options = msg.get("options") or {}
            if 9 not in options:
                # Unprotected group telegram — surface as-is.
                return CoapResponse(
                    code_class=msg["code_class"],
                    code_detail=msg["code_detail"],
                    payload=msg["payload"],
                    content_format=options.get(12),
                    msg_type=msg["type"], mid=msg["mid"],
                    token=msg.get("token", b""),
                    options=options, source_addr=addr)
            try:
                result = oscore_ctx.unprotect_request(options[9],
                                                     msg["payload"])
            except (InvalidTag, IndexError, ValueError) as exc:
                # Not for this context. Broader than the response path
                # (which only tolerates InvalidTag) ON PURPOSE: this is
                # multicast, so telegrams for OTHER contexts genuinely
                # arrive here, and unprotect_request indexes into the
                # OSCORE option before it ever reaches the AEAD — a
                # telegram whose option layout belongs to a different
                # context can raise IndexError/ValueError long before
                # InvalidTag. Still narrowed from `Exception` so a
                # TypeError/AttributeError in our own code surfaces
                # instead of masquerading as a foreign telegram.
                _dbg_oscore(f"[oscore] group telegram did not decrypt under "
                            f"this Rx context ({exc}); skipping")
                continue
            inner_code, inner_payload, inner_options = result[0], result[1], result[2]
            kid, piv = _parse_oscore_option(options[9])
            return CoapResponse(
                code_class=inner_code >> 5,
                code_detail=inner_code & 0x1F,
                payload=inner_payload,
                content_format=inner_options.get(12),
                msg_type=msg["type"], mid=msg["mid"],
                token=msg.get("token", b""),
                options=inner_options, source_addr=addr,
                echo_value=inner_options.get(252),
                oscore_kid=kid, oscore_piv=piv)
        return None

    def listen_multicast(self, mcast_addr: str, port: int = 5683,
                         interface: Optional[str] = None,
                         timeout: float = 5.0,
                         max_messages: int = 1,
                         on_message=None) -> list[tuple[dict, tuple]]:
        """Join a multicast group and listen for incoming CoAP messages.

        Args:
            mcast_addr: IPv6 multicast address to join.
            port: UDP port to listen on (default 5683).
            interface: Network interface name (e.g. "veth-test").
                       Falls back to DEVICE_IFACE env var.
            timeout: How long to listen (seconds).
            max_messages: Stop after receiving this many messages; pass 0
                       for "no count limit" (bounded only by timeout or
                       on_message).
            on_message: Optional callback invoked as (msg, addr) for each
                       parsed message. Returning True stops listening
                       immediately — lets a caller that recognises the
                       message it wanted avoid waiting out the timeout.

        Returns:
            List of (parsed_message_dict, sender_address) tuples.
        """
        scope_id = 0
        iface = interface or os.environ.get("DEVICE_IFACE")
        if iface:
            try:
                scope_id = int(iface)
            except ValueError:
                try:
                    scope_id = socket.if_nametoindex(iface)
                except (OSError, AttributeError):
                    _dbg_coap(f"[coap] Warning: could not resolve '{iface}'")

        sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(("", port))

        # Join multicast group
        mreq = struct.pack(
            "16sI",
            socket.inet_pton(socket.AF_INET6, mcast_addr),
            scope_id)
        sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_JOIN_GROUP, mreq)
        sock.settimeout(timeout)

        _dbg_coap(f"[coap] listening on [{mcast_addr}%{scope_id}]:{port}")

        import time
        messages = []
        deadline = time.monotonic() + timeout

        try:
            while max_messages == 0 or len(messages) < max_messages:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    break
                sock.settimeout(remaining)
                try:
                    data, addr = sock.recvfrom(_RECV_BUF)
                except socket.timeout:
                    break
                msg = self.parse_coap_message(data)
                if msg:
                    _dbg_coap(f"[coap] multicast recv from {addr[0]}:{addr[1]}"
                              f" type={msg['type']}"
                              f" code={msg['code_class']}.{msg['code_detail']:02d}"
                              f" opts={sorted(msg['options'].keys())}"
                              f" payload_len={len(msg['payload'])}")
                    messages.append((msg, addr))
                    if on_message is not None and on_message(msg, addr):
                        break
        finally:
            sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_LEAVE_GROUP,
                            mreq)
            sock.close()

        _dbg_coap(f"[coap] multicast listener: captured {len(messages)} message(s)")
        return messages


class GroupListener:
    """A multicast group listener that is already bound and joined.

    Created by CoapClient.arm_group_listener() BEFORE the request that
    provokes the telegram is sent, because the DUT answers a group read
    from inside its own request handler — bind afterwards and the reply
    is usually already gone. result() joins the listener thread and
    returns the first telegram that decrypts under the bound context.
    """

    def __init__(self, client: "CoapClient", oscore_ctx, mcast_addr: str,
                 port: int, interface: Optional[str], timeout: float):
        import threading
        import time
        self._client = client
        self._oscore_ctx = oscore_ctx
        self._timeout = timeout
        self._result: Optional[CoapResponse] = None
        self._ready = threading.Event()
        self._done = threading.Event()

        def _accept(msg, addr) -> bool:
            """Called per captured datagram. Returning True stops the
            listener immediately — a group we joined also carries other
            parties' traffic (and, when the tool is the sender, its own
            echo), so 'first datagram' is not 'the telegram we want';
            'first datagram that decrypts under this context' is. Stopping
            here rather than waiting out the timeout matters: 5.3.19.5
            plays 100 read/response pairs, and a 5s wait on each turns an
            11s test into 8.5 minutes."""
            resp = client._decrypt_group_telegram([(msg, addr)], oscore_ctx)
            if resp is None:
                return False
            self._result = resp
            self._done.set()
            return True

        def _listen():
            self._ready.set()
            client.listen_multicast(
                mcast_addr, port=port, interface=interface,
                timeout=timeout, max_messages=0, on_message=_accept)
            self._done.set()

        self._thread = threading.Thread(target=_listen, daemon=True)
        self._thread.start()
        self._ready.wait(timeout=2.0)
        time.sleep(0.3)           # let the bind/join actually settle

    def result(self) -> Optional[CoapResponse]:
        self._done.wait(timeout=self._timeout + 2)
        self._thread.join(timeout=2.0)
        return self._result


class PlainListener:
    """An already-bound listener for an UNPROTECTED request the DUT sends
    to us, created by CoapClient.arm_plain_listener().

    The captured CoapResponse carries the request's `token` and
    `source_addr`, which is what CoapClient.send_plain_response() needs to
    answer it (EITT's `UseLastRxToken`).
    """

    def __init__(self, client: "CoapClient", mcast_addr: str, port: int,
                 interface: Optional[str], timeout: float,
                 path: Optional[str]):
        import threading
        import time
        self._client = client
        self._timeout = timeout
        self._result: Optional[CoapResponse] = None
        self._ready = threading.Event()
        self._done = threading.Event()
        want = (path or "").strip("/") or None

        # Uri-Path (option 11) is repeatable, but parse_coap_message keeps
        # only the LAST segment (a GET /.well-known/core parses back as
        # {11: b'core'}), so match on the final segment rather than the
        # whole path.
        want_last = want.rsplit("/", 1)[-1] if want else None

        def _accept(msg, addr) -> bool:
            options = msg.get("options") or {}
            if want_last is not None:
                got = options.get(11)
                if isinstance(got, bytes):
                    got = got.decode(errors="replace")
                if (got or "") != want_last:
                    return False
            self._result = CoapResponse(
                code_class=msg["code_class"],
                code_detail=msg["code_detail"],
                payload=msg["payload"],
                content_format=options.get(12),
                msg_type=msg["type"], mid=msg["mid"],
                token=msg.get("token", b""),
                options=options, source_addr=addr)
            self._done.set()
            return True

        def _listen():
            self._ready.set()
            client.listen_multicast(
                mcast_addr, port=port, interface=interface,
                timeout=timeout, max_messages=0, on_message=_accept)
            self._done.set()

        self._thread = threading.Thread(target=_listen, daemon=True)
        self._thread.start()
        self._ready.wait(timeout=2.0)
        time.sleep(0.3)           # let the bind/join settle

    def result(self) -> Optional[CoapResponse]:
        self._done.wait(timeout=self._timeout + 2)
        self._thread.join(timeout=2.0)
        return self._result


class UnicastTelegramListener:
    """A read of the client's ALREADY-OPEN socket, waiting for an unsolicited
    request the DUT sends us by unicast. Created by
    CoapClient.recv_unicast_telegram().

    Unlike GroupListener/PlainListener there is no multicast address to bind
    or join and nothing to arm: the telegram arrives on the client's own
    socket, which has been listening since __init__, so this just reads
    `client._sock`. It is a class only to keep the timeout, the CON ACK and
    the decrypt in one place. One consequence worth knowing — while the read
    is running it OWNS the socket, so no ordinary request may be issued on
    the same CoapClient until result() has returned.

    See CoapClient.recv_unicast_telegram for why the DUT can only send this
    after it has resolved our address via multicast discovery, and for why
    EITT needs no listen command either.
    """

    def __init__(self, client: "CoapClient", oscore_ctx,
                 timeout: float, path: Optional[str] = None):
        import threading
        self._client = client
        self._oscore_ctx = oscore_ctx
        self._timeout = timeout
        self._result: Optional[CoapResponse] = None
        self._done = threading.Event()
        # Diagnostics for the "none arrived" case: how many datagrams were
        # seen and rejected, so a caller can tell "nothing came" from
        # "something came but was not ours".
        self._seen = 0
        self._skipped = 0
        want = (path or "").strip("/") or None
        # parse_coap_message keeps only the LAST Uri-Path segment, so match
        # on that (same reasoning as PlainListener).
        self._want_last = want.rsplit("/", 1)[-1] if want else None
        self._thread = threading.Thread(target=self._listen, daemon=True)
        self._thread.start()

    def _listen(self) -> None:
        client = self._client
        sock = client._sock
        old_timeout = sock.gettimeout()
        deadline = time.monotonic() + self._timeout
        try:
            # Anything already buffered wins: the telegram may have landed
            # while an earlier step was reading a response (5.4.1.4's arrives
            # 0.1s after our ACK), in which case it is here rather than still
            # on the socket.
            while client._pending_requests:
                msg, addr = client._pending_requests.pop(0)
                if self._accept(msg, addr):
                    return
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return
                sock.settimeout(remaining)
                try:
                    data, addr = sock.recvfrom(_RECV_BUF)
                except socket.timeout:
                    return
                except OSError:
                    return
                msg = client.parse_coap_message(data)
                if not msg:
                    continue
                if self._accept(msg, addr):
                    return
        finally:
            try:
                sock.settimeout(old_timeout)
            except OSError:
                pass
            self._done.set()

    def _accept(self, msg: dict, addr: tuple) -> bool:
        """Decide one message. True when it is the telegram we wanted (and
        `self._result` has been set); False to keep looking."""
        client = self._client
        self._seen += 1
        options = msg.get("options") or {}
        # Only a REQUEST (code class 0) can be a telegram the DUT
        # originated; anything else is a stale response still in
        # flight on this socket.
        if msg.get("code_class") != 0:
            _dbg_coap("[coap] unicast listener skipped a "
                      f"{msg.get('code_class')}.{msg.get('code_detail'):02d} "
                      "(not a request)")
            return False
        # NO path filter before decryption. An OSCORE-protected
        # request carries its Uri-Path INSIDE the ciphertext — the
        # outer options hold only the OSCORE option (9) — so matching
        # `options[11]` here rejects every protected telegram. (It
        # cost a debugging round: 3 datagrams arrived, all decryptable,
        # and all were dropped by exactly that test.) The path is
        # checked after decryption below, and only when the message
        # was NOT protected, where an outer path really does exist.
        if self._want_last is not None and 9 not in options:
            got = options.get(11)
            if isinstance(got, bytes):
                got = got.decode(errors="replace")
            if (got or "") != self._want_last:
                return False
        # A CON obliges us to ACK, or the DUT keeps retransmitting
        # and those copies land on top of the next request.
        if msg.get("type") == CON:
            try:
                client.send_empty_ack(msg["mid"], addr)
            except OSError as exc:
                _dbg_coap(f"[coap] unicast ACK failed: {exc}")
        resp = client._decrypt_group_telegram(
            [(msg, addr)], self._oscore_ctx)
        if resp is None:
            self._skipped += 1
            _dbg_coap("[coap] unicast telegram did not decrypt "
                      "under the bound context; skipping")
            return False
        # Now the inner options are available, so a protected
        # telegram's real Uri-Path can be checked.
        if self._want_last is not None and 9 in options:
            inner = (resp.options or {}).get(11)
            if isinstance(inner, bytes):
                inner = inner.decode(errors="replace")
            if (inner or "") != self._want_last:
                self._skipped += 1
                _dbg_coap(f"[coap] unicast telegram inner path "
                          f"{inner!r} != {self._want_last!r}; skipping")
                return False
        _dbg_coap(f"[coap] unicast telegram captured from "
                  f"{addr[0]}:{addr[1]} "
                  f"{resp.code_class}.{resp.code_detail:02d}")
        self._result = resp
        return True

    def result(self) -> Optional[CoapResponse]:
        self._done.wait(timeout=self._timeout + 2)
        self._thread.join(timeout=2.0)
        return self._result
