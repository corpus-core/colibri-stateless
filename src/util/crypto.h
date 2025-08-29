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

typedef uint8_t address_t[20];
typedef uint8_t bytes32_t[32];
typedef uint8_t bls_pubkey_t[48];
typedef uint8_t bls_signature_t[96];

void keccak(bytes_t data, uint8_t* out);
void sha256(bytes_t data, uint8_t* out);
void sha256_merkle(bytes_t data1, bytes_t data2, uint8_t* out);
#ifdef BLS_DESERIALIZE
bytes_t blst_deserialize_p1_affine(uint8_t* compressed_pubkeys, int num_public_keys, uint8_t* out);
#endif

bool blst_verify(bytes32_t       message,         /**< 32 bytes hashed message */
                 bls_signature_t signature,       /**< 96 bytes signature */
                 uint8_t*        public_keys,     /**< 48 bytes public key array */
                 int             num_public_keys, /**< number of public keys */
                 bytes_t         pibkey_bitmask,  /**< num_public_keys.len = num_public_keys/8 and indicates with the bits set which of the public keys are part of the signature */
                 bool            deserialized);

bool secp256k1_recover(const bytes32_t digest, bytes_t signature, uint8_t* pubkey);
/**
 * @brief Sign a digest with a private key
 * @param pk The private key
 * @param digest The digest to sign
 * @param signature The signature (65 bytes with th last byte as recovery bit)
 */
void secp256k1_sign(const bytes32_t pk, const bytes32_t digest, uint8_t* signature);

#ifdef __cplusplus
}
#endif

#endif