"""
OSCORE (Object Security for Constrained RESTful Environments) for CoAP.

Implements AES-CCM-16-64-128 encryption/decryption with HKDF-SHA256 key
derivation, matching the KNX IoT stack's OSCORE implementation.

Usage:
    ctx = OscoreContext(master_secret, sender_id=b"TmpTok", recipient_id=b"")
    # Protect a request
    oscore_opt, ciphertext = ctx.protect(coap_code, options_bytes, payload)
    # Parse a protected response
    coap_code, payload = ctx.unprotect(oscore_option_value, ciphertext)
"""

import os
import struct

import cbor2
from cryptography.exceptions import InvalidTag
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.ciphers.aead import AESCCM
from cryptography.hazmat.primitives.kdf.hkdf import HKDF


# Debug print switch. Off by default so it does not spoil test output.
# Enable the [oscore] traces with KNX_TEST_DEBUG_OSCORE=1.
_DEBUG_OSCORE = os.environ.get("KNX_TEST_DEBUG_OSCORE", "0") == "1"


def _dbg_oscore(*args, **kwargs):
    """Print an [oscore] debug line only when KNX_TEST_DEBUG_OSCORE=1."""
    if _DEBUG_OSCORE:
        print(*args, **kwargs)


# OSCORE constants (AES-CCM-16-64-128)
KEY_LEN = 16
TAG_LEN = 8
NONCE_LEN = 13
AEAD_ALG = 10  # COSE algorithm ID for AES-CCM-16-64-128

# CoAP option number for OSCORE
OSCORE_OPTION = 9


def _hkdf_sha256(ikm: bytes, info: bytes, length: int,
                 salt: bytes = b"") -> bytes:
    """HKDF-SHA256 key derivation."""
    hkdf = HKDF(
        algorithm=hashes.SHA256(),
        length=length,
        salt=salt if salt else None,
        info=info,
    )
    return hkdf.derive(ikm)


def _build_info(id_val: bytes, id_context: bytes | None, alg_aead: int,
                type_str: str, length: int) -> bytes:
    """Build CBOR info structure for OSCORE key derivation.

    info = [id, id_context, alg_aead, type, L]
    """
    elements = [
        id_val,
        # EMPTY and ABSENT are the same thing here. The device branches on
        # LENGTH (oc_oscore_context.c: `if (id_ctx_len > 0) encode_byte_string
        # else encode_null`), so both NULL and "" become CBOR null. Testing
        # `is not None` instead sent an empty byte string (0x40) where the
        # device sends null (0xf6) — a different HKDF `info`, and therefore a
        # different key, for any context configured with `contextid=""`.
        # Reachable because provisioning's hex path turns "" into b"".
        # See finding O4.
        id_context if id_context else None,
        alg_aead,
        type_str,
        length,
    ]
    return cbor2.dumps(elements)


