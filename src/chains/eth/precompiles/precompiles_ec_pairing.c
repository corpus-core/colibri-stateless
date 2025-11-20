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
    0x97, 0x81, 0x6a, 0x91, 0x68, 0x71, 0xca, 0x8d, 0x3c, 0x20, 0x8c, 0x16, 0xd8, 0x7c, 0xfd, 0x47};

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

// Modular inverse helper
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

// (a + bi)(c + di) = (ac - bd) + (ad + bc)i
static void fp2_mul(fp2_t* r, const fp2_t* a, const fp2_t* b, const uint256_t* p) {
  uint256_t t0, t1, t2, t3;
  uint256_t c0, c1; // Temporaries for result
  intx_init(&t0);
  intx_init(&t1);
  intx_init(&t2);
  intx_init(&t3);
  intx_init(&c0);
  intx_init(&c1);

  fp_mul(&t0, &a->c0, &b->c0, p); // ac
  fp_mul(&t1, &a->c1, &b->c1, p); // bd
  fp_sub(&c0, &t0, &t1, p);       // ac - bd

  fp_mul(&t2, &a->c0, &b->c1, p); // ad
  fp_mul(&t3, &a->c1, &b->c0, p); // bc
  fp_add(&c1, &t2, &t3, p);       // ad + bc

  memcpy(&r->c0, &c0, sizeof(uint256_t));
  memcpy(&r->c1, &c1, sizeof(uint256_t));
}

// (a + bi)^2 = (a^2 - b^2) + 2abi
static void fp2_sqr(fp2_t* r, const fp2_t* a, const uint256_t* p) {
  uint256_t t0, t1, t2;
  uint256_t c0, c1; // Temporaries for result
  intx_init(&t0);
  intx_init(&t1);
  intx_init(&t2);
  intx_init(&c0);
  intx_init(&c1);

  fp_mul(&t0, &a->c0, &a->c0, p); // a^2
  fp_mul(&t1, &a->c1, &a->c1, p); // b^2
  fp_sub(&c0, &t0, &t1, p);       // a^2 - b^2

  fp_mul(&t2, &a->c0, &a->c1, p); // ab
  fp_add(&c1, &t2, &t2, p);       // 2ab

  memcpy(&r->c0, &c0, sizeof(uint256_t));
  memcpy(&r->c1, &c1, sizeof(uint256_t));
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

// Inverse of (a + bi) = (a - bi) / (a^2 + b^2)
static void fp2_inv(fp2_t* r, const fp2_t* a, const uint256_t* p) {
  fp2_inv_impl(r, a, p);
}

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

  fp_mul(&t0, &a->c0, &nine, p); // 9a0
  fp_sub(&t2, &t0, &a->c1, p);   // 9a0 - a1 (store in t2)

  fp_mul(&t1, &a->c1, &nine, p); // 9a1
  fp_add(&t3, &t1, &a->c0, p);   // 9a1 + a0 (store in t3)

  memcpy(&r->c0, &t2, sizeof(uint256_t));
  memcpy(&r->c1, &t3, sizeof(uint256_t));
}

// Fp6 arithmetic
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

static void fp6_neg(fp6_t* r, const fp6_t* a, const uint256_t* p) {
  fp2_neg(&r->c0, &a->c0, p);
  fp2_neg(&r->c1, &a->c1, p);
  fp2_neg(&r->c2, &a->c2, p);
}

static void fp6_mul(fp6_t* r, const fp6_t* a, const fp6_t* b, const uint256_t* p) {
  // Karatsuba or standard?
  // (a0 + a1v + a2v^2)(b0 + b1v + b2v^2)
  // v^3 = xi
  fp2_t v0, v1, v2, t0, t1, t2;
  fp2_t c0, c1, c2; // Temporaries for result

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
  fp2_add(&c0, &v0, &t0, p);

  // c1 = (a0+a1)(b0+b1) - v0 - v1 + xi*v2
  fp2_add(&t0, &a->c0, &a->c1, p);
  fp2_add(&t1, &b->c0, &b->c1, p);
  fp2_mul(&t0, &t0, &t1, p); // (a0+a1)(b0+b1)
  fp2_sub(&t0, &t0, &v0, p);
  fp2_sub(&t0, &t0, &v1, p); // ... - v0 - v1
  fp2_mul_xi(&t1, &v2, p);   // xi*v2
  fp2_add(&c1, &t0, &t1, p);

  // c2 = (a0+a2)(b0+b2) - v0 - v2 + v1
  fp2_add(&t0, &a->c0, &a->c2, p);
  fp2_add(&t1, &b->c0, &b->c2, p);
  fp2_mul(&t0, &t0, &t1, p); // (a0+a2)(b0+b2)
  fp2_sub(&t0, &t0, &v0, p);
  fp2_sub(&t0, &t0, &v2, p); // ... - v0 - v2
  fp2_add(&c2, &t0, &v1, p);

  r->c0 = c0;
  r->c1 = c1;
  r->c2 = c2;
}

