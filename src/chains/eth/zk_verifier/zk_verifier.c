#include "zk_verifier.h"
#include "zk_verifier_constants.h"
#include "crypto.h" // util/crypto.h
#include <mcl/bn_c256.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Global flag to ensure mcl is initialized only once
static bool mcl_initialized = false;

static bool init_mcl() {
    if (mcl_initialized) return true;
    // Use mclBn_CurveSNARK1 (ID 4) for Ethereum BN254
    int ret = mclBn_init(mclBn_CurveSNARK1, MCLBN_COMPILED_TIME_VAR);
    if (ret != 0) {
        fprintf(stderr, "Failed to initialize MCL: %d\n", ret);
        return false;
    }
    mcl_initialized = true;
    return true;
}

// Helper to set G1 point directly from bytes [X(32)][Y(32)]
static int set_g1_from_bytes(mclBnG1 *p, const uint8_t *bytes) {
    // Set X
    if (mclBnFp_setBigEndianMod(&p->x, bytes, 32) != 0) return -1;
    // Set Y
    if (mclBnFp_setBigEndianMod(&p->y, bytes + 32, 32) != 0) return -1;
    // Set Z = 1 (Affine)
    mclBnFp_setInt32(&p->z, 1);
    
    // Validate point
    if (mclBnG1_isValid(p) == 0) return -1;
    return 0;
}

// Helper to set G2 point from MCL-ordered bytes [X0(32)][X1(32)][Y0(32)][Y1(32)] (Re, Im, Re, Im)
static int set_g2_from_bytes(mclBnG2 *p, const uint8_t *bytes) {
    mclBnFp x0, x1, y0, y1;
    
    if (mclBnFp_setBigEndianMod(&x0, bytes, 32) != 0) return -1;
    if (mclBnFp_setBigEndianMod(&x1, bytes + 32, 32) != 0) return -1;
    if (mclBnFp_setBigEndianMod(&y0, bytes + 64, 32) != 0) return -1;
    if (mclBnFp_setBigEndianMod(&y1, bytes + 96, 32) != 0) return -1;
    
    p->x.d[0] = x0; // Real
    p->x.d[1] = x1; // Imaginary
    p->y.d[0] = y0;
    p->y.d[1] = y1;
    mclBnFp_setInt32(&p->z.d[0], 1);
    mclBnFp_clear(&p->z.d[1]); // Z = 1 + 0i
    
    if (mclBnG2_isValid(p) == 0) return -1;
    return 0;
}

// Helper to set G2 point from ETH-ordered bytes [X1(32)][X0(32)][Y1(32)][Y0(32)] (Im, Re, Im, Re)
static int set_g2_from_eth_bytes(mclBnG2 *p, const uint8_t *bytes) {
    mclBnFp x0, x1, y0, y1;
    
    // X1 (Imaginary) is first
    if (mclBnFp_setBigEndianMod(&x1, bytes, 32) != 0) return -1;
    // X0 (Real) is second
    if (mclBnFp_setBigEndianMod(&x0, bytes + 32, 32) != 0) return -1;
    
    // Y1 (Imaginary) is third
    if (mclBnFp_setBigEndianMod(&y1, bytes + 64, 32) != 0) return -1;
    // Y0 (Real) is fourth
    if (mclBnFp_setBigEndianMod(&y0, bytes + 96, 32) != 0) return -1;
    
    p->x.d[0] = x0;
    p->x.d[1] = x1;
    p->y.d[0] = y0;
    p->y.d[1] = y1;
    mclBnFp_setInt32(&p->z.d[0], 1);
    mclBnFp_clear(&p->z.d[1]);
    
    if (mclBnG2_isValid(p) == 0) return -1;
    return 0;
}

