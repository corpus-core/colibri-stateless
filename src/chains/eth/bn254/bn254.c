/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */

#include "bn254.h"
#include <string.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------------

// Field modulus P = 21888242871839275222246405745257275088696311157297823662689037894645226208583
static const uint8_t BN254_PRIME[] = {
    0x30, 0x64, 0x4e, 0x72, 0xe1, 0x31, 0xa0, 0x29, 0xb8, 0x50, 0x45, 0xb6, 0x81, 0x81, 0x58, 0x5d,
    0x97, 0x81, 0x6a, 0x91, 0x68, 0x71, 0xca, 0x8d, 0x3c, 0x20, 0x8c, 0x16, 0xd8, 0x7c, 0xfd, 0x47};

// Curve parameter B = 3
static const uint8_t BN254_B[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03};

static bn254_fp_t bn254_modulus;

void bn254_init(void) {
    static bool initialized = false;
    if (!initialized) {
        memset(bn254_modulus.bytes, 0, 32);
        // intx wrapper expects Big Endian bytes in storage
        memcpy(bn254_modulus.bytes, BN254_PRIME, 32);
        // printf("DEBUG: bn254_init modulus: ");
        // for(int i=0; i<32; i++) printf("%02x", bn254_modulus.bytes[i]);
        // printf("\n");
        initialized = true;
    }
}

// -----------------------------------------------------------------------------
// Internal Fp Arithmetic
// -----------------------------------------------------------------------------

static void fp_add(bn254_fp_t* r, const bn254_fp_t* a, const bn254_fp_t* b) {
    intx_add_mod(r, a, b, &bn254_modulus);
}

static void fp_sub(bn254_fp_t* r, const bn254_fp_t* a, const bn254_fp_t* b) {
    intx_sub_mod(r, a, b, &bn254_modulus);
}

static void fp_mul(bn254_fp_t* r, const bn254_fp_t* a, const bn254_fp_t* b) {
    intx_mul_mod(r, a, b, &bn254_modulus);
}

static void fp_neg(bn254_fp_t* r, const bn254_fp_t* a) {
    if (intx_is_zero(a)) {
        memset(r, 0, 32);
    } else {
        intx_sub(r, &bn254_modulus, a);
    }
}

// Modular inverse
static void fp_inv(bn254_fp_t* result, const bn254_fp_t* a) {
    bn254_fp_t t, newt, r, newr, q, temp_rem;
    intx_init(&t); intx_init(&newt); intx_init(&r); intx_init(&newr);
    intx_init(&q); intx_init(&temp_rem);

    memset(&t, 0, sizeof(bn254_fp_t));
    memset(newt.bytes, 0, 32);
    newt.bytes[31] = 1; // Big Endian 1
    memcpy(&r, &bn254_modulus, sizeof(bn254_fp_t));
    memcpy(&newr, a, sizeof(bn254_fp_t));

    while (!intx_is_zero(&newr)) {
        intx_div(&q, &r, &newr);
        intx_mod(&temp_rem, &r, &newr);

        bn254_fp_t q_newt;
        intx_init(&q_newt);
        intx_mul_mod(&q_newt, &q, &newt, &bn254_modulus);

        bn254_fp_t t_new;
        intx_init(&t_new);
        intx_sub_mod(&t_new, &t, &q_newt, &bn254_modulus);

        memcpy(&t, &newt, sizeof(bn254_fp_t));
        memcpy(&newt, &t_new, sizeof(bn254_fp_t));
        memcpy(&r, &newr, sizeof(bn254_fp_t));
        memcpy(&newr, &temp_rem, sizeof(bn254_fp_t));
    }
    memcpy(result, &t, sizeof(bn254_fp_t));
}

// -----------------------------------------------------------------------------
// Fp2 Arithmetic
// -----------------------------------------------------------------------------

static void fp2_add(bn254_fp2_t* r, const bn254_fp2_t* a, const bn254_fp2_t* b) {
    fp_add(&r->c0, &a->c0, &b->c0);
    fp_add(&r->c1, &a->c1, &b->c1);
}

static void fp2_sub(bn254_fp2_t* r, const bn254_fp2_t* a, const bn254_fp2_t* b) {
    fp_sub(&r->c0, &a->c0, &b->c0);
    fp_sub(&r->c1, &a->c1, &b->c1);
}

static void fp2_mul(bn254_fp2_t* r, const bn254_fp2_t* a, const bn254_fp2_t* b) {
    bn254_fp_t t0, t1, t2, t3, c0, c1;
    intx_init(&t0); intx_init(&t1); intx_init(&t2); intx_init(&t3);
    intx_init(&c0); intx_init(&c1);

    fp_mul(&t0, &a->c0, &b->c0);
    fp_mul(&t1, &a->c1, &b->c1);
    fp_sub(&c0, &t0, &t1);

    fp_mul(&t2, &a->c0, &b->c1);
    fp_mul(&t3, &a->c1, &b->c0);
    fp_add(&c1, &t2, &t3);

    memcpy(&r->c0, &c0, sizeof(bn254_fp_t));
    memcpy(&r->c1, &c1, sizeof(bn254_fp_t));
}

static void fp2_sqr(bn254_fp2_t* r, const bn254_fp2_t* a) {
    bn254_fp_t t0, t1, t2, c0, c1;
    intx_init(&t0); intx_init(&t1); intx_init(&t2);
    intx_init(&c0); intx_init(&c1);

    fp_mul(&t0, &a->c0, &a->c0);
    fp_mul(&t1, &a->c1, &a->c1);
    fp_sub(&c0, &t0, &t1);

    fp_mul(&t2, &a->c0, &a->c1);
    fp_add(&c1, &t2, &t2);

    memcpy(&r->c0, &c0, sizeof(bn254_fp_t));
    memcpy(&r->c1, &c1, sizeof(bn254_fp_t));
}

static void fp2_neg(bn254_fp2_t* r, const bn254_fp2_t* a) {
    fp_neg(&r->c0, &a->c0);
    fp_neg(&r->c1, &a->c1);
}

