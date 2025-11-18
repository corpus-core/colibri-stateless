/*
 * Copyright (c) 2025 corpus.core
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PRECOMPILES_BLS_C
#define PRECOMPILES_BLS_C

#include "blst.h"
#include "bytes.h"
#include "precompiles.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// EIP-2537 uses 64-byte big endian limbs per Fp element
// G1 point: 128 bytes (X[64] || Y[64])
// G2 point: 256 bytes (X.c0[64] || X.c1[64] || Y.c0[64] || Y.c1[64])

static inline bool is_all_zero(const uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (p[i] != 0) return false;
  }
  return true;
}

static inline void blst_fp_from_be64(blst_fp* out, const uint8_t in[64]) {
  // Take the last 48 bytes as the canonical big-endian encoding
  blst_fp_from_bendian(out, in + 16);
}

static inline void be64_from_blst_fp(uint8_t out[64], const blst_fp* in) {
  memset(out, 0, 16);
  blst_bendian_from_fp(out + 16, in);
}

static inline bool read_g1_affine(const uint8_t in[128], blst_p1_affine* out, bool* is_inf) {
  *is_inf = is_all_zero(in, 128);
  if (*is_inf) {
    // Represent infinity by zero output in our encoding; BLST affine infinity representation
    // isn't constructed directly, so we handle it at call sites.
    return true;
  }
  blst_fp_from_be64(&out->x, in);
  blst_fp_from_be64(&out->y, in + 64);
  // Validate
  if (!blst_p1_affine_on_curve(out) || !blst_p1_affine_in_g1(out)) return false;
  return true;
}

static inline void write_g1_affine(const blst_p1_affine* in, uint8_t out[128], bool is_inf) {
  if (is_inf) {
    memset(out, 0, 128);
    return;
  }
  be64_from_blst_fp(out, &in->x);
  be64_from_blst_fp(out + 64, &in->y);
}

static inline bool read_g2_affine(const uint8_t in[256], blst_p2_affine* out, bool* is_inf) {
  *is_inf = is_all_zero(in, 256);
  if (*is_inf) return true;
  // X = (c0, c1), Y = (c0, c1)
  blst_fp_from_be64(&out->x.fp[0], in + 0);
  blst_fp_from_be64(&out->x.fp[1], in + 64);
  blst_fp_from_be64(&out->y.fp[0], in + 128);
  blst_fp_from_be64(&out->y.fp[1], in + 192);
  if (!blst_p2_affine_on_curve(out) || !blst_p2_affine_in_g2(out)) return false;
  return true;
}

static inline void write_g2_affine(const blst_p2_affine* in, uint8_t out[256], bool is_inf) {
  if (is_inf) {
    memset(out, 0, 256);
    return;
  }
  be64_from_blst_fp(out + 0, &in->x.fp[0]);
  be64_from_blst_fp(out + 64, &in->x.fp[1]);
  be64_from_blst_fp(out + 128, &in->y.fp[0]);
  be64_from_blst_fp(out + 192, &in->y.fp[1]);
}

// 0x0b: G1ADD
static pre_result_t pre_bls12_g1add(bytes_t input, buffer_t* output, uint64_t* gas_used) {
  *gas_used = 375;
  if (input.len != 256) return PRE_INVALID_INPUT;

  blst_p1_affine p1 = {0}, p2 = {0};
  bool           inf1 = false, inf2 = false;
  if (!read_g1_affine(input.data, &p1, &inf1)) return PRE_INVALID_INPUT;
  if (!read_g1_affine(input.data + 128, &p2, &inf2)) return PRE_INVALID_INPUT;

  blst_p1        r     = {0};
  blst_p1_affine r_aff = {0};
  bool           r_inf = false;

  if (inf1 && inf2) {
    r_inf = true;
  }
  else if (inf1) {
    blst_p1_from_affine(&r, &p2);
  }
  else if (inf2) {
    blst_p1_from_affine(&r, &p1);
  }
  else {
    blst_p1_from_affine(&r, &p1);
    blst_p1_add_affine(&r, &r, &p2);
  }

  if (!inf1 || !inf2) {
    if (blst_p1_is_inf(&r)) {
      r_inf = true;
    }
    else {
      blst_p1_to_affine(&r_aff, &r);
    }
  }

  buffer_reset(output);
  buffer_grow(output, 128);
  output->data.len = 128;
  write_g1_affine(&r_aff, output->data.data, r_inf);
  return PRE_SUCCESS;
}

// 0x0d: G2ADD
static pre_result_t pre_bls12_g2add(bytes_t input, buffer_t* output, uint64_t* gas_used) {
  *gas_used = 600;
  if (input.len != 512) return PRE_INVALID_INPUT;

  blst_p2_affine q1 = {0}, q2 = {0};
  bool           inf1 = false, inf2 = false;
  if (!read_g2_affine(input.data, &q1, &inf1)) return PRE_INVALID_INPUT;
  if (!read_g2_affine(input.data + 256, &q2, &inf2)) return PRE_INVALID_INPUT;

  blst_p2        r     = {0};
  blst_p2_affine r_aff = {0};
  bool           r_inf = false;

  if (inf1 && inf2) {
    r_inf = true;
  }
  else if (inf1) {
    blst_p2_from_affine(&r, &q2);
  }
  else if (inf2) {
    blst_p2_from_affine(&r, &q1);
  }
  else {
    blst_p2_from_affine(&r, &q1);
    blst_p2_add_affine(&r, &r, &q2);
  }

  if (!inf1 || !inf2) {
    if (blst_p2_is_inf(&r)) {
      r_inf = true;
    }
    else {
      blst_p2_to_affine(&r_aff, &r);
    }
  }

  buffer_reset(output);
  buffer_grow(output, 256);
  output->data.len = 256;
  write_g2_affine(&r_aff, output->data.data, r_inf);
  return PRE_SUCCESS;
}

// MSM gas discount as per EIP-2537: discount(k) scaled by 1000
// Table for k = 1..128. For k > 128 use 519.
static const uint16_t MSM_DISCOUNT_TABLE[129] = {
  0,
  1000, 949, 848, 797, 764, 750, 738, 728, 719, 712,
  705, 698, 692, 687, 682, 677, 673, 669, 665, 661,
  658, 654, 651, 648, 645, 642, 640, 637, 635, 632,
  630, 627, 625, 623, 621, 619, 617, 615, 613, 611,
  609, 608, 606, 604, 603, 601, 599, 598, 596, 595,
  593, 592, 591, 589, 588, 586, 585, 584, 582, 581,
  580, 579, 577, 576, 575, 574, 573, 572, 570, 569,
  568, 567, 566, 565, 564, 563, 562, 561, 560, 559,
  558, 557, 556, 555, 554, 553, 552, 551, 550, 549,
  548, 547, 547, 546, 545, 544, 543, 542, 541, 540,
  540, 539, 538, 537, 536, 536, 535, 534, 533, 532,
  532, 531, 530, 529, 528, 528, 527, 526, 525, 525,
  524, 523, 522, 522, 521, 520, 520, 519
};

static inline uint32_t msm_discount_factor(uint32_t k) {
  if (k == 0) return 0;
  if (k <= 128) return MSM_DISCOUNT_TABLE[k];
  return 519;
}

// 0x0c: G1MSM
static pre_result_t pre_bls12_g1msm(bytes_t input, buffer_t* output, uint64_t* gas_used) {
  const uint32_t LEN_PER_PAIR = 160; // 32 (scalar) + 128 (point)
  if (input.len < LEN_PER_PAIR || (input.len % LEN_PER_PAIR) != 0) return PRE_INVALID_INPUT;
  uint32_t k = (uint32_t) (input.len / LEN_PER_PAIR);

  // Gas per EIP-2537: gas = (k * multiplication_cost * discount(k)) // 1000
  const uint64_t multiplication_cost = 12000; // G1
  const uint32_t disc                = msm_discount_factor(k);
  *gas_used                          = ((uint64_t) k * multiplication_cost * disc) / 1000u;

  // Allocate arrays
  blst_p1_affine* points_store  = (blst_p1_affine*) safe_calloc(k, sizeof(blst_p1_affine));
  byte*           scalars_store = (byte*) safe_calloc(k, 32);
  // We'll compact into these arrays to avoid NULLs
  const blst_p1_affine** points  = (const blst_p1_affine**) safe_calloc(k, sizeof(blst_p1_affine*));
  const byte**           scalars = (const byte**) safe_calloc(k, sizeof(byte*));
  uint32_t               m       = 0;

  for (uint32_t i = 0; i < k; i++) {
    const uint8_t* base = input.data + i * LEN_PER_PAIR;
    // read scalar (big-endian 32 bytes)
    memcpy(scalars_store + 32 * i, base, 32);
    bool inf = false;
    if (!read_g1_affine(base + 32, &points_store[i], &inf)) {
      safe_free(points);
      safe_free(points_store);
      safe_free(scalars);
      safe_free(scalars_store);
      return PRE_INVALID_INPUT;
    }
    // Skip pairs where scalar==0 or point is infinity
    if (inf || bytes_all_zero(bytes(scalars_store + 32 * i, 32))) continue;
    points[m]  = &points_store[i];
    scalars[m] = scalars_store + 32 * i;
    m++;
  }

  blst_p1 result = {0};
  if (m > 0) {
    // Use Pippenger on compacted arrays
    size_t  scratch_size = blst_p1s_mult_pippenger_scratch_sizeof(m);
    limb_t* scratch      = (limb_t*) safe_calloc(scratch_size, 1);
    blst_p1s_mult_pippenger(&result, points, m, scalars, 256, scratch);
    safe_free(scratch);
  }
  else {
    // result remains infinity
  }

  // Serialize result
  buffer_reset(output);
  buffer_grow(output, 128);
  output->data.len = 128;
  if (blst_p1_is_inf(&result)) {
    memset(output->data.data, 0, 128);
  }
  else {
    blst_p1_affine aff;
    blst_p1_to_affine(&aff, &result);
    write_g1_affine(&aff, output->data.data, false);
  }

  safe_free(points_store);
  safe_free(scalars_store);
  safe_free(points);
  safe_free(scalars);
  return PRE_SUCCESS;
}

// 0x0e: G2MSM
static pre_result_t pre_bls12_g2msm(bytes_t input, buffer_t* output, uint64_t* gas_used) {
  const uint32_t LEN_PER_PAIR = 288; // 32 (scalar) + 256 (point)
  if (input.len < LEN_PER_PAIR || (input.len % LEN_PER_PAIR) != 0) return PRE_INVALID_INPUT;
  uint32_t k = (uint32_t) (input.len / LEN_PER_PAIR);

  const uint64_t multiplication_cost = 22500; // G2
  const uint32_t disc                = msm_discount_factor(k);
  *gas_used                          = ((uint64_t) k * multiplication_cost * disc) / 1000u;

  blst_p2_affine*        points_store  = (blst_p2_affine*) safe_calloc(k, sizeof(blst_p2_affine));
  byte*                  scalars_store = (byte*) safe_calloc(k, 32);
  const blst_p2_affine** points        = (const blst_p2_affine**) safe_calloc(k, sizeof(blst_p2_affine*));
  const byte**           scalars       = (const byte**) safe_calloc(k, sizeof(byte*));
  uint32_t               m             = 0;

  for (uint32_t i = 0; i < k; i++) {
    const uint8_t* base = input.data + i * LEN_PER_PAIR;
    memcpy(scalars_store + 32 * i, base, 32);
    bool inf = false;
    if (!read_g2_affine(base + 32, &points_store[i], &inf)) {
      safe_free(points_store);
      safe_free(scalars_store);
      safe_free(points);
      safe_free(scalars);
      return PRE_INVALID_INPUT;
    }
    if (inf || bytes_all_zero(bytes(scalars_store + 32 * i, 32))) continue;
    points[m]  = &points_store[i];
    scalars[m] = scalars_store + 32 * i;
    m++;
  }

  blst_p2 result = {0};
  if (m > 0) {
    size_t  scratch_size = blst_p2s_mult_pippenger_scratch_sizeof(m);
    limb_t* scratch      = (limb_t*) safe_calloc(scratch_size, 1);
    blst_p2s_mult_pippenger(&result, points, m, scalars, 256, scratch);
    safe_free(scratch);
  }
  else {
    // result remains infinity
  }

  buffer_reset(output);
  buffer_grow(output, 256);
  output->data.len = 256;
  if (blst_p2_is_inf(&result)) {
    memset(output->data.data, 0, 256);
  }
  else {
    blst_p2_affine aff;
    blst_p2_to_affine(&aff, &result);
    write_g2_affine(&aff, output->data.data, false);
  }

  safe_free(points_store);
  safe_free(scalars_store);
  safe_free(points);
  safe_free(scalars);
  return PRE_SUCCESS;
}

// 0x0f: Pairing check
static pre_result_t pre_bls12_pairing_check(bytes_t input, buffer_t* output, uint64_t* gas_used) {
  const uint32_t LEN_PER_PAIR = 384; // 128 (G1) + 256 (G2)
  if ((input.len % LEN_PER_PAIR) != 0) return PRE_INVALID_INPUT;
  uint32_t k = (uint32_t) (input.len / LEN_PER_PAIR);
  *gas_used  = 37700 + 32600 * (uint64_t) k;

  buffer_reset(output);
  buffer_grow(output, 32);
  output->data.len = 32;

  if (k == 0) {
    // e() = 1
    memset(output->data.data, 0, 31);
    output->data.data[31] = 1;
    return PRE_SUCCESS;
  }

  blst_p1_affine*        Ps_store = (blst_p1_affine*) safe_calloc(k, sizeof(blst_p1_affine));
  blst_p2_affine*        Qs_store = (blst_p2_affine*) safe_calloc(k, sizeof(blst_p2_affine));
  const blst_p1_affine** Ps       = (const blst_p1_affine**) safe_calloc(k, sizeof(blst_p1_affine*));
  const blst_p2_affine** Qs       = (const blst_p2_affine**) safe_calloc(k, sizeof(blst_p2_affine*));
  uint32_t               m        = 0;

  for (uint32_t i = 0; i < k; i++) {
    const uint8_t* base = input.data + i * LEN_PER_PAIR;
    bool           pinf = false, qinf = false;
    if (!read_g1_affine(base, &Ps_store[i], &pinf)) {
      safe_free(Ps);
      safe_free(Qs);
      safe_free(Ps_store);
      safe_free(Qs_store);
      return PRE_INVALID_INPUT;
    }
    if (!read_g2_affine(base + 128, &Qs_store[i], &qinf)) {
      safe_free(Ps);
      safe_free(Qs);
      safe_free(Ps_store);
      safe_free(Qs_store);
      return PRE_INVALID_INPUT;
    }
    if (pinf || qinf) continue;
    Ps[m] = &Ps_store[i];
    Qs[m] = &Qs_store[i];
    m++;
  }

  bool ok = true;
  if (m > 0) {
    blst_fp12 f = {0};
    blst_miller_loop_n(&f, Qs, Ps, m);
    blst_final_exp(&f, &f);
    ok = blst_fp12_is_one(&f);
  }
  else {
    ok = true; // neutral element => true
  }

  memset(output->data.data, 0, 31);
  output->data.data[31] = ok ? 1 : 0;

  safe_free(Ps);
  safe_free(Qs);
  safe_free(Ps_store);
  safe_free(Qs_store);
  return PRE_SUCCESS;
}

// 0x10: MAP FP -> G1
static pre_result_t pre_bls12_map_fp_to_g1(bytes_t input, buffer_t* output, uint64_t* gas_used) {
  *gas_used = 5500;
  if (input.len != 64) return PRE_INVALID_INPUT;
  blst_fp u;
  blst_fp_from_be64(&u, input.data);
  blst_p1 P;
  blst_map_to_g1(&P, &u, NULL);
  blst_p1_affine Pa;
  blst_p1_to_affine(&Pa, &P);

  buffer_reset(output);
  buffer_grow(output, 128);
  output->data.len = 128;
  write_g1_affine(&Pa, output->data.data, false);
  return PRE_SUCCESS;
}

// 0x11: MAP FP2 -> G2
static pre_result_t pre_bls12_map_fp2_to_g2(bytes_t input, buffer_t* output, uint64_t* gas_used) {
  *gas_used = 23800;
  if (input.len != 128) return PRE_INVALID_INPUT;
  blst_fp2 u;
  blst_fp_from_be64(&u.fp[0], input.data);
  blst_fp_from_be64(&u.fp[1], input.data + 64);
  blst_p2 Q;
  blst_map_to_g2(&Q, &u, NULL);
  blst_p2_affine Qa;
  blst_p2_to_affine(&Qa, &Q);

  buffer_reset(output);
  buffer_grow(output, 256);
  output->data.len = 256;
  write_g2_affine(&Qa, output->data.data, false);
  return PRE_SUCCESS;
}

#endif