bool verify_zk_proof(bytes_t proof, bytes_t public_inputs) {
    if (!init_mcl()) return false;

    // 1. Parse Proof
    if (proof.len != 260) {
        fprintf(stderr, "Invalid proof length: %u (expected 260)\n", proof.len);
        return false;
    }

    mclBnG1 A, C;
    mclBnG2 B;
    
    const uint8_t *p = proof.data + 4;
    if (set_g1_from_bytes(&A, p) != 0) { fprintf(stderr, "Failed to parse A\n"); return false; }
    p += 64;
    if (set_g2_from_eth_bytes(&B, p) != 0) { fprintf(stderr, "Failed to parse B\n"); return false; }
    p += 128;
    if (set_g1_from_bytes(&C, p) != 0) { fprintf(stderr, "Failed to parse C\n"); return false; }
    
    // 2. Compute Public Inputs Hash
    uint8_t pub_hash_bytes[32];
    
    // Use wrapper from crypto.h
    sha256(public_inputs, pub_hash_bytes);
    
    // Mask to 253 bits (SP1/Groth16 standard behavior)
    pub_hash_bytes[0] &= 0x1f; 
    
    mclBnFr pub_hash;
    if (mclBnFr_setBigEndianMod(&pub_hash, pub_hash_bytes, 32) != 0) return false;

    // 3. Load VK Constants
    mclBnG1 alpha, ic0, ic1, ic2;
    mclBnG2 beta_neg, gamma_neg, delta_neg;
    
    {
        uint8_t tmp[64];
        memcpy(tmp, VK_ALPHA_X, 32); memcpy(tmp+32, VK_ALPHA_Y, 32);
        if(set_g1_from_bytes(&alpha, tmp)) return false;
        
        memcpy(tmp, VK_IC0_X, 32); memcpy(tmp+32, VK_IC0_Y, 32);
        if(set_g1_from_bytes(&ic0, tmp)) return false;

        memcpy(tmp, VK_IC1_X, 32); memcpy(tmp+32, VK_IC1_Y, 32);
        if(set_g1_from_bytes(&ic1, tmp)) return false;

        memcpy(tmp, VK_IC2_X, 32); memcpy(tmp+32, VK_IC2_Y, 32);
        if(set_g1_from_bytes(&ic2, tmp)) return false;
    }
    
    {
        uint8_t tmp[128];
        memcpy(tmp, VK_BETA_NEG_X0, 32); memcpy(tmp+32, VK_BETA_NEG_X1, 32);
        memcpy(tmp+64, VK_BETA_NEG_Y0, 32); memcpy(tmp+96, VK_BETA_NEG_Y1, 32);
        if(set_g2_from_bytes(&beta_neg, tmp)) return false;

        memcpy(tmp, VK_GAMMA_NEG_X0, 32); memcpy(tmp+32, VK_GAMMA_NEG_X1, 32);
        memcpy(tmp+64, VK_GAMMA_NEG_Y0, 32); memcpy(tmp+96, VK_GAMMA_NEG_Y1, 32);
        if(set_g2_from_bytes(&gamma_neg, tmp)) return false;

        memcpy(tmp, VK_DELTA_NEG_X0, 32); memcpy(tmp+32, VK_DELTA_NEG_X1, 32);
        memcpy(tmp+64, VK_DELTA_NEG_Y0, 32); memcpy(tmp+96, VK_DELTA_NEG_Y1, 32);
        if(set_g2_from_bytes(&delta_neg, tmp)) return false;
    }
    
    // 4. Compute L
    mclBnFr vkey_fr;
    if (mclBnFr_setBigEndianMod(&vkey_fr, VK_PROGRAM_HASH, 32) != 0) return false;
    
    mclBnG1 L, t1, t2;
    L = ic0;
    
    mclBnG1_mul(&t1, &ic1, &vkey_fr);
    mclBnG1_mul(&t2, &ic2, &pub_hash);
    
    mclBnG1_add(&L, &L, &t1);
    mclBnG1_add(&L, &L, &t2);
    
    // 5. Pairing Check
    mclBnGT e1, e2, e3, e4, res;
    mclBn_pairing(&e1, &A, &B);
    mclBn_pairing(&e2, &C, &delta_neg);
    mclBn_pairing(&e3, &alpha, &beta_neg);
    mclBn_pairing(&e4, &L, &gamma_neg);
    
    mclBnGT_mul(&res, &e1, &e2);
    mclBnGT_mul(&res, &res, &e3);
    mclBnGT_mul(&res, &res, &e4);
    
    if (mclBnGT_isOne(&res)) {
        return true;
    } else {
        return false;
    }
}
