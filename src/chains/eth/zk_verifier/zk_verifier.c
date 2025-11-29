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
    zk_vk_t vk;
    struct vk_node* next;
} vk_node_t;

static vk_node_t* vk_registry = NULL;

void c4_zk_register_vk(const zk_vk_t* vk) {
    // Check if already registered
    if (c4_zk_get_vk(vk->program_hash) != NULL) return;

    vk_node_t* node = (vk_node_t*)malloc(sizeof(vk_node_t));
    if (!node) return; // OOM
    
    // Shallow copy the struct
    node->vk = *vk;
    // Deep copy the ic array to ensure persistence if input is stack-allocated
    if (vk->ic_count > 0) {
        node->vk.ic = (bn254_g1_t*)malloc(sizeof(bn254_g1_t) * vk->ic_count);
        if (node->vk.ic) {
            memcpy(node->vk.ic, vk->ic, sizeof(bn254_g1_t) * vk->ic_count);
        } else {
            free(node);
            return;
        }
    }
    
    node->next = vk_registry;
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
    
    // Parse IC (3 points)
    vk.ic_count = 3;
    // Use stack array temporarily, c4_zk_register_vk will allocate heap copy
    bn254_g1_t ics[3]; 
    
    memcpy(tmp, VK_IC0_X, 32); memcpy(tmp + 32, VK_IC0_Y, 32);
    bn254_g1_from_bytes_be(&ics[0], tmp);
    
    memcpy(tmp, VK_IC1_X, 32); memcpy(tmp + 32, VK_IC1_Y, 32);
    bn254_g1_from_bytes_be(&ics[1], tmp);
    
    memcpy(tmp, VK_IC2_X, 32); memcpy(tmp + 32, VK_IC2_Y, 32);
    bn254_g1_from_bytes_be(&ics[2], tmp);
    
    vk.ic = ics;
    
    c4_zk_register_vk(&vk);
    initialized = true;
}

bool c4_verify_zk_proof(bytes_t proof, bytes_t public_inputs, const uint8_t* program_hash) {
    const zk_vk_t* vk = c4_zk_get_vk(program_hash);
    if (!vk) {
        fprintf(stderr, "ZK Verifier: VK not found for program hash\n");
        return false;
    }

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
    sha256(public_inputs, pub_hash_bytes);
    pub_hash_bytes[0] &= 0x1f; // Mask to 253 bits

    uint256_t pub_hash;
    memset(pub_hash.bytes, 0, 32);
    memcpy(pub_hash.bytes, pub_hash_bytes, 32);

    // 3. Compute L
    // L = ic0 + ic1 * vkey + ic2 * pub_hash
    // vk->ic must have at least 3 elements for this specific logic.
    // If we want generic Groth16, we would need inputs array.
    // But here we have specific inputs: vkey_hash and pub_inputs_hash.
    if (vk->ic_count < 3) {
        fprintf(stderr, "ZK Verifier: VK has insufficient IC points\n");
        return false;
    }

    uint256_t vkey_fr;
    memset(vkey_fr.bytes, 0, 32);
    memcpy(vkey_fr.bytes, vk->program_hash, 32); // program_hash IS the vkey hash

    bn254_g1_t L, t1, t2;
    L = vk->ic[0];

    bn254_g1_mul(&t1, &vk->ic[1], &vkey_fr);
    bn254_g1_mul(&t2, &vk->ic[2], &pub_hash);

    bn254_g1_add(&L, &L, &t1);
    bn254_g1_add(&L, &L, &t2);

    // 5. Pairing Check
    bn254_g1_t P[4] = {A, C, vk->alpha, L};
    bn254_g2_t Q[4] = {B, vk->delta_neg, vk->beta_neg, vk->gamma_neg};

    return bn254_pairing_batch_check(P, Q, 4);
}

bool verify_zk_proof(bytes_t proof, bytes_t public_inputs) {
    init_default_vk();
    return c4_verify_zk_proof(proof, public_inputs, VK_PROGRAM_HASH);
}