static void fp2_inv(bn254_fp2_t* r, const bn254_fp2_t* a) {
    bn254_fp_t t0, t1, inv_norm;
    intx_init(&t0); intx_init(&t1); intx_init(&inv_norm);
    fp_mul(&t0, &a->c0, &a->c0);
    fp_mul(&t1, &a->c1, &a->c1);
    fp_add(&t0, &t0, &t1);
    fp_inv(&inv_norm, &t0);
    fp_mul(&r->c0, &a->c0, &inv_norm);
    fp_mul(&t1, &a->c1, &inv_norm);
    fp_neg(&r->c1, &t1);
}

static void fp2_mul_xi(bn254_fp2_t* r, const bn254_fp2_t* a) {
    // xi = 9 + i
    bn254_fp_t t0, t1, t2, t3, nine;
    intx_init(&t0); intx_init(&t1); intx_init(&t2); intx_init(&t3);
    intx_init(&nine); nine.bytes[31] = 9; // BE 9

    fp_mul(&t0, &a->c0, &nine);
    fp_sub(&t2, &t0, &a->c1);
    fp_mul(&t1, &a->c1, &nine);
    fp_add(&t3, &t1, &a->c0);

    memcpy(&r->c0, &t2, sizeof(bn254_fp_t));
    memcpy(&r->c1, &t3, sizeof(bn254_fp_t));
}

static void fp2_pow(bn254_fp2_t* r, const bn254_fp2_t* a, const bn254_fp_t* exp) {
    bn254_fp2_t res;
    memset(&res, 0, sizeof(bn254_fp2_t));
    res.c0.bytes[31] = 1; // BE 1
    
    // Scan MSB to LSB. Storage is BE, so bytes[0] is MSB.
    for (int i=0; i<32; i++) {
        uint8_t byte = exp->bytes[i];
        for (int j=7; j>=0; j--) {
            fp2_sqr(&res, &res);
            int bit = (byte >> j) & 1;
            if (bit) fp2_mul(&res, &res, a);
        }
    }
    *r = res;
}

// -----------------------------------------------------------------------------
// Fp6 Arithmetic
// -----------------------------------------------------------------------------

static void fp6_add(bn254_fp6_t* r, const bn254_fp6_t* a, const bn254_fp6_t* b) {
    fp2_add(&r->c0, &a->c0, &b->c0);
    fp2_add(&r->c1, &a->c1, &b->c1);
    fp2_add(&r->c2, &a->c2, &b->c2);
}

static void fp6_sub(bn254_fp6_t* r, const bn254_fp6_t* a, const bn254_fp6_t* b) {
    fp2_sub(&r->c0, &a->c0, &b->c0);
    fp2_sub(&r->c1, &a->c1, &b->c1);
    fp2_sub(&r->c2, &a->c2, &b->c2);
}

static void fp6_neg(bn254_fp6_t* r, const bn254_fp6_t* a) {
    fp2_neg(&r->c0, &a->c0);
    fp2_neg(&r->c1, &a->c1);
    fp2_neg(&r->c2, &a->c2);
}

static void fp6_mul(bn254_fp6_t* r, const bn254_fp6_t* a, const bn254_fp6_t* b) {
    bn254_fp2_t v0, v1, v2, t0, t1;
    bn254_fp2_t c0, c1, c2;
    fp2_mul(&v0, &a->c0, &b->c0);
    fp2_mul(&v1, &a->c1, &b->c1);
    fp2_mul(&v2, &a->c2, &b->c2);
    fp2_add(&t0, &a->c1, &a->c2);
    fp2_add(&t1, &b->c1, &b->c2);
    fp2_mul(&t0, &t0, &t1);
    fp2_sub(&t0, &t0, &v1);
    fp2_sub(&t0, &t0, &v2);
    fp2_mul_xi(&t0, &t0);
    fp2_add(&c0, &v0, &t0);
    fp2_add(&t0, &a->c0, &a->c1);
    fp2_add(&t1, &b->c0, &b->c1);
    fp2_mul(&t0, &t0, &t1);
    fp2_sub(&t0, &t0, &v0);
    fp2_sub(&t0, &t0, &v1);
    fp2_mul_xi(&t1, &v2);
    fp2_add(&c1, &t0, &t1);
    fp2_add(&t0, &a->c0, &a->c2);
    fp2_add(&t1, &b->c0, &b->c2);
    fp2_mul(&t0, &t0, &t1);
    fp2_sub(&t0, &t0, &v0);
    fp2_sub(&t0, &t0, &v2);
    fp2_add(&c2, &t0, &v1);
    r->c0 = c0; r->c1 = c1; r->c2 = c2;
}

static void fp6_sqr(bn254_fp6_t* r, const bn254_fp6_t* a) {
    bn254_fp2_t s0, s1, s2, t0, t1, c0, c1, c2;
    fp2_sqr(&s0, &a->c0);
    fp2_sqr(&s1, &a->c1);
    fp2_sqr(&s2, &a->c2);
    fp2_mul(&t0, &a->c1, &a->c2);
    fp2_add(&t0, &t0, &t0);
    fp2_mul_xi(&t0, &t0);
    fp2_add(&c0, &s0, &t0);
    fp2_mul(&t0, &a->c0, &a->c1);
    fp2_add(&t0, &t0, &t0);
    fp2_mul_xi(&t1, &s2);
    fp2_add(&c1, &t0, &t1);
    fp2_mul(&t0, &a->c0, &a->c2);
    fp2_add(&t0, &t0, &t0);
    fp2_add(&c2, &s1, &t0);
    r->c0 = c0; r->c1 = c1; r->c2 = c2;
}

static void fp6_mul_v(bn254_fp6_t* r, const bn254_fp6_t* a) {
    bn254_fp2_t t;
    fp2_mul_xi(&t, &a->c2);
    bn254_fp2_t tmp_c0 = a->c0;
    bn254_fp2_t tmp_c1 = a->c1;
    r->c0 = t;
    r->c1 = tmp_c0;
    r->c2 = tmp_c1;
}

