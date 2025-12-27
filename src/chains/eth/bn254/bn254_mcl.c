/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */

#ifdef USE_MCL

#include "bn254.h"
#include <mcl/bn_c384_256.h>
#include <stdio.h>
#include <string.h>

void bn254_init(void) {
  static bool initialized = false;
  if (!initialized) {
    // Ethereum uses Alt-BN128, which corresponds to MCL_BN_SNARK1
    int ret = mclBn_init(mclBn_CurveSNARK1, MCLBN_COMPILED_TIME_VAR);
    if (ret != 0) {
      fprintf(stderr, "mclBn_init failed: %d\n", ret);
    }
    initialized = true;
  }
}

// Helpers to convert byte arrays to MCL elements
static void set_fp_be(mclBnFp* fp, const uint8_t* bytes) {
  mclBnFp_setBigEndianMod(fp, bytes, 32);
}

static void set_fr_be(mclBnFr* fr, const uint8_t* bytes) {
  mclBnFr_setBigEndianMod(fr, bytes, 32);
}

static bool is_zero_bytes(const uint8_t* bytes, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (bytes[i] != 0) return false;
  }
  return true;
}

bool bn254_g1_from_bytes_be(bn254_g1_t* p, const uint8_t* bytes) {
  bn254_init();

  // Check for point at infinity (0, 0)
  if (is_zero_bytes(bytes, 64)) {
    mclBnG1_clear(p);
    return true;
  }

  mclBnFp x, y, z;
  set_fp_be(&x, bytes);
  set_fp_be(&y, bytes + 32);
  mclBnFp_setInt(&z, 1); // Z=1

  // Direct assignment for G1 (x, y, z)
  p->x = x;
  p->y = y;
  p->z = z;

  if (!mclBnG1_isValid(p)) return false;
  return true;
}

bool bn254_g1_from_bytes(bn254_g1_t* p, const uint8_t* bytes) {
  return bn254_g1_from_bytes_be(p, bytes);
}

void bn254_g1_to_bytes(const bn254_g1_t* p, uint8_t* out) {
  // Not strictly needed for verification
  memset(out, 0, 64);
}

bool bn254_g2_from_bytes_eth(bn254_g2_t* p, const uint8_t* bytes) {
  bn254_init();

  // Check for point at infinity (0, 0, 0, 0)
  if (is_zero_bytes(bytes, 128)) {
    mclBnG2_clear(p);
    return true;
  }

  // ETH: X_im, X_re, Y_im, Y_re
  mclBnFp x0, x1, y0, y1;
  set_fp_be(&x1, bytes);      // X_im
  set_fp_be(&x0, bytes + 32); // X_re
  set_fp_be(&y1, bytes + 64); // Y_im
  set_fp_be(&y0, bytes + 96); // Y_re

  mclBnFp2 x, y, z;
  // Direct assignment for Fp2 (d[0] = real, d[1] = imag)
  x.d[0] = x0;
  x.d[1] = x1;
  y.d[0] = y0;
  y.d[1] = y1;

  // Z = 1 + 0i
  mclBnFp_setInt(&z.d[0], 1);
  mclBnFp_setInt(&z.d[1], 0);

  // Direct assignment for G2 (x, y, z)
  p->x = x;
  p->y = y;
  p->z = z;

  if (!mclBnG2_isValid(p)) return false;
  return true;
}

bool bn254_g2_from_bytes_raw(bn254_g2_t* p, const uint8_t* bytes) {
  bn254_init();
  // Raw: X_re, X_im, Y_re, Y_im
  mclBnFp x0, x1, y0, y1;
  set_fp_be(&x0, bytes);      // X_re
  set_fp_be(&x1, bytes + 32); // X_im
  set_fp_be(&y0, bytes + 64); // Y_re
  set_fp_be(&y1, bytes + 96); // Y_im

  mclBnFp2 x, y, z;
  x.d[0] = x0;
  x.d[1] = x1;
  y.d[0] = y0;
  y.d[1] = y1;

  mclBnFp_setInt(&z.d[0], 1);
  mclBnFp_setInt(&z.d[1], 0);

  p->x = x;
  p->y = y;
  p->z = z;

  return true;
}

void bn254_g2_to_bytes_eth(const bn254_g2_t* p, uint8_t* out) {
  memset(out, 0, 128);
}

// Arithmetic
void bn254_g1_add(bn254_g1_t* r, const bn254_g1_t* a, const bn254_g1_t* b) {
  mclBnG1_add(r, a, b);
}

void bn254_g1_mul(bn254_g1_t* r, const bn254_g1_t* p, const uint256_t* scalar) {
  mclBnFr s;
  set_fr_be(&s, scalar->bytes);
  mclBnG1_mul(r, p, &s);
}

bool bn254_g1_is_on_curve(const bn254_g1_t* p) {
  return mclBnG1_isValid(p) == 1;
}

void bn254_g2_add(bn254_g2_t* r, const bn254_g2_t* a, const bn254_g2_t* b) {
  mclBnG2_add(r, a, b);
}

void bn254_g2_mul(bn254_g2_t* r, const bn254_g2_t* p, const uint256_t* scalar) {
  mclBnFr s;
  set_fr_be(&s, scalar->bytes);
  mclBnG2_mul(r, p, &s);
}

// Pairing
void bn254_miller_loop(bn254_fp12_t* res, const bn254_g1_t* P, const bn254_g2_t* Q) {
  mclBn_millerLoop(res, P, Q);
}

void bn254_final_exponentiation(bn254_fp12_t* r, const bn254_fp12_t* f) {
  mclBn_finalExp(r, f);
}

bool bn254_pairing_batch_check(const bn254_g1_t* P, const bn254_g2_t* Q, size_t count) {
  bn254_init();
  mclBnGT prod, final;

  // Optimized batch Miller loop
  mclBn_millerLoopVec(&prod, P, Q, count);

  // Final exponentiation on the product
  mclBn_finalExp(&final, &prod);

  return mclBnGT_isOne(&final) == 1;
}

void bn254_fp12_mul(bn254_fp12_t* r, const bn254_fp12_t* a, const bn254_fp12_t* b) {
  mclBnGT_mul(r, a, b);
}

bool bn254_fp12_is_one(const bn254_fp12_t* a) {
  return mclBnGT_isOne(a) == 1;
}

#endif // USE_MCL
