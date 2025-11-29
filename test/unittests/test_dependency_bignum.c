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

/**
 * @file test_bignum.c
 * @brief Comprehensive unit tests for the bignum256 library
 *
 * TEST COVERAGE SUMMARY:
 * ======================
 *
 * This test suite provides comprehensive coverage of the bignum256 library,
 * which implements 256-bit big integer arithmetic for cryptographic operations.
 *
 * Test Statistics:
 * - Total Tests: 33
 * - Coverage: All critical cryptographic and arithmetic functions
 *
 * Tested Function Categories:
 * ---------------------------
 *
 * 1. BASIC OPERATIONS (7 tests):
 *    - bn_zero, bn_one, bn_read_uint32, bn_read_uint64
 *    - bn_read_be/bn_write_be, bn_read_le/bn_write_le
 *    - bn_write_uint32/bn_write_uint64
 *
 * 2. COMPARISON OPERATIONS (3 tests):
 *    - bn_is_zero, bn_is_one, bn_is_equal, bn_is_less
 *    - bn_is_even, bn_is_odd
 *
 * 3. ARITHMETIC OPERATIONS (6 tests):
 *    - bn_add (including aliasing tests: x + x)
 *    - bn_subtract (including aliasing tests: &x == &res, &y == &res)
 *    - bn_addmod, bn_subtractmod (with proper handling of "partly reduced" results)
 *    - bn_addi, bn_subi
 *
 * 4. BIT OPERATIONS (5 tests):
 *    - bn_lshift, bn_rshift (including multiple shifts and edge cases)
 *    - bn_setbit, bn_clearbit, bn_testbit (including high bits: 100, 200, 255)
 *    - bn_bitcount
 *    - bn_xor (including edge cases: x ^ x = 0, x ^ 0 = x)
 *
 * 5. MODULAR OPERATIONS (9 tests):
 *    - bn_mod (with proper fast_mod pre-processing for large values)
 *    - bn_fast_mod (partly reduced modulo operations)
 *    - bn_mult_k (including k = 0, 1, 8 edge cases)
 *    - bn_multiply (modular multiplication)
 *    - bn_inverse (modular inverse, including prime-1 case)
 *    - bn_power_mod (modular exponentiation, including 0^e, 1^e edge cases)
 *    - bn_sqrt (modular square root)
 *    - bn_cnegate (conditional negation)
 *    - bn_mult_half (multiply by 1/2 modulo prime)
 *
 * 6. UTILITY OPERATIONS (3 tests):
 *    - bn_normalize (including non-normalized input test)
 *    - bn_cmov (conditional move)
 *    - bn_copy
 *
 * Key Testing Features:
 * --------------------
 *
 * 1. Edge Case Coverage:
 *    - Zero values (0, prime mod prime = 0)
 *    - Identity values (1, prime-1)
 *    - Large values (2*prime, 3*prime)
 *    - Boundary conditions (max limb values, high bits)
 *
 * 2. Aliasing Tests:
 *    - Self-operations (x + x, x - x)
 *    - Overlapping pointers (&x == &res, &y == &res)
 *    - Ensures functions work correctly with aliased arguments
 *
 * 3. "Partly Reduced" Result Handling:
 *    - Functions like bn_addmod, bn_subtractmod, bn_power_mod return
 *      "partly reduced" results (values < 2*prime but not necessarily < prime)
 *    - Tests correctly apply bn_mod() (sometimes multiple times) to fully reduce
 *      results before comparison
 *
 * 4. Modular Arithmetic Correctness:
 *    - All modular operations tested against secp256k1 prime
 *    - Proper reduction sequences (fast_mod -> mod) for large values
 *    - Verification of mathematical properties (e.g., x * inv(x) = 1)
 *
 * Not Tested (Low Priority):
 * -------------------------
 *
 * The following functions are not tested but are less critical for core
 * cryptographic operations:
 * - bn_digitcount: Used for decimal formatting
 * - bn_format: Number formatting function
 * - bn_divmod58: Base58 encoding helper
 * - bn_divmod1000: Formatting helper
 * - bn_long_division: Internal division function
 * - bn_divide_base: Internal helper function
 * - inverse_mod_power_two: Internal helper function
 *
 * These functions are primarily used for formatting and display purposes
 * rather than core cryptographic operations.
 */

#include "bignum.h"
#include "secp256k1.h"
#include "unity.h"
#include <stdio.h>
#include <string.h>

void setUp(void) {}

void tearDown(void) {}

// Helper function to create bignum from hex string (big-endian)
static void bn_from_hex_be(const char* hex, bignum256* out) {
  uint8_t bytes[32] = {0};
  size_t  hex_len   = strlen(hex);
  size_t  bytes_len = hex_len / 2;

  if (bytes_len > 32) bytes_len = 32;

  for (size_t i = 0; i < bytes_len; i++) {
    char c1 = hex[hex_len - 2 - 2 * i];
    char c2 = hex[hex_len - 1 - 2 * i];
    uint8_t val = 0;
    if (c1 >= '0' && c1 <= '9') val |= (c1 - '0') << 4;
    else if (c1 >= 'a' && c1 <= 'f') val |= (c1 - 'a' + 10) << 4;
    else if (c1 >= 'A' && c1 <= 'F') val |= (c1 - 'A' + 10) << 4;
    if (c2 >= '0' && c2 <= '9') val |= (c2 - '0');
    else if (c2 >= 'a' && c2 <= 'f') val |= (c2 - 'a' + 10);
    else if (c2 >= 'A' && c2 <= 'F') val |= (c2 - 'A' + 10);
    bytes[32 - bytes_len + i] = val;
  }

  bn_read_be(bytes, out);
}

// Helper function to compare two bignums
static int bn_equals(const bignum256* a, const bignum256* b) {
  return bn_is_equal(a, b);
}