static void fp6_inv(bn254_fp6_t* r, const bn254_fp6_t* a) {
    bn254_fp2_t T0, T1, T2, tmp, tmp2, N, invN;
    fp2_sqr(&T0, &a->c0);
    fp2_mul(&tmp, &a->c1, &a->c2);
    fp2_mul_xi(&tmp, &tmp);
    fp2_sub(&T0, &T0, &tmp);
    fp2_sqr(&T1, &a->c2);
    fp2_mul_xi(&T1, &T1);
    fp2_mul(&tmp, &a->c0, &a->c1);
    fp2_sub(&T1, &T1, &tmp);
    fp2_sqr(&T2, &a->c1);
    fp2_mul(&tmp, &a->c0, &a->c2);
    fp2_sub(&T2, &T2, &tmp);
    fp2_mul(&N, &a->c0, &T0);
    fp2_mul(&tmp, &a->c1, &T2);
    fp2_mul(&tmp2, &a->c2, &T1);
    fp2_add(&tmp, &tmp, &tmp2);
    fp2_mul_xi(&tmp, &tmp);
    fp2_add(&N, &N, &tmp);
    fp2_inv(&invN, &N);
    fp2_mul(&r->c0, &T0, &invN);
    fp2_mul(&r->c1, &T1, &invN);
    fp2_mul(&r->c2, &T2, &invN);
}

// -----------------------------------------------------------------------------
// Fp12 Arithmetic
// -----------------------------------------------------------------------------

static void fp12_add(bn254_fp12_t* r, const bn254_fp12_t* a, const bn254_fp12_t* b) {
    fp6_add(&r->c0, &a->c0, &b->c0);
    fp6_add(&r->c1, &a->c1, &b->c1);
}

static void fp12_sub(bn254_fp12_t* r, const bn254_fp12_t* a, const bn254_fp12_t* b) {
    fp6_sub(&r->c0, &a->c0, &b->c0);
    fp6_sub(&r->c1, &a->c1, &b->c1);
}

static void fp12_mul_internal(bn254_fp12_t* r, const bn254_fp12_t* a, const bn254_fp12_t* b) {
    bn254_fp6_t t0, t1, t2, t3;
    fp6_mul(&t0, &a->c0, &b->c0);
    fp6_mul(&t1, &a->c1, &b->c1);
    fp6_add(&t2, &a->c0, &a->c1);
    fp6_add(&t3, &b->c0, &b->c1);
    fp6_mul(&t2, &t2, &t3);
    fp6_sub(&t2, &t2, &t0);
    fp6_sub(&t2, &t2, &t1);
    r->c1 = t2;
    fp6_mul_v(&t1, &t1);
    fp6_add(&r->c0, &t0, &t1);
}

void bn254_fp12_mul(bn254_fp12_t* r, const bn254_fp12_t* a, const bn254_fp12_t* b) {
    bn254_init();
    fp12_mul_internal(r, a, b);
}

static void fp12_sqr(bn254_fp12_t* r, const bn254_fp12_t* a) {
    bn254_fp6_t t0, t1, t2;
    fp6_sqr(&t0, &a->c0);
    fp6_sqr(&t1, &a->c1);
    fp6_mul(&t2, &a->c0, &a->c1);
    fp6_add(&r->c1, &t2, &t2);
    fp6_mul_v(&t1, &t1);
    fp6_add(&r->c0, &t0, &t1);
}

static void fp12_inv(bn254_fp12_t* r, const bn254_fp12_t* a) {
    bn254_fp6_t t0, t1, invNorm;
    fp6_sqr(&t0, &a->c0);
    fp6_sqr(&t1, &a->c1);
    fp6_mul_v(&t1, &t1);
    fp6_sub(&t0, &t0, &t1);
    fp6_inv(&invNorm, &t0);
    fp6_mul(&r->c0, &a->c0, &invNorm);
    fp6_mul(&r->c1, &a->c1, &invNorm);
    fp6_neg(&r->c1, &r->c1);
}

static void fp12_pow(bn254_fp12_t* r, const bn254_fp12_t* a, uint64_t exp) {
    bn254_fp12_t res, base;
    memset(&res, 0, sizeof(bn254_fp12_t));
    res.c0.c0.c0.bytes[31] = 1; // BE 1
    base = *a;
    // exp is uint64_t, so standard bitwise ops work.
    // But wait, this implementation of pow expects LSB-first iteration?
    // "if (exp & 1) ... exp >>= 1"
    // Yes, that computes base^exp by scanning bits from LSB to MSB.
    // That is mathematically correct regardless of endianness of 'exp' variable itself.
    while (exp > 0) {
        if (exp & 1) fp12_mul_internal(&res, &res, &base);
        fp12_sqr(&base, &base);
        exp >>= 1;
    }
    *r = res;
}

static void fp12_frob(bn254_fp12_t* r, const bn254_fp12_t* a) {
    bn254_fp2_t xi;
    memset(&xi, 0, sizeof(bn254_fp2_t));
    xi.c0.bytes[31] = 9; // BE 9
    xi.c1.bytes[31] = 1;
    bn254_fp_t p_val, one, p_minus_1;
    memcpy(&p_val, &bn254_modulus, sizeof(bn254_fp_t));
    memset(&one, 0, sizeof(bn254_fp_t)); one.bytes[31] = 1; // BE 1
    intx_sub(&p_minus_1, &p_val, &one);
    bn254_fp_t exp1, exp2, three, six;
    memset(&three, 0, sizeof(bn254_fp_t)); three.bytes[31] = 3;
    memset(&six, 0, sizeof(bn254_fp_t)); six.bytes[31] = 6;

    intx_div(&exp1, &p_minus_1, &three);
    intx_div(&exp2, &p_minus_1, &six);
    bn254_fp2_t xi_p_3, xi_p_6, xi_p_3_2;
    fp2_pow(&xi_p_3, &xi, &exp1);
    fp2_pow(&xi_p_6, &xi, &exp2);
    fp2_sqr(&xi_p_3_2, &xi_p_3);
    
    bn254_fp12_t res;
    bn254_fp2_t t, tmp;
    
    // c00
    res.c0.c0 = a->c0.c0; fp_neg(&res.c0.c0.c1, &res.c0.c0.c1);
    // c01
    t = a->c0.c1; fp_neg(&t.c1, &t.c1); fp2_mul(&res.c0.c1, &t, &xi_p_3);
    // c02
    t = a->c0.c2; fp_neg(&t.c1, &t.c1); fp2_mul(&res.c0.c2, &t, &xi_p_3_2);
    // c10
    t = a->c1.c0; fp_neg(&t.c1, &t.c1); fp2_mul(&res.c1.c0, &t, &xi_p_6);
    // c11
    t = a->c1.c1; fp_neg(&t.c1, &t.c1); fp2_mul(&tmp, &xi_p_6, &xi_p_3); fp2_mul(&res.c1.c1, &t, &tmp);
    // c12
    t = a->c1.c2; fp_neg(&t.c1, &t.c1); fp2_mul(&tmp, &xi_p_6, &xi_p_3_2); fp2_mul(&res.c1.c2, &t, &tmp);
    
    *r = res;
}

