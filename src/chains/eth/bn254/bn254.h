/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */

#ifndef ETH_BN254_H
#define ETH_BN254_H

#ifdef __cplusplus
extern "C" {
#endif

#include "intx_c_api.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Types
typedef uint256_t bn254_fp_t;

typedef struct {
    bn254_fp_t c0;
    bn254_fp_t c1;
} bn254_fp2_t;

typedef struct {
    bn254_fp2_t c0;
    bn254_fp2_t c1;
    bn254_fp2_t c2;
} bn254_fp6_t;

typedef struct {
    bn254_fp6_t c0;
    bn254_fp6_t c1;
} bn254_fp12_t;

typedef struct {
    bn254_fp_t x;
    bn254_fp_t y;
    bn254_fp_t z; // Jacobian: (x, y, z) => (x/z^2, y/z^3)
} bn254_g1_t;

typedef struct {
    bn254_fp2_t x;
    bn254_fp2_t y;
    bn254_fp2_t z; // Jacobian
} bn254_g2_t;

// Initialization / Constants
// Loads internal constants (modulus, etc.)
void bn254_init(void);

// Point Parsing (Big Endian bytes)
// G1: [X (32)][Y (32)]
bool bn254_g1_from_bytes(bn254_g1_t* p, const uint8_t* bytes);
bool bn254_g1_from_bytes_be(bn254_g1_t* p, const uint8_t* bytes); // explicit alias

// G2: [X1 (32)][X0 (32)][Y1 (32)][Y0 (32)] (ETH format: Im, Re, Im, Re)
bool bn254_g2_from_bytes_eth(bn254_g2_t* p, const uint8_t* bytes);

// G2: [X0 (32)][X1 (32)][Y0 (32)][Y1 (32)] (MCL/Other format: Re, Im, Re, Im)
bool bn254_g2_from_bytes_raw(bn254_g2_t* p, const uint8_t* bytes);

// Serialization
void bn254_g1_to_bytes(const bn254_g1_t* p, uint8_t* out);
void bn254_g2_to_bytes_eth(const bn254_g2_t* p, uint8_t* out);

// Arithmetic
void bn254_g1_add(bn254_g1_t* r, const bn254_g1_t* a, const bn254_g1_t* b);
void bn254_g1_mul(bn254_g1_t* r, const bn254_g1_t* p, const uint256_t* scalar);
bool bn254_g1_is_on_curve(const bn254_g1_t* p);

void bn254_g2_add(bn254_g2_t* r, const bn254_g2_t* a, const bn254_g2_t* b);
void bn254_g2_mul(bn254_g2_t* r, const bn254_g2_t* p, const uint256_t* scalar); // Optional, if needed

// Pairing
void bn254_miller_loop(bn254_fp12_t* res, const bn254_g1_t* P, const bn254_g2_t* Q);
void bn254_final_exponentiation(bn254_fp12_t* r, const bn254_fp12_t* f);

// High-level pairing helpers
// Multi-pairing: result = final_exp( product( miller(P_i, Q_i) ) )
// Returns true if result == 1
bool bn254_pairing_batch_check(const bn254_g1_t* P, const bn254_g2_t* Q, size_t count);

// Accumulate into a generic FP12 result (for custom logic like ZK verify)
void bn254_fp12_mul(bn254_fp12_t* r, const bn254_fp12_t* a, const bn254_fp12_t* b);
bool bn254_fp12_is_one(const bn254_fp12_t* a);

#ifdef __cplusplus
}
#endif

#endif // ETH_BN254_H

