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

// Test verifying a valid proof (generated locally)
void test_verify_zk_proof_valid(void) {
    // Updated to 1600 proof
    bytes_t proof = read_testdata("zk_data/proof_1600_raw.bin");
    bytes_t pub = read_testdata("zk_data/public_values_1600.bin");
    
    if (proof.data == NULL) {
        TEST_IGNORE_MESSAGE("Skipping test: proof_1600_raw.bin not found in test/data/zk_data");
        return;
    }
    if (pub.data == NULL) {
        free(proof.data);
        TEST_IGNORE_MESSAGE("Skipping test: public_values_1600.bin not found in test/data/zk_data");
        return;
    }
    
    bool valid = verify_zk_proof(proof, pub);
    TEST_ASSERT_TRUE_MESSAGE(valid, "ZK Proof verification failed!");
    
    free(proof.data);
    free(pub.data);
}

// Test verifying with invalid input
void test_verify_zk_proof_invalid(void) {
    bytes_t proof = read_testdata("zk_data/proof_1600_raw.bin");
    bytes_t pub = read_testdata("zk_data/public_values_1600.bin");
    
    if (proof.data == NULL || pub.data == NULL) {
        if(proof.data) free(proof.data);
        if(pub.data) free(pub.data);
        TEST_IGNORE_MESSAGE("Skipping test: Proof files not found");
        return;
    }
    
    // Mutate public input: Flip ALL bits to be sure
    for(int i=0; i<pub.len; i++) {
        pub.data[i] ^= 0xFF;
    }
    
    bool valid = verify_zk_proof(proof, pub);
    TEST_ASSERT_FALSE_MESSAGE(valid, "Verification should fail with modified public input");
    
    free(proof.data);
    free(pub.data);
}

#endif

int main(void) {
    UNITY_BEGIN();
    
#ifdef ETH_ZKPROOF
    RUN_TEST(test_verify_zk_proof_valid);
    RUN_TEST(test_verify_zk_proof_invalid);
#else
    TEST_IGNORE_MESSAGE("ETH_ZKPROOF is disabled");
#endif

    return UNITY_END();
}