bool bn254_fp12_is_one(const bn254_fp12_t* a) {
    if (a->c0.c0.c0.bytes[31] != 1) return false; // BE 1
    for (int i=0; i<31; i++) if (a->c0.c0.c0.bytes[i] != 0) return false;
    if (!intx_is_zero(&a->c0.c0.c1)) return false;
    if (!intx_is_zero(&a->c0.c1.c0)) return false;
    if (!intx_is_zero(&a->c0.c1.c1)) return false;
    if (!intx_is_zero(&a->c0.c2.c0)) return false;
    if (!intx_is_zero(&a->c0.c2.c1)) return false;
    if (!intx_is_zero(&a->c1.c0.c0)) return false;
    if (!intx_is_zero(&a->c1.c0.c1)) return false;
    if (!intx_is_zero(&a->c1.c1.c0)) return false;
    if (!intx_is_zero(&a->c1.c1.c1)) return false;
    if (!intx_is_zero(&a->c1.c2.c0)) return false;
    if (!intx_is_zero(&a->c1.c2.c1)) return false;
    return true;
}

// -----------------------------------------------------------------------------
// Parsing Helpers
// -----------------------------------------------------------------------------

static void uint256_from_bytes_be(bn254_fp_t* out, const uint8_t* bytes) {
    memset(out->bytes, 0, 32);
    memcpy(out->bytes, bytes, 32); // BE input -> BE storage
}

static void uint256_to_bytes_be(const bn254_fp_t* in, uint8_t* bytes) {
    memcpy(bytes, in->bytes, 32); // BE storage -> BE output
}

bool bn254_g1_from_bytes_be(bn254_g1_t* p, const uint8_t* bytes) {
    bn254_init();
    uint256_from_bytes_be(&p->x, bytes);
    uint256_from_bytes_be(&p->y, bytes + 32);
    memset(&p->z, 0, 32);
    p->z.bytes[31] = 1; // Affine -> Jacobian (BE 1)
    
    // Check on curve
    if (!bn254_g1_is_on_curve(p)) return false;
    return true;
}

bool bn254_g1_from_bytes(bn254_g1_t* p, const uint8_t* bytes) {
    return bn254_g1_from_bytes_be(p, bytes);
}

static void g1_normalize(bn254_g1_t* r, const bn254_g1_t* p) {
    if (intx_is_zero(&p->z)) {
        memset(r, 0, sizeof(bn254_g1_t));
        return;
    }
    
    // If Z is already 1, copy and return
    if (p->z.bytes[31] == 1) {
        bool others_zero = true;
        for(int i=0; i<31; i++) if (p->z.bytes[i] != 0) others_zero = false;
        if (others_zero) {
            *r = *p;
            return;
        }
    }

    bn254_fp_t z_inv, z2, z3;
    intx_init(&z_inv); intx_init(&z2); intx_init(&z3);
    
    fp_inv(&z_inv, &p->z);
    fp_mul(&z2, &z_inv, &z_inv);
    fp_mul(&z3, &z2, &z_inv);
    
    fp_mul(&r->x, &p->x, &z2);
    fp_mul(&r->y, &p->y, &z3);
    memset(&r->z, 0, 32);
    r->z.bytes[31] = 1; // Z=1
}

void bn254_g1_to_bytes(const bn254_g1_t* p, uint8_t* out) {
    bn254_g1_t aff;
    g1_normalize(&aff, p);
    
    if (intx_is_zero(&aff.z)) {
        memset(out, 0, 64);
        return;
    }
    
    uint256_to_bytes_be(&aff.x, out);
    uint256_to_bytes_be(&aff.y, out + 32);
}

bool bn254_g2_is_on_curve(const bn254_g2_t* p) {
    bn254_init();
    if (intx_is_zero(&p->z.c0) && intx_is_zero(&p->z.c1)) return true;

    bn254_fp2_t x2, x3, y2, rhs, three_div_xi;
    
    // 3/xi constant
    // RE=2b149d40ceb8aaae81be18991be06ac3b5b4c5e559dbefa33267e6dc24a138e5
    // IM=009713b03af0fed4cd2cafadeed8fdf4a74fa084e52d1852e4a2bd0685c315d2
    static const uint8_t TB_RE[] = {
        0x2b, 0x14, 0x9d, 0x40, 0xce, 0xb8, 0xaa, 0xae, 0x81, 0xbe, 0x18, 0x99, 0x1b, 0xe0, 0x6a, 0xc3,
        0xb5, 0xb4, 0xc5, 0xe5, 0x59, 0xdb, 0xef, 0xa3, 0x32, 0x67, 0xe6, 0xdc, 0x24, 0xa1, 0x38, 0xe5
    };
    static const uint8_t TB_IM[] = {
        0x00, 0x97, 0x13, 0xb0, 0x3a, 0xf0, 0xfe, 0xd4, 0xcd, 0x2c, 0xaf, 0xad, 0xee, 0xd8, 0xfd, 0xf4,
        0xa7, 0x4f, 0xa0, 0x84, 0xe5, 0x2d, 0x18, 0x52, 0xe4, 0xa2, 0xbd, 0x06, 0x85, 0xc3, 0x15, 0xd2
    };
    memcpy(three_div_xi.c0.bytes, TB_RE, 32);
    memcpy(three_div_xi.c1.bytes, TB_IM, 32);

    // Projective: Y^2 = X^3 + (3/xi)*Z^6
    bn254_fp2_t z2, z6;
    fp2_sqr(&z2, &p->z);
    fp2_mul(&z6, &z2, &z2); 
    fp2_mul(&z6, &z6, &z2); // Z^6
    
    fp2_sqr(&x2, &p->x);
    fp2_mul(&x3, &x2, &p->x);
    
    bn254_fp2_t term;
    fp2_mul(&term, &three_div_xi, &z6);
    fp2_add(&rhs, &x3, &term);
    
    fp2_sqr(&y2, &p->y);
    
    bool c0_eq = intx_eq(&y2.c0, &rhs.c0);
    bool c1_eq = intx_eq(&y2.c1, &rhs.c1);
    
    if (!c0_eq || !c1_eq) {
        // printf("DEBUG: G2 Point NOT on curve!\n");
        return false;
    }
    return true;
}

