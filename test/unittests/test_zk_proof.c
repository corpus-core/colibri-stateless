/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */

#include "c4_assert.h" // Contains read_testdata and unity includes
#include "bytes.h"
#include "ssz.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Conditionally include the header only if ETH_ZKPROOF is enabled
#ifdef ETH_ZKPROOF
// Include directory is exposed via CMake interface, so simple include works
#include "zk_verifier.h"
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef ETH_ZKPROOF

static bytes_t copy_bytes(bytes_t src) {
    bytes_t dst;
    dst.len = src.len;
    dst.data = malloc(src.len);
    if (dst.data) {
        memcpy(dst.data, src.data, src.len);
    }
    return dst;
}

// Helper to validate a specific period proof
// If expected_anchor is not NULL, validates that current_keys_root matches it.
// If expected_anchor is NULL, returns the current_keys_root found (caller must free).
static uint8_t* verify_period_and_get_anchor(int period, const uint8_t* expected_anchor) {
    char proof_path[64];
    char pub_path[64];
    snprintf(proof_path, sizeof(proof_path), "zk_data/proof_%d_raw.bin", period);
    snprintf(pub_path, sizeof(pub_path), "zk_data/public_values_%d.bin", period);
    
    bytes_t proof = read_testdata(proof_path);
    bytes_t pub = read_testdata(pub_path);
    
    if (proof.data == NULL) {
        printf("Skipping period %d: proof not found\n", period);
        if (pub.data) free(pub.data);
        return NULL;
    }
    if (pub.data == NULL) {
        printf("Skipping period %d: public values not found\n", period);
        free(proof.data);
        return NULL;
    }
    
    // Verify Proof
    bool valid = verify_zk_proof(proof, pub);
    TEST_ASSERT_TRUE_MESSAGE(valid, "ZK Proof verification failed");
    
    // Verify Structure
    // [0..31]  = Current Keys Root (Anchor)
    // [32..63] = Next Keys Root
    // [64..71] = Next Period (LE)
    TEST_ASSERT_GREATER_OR_EQUAL_INT(72, pub.len);
    
    // Check Period
    uint64_t next_period = 0;
    memcpy(&next_period, pub.data + 64, 8);
    
    // proof_N produces next_period=N.
    TEST_ASSERT_EQUAL_UINT64(period, next_period);
    
    // Check Anchor
    if (expected_anchor != NULL) {
        int cmp = memcmp(pub.data, expected_anchor, 32);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, cmp, "Anchor hash mismatch (Aggregation broken?)");
    }
    
    uint8_t* anchor_out = malloc(32);
    memcpy(anchor_out, pub.data, 32);
    
    free(proof.data);
    free(pub.data);
    
    return anchor_out;
}

void test_verify_chain(void) {
    // Define the chain to test
    int periods[] = { 1600, 1601, 1602 };
    int count = sizeof(periods) / sizeof(int);
    
    uint8_t* trust_anchor = NULL;
    
    for (int i = 0; i < count; i++) {
        int p = periods[i];
        printf("Verifying Period %d...\n", p);
        
        uint8_t* current_anchor = verify_period_and_get_anchor(p, trust_anchor);
        
        if (current_anchor) {
            if (trust_anchor == NULL) {
                // First proof establishes the anchor
                trust_anchor = current_anchor;
                printf("Trust Anchor established: ");
                for(int k=0; k<4; k++) printf("%02x", trust_anchor[k]);
                printf("...\n");
            } else {
                // Subsequent proofs must match the anchor (Aggregation)
                // verify_period_and_get_anchor already checked this if expected_anchor was set
                free(current_anchor);
            }
        }
    }
    
    if (trust_anchor) free(trust_anchor);
}

// Define SSZ type for BLS Pubkey (48 bytes)
static const ssz_def_t ssz_bls_pubkey = SSZ_BYTE_VECTOR("BLSPubkey", 48);
static const ssz_def_t keys_ssz_def = SSZ_VECTOR("pubkeys", ssz_bls_pubkey, 512);

static const char* trusted_keys_hex = "0x351ed1af401593d7d8c9f742bc590395bfd0b3ad76209896955e455f364a8f64";

