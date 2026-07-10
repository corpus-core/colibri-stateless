#include "zk_verifier.h"
#include "../bn254/bn254.h"
#include "crypto.h" // util/crypto.h
#include "zk_verifier_constants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Helpers
// (Debug helpers removed)

// Registry node structure
typedef struct vk_node {
  zk_vk_t         vk;
  struct vk_node* next;
} vk_node_t;

static vk_node_t* vk_registry = NULL;

void c4_zk_register_vk(const zk_vk_t* vk) {
  // Check if already registered
  if (c4_zk_get_vk(vk->program_hash) != NULL) return;

  vk_node_t* node = (vk_node_t*) malloc(sizeof(vk_node_t));
  if (!node) return; // OOM

  // Shallow copy the struct
  node->vk = *vk;
  // Deep copy the ic array to ensure persistence if input is stack-allocated
  if (vk->ic_count > 0) {
    node->vk.ic = (bn254_g1_t*) malloc(sizeof(bn254_g1_t) * vk->ic_count);
    if (node->vk.ic) {
      memcpy(node->vk.ic, vk->ic, sizeof(bn254_g1_t) * vk->ic_count);
    }
    else {
      free(node);
      return;
    }
  }

  node->next  = vk_registry;
  vk_registry = node;
}

const zk_vk_t* c4_zk_get_vk(const uint8_t* program_hash) {
  vk_node_t* curr = vk_registry;
  while (curr) {
    if (memcmp(curr->vk.program_hash, program_hash, 32) == 0) {
      return &curr->vk;
    }
    curr = curr->next;
  }
  return NULL;
}

static void init_default_vk(void) {
  static bool initialized = false;
  if (initialized) return;

  zk_vk_t vk;
  memset(&vk, 0, sizeof(vk));

  // Copy Program Hash
  memcpy(vk.program_hash, VK_PROGRAM_HASH, 32);

  // Parse Alpha
  uint8_t tmp[128]; // Buffer for points (up to 128 bytes)
  memcpy(tmp, VK_ALPHA_X, 32);
  memcpy(tmp + 32, VK_ALPHA_Y, 32);
  bn254_g1_from_bytes_be(&vk.alpha, tmp);

  // Parse Beta, Gamma, Delta (G2)
  memcpy(tmp, VK_BETA_NEG_X0, 32);
  memcpy(tmp + 32, VK_BETA_NEG_X1, 32);
  memcpy(tmp + 64, VK_BETA_NEG_Y0, 32);
  memcpy(tmp + 96, VK_BETA_NEG_Y1, 32);
  bn254_g2_from_bytes_raw(&vk.beta_neg, tmp);

  memcpy(tmp, VK_GAMMA_NEG_X0, 32);
  memcpy(tmp + 32, VK_GAMMA_NEG_X1, 32);
  memcpy(tmp + 64, VK_GAMMA_NEG_Y0, 32);
  memcpy(tmp + 96, VK_GAMMA_NEG_Y1, 32);
  bn254_g2_from_bytes_raw(&vk.gamma_neg, tmp);

  memcpy(tmp, VK_DELTA_NEG_X0, 32);
  memcpy(tmp + 32, VK_DELTA_NEG_X1, 32);
  memcpy(tmp + 64, VK_DELTA_NEG_Y0, 32);
  memcpy(tmp + 96, VK_DELTA_NEG_Y1, 32);
  bn254_g2_from_bytes_raw(&vk.delta_neg, tmp);

  // Parse IC. SP1 v6 Groth16 commits 5 public inputs (vkey_hash, public_values_digest,
  // exit_code, vk_root, proof_nonce), so the verifying key carries 6 IC points
  // (CONSTANT + PUB_0..PUB_4).
  vk.ic_count = 6;
  // Use stack array temporarily, c4_zk_register_vk will allocate heap copy
  bn254_g1_t ics[6];

  memcpy(tmp, VK_IC0_X, 32);
  memcpy(tmp + 32, VK_IC0_Y, 32);
  bn254_g1_from_bytes_be(&ics[0], tmp);

  memcpy(tmp, VK_IC1_X, 32);
  memcpy(tmp + 32, VK_IC1_Y, 32);
  bn254_g1_from_bytes_be(&ics[1], tmp);

  memcpy(tmp, VK_IC2_X, 32);
  memcpy(tmp + 32, VK_IC2_Y, 32);
  bn254_g1_from_bytes_be(&ics[2], tmp);

  memcpy(tmp, VK_IC3_X, 32);
  memcpy(tmp + 32, VK_IC3_Y, 32);
  bn254_g1_from_bytes_be(&ics[3], tmp);

  memcpy(tmp, VK_IC4_X, 32);
  memcpy(tmp + 32, VK_IC4_Y, 32);
  bn254_g1_from_bytes_be(&ics[4], tmp);

  memcpy(tmp, VK_IC5_X, 32);
  memcpy(tmp + 32, VK_IC5_Y, 32);
  bn254_g1_from_bytes_be(&ics[5], tmp);

  vk.ic = ics;

  c4_zk_register_vk(&vk);
  initialized = true;
}