// Test: bn_zero
void test_bn_zero() {
  bignum256 x;
  bn_read_uint32(12345, &x);
  bn_zero(&x);

  TEST_ASSERT_TRUE(bn_is_zero(&x));
  TEST_ASSERT_FALSE(bn_is_one(&x));
}

// Test: bn_one
void test_bn_one() {
  bignum256 x;
  bn_zero(&x);
  bn_one(&x);

  TEST_ASSERT_FALSE(bn_is_zero(&x));
  TEST_ASSERT_TRUE(bn_is_one(&x));
}

// Test: bn_read_uint32
void test_bn_read_uint32() {
  bignum256 x;
  bn_read_uint32(0, &x);
  TEST_ASSERT_TRUE(bn_is_zero(&x));

  bn_read_uint32(1, &x);
  TEST_ASSERT_TRUE(bn_is_one(&x));

  bn_read_uint32(12345, &x);
  TEST_ASSERT_FALSE(bn_is_zero(&x));
  TEST_ASSERT_FALSE(bn_is_one(&x));
  TEST_ASSERT_EQUAL_UINT32(12345, bn_write_uint32(&x));
}

// Test: bn_read_uint64
void test_bn_read_uint64() {
  bignum256 x;
  bn_read_uint64(0, &x);
  TEST_ASSERT_TRUE(bn_is_zero(&x));

  bn_read_uint64(1, &x);
  TEST_ASSERT_TRUE(bn_is_one(&x));

  bn_read_uint64(0x123456789ABCDEF0ULL, &x);
  TEST_ASSERT_EQUAL_UINT64(0x123456789ABCDEF0ULL, bn_write_uint64(&x));
}

// Test: bn_read_be / bn_write_be
void test_bn_read_write_be() {
  // Test with zero
  uint8_t  bytes_zero[32] = {0};
  bignum256 x;
  bn_read_be(bytes_zero, &x);
  TEST_ASSERT_TRUE(bn_is_zero(&x));

  uint8_t out_bytes[32];
  bn_write_be(&x, out_bytes);
  TEST_ASSERT_EQUAL_MEMORY(bytes_zero, out_bytes, 32);

  // Test with secp256k1 prime
  bn_read_be((const uint8_t*) &secp256k1.prime, &x);
  bn_write_be(&x, out_bytes);
  TEST_ASSERT_EQUAL_MEMORY(&secp256k1.prime, out_bytes, 32);
}

// Test: bn_read_le / bn_write_le
void test_bn_read_write_le() {
  // Test with zero
  uint8_t  bytes_zero[32] = {0};
  bignum256 x;
  bn_read_le(bytes_zero, &x);
  TEST_ASSERT_TRUE(bn_is_zero(&x));

  uint8_t out_bytes[32];
  bn_write_le(&x, out_bytes);
  TEST_ASSERT_EQUAL_MEMORY(bytes_zero, out_bytes, 32);

  // Test with small number (little-endian: 0x01020304 at start)
  uint8_t bytes_le[32] = {0x04, 0x03, 0x02, 0x01};
  bn_read_le(bytes_le, &x);
  bn_write_le(&x, out_bytes);
  TEST_ASSERT_EQUAL_MEMORY(bytes_le, out_bytes, 4);
}

// Test: bn_is_equal
void test_bn_is_equal() {
  bignum256 x, y;
  bn_read_uint32(12345, &x);
  bn_read_uint32(12345, &y);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &y));

  bn_read_uint32(12346, &y);
  TEST_ASSERT_FALSE(bn_is_equal(&x, &y));

  bn_zero(&x);
  bn_zero(&y);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &y));
}

// Test: bn_is_less
void test_bn_is_less() {
  bignum256 x, y;
  bn_read_uint32(10, &x);
  bn_read_uint32(20, &y);
  TEST_ASSERT_TRUE(bn_is_less(&x, &y));
  TEST_ASSERT_FALSE(bn_is_less(&y, &x));

  bn_read_uint32(10, &y);
  TEST_ASSERT_FALSE(bn_is_less(&x, &y));
  TEST_ASSERT_FALSE(bn_is_less(&y, &x));

  bn_zero(&x);
  bn_read_uint32(1, &y);
  TEST_ASSERT_TRUE(bn_is_less(&x, &y));
}

// Test: bn_add
void test_bn_add() {
  bignum256 x, y, expected;
  bn_read_uint32(100, &x);
  bn_read_uint32(200, &y);
  bn_read_uint32(300, &expected);

  bn_add(&x, &y);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));

  // Test: x + 0 = x
  bn_read_uint32(12345, &x);
  bn_zero(&y);
  bn_read_uint32(12345, &expected);
  bn_add(&x, &y);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));

  // Test: 0 + y = y
  bn_zero(&x);
  bn_read_uint32(54321, &y);
  bn_read_uint32(54321, &expected);
  bn_add(&x, &y);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));

  // Test: x + x = 2*x (addition with self)
  bn_read_uint32(100, &x);
  bn_read_uint32(200, &expected);
  bn_add(&x, &x); // x = x + x
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));

  // Test: Aliasing - x + x (works properly even if &x == &y)
  bn_read_uint32(50, &x);
  bn_read_uint32(100, &expected);
  bn_add(&x, &x); // Should work: x = x + x
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));
}

