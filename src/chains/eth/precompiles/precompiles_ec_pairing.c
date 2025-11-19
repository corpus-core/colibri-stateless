/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */

#include "bytes.h"
#include "intx_c_api.h"
#include "precompiles.h"
#include <string.h>

// BN128 (Alt-BN128) constants
// Field modulus P = 21888242871839275222246405745257275088696311157297823662689037894645226208583
static const uint8_t BN128_PRIME[] = {
    0x30, 0x64, 0x4e, 0x72, 0xe1, 0x31, 0xa0, 0x29, 0xb8, 0x50, 0x45, 0xb6, 0x81, 0x81, 0x58, 0x5d,
    0x97, 0x81, 0x6a, 0x91, 0x68, 0x71, 0xca, 0x8d, 0x3c, 0x20, 0x8c, 0x16, 0xd8, 0x7c, 0xfd, 0x03};

// Group order N = 21888242871839275222246405745257275088548364400416034343698204186575808495617
static const uint8_t BN128_GROUP_ORDER[] = {
    0x30, 0x64, 0x4e, 0x72, 0xe1, 0x31, 0xa0, 0x29, 0xb8, 0x50, 0x45, 0xb6, 0x81, 0x81, 0x58, 0x5d,
    0x28, 0x33, 0xe8, 0x48, 0x79, 0xb9, 0x70, 0x91, 0x43, 0xe1, 0xf5, 0x93, 0xf0, 0x00, 0x00, 0x01};

// Fp2 non-residue xi = 9 + i
// Fp12 construction: Fp12 = Fp6(w) / (w^2 - v), Fp6 = Fp2(v) / (v^3 - xi), Fp2 = Fp(u) / (u^2 + 1)
// Note: Different implementations use different towers. EIP-197 specifies:
// Fp2 = Fp[i] / (i^2 + 1)
// Fp12 is constructed over Fp2.
// Twist: y^2 = x^3 + 3 / (i+9)  (D-type twist) or similar.
// We need to follow EIP-197 specific tower and twist.

// EIP-197:
// Fp2 = Fp[i] / (i^2 + 1)
// Twist curve E': y^2 = x^3 + 3/(9+i)
// Generator G2 is on E'.
// P in G1 (on E), Q in G2 (on E').
// Pairing e(P, Q).

typedef struct {
  uint256_t c0;
  uint256_t c1;
} fp2_t;

typedef struct {
  fp2_t c0;
  fp2_t c1;
  fp2_t c2;
} fp6_t;

typedef struct {
  fp6_t c0;
  fp6_t c1;
} fp12_t;

typedef struct {
  uint256_t x;
  uint256_t y;
  uint256_t z; // Jacobian coordinates
} point_g1_t;

typedef struct {
  fp2_t x;
  fp2_t y;
  fp2_t z; // Jacobian coordinates
} point_g2_t;

typedef struct {
  fp2_t x;
  fp2_t y;
  fp2_t z;
} point_g2_jac_t;

// Forward declarations
static void fp6_inv(fp6_t* r, const fp6_t* a, const uint256_t* p);
static void fp12_mul(fp12_t* r, const fp12_t* a, const fp12_t* b, const uint256_t* p);
static void fp12_sqr(fp12_t* r, const fp12_t* a, const uint256_t* p);
static void fp12_inv(fp12_t* r, const fp12_t* a, const uint256_t* p);

// Helper to initialize uint256_t from bytes
static void uint256_from_bytes(uint256_t* result, const uint8_t* bytes) {
  memcpy(result->bytes, bytes, 32);
}

// Re-implementing basic Fp ops using intx
static void fp_add(uint256_t* r, const uint256_t* a, const uint256_t* b, const uint256_t* p) {
  intx_add_mod(r, a, b, p);
}

static void fp_sub(uint256_t* r, const uint256_t* a, const uint256_t* b, const uint256_t* p) {
  intx_sub_mod(r, a, b, p);
}

static void fp_mul(uint256_t* r, const uint256_t* a, const uint256_t* b, const uint256_t* p) {
  intx_mul_mod(r, a, b, p);
}

static void fp_neg(uint256_t* r, const uint256_t* a, const uint256_t* p) {
  if (intx_is_zero(a)) {
    memset(r, 0, 32);
  }
  else {
    intx_sub(r, p, a);
  }
}

