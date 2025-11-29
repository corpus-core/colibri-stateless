/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */

#include "../../src/chains/eth/bn254/bn254.h"
#include "../../src/util/bytes.h"
#include "unity.h"
#include <string.h>
#include <stdlib.h>

static bytes_t hex_to_bytes_alloc(const char* hex) {
    size_t len = strlen(hex);
    if (len % 2 != 0) return (bytes_t){0, NULL};
    bytes_t b;
    b.len = len / 2;
    b.data = malloc(b.len);
    hex_to_bytes(hex, len, b);
    return b;
}

void setUp(void) {}
void tearDown(void) {}

void test_bn254_init(void) {
    bn254_init();
    // No assertion, just ensuring it doesn't crash
}

void test_bn254_g1_add_dbl(void) {
    bn254_g1_t p1, p2, p3, p_dbl;
    // G1 Generator P1 = (1, 2)
    uint8_t buf[64] = {0};
    buf[31] = 1; // x = 1
    buf[63] = 2; // y = 2
    
    TEST_ASSERT_TRUE(bn254_g1_from_bytes_be(&p1, buf));
    p2 = p1;
    
    // p3 = p1 + p2 (should be doubling)
    bn254_g1_add(&p3, &p1, &p2);
    
    // p_dbl = 2*p1 (scalar mul)
    uint256_t scalar;
    memset(&scalar, 0, 32);
    scalar.bytes[31] = 2;
    bn254_g1_mul(&p_dbl, &p1, &scalar);
    
    // Compare p3 and p_dbl
    // Convert to bytes to compare affine coords
    uint8_t b3[64], bd[64];
    bn254_g1_to_bytes(&p3, b3);
    bn254_g1_to_bytes(&p_dbl, bd);
    
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bd, b3, 64);
}

void test_bn254_pairing_check(void) {
    bn254_init();

    // P = (1, 2)
    bn254_g1_t P, P2;
    uint8_t buf[64] = {0};
    buf[31] = 1; buf[63] = 2;
    TEST_ASSERT_TRUE(bn254_g1_from_bytes_be(&P, buf));
    
    // Q = Generator G2
    const char* Q_hex = 
        "198e9393920d483a7260bfb731fb5d25f1aa493335a9e71297e485b7aef312c2"
        "1800deef121f1e76426a00665e5c4479674322d4f75edadd46debd5cd992f6ed"
        "090689d0585ff075ec9e99ad690c3395bc4b313370b38ef355acdadcd122975b"
        "12c85ea5db8c6deb4aab71808dcb408fe3d1e7690c43d37b4ce6cc0166fa7daa";
    
    bytes_t Q_bytes = hex_to_bytes_alloc(Q_hex);
    bn254_g2_t Q;
    TEST_ASSERT_TRUE(bn254_g2_from_bytes_eth(&Q, Q_bytes.data));
    
    // P2 = 2*P
    bn254_g1_add(&P2, &P, &P);
    
    // negP2 = -P2 (negate Y)
    // Modulus P
    uint256_t mod;
    uint8_t mod_bytes[32] = {
        0x30, 0x64, 0x4e, 0x72, 0xe1, 0x31, 0xa0, 0x29, 0xb8, 0x50, 0x45, 0xb6, 0x81, 0x81, 0x58, 0x5d,
        0x97, 0x81, 0x6a, 0x91, 0x68, 0x71, 0xca, 0x8d, 0x3c, 0x20, 0x8c, 0x16, 0xd8, 0x7c, 0xfd, 0x47
    };
    bytes_t mod_b = {.data = mod_bytes, .len = 32};
    intx_from_bytes(&mod, mod_b);
    
    bn254_g1_t negP2 = P2;
    intx_sub(&negP2.y, &mod, &P2.y);
    
    // Pairing Check: e(P2, Q) * e(negP2, Q) == 1
    bn254_g1_t Ps[2] = {P2, negP2};
    bn254_g2_t Qs[2] = {Q, Q};
    
    TEST_ASSERT_TRUE(bn254_pairing_batch_check(Ps, Qs, 2));
    
    free(Q_bytes.data);
}


int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_bn254_init);
    RUN_TEST(test_bn254_g1_add_dbl);
    RUN_TEST(test_bn254_pairing_check);
    return UNITY_END();
}