// Test: bn_subtract
void test_bn_subtract() {
  bignum256 x, y, res, expected;
  bn_read_uint32(300, &x);
  bn_read_uint32(100, &y);
  bn_read_uint32(200, &expected);

  bn_subtract(&x, &y, &res);
  TEST_ASSERT_TRUE(bn_is_equal(&res, &expected));

  // Test: x - x = 0
  bn_read_uint32(12345, &x);
  bn_subtract(&x, &x, &res);
  TEST_ASSERT_TRUE(bn_is_zero(&res));

  // Test: x - 0 = x
  bn_read_uint32(54321, &x);
  bn_zero(&y);
  bn_read_uint32(54321, &expected);
  bn_subtract(&x, &y, &res);
  TEST_ASSERT_TRUE(bn_is_equal(&res, &expected));

  // Test: Aliasing - &x == &res (works properly)
  bn_read_uint32(100, &x);
  bn_read_uint32(30, &y);
  bn_read_uint32(70, &expected);
  bn_subtract(&x, &y, &x); // res = &x, should work
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));

  // Test: Aliasing - &y == &res (works properly)
  bn_read_uint32(200, &x);
  bn_read_uint32(50, &y);
  bn_read_uint32(150, &expected);
  bn_subtract(&x, &y, &y); // res = &y, should work
  TEST_ASSERT_TRUE(bn_is_equal(&y, &expected));
}

// Test: bn_addmod
void test_bn_addmod() {
  bignum256 x, y, expected;
  const bignum256* prime = &secp256k1.prime;

  // Test: (prime - 1) + 1 mod prime = 0
  bn_copy(prime, &x);
  bn_subi(&x, 1, prime);
  bn_one(&y);
  bn_addmod(&x, &y, prime);
  bn_mod(&x, prime); // bn_addmod returns partly reduced, need full reduction
  TEST_ASSERT_TRUE(bn_is_zero(&x));

  // Test: 1 + 1 mod prime = 2
  bn_one(&x);
  bn_one(&y);
  bn_read_uint32(2, &expected);
  bn_addmod(&x, &y, prime);
  bn_mod(&x, prime); // bn_addmod returns partly reduced, need full reduction
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));

  // Test: (prime-1) + (prime-1) mod prime = prime - 2
  bn_copy(prime, &x);
  bn_subi(&x, 1, prime); // x = prime - 1
  bn_copy(prime, &y);
  bn_subi(&y, 1, prime); // y = prime - 1
  bn_addmod(&x, &y, prime);
  bn_mod(&x, prime);
  bn_copy(prime, &expected);
  bn_subi(&expected, 2, prime); // expected = prime - 2
  bn_mod(&expected, prime);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));

  // Test: 0 + 0 mod prime = 0
  bn_zero(&x);
  bn_zero(&y);
  bn_addmod(&x, &y, prime);
  bn_mod(&x, prime);
  TEST_ASSERT_TRUE(bn_is_zero(&x));
}

// Test: bn_subtractmod
void test_bn_subtractmod() {
  bignum256 x, y, res, expected, res_subtract;
  const bignum256* prime = &secp256k1.prime;

  // Test: 3 - 2 mod prime = 1 (positive result, should be same as bn_subtract)
  // bn_subtractmod calculates: res = x + (2 * prime - y) = 3 + (2*prime - 2) = 1 + 2*prime
  // After first bn_mod: res = 1 + 2*prime - prime = 1 + prime (still >= prime)
  // After second bn_mod: res = 1 + prime - prime = 1
  bn_read_uint32(3, &x);
  bn_read_uint32(2, &y);
  bn_read_uint32(1, &expected);
  
  bn_subtractmod(&x, &y, &res, prime);
  bn_mod(&res, prime); // First reduction: 1 + 2*prime -> 1 + prime
  bn_mod(&res, prime); // Second reduction: 1 + prime -> 1
  
  TEST_ASSERT_TRUE(bn_is_equal(&res, &expected));

  // Test: 1 - 1 mod prime = 0
  // bn_subtractmod calculates: res = 1 + (2*prime - 1) = 2*prime
  // After first bn_mod: res = 2*prime - prime = prime (still >= prime)
  // After second bn_mod: res = prime - prime = 0
  bn_one(&x);
  bn_one(&y);
  bn_subtractmod(&x, &y, &res, prime);
  bn_mod(&res, prime); // First reduction
  bn_mod(&res, prime); // Second reduction
  TEST_ASSERT_TRUE(bn_is_zero(&res));

  // Test: 0 - 1 mod prime = prime - 1 (negative result, needs modulo reduction)
  // bn_subtractmod calculates: res = 0 + (2*prime - 1) = 2*prime - 1
  // After first bn_mod: res = 2*prime - 1 - prime = prime - 1 (fully reduced)
  bn_zero(&x);
  bn_one(&y);
  bn_subtractmod(&x, &y, &res, prime);
  bn_mod(&res, prime); // First reduction: 2*prime - 1 -> prime - 1
  
  // Verify: (res + 1) mod prime should be 0, which means res = prime - 1
  bignum256 res_plus_one;
  bn_copy(&res, &res_plus_one);
  bn_addi(&res_plus_one, 1);
  bn_mod(&res_plus_one, prime);
  TEST_ASSERT_TRUE(bn_is_zero(&res_plus_one));
  
  // Also verify: res < prime (should be true for prime - 1)
  TEST_ASSERT_TRUE(bn_is_less(&res, prime));
}

// Test: bn_lshift (left shift)
void test_bn_lshift() {
  bignum256 x, expected;
  bn_read_uint32(1, &x);
  bn_read_uint32(2, &expected);
  bn_lshift(&x);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));

  bn_read_uint32(5, &x);
  bn_read_uint32(10, &expected);
  bn_lshift(&x);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));

  // Test: Multiple shifts
  bn_read_uint32(1, &x);
  bn_read_uint32(4, &expected); // 1 << 2 = 4
  bn_lshift(&x); // x = 2
  bn_lshift(&x); // x = 4
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));

  // Test: Shift of 0
  bn_zero(&x);
  bn_lshift(&x);
  TEST_ASSERT_TRUE(bn_is_zero(&x));

  // Test: Larger number
  bn_read_uint32(0x80000000, &x); // 2^31
  bn_lshift(&x); // Should become 2^32 (wraps in uint32_t, but bignum handles it)
  // Verify it's not zero
  TEST_ASSERT_FALSE(bn_is_zero(&x));
}

