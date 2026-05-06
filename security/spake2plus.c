/*
 * Copyright (c) 2022 Cascoda Ltd.
 * Copyright (c) 2024-2026 KNX Association
 * Copyright (c) 2026 Alexander Burker
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Note:
 * spake2plus.c uses private TF-PSA-Crypto ECP/BigNum headers for raw P-256
 * point arithmetic (ecp_mul, ecp_muladd) and modular reduction, which have no
 * PSA equivalent in Mbed TLS 4.x (see Mbed TLS issue #9378 and
 * docs/mbedtls_3.x_to_4.x_migration.md, section "SPAKE2+ Migration Design").
 *
 * All other operations (PBKDF2, HKDF, HMAC, SHA-256, key generation) use the
 * public PSA Crypto API.
 *
 * MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS unlocks the private struct definitions
 * (mbedtls_mpi, mbedtls_ecp_group, mbedtls_ecp_point) in the private headers.
 * This is the same mechanism used by Mbed TLS internally. External code should
 * not normally define this — here it is required because there is no PSA
 * replacement for the raw ECC arithmetic we need.
 */
#define MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS

#include <stdlib.h>
#include <string.h>

#include "psa/crypto.h"
#include "mbedtls/psa_util.h"         /* mbedtls_psa_get_random, MBEDTLS_PSA_RANDOM_STATE */
#include "mbedtls/private/bignum.h"
#include "mbedtls/private/ecp.h"

#include "spake2plus.h"
#include "port/oc_log.h"

/* clang-format off */
/*
 * M and N are the fixed SPAKE2+ P-256 base points from the specification.
 * mbedTLS cannot decode compressed points directly, so these are stored as
 * uncompressed 65-byte arrays (0x04 || X || Y).
 * Generated from the compressed-point spec values using the Python
 * `cryptography` module:
 *   M = ec.EllipticCurvePublicKey.from_encoded_point(curve(), (0x02886...).to_bytes(33, 'big'))
 *   N = ec.EllipticCurvePublicKey.from_encoded_point(curve(), (0x03d8b...).to_bytes(33, 'big'))
 *   M.public_bytes(Encoding.X962, PublicFormat.UncompressedPoint).hex()
 *   N.public_bytes(Encoding.X962, PublicFormat.UncompressedPoint).hex()
 */
static const uint8_t bytes_M[] = {
  0x04, 0x88, 0x6e, 0x2f, 0x97, 0xac, 0xe4, 0x6e, 0x55, 0xba, 0x9d, 0xd7, 0x24,
  0x25, 0x79, 0xf2, 0x99, 0x3b, 0x64, 0xe1, 0x6e, 0xf3, 0xdc, 0xab, 0x95, 0xaf,
  0xd4, 0x97, 0x33, 0x3d, 0x8f, 0xa1, 0x2f, 0x5f, 0xf3, 0x55, 0x16, 0x3e, 0x43,
  0xce, 0x22, 0x4e, 0x0b, 0x0e, 0x65, 0xff, 0x02, 0xac, 0x8e, 0x5c, 0x7b, 0xe0,
  0x94, 0x19, 0xc7, 0x85, 0xe0, 0xca, 0x54, 0x7d, 0x55, 0xa1, 0x2e, 0x2d, 0x20
};
static const uint8_t bytes_N[] = {
  0x04, 0xd8, 0xbb, 0xd6, 0xc6, 0x39, 0xc6, 0x29, 0x37, 0xb0, 0x4d, 0x99, 0x7f,
  0x38, 0xc3, 0x77, 0x07, 0x19, 0xc6, 0x29, 0xd7, 0x01, 0x4d, 0x49, 0xa2, 0x4b,
  0x4f, 0x98, 0xba, 0xa1, 0x29, 0x2b, 0x49, 0x07, 0xd6, 0x0a, 0xa6, 0xbf, 0xad,
  0xe4, 0x50, 0x08, 0xa6, 0x36, 0x33, 0x7f, 0x51, 0x68, 0xc6, 0x4d, 0x9b, 0xd3,
  0x60, 0x34, 0x80, 0x8c, 0xd5, 0x64, 0x49, 0x0b, 0x1e, 0x65, 0x6e, 0xdb, 0xe7
};
/* clang-format on */

/* P-256 group used for all private ECP operations. */
static mbedtls_ecp_group grp;

/* ── Public encoding helpers ─────────────────────────────────────────────── */

