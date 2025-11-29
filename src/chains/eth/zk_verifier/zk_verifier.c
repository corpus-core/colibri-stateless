#include "zk_verifier.h"
#include "../bn254/bn254.h"
#include "crypto.h" // util/crypto.h
#include "zk_verifier_constants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Helpers
// (Debug helpers removed)

bool verify_zk_proof(bytes_t proof, bytes_t public_inputs) {
  // 1. Parse Proof
  if (proof.len != 260) {
    fprintf(stderr, "Invalid proof length: %u (expected 260)\n", proof.len);
    return false;
  }

  bn254_g1_t A, C;
  bn254_g2_t B;

  const uint8_t* p = proof.data + 4;
  if (!bn254_g1_from_bytes_be(&A, p)) {
    fprintf(stderr, "Failed to parse A\n");
    return false;
  }
  p += 64;
  // Use ETH loading (Im, Re) for proof points (standard Ethereum format)
  if (!bn254_g2_from_bytes_eth(&B, p)) {
    fprintf(stderr, "Failed to parse B\n");
    return false;
  }
  p += 128;
  if (!bn254_g1_from_bytes_be(&C, p)) {
    fprintf(stderr, "Failed to parse C\n");
    return false;
  }

  // 2. Compute Public Inputs Hash
  uint8_t pub_hash_bytes[32];

  // Use wrapper from crypto.h
  sha256(public_inputs, pub_hash_bytes);

  // Mask to 253 bits (SP1/Groth16 standard behavior)
  pub_hash_bytes[0] &= 0x1f;

  uint256_t pub_hash;
  memset(pub_hash.bytes, 0, 32);
  memcpy(pub_hash.bytes, pub_hash_bytes, 32);

  // 3. Load VK Constants
  bn254_g1_t alpha, ic0, ic1, ic2;
  bn254_g2_t beta_neg, gamma_neg, delta_neg;

  {
    uint8_t tmp[64];
    memcpy(tmp, VK_ALPHA_X, 32);
    memcpy(tmp + 32, VK_ALPHA_Y, 32);
    if (!bn254_g1_from_bytes_be(&alpha, tmp)) return false;

    memcpy(tmp, VK_IC0_X, 32);
    memcpy(tmp + 32, VK_IC0_Y, 32);
    if (!bn254_g1_from_bytes_be(&ic0, tmp)) return false;

    memcpy(tmp, VK_IC1_X, 32);
    memcpy(tmp + 32, VK_IC1_Y, 32);
    if (!bn254_g1_from_bytes_be(&ic1, tmp)) return false;

    memcpy(tmp, VK_IC2_X, 32);
    memcpy(tmp + 32, VK_IC2_Y, 32);
    if (!bn254_g1_from_bytes_be(&ic2, tmp)) return false;
  }

  {
    uint8_t tmp[128];
    memcpy(tmp, VK_BETA_NEG_X0, 32);
    memcpy(tmp + 32, VK_BETA_NEG_X1, 32);
    memcpy(tmp + 64, VK_BETA_NEG_Y0, 32);
    memcpy(tmp + 96, VK_BETA_NEG_Y1, 32);
    if (!bn254_g2_from_bytes_raw(&beta_neg, tmp)) return false;

    memcpy(tmp, VK_GAMMA_NEG_X0, 32);
    memcpy(tmp + 32, VK_GAMMA_NEG_X1, 32);
    memcpy(tmp + 64, VK_GAMMA_NEG_Y0, 32);
    memcpy(tmp + 96, VK_GAMMA_NEG_Y1, 32);
    if (!bn254_g2_from_bytes_raw(&gamma_neg, tmp)) return false;

    memcpy(tmp, VK_DELTA_NEG_X0, 32);
    memcpy(tmp + 32, VK_DELTA_NEG_X1, 32);
    memcpy(tmp + 64, VK_DELTA_NEG_Y0, 32);
    memcpy(tmp + 96, VK_DELTA_NEG_Y1, 32);
    if (!bn254_g2_from_bytes_raw(&delta_neg, tmp)) return false;
  }

  // 4. Compute L
  uint256_t vkey_fr;
  memset(vkey_fr.bytes, 0, 32);
  memcpy(vkey_fr.bytes, VK_PROGRAM_HASH, 32);

  bn254_g1_t L, t1, t2;
  L = ic0;

  bn254_g1_mul(&t1, &ic1, &vkey_fr);
  bn254_g1_mul(&t2, &ic2, &pub_hash);

  bn254_g1_add(&L, &L, &t1);
  bn254_g1_add(&L, &L, &t2);

  // 5. Pairing Check
  // e(A, B) * e(C, delta_neg) * e(alpha, beta_neg) * e(L, gamma_neg) == 1

  bn254_g1_t P[4] = {A, C, alpha, L};
  bn254_g2_t Q[4] = {B, delta_neg, beta_neg, gamma_neg};

  return bn254_pairing_batch_check(P, Q, 4);
}