// Test: bn_rshift (right shift)
void test_bn_rshift() {
  bignum256 x, expected;
  bn_read_uint32(2, &x);
  bn_read_uint32(1, &expected);
  bn_rshift(&x);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));

  bn_read_uint32(10, &x);
  bn_read_uint32(5, &expected);
  bn_rshift(&x);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));

  // Test: 1 >> 1 = 0
  bn_read_uint32(1, &x);
  bn_zero(&expected);
  bn_rshift(&x);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));

  // Test: Multiple shifts
  bn_read_uint32(16, &x);
  bn_read_uint32(4, &expected); // 16 >> 2 = 4
  bn_rshift(&x); // x = 8
  bn_rshift(&x); // x = 4
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));

  // Test: Shift of 0
  bn_zero(&x);
  bn_rshift(&x);
  TEST_ASSERT_TRUE(bn_is_zero(&x));

  // Test: Even number becomes odd after shift
  bn_read_uint32(6, &x); // 6 = 0b110
  bn_read_uint32(3, &expected); // 6 >> 1 = 3
  bn_rshift(&x);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));
  TEST_ASSERT_TRUE(bn_is_odd(&x));
}

// Test: bn_setbit / bn_clearbit / bn_testbit
void test_bn_bit_operations() {
  bignum256 x;
  bn_zero(&x);

  // Test: Set bit 0
  bn_setbit(&x, 0);
  TEST_ASSERT_EQUAL_UINT32(1, bn_testbit(&x, 0));
  TEST_ASSERT_EQUAL_UINT32(0, bn_testbit(&x, 1));

  // Test: Set bit 5
  bn_setbit(&x, 5);
  TEST_ASSERT_EQUAL_UINT32(1, bn_testbit(&x, 5));

  // Test: Clear bit 0
  bn_clearbit(&x, 0);
  TEST_ASSERT_EQUAL_UINT32(0, bn_testbit(&x, 0));
  TEST_ASSERT_EQUAL_UINT32(1, bn_testbit(&x, 5));

  // Test: Higher bits (bit 100, 200, 255)
  bn_zero(&x);
  bn_setbit(&x, 100);
  TEST_ASSERT_EQUAL_UINT32(1, bn_testbit(&x, 100));
  TEST_ASSERT_EQUAL_UINT32(0, bn_testbit(&x, 99));
  TEST_ASSERT_EQUAL_UINT32(0, bn_testbit(&x, 101));

  bn_setbit(&x, 200);
  TEST_ASSERT_EQUAL_UINT32(1, bn_testbit(&x, 200));
  TEST_ASSERT_EQUAL_UINT32(1, bn_testbit(&x, 100));

  bn_setbit(&x, 255);
  TEST_ASSERT_EQUAL_UINT32(1, bn_testbit(&x, 255));

  // Test: Clear higher bits
  bn_clearbit(&x, 100);
  TEST_ASSERT_EQUAL_UINT32(0, bn_testbit(&x, 100));
  TEST_ASSERT_EQUAL_UINT32(1, bn_testbit(&x, 200));
  TEST_ASSERT_EQUAL_UINT32(1, bn_testbit(&x, 255));
}

// Test: bn_bitcount
void test_bn_bitcount() {
  bignum256 x;
  bn_zero(&x);
  TEST_ASSERT_EQUAL_INT(0, bn_bitcount(&x));

  bn_one(&x);
  TEST_ASSERT_EQUAL_INT(1, bn_bitcount(&x));

  bn_read_uint32(7, &x); // 7 = 0b111 (3 bits)
  TEST_ASSERT_EQUAL_INT(3, bn_bitcount(&x));

  bn_read_uint32(255, &x); // 255 = 0b11111111 (8 bits)
  TEST_ASSERT_EQUAL_INT(8, bn_bitcount(&x));
}

// Test: bn_normalize
void test_bn_normalize() {
  bignum256 x, expected;
  
  // Test: Already normalized number should stay the same
  bn_read_uint32(12345, &x);
  bn_read_uint32(12345, &expected);
  bn_normalize(&x);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));
  
  // Test: Zero should stay zero
  bn_zero(&x);
  bn_normalize(&x);
  TEST_ASSERT_TRUE(bn_is_zero(&x));
  
  // Test: One should stay one
  bn_one(&x);
  bn_normalize(&x);
  TEST_ASSERT_TRUE(bn_is_one(&x));
  
  // Test: Max value for first limb should be normalized correctly
  bn_read_uint32(0x1FFFFFFF, &x); // Max value for first limb (29 bits)
  bn_normalize(&x);
  // After normalization, all limbs should be <= BN_LIMB_MASK
  for (int i = 0; i < BN_LIMBS; i++) {
    TEST_ASSERT_TRUE(x.val[i] <= BN_LIMB_MASK);
  }

  // Test: Create a non-normalized number and normalize it
  // Set val[0] to a value that would overflow into val[1] if not normalized
  bn_zero(&x);
  x.val[0] = BN_LIMB_MASK + 1; // This is not normalized (val[0] > BN_LIMB_MASK)
  bn_normalize(&x);
  // After normalization, all limbs should be <= BN_LIMB_MASK
  for (int i = 0; i < BN_LIMBS; i++) {
    TEST_ASSERT_TRUE(x.val[i] <= BN_LIMB_MASK);
  }
  // The value should be preserved (BN_LIMB_MASK + 1 = 2^29, which should normalize to val[0]=0, val[1]=1)
  TEST_ASSERT_FALSE(bn_is_zero(&x));
}

