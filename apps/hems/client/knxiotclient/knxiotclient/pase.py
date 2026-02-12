import binascii
import hashlib
import hmac
import struct
from typing import Optional
import cbor2
from ecdsa import ellipticcurve, NIST256p


curve = NIST256p.curve

class KNXIoTPaseUtils:
    c = curve
    p = curve.p()
    a = curve.a()
    key_length = 80
    generator = NIST256p.generator
    order = NIST256p.order
    h = curve.cofactor()
    s_crv_M = "02886e2f97ace46e55ba9dd7242579f2993b64e16ef3dcab95afd497333d8fa12f"
    s_crv_N = "03d8bbd6c639c62937b04d997f38c3770719c629d7014d49a24b4f98baa1292b49"

    @staticmethod
    def decode_point(hex_str):
        data = binascii.unhexlify(hex_str)
        prefix = data[0]

        p = curve.p()
        field_len = (p.bit_length() + 7) // 8

        if prefix == 4:
            # Uncompressed point
            if len(data) != 1 + 2 * field_len:
                raise ValueError("Incorrect length for uncompressed encoding")

            x_bytes = data[1:1 + field_len]
            y_bytes = data[1 + field_len:1 + 2 * field_len]

            x = int.from_bytes(x_bytes, "big")
            y = int.from_bytes(y_bytes, "big")

            return ellipticcurve.Point(curve, x, y)

        elif prefix in (2, 3):
            # Compressed point
            x = int.from_bytes(data[1:], "big")

            # y^2 = x^3 + ax + b (mod p)
            alpha = (pow(x, 3, p) + curve.a() * x + curve.b()) % p
            beta = pow(alpha, (p + 1) // 4, p)

            if (beta % 2 == 0 and prefix == 2) or (beta % 2 == 1 and prefix == 3):
                y = beta
            else:
                y = p - beta

            return ellipticcurve.Point(curve, x, y)

        else:
            raise ValueError("Invalid point prefix")

    @staticmethod
    def encode_point_uncompressed(point: ellipticcurve.Point) -> bytes:
        x_bytes = point.x().to_bytes(32, byteorder="big")
        y_bytes = point.y().to_bytes(32, byteorder="big")
        return bytes([4]) + x_bytes + y_bytes

    @staticmethod
    def to_affine(P):
        # Convert PointJacobi → Point; leave Point unchanged
        return P.to_affine() if hasattr(P, "to_affine") else P

    @staticmethod    
    def point_neg(P, p):
        return ellipticcurve.Point(P.curve(), P.x(), (-P.y()) % p)

    @staticmethod
    def compute_pw(password: str) -> bytes:
            password_bytes = password.encode('utf-8')  # UTF-8 bytes
            password_length = len(password_bytes).to_bytes(8, byteorder='little')  # ulong little-endian

            if len(password_bytes) > 255:
                raise ValueError("Password for SPAKE2+ Onboarding is too long!")

            length_id_prover = bytes(8)    # 8 zero bytes
            length_id_verifier = bytes(8)  # 8 zero bytes

            result = password_length + password_bytes + length_id_prover + length_id_verifier
            return result

    @staticmethod
    def derive_key(password_bytes: bytes, salt: bytes, iteration_count: int, num_bytes_requested: int) -> bytes:
        # PBKDF2 with SHA-256
        return hashlib.pbkdf2_hmac('sha256', password_bytes, salt, iteration_count, dklen=num_bytes_requested)

    @staticmethod
    def compute_tt(shareP, shareV, z, v, w0):  
        M = KNXIoTPaseUtils.decode_point(KNXIoTPaseUtils.s_crv_M)
        N = KNXIoTPaseUtils.decode_point(KNXIoTPaseUtils.s_crv_N)
        M_bytes = KNXIoTPaseUtils.encode_point_uncompressed(M)
        N_bytes = KNXIoTPaseUtils.encode_point_uncompressed(N)

        id = "0000000000000000"
        id_bytes = bytes.fromhex(id)

        # Helper: 8-byte little-endian length + value
        def length_and_value(value: bytes) -> bytes:
            return struct.pack("<Q", len(value)) + value

        result = (
            length_and_value(b"knxpase")
            + id_bytes
            + id_bytes
            + length_and_value(M_bytes)
            + length_and_value(N_bytes)
            + length_and_value(shareP)
            + length_and_value(shareV)
            + length_and_value(z)
            + length_and_value(v)
            + length_and_value(w0)
        )

        return result

    @staticmethod
    def compute_main_secret(TT: bytes) -> bytes:
        return hashlib.sha256(TT).digest()

    @staticmethod
    def hkdf_extract(salt: Optional[bytes], ikm: bytes) -> bytes:
        """HKDF-Extract using SHA-256"""
        if salt is None:
            salt = b"\x00" * 32
        return hmac.new(salt, ikm, hashlib.sha256).digest()

    @staticmethod
    def hkdf_expand(prk: bytes, info: bytes, length: int) -> bytes:
        """HKDF-Expand using SHA-256"""
        output = b""
        block = b""
        counter = 1

        while len(output) < length:
            block = hmac.new(
                prk,
                block + info + bytes([counter]),
                hashlib.sha256
            ).digest()
            output += block
            counter += 1

        return output[:length]

    @staticmethod
    def KeyDerivationFunction(key: bytes, salt: Optional[bytes], info_str: str, length: int) -> bytes:
        """Equivalent to your C# KDF wrapper"""
        prk = KNXIoTPaseUtils.hkdf_extract(salt, key)
        info = info_str.encode("utf-8")
        return KNXIoTPaseUtils.hkdf_expand(prk, info, length)

    @staticmethod    
    def compute_shared_secret(kMain: bytes) -> bytes:
        return KNXIoTPaseUtils.KeyDerivationFunction(kMain, None, "SharedKey", 32)

    @staticmethod
    def compute_confirmation_secret(kMain: bytes) -> bytes:
        return KNXIoTPaseUtils.KeyDerivationFunction(kMain, None, "ConfirmationKeys", 64)

    @staticmethod
    def mac(key: bytes, data: bytes) -> bytes:
        return hmac.new(key, data, hashlib.sha256).digest()