size_t encode_uint(const uint64_t value, uint8_t *buf)
{
  buf[0] = (uint8_t)(value >> 0) & 0xff;
  buf[1] = (uint8_t)(value >> 8) & 0xff;
  buf[2] = (uint8_t)(value >> 16) & 0xff;
  buf[3] = (uint8_t)(value >> 24) & 0xff;
  buf[4] = (uint8_t)(value >> 32) & 0xff;
  buf[5] = (uint8_t)(value >> 40) & 0xff;
  buf[6] = (uint8_t)(value >> 48) & 0xff;
  buf[7] = (uint8_t)(value >> 56) & 0xff;
  return 8;
}

size_t encode_string(const char *str, uint8_t *buf)
{
  size_t slen = strlen(str);
  size_t len = encode_uint((uint64_t)slen, buf);
  memcpy(buf + len, str, slen);
  return len + slen;
}

size_t encode_point(const uint8_t *point_bytes, size_t len, uint8_t *buf)
{
  size_t len_len = encode_uint((uint64_t)len, buf);
  memcpy(buf + len_len, point_bytes, len);
  return len_len + len;
}

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/*
 * Load an uncompressed P-256 point from a 65-byte buffer into an ecp_point.
 * Returns 0 on success, MBEDTLS error code on failure.
 */
static int ecp_point_from_bytes(mbedtls_ecp_point *P, const uint8_t buf[kPubKeySize])
{
  return mbedtls_ecp_point_read_binary(&grp, P, buf, kPubKeySize);
}

/*
 * Write an ecp_point as an uncompressed P-256 point into a 65-byte buffer.
 * Returns 0 on success, MBEDTLS error code on failure.
 */
static int ecp_point_to_bytes(const mbedtls_ecp_point *P, uint8_t buf[kPubKeySize])
{
  size_t olen = 0;
  return mbedtls_ecp_point_write_binary(&grp, P, MBEDTLS_ECP_PF_UNCOMPRESSED, &olen, buf, kPubKeySize);
}

/*
 * Reduce a big-endian input of `input_len` bytes modulo the P-256 group order
 * N, writing the result as a 32-byte big-endian integer into `out`.
 * Returns 0 on success, MBEDTLS error code on failure.
 */
static int mpi_mod_N(const uint8_t *input, size_t input_len, uint8_t out[32])
{
  int ret;
  mbedtls_mpi val;
  mbedtls_mpi_init(&val);

  MBEDTLS_MPI_CHK(mbedtls_mpi_read_binary(&val, input, input_len));
  MBEDTLS_MPI_CHK(mbedtls_mpi_mod_mpi(&val, &val, &grp.N));
  MBEDTLS_MPI_CHK(mbedtls_mpi_write_binary(&val, out, 32));

cleanup:
  mbedtls_mpi_free(&val);
  return ret;
}

/*
 * Compute J = f * (K + (-g) * L) using private ECP arithmetic, where f and g
 * are big-endian 32-byte scalars and K, L are uncompressed 65-byte points.
 * Output J is written as an uncompressed 65-byte point.
 * Returns 0 on success.
 */
static int calculate_JfKgL(uint8_t J_out[kPubKeySize],
        const uint8_t f[32],
        const uint8_t K_bytes[kPubKeySize],
        const uint8_t g[32],
        const uint8_t L_bytes[kPubKeySize])
{
  int ret;
  mbedtls_mpi f_mpi, neg_g, zero, one;
  mbedtls_ecp_point K_pt, L_pt, K_minus_gL, J_pt;

  mbedtls_mpi_init(&f_mpi);
  mbedtls_mpi_init(&neg_g);
  mbedtls_mpi_init(&zero);
  mbedtls_mpi_init(&one);
  mbedtls_ecp_point_init(&K_pt);
  mbedtls_ecp_point_init(&L_pt);
  mbedtls_ecp_point_init(&K_minus_gL);
  mbedtls_ecp_point_init(&J_pt);

  MBEDTLS_MPI_CHK(mbedtls_mpi_read_binary(&f_mpi, f, 32));
  MBEDTLS_MPI_CHK(mbedtls_mpi_read_binary(&neg_g, g, 32));
  MBEDTLS_MPI_CHK(ecp_point_from_bytes(&K_pt, K_bytes));
  MBEDTLS_MPI_CHK(ecp_point_from_bytes(&L_pt, L_bytes));

  /* neg_g = -g mod N = N - g */
  MBEDTLS_MPI_CHK(mbedtls_mpi_read_string(&zero, 10, "0"));
  MBEDTLS_MPI_CHK(mbedtls_mpi_sub_mpi(&neg_g, &zero, &neg_g));
  MBEDTLS_MPI_CHK(mbedtls_mpi_mod_mpi(&neg_g, &neg_g, &grp.N));

  /* K_minus_gL = 1*K + (-g)*L = K - g*L */
  MBEDTLS_MPI_CHK(mbedtls_mpi_read_string(&one, 10, "1"));
  MBEDTLS_MPI_CHK(mbedtls_ecp_muladd(&grp, &K_minus_gL, &one, &K_pt, &neg_g, &L_pt));

  /* J = f * K_minus_gL */
  MBEDTLS_MPI_CHK(mbedtls_ecp_mul(&grp, &J_pt, &f_mpi, &K_minus_gL,
                                   mbedtls_psa_get_random, MBEDTLS_PSA_RANDOM_STATE));

  MBEDTLS_MPI_CHK(ecp_point_to_bytes(&J_pt, J_out));

cleanup:
  mbedtls_mpi_free(&f_mpi);
  mbedtls_mpi_free(&neg_g);
  mbedtls_mpi_free(&zero);
  mbedtls_mpi_free(&one);
  mbedtls_ecp_point_free(&K_pt);
  mbedtls_ecp_point_free(&L_pt);
  mbedtls_ecp_point_free(&K_minus_gL);
  mbedtls_ecp_point_free(&J_pt);
  return ret;
}