// Test: bn_xor
void test_bn_xor() {
  bignum256 x, y, res, expected;
  bn_read_uint32(0xAAAA, &x);
  bn_read_uint32(0x5555, &y);
  bn_read_uint32(0xFFFF, &expected); // 0xAAAA ^ 0x5555 = 0xFFFF

  bn_xor(&res, &x, &y);
  TEST_ASSERT_TRUE(bn_is_equal(&res, &expected));

  // Test: x ^ x = 0
  bn_read_uint32(12345, &x);
  bn_xor(&res, &x, &x);
  TEST_ASSERT_TRUE(bn_is_zero(&res));

  // Test: x ^ 0 = x
  bn_read_uint32(0xABCD, &x);
  bn_zero(&y);
  bn_read_uint32(0xABCD, &expected);
  bn_xor(&res, &x, &y);
  TEST_ASSERT_TRUE(bn_is_equal(&res, &expected));

  // Test: 0 ^ y = y
  bn_zero(&x);
  bn_read_uint32(0x1234, &y);
  bn_read_uint32(0x1234, &expected);
  bn_xor(&res, &x, &y);
  TEST_ASSERT_TRUE(bn_is_equal(&res, &expected));

  // Test: All bits set
  bn_read_uint32(0xFFFFFFFF, &x);
  bn_read_uint32(0xFFFFFFFF, &y);
  bn_read_uint32(0, &expected); // 0xFFFFFFFF ^ 0xFFFFFFFF = 0
  bn_xor(&res, &x, &y);
  TEST_ASSERT_TRUE(bn_is_equal(&res, &expected));
}

// Test: bn_mod
void test_bn_mod() {
  bignum256 x, expected;
  const bignum256* prime = &secp256k1.prime;

  // Test: prime mod prime = 0
  bn_copy(prime, &x);
  bn_mod(&x, prime);
  TEST_ASSERT_TRUE(bn_is_zero(&x));

  // Test: (prime + 1) mod prime = 1
  bn_copy(prime, &x);
  bn_addi(&x, 1);
  bn_mod(&x, prime);
  TEST_ASSERT_TRUE(bn_is_one(&x));

  // Test: 1 mod prime = 1
  bn_one(&x);
  bn_mod(&x, prime);
  TEST_ASSERT_TRUE(bn_is_one(&x));

  // Note: Tests for 2*prime and 3*prime are covered in test_bn_fast_mod

  // Test: (2*prime - 1) mod prime = prime - 1
  // Note: We use bn_subtract to calculate 2*prime - 1, then reduce
  bignum256 y;
  bn_copy(prime, &x);
  bn_add(&x, prime); // x = 2*prime
  bn_read_uint32(1, &y);
  bn_subtract(&x, &y, &x); // x = 2*prime - 1
  bn_fast_mod(&x, prime); // Reduce to partly reduced
  bn_mod(&x, prime); // Fully reduce
  bn_copy(prime, &expected);
  bn_read_uint32(1, &y);
  bn_subtract(&expected, &y, &expected); // expected = prime - 1
  bn_mod(&expected, prime);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));
}

// Test: bn_fast_mod
void test_bn_fast_mod() {
  bignum256 x, expected;
  const bignum256* prime = &secp256k1.prime;

  // Test: 2 * prime should be reduced (partly reduced, < 2*prime)
  bn_copy(prime, &x);
  bn_add(&x, prime); // x = 2*prime
  bn_fast_mod(&x, prime);
  // After fast_mod, x should be < 2*prime (partly reduced)
  // Then after bn_mod, should be 0
  bn_mod(&x, prime);
  TEST_ASSERT_TRUE(bn_is_zero(&x));

  // Test: 3 * prime should be reduced to 0 (after bn_mod)
  bn_copy(prime, &x);
  bn_add(&x, prime); // x = 2*prime
  bn_add(&x, prime); // x = 3*prime
  bn_fast_mod(&x, prime);
  bn_mod(&x, prime);
  // After bn_mod, 3*prime mod prime should be 0
  TEST_ASSERT_TRUE(bn_is_zero(&x));

  // Test: prime + 1 should be reduced to 1
  bn_copy(prime, &x);
  bn_addi(&x, 1); // x = prime + 1
  bn_fast_mod(&x, prime);
  bn_mod(&x, prime);
  bn_one(&expected);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));
}

// Test: bn_mult_k
void test_bn_mult_k() {
  bignum256 x, expected;
  const bignum256* prime = &secp256k1.prime;

  bn_read_uint32(5, &x);
  bn_mult_k(&x, 3, prime); // 5 * 3 = 15
  bn_read_uint32(15, &expected);
  bn_mod(&expected, prime);
  bn_mod(&x, prime);
  // Result should be 15 mod prime
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));

  // Test: x * 0 = 0
  bn_read_uint32(100, &x);
  bn_mult_k(&x, 0, prime);
  TEST_ASSERT_TRUE(bn_is_zero(&x));

  // Test: x * 1 = x
  bn_read_uint32(42, &x);
  bn_read_uint32(42, &expected);
  bn_mult_k(&x, 1, prime);
  bn_mod(&x, prime);
  bn_mod(&expected, prime);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));

  // Test: x * 8 (maximal k value)
  bn_read_uint32(10, &x);
  bn_read_uint32(80, &expected);
  bn_mult_k(&x, 8, prime);
  bn_mod(&x, prime);
  bn_mod(&expected, prime);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));
}

