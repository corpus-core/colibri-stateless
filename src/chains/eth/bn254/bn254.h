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
#include <stddef.h>
#include <stdint.h>

#ifdef USE_MCL
#include <mcl/bn_c384_256.h>

// Types map to MCL types
// Note: intx types are still available for scalar inputs
typedef mclBnG1 bn254_g1_t;
typedef mclBnG2 bn254_g2_t;
typedef mclBnGT bn254_fp12_t;

// Helper types not used in opaque MCL structs but might be needed if referenced
typedef mclBnFp bn254_fp_t;
typedef struct {
  bn254_fp_t c0;
  bn254_fp_t c1;
} bn254_fp2_t; // Mock or map if needed
typedef struct {
  bn254_fp2_t c0;
  bn254_fp2_t c1;
  bn254_fp2_t c2;
} bn254_fp6_t;

#else

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

#endif

// Initialization / Constants
/**
 * @brief Initializes the BN254 library constants (modulus, generator, etc.).
 * Must be called before any other function. Safe to call multiple times.
 */
void bn254_init(void);

// Point Parsing (Big Endian bytes)

/**
 * @brief Parses a G1 point from 64 bytes (Big Endian).
 * Format: [X (32 bytes)][Y (32 bytes)]
 * @param p Output G1 point (Jacobian coordinates)
 * @param bytes Input buffer (64 bytes)
 * @return true if parsing succeeded and point is on curve, false otherwise.
 */
bool bn254_g1_from_bytes(bn254_g1_t* p, const uint8_t* bytes);

/**
 * @brief Explicit alias for bn254_g1_from_bytes.
 * @see bn254_g1_from_bytes
 */
bool bn254_g1_from_bytes_be(bn254_g1_t* p, const uint8_t* bytes);

/**
 * @brief Parses a G2 point from 128 bytes in Ethereum format.
 * Format: [X_im (32)][X_re (32)][Y_im (32)][Y_re (32)]
 * Note: Ethereum precompiles use imaginary part first for Fp2 coefficients.
 * @param p Output G2 point (Jacobian coordinates)
 * @param bytes Input buffer (128 bytes)
 * @return true if parsing succeeded and point is on curve, false otherwise.
 */
bool bn254_g2_from_bytes_eth(bn254_g2_t* p, const uint8_t* bytes);

/**
 * @brief Parses a G2 point from 128 bytes in raw/MCL format.
 * Format: [X_re (32)][X_im (32)][Y_re (32)][Y_im (32)]
 * Note: This is the standard format used by many libraries (e.g. MCL, Go).
 * @param p Output G2 point (Jacobian coordinates)
 * @param bytes Input buffer (128 bytes)
 * @return true if parsing succeeded and point is on curve, false otherwise.
 */
bool bn254_g2_from_bytes_raw(bn254_g2_t* p, const uint8_t* bytes);

// Serialization

/**
 * @brief Serializes a G1 point to 64 bytes (Big Endian).
 * Format: [X (32)][Y (32)]
 * @param p Input G1 point
 * @param out Output buffer (64 bytes)
 */
void bn254_g1_to_bytes(const bn254_g1_t* p, uint8_t* out);

/**
 * @brief Serializes a G2 point to 128 bytes in Ethereum format.
 * Format: [X_im (32)][X_re (32)][Y_im (32)][Y_re (32)]
 * @param p Input G2 point
 * @param out Output buffer (128 bytes)
 */
void bn254_g2_to_bytes_eth(const bn254_g2_t* p, uint8_t* out);

// Arithmetic

/**
 * @brief Adds two G1 points: r = a + b.
 * @param r Output point
 * @param a First operand
 * @param b Second operand
 */
void bn254_g1_add(bn254_g1_t* r, const bn254_g1_t* a, const bn254_g1_t* b);

/**
 * @brief Multiplies a G1 point by a scalar: r = p * scalar.
 * @param r Output point
 * @param p Base point
 * @param scalar Scalar value (256-bit integer)
 */
void bn254_g1_mul(bn254_g1_t* r, const bn254_g1_t* p, const uint256_t* scalar);

/**
 * @brief Checks if a G1 point is on the BN254 curve.
 * @param p Point to check
 * @return true if on curve, false otherwise
 */
bool bn254_g1_is_on_curve(const bn254_g1_t* p);

/**
 * @brief Adds two G2 points: r = a + b.
 * @param r Output point
 * @param a First operand
 * @param b Second operand
 */
void bn254_g2_add(bn254_g2_t* r, const bn254_g2_t* a, const bn254_g2_t* b);

/**
 * @brief Multiplies a G2 point by a scalar: r = p * scalar.
 * @param r Output point
 * @param p Base point
 * @param scalar Scalar value
 */
void bn254_g2_mul(bn254_g2_t* r, const bn254_g2_t* p, const uint256_t* scalar);

// Pairing

/**
 * @brief Performs the Miller Loop step of the pairing: f = ML(P, Q).
 * @param res Output Fp12 element
 * @param P G1 point (affine or Jacobian)
 * @param Q G2 point (affine or Jacobian)
 */
void bn254_miller_loop(bn254_fp12_t* res, const bn254_g1_t* P, const bn254_g2_t* Q);

/**
 * @brief Performs the Final Exponentiation step: r = f^((p^12-1)/r).
 * @param r Output Fp12 element
 * @param f Input Fp12 element (result of Miller Loop)
 */
void bn254_final_exponentiation(bn254_fp12_t* r, const bn254_fp12_t* f);

// High-level pairing helpers

/**
 * @brief Checks the pairing equation: product(e(P_i, Q_i)) == 1.
 * This function performs the Miller Loop for each pair, accumulates the result,
 * performs the Final Exponentiation, and checks if the result is unity.
 * @param P Array of G1 points
 * @param Q Array of G2 points
 * @param count Number of pairs
 * @return true if the product of pairings is 1, false otherwise.
 */
bool bn254_pairing_batch_check(const bn254_g1_t* P, const bn254_g2_t* Q, size_t count);

// Accumulate into a generic FP12 result (for custom logic like ZK verify)

/**
 * @brief Multiplies two Fp12 elements: r = a * b.
 * @param r Output element
 * @param a First operand
 * @param b Second operand
 */
void bn254_fp12_mul(bn254_fp12_t* r, const bn254_fp12_t* a, const bn254_fp12_t* b);

/**
 * @brief Checks if an Fp12 element is equal to one (unity).
 * @param a Element to check
 * @return true if a == 1, false otherwise
 */
bool bn254_fp12_is_one(const bn254_fp12_t* a);

#ifdef __cplusplus
}
#endif

#endif // ETH_BN254_H
