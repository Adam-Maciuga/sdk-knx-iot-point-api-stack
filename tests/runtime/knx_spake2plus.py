"""
SPAKE2+ client for KNX IoT Point API PASE authentication.

Implements the prover (initiator/Party A) side of the SPAKE2+ protocol
as specified in RFC 9383, with KNX IoT specific constants.

Usage:
    handshake = Spake2PlusClient(password="2X4W3TE0DFLLS19Y1FCH")
    # Step 1: send parameter request
    req1 = handshake.create_parameter_request()
    # ... send to server, get response ...
    handshake.process_parameter_response(resp1_cbor)
    # Step 2: send shareP
    req2 = handshake.create_key_exchange_request()
    # ... send to server, get response ...
    handshake.process_key_exchange_response(resp2_cbor)
    # Step 3: send confirmP
    req3 = handshake.create_confirmation_request()
    # ... send to server, get 2.04 ...
    master_secret = handshake.shared_key  # 16 bytes
"""

import hashlib
import hmac
import os
import struct

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.kdf.hkdf import HKDF
from cryptography.hazmat.primitives.kdf.pbkdf2 import PBKDF2HMAC

# P-256 curve order
P256_ORDER = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551

# SPAKE2+ M and N constants (uncompressed P-256 points, 65 bytes each)
M_POINT = bytes.fromhex(
    "04"
    "886e2f97ace46e55ba9dd7242579f2993b64e16ef3dcab95afd497333d8fa12f"
    "5ff355163e43ce224e0b0e65ff02ac8e5c7be09419c785e0ca547d55a12e2d20"
)
N_POINT = bytes.fromhex(
    "04"
    "d8bbd6c639c62937b04d997f38c3770719c629d7014d49a24b4f98baa1292b49"
    "07d60aa6bfade45008a636337f5168c64d9bd36034808cd564490b1e656edbe7"
)

# KNX IoT constants
CONTEXT = "knxpase"
ID_PROVER = ""
ID_VERIFIER = ""


def _encode_uint64_le(val: int) -> bytes:
    return struct.pack("<Q", val)


def _encode_string(s: str) -> bytes:
    b = s.encode("utf-8") if isinstance(s, str) else s
    return _encode_uint64_le(len(b)) + b


def _encode_point(pt: bytes) -> bytes:
    return _encode_uint64_le(len(pt)) + pt


def _encode_w0_mpi(w0: bytes) -> bytes:
    """Encode w0 as MPI: strip leading zero bytes, then length-prefix."""
    stripped = w0.lstrip(b"\x00") or b"\x00"
    return _encode_uint64_le(len(stripped)) + stripped


def _p256_point_from_uncompressed(data: bytes) -> ec.EllipticCurvePublicKey:
    return ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), data)


def _p256_point_to_uncompressed(key: ec.EllipticCurvePublicKey) -> bytes:
    return key.public_bytes(
        serialization.Encoding.X962,
        serialization.PublicFormat.UncompressedPoint
    )


def _p256_scalar_mult(scalar: int, point_bytes: bytes) -> bytes:
    """Multiply a P-256 point by a scalar. Returns uncompressed point."""
    # Parse point
    pt = _p256_point_from_uncompressed(point_bytes)
    nums = pt.public_numbers()

    # Use the curve's field arithmetic via the private key trick:
    # Create a private key with the scalar and compute scalar*G,
    # then use point addition. For arbitrary point multiplication,
    # we need to use the underlying math.
    # For P-256 point multiplication, use ecdh with a crafted private key
    # scalar * Point = ECDH(scalar_as_privkey, Point_as_pubkey)
    # But ECDH only gives x-coordinate. We need the full point.

    # Use the sympy-free approach: reconstruct from affine coordinates
    p = 0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF
    a = 0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFC
    b = 0x5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B

    Px, Py = nums.x, nums.y

    # Double-and-add scalar multiplication on the curve
    Rx, Ry = _ec_scalar_mult(scalar, Px, Py, a, p, P256_ORDER)

    # Encode as uncompressed point
    return b"\x04" + Rx.to_bytes(32, "big") + Ry.to_bytes(32, "big")


