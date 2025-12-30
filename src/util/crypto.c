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
#include "logger.h"
#include "plugin.h"
#include "secp256k1.h"
#include "sha2.h"
#include "sha3.h"
#include <stdlib.h> // For malloc and free
#include <string.h>

// ECDSA recovery id adjustment constant
// Ethereum uses recovery ids 27/28, but the underlying library expects 0/1
#define ECDSA_RECOVERY_ID_OFFSET 27

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

// BLS signature domain separation tag for Ethereum 2.0
static const uint8_t blst_dst[]   = "BLS_SIG_BLS12381G2_XMD:SHA-256_SSWU_RO_POP_";
static const size_t  blst_dst_len = sizeof(blst_dst) - 1;

#ifdef BLS_DESERIALIZE
typedef struct {
  const uint8_t*  compressed;
  blst_p1_affine* out;
  volatile int    failed;
} blst_deser_ctx_t;

static void blst_deser_body(int i, void* ctx) {
  blst_deser_ctx_t* c = (blst_deser_ctx_t*) ctx;
  if (!c || c->failed) return;
  if (blst_p1_deserialize(c->out + i, c->compressed + (size_t) i * BLS_PUBKEY_SIZE) != BLST_SUCCESS) {
    c->failed = 1;
  }
}

bytes_t blst_deserialize_p1_affine(uint8_t* compressed_pubkeys, int num_public_keys, uint8_t* out) {
  if (num_public_keys <= 0) return NULL_BYTES;

  blst_p1_affine* pubkeys = out ? (blst_p1_affine*) out : (blst_p1_affine*) safe_malloc(num_public_keys * sizeof(blst_p1_affine));

  c4_parallel_for_fn pf = c4_get_parallel_for();
  if (pf && num_public_keys >= 128) {
    blst_deser_ctx_t c = {.compressed = compressed_pubkeys, .out = pubkeys, .failed = 0};
    pf(0, num_public_keys, blst_deser_body, &c);
    if (c.failed) {
      if (!out) safe_free(pubkeys); // Only free if we allocated
      return NULL_BYTES;
    }
  }
  else {
    for (int i = 0; i < num_public_keys; i++) {
      if (blst_p1_deserialize(pubkeys + i, compressed_pubkeys + i * BLS_PUBKEY_SIZE) != BLST_SUCCESS) {
        if (!out) safe_free(pubkeys); // Only free if we allocated
        return NULL_BYTES;
      }
    }
  }
  return bytes((uint8_t*) pubkeys, num_public_keys * sizeof(blst_p1_affine));
}
#endif
bool blst_verify(bytes32_t       message_hash,
                 bls_signature_t signature,
                 uint8_t*        public_keys,
                 int             num_public_keys,
                 bytes_t         pubkeys_used,
                 bool            deserialized) {
  // Input validation
  if (num_public_keys <= 0) return false;
  if (pubkeys_used.data == NULL) return false;

  // Validate bitmask length: must be ceil(num_public_keys / 8)
  if (pubkeys_used.len != (num_public_keys + 7) / 8) return false;

  // Step 1: Aggregate the public keys according to the bitmask
  blst_p2_affine sig;
  blst_p1_affine pubkey_aggregated;
  blst_p1        pubkey_sum;
  bool           first_key = true;

  for (int i = 0; i < num_public_keys; i++) {
    // Check if this public key is included in the signature (via bitmask)
    if (pubkeys_used.data[i / 8] & (1 << (i % 8))) {
      blst_p1_affine pubkey_affine;

      if (deserialized) {
        // Public keys are already deserialized affine points
        pubkey_affine = ((blst_p1_affine*) public_keys)[i];
      }
      else {
        // Deserialize compressed public key (48 bytes)
        if (blst_p1_deserialize(&pubkey_affine, public_keys + i * BLS_PUBKEY_SIZE) != BLST_SUCCESS)
          return false;
      }

      // Aggregate the public key
      if (first_key) {
        blst_p1_from_affine(&pubkey_sum, &pubkey_affine);
        first_key = false;
      }
      else {
        blst_p1_add_or_double_affine(&pubkey_sum, &pubkey_sum, &pubkey_affine);
      }
    }
  }

  // Ensure at least one public key was aggregated
  if (first_key) return false;

  blst_p1_to_affine(&pubkey_aggregated, &pubkey_sum);

  // Step 2: Deserialize the signature
  // Signature is provided in compressed form (96 bytes). Use uncompress, not deserialize (expects 192 bytes).
  if (blst_p2_uncompress(&sig, signature) != BLST_SUCCESS) return false;

  // Step 3: Perform pairing verification
  // Verify that e(pubkey, H(message)) == e(G1, signature)
  blst_pairing* ctx = (blst_pairing*) safe_malloc(blst_pairing_sizeof());
  if (!ctx) return false;

  blst_pairing_init(ctx, true, blst_dst, blst_dst_len);

  if (blst_pairing_aggregate_pk_in_g1(ctx, &pubkey_aggregated, &sig, message_hash, BYTES32_SIZE, NULL, 0) != BLST_SUCCESS) {
    safe_free(ctx);
    return false;
  }

  blst_pairing_commit(ctx);
  bool result = blst_pairing_finalverify(ctx, NULL);

  // Cleanup
  safe_free(ctx);
  return result;
}

bool secp256k1_recover(const bytes32_t digest, bytes_t signature, uint8_t* pubkey) {
  // Input validation
  if (signature.data == NULL) return false;

  // Signature must be exactly 65 bytes (r || s || v)
  if (signature.len != SECP256K1_SIGNATURE_SIZE) return false;

  uint8_t pub[SECP256K1_SIGNATURE_SIZE] = {0};

  // Recover public key from signature
  // The recovery id (v) is adjusted by subtracting the Ethereum offset (27)
  uint8_t recovery_id = signature.data[64];
  if (recovery_id >= ECDSA_RECOVERY_ID_OFFSET) {
    recovery_id -= ECDSA_RECOVERY_ID_OFFSET;
  }

  if (ecdsa_recover_pub_from_sig(&secp256k1, pub, signature.data, digest, recovery_id))
    return false;

  // Copy the uncompressed public key (skip the 0x04 prefix byte)
  memcpy(pubkey, pub + 1, SECP256K1_PUBKEY_SIZE);
  return true;
}

bool secp256k1_sign(const bytes32_t sk, const bytes32_t digest, uint8_t* signature) {
  // Sign the digest
  // signature format: r (32 bytes) || s (32 bytes) || v (1 byte recovery id)
  int result = ecdsa_sign_digest(&secp256k1, sk, digest, signature, signature + 64, NULL);

  // Adjust recovery id to Ethereum format (add 27)
  signature[64] += ECDSA_RECOVERY_ID_OFFSET;

  return result == 0; // 0 indicates success
}