bool bn254_g2_from_bytes_eth(bn254_g2_t* p, const uint8_t* bytes) {
    bn254_init();
    // printf("DEBUG from_bytes_eth: p=%p, x=%p, c0=%p, c1=%p\n", p, &p->x, &p->x.c0, &p->x.c1);
    // ETH format: X_im, X_re, Y_im, Y_re
    uint256_from_bytes_be(&p->x.c1, bytes);
    uint256_from_bytes_be(&p->x.c0, bytes + 32);
    // printf("DEBUG Loaded X: c0[0]=%02x, c1[0]=%02x\n", p->x.c0.bytes[0], p->x.c1.bytes[0]);
    uint256_from_bytes_be(&p->y.c1, bytes + 64);
    uint256_from_bytes_be(&p->y.c0, bytes + 96);
    memset(&p->z, 0, sizeof(bn254_fp2_t));
    p->z.c0.bytes[31] = 1; // Z=1 (BE)
    
    if (!bn254_g2_is_on_curve(p)) return false;
    return true;
}

bool bn254_g2_from_bytes_raw(bn254_g2_t* p, const uint8_t* bytes) {
    bn254_init();
    // Raw/MCL format: X_re, X_im, Y_re, Y_im
    uint256_from_bytes_be(&p->x.c0, bytes);
    uint256_from_bytes_be(&p->x.c1, bytes + 32);
    uint256_from_bytes_be(&p->y.c0, bytes + 64);
    uint256_from_bytes_be(&p->y.c1, bytes + 96);
    memset(&p->z, 0, sizeof(bn254_fp2_t));
    p->z.c0.bytes[31] = 1; // Z=1 (BE)
    return true;
}

void bn254_g2_to_bytes_eth(const bn254_g2_t* p, uint8_t* out) {
    bn254_fp2_t x, y, z_inv, z2, z3;
    
    if (intx_is_zero(&p->z.c0) && intx_is_zero(&p->z.c1)) {
         memset(out, 0, 128); return;
    }

    if (p->z.c0.bytes[31] == 1 && intx_is_zero(&p->z.c1) && intx_is_zero(&p->z.c0)) { // Check if 1 (Partial check, c0>1 not checked)
         // Already affine?
         // Actually strict check needed for 1
    }
    
    fp2_inv(&z_inv, &p->z);
    fp2_sqr(&z2, &z_inv);
    fp2_mul(&z3, &z2, &z_inv);
    fp2_mul(&x, &p->x, &z2);
    fp2_mul(&y, &p->y, &z3);
    
    uint256_to_bytes_be(&x.c1, out);
    uint256_to_bytes_be(&x.c0, out + 32);
    uint256_to_bytes_be(&y.c1, out + 64);
    uint256_to_bytes_be(&y.c0, out + 96);
}

// -----------------------------------------------------------------------------
// G1 Logic (reusing internal helpers)
// -----------------------------------------------------------------------------

bool bn254_g1_is_on_curve(const bn254_g1_t* p) {
    bn254_init();
    if (intx_is_zero(&p->z)) return true;

    bn254_fp_t x2, x3, y2, rhs, three;
    intx_init(&x2); intx_init(&x3); intx_init(&y2); intx_init(&rhs);
    intx_init(&three); three.bytes[31] = 3; // BE 3

    // Projective: Y^2 = X^3 + 3*Z^6
    bn254_fp_t z2, z6;
    intx_init(&z2); intx_init(&z6);
    fp_mul(&z2, &p->z, &p->z);
    fp_mul(&z6, &z2, &z2); 
    fp_mul(&z6, &z6, &z2); // Z^6
    
    fp_mul(&x2, &p->x, &p->x);
    fp_mul(&x3, &x2, &p->x);
    
    bn254_fp_t term; intx_init(&term);
    fp_mul(&term, &three, &z6);
    fp_add(&rhs, &x3, &term);
    
    fp_mul(&y2, &p->y, &p->y);
    
    bool on_curve = intx_eq(&y2, &rhs);
    /*
    printf("DEBUG Check Curve:\n");
    printf("Modulus: "); for(int i=0; i<32; i++) printf("%02x", bn254_modulus.bytes[i]); printf("\n");
    printf("Y: "); for(int i=0; i<32; i++) printf("%02x", p->y.bytes[i]); printf("\n");
    printf("Y^2: "); for(int i=0; i<32; i++) printf("%02x", y2.bytes[i]); printf("\n");
    printf("X: "); for(int i=0; i<32; i++) printf("%02x", p->x.bytes[i]); printf("\n");
    printf("X^2: "); for(int i=0; i<32; i++) printf("%02x", x2.bytes[i]); printf("\n");
    printf("X^3: "); for(int i=0; i<32; i++) printf("%02x", x3.bytes[i]); printf("\n");
    printf("RHS: "); for(int i=0; i<32; i++) printf("%02x", rhs.bytes[i]); printf("\n");
    */
    if (!on_curve) {
        // printf("DEBUG: G1 Point NOT on curve!\n");
        return false;
    }
    return on_curve;
}

// Helper for debugging (removed)