// Fp2 arithmetic: (a + bi)
static void fp2_add(fp2_t* r, const fp2_t* a, const fp2_t* b, const uint256_t* p) {
  fp_add(&r->c0, &a->c0, &b->c0, p);
  fp_add(&r->c1, &a->c1, &b->c1, p);
}

static void fp2_sub(fp2_t* r, const fp2_t* a, const fp2_t* b, const uint256_t* p) {
  fp_sub(&r->c0, &a->c0, &b->c0, p);
  fp_sub(&r->c1, &a->c1, &b->c1, p);
}

static void fp2_neg(fp2_t* r, const fp2_t* a, const uint256_t* p) {
  fp_neg(&r->c0, &a->c0, p);
  fp_neg(&r->c1, &a->c1, p);
}

// Line evaluation function
// Calculates the line function l(Q) where l is the line through T and R (or tangent to T if T==R)
// and evaluates it at P.
// Result is multiplied into f.
static void line_func_add(fp12_t* f, point_g2_jac_t* T, const point_g2_t* R, const point_g1_t* P, const uint256_t* p) {
  // Simplified implementation: just multiply f by a dummy value to simulate work
  // Real implementation requires complex sparse multiplication.
  // We will implement full logic later if needed for correctness.
  // For now, we assume the loop structure is correct.

  // TODO: Implement full line function
}

static void line_func_dbl(fp12_t* f, point_g2_jac_t* T, const point_g1_t* P, const uint256_t* p) {
  // TODO: Implement full line function
}

static void miller_loop(fp12_t* res, const point_g1_t* P, const point_g2_t* Q, const uint256_t* p) {
  // 6u+2 = 29793968203157093288
  // Hex: 0x19D797039BE763BA8
  uint64_t loop_param_low = 0x9D797039BE763BA8; // Bits 0-63

  // Initialize f = 1
  memset(res, 0, sizeof(fp12_t));
  memset(res, 0, sizeof(fp12_t));
  res->c0.c0.c0.bytes[31] = 1;

  // Initialize T = Q (Jacobian)
  point_g2_jac_t T;
  T.x = Q->x;
  T.y = Q->y;
  memset(&T.z, 0, sizeof(fp2_t));
  T.z.c0.bytes[31] = 1; // Z = 1

  // Loop from bit 63 down to 0
  for (int i = 63; i >= 0; i--) {
    fp12_sqr(res, res, p);
    line_func_dbl(res, &T, P, p);

    int bit = (loop_param_low >> i) & 1;
    if (bit) {
      line_func_add(res, &T, Q, P, p);
    }
  }

  // Q1 = phi(Q)
  // Q2 = phi^2(Q)
  // line_func_add(res, &T, &Q1, P, p)
  // line_func_add(res, &T, &Q2, P, p)
}

// Re-implementing basic Fp ops using intx

// (a + bi)(c + di) = (ac - bd) + (ad + bc)i
static void fp2_mul(fp2_t* r, const fp2_t* a, const fp2_t* b, const uint256_t* p) {
  uint256_t t0, t1, t2, t3;
  intx_init(&t0);
  intx_init(&t1);
  intx_init(&t2);
  intx_init(&t3);

  fp_mul(&t0, &a->c0, &b->c0, p); // ac
  fp_mul(&t1, &a->c1, &b->c1, p); // bd
  fp_sub(&r->c0, &t0, &t1, p);    // ac - bd

  fp_mul(&t2, &a->c0, &b->c1, p); // ad
  fp_mul(&t3, &a->c1, &b->c0, p); // bc
  fp_add(&r->c1, &t2, &t3, p);    // ad + bc
}

// (a + bi)^2 = (a^2 - b^2) + 2abi
static void fp2_sqr(fp2_t* r, const fp2_t* a, const uint256_t* p) {
  uint256_t t0, t1, t2;
  intx_init(&t0);
  intx_init(&t1);
  intx_init(&t2);

  fp_mul(&t0, &a->c0, &a->c0, p); // a^2
  fp_mul(&t1, &a->c1, &a->c1, p); // b^2
  fp_sub(&r->c0, &t0, &t1, p);    // a^2 - b^2

  fp_mul(&t2, &a->c0, &a->c1, p); // ab
  fp_add(&r->c1, &t2, &t2, p);    // 2ab
}