def _ec_scalar_mult(k: int, Px: int, Py: int, a: int, p: int, n: int) -> tuple:
    """Scalar multiplication on a Weierstrass curve using double-and-add."""
    k = k % n
    if k == 0:
        raise ValueError("Zero scalar")

    # Point at infinity represented as None
    Rx, Ry = None, None  # identity

    Qx, Qy = Px, Py
    while k > 0:
        if k & 1:
            Rx, Ry = _ec_point_add(Rx, Ry, Qx, Qy, a, p)
        Qx, Qy = _ec_point_double(Qx, Qy, a, p)
        k >>= 1

    return Rx, Ry


def _ec_point_add(Px, Py, Qx, Qy, a: int, p: int):
    """Add two points on a Weierstrass curve."""
    if Px is None:
        return Qx, Qy
    if Qx is None:
        return Px, Py
    if Px == Qx and Py == Qy:
        return _ec_point_double(Px, Py, a, p)
    if Px == Qx:
        return None, None  # point at infinity

    lam = ((Qy - Py) * pow(Qx - Px, -1, p)) % p
    Rx = (lam * lam - Px - Qx) % p
    Ry = (lam * (Px - Rx) - Py) % p
    return Rx, Ry


def _ec_point_double(Px, Py, a: int, p: int):
    """Double a point on a Weierstrass curve."""
    if Px is None:
        return None, None
    if Py == 0:
        return None, None

    lam = ((3 * Px * Px + a) * pow(2 * Py, -1, p)) % p
    Rx = (lam * lam - 2 * Px) % p
    Ry = (lam * (Px - Rx) - Py) % p
    return Rx, Ry


def _p256_point_add(a_bytes: bytes, b_bytes: bytes) -> bytes:
    """Add two P-256 points (uncompressed format)."""
    p = 0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF
    a_coeff = 0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFC

    ax = int.from_bytes(a_bytes[1:33], "big")
    ay = int.from_bytes(a_bytes[33:65], "big")
    bx = int.from_bytes(b_bytes[1:33], "big")
    by = int.from_bytes(b_bytes[33:65], "big")

    rx, ry = _ec_point_add(ax, ay, bx, by, a_coeff, p)
    return b"\x04" + rx.to_bytes(32, "big") + ry.to_bytes(32, "big")


def _p256_point_sub(a_bytes: bytes, b_bytes: bytes) -> bytes:
    """Subtract two P-256 points: a - b = a + (-b)."""
    p = 0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF
    # Negate b: flip y coordinate
    bx = int.from_bytes(b_bytes[1:33], "big")
    by = int.from_bytes(b_bytes[33:65], "big")
    neg_by = (-by) % p
    neg_b = b"\x04" + bx.to_bytes(32, "big") + neg_by.to_bytes(32, "big")
    return _p256_point_add(a_bytes, neg_b)


