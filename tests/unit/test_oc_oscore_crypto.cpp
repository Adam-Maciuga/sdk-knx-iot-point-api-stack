/*
 * Unit tests for security/oc_oscore_crypto.c
 *
 * Covers: HKDF_SHA256, oc_oscore_AEAD_nonce, oc_oscore_compose_AAD,
 *         oc_oscore_encrypt / oc_oscore_decrypt round-trips.
 *
 * Uses mbedtls (linked via kisClientServer) for the underlying AES-CCM.
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "security/oc_oscore_crypto.h"
#include "messaging/coap/oscore_constants.h"
#include "psa/crypto.h"

/* Stubs for mbedtls platform callbacks (normally in port/windows/abort.c) */
void abort_impl(void) { abort(); }
void exit_impl(int status) { exit(status); }
}

/* Global setup: PSA crypto must be initialized before any HMAC/AEAD ops */
class PsaCryptoEnv : public ::testing::Environment {
public:
  void SetUp() override { psa_crypto_init(); }
};
static auto *g_psa_env __attribute__((unused)) =
    ::testing::AddGlobalTestEnvironment(new PsaCryptoEnv);

/* ═══════════════════════════════════════════════════════════════════════════
 * HKDF_SHA256
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(HkdfSha256, BasicDerivation)
{
  /* RFC 5869 Test Case 1 inputs */
  uint8_t ikm[22];
  memset(ikm, 0x0b, sizeof(ikm));
  uint8_t salt[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                    0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c};
  uint8_t info[] = {0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6,
                    0xf7, 0xf8, 0xf9};
  uint8_t okm[42] = {};

  int rc = HKDF_SHA256(salt, sizeof(salt), ikm, sizeof(ikm),
                       info, sizeof(info), okm, sizeof(okm));
  EXPECT_EQ(rc, 0);

  /* RFC 5869 Test Case 1 expected OKM */
  const uint8_t expected[] = {
    0x3c, 0xb2, 0x5f, 0x25, 0xfa, 0xac, 0xd5, 0x7a,
    0x90, 0x43, 0x4f, 0x64, 0xd0, 0x36, 0x2f, 0x2a,
    0x2d, 0x2d, 0x0a, 0x90, 0xcf, 0x1a, 0x5a, 0x4c,
    0x5d, 0xb0, 0x2d, 0x56, 0xec, 0xc4, 0xc5, 0xbf,
    0x34, 0x00, 0x72, 0x08, 0xd5, 0xb8, 0x87, 0x18,
    0x58, 0x65
  };
  EXPECT_EQ(memcmp(okm, expected, sizeof(expected)), 0);
}

TEST(HkdfSha256, EmptySalt)
{
  uint8_t ikm[22];
  memset(ikm, 0x0b, sizeof(ikm));
  uint8_t okm[32] = {};

  /* NULL salt should use zeros */
  int rc = HKDF_SHA256(nullptr, 0, ikm, sizeof(ikm),
                       nullptr, 0, okm, sizeof(okm));
  EXPECT_EQ(rc, 0);
  /* Just verify it doesn't crash and produces non-zero output */
  bool all_zero = true;
  for (int i = 0; i < 32; i++) {
    if (okm[i] != 0) { all_zero = false; break; }
  }
  EXPECT_FALSE(all_zero);
}

TEST(HkdfSha256, ShortOutput)
{
  uint8_t ikm[] = {0x01, 0x02, 0x03};
  uint8_t okm[16] = {};

  int rc = HKDF_SHA256(nullptr, 0, ikm, sizeof(ikm),
                       nullptr, 0, okm, 16);
  EXPECT_EQ(rc, 0);
}