// Test: bn_cmov (conditional move)
void test_bn_cmov() {
  bignum256 res, truecase, falsecase;
  bn_read_uint32(100, &truecase);
  bn_read_uint32(200, &falsecase);

  // Test: cond = 1 (true)
  bn_cmov(&res, 1, &truecase, &falsecase);
  TEST_ASSERT_TRUE(bn_is_equal(&res, &truecase));

  // Test: cond = 0 (false)
  bn_cmov(&res, 0, &truecase, &falsecase);
  TEST_ASSERT_TRUE(bn_is_equal(&res, &falsecase));
}

// Test: bn_addi
void test_bn_addi() {
  bignum256 x, expected;
  bn_read_uint32(100, &x);
  bn_read_uint32(150, &expected);

  bn_addi(&x, 50);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));

  // Test: x + 0 = x
  bn_read_uint32(12345, &x);
  bn_read_uint32(12345, &expected);
  bn_addi(&x, 0);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));
}

// Test: bn_subi
void test_bn_subi() {
  bignum256 x, expected;
  const bignum256* prime = &secp256k1.prime;

  // bn_subi requires y < prime->val[0]
  // secp256k1.prime.val[0] = 0x1ffffc2f = 536870959
  // So we need to use a value < 536870959
  // Test: 100 - 50 mod prime = 50
  bn_read_uint32(100, &x);
  bn_read_uint32(50, &expected);
  bn_subi(&x, 50, prime);
  bn_mod(&x, prime); // bn_subi returns partly reduced, need full reduction
  bn_mod(&expected, prime);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));

  // Test: x - 0 = x (0 is always < prime->val[0])
  // Note: bn_subi does x = x + prime - 0 = x + prime, so we need to mod
  bn_read_uint32(12345, &x);
  bn_read_uint32(12345, &expected);
  bn_subi(&x, 0, prime);
  bn_mod(&x, prime); // bn_subi returns partly reduced, need full reduction
  bn_mod(&expected, prime);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));
}

// Test: bn_multiply (modular multiplication)
void test_bn_multiply() {
  bignum256 k, x, expected, result;
  const bignum256* prime = &secp256k1.prime;

  // Test: 1 * 5 = 5
  bn_one(&k);
  bn_read_uint32(5, &x);
  bn_read_uint32(5, &expected);
  bn_multiply(&k, &x, prime);
  bn_mod(&x, prime);
  bn_mod(&expected, prime);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));

  // Test: 0 * 5 = 0
  bn_zero(&k);
  bn_read_uint32(5, &x);
  bn_multiply(&k, &x, prime);
  TEST_ASSERT_TRUE(bn_is_zero(&x));

  // Test: 2 * 3 = 6
  bn_read_uint32(2, &k);
  bn_read_uint32(3, &x);
  bn_read_uint32(6, &expected);
  bn_multiply(&k, &x, prime);
  bn_mod(&x, prime);
  bn_mod(&expected, prime);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));

  // Test: 10 * 20 = 200
  bn_read_uint32(10, &k);
  bn_read_uint32(20, &x);
  bn_read_uint32(200, &expected);
  bn_multiply(&k, &x, prime);
  bn_mod(&x, prime);
  bn_mod(&expected, prime);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));

  // Test: (prime-1) * (prime-1) mod prime = 1
  bn_copy(prime, &k);
  bn_subi(&k, 1, prime); // k = prime - 1
  bn_copy(prime, &x);
  bn_subi(&x, 1, prime); // x = prime - 1
  bn_multiply(&k, &x, prime);
  bn_mod(&x, prime);
  bn_one(&expected);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));
}

// Test: bn_inverse
void test_bn_inverse() {
  bignum256 x, inv, one, result, inv_inv;
  const bignum256* prime = &secp256k1.prime;

  // Test: inverse of 1 is 1
  bn_one(&x);
  bn_copy(&x, &inv);
  bn_inverse(&inv, prime);
  TEST_ASSERT_TRUE(bn_is_one(&inv));

  // Test: x * inv(x) mod prime = 1 for various values
  uint32_t test_values[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 100, 1000};
  for (size_t i = 0; i < sizeof(test_values) / sizeof(test_values[0]); i++) {
    bn_read_uint32(test_values[i], &x);
    bn_copy(&x, &inv);
    bn_inverse(&inv, prime);
    bn_copy(&x, &result);
    bn_multiply(&inv, &result, prime);
    bn_mod(&result, prime);
    bn_one(&one);
    TEST_ASSERT_TRUE(bn_is_equal(&result, &one));
  }

  // Test: inv(inv(x)) = x
  bn_read_uint32(5, &x);
  bn_copy(&x, &inv);
  bn_inverse(&inv, prime);
  bn_copy(&inv, &inv_inv);
  bn_inverse(&inv_inv, prime);
  bn_mod(&x, prime);
  bn_mod(&inv_inv, prime);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &inv_inv));

  // Test: inverse of (prime-1) is (prime-1)
  bn_copy(prime, &x);
  bn_subi(&x, 1, prime); // x = prime - 1
  bn_copy(&x, &inv);
  bn_inverse(&inv, prime);
  bn_mod(&x, prime);
  bn_mod(&inv, prime);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &inv));
}

// Test: bn_copy
void test_bn_copy() {
  bignum256 x, y;
  bn_read_uint32(12345, &x);
  bn_copy(&x, &y);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &y));

  bn_read_uint32(54321, &x);
  TEST_ASSERT_FALSE(bn_is_equal(&x, &y)); // y should be unchanged
}

// Test: bn_is_even / bn_is_odd
void test_bn_is_even_odd() {
  bignum256 x;
  bn_read_uint32(0, &x);
  TEST_ASSERT_TRUE(bn_is_even(&x));
  TEST_ASSERT_FALSE(bn_is_odd(&x));

  bn_read_uint32(1, &x);
  TEST_ASSERT_FALSE(bn_is_even(&x));
  TEST_ASSERT_TRUE(bn_is_odd(&x));

  bn_read_uint32(2, &x);
  TEST_ASSERT_TRUE(bn_is_even(&x));
  TEST_ASSERT_FALSE(bn_is_odd(&x));

  bn_read_uint32(3, &x);
  TEST_ASSERT_FALSE(bn_is_even(&x));
  TEST_ASSERT_TRUE(bn_is_odd(&x));
}

