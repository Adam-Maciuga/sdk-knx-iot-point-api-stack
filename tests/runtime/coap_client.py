"""
Minimal CoAP client using raw UDP sockets.

Replaces aiocoap for the runtime conformance tests because aiocoap's
Context management exhausts after ~11 requests in a shared session.

This client is synchronous and stateless — each request is a single
UDP send/recv pair with no retransmission, which is fine for localhost
testing where packet loss doesn't occur.
"""

import os
import socket
import struct
from dataclasses import dataclass
from typing import Optional

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


def _parse_options(data: bytes, offset: int) -> tuple[dict, int]:
    """Parse CoAP options from raw bytes. Returns (options_dict, payload_offset)."""
    options = {}
    opt_num = 0
    while offset < len(data):
        if data[offset] == 0xFF:
            # Payload marker
            offset += 1
            break
        byte = data[offset]
        delta = (byte >> 4) & 0x0F
        length = byte & 0x0F
        offset += 1

        if delta == 13:
            delta = data[offset] + 13
            offset += 1
        elif delta == 14:
            delta = struct.unpack("!H", data[offset:offset + 2])[0] + 269
            offset += 2

        if length == 13:
            length = data[offset] + 13
            offset += 1
        elif length == 14:
            length = struct.unpack("!H", data[offset:offset + 2])[0] + 269
            offset += 2

        opt_num += delta
        opt_val = data[offset:offset + length]
        offset += length

        options[opt_num] = opt_val

    return options, offset


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
        token = struct.pack("!H", self._token_counter & 0xFFFF)
        self._token_counter += 1
        return token

    def _build_request(self, method: tuple, uri_path: str,
                       payload: bytes = b"",
                       accept: Optional[int] = None,
                       content_format: Optional[int] = None,
                       observe: Optional[int] = None,
                       token: Optional[bytes] = None) -> bytes:
        """Build a CoAP CON request message.

        Args:
            observe: If not None, encode the CoAP Observe option (6).
                     0 = register, 1 = deregister (RFC 7641).
            token: If provided, use this exact token instead of generating
                   a fresh one (needed to deregister/refresh an existing
                   observe relationship, which is keyed by token).
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

        msg = header + token + options
        if payload:
            msg += b"\xFF" + payload

        return msg

    def drain_socket(self, timeout: float = 0.1):
        """Drain any pending messages from the socket.

        Useful after factory reset or test failures to clear stale
        retransmissions from the DUT.
        """
        old_timeout = self._sock.gettimeout()
        self._sock.settimeout(timeout)
        drained = 0
        try:
            while True:
                self._sock.recvfrom(4096)
                drained += 1
        except socket.timeout:
            pass
        finally:
            self._sock.settimeout(old_timeout)
        if drained:
            _dbg_coap(f"[coap] drained {drained} stale message(s)")

    def request(self, method: tuple, path: str,
                payload: bytes = b"",
                accept: Optional[int] = None,
                content_format: Optional[int] = None,
                timeout: Optional[float] = None) -> Optional[CoapResponse]:
        """
        Send a CoAP request and wait for a response.
        Handles CoAP separate responses (empty ACK followed by CON).
        Returns CoapResponse or None on timeout.
        """
        msg = self._build_request(method, path, payload, accept,
                                  content_format)
        # Extract expected token from built message
        tkl = msg[0] & 0x0F
        expected_token = msg[4:4 + tkl]

        old_timeout = self._sock.gettimeout()
        effective_timeout = timeout if timeout is not None else self.timeout
        self._sock.settimeout(effective_timeout)

        try:
            self._sock.sendto(msg, (self.host, self.port, 0, 0))
            return self._recv_response(effective_timeout, expected_token)
        except socket.timeout:
            return None
        finally:
            self._sock.settimeout(old_timeout)

    def _recv_response(self, timeout: float,
                       expected_token: bytes = None) -> Optional[CoapResponse]:
        """Receive a CoAP response, handling separate response pattern.

        Skips messages whose token doesn't match expected_token (stale
        retransmissions from DUT).
        """
        import time
        deadline = time.monotonic() + timeout

        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            self._sock.settimeout(remaining)

            try:
                data, addr = self._sock.recvfrom(4096)
            except socket.timeout:
                return None

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

            # Empty ACK (code 0.00) = separate response pattern.
            # The actual response will follow as a CON message.
            if msg_type == ACK and code_byte == 0:
                continue

            # If server sent a CON response, ACK it.
            if msg_type == CON:
                ack = struct.pack("!BBH", (1 << 6) | (ACK << 4), 0, mid)
                self._sock.sendto(ack, (self.host, self.port, 0, 0))

            # Skip messages with wrong token (stale retransmissions)
            if expected_token is not None and token != expected_token:
                _dbg_coap(f"[coap] skipping stale msg: token={token.hex()}"
                          f" expected={expected_token.hex()}")
                continue

            offset = 4 + tkl
            options, payload_offset = _parse_options(data, offset)

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
        return self.request(GET, path, accept=accept, timeout=timeout)

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
                              token: Optional[bytes] = None) -> bytes:
        """Build a CoAP CON POST request carrying an OSCORE option.

        OSCORE outer message:
        - Code = POST (0.02)
        - Option 6 (Observe, outer/Class U) = observe, if requested
        - Option 9 (OSCORE) = oscore_option
        - Payload = ciphertext (the encrypted inner message)

        The inner (encrypted) message also carries the Observe option; the
        outer copy is required so the receiving CoAP/observe layer treats
        the message as an observe (de)registration (RFC 8613 §4.1.3.5).
        """
        mid = self._next_mid()
        if token is None:
            token = self._next_token()
        tkl = len(token)

        ver_type_tkl = (1 << 6) | (CON << 4) | tkl
        code = (POST[0] << 5) | POST[1]  # 0.02
        header = struct.pack("!BBH", ver_type_tkl, code, mid)

        # Outer options in option-number order: Observe (6) then OSCORE (9)
        options = b""
        prev_opt = 0
        if observe is not None:
            obs_bytes = (b"" if observe == 0
                         else observe.to_bytes(
                             (observe.bit_length() + 7) // 8 or 1, "big"))
            options += _encode_option(6 - prev_opt, obs_bytes)
            prev_opt = 6
        options += _encode_option(9 - prev_opt, oscore_option)

        msg = header + token + options + b"\xFF" + ciphertext
        return msg

    def oscore_request(self, oscore_ctx, method_code: int, path: str,
                       payload: bytes = b"",
                       uri_queries: list = None,
                       accept: Optional[int] = None,
                       content_format: Optional[int] = None,
                       timeout: Optional[float] = None) -> Optional[
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

        Returns:
            CoapResponse with decrypted payload, or None on timeout.
            The code_class/code_detail reflect the INNER response code.
        """
        echo_value = None
        max_retries = 3

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
                accept=accept, content_format=content_format)

            msg = self._build_oscore_request(oscore_opt, ciphertext)
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

            # Check for Echo challenge (option 252) — both inner
            # (OSCORE-protected) and outer (unprotected 4.01)
            if (resp.code_class == 4 and resp.code_detail == 1
                    and 252 in inner_options):
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
                data, addr = self._sock.recvfrom(4096)
            except socket.timeout:
                return None

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

            # ACK CON responses
            if msg_type == CON:
                ack = struct.pack("!BBH", (1 << 6) | (ACK << 4), 0, mid)
                self._sock.sendto(ack, (self.host, self.port, 0, 0))

            # Skip messages with wrong token (stale DUT retransmissions)
            if expected_token is not None and token != expected_token:
                _dbg_coap(f"[coap] skipping stale oscore msg: token={token.hex()}"
                          f" expected={expected_token.hex()}")
                continue

            offset = 4 + tkl
            options, payload_offset = _parse_options(data, offset)

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
        return self.oscore_request(oscore_ctx, 1, path, accept=accept,
                                  timeout=timeout)

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

    # ------------------------------------------------------------------
    # CoAP Observe (RFC 7641)
    # ------------------------------------------------------------------
    #
    # A single client socket = a single subscription source (IP+port).
    # The KNX stack replaces a subscription based on source IP+port, not
    # the CoAP token (spec 2.6.10.1), so dedicate one CoapClient per
    # observer when a test needs independent subscriptions.

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
                    data, _ = self._sock.recvfrom(4096)
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
                options, payload_offset = _parse_options(data, offset)
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
                       ) -> list[CoapResponse]:
        """Send a NON CoAP request to a KNX multicast address and collect
        all responses within collect_timeout seconds.

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

        Returns:
            List of CoapResponse from all responders.
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
                data, addr = mcast_sock.recvfrom(4096)
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
            options, payload_offset = _parse_options(data, offset)

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
            )
            _dbg_coap(f"[coap] multicast response from {addr[0]}: "
                      f"{resp.code} payload_len={len(payload_data)}")
            responses.append(resp)

        mcast_sock.close()
        _dbg_coap(f"[coap] multicast: collected {len(responses)} response(s)")
        return responses

    def multicast_get(self, path: str, scope: int = _DEFAULT_MC_SCOPE,
                      accept: Optional[int] = None,
                      interface: Optional[str] = None,
                      collect_timeout: float = 2.0,
                      ) -> list[CoapResponse]:
        """Send a multicast GET and collect responses."""
        return self.send_multicast(GET, path, accept=accept, scope=scope,
                                  interface=interface,
                                  collect_timeout=collect_timeout)

    def multicast_post(self, path: str, payload: bytes = b"",
                       content_format: int = APPLICATION_CBOR,
                       scope: int = _DEFAULT_MC_SCOPE,
                       interface: Optional[str] = None,
                       collect_timeout: float = 2.0,
                       ) -> list[CoapResponse]:
        """Send a multicast POST and collect responses."""
        return self.send_multicast(POST, path, payload=payload,
                                  content_format=content_format, scope=scope,
                                  interface=interface,
                                  collect_timeout=collect_timeout)

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

    # ------------------------------------------------------------------
    # Multicast listener (receive unsolicited group messages)
    # ------------------------------------------------------------------

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

    def listen_multicast(self, mcast_addr: str, port: int = 5683,
                         interface: Optional[str] = None,
                         timeout: float = 5.0,
                         max_messages: int = 1) -> list[tuple[dict, tuple]]:
        """Join a multicast group and listen for incoming CoAP messages.

        Args:
            mcast_addr: IPv6 multicast address to join.
            port: UDP port to listen on (default 5683).
            interface: Network interface name (e.g. "veth-test").
                       Falls back to DEVICE_IFACE env var.
            timeout: How long to listen (seconds).
            max_messages: Stop after receiving this many messages.

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
            while len(messages) < max_messages:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    break
                sock.settimeout(remaining)
                try:
                    data, addr = sock.recvfrom(4096)
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
        finally:
            sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_LEAVE_GROUP,
                            mreq)
            sock.close()

        _dbg_coap(f"[coap] multicast listener: captured {len(messages)} message(s)")
        return messages
