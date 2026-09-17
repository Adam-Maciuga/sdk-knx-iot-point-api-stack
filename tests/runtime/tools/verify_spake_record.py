"""
End-to-end verification of the precalculated SPAKE2+ record.

Drives the real Spake2PlusClient (prover, as used by the runtime tests) against
a verifier that uses ONLY the stored registration record (w0, L) - never the
password and never w1. If the client's internal confirmV check passes and both
sides derive the same shared key, the stored L is proven interchangeable with
the password-derived w1.
"""

import hashlib
import hmac

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.kdf.hkdf import HKDF

from knx_spake2plus import (
    M_POINT,
    N_POINT,
    P256_ORDER,
    Spake2PlusClient,
    _encode_point,
    _encode_string,
    _encode_w0_mpi,
    _p256_point_add,
    _p256_point_sub,
    _p256_scalar_mult,
    CONTEXT,
    ID_PROVER,
    ID_VERIFIER,
)
from gen_spake_record import (
    G_POINT,
    ITERATIONS,
    PASSWORD,
    SALT,
    _derive_record,
)


def main():
    w0, w1, L = _derive_record(PASSWORD, SALT, ITERATIONS)
    w0_int = int.from_bytes(w0, "big")

    # --- Prover side: the real client used by the runtime tests ---------------
    client = Spake2PlusClient(password=PASSWORD)
    client.process_parameter_response({15: b"\x00" * 32, 12: {16: ITERATIONS, 5: SALT}})
    assert client.w0 == w0, "client w0 differs from record w0"
    req2 = client.create_key_exchange_request()
    shareP = req2[10]

    # --- Verifier side: uses ONLY the stored record (w0, L), never w1 ---------
    y_priv = 0x00AABBCCDDEEFF00112233445566778899AABBCCDDEEFF00112233445566778899 % P256_ORDER
    pub_y = _p256_scalar_mult(y_priv, G_POINT)
    shareV = _p256_point_add(pub_y, _p256_scalar_mult(w0_int, N_POINT))

    base_v = _p256_point_sub(shareP, _p256_scalar_mult(w0_int, M_POINT))  # = pub_x
    Z_v = _p256_scalar_mult(y_priv, base_v)
    V_v = _p256_scalar_mult(y_priv, L)  # stored L stands in for w1

    tt = b""
    tt += _encode_string(CONTEXT)
    tt += _encode_string(ID_PROVER)
    tt += _encode_string(ID_VERIFIER)
    tt += _encode_point(M_POINT)
    tt += _encode_point(N_POINT)
    tt += _encode_point(shareP)
    tt += _encode_point(shareV)
    tt += _encode_point(Z_v)
    tt += _encode_point(V_v)
    tt += _encode_w0_mpi(w0)
    k_main_v = hashlib.sha256(tt).digest()

    kc = HKDF(algorithm=hashes.SHA256(), length=64, salt=None, info=b"ConfirmationKeys").derive(k_main_v)
    KcB = kc[32:64]
    confirmV = hmac.new(KcB, shareP, hashlib.sha256).digest()

    # --- Feed verifier output into the real client and let it validate --------
    client.process_key_exchange_response({11: shareV, 13: confirmV})
    client_req3 = client.create_confirmation_request()

    sk_v = HKDF(algorithm=hashes.SHA256(), length=16, salt=None, info=b"SharedKey").derive(k_main_v)
    assert client.shared_key == sk_v, "shared key mismatch"

    print("PASS: real Spake2PlusClient handshake succeeded using stored L only.")
    print(f"  K_main      = {k_main_v.hex()}")
    print(f"  shared_key  = {client.shared_key.hex()}")
    print(f"  confirmP    = {client_req3[14].hex()}")


if __name__ == "__main__":
    main()