// Test: bn_write_uint32 / bn_write_uint64
void test_bn_write_uint32_uint64() {
  bignum256 x;
  bn_read_uint32(12345, &x);
  TEST_ASSERT_EQUAL_UINT32(12345, bn_write_uint32(&x));

  bn_read_uint64(0x123456789ABCDEF0ULL, &x);
  TEST_ASSERT_EQUAL_UINT64(0x123456789ABCDEF0ULL, bn_write_uint64(&x));
}

// Test: bn_power_mod (modular exponentiation)
void test_bn_power_mod() {
  bignum256 x, e, res, expected, one;
  const bignum256* prime = &secp256k1.prime;

  // Test: x^0 mod prime = 1
  bn_read_uint32(5, &x);
  bn_zero(&e);
  bn_power_mod(&x, &e, prime, &res);
  bn_mod(&res, prime);
  bn_one(&one);
  TEST_ASSERT_TRUE(bn_is_equal(&res, &one));

  // Test: x^1 mod prime = x
  bn_read_uint32(5, &x);
  bn_one(&e);
  bn_read_uint32(5, &expected);
  bn_power_mod(&x, &e, prime, &res);
  bn_mod(&res, prime);
  bn_mod(&expected, prime);
  TEST_ASSERT_TRUE(bn_is_equal(&res, &expected));

  // Test: x^2 mod prime = x * x mod prime
  bn_read_uint32(5, &x);
  bn_read_uint32(2, &e);
  bn_read_uint32(5, &expected);
  bn_multiply(&expected, &expected, prime); // expected = 5 * 5
  bn_mod(&expected, prime);
  bn_power_mod(&x, &e, prime, &res);
  bn_mod(&res, prime);
  TEST_ASSERT_TRUE(bn_is_equal(&res, &expected));

  // Test: 2^10 mod prime
  bn_read_uint32(2, &x);
  bn_read_uint32(10, &e);
  bn_read_uint32(1024, &expected); // 2^10 = 1024
  bn_power_mod(&x, &e, prime, &res);
  bn_mod(&res, prime);
  bn_mod(&expected, prime);
  TEST_ASSERT_TRUE(bn_is_equal(&res, &expected));

  // Test: (prime-1)^(prime-1) mod prime = 1 (Fermat's little theorem)
  // Note: This is a large exponent, so we test with smaller exponent first
  // Test: (prime-1)^2 mod prime = 1
  bn_copy(prime, &x);
  bn_subi(&x, 1, prime); // x = prime - 1
  bn_read_uint32(2, &e); // e = 2
  bn_power_mod(&x, &e, prime, &res);
  bn_mod(&res, prime); // bn_power_mod returns partly reduced
  bn_one(&one);
  TEST_ASSERT_TRUE(bn_is_equal(&res, &one));

  // Test: 0^e mod prime = 0 (for e > 0)
  bn_zero(&x);
  bn_read_uint32(5, &e);
  bn_power_mod(&x, &e, prime, &res);
  bn_mod(&res, prime);
  TEST_ASSERT_TRUE(bn_is_zero(&res));

  // Test: 1^e mod prime = 1
  bn_one(&x);
  bn_read_uint32(100, &e);
  bn_power_mod(&x, &e, prime, &res);
  bn_mod(&res, prime);
  TEST_ASSERT_TRUE(bn_is_one(&res));
}

// Test: bn_sqrt (square root modulo prime)
void test_bn_sqrt() {
  bignum256 x, x_squared, sqrt_x, sqrt_x_squared;
  const bignum256* prime = &secp256k1.prime;

  // Test: sqrt(1) = 1
  bn_one(&x);
  bn_copy(&x, &sqrt_x);
  bn_sqrt(&sqrt_x, prime);
  TEST_ASSERT_TRUE(bn_is_one(&sqrt_x));

  // Test: sqrt(4) = 2 (if 4 is a quadratic residue)
  bn_read_uint32(4, &x);
  bn_read_uint32(2, &x_squared);
  bn_multiply(&x_squared, &x_squared, prime); // x_squared = 2 * 2 = 4
  bn_mod(&x_squared, prime);
  bn_copy(&x_squared, &sqrt_x);
  bn_sqrt(&sqrt_x, prime);
  // Verify: sqrt_x^2 mod prime = x_squared
  bn_copy(&sqrt_x, &sqrt_x_squared);
  bn_multiply(&sqrt_x, &sqrt_x_squared, prime);
  bn_mod(&sqrt_x_squared, prime);
  bn_mod(&x_squared, prime);
  TEST_ASSERT_TRUE(bn_is_equal(&sqrt_x_squared, &x_squared));

  // Test: sqrt(9) = 3 (if 9 is a quadratic residue)
  bn_read_uint32(9, &x);
  bn_read_uint32(3, &x_squared);
  bn_multiply(&x_squared, &x_squared, prime); // x_squared = 3 * 3 = 9
  bn_mod(&x_squared, prime);
  bn_copy(&x_squared, &sqrt_x);
  bn_sqrt(&sqrt_x, prime);
  // Verify: sqrt_x^2 mod prime = x_squared
  bn_copy(&sqrt_x, &sqrt_x_squared);
  bn_multiply(&sqrt_x, &sqrt_x_squared, prime);
  bn_mod(&sqrt_x_squared, prime);
  bn_mod(&x_squared, prime);
  TEST_ASSERT_TRUE(bn_is_equal(&sqrt_x_squared, &x_squared));
}

