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

#ifndef crypto_h__
#define crypto_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "bytes.h"
#include <stdbool.h>
#include <stdint.h>

// : APIs

// :: Internal APIs

// ::: crypto.h
//
// Helper functions for crypto operations.
//

// Size constants for cryptographic types
#define ADDRESS_SIZE             20
#define BYTES32_SIZE             32
#define BLS_PUBKEY_SIZE          48
#define BLS_SIGNATURE_SIZE       96
#define SECP256K1_PUBKEY_SIZE    64
#define SECP256K1_SIGNATURE_SIZE 65

/**
 * Ethereum address type (20 bytes)
 */
typedef uint8_t address_t[ADDRESS_SIZE];

/**
 * 32-byte hash or value type
 */
typedef uint8_t bytes32_t[BYTES32_SIZE];

/**
 * BLS12-381 public key type (48 bytes compressed)
 */
typedef uint8_t bls_pubkey_t[BLS_PUBKEY_SIZE];

/**
 * BLS12-381 signature type (96 bytes)
 */
typedef uint8_t bls_signature_t[BLS_SIGNATURE_SIZE];

/**
 * Computes the Keccak-256 hash of the input data.
 * @param data The input data to hash
 * @param out Pointer to 32-byte buffer to store the hash result (must not be NULL)
 */
void keccak(bytes_t data, uint8_t* out) NONNULL_FOR((2));

/**
 * Computes the SHA-256 hash of the input data.
 * @param data The input data to hash
 * @param out Pointer to 32-byte buffer to store the hash result (must not be NULL)
 */
void sha256(bytes_t data, uint8_t* out) NONNULL_FOR((2));

/**
 * Computes the SHA-256 hash of two concatenated data buffers (merkle node hash).
 * This is equivalent to `sha256(data1 || data2)` but more efficient.
 * @param data1 The first data buffer
 * @param data2 The second data buffer
 * @param out Pointer to 32-byte buffer to store the hash result (must not be NULL)
 */
void sha256_merkle(bytes_t data1, bytes_t data2, uint8_t* out) NONNULL_FOR((3));

#ifdef BLS_DESERIALIZE
/**
 * Deserializes compressed BLS12-381 public keys into affine point representation.
 * This is used as an optimization to avoid repeated deserialization during verification.
 * @param compressed_pubkeys Pointer to compressed public keys (48 bytes each, must not be NULL)
 * @param num_public_keys The number of public keys to deserialize
 * @param out Optional pre-allocated buffer for the result. If NULL, memory will be allocated.
 * @return A bytes_t containing the deserialized public keys, or NULL_BYTES on error
 */
bytes_t blst_deserialize_p1_affine(uint8_t* compressed_pubkeys, int num_public_keys, uint8_t* out) NONNULL_FOR((1, 3));
#endif

/**
 * Verifies a BLS12-381 aggregate signature against a message and a set of public keys.
 * This function aggregates the specified public keys and verifies the signature using
 * pairing operations on the BLS12-381 curve.
 *
 * Example:
 * ```c
 * bytes32_t msg_hash;
 * bls_signature_t sig;
 * uint8_t pubkeys[3 * 48];  // 3 public keys
 * uint8_t bitmask[1] = {0b00000101};  // Use keys 0 and 2
 * bool valid = blst_verify(msg_hash, sig, pubkeys, 3, bytes(bitmask, 1), false);
 * ```
 *
 * @param message 32-byte hashed message (must not be NULL)
 * @param signature 96-byte BLS signature (must not be NULL)
 * @param public_keys Array of public keys, either 48 bytes each (compressed) or 96 bytes each (deserialized affine points) (must not be NULL)
 * @param num_public_keys The total number of public keys in the array
 * @param pubkey_bitmask Bitmask indicating which public keys to aggregate (length must be num_public_keys/8)
 * @param deserialized If true, public_keys contains deserialized affine points (96 bytes each); if false, compressed keys (48 bytes each)
 * @return true if the signature is valid, false otherwise
 */
bool blst_verify(bytes32_t       message,
                 bls_signature_t signature,
                 uint8_t*        public_keys,
                 int             num_public_keys,
                 bytes_t         pubkey_bitmask,
                 bool            deserialized) NONNULL_FOR((1, 2, 3));

/**
 * Recovers the public key from a secp256k1 ECDSA signature.
 * Used primarily in Ethereum to derive the signer's address from a transaction signature.
 * @param digest The 32-byte message digest that was signed (must not be NULL)
 * @param signature The signature bytes (must be exactly 65 bytes: r||s||v where v is the recovery id)
 * @param pubkey Pointer to 64-byte buffer to store the recovered uncompressed public key (must not be NULL)
 * @return true if recovery was successful, false on error (invalid signature or recovery id)
 */
bool secp256k1_recover(const bytes32_t digest, bytes_t signature, uint8_t* pubkey) NONNULL_FOR((1, 3));

/**
 * Signs a digest with a secp256k1 private key using ECDSA.
 * @param sk The 32-byte secret key (must not be NULL)
 * @param digest The 32-byte message digest to sign (must not be NULL)
 * @param signature Pointer to 65-byte buffer to store the signature (r||s||v format, must not be NULL)
 * @return true if signing was successful, false on error (invalid private key)
 */
bool secp256k1_sign(const bytes32_t sk, const bytes32_t digest, uint8_t* signature) NONNULL;

#ifdef __cplusplus
}
#endif

#endif