// SP1 v6 Groth16 proof layout (356 bytes). Compared to v5 (260 bytes, 2 public
// inputs) the proof now carries three additional 32-byte public inputs in front of
// the (A, B, C) curve points:
//
//   [0   .. 4)    selector    -- sha256(groth16_vk)[0..4]; not checked here, the pairing
//                                binds the proof to the hard-coded verifying key.
//   [4   .. 36)   exit_code   -- bn254 Fr (big-endian); must be 0 (guest halted cleanly).
//   [36  .. 68)   vk_root     -- bn254 Fr (big-endian); must equal VK_ROOT (recursion anchor).
//   [68  .. 100)  proof_nonce -- bn254 Fr (big-endian).
//   [100 .. 164)  A           -- G1 point, big-endian X || Y.
//   [164 .. 292)  B           -- G2 point, EIP-197 order (imaginary part first).
//   [292 .. 356)  C           -- G1 point, big-endian X || Y.
#define ZK_PROOF_LEN_V6  356
#define ZK_OFF_EXIT_CODE 4
#define ZK_OFF_VK_ROOT   36
#define ZK_OFF_NONCE     68
#define ZK_OFF_A         100
#define ZK_OFF_B         164
#define ZK_OFF_C         292
#define ZK_NUM_INPUTS    5

bool c4_verify_zk_proof(bytes_t proof, bytes_t public_inputs, const uint8_t* program_hash) {
  init_default_vk();
  const zk_vk_t* vk = c4_zk_get_vk(program_hash);
  if (!vk) {
    fprintf(stderr, "ZK Verifier: VK not found for program hash\n");
    return false;
  }

  // 1. Validate proof length and the number of available IC points.
  if (proof.len != ZK_PROOF_LEN_V6) {
    fprintf(stderr, "Invalid proof length: %u (expected %u)\n", (unsigned) proof.len, (unsigned) ZK_PROOF_LEN_V6);
    return false;
  }
  if (vk->ic_count < ZK_NUM_INPUTS + 1) {
    fprintf(stderr, "ZK Verifier: VK has insufficient IC points\n");
    return false;
  }

  // 2. Enforce the constant public inputs that are NOT derived from the application:
  //    exit_code must be zero (the guest must have halted successfully) and vk_root must
  //    match the trusted SP1 recursion VK merkle root. Both are folded into L below, so
  //    without these checks an attacker could supply a proof for a panicking guest or a
  //    proof rooted at a different (attacker-chosen) recursion VK set.
  for (uint32_t i = 0; i < 32; i++) {
    if (proof.data[ZK_OFF_EXIT_CODE + i] != 0) {
      fprintf(stderr, "ZK Verifier: non-zero exit_code in proof\n");
      return false;
    }
  }
  if (memcmp(proof.data + ZK_OFF_VK_ROOT, VK_ROOT, 32) != 0) {
    fprintf(stderr, "ZK Verifier: vk_root mismatch\n");
    return false;
  }

  // 3. Parse the Groth16 curve points (A, B, C).
  bn254_g1_t A, C;
  bn254_g2_t B;
  if (!bn254_g1_from_bytes_be(&A, proof.data + ZK_OFF_A)) {
    fprintf(stderr, "Failed to parse A\n");
    return false;
  }
  if (!bn254_g2_from_bytes_eth(&B, proof.data + ZK_OFF_B)) {
    fprintf(stderr, "Failed to parse B\n");
    return false;
  }
  if (!bn254_g1_from_bytes_be(&C, proof.data + ZK_OFF_C)) {
    fprintf(stderr, "Failed to parse C\n");
    return false;
  }

  // 4. Compute the masked public-values digest (SP1 hashes the serialized public values
  //    with sha256 and clears the top 3 bits to fit into the 253-bit bn254 scalar field).
  uint8_t pub_hash_bytes[32];
  sha256(public_inputs, pub_hash_bytes);
  pub_hash_bytes[0] &= 0x1f;

  // 5. Assemble the five public inputs as bn254 Fr scalars (big-endian 32 bytes):
  //    in[0] = program vkey hash, in[1] = public values digest,
  //    in[2] = exit_code, in[3] = vk_root, in[4] = proof_nonce.
  uint256_t in[ZK_NUM_INPUTS];
  memset(in, 0, sizeof(in));
  memcpy(in[0].bytes, vk->program_hash, 32);
  memcpy(in[1].bytes, pub_hash_bytes, 32);
  memcpy(in[2].bytes, proof.data + ZK_OFF_EXIT_CODE, 32);
  memcpy(in[3].bytes, proof.data + ZK_OFF_VK_ROOT, 32);
  memcpy(in[4].bytes, proof.data + ZK_OFF_NONCE, 32);

  // 6. Fold the public inputs into the IC commitment: L = ic[0] + sum_i ic[i+1] * in[i].
  //    Zero scalars (exit_code and proof_nonce in the common case) are skipped, which is
  //    mathematically identical (ic * 0 = O) and avoids depending on multiply-by-zero edge
  //    cases in the curve backend.
  bn254_g1_t L = vk->ic[0];
  for (uint32_t i = 0; i < ZK_NUM_INPUTS; i++) {
    bool is_zero = true;
    for (uint32_t j = 0; j < 32; j++) {
      if (in[i].bytes[j] != 0) {
        is_zero = false;
        break;
      }
    }
    if (is_zero) continue;

    bn254_g1_t t;
    bn254_g1_mul(&t, &vk->ic[i + 1], &in[i]);
    bn254_g1_add(&L, &L, &t);
  }

  // 7. Pairing check: e(A, B) * e(C, -delta) * e(alpha, -beta) * e(L, -gamma) == 1.
  bn254_g1_t P[4] = {A, C, vk->alpha, L};
  bn254_g2_t Q[4] = {B, vk->delta_neg, vk->beta_neg, vk->gamma_neg};

  return bn254_pairing_batch_check(P, Q, 4);
}

bool verify_zk_proof(bytes_t proof, bytes_t public_inputs) {

  return c4_verify_zk_proof(proof, public_inputs, VK_PROGRAM_HASH);
}