static void g1_dbl_jacobian(bn254_g1_t* r, const bn254_g1_t* p) {
    if (intx_is_zero(&p->z)) { *r = *p; return; }
    bn254_fp_t a, b, c, d, e, f, x_sq, y_sq, z_sq, z_new;
    
    // Compute Z3 first (into temp) to handle aliasing (r == p)
    // z' = 2*y*z
    fp_mul(&z_new, &p->y, &p->z);
    fp_add(&z_new, &z_new, &z_new);

    fp_mul(&x_sq, &p->x, &p->x);
    fp_mul(&y_sq, &p->y, &p->y);
    fp_mul(&z_sq, &p->z, &p->z);
    fp_mul(&a, &p->x, &y_sq);
    fp_add(&a, &a, &a); fp_add(&a, &a, &a);
    fp_add(&b, &x_sq, &x_sq); fp_add(&b, &b, &x_sq);
    
    // fp_print("DEBUG DBL a (4xy^2)", &a);
    // fp_print("DEBUG DBL b (3x^2)", &b);
    
    fp_mul(&r->x, &b, &b);
    fp_add(&c, &a, &a);
    fp_sub(&r->x, &r->x, &c);
    
    // fp_print("DEBUG DBL x' (b^2-2a)", &r->x);
    
    fp_sub(&c, &a, &r->x);
    fp_mul(&r->y, &c, &b);
    fp_mul(&c, &y_sq, &y_sq);
    fp_add(&c, &c, &c); fp_add(&c, &c, &c); fp_add(&c, &c, &c);
    fp_sub(&r->y, &r->y, &c);
    
    r->z = z_new;
}

static void g1_add_jacobian(bn254_g1_t* r, const bn254_g1_t* p, const bn254_g1_t* q) {
    if (intx_is_zero(&p->z)) { *r = *q; return; }
    if (intx_is_zero(&q->z)) { *r = *p; return; }

    bn254_fp_t z1z1, z2z2, u1, u2, s1, s2, h, i, j, r_val, v;
    intx_init(&z1z1); intx_init(&z2z2);
    fp_mul(&z1z1, &p->z, &p->z);
    fp_mul(&z2z2, &q->z, &q->z);
    fp_mul(&u1, &p->x, &z2z2);
    fp_mul(&u2, &q->x, &z1z1);
    bn254_fp_t tmp; intx_init(&tmp);
    fp_mul(&tmp, &p->y, &q->z); fp_mul(&s1, &tmp, &z2z2);
    fp_mul(&tmp, &q->y, &p->z); fp_mul(&s2, &tmp, &z1z1);

    if (intx_eq(&u1, &u2)) {
        if (intx_eq(&s1, &s2)) {
            g1_dbl_jacobian(r, p);
            return;
        }
        memset(r, 0, sizeof(bn254_g1_t)); return;
    }

    fp_sub(&h, &u2, &u1);
    fp_sub(&r_val, &s2, &s1);
    bn254_fp_t h2, h3;
    fp_mul(&h2, &h, &h);
    fp_mul(&h3, &h2, &h);
    fp_mul(&v, &u1, &h2);
    
    fp_mul(&r->x, &r_val, &r_val);
    fp_sub(&r->x, &r->x, &h3);
    fp_sub(&r->x, &r->x, &v);
    fp_sub(&r->x, &r->x, &v);
    
    fp_sub(&r->y, &v, &r->x);
    fp_mul(&r->y, &r->y, &r_val);
    fp_mul(&tmp, &s1, &h3);
    fp_sub(&r->y, &r->y, &tmp);
    
    fp_mul(&r->z, &p->z, &q->z);
    fp_mul(&r->z, &r->z, &h);
}

void bn254_g1_add(bn254_g1_t* r, const bn254_g1_t* a, const bn254_g1_t* b) {
    bn254_init();
    g1_add_jacobian(r, a, b);
}

void bn254_g1_mul(bn254_g1_t* r, const bn254_g1_t* p, const uint256_t* scalar) {
    bn254_init();
    bn254_g1_t res;
    memset(&res, 0, sizeof(res));
    bn254_g1_t base = *p;
    
    // Scalar is stored in BE (bytes[0] is MSB).
    // We want to process LSB to MSB because we double 'base' in each step.
    // LSB is byte 31, bit 0.
    for (int i=31; i>=0; i--) {
        uint8_t byte = scalar->bytes[i];
        for (int j=0; j<8; j++) {
            if ((byte >> j) & 1) bn254_g1_add(&res, &res, &base);
            g1_dbl_jacobian(&base, &base);
        }
    }
    *r = res;
}

// -----------------------------------------------------------------------------
// Pairing
// -----------------------------------------------------------------------------

static void fp2_mul_twist_b(bn254_fp2_t* r, const bn254_fp2_t* a) {
    // Twist B = 3/xi
    // RE=2b149d40ceb8aaae81be18991be06ac3b5b4c5e559dbefa33267e6dc24a138e5
    // IM=009713b03af0fed4cd2cafadeed8fdf4a74fa084e52d1852e4a2bd0685c315d2
    
    static const uint8_t TB_RE[] = {
        0x2b, 0x14, 0x9d, 0x40, 0xce, 0xb8, 0xaa, 0xae, 0x81, 0xbe, 0x18, 0x99, 0x1b, 0xe0, 0x6a, 0xc3,
        0xb5, 0xb4, 0xc5, 0xe5, 0x59, 0xdb, 0xef, 0xa3, 0x32, 0x67, 0xe6, 0xdc, 0x24, 0xa1, 0x38, 0xe5
    };
    static const uint8_t TB_IM[] = {
        0x00, 0x97, 0x13, 0xb0, 0x3a, 0xf0, 0xfe, 0xd4, 0xcd, 0x2c, 0xaf, 0xad, 0xee, 0xd8, 0xfd, 0xf4,
        0xa7, 0x4f, 0xa0, 0x84, 0xe5, 0x2d, 0x18, 0x52, 0xe4, 0xa2, 0xbd, 0x06, 0x85, 0xc3, 0x15, 0xd2
    };
    
    bn254_fp2_t tb;
    memcpy(tb.c0.bytes, TB_RE, 32); // BE load
    memcpy(tb.c1.bytes, TB_IM, 32); // BE load
    
    fp2_mul(r, a, &tb);
}

static void fp_div2(bn254_fp_t* r, const bn254_fp_t* a) {
    // (p+1)/2
    static const uint8_t INV2[] = {
        0x18, 0x32, 0x27, 0x39, 0x70, 0x98, 0xd0, 0x14, 0xdc, 0x28, 0x22, 0xdb, 0x40, 0xc0, 0xac, 0x2e,
        0xcb, 0xc0, 0xb5, 0x48, 0xb4, 0x38, 0xe5, 0x46, 0x9e, 0x10, 0x46, 0x0b, 0x6c, 0x3e, 0x7e, 0xa4
    };
    bn254_fp_t inv2;
    memset(inv2.bytes, 0, 32);
    memcpy(inv2.bytes, INV2, 32); // BE load
    fp_mul(r, a, &inv2);
}

