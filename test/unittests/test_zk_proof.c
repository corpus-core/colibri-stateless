/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */

#include "c4_assert.h" // Contains read_testdata and unity includes
#include "bytes.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

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

// Test verifying a valid recursive proof (1601 -> 1600)
void test_verify_zk_proof_valid(void) {
    bytes_t proof = read_testdata("zk_data/proof_1601_raw.bin");
    bytes_t pub = read_testdata("zk_data/public_values_1601.bin");
    
    if (proof.data == NULL) {
        TEST_IGNORE_MESSAGE("Skipping test: proof_1601_raw.bin not found in test/data/zk_data");
        return;
    }
    if (pub.data == NULL) {
        free(proof.data);
        TEST_IGNORE_MESSAGE("Skipping test: public_values_1601.bin not found in test/data/zk_data");
        return;
    }
    
    bool valid = verify_zk_proof(proof, pub);
    TEST_ASSERT_TRUE_MESSAGE(valid, "ZK Proof verification failed for valid recursive proof!");
    
    free(proof.data);
    free(pub.data);
}

// Test specific modifications to public inputs
void test_verify_zk_proof_tampered_inputs(void) {
    bytes_t proof = read_testdata("zk_data/proof_1601_raw.bin");
    bytes_t pub_orig = read_testdata("zk_data/public_values_1601.bin");
    
    if (proof.data == NULL || pub_orig.data == NULL) {
        if(proof.data) free(proof.data);
        if(pub_orig.data) free(pub_orig.data);
        TEST_IGNORE_MESSAGE("Skipping test: Proof files not found");
        return;
    }
    
    // Ensure public values are large enough (should be 32+32+8 = 72 bytes)
    TEST_ASSERT_GREATER_OR_EQUAL_INT(72, pub_orig.len);

    // 1. Tamper Current Keys Root (First 32 bytes)
    {
        bytes_t pub = copy_bytes(pub_orig);
        pub.data[0] ^= 0xFF; // Flip first byte
        bool valid = verify_zk_proof(proof, pub);
        TEST_ASSERT_FALSE_MESSAGE(valid, "Should fail when Current Keys Root is modified");
        free(pub.data);
    }

    // 2. Tamper Next Keys Root (Next 32 bytes, offset 32)
    {
        bytes_t pub = copy_bytes(pub_orig);
        pub.data[32] ^= 0xFF; // Flip first byte of next keys
        bool valid = verify_zk_proof(proof, pub);
        TEST_ASSERT_FALSE_MESSAGE(valid, "Should fail when Next Keys Root is modified");
        free(pub.data);
    }

    // 3. Tamper Next Period (Last 8 bytes, offset 64)
    {
        bytes_t pub = copy_bytes(pub_orig);
        pub.data[64] ^= 0xFF; // Flip first byte of period
        bool valid = verify_zk_proof(proof, pub);
        TEST_ASSERT_FALSE_MESSAGE(valid, "Should fail when Next Period is modified");
        free(pub.data);
    }
    
    // 4. Tamper Proof Data
    {
        bytes_t proof_mod = copy_bytes(proof);
        // Flip a byte in the middle to be sure we hit actual data
        // (First bytes might be padding/flags depending on encoding)
        if (proof_mod.len > 64) {
             proof_mod.data[64] ^= 0xFF; 
        } else {
             // If proof is surprisingly short, flip everything
             for(int i=0; i<proof_mod.len; i++) proof_mod.data[i] ^= 0xFF;
        }
        
        bool valid = verify_zk_proof(proof_mod, pub_orig);
        TEST_ASSERT_FALSE_MESSAGE(valid, "Should fail when Proof is modified");
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
    RUN_TEST(test_verify_zk_proof_valid);
    RUN_TEST(test_verify_zk_proof_tampered_inputs);
#else
    RUN_TEST(test_skipped);
#endif

    return UNITY_END();
}