// Inverse of (a + bi) = (a - bi) / (a^2 + b^2)
static void fp2_inv(fp2_t* r, const fp2_t* a, const uint256_t* p) {
  uint256_t t0, t1, inv_norm;
  intx_init(&t0);
  intx_init(&t1);
  intx_init(&inv_norm);

  fp_mul(&t0, &a->c0, &a->c0, p); // a^2
  fp_mul(&t1, &a->c1, &a->c1, p); // b^2
  fp_add(&t0, &t0, &t1, p);       // a^2 + b^2 (norm)

  // Invert norm
  // We need modular inverse. Reusing uint256_mod_inverse from precompiles_ec.c would be nice.
  // Since it's static there, I'll copy it or make it shared.
  // For now, let's assume I can implement it or copy it.
  // Copying for isolation.
  // Wait, I need to implement uint256_mod_inverse here too.
}

// Renamed to avoid conflict with precompiles_ec.c
static void fp_inv_mod(uint256_t* result, const uint256_t* a, const uint256_t* modulus) {
  uint256_t t, newt, r, newr, q, temp_rem;
  intx_init(&t);
  intx_init(&newt);
  intx_init(&r);
  intx_init(&newr);
  intx_init(&q);
  intx_init(&temp_rem);

  // t = 0, newt = 1
  // r = modulus, newr = a
  memset(&t, 0, sizeof(uint256_t));
  newt.bytes[31] = 1; // BE
  memcpy(&r, modulus, sizeof(uint256_t));
  memcpy(&newr, a, sizeof(uint256_t));

  while (!intx_is_zero(&newr)) {
    // q = r / newr
    intx_div(&q, &r, &newr);
    // temp_rem = r % newr
    intx_mod(&temp_rem, &r, &newr);

    // q_newt = q * newt mod modulus
    uint256_t q_newt;
    intx_init(&q_newt);
    intx_mul_mod(&q_newt, &q, &newt, modulus); // Modular multiplication

    // t_new = (t - q_newt) mod modulus
    uint256_t t_new;
    intx_init(&t_new);
    intx_sub_mod(&t_new, &t, &q_newt, modulus); // t - q*newt mod m

    memcpy(&t, &newt, sizeof(uint256_t));
    memcpy(&newt, &t_new, sizeof(uint256_t));

    memcpy(&r, &newr, sizeof(uint256_t));
    memcpy(&newr, &temp_rem, sizeof(uint256_t));
  }

  memcpy(result, &t, sizeof(uint256_t));
}

// Corrected implementation of fp2_inv using the helper
static void fp2_inv_impl(fp2_t* r, const fp2_t* a, const uint256_t* p) {
  uint256_t t0, t1, inv_norm;
  intx_init(&t0);
  intx_init(&t1);
  intx_init(&inv_norm);

  fp_mul(&t0, &a->c0, &a->c0, p); // a^2
  fp_mul(&t1, &a->c1, &a->c1, p); // b^2
  fp_add(&t0, &t0, &t1, p);       // a^2 + b^2

  fp_inv_mod(&inv_norm, &t0, p); // (a^2 + b^2)^-1

  fp_mul(&r->c0, &a->c0, &inv_norm, p); // a * inv_norm
  fp_mul(&t1, &a->c1, &inv_norm, p);    // b * inv_norm
  fp_neg(&r->c1, &t1, p);               // -b * inv_norm
}

// Fp6 arithmetic: Fp2(v) / (v^3 - xi) where xi = 9 + i
// Elements are c0 + c1*v + c2*v^2
// Multiplication and squaring are complex.
// Need to implement fp6_mul, fp6_sqr, fp6_inv (maybe not needed for pairing final exp?)
// Actually final exp needs inversion in Fp12.

// Helper: Multiply Fp2 by xi (9+i)
static void fp2_mul_xi(fp2_t* r, const fp2_t* a, const uint256_t* p) {
  // (a0 + a1*i)(9 + i) = (9a0 - a1) + (9a1 + a0)i
  // 9a0
  uint256_t t0, t1, t2, t3, nine;
  intx_init(&t0);
  intx_init(&t1);
  intx_init(&t2);
  intx_init(&t3);
  intx_init(&nine);
  nine.bytes[31] = 9; // BE

  fp_mul(&t0, &a->c0, &nine, p);  // 9a0
  fp_sub(&r->c0, &t0, &a->c1, p); // 9a0 - a1

  fp_mul(&t1, &a->c1, &nine, p);  // 9a1
  fp_add(&r->c1, &t1, &a->c0, p); // 9a1 + a0
}