/*
 * Compute pX = pubX + wX * L_bytes, writing the result as an uncompressed
 * 65-byte point into pX_out. L_bytes is a constant (bytes_M or bytes_N).
 * Returns 0 on success.
 */
static int calculate_pX(uint8_t pX_out[kPubKeySize],
        const uint8_t pubX[kPubKeySize],
        const uint8_t wX[32],
        const uint8_t L_bytes[kPubKeySize])
{
  int ret;
  mbedtls_mpi one, wX_mpi;
  mbedtls_ecp_point pubX_pt, L_pt, pX_pt;

  mbedtls_mpi_init(&one);
  mbedtls_mpi_init(&wX_mpi);
  mbedtls_ecp_point_init(&pubX_pt);
  mbedtls_ecp_point_init(&L_pt);
  mbedtls_ecp_point_init(&pX_pt);

  MBEDTLS_MPI_CHK(ecp_point_from_bytes(&pubX_pt, pubX));
  MBEDTLS_MPI_CHK(ecp_point_from_bytes(&L_pt, L_bytes));
  MBEDTLS_MPI_CHK(mbedtls_mpi_read_binary(&wX_mpi, wX, 32));
  MBEDTLS_MPI_CHK(mbedtls_mpi_read_string(&one, 10, "1"));

  /* pX = 1*pubX + wX*L */
  MBEDTLS_MPI_CHK(mbedtls_ecp_muladd(&grp, &pX_pt, &one, &pubX_pt, &wX_mpi, &L_pt));
  MBEDTLS_MPI_CHK(ecp_point_to_bytes(&pX_pt, pX_out));

cleanup:
  mbedtls_mpi_free(&one);
  mbedtls_mpi_free(&wX_mpi);
  mbedtls_ecp_point_free(&pubX_pt);
  mbedtls_ecp_point_free(&L_pt);
  mbedtls_ecp_point_free(&pX_pt);
  return ret;
}

/*
 * Compute Z = h * x * (Y - w0*N) and similarly V = h * w1 * (Y - w0*N) by
 * calling calculate_JfKgL with the appropriate scalars. For P-256 h = 1.
 * Y_bytes is the remote public share (shareV for initiator, shareP for
 * responder). L_bytes is bytes_N or bytes_M respectively.
 */
static int calculate_ZV(uint8_t ZV_out[kPubKeySize],
        const uint8_t x[32],
        const uint8_t Y_bytes[kPubKeySize],
        const uint8_t w0[32],
        const uint8_t L_bytes[kPubKeySize])
{
  /* J = x * (Y - w0*L) */
  return calculate_JfKgL(ZV_out, x, Y_bytes, w0, L_bytes);
}

/*
 * Encode w0 (big-endian 32-byte scalar) with a leading uint64 length prefix,
 * stripping leading zero bytes to match the mbedtls_mpi_size() behaviour used
 * in the 3.x implementation. This keeps transcript compatibility.
 */