static void fp6_sqr(fp6_t* r, const fp6_t* a, const uint256_t* p) {
  // (a0 + a1v + a2v^2)^2
  // = a0^2 + a1^2v^2 + a2^2v^4 + 2a0a1v + 2a0a2v^2 + 2a1a2v^3
  // v^3 = xi
  // = (a0^2 + 2a1a2xi) + (2a0a1 + a2^2xi)v + (a1^2 + 2a0a2)v^2

  fp2_t s0, s1, s2, t0, t1;
  fp2_t c0, c1, c2; // Temporaries for result

  fp2_sqr(&s0, &a->c0, p); // a0^2
  fp2_sqr(&s1, &a->c1, p); // a1^2
  fp2_sqr(&s2, &a->c2, p); // a2^2

  // c0 = s0 + 2a1a2xi
  fp2_mul(&t0, &a->c1, &a->c2, p);
  fp2_add(&t0, &t0, &t0, p); // 2a1a2
  fp2_mul_xi(&t0, &t0, p);   // 2a1a2xi
  fp2_add(&c0, &s0, &t0, p);

  // c1 = 2a0a1 + s2xi
  fp2_mul(&t0, &a->c0, &a->c1, p);
  fp2_add(&t0, &t0, &t0, p); // 2a0a1
  fp2_mul_xi(&t1, &s2, p);   // s2xi
  fp2_add(&c1, &t0, &t1, p);

  // c2 = s1 + 2a0a2
  fp2_mul(&t0, &a->c0, &a->c2, p);
  fp2_add(&t0, &t0, &t0, p); // 2a0a2
  fp2_add(&c2, &s1, &t0, p);

  r->c0 = c0;
  r->c1 = c1;
  r->c2 = c2;
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

// Fp12 arithmetic
static void fp12_add(fp12_t* r, const fp12_t* a, const fp12_t* b, const uint256_t* p) {
  fp6_add(&r->c0, &a->c0, &b->c0, p);
  fp6_add(&r->c1, &a->c1, &b->c1, p);
}

static void fp12_sub(fp12_t* r, const fp12_t* a, const fp12_t* b, const uint256_t* p) {
  fp6_sub(&r->c0, &a->c0, &b->c0, p);
  fp6_sub(&r->c1, &a->c1, &b->c1, p);
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

// Point doubling in Jacobian coordinates + Line evaluation
static void line_func_dbl(fp12_t* f, point_g2_jac_t* T, const point_g1_t* P, const uint256_t* p) {
  fp2_t t0, t1, t2, t3, t4, t5, t6;

  // t0 = T->x^2
  fp2_sqr(&t0, &T->x, p);
  // t1 = T->y^2
  fp2_sqr(&t1, &T->y, p);
  // t2 = t1^2
  fp2_sqr(&t2, &t1, p);

  // t3 = (T->x + t1)^2 - t0 - t2 = 2 * T->x * t1
  fp2_add(&t3, &T->x, &t1, p);
  fp2_sqr(&t3, &t3, p);
  fp2_sub(&t3, &t3, &t0, p);
  fp2_sub(&t3, &t3, &t2, p);
  fp2_add(&t3, &t3, &t3, p); // 2 * ...

  // t4 = 3 * t0
  fp2_add(&t4, &t0, &t0, p);
  fp2_add(&t4, &t4, &t0, p);

  // t6 = T->z^2
  fp2_sqr(&t6, &T->z, p);

  // T->x = t4^2 - 2 * t3
  fp2_sqr(&T->x, &t4, p);
  fp2_add(&t5, &t3, &t3, p);
  fp2_sub(&T->x, &T->x, &t5, p);

  // T->z = (T->y + T->z)^2 - t1 - t6
  fp2_add(&t5, &T->y, &T->z, p);
  fp2_sqr(&T->z, &t5, p);
  fp2_sub(&T->z, &T->z, &t1, p);
  fp2_sub(&T->z, &T->z, &t6, p);

  // T->y = t4 * (t3 - T->x) - 8 * t2
  fp2_sub(&t3, &t3, &T->x, p);
  fp2_mul(&T->y, &t4, &t3, p);
  fp2_add(&t2, &t2, &t2, p); // 2*t2
  fp2_add(&t2, &t2, &t2, p); // 4*t2
  fp2_add(&t2, &t2, &t2, p); // 8*t2
  fp2_sub(&T->y, &T->y, &t2, p);

  fp2_t A, B, C;
  // A = lambda_num * Z2^2
  fp2_sqr(&t6, &T->z, p);   // Z2^2
  fp2_mul(&A, &t4, &t6, p); // 3X2^2 * Z2^2

  // B = - lambda_den * Z2^3
  fp2_mul(&t5, &T->y, &T->z, p); // Y2 * Z2
  fp2_add(&t5, &t5, &t5, p);     // 2 * Y2 * Z2
  fp2_mul(&t5, &t5, &t6, p);     // 2 * Y2 * Z2^3
  fp2_neg(&B, &t5, p);

  // C = lambda_den * Y2 - lambda_num * X2
  fp2_mul(&t0, &t4, &T->x, p); // 3X2^3
  fp2_sqr(&t1, &T->y, p);      // Y2^2
  fp2_add(&t1, &t1, &t1, p);   // 2Y2^2
  fp2_sub(&C, &t1, &t0, p);    // 2Y2^2 - 3X2^3

  fp12_t l;
  memset(&l, 0, sizeof(fp12_t));

  // Correct construction for D-twist:
  // l = B*y + (A*x + C*v)*w
  // c0 = B*y
  // c1 = A*x + C*v -> c1.c0 = A*x, c1.c1 = C

  // l.c0.c0 = B * y
  fp2_t By;
  fp_mul(&By.c0, &B.c0, &P->y, p);
  fp_mul(&By.c1, &B.c1, &P->y, p);
  l.c0.c0 = By;

  // l.c1.c0 = A * x
  fp2_t Ax;
  fp_mul(&Ax.c0, &A.c0, &P->x, p);
  fp_mul(&Ax.c1, &A.c1, &P->x, p);
  l.c1.c0 = Ax;

  // l.c1.c1 = C
  l.c1.c1 = C;

  fp12_mul(f, f, &l, p);

  fp12_mul(f, f, &l, p);
}

// Point addition T = T + R
static void line_func_add(fp12_t* f, point_g2_jac_t* T, const point_g2_t* R, const point_g1_t* P, const uint256_t* p) {
  fp2_t Z2, Z3, num, den, lambda;
  fp2_sqr(&Z2, &T->z, p);
  fp2_mul(&Z3, &Z2, &T->z, p);

  fp2_mul(&num, &R->y, &Z3, p);
  fp2_sub(&num, &num, &T->y, p);

  fp2_mul(&den, &R->x, &Z2, p);
  fp2_sub(&den, &den, &T->x, p);

  if (intx_is_zero(&den.c0) && intx_is_zero(&den.c1)) {
    return;
  }

  fp2_inv(&lambda, &den, p);
  fp2_mul(&lambda, &lambda, &num, p);

  fp2_t X3, Y3, Z3_new;
  fp2_t H2, H3, U1H2, r2;
  fp2_sqr(&H2, &den, p);
  fp2_mul(&H3, &H2, &den, p);
  fp2_mul(&U1H2, &T->x, &H2, p);
  fp2_sqr(&r2, &num, p);

  fp2_sub(&X3, &r2, &H3, p);
  fp2_t tmp;
  fp2_add(&tmp, &U1H2, &U1H2, p);
  fp2_sub(&X3, &X3, &tmp, p);

  fp2_sub(&tmp, &U1H2, &X3, p);
  fp2_mul(&Y3, &num, &tmp, p);
  fp2_mul(&tmp, &T->y, &H3, p);
  fp2_sub(&Y3, &Y3, &tmp, p);

  fp2_mul(&Z3_new, &den, &T->z, p);

  T->x = X3;
  T->y = Y3;
  T->z = Z3_new;

  fp2_t C;
  fp2_mul(&tmp, &lambda, &R->x, p);
  fp2_sub(&C, &R->y, &tmp, p);

  fp12_t l;
  memset(&l, 0, sizeof(fp12_t));

  fp_neg(&l.c0.c0.c0, &P->y, p);

  fp_mul(&l.c1.c0.c0, &lambda.c0, &P->x, p);
  fp_mul(&l.c1.c0.c1, &lambda.c1, &P->x, p);

  l.c1.c1 = C;

  // Fix sign inconsistency with dbl: negate the w term (c1.c0)
  fp2_neg(&l.c1.c0, &l.c1.c0, p);

  fp12_mul(f, f, &l, p);

  fp12_mul(f, f, &l, p);
}

// Forward declaration
static void fp2_pow(fp2_t* r, const fp2_t* a, const uint256_t* exp, const uint256_t* p);

static void miller_loop(fp12_t* res, const point_g1_t* P, const point_g2_t* Q, const uint256_t* p) {
  // 6u+2 = 29793968203157093288
  // Hex: 0x19D797039BE763BA8 (65 bits)
  uint64_t loop_param[2] = {0x9D797039BE763BA8, 0x1};

  // Initialize f = 1
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

    int word_idx = i / 64;
    int bit_idx  = i % 64;
    int bit      = (loop_param[word_idx] >> bit_idx) & 1;

    if (bit) {
      line_func_add(res, &T, Q, P, p);
    }
  }

  // Q1 = phi(Q)
  // Q1.x = conj(Q.x) * xi^((p-1)/3)
  // Q1.y = conj(Q.y) * xi^((p-1)/2)

  // Compute constants
  fp2_t xi;
  memset(&xi, 0, sizeof(fp2_t));
  xi.c0.bytes[31] = 9;
  xi.c1.bytes[31] = 1;

  uint256_t p_val;
  memcpy(&p_val, p, sizeof(uint256_t));
  uint256_t one;
  memset(&one, 0, sizeof(uint256_t));
  one.bytes[31] = 1;
  uint256_t p_minus_1;
  intx_sub(&p_minus_1, &p_val, &one);

  uint256_t exp1, exp2, three, two;
  memset(&three, 0, sizeof(uint256_t));
  three.bytes[31] = 3;
  memset(&two, 0, sizeof(uint256_t));
  two.bytes[31] = 2;

  intx_div(&exp1, &p_minus_1, &three); // (p-1)/3
  intx_div(&exp2, &p_minus_1, &two);   // (p-1)/2

  fp2_t xi_p_3, xi_p_2;
  fp2_pow(&xi_p_3, &xi, &exp1, p);
  fp2_pow(&xi_p_2, &xi, &exp2, p);

  point_g2_t Q1;
  // Q1.x
  Q1.x = Q->x;
  fp_neg(&Q1.x.c1, &Q1.x.c1, p); // conj
  fp2_mul(&Q1.x, &Q1.x, &xi_p_3, p);

  // Q1.y
  Q1.y = Q->y;
  fp_neg(&Q1.y.c1, &Q1.y.c1, p); // conj
  fp2_mul(&Q1.y, &Q1.y, &xi_p_2, p);

  // Q2 = phi^2(Q)
  // Q2.x = Q.x * xi^((p^2-1)/3) ? No.
  // Q2.x = conj(Q1.x) * xi^((p-1)/3)
  //      = conj(conj(Q.x) * xi^((p-1)/3)) * xi^((p-1)/3)
  //      = Q.x * conj(xi^((p-1)/3)) * xi^((p-1)/3)
  //      = Q.x * |xi^((p-1)/3)|^2 ?
  // Actually simpler: Q2.x = Q.x * xi^((p-1)/3 * p) ? No.
  // Q2.x = Q.x * xi^( (p^2-1)/3 ) ?
  // Let's compute Q2 from Q1 using the same map?
  // Q2 = phi(Q1).
  // Q2.x = conj(Q1.x) * xi^((p-1)/3)
  // Q2.y = conj(Q1.y) * xi^((p-1)/2)

  point_g2_t Q2;
  // Q2.x
  Q2.x = Q1.x;
  fp_neg(&Q2.x.c1, &Q2.x.c1, p); // conj
  fp2_mul(&Q2.x, &Q2.x, &xi_p_3, p);

  // Q2.y
  Q2.y = Q1.y;
  fp_neg(&Q2.y.c1, &Q2.y.c1, p); // conj
  fp2_mul(&Q2.y, &Q2.y, &xi_p_2, p);

  // Steps
  line_func_add(res, &T, &Q1, P, p);
  line_func_add(res, &T, &Q2, P, p);
}

// Frobenius constants
// xi = 9+i
// xi^((p-1)/6)
static const uint8_t BN128_FROB_COEFF_1[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // c0 = 0
    0x25, 0x23, 0x64, 0x82, 0x40, 0x00, 0x00, 0x01, 0xba, 0x34, 0x4d, 0x80, 0x00, 0x00, 0x00, 0x08,
    0x61, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0xa3, 0x38, 0x50, 0x00, 0x00, 0x00, 0x00, 0x12 // c1
};
// xi^((p-1)/3)
static const uint8_t BN128_FROB_COEFF_2[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // c0 = 0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 // c1 = 1? No.
};
// Wait, I need correct constants.
// Using values from go-ethereum/crypto/bn256/cloudflare/constants.go
// xiToPMinus1Over6: (0, 21575463638280843010398324269430826099269044274347216827212613867836435027261)
// xiToPMinus1Over3: (21888242871839275222246405745257275088696311157297823662689037894645226208582, 0) = -1?
// xiToPMinus1Over2: (0, 21888242871839275222246405745257275088696311157297823662689037894645226208582) = -i?

// Let's use a helper to compute them or just trust the logic that I can implement Frobenius without precomputed tables if I'm careful.
// But precomputed is faster and easier.
// I will implement `fp12_pow` and use the decomposition.
// u = 4965661367192848881

static void fp12_pow(fp12_t* r, const fp12_t* a, uint64_t exp, const uint256_t* p) {
  fp12_t res, base;
  memset(&res, 0, sizeof(fp12_t));
  res.c0.c0.c0.bytes[31] = 1; // res = 1
  base                   = *a;

  while (exp > 0) {
    if (exp & 1) {
      fp12_mul(&res, &res, &base, p);
    }
    fp12_sqr(&base, &base, p);
    exp >>= 1;
  }
  *r = res;
}

// Frobenius map f -> f^p
// f = c0 + c1 w
// c0 = c00 + c01 v + c02 v^2
// c1 = c10 + c11 v + c12 v^2
// f^p = c0^p + c1^p w^p
// c0^p = c00^p + c01^p v^p + c02^p v^2p
// cij^p = conjugate(cij) (since cij in Fp2)
// v^p = v * xi^((p-1)/3)
// w^p = w * xi^((p-1)/6)
// We need these constants.
// xi = 9+i.
// (9+i)^((p-1)/3)
// (9+i)^((p-1)/6)

// I will implement a helper to compute these constants at runtime (once) or just implement `fp2_pow` to compute them on the fly (slow but correct).
// Since this is "final" exponentiation, doing a few extra pows is fine.

static void fp2_pow(fp2_t* r, const fp2_t* a, const uint256_t* exp, const uint256_t* p) {
  fp2_t res, base;
  memset(&res, 0, sizeof(fp2_t));
  res.c0.bytes[31] = 1; // res = 1
  base             = *a;

  uint256_t e;
  memcpy(&e, exp, sizeof(uint256_t));

  // Simple double-and-add
  for (int i = 0; i < 256; i++) {
    // Scan bits from 0 to 255
    // Bit j is in byte 31 - (j/8), bit (j%8)
    int byte_idx = 31 - (i / 8);
    int bit_idx  = i % 8;
    int bit      = (e.bytes[byte_idx] >> bit_idx) & 1;

    if (bit) {
      fp2_mul(&res, &res, &base, p);
    }
    fp2_sqr(&base, &base, p);
  }
  *r = res;
}

static void fp12_frob(fp12_t* r, const fp12_t* a, const uint256_t* p) {
  // Compute constants
  fp2_t xi;
  memset(&xi, 0, sizeof(fp2_t));
  xi.c0.bytes[31] = 9;
  xi.c1.bytes[31] = 1;

  uint256_t p_val;
  memcpy(&p_val, p, sizeof(uint256_t));

  uint256_t one;
  memset(&one, 0, sizeof(uint256_t));
  one.bytes[31] = 1;

  uint256_t p_minus_1;
  intx_sub(&p_minus_1, &p_val, &one); // p - 1

  uint256_t exp1, exp2, three, six;
  memset(&three, 0, sizeof(uint256_t));
  three.bytes[31] = 3;
  memset(&six, 0, sizeof(uint256_t));
  six.bytes[31] = 6;

  intx_div(&exp1, &p_minus_1, &three); // (p-1)/3
  intx_div(&exp2, &p_minus_1, &six);   // (p-1)/6

  fp2_t xi_p_3, xi_p_6;
  fp2_pow(&xi_p_3, &xi, &exp1, p);
  fp2_pow(&xi_p_6, &xi, &exp2, p);

  fp2_t xi_p_3_2; // xi^((p-1)/3 * 2)
  fp2_sqr(&xi_p_3_2, &xi_p_3, p);

  fp12_t res;
  // c0 = c00^p + c01^p v^p + c02^p v^2p
  // c00^p = conj(c00)
  fp2_t t;

  // c00
  res.c0.c0 = a->c0.c0;
  fp_neg(&res.c0.c0.c1, &res.c0.c0.c1, p); // conj

  // c01
  t = a->c0.c1;
  fp_neg(&t.c1, &t.c1, p);             // conj
  fp2_mul(&res.c0.c1, &t, &xi_p_3, p); // * xi^((p-1)/3)

  // c02
  t = a->c0.c2;
  fp_neg(&t.c1, &t.c1, p);
  fp2_mul(&res.c0.c2, &t, &xi_p_3_2, p); // * xi^(2(p-1)/3)

  // c1 = c10^p w^p + ...
  // w^p = w * xi^((p-1)/6)

  // c10
  t = a->c1.c0;
  fp_neg(&t.c1, &t.c1, p);
  fp2_mul(&res.c1.c0, &t, &xi_p_6, p);

  // c11
  t = a->c1.c1;
  fp_neg(&t.c1, &t.c1, p);
  // coeff of w*v is c11^p * xi^((p-1)/6) * xi^((p-1)/3)
  fp2_t tmp;
  fp2_mul(&tmp, &xi_p_6, &xi_p_3, p);
  fp2_mul(&res.c1.c1, &t, &tmp, p);

  // c12
  t = a->c1.c2;
  fp_neg(&t.c1, &t.c1, p);
  // coeff of w*v^2 is c12^p * xi^((p-1)/6) * xi^(2(p-1)/3)
  fp2_mul(&tmp, &xi_p_6, &xi_p_3_2, p);
  fp2_mul(&res.c1.c2, &t, &tmp, p);

  *r = res;
}

static void final_exponentiation(fp12_t* r, const fp12_t* f, const uint256_t* p) {
  // Easy part
  fp12_t t0, t1;
  t0 = *f;
  fp6_neg(&t0.c1, &t0.c1, p); // f^(p^6)

  fp12_inv(&t1, f, p);        // f^(-1)
  fp12_mul(&t0, &t0, &t1, p); // f^(p^6 - 1)

  fp12_t f_easy = t0;
  fp12_frob(&t1, &t0, p);             // t0^p
  fp12_frob(&t1, &t1, p);             // t0^p^2
  fp12_mul(&f_easy, &f_easy, &t1, p); // f^(p^6-1)(p^2+1)

  // Hard part
  // f_hard = f_easy ^ ((p^4 - p^2 + 1) / n)
  // Using decomposition:
  // 4 * exp = x0 + x1*p + x2*p^2 + x3*p^3
  // x0 = 6u^2 + 4u + 1
  // x1 = -2u^2 - 2u
  // x2 = 2u^2 + 2u
  // x3 = -2u^2 - 2u - 1
  // u = 4965661367192848881

  uint64_t u = 4965661367192848881ULL;

  fp12_t fu, fu2, fu3;
  fp12_pow(&fu, &f_easy, u, p);
  fp12_pow(&fu2, &fu, u, p);
  fp12_pow(&fu3, &fu2, u, p);

  fp12_t y0, y1, y2, y3;

  // y0 = f^x0 = f^(6u^2 + 4u + 1)
  //    = (f^(u^2))^6 * f^(4u) * f
  fp12_t t;
  fp12_sqr(&t, &fu2, p);     // u^2 * 2
  fp12_mul(&t, &t, &fu2, p); // u^2 * 3
  fp12_sqr(&t, &t, p);       // u^2 * 6

  fp12_t t2;
  fp12_sqr(&t2, &fu, p); // u*2
  fp12_sqr(&t2, &t2, p); // u*4

  fp12_mul(&y0, &t, &t2, p);
  fp12_mul(&y0, &y0, &f_easy, p);

  // y1 = f^x1 = f^(-2u^2 - 2u) = (f^(2u^2 + 2u))^-1
  // 2u^2 + 2u
  fp12_sqr(&t, &fu2, p); // 2u^2
  fp12_sqr(&t2, &fu, p); // 2u
  fp12_mul(&y1, &t, &t2, p);
  fp12_inv(&y1, &y1, p);

  // y2 = f^x2 = f^(2u^2 + 2u)
  fp12_inv(&y2, &y1, p); // Inverse of y1

  // y3 = f^x3 = f^(-2u^2 - 2u - 1)
  fp12_mul(&y3, &y1, &f_easy, p); // y1 * f^(-1)
  fp12_inv(&t, &f_easy, p);
  fp12_mul(&y3, &y1, &t, p);

  // Result = y0 * y1^p * y2^p^2 * y3^p^3
  fp12_t res = y0;

  fp12_frob(&t, &y1, p);
  fp12_mul(&res, &res, &t, p);

  fp12_frob(&t, &y2, p);
  fp12_frob(&t, &t, p);
  fp12_mul(&res, &res, &t, p);

  fp12_frob(&t, &y3, p);
  fp12_frob(&t, &t, p);
  fp12_frob(&t, &t, p);
  fp12_mul(&res, &res, &t, p);

  *r = res;
}

static int fp12_is_one(const fp12_t* a) {
  // Check if a == 1
  // c0.c0.c0 == 1, all others 0

  // Check c0.c0.c0 == 1
  if (a->c0.c0.c0.bytes[31] != 1) return 0;
  for (int i = 0; i < 31; i++)
    if (a->c0.c0.c0.bytes[i] != 0) return 0;

  // Check others are 0
  if (!intx_is_zero(&a->c0.c0.c1)) return 0;
  if (!intx_is_zero(&a->c0.c1.c0)) return 0;
  if (!intx_is_zero(&a->c0.c1.c1)) return 0;
  if (!intx_is_zero(&a->c0.c2.c0)) return 0;
  if (!intx_is_zero(&a->c0.c2.c1)) return 0;

  if (!intx_is_zero(&a->c1.c0.c0)) return 0;
  if (!intx_is_zero(&a->c1.c0.c1)) return 0;
  if (!intx_is_zero(&a->c1.c1.c0)) return 0;
  if (!intx_is_zero(&a->c1.c1.c1)) return 0;
  if (!intx_is_zero(&a->c1.c2.c0)) return 0;
  if (!intx_is_zero(&a->c1.c2.c1)) return 0;

  return 1;
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
    uint256_from_bytes(&Q.y.c1, input.data + i * 192 + 128); // y1
    uint256_from_bytes(&Q.y.c0, input.data + i * 192 + 160); // y0

    // Miller loop
    fp12_t miller_res;
    miller_loop(&miller_res, &P, &Q, &p);

    // Accumulate result: result = result * miller_res
    fp12_mul(&result, &result, &miller_res, &p);
  }

  // Final exponentiation
  final_exponentiation(&result, &result, &p);

  // Check if result is 1
  int is_one = fp12_is_one(&result);

  buffer_reset(output);
  buffer_grow(output, 32);
  memset(output->data.data, 0, 32);
  output->data.data[31] = is_one ? 1 : 0;
  output->data.len      = 32;

  return PRE_SUCCESS;
}