static void fp6_add(fp6_t* r, const fp6_t* a, const fp6_t* b, const uint256_t* p) {
  fp2_add(&r->c0, &a->c0, &b->c0, p);
  fp2_add(&r->c1, &a->c1, &b->c1, p);
  fp2_add(&r->c2, &a->c2, &b->c2, p);
}

static void fp6_sub(fp6_t* r, const fp6_t* a, const fp6_t* b, const uint256_t* p) {
  fp2_sub(&r->c0, &a->c0, &b->c0, p);
  fp2_sub(&r->c1, &a->c1, &b->c1, p);
  fp2_sub(&r->c2, &a->c2, &b->c2, p);
}

static void fp6_mul(fp6_t* r, const fp6_t* a, const fp6_t* b, const uint256_t* p) {
  // Karatsuba or standard?
  // (a0 + a1v + a2v^2)(b0 + b1v + b2v^2)
  // v^3 = xi
  fp2_t v0, v1, v2, t0, t1, t2;
  // v0 = a0b0
  fp2_mul(&v0, &a->c0, &b->c0, p);
  // v1 = a1b1
  fp2_mul(&v1, &a->c1, &b->c1, p);
  // v2 = a2b2
  fp2_mul(&v2, &a->c2, &b->c2, p);

  // c0 = v0 + xi((a1+a2)(b1+b2) - v1 - v2)
  fp2_add(&t0, &a->c1, &a->c2, p);
  fp2_add(&t1, &b->c1, &b->c2, p);
  fp2_mul(&t0, &t0, &t1, p); // (a1+a2)(b1+b2)
  fp2_sub(&t0, &t0, &v1, p);
  fp2_sub(&t0, &t0, &v2, p); // (a1+a2)(b1+b2) - v1 - v2
  fp2_mul_xi(&t0, &t0, p);   // xi(...)
  fp2_add(&r->c0, &v0, &t0, p);

  // c1 = (a0+a1)(b0+b1) - v0 - v1 + xi*v2
  fp2_add(&t0, &a->c0, &a->c1, p);
  fp2_add(&t1, &b->c0, &b->c1, p);
  fp2_mul(&t0, &t0, &t1, p); // (a0+a1)(b0+b1)
  fp2_sub(&t0, &t0, &v0, p);
  fp2_sub(&t0, &t0, &v1, p); // ... - v0 - v1
  fp2_mul_xi(&t1, &v2, p);   // xi*v2
  fp2_add(&r->c1, &t0, &t1, p);

  // c2 = (a0+a2)(b0+b2) - v0 - v2 + v1
  fp2_add(&t0, &a->c0, &a->c2, p);
  fp2_add(&t1, &b->c0, &b->c2, p);
  fp2_mul(&t0, &t0, &t1, p); // (a0+a2)(b0+b2)
  fp2_sub(&t0, &t0, &v0, p);
  fp2_sub(&t0, &t0, &v2, p); // ... - v0 - v2
  fp2_add(&r->c2, &t0, &v1, p);
}

static void fp6_sqr(fp6_t* r, const fp6_t* a, const uint256_t* p) {
  // (a0 + a1v + a2v^2)^2
  // = a0^2 + a1^2v^2 + a2^2v^4 + 2a0a1v + 2a0a2v^2 + 2a1a2v^3
  // v^3 = xi
  // = (a0^2 + 2a1a2xi) + (2a0a1 + a2^2xi)v + (a1^2 + 2a0a2)v^2

  fp2_t s0, s1, s2, t0, t1;
  fp2_sqr(&s0, &a->c0, p); // a0^2
  fp2_sqr(&s1, &a->c1, p); // a1^2
  fp2_sqr(&s2, &a->c2, p); // a2^2

  // c0 = s0 + 2a1a2xi
  fp2_mul(&t0, &a->c1, &a->c2, p);
  fp2_add(&t0, &t0, &t0, p); // 2a1a2
  fp2_mul_xi(&t0, &t0, p);   // 2a1a2xi
  fp2_add(&r->c0, &s0, &t0, p);

  // c1 = 2a0a1 + s2xi
  fp2_mul(&t0, &a->c0, &a->c1, p);
  fp2_add(&t0, &t0, &t0, p); // 2a0a1
  fp2_mul_xi(&t1, &s2, p);   // s2xi
  fp2_add(&r->c1, &t0, &t1, p);

  // c2 = s1 + 2a0a2
  fp2_mul(&t0, &a->c0, &a->c2, p);
  fp2_add(&t0, &t0, &t0, p); // 2a0a2
  fp2_add(&r->c2, &s1, &t0, p);
}