TEST(HkdfSha256, DifferentIKM_DifferentOutput)
{
  uint8_t ikm1[] = {0x01};
  uint8_t ikm2[] = {0x02};
  uint8_t okm1[16] = {}, okm2[16] = {};

  HKDF_SHA256(nullptr, 0, ikm1, 1, nullptr, 0, okm1, 16);
  HKDF_SHA256(nullptr, 0, ikm2, 1, nullptr, 0, okm2, 16);
  EXPECT_NE(memcmp(okm1, okm2, 16), 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_oscore_AEAD_nonce
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(AeadNonce, BasicComputation)
{
  uint8_t id[] = {0x01};
  uint8_t piv[] = {0x00, 0x00, 0x00, 0x05};
  uint8_t civ[OSCORE_COMMON_IV_LEN];
  memset(civ, 0, sizeof(civ));
  uint8_t nonce[OSCORE_AEAD_NONCE_LEN] = {};

  oc_oscore_AEAD_nonce(id, sizeof(id), piv, sizeof(piv),
                       civ, nonce, OSCORE_AEAD_NONCE_LEN);

  /* nonce[0] = id_len = 1, XOR with civ[0]=0 → 1 */
  EXPECT_EQ(nonce[0], 1);
  /* last byte: piv[3]=5 XOR civ[12]=0 → 5 */
  EXPECT_EQ(nonce[OSCORE_AEAD_NONCE_LEN - 1], 5);
}

TEST(AeadNonce, XorWithCommonIV)
{
  uint8_t id[] = {0x00};
  uint8_t piv[] = {0x00};
  uint8_t civ[OSCORE_COMMON_IV_LEN];
  memset(civ, 0xFF, sizeof(civ));
  uint8_t nonce[OSCORE_AEAD_NONCE_LEN] = {};

  oc_oscore_AEAD_nonce(id, 1, piv, 1, civ, nonce, OSCORE_AEAD_NONCE_LEN);

  /* nonce[0] = (1 for id_len) XOR 0xFF = 0xFE */
  EXPECT_EQ(nonce[0], 0xFE);
  /* last byte: 0 XOR 0xFF = 0xFF */
  EXPECT_EQ(nonce[OSCORE_AEAD_NONCE_LEN - 1], 0xFF);
}

TEST(AeadNonce, EmptyID)
{
  uint8_t piv[] = {0x01};
  uint8_t civ[OSCORE_COMMON_IV_LEN];
  memset(civ, 0, sizeof(civ));
  uint8_t nonce[OSCORE_AEAD_NONCE_LEN] = {};

  oc_oscore_AEAD_nonce(nullptr, 0, piv, 1, civ, nonce, OSCORE_AEAD_NONCE_LEN);

  /* nonce[0] = 0 (id_len=0) XOR 0 = 0 */
  EXPECT_EQ(nonce[0], 0);
  /* last byte: 1 XOR 0 = 1 */
  EXPECT_EQ(nonce[OSCORE_AEAD_NONCE_LEN - 1], 1);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_oscore_compose_AAD
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(ComposeAAD, BasicComposition)
{
  uint8_t kid[] = {0x01};
  uint8_t piv[] = {0x00};
  uint8_t aad[OSCORE_AAD_MAX_LEN] = {};
  uint8_t aad_len = 0;

  int rc = oc_oscore_compose_AAD(kid, sizeof(kid), piv, sizeof(piv),
                                 aad, &aad_len);
  EXPECT_EQ(rc, 0);
  EXPECT_GT(aad_len, 0);
}

TEST(ComposeAAD, EmptyKidPiv)
{
  uint8_t aad[OSCORE_AAD_MAX_LEN] = {};
  uint8_t aad_len = 0;

  int rc = oc_oscore_compose_AAD(nullptr, 0, nullptr, 0, aad, &aad_len);
  EXPECT_EQ(rc, 0);
  EXPECT_GT(aad_len, 0);
}

TEST(ComposeAAD, DifferentKid_DifferentAAD)
{
  uint8_t kid1[] = {0x01};
  uint8_t kid2[] = {0x02};
  uint8_t piv[] = {0x00};
  uint8_t aad1[OSCORE_AAD_MAX_LEN] = {}, aad2[OSCORE_AAD_MAX_LEN] = {};
  uint8_t len1 = 0, len2 = 0;

  oc_oscore_compose_AAD(kid1, 1, piv, 1, aad1, &len1);
  oc_oscore_compose_AAD(kid2, 1, piv, 1, aad2, &len2);
  EXPECT_EQ(len1, len2);
  EXPECT_NE(memcmp(aad1, aad2, len1), 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * oc_oscore_encrypt / oc_oscore_decrypt round-trip
 * ═══════════════════════════════════════════════════════════════════════════ */

class OscoreEncryptTest : public ::testing::Test {
protected:
  /* AES-CCM-16-64-128: 16-byte key, 13-byte nonce, 8-byte tag */
  uint8_t key[OSCORE_KEY_LEN];
  uint8_t nonce[OSCORE_AEAD_NONCE_LEN];
  uint8_t aad[32];
  uint8_t aad_len;

  void SetUp() override
  {
    memset(key, 0xAA, sizeof(key));
    memset(nonce, 0xBB, sizeof(nonce));
    /* Simple AAD */
    memset(aad, 0xCC, sizeof(aad));
    aad_len = sizeof(aad);
  }
};

TEST_F(OscoreEncryptTest, RoundTrip)
{
  /* plaintext */
  uint8_t pt_buf[64] = {};
  const char *msg = "Hello, OSCORE!";
  size_t pt_len = strlen(msg);
  memcpy(pt_buf, msg, pt_len);

  /* output receives ciphertext || tag */
  uint8_t ct_out[64] = {};

  int rc = oc_oscore_encrypt(pt_buf, pt_len, OSCORE_AEAD_TAG_LEN,
                             key, sizeof(key), nonce, sizeof(nonce),
                             aad, aad_len, ct_out);
  ASSERT_EQ(rc, 0);

  uint8_t decrypted[64] = {};
  rc = oc_oscore_decrypt(ct_out, pt_len + OSCORE_AEAD_TAG_LEN,
                         OSCORE_AEAD_TAG_LEN,
                         key, sizeof(key), nonce, sizeof(nonce),
                         aad, aad_len, decrypted);
  ASSERT_EQ(rc, 0);
  EXPECT_EQ(memcmp(decrypted, msg, pt_len), 0);
}

TEST_F(OscoreEncryptTest, WrongKey_DecryptFails)
{
  uint8_t pt_buf[32] = {0x01, 0x02, 0x03, 0x04};
  size_t pt_len = 4;
  uint8_t ct_out[32] = {};

  oc_oscore_encrypt(pt_buf, pt_len, OSCORE_AEAD_TAG_LEN,
                    key, sizeof(key), nonce, sizeof(nonce),
                    aad, aad_len, ct_out);

  /* Corrupt key */
  uint8_t bad_key[OSCORE_KEY_LEN];
  memset(bad_key, 0x00, sizeof(bad_key));

  uint8_t decrypted[32] = {};
  int rc = oc_oscore_decrypt(ct_out, pt_len + OSCORE_AEAD_TAG_LEN,
                             OSCORE_AEAD_TAG_LEN,
                             bad_key, sizeof(bad_key), nonce, sizeof(nonce),
                             aad, aad_len, decrypted);
  EXPECT_NE(rc, 0);
}

TEST_F(OscoreEncryptTest, TamperedCiphertext_DecryptFails)
{
  uint8_t pt_buf[32] = {0x01, 0x02, 0x03, 0x04};
  size_t pt_len = 4;
  uint8_t ct_out[32] = {};

  oc_oscore_encrypt(pt_buf, pt_len, OSCORE_AEAD_TAG_LEN,
                    key, sizeof(key), nonce, sizeof(nonce),
                    aad, aad_len, ct_out);

  /* Flip a byte in ciphertext */
  ct_out[0] ^= 0xFF;

  uint8_t decrypted[32] = {};
  int rc = oc_oscore_decrypt(ct_out, pt_len + OSCORE_AEAD_TAG_LEN,
                             OSCORE_AEAD_TAG_LEN,
                             key, sizeof(key), nonce, sizeof(nonce),
                             aad, aad_len, decrypted);
  EXPECT_NE(rc, 0);
}

TEST_F(OscoreEncryptTest, WrongAAD_DecryptFails)
{
  uint8_t pt_buf[32] = {0x01, 0x02};
  size_t pt_len = 2;
  uint8_t ct_out[32] = {};

  oc_oscore_encrypt(pt_buf, pt_len, OSCORE_AEAD_TAG_LEN,
                    key, sizeof(key), nonce, sizeof(nonce),
                    aad, aad_len, ct_out);

  /* Different AAD */
  uint8_t bad_aad[32];
  memset(bad_aad, 0xDD, sizeof(bad_aad));

  uint8_t decrypted[32] = {};
  int rc = oc_oscore_decrypt(ct_out, pt_len + OSCORE_AEAD_TAG_LEN,
                             OSCORE_AEAD_TAG_LEN,
                             key, sizeof(key), nonce, sizeof(nonce),
                             bad_aad, sizeof(bad_aad), decrypted);
  EXPECT_NE(rc, 0);
}

TEST_F(OscoreEncryptTest, EmptyPlaintext)
{
  uint8_t pt_buf[32] = {};  /* empty plaintext */
  uint8_t ct_out[32] = {};

  int rc = oc_oscore_encrypt(pt_buf, 0, OSCORE_AEAD_TAG_LEN,
                             key, sizeof(key), nonce, sizeof(nonce),
                             aad, aad_len, ct_out);
  ASSERT_EQ(rc, 0);

  /* ct_out contains tag only (pt_len = 0) */
  uint8_t decrypted[32] = {};
  rc = oc_oscore_decrypt(ct_out, OSCORE_AEAD_TAG_LEN,
                         OSCORE_AEAD_TAG_LEN,
                         key, sizeof(key), nonce, sizeof(nonce),
                         aad, aad_len, decrypted);
  EXPECT_EQ(rc, 0);
}