class OscoreContext:
    """OSCORE security context derived from a master secret."""

    def __init__(self, master_secret: bytes, sender_id: bytes,
                 recipient_id: bytes = b"",
                 id_context: bytes | None = None,
                 master_salt: bytes = b""):
        self.master_secret = master_secret
        self.sender_id = sender_id
        self.recipient_id = recipient_id
        self.id_context = id_context
        self.ssn = 0  # Sender Sequence Number (partial IV counter)

        # Derive keys
        salt = master_salt  # empty for SPAKE2+ derived contexts
        self._master_salt = salt  # kept for lazy empty-sender-id key derivation

        sender_info = _build_info(sender_id, id_context, AEAD_ALG, "Key",
                                  KEY_LEN)
        self.sender_key = _hkdf_sha256(master_secret, sender_info, KEY_LEN,
                                       salt)

        recipient_info = _build_info(recipient_id, id_context, AEAD_ALG, "Key",
                                     KEY_LEN)
        self.recipient_key = _hkdf_sha256(master_secret, recipient_info,
                                          KEY_LEN, salt)

        iv_info = _build_info(b"", id_context, AEAD_ALG, "IV", NONCE_LEN)
        self.common_iv = _hkdf_sha256(master_secret, iv_info, NONCE_LEN, salt)

        _dbg_oscore(f"[oscore] Context created:"
                    f" sender_id={sender_id.hex()}"
                    f" recipient_id={recipient_id.hex()}"
                    f" ms={master_secret.hex()}")
        _dbg_oscore(f"[oscore]   sender_key={self.sender_key.hex()}")
        _dbg_oscore(f"[oscore]   recipient_key={self.recipient_key.hex()}")
        _dbg_oscore(f"[oscore]   common_iv={self.common_iv.hex()}")

    def with_id_context(self, id_context: bytes,
                        recipient_id: bytes | None = None) -> "OscoreContext":
        """A sibling context: same master secret and salt, derived under a
        different ID Context (and, optionally, peer id).

        This is how the device protects its 4.01 Echo challenge for an
        s-mode MULTICAST write: oc_oscore_engine.c (unicast_echo_response_by_mc)
        generates OC_ECHO_KID_CTX_LEN random bytes and builds a NEW sender
        context from the same access token with `.id_context = rnd` and
        `sender_id = the token's osc id` (oc_oscore_add_sender_context) -
        the kid it then puts in the outer OSCORE option. The receiver has to
        derive the same keys from what that option carries: EITT logs this
        as "SPECIAL DECODING: use received random kid_context" and its
        updated context shows both ids equal to that kid. So `recipient_id`
        should be the kid of the message being unprotected; the script's
        own provisioning may well have left it empty (5.4.1.12's MulticastTx
        has no recipientid, 5.3.17.3's has). The SSN is not copied; a
        derived context is only ever used to unprotect.
        """
        return OscoreContext(master_secret=self.master_secret,
                             sender_id=self.sender_id,
                             recipient_id=(self.recipient_id
                                           if recipient_id is None
                                           else recipient_id),
                             id_context=id_context,
                             master_salt=self._master_salt)

    def _empty_sender_id_key(self) -> bytes:
        """AEAD key derived with an EMPTY sender ID (id="").

        Used to decrypt a device echo/re-protected response, which the
        device signs with its own sender context whose Sender ID is empty
        for a group AT (see oc_oscore_context.c's "Request Key"). When our
        recipient_id is already empty this equals recipient_key, so we reuse
        that; otherwise (group AT) we derive it once and cache it.
        """
        if not self.recipient_id:
            return self.recipient_key
        cached = getattr(self, "_empty_id_key", None)
        if cached is None:
            info = _build_info(b"", self.id_context, AEAD_ALG, "Key", KEY_LEN)
            cached = _hkdf_sha256(self.master_secret, info, KEY_LEN,
                                  self._master_salt)
            self._empty_id_key = cached
        return cached

    def _next_piv(self) -> bytes:
        """Get next Partial IV and increment SSN."""
        piv = self.ssn.to_bytes(5, "big").lstrip(b"\x00") or b"\x00"
        self.ssn += 1
        return piv

    def _reserve_ssn(self, seq: int) -> None:
        """Ensure the counter can never re-emit `seq` under this key.

        The AEAD nonce is a pure function of (sender_id, piv, common_iv), so
        transmitting a hand-picked Partial IV and then letting the ordinary
        counter reach that same value later produces a BYTE-IDENTICAL nonce
        under an unchanged sender_key, on different plaintext. For AES-CCM
        that leaks the plaintext XOR and breaks the tag's forgery
        resistance. Measured before this fix:

            protect_request(seq_no_override=0) -> piv 00, ssn left at 0
            next ordinary send                 -> piv 00 again, same nonce

        The device never allows this: oc_oscore_engine.c increments the SSN
        for every outbound request except a CON retransmission, where the
        plaintext is identical and reuse is therefore safe, and
        oc_oscore_context.c additionally bumps the SSN on restore for the
        same reason (RFC 8613 Appendix B.1.1).

        Reserving rather than clamping keeps the WIRE value untouched — the
        negative tests assert on the exact PIV they asked for — while moving
        the counter past it so no future ordinary send can collide.
        See finding O1.
        """
        if seq >= self.ssn:
            self.ssn = seq + 1

    def _build_nonce(self, sender_id: bytes, piv: bytes) -> bytes:
        """Construct 13-byte nonce from sender ID, PIV, and common IV.

        Layout (13 bytes):
        [id_len] [padding] [sender_id_right_aligned] [piv_right_aligned(5)]
        XOR with common_iv
        """
        # The layout only has room for a 7-byte ID (13 = 1 + 7 + 5), which is
        # also the device's hard cap (OSCORE_SENDER_ID_LEN in
        # messaging/coap/oscore_constants.h — the equivalent memcpy there
        # would underflow the stack buffer). Python does NOT fail on an
        # over-long one: `id_offset` goes negative and slice assignment
        # silently GROWS the bytearray, so a 9-byte ID produced an 18-byte
        # "nonce" and a 12-byte one produced 25. Fail loudly instead — the
        # AEAD only rejects it by luck, and `provisioning._hex_or_ascii`
        # will happily build any length from an XML `senderid=`.
        # See finding O2.
        if len(sender_id) > NONCE_LEN - 5 - 1:
            raise ValueError(
                f"OSCORE sender/recipient ID is {len(sender_id)} bytes; the "
                f"AEAD nonce layout and the device both cap it at "
                f"{NONCE_LEN - 5 - 1}")
        if len(piv) > 5:
            raise ValueError(
                f"OSCORE Partial IV is {len(piv)} bytes; the nonce layout "
                f"caps it at 5")

        nonce = bytearray(NONCE_LEN)
        # PIV right-aligned in last 5 bytes
        piv_offset = NONCE_LEN - len(piv)
        nonce[piv_offset:NONCE_LEN] = piv
        # Sender ID right-aligned before last 5 bytes
        id_offset = NONCE_LEN - 5 - len(sender_id)
        nonce[id_offset:id_offset + len(sender_id)] = sender_id
        # First byte = sender_id length
        nonce[0] = len(sender_id)
        # XOR with common IV
        for i in range(NONCE_LEN):
            nonce[i] ^= self.common_iv[i]
        return bytes(nonce)

    def _build_aad(self, kid: bytes, piv: bytes) -> bytes:
        """Build OSCORE AAD (Enc_structure).

        AAD = ["Encrypt0", b"", aad_array_encoded]
        aad_array = [1, [10], kid, piv, b""]
        """
        aad_array = cbor2.dumps([1, [AEAD_ALG], kid, piv, b""])
        enc_structure = cbor2.dumps(["Encrypt0", b"", aad_array])
        return enc_structure

    def _build_oscore_option(self, piv: bytes, kid: bytes | None = None,
                             kid_context: bytes | None = None) -> bytes:
        """Build OSCORE option value.

        Flags byte:
          bits 0-2: PIV length
          bit 3: kid present
          bit 4: kid_context present
        """
        flags = len(piv) & 0x07
        if kid is not None:
            flags |= 0x08
        if kid_context is not None:
            flags |= 0x10

        opt = bytearray([flags])
        opt.extend(piv)
        if kid_context is not None:
            opt.append(len(kid_context))
            opt.extend(kid_context)
        if kid is not None:
            opt.extend(kid)
        return bytes(opt)

    def protect_request(self, coap_code: int, uri_path: str,
                        payload: bytes = b"",
                        uri_queries: list[str] | None = None,
                        echo: bytes | None = None,
                        accept: int | None = None,
                        content_format: int | None = None,
                        observe: int | None = None,
                        block2: bytes | None = None,
                        seq_no_override: int | None = None,
                        seq_no_offset: int | None = None,
                        invalid_auth_tag: bytes | None = None,
                        kid_override: bytes | None = None) -> tuple[
                            bytes, bytes, bytes]:
        """Protect a CoAP request with OSCORE.

        Args:
            coap_code: Original CoAP code byte (e.g., 1=GET, 2=POST, 3=PUT)
            uri_path: Request URI path (not included in inner message for
                      class U options, but we include it in inner options
                      per the spec for E options)
            payload: Application payload
            observe: Observe option value (0=register, 1=deregister). When
                     set, the Observe option is encoded inside the encrypted
                     inner message (it must also be replicated as an outer
                     Class-U option by the caller).
            block2: Block2 option value (RFC 7959). Block2 is a Class E
                    option, so it travels inside the encrypted inner message
                    and never appears as an outer option.
            seq_no_offset: EITT's SeqNoOffset — this send still CONSUMES
                     the next sequence number (self.ssn advances by 1,
                     exactly like a normal send: EITT keeps a monotonic
                     per-context counter and increments it every telegram),
                     but the Partial IV actually TRANSMITTED is that
                     consumed number shifted by this signed offset. This is
                     what lets 5.3.17.x walk the device's replay window:
                     a +31 lands ahead (accepted, slides the window), a
                     following -1 lands just inside the now-advanced window
                     (a "replay" of an already-covered slot), a -32/-35/-64
                     lands outside the 32-wide window (rejected). Because
                     the counter still advances, consecutive offsets
                     accumulate the same way EITT's do (traces/5_3.xml:
                     SeqNum 103173 then 103142,103143,... = base+31 then
                     base,base+1,...). The device only compares RELATIVE to
                     the highest SSN it has seen (oc_replay.c
                     oc_replay_check_client), so the absolute base is
                     irrelevant — only the offset pattern matters.
            seq_no_override: send this EXACT sequence number as the Partial
                     IV (EITT's SeqNoFn="fix"+SeqNo — pin it to a fixed
                     value so each attempt is reproducible). Unlike
                     seq_no_offset this does NOT consume/advance self.ssn.
            invalid_auth_tag: replace the last len(tag) bytes of the AEAD
                     ciphertext with this value, corrupting the
                     authentication tag so the device MUST reject the
                     message (EITT's InvalidAuthTag integrity-protection
                     negative test).
            kid_override: send this kid in the OSCORE option instead of the
                     context's real sender_id (EITT's RndKid — a random/
                     unknown key id the device won't have a context for).

        Returns:
            (oscore_option_value, ciphertext, piv) where:
            - oscore_option_value: bytes to put in CoAP option 9
            - ciphertext: encrypted inner message (becomes outer payload)
            - piv: the partial IV used (needed for response decryption)
        """
        if seq_no_override is not None:
            # Fixed pin: TRANSMIT exactly this value (the negative tests
            # depend on the precise wire PIV), but still make sure the
            # counter can never re-emit it later under the same key.
            #
            # T6: a negative pin cannot be honoured — "transmit exactly this"
            # and "PIV is an unsigned integer" contradict each other — and
            # this value comes straight from the template's SeqNo=, so
            # clamping it to 0 would silently substitute a DIFFERENT wire
            # value for the one the test names. That is malformed input, not
            # a boundary condition; surface it (cf. F9, values failing open).
            if seq_no_override < 0:
                raise ValueError(
                    f"seq_no_override must be >= 0, got {seq_no_override}; "
                    f"a pinned PIV is transmitted verbatim, so a negative "
                    f"value means the template's SeqNo= was misparsed.")
            seq = seq_no_override
            piv = seq.to_bytes(5, "big").lstrip(b"\x00") or b"\x00"
            self._reserve_ssn(seq)
        elif seq_no_offset is not None:
            # Consume the next SSN normally (advance the counter, exactly
            # like EITT), but TRANSMIT it shifted by the offset. self.ssn
            # is "next value to send", so the number this send consumes is
            # self.ssn; increment the counter, then apply the offset to
            # the consumed value for the wire PIV.
            consumed = self.ssn
            self.ssn += 1
            seq = consumed + seq_no_offset
            if seq < 0:
                # T6: clamping to 0 silently collapses distinct offsets onto
                # the same wire PIV — at ssn 0, offsets -1/-32/-35/-40/-64 all
                # became PIV 0, so "just inside the replay window" and "far
                # outside it" tested the SAME thing and the negative test
                # passed vacuously. 133 of the template's 139 SeqNoOffset uses
                # are negative and exist precisely to land a chosen DISTANCE
                # from the counter, so the distance is the thing under test.
                #
                # Fail loudly instead: this is unreachable today (the large
                # offsets sit at OUT#66+ where ssn is already 65+), so it can
                # only fire if steps are reordered or a case is run in
                # isolation — both of which would otherwise produce a
                # green-but-meaningless result.
                raise ValueError(
                    f"SeqNoOffset {seq_no_offset:+d} at ssn {consumed} would "
                    f"transmit a negative PIV ({seq}). The replay-window test "
                    f"needs the counter to be at least {abs(seq_no_offset)} "
                    f"here; running this TestCase in isolation or reordering "
                    f"it ahead of the traffic that advances the SSN makes the "
                    f"offset meaningless rather than merely wrong.")
            piv = seq.to_bytes(5, "big").lstrip(b"\x00") or b"\x00"
            # Deliberately NO _reserve_ssn() here — the counter advances by
            # exactly one, never to the shifted value.
            #
            # `SeqNoOffset` exists to probe the device's replay WINDOW, so
            # the shift must not disturb our own sequence: 5.3.17.1 sends
            # +31 (a jump the window still accepts) and then continues from
            # where the counter really was. Reserving the shifted value was
            # tried and broke 13 TestCases across 5.3.17-5.3.20 — a +31 at
            # ssn 5 transmits 36, and reserving pushed the counter to 37, so
            # every later send landed outside the device's window and came
            # back 4.01/4.00, cascading through AUTH PREPARATION.
            #
            # Nonce-reuse safety here comes from the offsets themselves: a
            # NEGATIVE offset replays a PIV the context already used, which
            # is the point of the test and is not a live-plaintext collision
            # because the device rejects the message. A POSITIVE offset
            # transmits a value AHEAD of the counter, which the counter will
            # later reach — but the intervening sends are exactly the
            # window-probing traffic the test intends, and the device
            # accepts only one of them.
        else:
            piv = self._next_piv()
        kid = self.sender_id

        # Build inner plaintext: code || inner_options || 0xFF || payload
        # Inner options: Uri-Path (11) and Uri-Query (15) are class E
        plaintext = bytearray()
        plaintext.append(coap_code)

        prev_opt = 0

        # Encode Observe option (option 6) — comes before Uri-Path (11)
        if observe is not None:
            obs_bytes = (b"" if observe == 0
                         else observe.to_bytes(
                             (observe.bit_length() + 7) // 8 or 1, 'big'))
            delta = 6 - prev_opt
            prev_opt = 6
            plaintext.extend(self._encode_option(delta, obs_bytes))

        # Encode Uri-Path options (option 11)
        for part in uri_path.strip("/").split("/"):
            if not part:
                continue
            delta = 11 - prev_opt
            prev_opt = 11
            plaintext.extend(self._encode_option(delta, part.encode()))

        # Encode Content-Format option (option 12) — class E
        if content_format is not None:
            delta = 12 - prev_opt
            prev_opt = 12
            cf_bytes = content_format.to_bytes(
                (content_format.bit_length() + 7) // 8 or 1, 'big')
            plaintext.extend(self._encode_option(delta, cf_bytes))

        # Encode Uri-Query options (option 15)
        if uri_queries:
            for q in uri_queries:
                delta = 15 - prev_opt
                prev_opt = 15
                plaintext.extend(self._encode_option(delta, q.encode()))

        # Encode Accept option (option 17) — class E
        if accept is not None:
            delta = 17 - prev_opt
            prev_opt = 17
            accept_bytes = accept.to_bytes(
                (accept.bit_length() + 7) // 8 or 1, 'big')
            plaintext.extend(self._encode_option(delta, accept_bytes))

        # Encode Block2 option (option 23) -- class E
        if block2 is not None:
            delta = 23 - prev_opt
            prev_opt = 23
            plaintext.extend(self._encode_option(delta, block2))

        # Encode Echo option (option 252) for freshness challenge
        if echo is not None:
            delta = 252 - prev_opt
            prev_opt = 252
            plaintext.extend(self._encode_option(delta, echo))

        if payload:
            plaintext.append(0xFF)
            plaintext.extend(payload)

        # Encrypt
        nonce = self._build_nonce(self.sender_id, piv)
        aad = self._build_aad(kid, piv)

        aesccm = AESCCM(self.sender_key, tag_length=TAG_LEN)
        ciphertext = aesccm.encrypt(nonce, bytes(plaintext), aad)

        # Corrupt the AEAD tag (trailing TAG_LEN bytes) if asked to — the
        # device's AEAD verification must then fail and reject the message.
        if invalid_auth_tag is not None:
            bad = invalid_auth_tag[:TAG_LEN].ljust(TAG_LEN, b"\x00")
            ciphertext = ciphertext[:-TAG_LEN] + bad

        # Build OSCORE option (request always includes kid)
        oscore_opt = self._build_oscore_option(
            piv, kid=(kid_override if kid_override is not None else kid),
            kid_context=self.id_context)

        return oscore_opt, ciphertext, piv

    def protect_echo_challenge(self, request_kid: bytes, request_piv: bytes,
                               echo: bytes,
                               kid_context: bytes,
                               coap_code: int = 0x81) -> tuple[bytes, bytes]:
        """Protect a 4.01-with-Echo challenge answering a telegram the DUT
        sent us — the mirror image of the device's own
        'unicast echo response' (oc_oscore_engine.c:1526).

        We are the RESPONDER here, so the message is signed with the key
        derived for the *responder* direction. The device validates it in
        oc_oscore_receive_message's is_inbound_response branch, which is
        strict about three things (all reflected in EITT's attributes on
        the 4.01 step, and in traces/5_3.xml TelNo 762):

        - `kid` must be the sender id from OUR access token, so the device
          finds the AT entry (oc_core_find_at_entry_by_osc_id); it is NOT
          randomised.
        - `kid_context` must be exactly OC_ECHO_KID_CTX_LEN (10) random
          bytes — that length is the device's marker for "this is an echo
          response" (`coap_pkt->kid_ctx_len == OC_ECHO_KID_CTX_LEN`), and
          it is what EITT's RndKid="yes" randomises on this step.
        - `piv` must be the PIV of the telegram being challenged
          (EITT's SeqNoFn="asinrequest"): oc_oscore_echo_check_and_consume
          looks up (kid, ssn) in the device's echo whitelist, which it
          anchored when it sent that telegram, so any other value is
          dropped as "kid/SSN not in white-list".

        Args:
            request_kid: kid of the telegram we are challenging.
            request_piv: its Partial IV, reused verbatim.
            echo: Echo option value to challenge with.
            kid_context: the 10-byte random echo kid context.
            coap_code: inner response code (default 0x81 = 4.01).

        Returns:
            (oscore_option_value, ciphertext)
        """
        plaintext = bytearray()
        plaintext.append(coap_code)
        # Echo (252) is the only inner option on this message.
        plaintext.extend(self._encode_option(252, echo))

        # A response carrying its OWN PIV is signed with an empty sender id
        # (see unprotect_response's resp_has_own_piv branch, which is the
        # receive side of exactly this construction).
        nonce = self._build_nonce(b"", request_piv)
        aad = self._build_aad(request_kid, request_piv)
        aesccm = AESCCM(self._empty_sender_id_key(), tag_length=TAG_LEN)
        ciphertext = aesccm.encrypt(nonce, bytes(plaintext), aad)

        oscore_opt = self._build_oscore_option(request_piv, kid=request_kid,
                                               kid_context=kid_context)
        return oscore_opt, ciphertext

    def unprotect_response(self, oscore_option: bytes, ciphertext: bytes,
                           request_piv: bytes,
                           request_kid: bytes | None = None) -> tuple[
                               int, bytes]:
        """Unprotect an OSCORE-protected response.

        Args:
            oscore_option: OSCORE option value from the response
            ciphertext: Encrypted payload from the response
            request_piv: PIV from the original request
            request_kid: KID from the original request (defaults to sender_id)

        Returns:
            (coap_code, payload) - the inner CoAP code and decrypted payload
        """
        if request_kid is None:
            request_kid = self.sender_id

        # Parse response OSCORE option to get PIV (if present)
        resp_piv = request_piv

        # A response that carries its own PIV in the OSCORE option is a
        # message the device built fresh (Echo 4.01, observe notification)
        # rather than a piggyback 2.xx that reuses the request's PIV; that
        # distinction drives the AAD below.
        resp_has_own_piv = False
        if oscore_option and len(oscore_option) > 0:
            flags = oscore_option[0]
            piv_len = flags & 0x07
            if piv_len > 0:
                resp_piv = oscore_option[1:1 + piv_len]
                resp_has_own_piv = True

        # A response that carries its own PIV can be one of TWO things, and
        # nothing in the *outer* message distinguishes them — the deciding
        # fact is the inner CoAP code, which is inside the ciphertext we have
        # not decrypted yet:
        #
        #  a) A device-re-protected message (an Echo 4.01, and for a group AT
        #     also the 2.xx that follows). The device built it fresh with ITS
        #     OWN sender context, so nonce and AAD both come from that
        #     context: nonce uses an EMPTY sender ID (device-side a group
        #     AT's sender context has Sender ID "" — oc_oscore_context.c
        #     derives its "Request Key" with id="") and AAD binds the
        #     RESPONSE's PIV. Verified byte-for-byte against `-ServerLogs`
        #     for 5.3.17.x (MulticastTx): with
        #     common_iv=37ec5d919a6b04fa859b1892b9, empty-id + piv=00
        #     reproduces the device nonce exactly, whereas the mirrored
        #     7-byte recipient_id gives 30fc77aad6366a85859b1892b9.
        #
        #  b) An Observe registration response or notification. These also
        #     carry their own PIV — it goes in the nonce and is what binds
        #     the observe sequence (RFC 8613 §4.1.3.5.2) — but they are
        #     signed with the ordinary request-sender construction: nonce
        #     uses our sender_id and AAD reuses the REQUEST's PIV.
        #     test_observe.py's test_observe_notification_aad_binds_request_piv
        #     pins that contract explicitly.
        #
        # Both shapes occur on contexts with an empty recipient_id (PASE's
        # echo challenge and every observe subscription), so no field on the
        # context or the option tells them apart. Try (a) then (b): AEAD
        # authentication IS the discriminator, and a wrong guess fails
        # cleanly with InvalidTag rather than mis-decrypting.
        # Baseline (RFC 8613 §8.3, and what every unicast context needs):
        # a response carrying its own PIV is built with the DEVICE's sender
        # ID — which is our recipient_id, empty for an ordinary unicast or
        # PASE context — while the AAD always reuses the request's kid+PIV
        # and the key is always recipient_key. This covers PASE's Echo 4.01
        # and every Observe registration response/notification (whose own
        # PIV rides in the option to carry the observe sequence but is used
        # for the nonce only).
        if resp_has_own_piv:
            nonce = self._build_nonce(self.recipient_id, resp_piv)
        else:
            nonce = self._build_nonce(self.sender_id, resp_piv)
        aad = self._build_aad(request_kid, request_piv)
        decrypt_key = self.recipient_key

        # GROUP AT alternative. Device-side, a group AT's sender context has
        # Sender ID "" (oc_oscore_context.c derives its "Request Key" with
        # id="") even though we provisioned a NON-empty recipient_id for the
        # same AT, so nonce, AAD PIV and key all differ from the baseline.
        # Verified byte-for-byte against `-ServerLogs` for 5.3.17.x
        # (MulticastTx): with common_iv=37ec5d919a6b04fa859b1892b9, empty-id
        # + piv=00 reproduces the device nonce exactly, whereas the mirrored
        # 7-byte recipient_id gives 30fc77aad6366a85859b1892b9.
        #
        # BOTH messages the device sends on a group AT — the Echo 4.01 AND
        # the 2.xx that follows the retry — are signed with that empty-id
        # sender context, so the key alternative applies whether or not the
        # response carries its own PIV; only the nonce/AAD differ between
        # those two shapes.
        #
        # Selecting this by inspecting the context alone is not possible: the
        # multicast echo-challenge context test_group_comm.py builds per
        # response has the SAME shape (non-empty recipient_id, response with
        # its own PIV) yet needs the baseline, because it is keyed on the
        # random echo kid_ctx the device generated rather than the group's
        # own context id. `id_context` length separates them, and not by
        # coincidence: the device marks an echo response with a kid_ctx of
        # exactly OC_ECHO_KID_CTX_LEN (10) bytes — oc_oscore_engine.c keys
        # its whole is_inbound_response branch on
        # `kid_ctx_len == OC_ECHO_KID_CTX_LEN` — while a provisioned group
        # context id is shorter. Order the candidates by that signal and keep
        # the other as a fallback: AEAD authentication is the final arbiter,
        # and a wrong guess fails cleanly with InvalidTag rather than
        # mis-decrypting.
        #
        # (Gating on resp_has_own_piv alone regressed 22 observe/group-comm
        # tests; on recipient_id alone, 7 group-comm tests; baseline-first
        # unconditionally, 4 of 5.3. Hence: order, then fall back.)
        attempts = [(nonce, aad, decrypt_key)]
        if self.recipient_id:
            group_key = self._empty_sender_id_key()
            if resp_has_own_piv:
                group_attempt = (self._build_nonce(b"", resp_piv),
                                 self._build_aad(request_kid, resp_piv),
                                 group_key)
            else:
                # Follow-up 2.xx: same nonce/AAD as the baseline, but the
                # device signed it with its empty-id sender key.
                group_attempt = (nonce, aad, group_key)
            is_echo_ctx = (self.id_context is not None
                           and len(self.id_context) == 10)
            attempts = ([*attempts, group_attempt] if is_echo_ctx
                        else [group_attempt, *attempts])

        plaintext = None
        last_error: Exception | None = None
        for nonce, aad, key in attempts:
            _dbg_oscore(f"[oscore] unprotect_response:"
                        f" has_piv={resp_has_own_piv}"
                        f" piv={resp_piv.hex()}"
                        f" nonce={nonce.hex()}"
                        f" aad={aad.hex()}"
                        f" key={key.hex()}"
                        f" ct_len={len(ciphertext)}")
            try:
                plaintext = AESCCM(key, tag_length=TAG_LEN).decrypt(
                    nonce, ciphertext, aad)
                break
            except InvalidTag as exc:
                # ONLY an authentication failure means "wrong candidate,
                # try the next one". Anything else — a malformed nonce
                # length, a bad key size, a type error — is OUR bug, and
                # swallowing it here would report it as "the device sent
                # something we could not decrypt", sending the next reader
                # after a device fault that does not exist. See finding O3.
                last_error = exc
        if plaintext is None:
            raise last_error

        # Parse inner plaintext: code || inner_options || [0xFF || payload]
        inner_code = plaintext[0]
        # Parse options and find payload
        payload = b""
        inner_options = {}
        i = 1
        opt_num = 0
        while i < len(plaintext):
            if plaintext[i] == 0xFF:
                payload = plaintext[i + 1:]
                break
            # Parse option
            byte = plaintext[i]
            delta = (byte >> 4) & 0x0F
            length = byte & 0x0F
            i += 1
            if delta == 13:
                delta = plaintext[i] + 13
                i += 1
            elif delta == 14:
                delta = struct.unpack("!H", plaintext[i:i + 2])[0] + 269
                i += 2
            if length == 13:
                length = plaintext[i] + 13
                i += 1
            elif length == 14:
                length = struct.unpack("!H", plaintext[i:i + 2])[0] + 269
                i += 2
            opt_num += delta
            inner_options[opt_num] = plaintext[i:i + length]
            i += length

        return inner_code, payload, inner_options

    def unprotect_request(self, oscore_option: bytes,
                          ciphertext: bytes) -> tuple[
                              int, bytes, dict, bytes, bytes,
                              bytes | None]:
        """Unprotect an incoming OSCORE-protected request.

        Used to decrypt multicast group messages or any inbound request
        where the remote party is the sender and we are the receiver.

        Uses recipient_key (= remote sender's key) for decryption and
        recipient_id (= remote sender's ID) for nonce computation.

        Args:
            oscore_option: OSCORE option value from the incoming message.
            ciphertext: Encrypted payload.

        Returns:
            (inner_code, payload, inner_options, piv, kid, kid_context)
        """
        # Parse OSCORE option
        flags = oscore_option[0]
        piv_len = flags & 0x07
        has_kid = bool(flags & 0x08)
        has_kid_ctx = bool(flags & 0x10)

        offset = 1
        piv = oscore_option[offset:offset + piv_len]
        offset += piv_len

        kid_context = None
        if has_kid_ctx:
            kid_ctx_len = oscore_option[offset]
            offset += 1
            kid_context = oscore_option[offset:offset + kid_ctx_len]
            offset += kid_ctx_len

        kid = oscore_option[offset:] if has_kid else b""

        # Decrypt: nonce uses recipient_id (= remote sender's ID)
        nonce = self._build_nonce(self.recipient_id, piv)
        aad = self._build_aad(kid, piv)

        _dbg_oscore(f"[oscore] unprotect_request:"
                    f" kid={kid.hex()}"
                    f" piv={piv.hex()}"
                    f" kid_ctx={kid_context.hex() if kid_context else 'none'}"
                    f" nonce={nonce.hex()}"
                    f" key={self.recipient_key.hex()}"
                    f" ct_len={len(ciphertext)}")

        aesccm = AESCCM(self.recipient_key, tag_length=TAG_LEN)
        plaintext = aesccm.decrypt(nonce, ciphertext, aad)

        # Parse inner plaintext: code || options || [0xFF || payload]
        inner_code = plaintext[0]
        payload = b""
        inner_options = {}
        i = 1
        opt_num = 0
        while i < len(plaintext):
            if plaintext[i] == 0xFF:
                payload = plaintext[i + 1:]
                break
            byte = plaintext[i]
            delta = (byte >> 4) & 0x0F
            length = byte & 0x0F
            i += 1
            if delta == 13:
                delta = plaintext[i] + 13
                i += 1
            elif delta == 14:
                delta = struct.unpack("!H",
                                     plaintext[i:i + 2])[0] + 269
                i += 2
            if length == 13:
                length = plaintext[i] + 13
                i += 1
            elif length == 14:
                length = struct.unpack("!H",
                                     plaintext[i:i + 2])[0] + 269
                i += 2
            opt_num += delta
            inner_options[opt_num] = plaintext[i:i + length]
            i += length

        return inner_code, payload, inner_options, piv, kid, kid_context

    @staticmethod
    def _encode_option(delta: int, value: bytes) -> bytes:
        """Encode a single CoAP option (delta, length, value)."""
        length = len(value)
        if delta < 13:
            d = delta
            d_ext = b""
        elif delta < 269:
            d = 13
            d_ext = struct.pack("B", delta - 13)
        else:
            d = 14
            d_ext = struct.pack("!H", delta - 269)

        if length < 13:
            ln = length
            l_ext = b""
        elif length < 269:
            ln = 13
            l_ext = struct.pack("B", length - 13)
        else:
            ln = 14
            l_ext = struct.pack("!H", length - 269)

        return struct.pack("B", (d << 4) | ln) + d_ext + l_ext + value
