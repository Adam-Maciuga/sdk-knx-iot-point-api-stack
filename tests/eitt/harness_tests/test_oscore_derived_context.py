"""The device signs its 4.01 Echo challenge for an s-mode MULTICAST write
with a context derived under a FRESH RANDOM ID Context
(oc_oscore_engine.c, unicast_echo_response_by_mc). The tool must derive the
same keys from the kid context the outer OSCORE option carries - EITT logs
this as "SPECIAL DECODING: use received random kid_context". These tests
pin that derivation (`OscoreContext.with_id_context`) and the client's use
of it in `_recv_oscore_response` (tracker T13 / F6 CheckRxKid).

No device, no network: the device role is played by a second context built
from the same access-token material, exactly as the C code does.
"""
import os
import socket
import struct

import pytest
from cryptography.exceptions import InvalidTag

from transport.coap_client import (CoapClient, _parse_oscore_option_full,
                                   _parse_options)
from transport.knx_oscore import OscoreContext

_MS = bytes.fromhex("cbcfc5c8471986cc29268499d749bf6e")
_KID = bytes.fromhex("102a3b4c5d6e7f")   # group AT: sender id == recipient id
_GROUP_CTX = bytes.fromhex("102a3b")
_ECHO_KID_CTX_LEN = 10


def _tool_context() -> OscoreContext:
    return OscoreContext(master_secret=_MS, sender_id=_KID, recipient_id=_KID,
                         id_context=_GROUP_CTX)


def _device_echo_challenge(request_piv: bytes, echo: bytes,
                           rnd_ctx: bytes) -> tuple[bytes, bytes]:
    """What the device puts on the wire: a 4.01 + Echo signed under a
    context derived with `rnd_ctx`, PIV as in the challenged request.

    Built the way oc_oscore_engine.c (unicast_echo_response_by_mc) does it,
    not with the tool-side `protect_echo_challenge`: the device's sender
    context takes `sender_id` from the token's osc id
    (oc_oscore_add_sender_context), so the key is derived with THAT id,
    the nonce uses it and the AAD names it; the tool-side helper signs with
    the empty-id key instead, which is the other direction of the same
    handshake and would make the peer-id test below pass for the wrong
    reason.
    """
    from cryptography.hazmat.primitives.ciphers.aead import AESCCM
    from transport.knx_oscore import TAG_LEN
    device = OscoreContext(master_secret=_MS, sender_id=_KID,
                           recipient_id=_KID, id_context=rnd_ctx)
    plaintext = bytes([0x81]) + device._encode_option(252, echo)
    nonce = device._build_nonce(_KID, request_piv)
    aad = device._build_aad(_KID, request_piv)
    ciphertext = AESCCM(device.sender_key, tag_length=TAG_LEN).encrypt(
        nonce, plaintext, aad)
    opt = device._build_oscore_option(request_piv, kid=_KID,
                                      kid_context=rnd_ctx)
    return opt, ciphertext


def test_with_id_context_keeps_ids_and_secret_but_rederives_keys():
    base = _tool_context()
    rnd = os.urandom(_ECHO_KID_CTX_LEN)
    sib = base.with_id_context(rnd)
    assert sib.id_context == rnd
    assert (sib.sender_id, sib.recipient_id) == (base.sender_id,
                                                 base.recipient_id)
    assert sib.master_secret == base.master_secret
    assert sib.recipient_key != base.recipient_key
    assert sib.common_iv != base.common_iv
    # The original is untouched, including its sequence number.
    assert base.id_context == _GROUP_CTX
    assert base.ssn == 0


def test_configured_context_cannot_open_a_random_context_challenge():
    tool = _tool_context()
    rnd = os.urandom(_ECHO_KID_CTX_LEN)
    opt, ct = _device_echo_challenge(b"\x05", b"\x11\x22\x33\x44", rnd)
    with pytest.raises(InvalidTag):
        tool.unprotect_response(opt, ct, request_piv=b"\x05")