static size_t encode_w0_mpi(const uint8_t w0[32], uint8_t *buffer)
{
  /* Strip leading zero bytes (same as mbedtls_mpi_size / mbedtls_mpi_write_binary). */
  size_t start = 0;
  while (start < 32 && w0[start] == 0) {
    start++;
  }

  size_t len = 32 - start;
  size_t len_len = encode_uint((uint64_t)len, buffer);
  memcpy(buffer + len_len, w0 + start, len);
  return len_len + len;
}

/* ── SPAKE2+ core ────────────────────────────────────────────────────────── */

int spake2plus_init(void)
{
  mbedtls_ecp_group_init(&grp);
  int ret = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
  if (ret != 0) {
    OC_ERR("Failed to load P-256 group (ret %d)!", ret);
  }

  return ret;
}

int spake2plus_free(void)
{
  mbedtls_ecp_group_free(&grp);
  return 0;
}

int spake2plus_parameter_exchange(uint8_t *rand, size_t rand_length, uint8_t *salt, size_t salt_length)
{
  psa_status_t status = psa_generate_random(rand, rand_length);
  if (status != PSA_SUCCESS) {
    OC_ERR("Failed to generate SPAKE2+ random nonce (status %d)!", (int)status);
    return (int)status;
  }

  status = psa_generate_random(salt, salt_length);
  if (status != PSA_SUCCESS) {
    OC_ERR("Failed to generate SPAKE2+ salt (status %d)!", (int)status);
  }

  return (int)status;
}

/*
 * Compute w0 and w1 from the device password via PBKDF2-HMAC-SHA256.
 * Outputs are 32-byte big-endian scalars reduced modulo the P-256 order N.
 */
static int spake2plus_calc_w0_w1(const uint8_t *password, size_t password_length,
        const uint8_t *salt, size_t salt_length,
        uint32_t it, 
        const char *id_prover, const char *id_verifier,
        uint8_t w0[32], uint8_t w1[32])
{
  /* PBKDF2 output: w0s (40 bytes) || w1s (40 bytes).
   * RFC 9383 requires at least 40 bytes per scalar for P-256 to provide
   * sufficient security margin above the 32-byte curve order.
   */
#define PBKDF2_OUTPUT_LEN 80
  uint8_t output[PBKDF2_OUTPUT_LEN];

  /*
   * SPAKE2+ PBKDF2 input is encoded as:
   *   encode(password) || encode(idProver) || encode(idVerifier)
   * using the little-endian uint64 length-prefix format (RFC 9383).
   */
  size_t id_prover_length = strlen(id_prover);
  size_t id_verifier_length = strlen(id_verifier);
  uint8_t *input = malloc(3 * sizeof(uint64_t) + password_length + id_prover_length + id_verifier_length);
  if (!input) {
    return (int)PSA_ERROR_INSUFFICIENT_MEMORY;
  }

  size_t input_length = 0;
  input_length += encode_uint((uint64_t)password_length, input + input_length);
  memcpy(input + input_length, password, password_length);
  input_length += password_length;
  input_length += encode_string(id_prover, input + input_length);
  input_length += encode_string(id_verifier, input + input_length);

  /* Import the PBKDF2 password as a derivation key. */
  psa_key_attributes_t attrs = PSA_KEY_ATTRIBUTES_INIT;
  psa_set_key_type(&attrs, PSA_KEY_TYPE_DERIVE);
  psa_set_key_usage_flags(&attrs, PSA_KEY_USAGE_DERIVE);
  psa_set_key_algorithm(&attrs, PSA_ALG_PBKDF2_HMAC(PSA_ALG_SHA_256));

  psa_key_id_t pw_key_id = PSA_KEY_ID_NULL;
  psa_status_t status = psa_import_key(&attrs, input, input_length, &pw_key_id);
  free(input);
  if (status != PSA_SUCCESS) {
    return (int)status;
  }

  /* Run PBKDF2-HMAC-SHA256. */
  psa_key_derivation_operation_t op = PSA_KEY_DERIVATION_OPERATION_INIT;
  status = psa_key_derivation_setup(&op, PSA_ALG_PBKDF2_HMAC(PSA_ALG_SHA_256));
  if (status != PSA_SUCCESS) {
    goto cleanup_key;
  }

  status = psa_key_derivation_input_integer(&op, PSA_KEY_DERIVATION_INPUT_COST, (uint64_t)it);
  if (status != PSA_SUCCESS) {
    goto cleanup_op;
  }

  status = psa_key_derivation_input_bytes(&op, PSA_KEY_DERIVATION_INPUT_SALT, salt, salt_length);
  if (status != PSA_SUCCESS) {
    goto cleanup_op;
  }

  status = psa_key_derivation_input_key(&op, PSA_KEY_DERIVATION_INPUT_PASSWORD, pw_key_id);
  if (status != PSA_SUCCESS) {
    goto cleanup_op;
  }

  status = psa_key_derivation_output_bytes(&op, output, PBKDF2_OUTPUT_LEN);
  if (status != PSA_SUCCESS) {
    goto cleanup_op;
  }

  /* Reduce w0s and w1s modulo the P-256 group order N. */
  int ret;
  ret = mpi_mod_N(output, PBKDF2_OUTPUT_LEN / 2, w0);
  if (ret != 0) {
    status = PSA_ERROR_GENERIC_ERROR;
    goto cleanup_op;
  }

  ret = mpi_mod_N(output + PBKDF2_OUTPUT_LEN / 2, PBKDF2_OUTPUT_LEN / 2, w1);
  if (ret != 0) {
    status = PSA_ERROR_GENERIC_ERROR;
  }

cleanup_op:
  psa_key_derivation_abort(&op);

cleanup_key:
  psa_destroy_key(pw_key_id);
  return (int)status;
}