class Spake2PlusClient:
    """SPAKE2+ prover (initiator) for KNX IoT PASE."""

    def __init__(self, password: str, sender_id: str = "TmpTok"):
        self.password = password.encode("utf-8")
        self.sender_id = sender_id
        self.client_rnd = os.urandom(32)

        # Filled in during the handshake
        self.server_rnd: bytes = b""
        self.salt: bytes = b""
        self.iterations: int = 0
        self.w0: bytes = b""
        self.w1: bytes = b""
        self.x_priv: int = 0
        self.pub_x: bytes = b""
        self.shareP: bytes = b""
        self.shareV: bytes = b""
        self.K_main: bytes = b""
        self.confirmV: bytes = b""
        self.confirmP: bytes = b""
        self.shared_key: bytes = b""

    # ------------------------------------------------------------------
    # Step 1: Parameter exchange
    # ------------------------------------------------------------------

    def create_parameter_request(self) -> dict:
        """Return CBOR map for step 1: {0: id, 15: rnd}."""
        return {0: self.sender_id, 15: self.client_rnd}

    def process_parameter_response(self, resp: dict):
        """Parse server's step 1 response: {15: rnd, 12: {16: it, 5: salt}}."""
        self.server_rnd = resp[15]
        pbkdf2_params = resp[12]
        self.iterations = pbkdf2_params[16]
        self.salt = pbkdf2_params[5]

        # Derive w0 and w1
        self._derive_w0_w1()

    def _derive_w0_w1(self):
        """PBKDF2-HMAC-SHA256 → 80 bytes → w0, w1 (mod P-256 order)."""
        # Build PBKDF2 input: encode(password) || encode(idProver) || encode(idVerifier)
        pbkdf2_input = (
            _encode_uint64_le(len(self.password)) + self.password
            + _encode_string(ID_PROVER)
            + _encode_string(ID_VERIFIER)
        )

        kdf = PBKDF2HMAC(
            algorithm=hashes.SHA256(),
            length=80,
            salt=self.salt,
            iterations=self.iterations,
        )
        output = kdf.derive(pbkdf2_input)

        w0s = output[:40]
        w1s = output[40:80]

        self.w0 = (int.from_bytes(w0s, "big") % P256_ORDER).to_bytes(32, "big")
        self.w1 = (int.from_bytes(w1s, "big") % P256_ORDER).to_bytes(32, "big")

    # ------------------------------------------------------------------
    # Step 2: Key exchange
    # ------------------------------------------------------------------

    def create_key_exchange_request(self) -> dict:
        """Generate ephemeral keypair, compute shareP. Return {10: shareP}."""
        # Generate ephemeral P-256 keypair
        priv_key = ec.generate_private_key(ec.SECP256R1())
        self.x_priv = priv_key.private_numbers().private_value
        self.pub_x = _p256_point_to_uncompressed(priv_key.public_key())

        # shareP = pub_x + w0 * M
        w0_int = int.from_bytes(self.w0, "big")
        w0M = _p256_scalar_mult(w0_int, M_POINT)
        self.shareP = _p256_point_add(self.pub_x, w0M)

        return {10: self.shareP}

    def process_key_exchange_response(self, resp: dict):
        """Parse server's step 2 response: {11: shareV, 13: confirmV}."""
        self.shareV = resp[11]
        self.confirmV = resp[13]

        # Compute Z and V as initiator:
        # Z = x * (shareV - w0 * N)
        # V = w1 * (shareV - w0 * N)
        w0_int = int.from_bytes(self.w0, "big")
        w1_int = int.from_bytes(self.w1, "big")

        w0N = _p256_scalar_mult(w0_int, N_POINT)
        base = _p256_point_sub(self.shareV, w0N)  # shareV - w0*N = pub_y

        Z = _p256_scalar_mult(self.x_priv, base)
        V = _p256_scalar_mult(w1_int, base)

        # Compute transcript hash
        tt = b""
        tt += _encode_string(CONTEXT)
        tt += _encode_string(ID_PROVER)
        tt += _encode_string(ID_VERIFIER)
        tt += _encode_point(M_POINT)
        tt += _encode_point(N_POINT)
        tt += _encode_point(self.shareP)
        tt += _encode_point(self.shareV)
        tt += _encode_point(Z)
        tt += _encode_point(V)
        tt += _encode_w0_mpi(self.w0)

        self.K_main = hashlib.sha256(tt).digest()

        # Derive KcA, KcB
        hkdf = HKDF(
            algorithm=hashes.SHA256(),
            length=64,
            salt=None,
            info=b"ConfirmationKeys",
        )
        kc = hkdf.derive(self.K_main)
        KcA = kc[:32]
        KcB = kc[32:64]

        # Verify confirmV = HMAC-SHA256(KcB, shareP)
        expected_confirmV = hmac.new(KcB, self.shareP, hashlib.sha256).digest()
        if not hmac.compare_digest(self.confirmV, expected_confirmV):
            raise ValueError("SPAKE2+ confirmV verification failed!")

        # Compute confirmP = HMAC-SHA256(KcA, shareV)
        self.confirmP = hmac.new(KcA, self.shareV, hashlib.sha256).digest()

        # Derive shared key (16 bytes for OSCORE master secret)
        hkdf_sk = HKDF(
            algorithm=hashes.SHA256(),
            length=16,
            salt=None,
            info=b"SharedKey",
        )
        self.shared_key = hkdf_sk.derive(self.K_main)

    # ------------------------------------------------------------------
    # Step 3: Confirmation
    # ------------------------------------------------------------------

    def create_confirmation_request(self) -> dict:
        """Return {14: confirmP}."""
        return {14: self.confirmP}