static void fp2_div2(bn254_fp2_t* r, const bn254_fp2_t* a) {
    fp_div2(&r->c0, &a->c0);
    fp_div2(&r->c1, &a->c1);
}

static void line_func_dbl(bn254_fp12_t* f, bn254_g2_t* Q, const bn254_g1_t* P) {
    // Ported from MCL dblLineWithoutP + updateLine
    bn254_fp2_t t0, t1, t2, t3, t4, t5;
    bn254_fp2_t T0, T1;
    bn254_fp2_t l_a, l_b, l_c;

    fp2_sqr(&t0, &Q->z);
    fp2_mul(&t4, &Q->x, &Q->y);
    fp2_sqr(&t1, &Q->y);
    
    fp2_add(&t3, &t0, &t0);
    fp2_div2(&t4, &t4);
    fp2_add(&t5, &t0, &t1);
    fp2_add(&t0, &t0, &t3);
    
    fp2_mul_twist_b(&t2, &t0);
    fp2_sqr(&t0, &Q->x);
    
    fp2_add(&t3, &t2, &t2);
    fp2_add(&t3, &t3, &t2);
    
    fp2_sub(&Q->x, &t1, &t3);
    fp2_add(&t3, &t3, &t1);
    fp2_mul(&Q->x, &Q->x, &t4);
    
    fp2_div2(&t3, &t3);
    fp2_sqr(&T0, &t3);
    fp2_sqr(&T1, &t2);
    
    fp2_sub(&T0, &T0, &T1);
    fp2_add(&T1, &T1, &T1);
    fp2_sub(&T0, &T0, &T1);
    
    fp2_add(&t3, &Q->y, &Q->z);
    Q->y = T0;
    
    fp2_sqr(&t3, &t3);
    fp2_sub(&t3, &t3, &t5);
    fp2_mul(&Q->z, &t1, &t3);
    
    fp2_sub(&l_a, &t2, &t1);
    l_c = t0;
    l_b = t3;
    
    // Update Line with P
    bn254_fp2_t px_fp2, py_fp2;
    memset(&px_fp2, 0, sizeof(bn254_fp2_t)); px_fp2.c0 = P->x;
    memset(&py_fp2, 0, sizeof(bn254_fp2_t)); py_fp2.c0 = P->y;
    
    fp2_mul(&l_c, &l_c, &px_fp2);
    fp2_mul(&l_b, &l_b, &py_fp2);
    
    // Map to Fp12
    bn254_fp12_t l;
    memset(&l, 0, sizeof(bn254_fp12_t));
    l.c1.c1 = l_a;
    l.c0.c0 = l_b;
    l.c1.c0 = l_c;
    
    fp12_mul_internal(f, f, &l);
}

static void line_func_add(bn254_fp12_t* f, bn254_g2_t* R, const bn254_g2_t* Q, const bn254_g1_t* P) {
    // Ported from MCL addLineWithoutP + updateLine
    bn254_fp2_t t1, t2, t3, t4;
    bn254_fp2_t T1, T2;
    bn254_fp2_t l_a, l_b, l_c;
    
    fp2_mul(&t1, &R->z, &Q->x);
    fp2_mul(&t2, &R->z, &Q->y);
    fp2_sub(&t1, &R->x, &t1);
    fp2_sub(&t2, &R->y, &t2);
    fp2_sqr(&t3, &t1);
    fp2_mul(&R->x, &t3, &R->x);
    fp2_sqr(&t4, &t2);
    fp2_mul(&t3, &t3, &t1);
    fp2_mul(&t4, &t4, &R->z);
    fp2_add(&t4, &t4, &t3);
    fp2_sub(&t4, &t4, &R->x);
    fp2_sub(&t4, &t4, &R->x);
    fp2_sub(&R->x, &R->x, &t4);
    fp2_mul(&T1, &t2, &R->x);
    fp2_mul(&T2, &t3, &R->y);
    fp2_sub(&T2, &T1, &T2);
    R->y = T2;
    fp2_mul(&R->x, &t1, &t4);
    fp2_mul(&R->z, &t3, &R->z);
    
    fp2_neg(&l_c, &t2);
    fp2_mul(&T1, &t2, &Q->x);
    fp2_mul(&T2, &t1, &Q->y);
    fp2_sub(&l_a, &T1, &T2);
    
    l_b = t1;
    
    // Update Line with P
    bn254_fp2_t px_fp2, py_fp2;
    memset(&px_fp2, 0, sizeof(bn254_fp2_t)); px_fp2.c0 = P->x;
    memset(&py_fp2, 0, sizeof(bn254_fp2_t)); py_fp2.c0 = P->y;
    
    fp2_mul(&l_c, &l_c, &px_fp2);
    fp2_mul(&l_b, &l_b, &py_fp2);
    
    // Map to Fp12
    bn254_fp12_t l;
    memset(&l, 0, sizeof(bn254_fp12_t));
    l.c1.c1 = l_a;
    l.c0.c0 = l_b;
    l.c1.c0 = l_c;
    
    fp12_mul_internal(f, f, &l);
}