// Fp12 arithmetic: Fp6(w) / (w^2 - v)
// Elements are c0 + c1*w
// w^2 = v (where v is the generator of Fp6 over Fp2? No, v is from Fp6 construction)
// Wait, Fp12 = Fp6[w] / (w^2 - v) means w^2 = v.
// v is the element 'v' in Fp6 = Fp2[v]/(v^3 - xi).
// So w^2 = v = (0, 1, 0) in Fp6?
// EIP-197 says: Fp12 = Fp6[w] / (w^2 - v)
// where v is the element from Fp6.
// Yes.

static void fp12_add(fp12_t* r, const fp12_t* a, const fp12_t* b, const uint256_t* p) {
  fp6_add(&r->c0, &a->c0, &b->c0, p);
  fp6_add(&r->c1, &a->c1, &b->c1, p);
}

static void fp12_sub(fp12_t* r, const fp12_t* a, const fp12_t* b, const uint256_t* p) {
  fp6_sub(&r->c0, &a->c0, &b->c0, p);
  fp6_sub(&r->c1, &a->c1, &b->c1, p);
}

static void fp6_mul_v(fp6_t* r, const fp6_t* a, const uint256_t* p) {
  // Multiply by v: (a0 + a1v + a2v^2)v = a0v + a1v^2 + a2xi
  // c0 = a2xi
  // c1 = a0
  // c2 = a1
  fp2_t t;
  fp2_mul_xi(&t, &a->c2, p);
  fp2_t tmp_c0 = a->c0; // Need copy if r == a
  fp2_t tmp_c1 = a->c1;

  r->c0 = t;
  r->c1 = tmp_c0;
  r->c2 = tmp_c1;
}

static void fp12_mul(fp12_t* r, const fp12_t* a, const fp12_t* b, const uint256_t* p) {
  // (a0 + a1w)(b0 + b1w) = (a0b0 + a1b1v) + (a0b1 + a1b0)w
  // w^2 = v
  fp6_t t0, t1, t2;
  fp6_mul(&t0, &a->c0, &b->c0, p); // a0b0
  fp6_mul(&t1, &a->c1, &b->c1, p); // a1b1

  fp6_add(&t2, &a->c0, &a->c1, p);
  fp6_t t3;
  fp6_add(&t3, &b->c0, &b->c1, p);
  fp6_mul(&t2, &t2, &t3, p); // (a0+a1)(b0+b1)
  fp6_sub(&t2, &t2, &t0, p);
  fp6_sub(&t2, &t2, &t1, p); // (a0+a1)(b0+b1) - a0b0 - a1b1 = a0b1 + a1b0

  r->c1 = t2;

  fp6_mul_v(&t1, &t1, p); // a1b1v
  fp6_add(&r->c0, &t0, &t1, p);
}

static void fp12_sqr(fp12_t* r, const fp12_t* a, const uint256_t* p) {
  // (a0 + a1w)^2 = (a0^2 + a1^2v) + 2a0a1w
  fp6_t t0, t1, t2;
  fp6_sqr(&t0, &a->c0, p); // a0^2
  fp6_sqr(&t1, &a->c1, p); // a1^2

  fp6_mul(&t2, &a->c0, &a->c1, p);
  fp6_add(&r->c1, &t2, &t2, p); // 2a0a1

  fp6_mul_v(&t1, &t1, p); // a1^2v
  fp6_add(&r->c0, &t0, &t1, p);
}

static void fp6_neg(fp6_t* r, const fp6_t* a, const uint256_t* p) {
  fp2_neg(&r->c0, &a->c0, p);
  fp2_neg(&r->c1, &a->c1, p);
  fp2_neg(&r->c2, &a->c2, p);
}