void test_verify_1602_realistic(void) {
    printf("Running Realistic Test for Period 1602...\n");

    // 1. Trust Anchor (from 1600)
    bytes32_t current_keys_root;
    hex_to_bytes(trusted_keys_hex, -1, bytes(current_keys_root, 32));

    // 2. Load Proof 1602
    bytes_t proof = read_testdata("zk_data/proof_1602_raw.bin");

    // 3. Load New Keys 1602
    uint64_t next_period = 1602;
    bytes_t new_keys_data = read_testdata("zk_data/1602_keys.bin");

    // Check size: 512 keys * 48 bytes = 24576 bytes
    TEST_ASSERT_EQUAL_INT(512 * 48, new_keys_data.len);

    // 4. Calculate Next Keys Root
    bytes32_t next_keys_root;
    ssz_hash_tree_root(ssz_ob(keys_ssz_def, new_keys_data), next_keys_root);
    
    // 5. Construct Public Values
    // [current_keys_root (32)] [next_keys_root (32)] [next_period (8)]
    uint8_t public_values_data[72];
    memcpy(public_values_data, current_keys_root, 32); // trustanchor
    memcpy(public_values_data + 32, next_keys_root, 32); // new keys root
    uint64_to_le(public_values_data + 64, next_period); // new period
    
    // 6. Verify
    bool valid = verify_zk_proof(proof, bytes(public_values_data, 72));
    TEST_ASSERT_TRUE_MESSAGE(valid, "Realistic 1602 verification failed");

    // Cleanup
    free(proof.data);
    free(new_keys_data.data);
}

// Keep the tampering test for robustness (using 1601 as target if available, else 1600)
void test_verify_tampered(void) {
    int period = 1601;
    char proof_path[64];
    snprintf(proof_path, sizeof(proof_path), "zk_data/proof_%d_raw.bin", period);
    
    // Fallback to 1600 if 1601 not yet built
    bytes_t check = read_testdata(proof_path);
    if (check.data == NULL) {
        period = 1600;
        snprintf(proof_path, sizeof(proof_path), "zk_data/proof_%d_raw.bin", period);
    } else {
        free(check.data);
    }

    char pub_path[64];
    snprintf(pub_path, sizeof(pub_path), "zk_data/public_values_%d.bin", period);

    bytes_t proof = read_testdata(proof_path);
    bytes_t pub_orig = read_testdata(pub_path);
    
    if (proof.data == NULL || pub_orig.data == NULL) {
        TEST_IGNORE_MESSAGE("Skipping tampering test: No proof files found");
        return;
    }
    
    printf("Running Tampering Tests on Period %d\n", period);

    // 0. Baseline Check (MUST PASS)
    TEST_ASSERT_TRUE_MESSAGE(verify_zk_proof(proof, pub_orig), "Baseline verification failed! Cannot run tampering tests.");

    // 1. Tamper Current Keys Root
    {
        bytes_t pub = copy_bytes(pub_orig);
        pub.data[0] ^= 0xFF;
        TEST_ASSERT_FALSE(verify_zk_proof(proof, pub));
        free(pub.data);
    }
    // 2. Tamper Next Keys Root
    {
        bytes_t pub = copy_bytes(pub_orig);
        pub.data[32] ^= 0xFF;
        TEST_ASSERT_FALSE(verify_zk_proof(proof, pub));
        free(pub.data);
    }
    // 3. Tamper Period
    {
        bytes_t pub = copy_bytes(pub_orig);
        pub.data[64] ^= 0xFF;
        TEST_ASSERT_FALSE(verify_zk_proof(proof, pub));
        free(pub.data);
    }
    // 4. Tamper Proof
    {
        bytes_t proof_mod = copy_bytes(proof);
        if (proof_mod.len > 64) proof_mod.data[64] ^= 0xFF;
        else proof_mod.data[0] ^= 0xFF;
        TEST_ASSERT_FALSE(verify_zk_proof(proof_mod, pub_orig));
        free(proof_mod.data);
    }

    free(proof.data);
    free(pub_orig.data);
}

#endif

#ifndef ETH_ZKPROOF
void test_skipped(void) {
    TEST_IGNORE_MESSAGE("ETH_ZKPROOF is disabled");
}
#endif

int main(void) {
    UNITY_BEGIN();
    
#ifdef ETH_ZKPROOF
    RUN_TEST(test_verify_chain);
    RUN_TEST(test_verify_tampered);
    RUN_TEST(test_verify_1602_realistic);
#else
    RUN_TEST(test_skipped);
#endif

    return UNITY_END();
}