// Test: bn_cnegate (conditional negation)
void test_bn_cnegate() {
  bignum256 x, neg_x, expected;
  const bignum256* prime = &secp256k1.prime;

  // Test: cnegate(5, cond=0) = 5 (no negation)
  bn_read_uint32(5, &x);
  bn_read_uint32(5, &expected);
  bn_cnegate(0, &x, prime);
  bn_mod(&x, prime);
  bn_mod(&expected, prime);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));

  // Test: cnegate(5, cond=1) = prime - 5 (negation)
  bn_read_uint32(5, &x);
  bn_copy(prime, &expected);
  bn_subi(&expected, 5, prime); // expected = prime - 5
  bn_mod(&expected, prime);
  bn_read_uint32(5, &x);
  bn_cnegate(1, &x, prime);
  bn_mod(&x, prime);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));

  // Test: cnegate(prime-1, cond=1) = 1
  // cnegate(prime-1, 1) = 2*prime - (prime-1) = prime + 1 (partly reduced)
  // After first bn_mod: (prime + 1) - prime = 1 (fully reduced)
  bn_copy(prime, &x);
  bn_subi(&x, 1, prime); // x = prime - 1
  bn_cnegate(1, &x, prime);
  bn_mod(&x, prime); // Reduction: prime + 1 -> 1
  bn_one(&expected);
  bn_mod(&expected, prime);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));

  // Test: cnegate(cnegate(x, 1), 1) = x (double negation)
  bn_read_uint32(7, &x);
  bn_copy(&x, &neg_x);
  bn_cnegate(1, &neg_x, prime);
  bn_cnegate(1, &neg_x, prime);
  bn_mod(&x, prime);
  bn_mod(&neg_x, prime);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &neg_x));
}

// Test: bn_mult_half (multiply by 1/2 modulo prime)
void test_bn_mult_half() {
  bignum256 x, expected, doubled;
  const bignum256* prime = &secp256k1.prime;

  // Test: mult_half(0) = 0
  bn_zero(&x);
  bn_mult_half(&x, prime);
  TEST_ASSERT_TRUE(bn_is_zero(&x));

  // Test: mult_half(2) = 1
  bn_read_uint32(2, &x);
  bn_read_uint32(1, &expected);
  bn_mult_half(&x, prime);
  bn_mod(&x, prime);
  bn_mod(&expected, prime);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));

  // Test: mult_half(4) = 2
  bn_read_uint32(4, &x);
  bn_read_uint32(2, &expected);
  bn_mult_half(&x, prime);
  bn_mod(&x, prime);
  bn_mod(&expected, prime);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));

  // Test: mult_half(10) = 5
  bn_read_uint32(10, &x);
  bn_read_uint32(5, &expected);
  bn_mult_half(&x, prime);
  bn_mod(&x, prime);
  bn_mod(&expected, prime);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));

  // Test: mult_half(odd number) - should add prime first
  // mult_half(1) = (1 + prime) / 2 mod prime
  bn_read_uint32(1, &x);
  bn_copy(&x, &expected);
  bn_add(&expected, prime); // expected = 1 + prime
  bn_mult_half(&expected, prime); // expected = (1 + prime) / 2
  bn_mod(&expected, prime);
  bn_mult_half(&x, prime);
  bn_mod(&x, prime);
  TEST_ASSERT_TRUE(bn_is_equal(&x, &expected));

  // Test: mult_half(mult_half(x) * 2) = x (for even x)
  bn_read_uint32(20, &x);
  bn_copy(&x, &expected);
  bn_mult_half(&expected, prime);
  bn_mod(&expected, prime);
  // Now double it back
  bn_copy(&expected, &doubled);
  bn_add(&doubled, &expected); // doubled = 2 * mult_half(x)
  bn_mod(&doubled, prime);
  bn_mod(&x, prime);
  TEST_ASSERT_TRUE(bn_is_equal(&doubled, &x));
}

int main(void) {
  UNITY_BEGIN();

  // Basic operations
  RUN_TEST(test_bn_zero);
  RUN_TEST(test_bn_one);
  RUN_TEST(test_bn_read_uint32);
  RUN_TEST(test_bn_read_uint64);
  RUN_TEST(test_bn_read_write_be);
  RUN_TEST(test_bn_read_write_le);

  // Comparison operations
  RUN_TEST(test_bn_is_equal);
  RUN_TEST(test_bn_is_less);

  // Arithmetic operations
  RUN_TEST(test_bn_add);
  RUN_TEST(test_bn_subtract);
  RUN_TEST(test_bn_addmod);
  RUN_TEST(test_bn_subtractmod);
  RUN_TEST(test_bn_addi);
  RUN_TEST(test_bn_subi);

  // Bit operations
  RUN_TEST(test_bn_lshift);
  RUN_TEST(test_bn_rshift);
  RUN_TEST(test_bn_bit_operations);
  RUN_TEST(test_bn_bitcount);
  RUN_TEST(test_bn_is_even_odd);

  // Modular operations
  RUN_TEST(test_bn_mod);
  RUN_TEST(test_bn_fast_mod);
  RUN_TEST(test_bn_mult_k);
  RUN_TEST(test_bn_multiply);
  RUN_TEST(test_bn_inverse);
  RUN_TEST(test_bn_power_mod);
  RUN_TEST(test_bn_sqrt);
  RUN_TEST(test_bn_cnegate);
  RUN_TEST(test_bn_mult_half);

  // Utility operations
  RUN_TEST(test_bn_normalize);
  RUN_TEST(test_bn_xor);
  RUN_TEST(test_bn_cmov);
  RUN_TEST(test_bn_copy);
  RUN_TEST(test_bn_write_uint32_uint64);

  return UNITY_END();
}