/*
 * Compute w0, w1 and L = w1*G from the device password.
 */
static int spake2plus_calc_w0_L(const uint8_t *password, size_t password_length,
        const uint8_t *salt, size_t salt_length,
        uint32_t it, const char *id_prover,
        const char *id_verifier,
        uint8_t w0[32], uint8_t L[kPubKeySize])
{
  uint8_t w1[32];
  int ret = spake2plus_calc_w0_w1(password, password_length, salt, salt_length, 
                    it, id_prover, id_verifier, w0, w1);
  if (ret != 0) {
    return ret;
  }

  /* L = w1 * G */
  mbedtls_mpi w1_mpi;
  mbedtls_ecp_point L_pt;
  mbedtls_mpi_init(&w1_mpi);
  mbedtls_ecp_point_init(&L_pt);

  MBEDTLS_MPI_CHK(mbedtls_mpi_read_binary(&w1_mpi, w1, 32));
  MBEDTLS_MPI_CHK(mbedtls_ecp_mul(&grp, &L_pt, &w1_mpi, &grp.G,
         mbedtls_psa_get_random, MBEDTLS_PSA_RANDOM_STATE));
  MBEDTLS_MPI_CHK(ecp_point_to_bytes(&L_pt, L));

cleanup:
  mbedtls_mpi_free(&w1_mpi);
  mbedtls_ecp_point_free(&L_pt);
  return ret;
}

int spake2plus_get_w0_L_params(const uint8_t *password, size_t password_length,
        const uint8_t *salt, size_t salt_length, uint32_t it,
        const char *id_prover, const char *id_verifier,
        uint8_t w0[32], uint8_t L[kPubKeySize])
{
  int ret = spake2plus_calc_w0_L(password, password_length, salt, salt_length,
         it, id_prover, id_verifier, w0, L);
  if (ret != 0) {
    OC_ERR("SPAKE2+ password expansion failed (ret %d)!", ret);
  }

  return ret;
}

