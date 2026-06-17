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

import struct

import cbor2
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.ciphers.aead import AESCCM
from cryptography.hazmat.primitives.kdf.hkdf import HKDF

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
        id_context if id_context is not None else None,
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

        print(f"[oscore] Context created:"
              f" sender_id={sender_id.hex()}"
              f" recipient_id={recipient_id.hex()}"
              f" ms={master_secret.hex()}")
        print(f"[oscore]   sender_key={self.sender_key.hex()}")
        print(f"[oscore]   recipient_key={self.recipient_key.hex()}")
        print(f"[oscore]   common_iv={self.common_iv.hex()}")

    def _next_piv(self) -> bytes:
        """Get next Partial IV and increment SSN."""
        piv = self.ssn.to_bytes(5, "big").lstrip(b"\x00") or b"\x00"
        self.ssn += 1
        return piv

    def _build_nonce(self, sender_id: bytes, piv: bytes) -> bytes:
        """Construct 13-byte nonce from sender ID, PIV, and common IV.

        Layout (13 bytes):
        [id_len] [padding] [sender_id_right_aligned] [piv_right_aligned(5)]
        XOR with common_iv
        """
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
                        observe: int | None = None) -> tuple[
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

        Returns:
            (oscore_option_value, ciphertext, piv) where:
            - oscore_option_value: bytes to put in CoAP option 9
            - ciphertext: encrypted inner message (becomes outer payload)
            - piv: the partial IV used (needed for response decryption)
        """
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

        # Build OSCORE option (request always includes kid)
        oscore_opt = self._build_oscore_option(piv, kid=kid,
                                               kid_context=self.id_context)

        return oscore_opt, ciphertext, piv

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

        # Response uses recipient's key (server's sender key = our recipient key)
        # Nonce computation depends on the response type:
        # - Echo responses include PIV in the OSCORE option and use the
        #   server's sender ID (= our recipient_id, typically empty) for nonce
        # - Normal responses omit PIV and reuse the request sender's ID
        #   (= our sender_id) for nonce (RFC 8613 §8.3)
        resp_has_own_piv = False
        if oscore_option and len(oscore_option) > 0:
            flags = oscore_option[0]
            piv_len = flags & 0x07
            if piv_len > 0:
                resp_piv = oscore_option[1:1 + piv_len]
                resp_has_own_piv = True

        if resp_has_own_piv:
            # Echo response: nonce uses server's sender ID (our recipient_id)
            nonce = self._build_nonce(self.recipient_id, resp_piv)
        else:
            # Normal response: nonce uses request sender's ID (our sender_id)
            nonce = self._build_nonce(self.sender_id, resp_piv)

        # AAD always uses the request's kid and piv
        aad = self._build_aad(request_kid, request_piv)

        print(f"[oscore] unprotect_response:"
              f" has_piv={resp_has_own_piv}"
              f" piv={resp_piv.hex()}"
              f" nonce={nonce.hex()}"
              f" key={self.recipient_key.hex()}"
              f" ct_len={len(ciphertext)}")

        aesccm = AESCCM(self.recipient_key, tag_length=TAG_LEN)
        plaintext = aesccm.decrypt(nonce, ciphertext, aad)

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

        print(f"[oscore] unprotect_request:"
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