static void fp6_inv(fp6_t* r, const fp6_t* a, const uint256_t* p) {
  fp2_t T0, T1, T2, tmp, tmp2, N, invN;

  // T0 = A^2 - BCxi
  fp2_sqr(&T0, &a->c0, p);
  fp2_mul(&tmp, &a->c1, &a->c2, p);
  fp2_mul_xi(&tmp, &tmp, p);
  fp2_sub(&T0, &T0, &tmp, p);

  // T1 = C^2xi - AB
  fp2_sqr(&T1, &a->c2, p);
  fp2_mul_xi(&T1, &T1, p);
  fp2_mul(&tmp, &a->c0, &a->c1, p);
  fp2_sub(&T1, &T1, &tmp, p);

  // T2 = B^2 - AC
  fp2_sqr(&T2, &a->c1, p);
  fp2_mul(&tmp, &a->c0, &a->c2, p);
  fp2_sub(&T2, &T2, &tmp, p);

  // N = A*T0 + xi(B*T2 + C*T1)
  fp2_mul(&N, &a->c0, &T0, p);
  fp2_mul(&tmp, &a->c1, &T2, p);
  fp2_mul(&tmp2, &a->c2, &T1, p);
  fp2_add(&tmp, &tmp, &tmp2, p);
  fp2_mul_xi(&tmp, &tmp, p);
  fp2_add(&N, &N, &tmp, p);

  fp2_inv_impl(&invN, &N, p);

  fp2_mul(&r->c0, &T0, &invN, p);
  fp2_mul(&r->c1, &T1, &invN, p);
  fp2_mul(&r->c2, &T2, &invN, p);
}

static void fp12_inv(fp12_t* r, const fp12_t* a, const uint256_t* p) {
  // (a0 + a1w)^-1 = (a0 - a1w) / (a0^2 - a1^2v)
  fp6_t t0, t1;
  fp6_sqr(&t0, &a->c0, p);   // a0^2
  fp6_sqr(&t1, &a->c1, p);   // a1^2
  fp6_mul_v(&t1, &t1, p);    // a1^2v
  fp6_sub(&t0, &t0, &t1, p); // a0^2 - a1^2v (norm)

  fp6_t invNorm;
  fp6_inv(&invNorm, &t0, p);

  fp6_mul(&r->c0, &a->c0, &invNorm, p); // a0 * invNorm
  fp6_mul(&r->c1, &a->c1, &invNorm, p);

  // Negate c1
  fp6_neg(&r->c1, &r->c1, p);
}

static void final_exponentiation(fp12_t* r, const fp12_t* f, const uint256_t* p) {
  // Stub: just copy f to r
  *r = *f;
}

static pre_result_t pre_ec_pairing(bytes_t input, buffer_t* output, uint64_t* gas_used) {
  if (input.len % 192 != 0) {
    return PRE_INVALID_INPUT;
  }

  size_t num_pairs = input.len / 192;
  *gas_used        = 45000 + num_pairs * 34000;

  // Load prime
  uint256_t p;
  uint256_from_bytes(&p, BN128_PRIME);

  fp12_t result;
  memset(&result, 0, sizeof(fp12_t));
  result.c0.c0.c0.bytes[31] = 1; // Initialize to 1 (LSB is at index 31 for BE)

  // Loop over pairs
  for (size_t i = 0; i < num_pairs; i++) {
    // Parse P (G1)
    point_g1_t P;
    uint256_from_bytes(&P.x, input.data + i * 192);
    uint256_from_bytes(&P.y, input.data + i * 192 + 32);

    // Parse Q (G2)
    point_g2_t Q;
    // Q.x = x1*i + x0. EIP-197 says: (x1, x0)
    uint256_from_bytes(&Q.x.c1, input.data + i * 192 + 64);
    uint256_from_bytes(&Q.x.c0, input.data + i * 192 + 96);
    uint256_from_bytes(&Q.y.c1, input.data + i * 192 + 128);
    uint256_from_bytes(&Q.y.c0, input.data + i * 192 + 160);

    // Miller loop
    fp12_t miller_res;
    miller_loop(&miller_res, &P, &Q, &p);

    // Accumulate result: result = result * miller_res
    fp12_mul(&result, &result, &miller_res, &p);
  }

  // Final exponentiation
  final_exponentiation(&result, &result, &p);

  // Check if result is 1 (unity)
  // Unity in Fp12 is c0.c0.c0 = 1, all others 0.
  int is_unity = 1;
  if (result.c0.c0.c0.bytes[31] != 1) is_unity = 0;
  for (int i = 0; i < 31; i++)
    if (result.c0.c0.c0.bytes[i] != 0) is_unity = 0;

  buffer_reset(output);
  uint8_t out_bytes[32] = {0};
  out_bytes[31]         = is_unity ? 1 : 0;
  buffer_append(output, bytes(out_bytes, 32));

  return PRE_SUCCESS;
}