int spake2plus_gen_keypair(uint8_t y[32], uint8_t pub_y[kPubKeySize])
{
  /* Generate an ephemeral P-256 key pair using the PSA API. */
  psa_key_attributes_t attrs = PSA_KEY_ATTRIBUTES_INIT;
  psa_set_key_type(&attrs, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
  psa_set_key_bits(&attrs, 256);
  psa_set_key_usage_flags(&attrs, PSA_KEY_USAGE_EXPORT | PSA_KEY_USAGE_DERIVE);
  psa_set_key_algorithm(&attrs, PSA_ALG_ECDH);

  psa_key_id_t key_id = PSA_KEY_ID_NULL;
  psa_status_t status = psa_generate_key(&attrs, &key_id);
  if (status != PSA_SUCCESS) {
    OC_ERR("SPAKE2+ ephemeral key generation failed (status %d)!", (int)status);
    return (int)status;
  }

  size_t olen;
  status = psa_export_key(key_id, y, 32, &olen);
  if (status != PSA_SUCCESS) {
    OC_ERR("SPAKE2+ private key export failed (status %d)!", (int)status);
    psa_destroy_key(key_id);
    return (int)status;
  }

  status = psa_export_public_key(key_id, pub_y, kPubKeySize, &olen);
  if (status != PSA_SUCCESS) {
    OC_ERR("SPAKE2+ public key export failed (status %d)!", (int)status);
  }

  psa_destroy_key(key_id);
  return (int)status;
}

/* shareP = pubA + w0 * M */
int spake2plus_calc_shareP(uint8_t shareP[kPubKeySize],
        const uint8_t pubA[kPubKeySize],
        const uint8_t w0[32])
{
  return calculate_pX(shareP, pubA, w0, bytes_M);
}

/* shareV = pubB + w0 * N */
int spake2plus_calc_shareV(uint8_t shareV[kPubKeySize],
        const uint8_t pubB[kPubKeySize],
        const uint8_t w0[32])
{
  return calculate_pX(shareV, pubB, w0, bytes_N);
}

/* ── Transcript computation ──────────────────────────────────────────────── */

int spake2plus_calc_transcript_responder(spake_data_t *spake_data,
        const uint8_t shareP_enc[kPubKeySize],
        const uint8_t shareV_enc[kPubKeySize],
        char *idProver, char *idVerifier, char *context)
{
  uint8_t Z[kPubKeySize];
  uint8_t V[kPubKeySize];
  uint8_t ttbuf[2048] = {0};
  size_t ttlen = 0;
  size_t olen;

  /*
   * Verify shareP is not the point at infinity by attempting to parse it.
   * The private ecp_point_read_binary rejects the zero point.
   */
  mbedtls_ecp_point shareP_pt;
  mbedtls_ecp_point_init(&shareP_pt);
  int ecp_ret = ecp_point_from_bytes(&shareP_pt, shareP_enc);
  mbedtls_ecp_point_free(&shareP_pt);
  if (ecp_ret != 0) {
    OC_ERR("SPAKE2+ shareP is invalid (ret %d)!", ecp_ret);
    return ecp_ret;
  }

  /* Z = y * (shareP - w0 * M) */
  int ret = calculate_ZV(Z, spake_data->y, shareP_enc, spake_data->w0, bytes_M);
  if (ret != 0) {
    OC_ERR("SPAKE2+ Z computation failed (ret %d)!", ret);
    return ret;
  }

  /* V = y * L, where L = w1*G was computed during parameter exchange. */
  mbedtls_mpi y_mpi;
  mbedtls_ecp_point L_pt, V_pt;
  mbedtls_mpi_init(&y_mpi);
  mbedtls_ecp_point_init(&L_pt);
  mbedtls_ecp_point_init(&V_pt);

  MBEDTLS_MPI_CHK(mbedtls_mpi_read_binary(&y_mpi, spake_data->y, 32));
  MBEDTLS_MPI_CHK(ecp_point_from_bytes(&L_pt, spake_data->L));
  MBEDTLS_MPI_CHK(mbedtls_ecp_mul(&grp, &V_pt, &y_mpi, &L_pt, 
          mbedtls_psa_get_random, MBEDTLS_PSA_RANDOM_STATE));
  MBEDTLS_MPI_CHK(ecp_point_to_bytes(&V_pt, V));

cleanup:
  mbedtls_mpi_free(&y_mpi);
  mbedtls_ecp_point_free(&L_pt);
  mbedtls_ecp_point_free(&V_pt);
  if (ret != 0) {
    OC_ERR("SPAKE2+ V computation failed (ret %d)!", ret);
    return ret;
  }

  /* Assemble transcript TT. */
  ttlen += encode_string(context, ttbuf + ttlen);
  ttlen += encode_string(idProver, ttbuf + ttlen);
  ttlen += encode_string(idVerifier, ttbuf + ttlen);
  ttlen += encode_point(bytes_M, sizeof(bytes_M), ttbuf + ttlen);  /* M */
  ttlen += encode_point(bytes_N, sizeof(bytes_N), ttbuf + ttlen);  /* N */
  ttlen += encode_point(shareP_enc, kPubKeySize, ttbuf + ttlen);   /* X */
  ttlen += encode_point(shareV_enc, kPubKeySize, ttbuf + ttlen);   /* Y */
  ttlen += encode_point(Z, kPubKeySize, ttbuf + ttlen);            /* Z */
  ttlen += encode_point(V, kPubKeySize, ttbuf + ttlen);            /* V */
  ttlen += encode_w0_mpi(spake_data->w0, ttbuf + ttlen);           /* w0 */

  /* K_main = SHA-256(TT) */
  psa_status_t status = psa_hash_compute(PSA_ALG_SHA_256, ttbuf, ttlen,
                                         spake_data->K_main, 32, &olen);
  if (status != PSA_SUCCESS) {
    OC_ERR("SPAKE2+ transcript hash failed (status %d)!", (int)status);
  }

  return (int)status;
}

int spake2plus_calc_transcript_initiator(const uint8_t w0[32], const uint8_t w1[32],
        const uint8_t x[32],
        const uint8_t shareP_enc[kPubKeySize],
        const uint8_t shareV_enc[kPubKeySize],
        uint8_t K_main[32],
        char *idProver, char *idVerifier, char *context)
{
  uint8_t Z[kPubKeySize];
  uint8_t V[kPubKeySize];
  uint8_t ttbuf[2048] = {0};
  size_t ttlen = 0;
  size_t olen;

  /* Z = x * (shareV - w0 * N) */
  int ret = calculate_ZV(Z, x, shareV_enc, w0, bytes_N);
  if (ret != 0) {
    OC_ERR("SPAKE2+ initiator Z computation failed (ret %d)!", ret);
    return ret;
  }

  /* V = w1 * (shareV - w0 * N) */
  ret = calculate_ZV(V, w1, shareV_enc, w0, bytes_N);
  if (ret != 0) {
    OC_ERR("SPAKE2+ initiator V computation failed (ret %d)!", ret);
    return ret;
  }

  /* Assemble transcript TT. */
  ttlen += encode_string(context, ttbuf + ttlen);
  ttlen += encode_string(idProver, ttbuf + ttlen);
  ttlen += encode_string(idVerifier, ttbuf + ttlen);
  ttlen += encode_point(bytes_M, sizeof(bytes_M), ttbuf + ttlen);  /* M */
  ttlen += encode_point(bytes_N, sizeof(bytes_N), ttbuf + ttlen);  /* N */
  ttlen += encode_point(shareP_enc, kPubKeySize, ttbuf + ttlen);   /* X */
  ttlen += encode_point(shareV_enc, kPubKeySize, ttbuf + ttlen);   /* Y */
  ttlen += encode_point(Z, kPubKeySize, ttbuf + ttlen);            /* Z */
  ttlen += encode_point(V, kPubKeySize, ttbuf + ttlen);            /* V */
  ttlen += encode_w0_mpi(w0, ttbuf + ttlen);                       /* w0 */

  /* K_main = SHA-256(TT) */
  psa_status_t status = psa_hash_compute(PSA_ALG_SHA_256, ttbuf, ttlen, K_main, 32, &olen);
  if (status != PSA_SUCCESS) {
    OC_ERR("SPAKE2+ initiator transcript hash failed (status %d)!", (int)status);
  }

  return (int)status;
}

/* ── Key confirmation and shared key derivation ──────────────────────────── */

/*
 * Derive KcA||KcB (64 bytes) from K_main via HKDF-SHA256 with
 * info="ConfirmationKeys". KcA is the first 32 bytes, KcB the last 32 bytes.
 * Returns PSA_SUCCESS on success.
 */
static psa_status_t derive_confirmation_keys(const uint8_t *K_main, uint8_t KcA_KcB[64])
{
  static const uint8_t info[] = "ConfirmationKeys";
  psa_key_derivation_operation_t op = PSA_KEY_DERIVATION_OPERATION_INIT;

  psa_status_t status = psa_key_derivation_setup(&op, PSA_ALG_HKDF(PSA_ALG_SHA_256));
  if (status != PSA_SUCCESS) {
    return status;
  }

  status = psa_key_derivation_input_bytes(&op, PSA_KEY_DERIVATION_INPUT_SECRET, K_main, 32);
  if (status != PSA_SUCCESS) {
    goto cleanup;
  }

  status = psa_key_derivation_input_bytes(&op, PSA_KEY_DERIVATION_INPUT_INFO, info, sizeof(info) - 1);
  if (status != PSA_SUCCESS) {
    goto cleanup;
  }

  status = psa_key_derivation_output_bytes(&op, KcA_KcB, 64);

cleanup:
  psa_key_derivation_abort(&op);
  return status;
}

/*
 * Compute HMAC-SHA256(key_bytes, key_len, msg, msg_len) into mac_out[32].
 * Returns PSA_SUCCESS on success.
 */
static psa_status_t hmac_sha256(const uint8_t *key_bytes, size_t key_len,
        const uint8_t *msg, size_t msg_len,
        uint8_t mac_out[32])
{
  psa_key_attributes_t attrs = PSA_KEY_ATTRIBUTES_INIT;
  psa_set_key_type(&attrs, PSA_KEY_TYPE_HMAC);
  psa_set_key_usage_flags(&attrs, PSA_KEY_USAGE_SIGN_MESSAGE);
  psa_set_key_algorithm(&attrs, PSA_ALG_HMAC(PSA_ALG_SHA_256));

  psa_key_id_t key_id = PSA_KEY_ID_NULL;
  psa_status_t status = psa_import_key(&attrs, key_bytes, key_len, &key_id);
  if (status != PSA_SUCCESS) {
    return status;
  }

  size_t mac_len;
  status = psa_mac_compute(key_id, PSA_ALG_HMAC(PSA_ALG_SHA_256), msg, msg_len, mac_out, 32, &mac_len);
  psa_destroy_key(key_id);
  return status;
}

int spake2plus_calc_confirmV(uint8_t *K_main, uint8_t confirmV[32], uint8_t bytes_shareP[kPubKeySize])
{
  /* KcA||KcB = HKDF(K_main, "ConfirmationKeys", 64) */
  uint8_t KcA_KcB[64];
  psa_status_t status = derive_confirmation_keys(K_main, KcA_KcB);
  if (status != PSA_SUCCESS) {
    OC_ERR("SPAKE2+ confirmation key derivation failed (status %d)!", (int)status);
    return (int)status;
  }

  /* confirmV = HMAC-SHA256(KcB, shareP), KcB = KcA_KcB[32..63] */
  status = hmac_sha256(KcA_KcB + 32, 32, bytes_shareP, kPubKeySize, confirmV);
  if (status != PSA_SUCCESS) {
    OC_ERR("SPAKE2+ confirmV HMAC failed (status %d)!", (int)status);
  }

  return (int)status;
}

int spake2plus_calc_confirmP(uint8_t *K_main, uint8_t confirmP[32], uint8_t bytes_shareV[kPubKeySize])
{
  /* KcA||KcB = HKDF(K_main, "ConfirmationKeys", 64) */
  uint8_t KcA_KcB[64];
  psa_status_t status = derive_confirmation_keys(K_main, KcA_KcB);
  if (status != PSA_SUCCESS) {
    OC_ERR("SPAKE2+ confirmation key derivation failed (status %d)!", (int)status);
    return (int)status;
  }

  /* confirmP = HMAC-SHA256(KcA, shareV), KcA = KcA_KcB[0..31] */
  status = hmac_sha256(KcA_KcB, 32, bytes_shareV, kPubKeySize, confirmP);
  if (status != PSA_SUCCESS) {
    OC_ERR("SPAKE2+ confirmP HMAC failed (status %d)!", (int)status);
  }

  return (int)status;
}

/*
 * Common HKDF-SHA256 extraction for K_shared derivation.
 * Returns PSA_SUCCESS on success.
 */
static psa_status_t derive_K_shared(const uint8_t *K_main, uint8_t *K_shared, size_t output_length)
{
  static const uint8_t info[] = "SharedKey";
  psa_key_derivation_operation_t op = PSA_KEY_DERIVATION_OPERATION_INIT;

  psa_status_t status = psa_key_derivation_setup(&op, PSA_ALG_HKDF(PSA_ALG_SHA_256));
  if (status != PSA_SUCCESS) {
    return status;
  }

  status = psa_key_derivation_input_bytes(&op, PSA_KEY_DERIVATION_INPUT_SECRET, K_main, 32);
  if (status != PSA_SUCCESS) {
    goto cleanup;
  }

  status = psa_key_derivation_input_bytes(&op, PSA_KEY_DERIVATION_INPUT_INFO,
                                          info, sizeof(info) - 1);
  if (status != PSA_SUCCESS) {
    goto cleanup;
  }

  status = psa_key_derivation_output_bytes(&op, K_shared, output_length);

cleanup:
  psa_key_derivation_abort(&op);
  return status;
}

int spake2plus_calc_K_shared(uint8_t *K_main, uint8_t K_shared[16])
{
  psa_status_t status = derive_K_shared(K_main, K_shared, 16);
  if (status != PSA_SUCCESS) {
    OC_ERR("SPAKE2+ K_shared derivation failed (status %d)!", (int)status);
  }

  return (int)status;
}

int spake2plus_calc_K_shared_256(uint8_t *K_main, uint8_t K_shared[32])
{
  psa_status_t status = derive_K_shared(K_main, K_shared, 32);
  if (status != PSA_SUCCESS) {
    OC_ERR("SPAKE2+ K_shared_256 derivation failed (status %d)!", (int)status);
  }

  return (int)status;
}