def test_sibling_context_opens_the_challenge_and_recovers_the_echo():
    tool = _tool_context()
    rnd = os.urandom(_ECHO_KID_CTX_LEN)
    echo = b"\x93\x6c\x54\xa7\xa0\x01\x00\x00"
    opt, ct = _device_echo_challenge(b"\x05", echo, rnd)
    kid, piv, kid_ctx = _parse_oscore_option_full(opt)
    assert (kid, piv, kid_ctx) == (_KID, b"\x05", rnd)
    code, payload, inner = tool.with_id_context(kid_ctx).unprotect_response(
        opt, ct, request_piv=b"\x05")
    assert code == 0x81           # 4.01
    assert inner.get(252) == echo  # Echo option recovered
    assert payload == b""


def test_sibling_context_takes_the_peer_id_from_the_wire_kid():
    """5.4.1.12 provisions MulticastTx WITHOUT a recipient id (5.3.17.3
    provisions one). The device derives the echo context's keys from the
    kid it puts in the option, so the sibling must use that kid as the peer
    id whatever the script configured - the configured context alone, and
    a sibling that keeps the empty recipient id, both fail the tag check."""
    tool = OscoreContext(master_secret=_MS, sender_id=_KID, recipient_id=b"",
                         id_context=_GROUP_CTX)
    rnd = os.urandom(_ECHO_KID_CTX_LEN)
    echo = b"\x0a\x0b\x0c\x0d\x0e\x0f\x10\x11"
    opt, ct = _device_echo_challenge(b"\x03", echo, rnd)
    kid, _piv, kid_ctx = _parse_oscore_option_full(opt)
    with pytest.raises(InvalidTag):
        tool.unprotect_response(opt, ct, request_piv=b"\x03")
    with pytest.raises(InvalidTag):
        tool.with_id_context(kid_ctx).unprotect_response(
            opt, ct, request_piv=b"\x03")
    code, _payload, inner = tool.with_id_context(
        kid_ctx, recipient_id=kid).unprotect_response(
            opt, ct, request_piv=b"\x03")
    assert code == 0x81
    assert inner.get(252) == echo


def _serialise_response(msg_type: int, mid: int, token: bytes,
                        oscore_opt: bytes, ciphertext: bytes) -> bytes:
    """A NON response: outer code 4.01, options {9: oscore_opt}, payload."""
    hdr = bytes([0x40 | (msg_type << 4) | len(token), 0x81]) + struct.pack(
        "!H", mid) + token
    # Option 9 (OSCORE), delta 9 from 0; length < 13 or extended.
    ln = len(oscore_opt)
    if ln < 13:
        opt = bytes([(9 << 4) | ln]) + oscore_opt
    else:
        opt = bytes([(9 << 4) | 13, ln - 13]) + oscore_opt
    return hdr + opt + b"\xff" + ciphertext


def test_recv_oscore_response_derives_the_context_from_the_wire(monkeypatch):
    """End to end through the client's receive path: the datagram carries
    a kid context the tool never configured, and the client still yields
    a decrypted 4.01 with the Echo option and surfaces the kid context."""
    tool = _tool_context()
    rnd = os.urandom(_ECHO_KID_CTX_LEN)
    echo = b"\x01\x02\x03\x04\x05\x06\x07\x08"
    request_piv = b"\x07"
    opt, ct = _device_echo_challenge(request_piv, echo, rnd)
    token = b"\xaa\xbb\xcc\xdd\x00\x00\x00\x07"
    datagram = _serialise_response(1, 0x1234, token, opt, ct)
    # sanity: our serialiser round-trips through the client's parser
    options, _ = _parse_options(datagram, 4 + len(token))
    assert options[9] == opt

    client = CoapClient.__new__(CoapClient)
    client.host = "fd11:9988:7766::1"
    client.port = 5683
    client._pending_requests = []
    client._last_discards = []

    class _Sock:
        def __init__(self):
            self.sent = 0

        def recvfrom(self, _n):
            if self.sent:
                raise socket.timeout()
            self.sent = 1
            return datagram, ("fd11:9988:7766::1", 41963, 0, 0)

        def settimeout(self, _t):
            pass

        def sendto(self, *_a):
            pass

    client._sock = _Sock()
    resp, _inner = client._recv_oscore_response(
        tool, request_piv, 0.2, expected_token=token)
    assert resp is not None
    assert (resp.code_class, resp.code_detail) == (4, 1)
    assert resp.oscore_kid_context == rnd
    assert resp.echo_value == echo or (resp.options or {}).get(252) == echo
    # The configured context was not mutated by the derivation.
    assert tool.id_context == _GROUP_CTX