void bn254_miller_loop(bn254_fp12_t* res, const bn254_g1_t* P_in, const bn254_g2_t* Q) {
    bn254_init();
    
    // Normalize P to affine coordinates (Z=1)
    bn254_g1_t P_aff;
    g1_normalize(&P_aff, P_in);
    const bn254_g1_t* P = &P_aff;

    // Loop parameter u = 4965661367192848881
    // 6u+2 = 29793968203157093288 = 0x19D797039BE763BA8
    // This requires 65 bits. Bit 64 is 1.
    // Lower 64 bits: 0x9D797039BE763BA8
    uint64_t loop_param_lower = 0x9D797039BE763BA8ULL;
    
    memset(res, 0, sizeof(bn254_fp12_t)); res->c0.c0.c0.bytes[31] = 1; // BE 1
    bn254_g2_t T = *Q;
    
    // Ensure T is normalized if Z is zero? No, Z=1 for affine input.
    if (intx_is_zero(&T.z.c0) && intx_is_zero(&T.z.c1)) T.z.c0.bytes[31] = 1; // BE 1
    
    // Prepare P_dbl for doubling steps (scaled P)
    bn254_g1_t P_dbl;
    memset(&P_dbl, 0, sizeof(bn254_g1_t));
    // P_dbl.x = 3 * P.x
    fp_add(&P_dbl.x, &P->x, &P->x);
    fp_add(&P_dbl.x, &P_dbl.x, &P->x);
    // P_dbl.y = -P.y
    fp_neg(&P_dbl.y, &P->y);
    
    // Loop from 63 down to 0 (skipping MSB 64 which is handled by init T=Q, f=1)
    for (int i = 63; i >= 0; i--) {
        fp12_sqr(res, res);
        line_func_dbl(res, &T, &P_dbl);
        
        bool bit = (loop_param_lower >> i) & 1;
        
        if (bit) {
            line_func_add(res, &T, Q, P);
        }
    }
    // Endomorphism for Q
    bn254_fp2_t xi; memset(&xi, 0, sizeof(bn254_fp2_t)); xi.c0.bytes[31] = 9; xi.c1.bytes[31] = 1; // BE 9, BE 1
    bn254_fp_t p_minus_1, one, three, two, exp1, exp2;
    memset(&one, 0, sizeof(bn254_fp_t)); one.bytes[31] = 1;
    memset(&three, 0, sizeof(bn254_fp_t)); three.bytes[31] = 3;
    memset(&two, 0, sizeof(bn254_fp_t)); two.bytes[31] = 2;
    intx_sub(&p_minus_1, &bn254_modulus, &one);
    intx_div(&exp1, &p_minus_1, &three);
    intx_div(&exp2, &p_minus_1, &two);
    
    bn254_fp2_t xi_p_3, xi_p_2;
    fp2_pow(&xi_p_3, &xi, &exp1);
    fp2_pow(&xi_p_2, &xi, &exp2);
    
    bn254_g2_t Q1, Q2;
    Q1.x = Q->x; fp_neg(&Q1.x.c1, &Q1.x.c1); fp2_mul(&Q1.x, &Q1.x, &xi_p_3);
    Q1.y = Q->y; fp_neg(&Q1.y.c1, &Q1.y.c1); fp2_mul(&Q1.y, &Q1.y, &xi_p_2);
    memset(&Q1.z, 0, sizeof(bn254_fp2_t)); Q1.z.c0.bytes[31] = 1;
    Q2.x = Q1.x; fp_neg(&Q2.x.c1, &Q2.x.c1); fp2_mul(&Q2.x, &Q2.x, &xi_p_3);
    Q2.y = Q1.y; fp_neg(&Q2.y.c1, &Q2.y.c1); fp2_mul(&Q2.y, &Q2.y, &xi_p_2);
    memset(&Q2.z, 0, sizeof(bn254_fp2_t)); Q2.z.c0.bytes[31] = 1;
    
    // Q2 = -Q2 for the final step
    fp2_neg(&Q2.y, &Q2.y);
    
    line_func_add(res, &T, &Q1, P);
    line_func_add(res, &T, &Q2, P);
}

void bn254_final_exponentiation(bn254_fp12_t* r, const bn254_fp12_t* f) {
    bn254_init();
    bn254_fp12_t t0, t1, t2;
    
    // Easy part
    t0 = *f; fp6_neg(&t0.c1, &t0.c1);
    fp12_inv(&t1, f);
    fp12_mul_internal(&t0, &t0, &t1);
    
    fp12_frob(&t1, &t0);
    fp12_frob(&t1, &t1);
    fp12_mul_internal(&t0, &t0, &t1);
    
    bn254_fp12_t f_easy = t0;
    
    // Hard part
    uint64_t u = 4965661367192848881ULL;
    
    bn254_fp12_t a, b, a2, a3, x;
    x = f_easy;

    fp12_pow(&b, &x, u);
    // fp12_print("FE x^u", &b);
    
    fp12_sqr(&b, &b);
    // fp12_print("FE x^2u", &b);
    
    fp12_sqr(&a, &b);
    fp12_mul_internal(&a, &a, &b);
    fp12_pow(&a2, &a, u);
    fp12_mul_internal(&a, &a, &a2);
    fp12_sqr(&a3, &a2);
    fp12_pow(&a3, &a3, u);
    fp12_mul_internal(&a, &a, &a3);
    // fp12_print("FE a (part1)", &a);
    
    fp6_neg(&b.c1, &b.c1);
    fp12_mul_internal(&b, &b, &a);
    fp12_mul_internal(&a2, &a2, &a);
    fp12_frob(&a, &a);
    fp12_frob(&a, &a);
    fp12_mul_internal(&a, &a, &a2);
    fp12_mul_internal(&a, &a, &x);
    // fp12_print("FE a (part2)", &a);
    
    bn254_fp12_t y = x;
    fp6_neg(&y.c1, &y.c1);
    fp12_mul_internal(&y, &y, &b);
    fp12_frob(&b, &b);
    fp12_mul_internal(&a, &a, &b);
    fp12_frob(&y, &y);
    fp12_frob(&y, &y);
    fp12_frob(&y, &y);
    fp12_mul_internal(&y, &y, &a);
    // fp12_print("FE y (final)", &y);
    
    *r = y;
}

bool bn254_pairing_batch_check(const bn254_g1_t* P, const bn254_g2_t* Q, size_t count) {
    bn254_init();
    bn254_fp12_t res, miller;
    memset(&res, 0, sizeof(bn254_fp12_t)); res.c0.c0.c0.bytes[31] = 1; // BE 1
    
    for (size_t i=0; i<count; i++) {
        bn254_miller_loop(&miller, &P[i], &Q[i]);
        fp12_mul_internal(&res, &res, &miller);
    }
    bn254_final_exponentiation(&res, &res);
    
    // fp12_print("DEBUG FINAL PAIRING", &res);
    
    return bn254_fp12_is_one(&res);
}

#ifdef __cplusplus
}
#endif
