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

#include "crypto.h"
#include "blst.h"
#include "bytes.h"
#include "secp256k1.h"
#include "sha2.h"
#include "sha3.h"
#include <stdlib.h> // For malloc and free
#include <string.h>

void sha256(bytes_t data, uint8_t* out) {
  SHA256_CTX ctx;
  sha256_Init(&ctx);
  sha256_Update(&ctx, data.data, data.len);
  sha256_Final(&ctx, out);
}

void keccak(bytes_t data, uint8_t* out) {
  SHA3_CTX ctx;
  sha3_256_Init(&ctx);
  sha3_Update(&ctx, data.data, data.len);
  keccak_Final(&ctx, out);
}

void sha256_merkle(bytes_t data1, bytes_t data2, uint8_t* out) {
  SHA256_CTX ctx;
  sha256_Init(&ctx);
  sha256_Update(&ctx, data1.data, data1.len);
  sha256_Update(&ctx, data2.data, data2.len);
  sha256_Final(&ctx, out);
}

static const uint8_t blst_dst[]   = "BLS_SIG_BLS12381G2_XMD:SHA-256_SSWU_RO_POP_";
static const size_t  blst_dst_len = sizeof(blst_dst) - 1;
#ifdef BLS_DESERIALIZE
bytes_t blst_deserialize_p1_affine(uint8_t* compressed_pubkeys, int num_public_keys, uint8_t* out) {
  blst_p1_affine* pubkeys = out ? (blst_p1_affine*) out : (blst_p1_affine*) safe_malloc(num_public_keys * sizeof(blst_p1_affine));
  for (int i = 0; i < num_public_keys; i++) {
    if (blst_p1_deserialize(pubkeys + i, compressed_pubkeys + i * 48) != BLST_SUCCESS) {
      safe_free(pubkeys);
      return NULL_BYTES;
    }
  }
  return bytes((uint8_t*) pubkeys, num_public_keys * sizeof(blst_p1_affine));
}
#endif
bool blst_verify(bytes32_t       message_hash,    /**< 32 bytes hashed message */
                 bls_signature_t signature,       /**< 96 bytes signature */
                 uint8_t*        public_keys,     /**< 48 bytes public key array */
                 int             num_public_keys, /**< number of public keys */
                 bytes_t         pubkeys_used,
                 bool            deserialized // if true the publickeys are already deserialized (96 bytes(p1_affine))
) {                                           /**< num_public_keys.len = num_public_keys/8 and indicates with the bits set which of the public keys are part of the signature */

  if (pubkeys_used.len != num_public_keys / 8) return false;

  // generate the aggregated pubkey
  blst_p2_affine sig;
  blst_p1_affine pubkey_aggregated;
  blst_p1        pubkey_sum;
  bool           first_key = true;
  for (int i = 0; i < num_public_keys; i++) {
    if (pubkeys_used.data[i / 8] & (1 << (i % 8))) {
      blst_p1_affine pubkey_affine;
      if (deserialized)
        pubkey_affine = ((blst_p1_affine*) public_keys)[i];
      else if (blst_p1_deserialize(&pubkey_affine, public_keys + i * 48) != BLST_SUCCESS)
        return false;

      if (first_key) {
        blst_p1_from_affine(&pubkey_sum, &pubkey_affine);
        first_key = false;
      }
      else
        blst_p1_add_or_double_affine(&pubkey_sum, &pubkey_sum, &pubkey_affine);
    }
  }
  blst_p1_to_affine(&pubkey_aggregated, &pubkey_sum);

  // deserialize signature
  if (blst_p2_deserialize(&sig, signature) != BLST_SUCCESS) return false;

  // Pairing...
  blst_pairing* ctx = (blst_pairing*) safe_malloc(blst_pairing_sizeof());
  if (!ctx) return false;
  blst_pairing_init(ctx, true, blst_dst, blst_dst_len);
  if (blst_pairing_aggregate_pk_in_g1(ctx, &pubkey_aggregated, &sig, message_hash, 32, NULL, 0) != BLST_SUCCESS) {
    safe_free(ctx);
    return false;
  }
  blst_pairing_commit(ctx);
  bool result = blst_pairing_finalverify(ctx, NULL);

  // cleanup
  safe_free(ctx);
  return result;
}

bool secp256k1_recover(const bytes32_t digest, bytes_t signature, uint8_t* pubkey) {
  uint8_t pub[65] = {0};
  if (ecdsa_recover_pub_from_sig(&secp256k1, pub, signature.data, digest, signature.data[64] % 27))
    return false;
  else
    memcpy(pubkey, pub + 1, 64);
  return true;
}
