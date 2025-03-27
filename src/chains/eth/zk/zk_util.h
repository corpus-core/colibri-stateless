#ifndef ZK_UTIL_H
#define ZK_UTIL_H

#include <stdint.h>
#include <string.h>

#include <stdio.h>

typedef struct {
  uint8_t* data;
  uint32_t len;
} bytes_t;

typedef uint8_t bytes32_t[32];
#define bytes(ptr, length) \
  (bytes_t) { .data = (uint8_t*) ptr, .len = length }
static inline uint64_t get_uint64_le(uint8_t* data) {
  return (uint64_t) (data[0]) |
         ((uint64_t) (data[1]) << 8) |
         ((uint64_t) (data[2]) << 16) |
         ((uint64_t) (data[3]) << 24) |
         ((uint64_t) (data[4]) << 32) |
         ((uint64_t) (data[5]) << 40) |
         ((uint64_t) (data[6]) << 48) |
         ((uint64_t) (data[7]) << 56);
}

// we include the blst-lib but tell them it's wasm and not arm, so it does not use asm.
#define __BLST_NO_ASM__
#define __wasm64__
#undef __aarch64__
#include "blst/server.c"

// blst definitions
static const uint8_t blst_dst[]   = "BLS_SIG_BLS12381G2_XMD:SHA-256_SSWU_RO_POP_";
static const size_t  blst_dst_len = sizeof(blst_dst) - 1;

typedef struct {
  byte b[256 / 8];
} blst_scalar;
typedef struct {
  limb_t l[256 / 8 / sizeof(limb_t)];
} blst_fr;
typedef struct {
  limb_t l[384 / 8 / sizeof(limb_t)];
} blst_fp;
typedef struct {
  blst_fp fp[2];
} blst_fp2;
typedef struct {
  blst_fp2 x, y, z;
} blst_p2;
typedef struct {
  blst_fp2 x, y;
} blst_p2_affine;
typedef struct {
  blst_fp x, y, z;
} blst_p1;
typedef struct {
  blst_fp x, y;
} blst_p1_affine;

// veriries a BLS Signature
static int blst_verify(uint8_t message_hash[32],   /**< 32 bytes hashed message */
                       uint8_t signature[96],      /**< 96 bytes signature */
                       uint8_t public_keys[24576], /**< 48 bytes public key array */
                       uint8_t pubkeys_used[64]) {

  // generate the aggregated pubkey
  blst_p2_affine sig;
  blst_p1_affine pubkey_aggregated;
  blst_p1        pubkey_sum;
  int            first_key = 1;
  for (int i = 0; i < 512; i++) {
    if (pubkeys_used[i / 8] & (1 << (i % 8))) {
      blst_p1_affine pubkey_affine;
      if (blst_p1_deserialize(&pubkey_affine, public_keys + i * 48) != BLST_SUCCESS)
        return 0;

      if (first_key) {
        blst_p1_from_affine(&pubkey_sum, &pubkey_affine);
        first_key = 0;
      }
      else
        blst_p1_add_or_double_affine(&pubkey_sum, &pubkey_sum, &pubkey_affine);
    }
  }
  blst_p1_to_affine(&pubkey_aggregated, &pubkey_sum);

  // deserialize signature
  if (blst_p2_deserialize(&sig, signature) != BLST_SUCCESS) return 0;

  // Pairing...
  uint8_t ctx[3192];
  blst_pairing_init(ctx, 1, blst_dst, blst_dst_len);
  if (blst_pairing_aggregate_pk_in_g1(ctx, &pubkey_aggregated, &sig, message_hash, 32, NULL, 0) != BLST_SUCCESS) return 0;
  blst_pairing_commit(ctx);
  return blst_pairing_finalverify(ctx, NULL);
}

// hashes 2 hashes together using sha256
static void sha256_Merkle(bytes32_t left, bytes32_t right, bytes32_t out) {
  SHA256_CTX ctx = {0};
  sha256_init(&ctx);
  sha256_update(&ctx, left, 32);
  sha256_update(&ctx, right, 32);
  sha256_final(out, &ctx);
}

// runs the merkle proof from leaf to root
// the initial leaf value must be set at out before calling.
static void verify_merkle_proof(bytes_t proof_data, uint32_t gindex, bytes32_t out) {
  for (uint32_t i = 0; i < proof_data.len; i += 32, gindex >>= 1) {
    if (gindex & 1)
      sha256_Merkle(proof_data.data + i, out, out);
    else
      sha256_Merkle(out, proof_data.data + i, out);
  }
}

static void _root_hash(bytes_t keys, bytes32_t out, uint32_t gindex) {
  bytes32_t left;
  bytes32_t right;
  if (gindex >= 512) {
    memcpy(left, keys.data + (gindex - 512) * 48, 32);
    memcpy(right, keys.data + (gindex - 512) * 48 + 32, 16);
    memset(right + 16, 0, 16);
  }
  else {
    _root_hash(keys, left, gindex * 2);
    _root_hash(keys, right, gindex * 2 + 1);
  }
  sha256_Merkle(left, right, out);
}

// calculate the root hash of the pubkeys
static void create_root_hash(bytes_t keys, bytes32_t out) {
  _root_hash(keys, out, 1);
}
// hashes the 2 values together and compares it with the hash from the proof
static int verify_slot(uint8_t slot[8], uint8_t proposer[8], uint8_t proof[32]) {
  bytes32_t slot_hash     = {0};
  bytes32_t proposer_hash = {0};
  memcpy(slot_hash, slot, 8);
  memcpy(proposer_hash, proposer, 8);
  sha256_Merkle(slot_hash, proposer_hash, slot_hash);
  return (memcmp(slot_hash, proof, 32) == 0) ? 1 : 0;
}

#endif