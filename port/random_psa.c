/*
 * Copyright (c) 2026 Alexander Burker
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "psa/crypto.h"

#include "port/oc_log.h"
#include "port/oc_random.h"

void oc_random_init(void)
{
  psa_status_t status = psa_crypto_init();
  if (status != PSA_SUCCESS) {
    OC_ERR("PSA crypto initialization failed (status %d)!", (int)status);
  }
}

unsigned int oc_random_value(void)
{
  unsigned int val = 0;
  psa_status_t status = psa_generate_random((uint8_t *)&val, sizeof(val));
  if (status != PSA_SUCCESS) {
    OC_ERR("Failed to generate random value (status %d)!", (int)status);
  }

  return val;
}

int oc_random_fill(uint8_t *buf, size_t len)
{
  psa_status_t status = psa_generate_random(buf, len);
  if (status != PSA_SUCCESS) {
    OC_ERR("Failed to fill random buffer (status %d)!", (int)status);
    return -1;
  }

  return 0;
}

void oc_random_destroy(void)
{
  mbedtls_psa_crypto_free();
}
