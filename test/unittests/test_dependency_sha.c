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
 * @file test_sha.c
 * @brief Comprehensive unit tests for SHA-2 and SHA-3 hash functions
 *
 * This test suite provides comprehensive coverage of SHA-2 (SHA-256, SHA-512)
 * and SHA-3 (SHA3-256, SHA3-512, Keccak-256) hash functions.
 *
 * Test Statistics:
 * - Total Tests: 20 (15 without USE_KECCAK)
 * - Coverage: SHA-256, SHA-512, SHA3-256, SHA3-512, Keccak-256
 *
 * Tested Function Categories:
 * ---------------------------
 *
 * 1. SHA-256 (SHA-2):
 *    - sha256_Init, sha256_Update, sha256_Final (incremental)
 *    - sha256_Raw (one-shot)
 *    - Known test vectors (NIST, empty string, various lengths)
 *
 * 2. SHA-512 (SHA-2):
 *    - sha512_Init, sha512_Update, sha512_Final (incremental)
 *    - sha512_Raw (one-shot)
 *    - Known test vectors
 *
 * 3. SHA3-256:
 *    - sha3_256_Init, sha3_Update, sha3_Final (incremental)
 *    - sha3_256 (one-shot)
 *    - Known test vectors
 *
 * 4. SHA3-512:
 *    - sha3_512_Init, sha3_Update, sha3_Final (incremental)
 *    - sha3_512 (one-shot)
 *    - Known test vectors
 *
 * 5. Keccak-256 (if enabled):
 *    - keccak_256 (one-shot)
 *    - Known test vectors
 */

#include "sha2.h"
#include "sha3.h"
#include "unity.h"
#include <string.h>
#include <stdio.h>

void setUp(void) {}

void tearDown(void) {}

// Helper function to compare hex strings
static void assert_hex_equal(const uint8_t* expected, const uint8_t* actual, size_t len, const char* msg) {
  for (size_t i = 0; i < len; i++) {
    if (expected[i] != actual[i]) {
      char error_msg[256];
      snprintf(error_msg, sizeof(error_msg), "%s: Byte %zu: expected 0x%02x, got 0x%02x", 
               msg, i, expected[i], actual[i]);
      TEST_FAIL_MESSAGE(error_msg);
    }
  }
}

// Test: SHA-256 empty string
void test_sha256_empty() {
  // Known test vector: SHA-256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
  const uint8_t expected[32] = {
    0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
    0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
    0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
    0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55
  };
  
  uint8_t digest[32] = {0};
  sha256_Raw((const uint8_t*)"", 0, digest);
  
  assert_hex_equal(expected, digest, 32, "SHA-256 empty string");
}

// Test: SHA-256 "abc"
void test_sha256_abc() {
  // Known test vector: SHA-256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
  const uint8_t expected[32] = {
    0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
    0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
    0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
    0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
  };
  
  uint8_t digest[32] = {0};
  sha256_Raw((const uint8_t*)"abc", 3, digest);
  
  assert_hex_equal(expected, digest, 32, "SHA-256 \"abc\"");
}

// Test: SHA-256 incremental (multiple updates)
void test_sha256_incremental() {
  // SHA-256("abc") using incremental API
  const uint8_t expected[32] = {
    0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
    0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
    0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
    0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
  };
  
  SHA256_CTX ctx;
  sha256_Init(&ctx);
  sha256_Update(&ctx, (const uint8_t*)"a", 1);
  sha256_Update(&ctx, (const uint8_t*)"b", 1);
  sha256_Update(&ctx, (const uint8_t*)"c", 1);
  
  uint8_t digest[32] = {0};
  sha256_Final(&ctx, digest);
  
  assert_hex_equal(expected, digest, 32, "SHA-256 incremental");
}

// Test: SHA-256 long string
void test_sha256_long_string() {
  // SHA-256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
  const char* input = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  // Known hash: 248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1
  const uint8_t expected[32] = {
    0x24, 0x8d, 0x6a, 0x61, 0xd2, 0x06, 0x38, 0xb8,
    0xe5, 0xc0, 0x26, 0x93, 0x0c, 0x3e, 0x60, 0x39,
    0xa3, 0x3c, 0xe4, 0x59, 0x64, 0xff, 0x21, 0x67,
    0xf6, 0xec, 0xed, 0xd4, 0x19, 0xdb, 0x06, 0xc1
  };
  
  uint8_t digest[32] = {0};
  sha256_Raw((const uint8_t*)input, strlen(input), digest);
  
  assert_hex_equal(expected, digest, 32, "SHA-256 long string");
}

// Test: SHA-512 empty string
void test_sha512_empty() {
  // Known test vector: SHA-512("") = cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e
  const uint8_t expected[64] = {
    0xcf, 0x83, 0xe1, 0x35, 0x7e, 0xef, 0xb8, 0xbd,
    0xf1, 0x54, 0x28, 0x50, 0xd6, 0x6d, 0x80, 0x07,
    0xd6, 0x20, 0xe4, 0x05, 0x0b, 0x57, 0x15, 0xdc,
    0x83, 0xf4, 0xa9, 0x21, 0xd3, 0x6c, 0xe9, 0xce,
    0x47, 0xd0, 0xd1, 0x3c, 0x5d, 0x85, 0xf2, 0xb0,
    0xff, 0x83, 0x18, 0xd2, 0x87, 0x7e, 0xec, 0x2f,
    0x63, 0xb9, 0x31, 0xbd, 0x47, 0x41, 0x7a, 0x81,
    0xa5, 0x38, 0x32, 0x7a, 0xf9, 0x27, 0xda, 0x3e
  };
  
  uint8_t digest[64] = {0};
  sha512_Raw((const uint8_t*)"", 0, digest);
  
  assert_hex_equal(expected, digest, 64, "SHA-512 empty string");
}

// Test: SHA-512 "abc"
void test_sha512_abc() {
  // Known test vector: SHA-512("abc") = ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f
  const uint8_t expected[64] = {
    0xdd, 0xaf, 0x35, 0xa1, 0x93, 0x61, 0x7a, 0xba,
    0xcc, 0x41, 0x73, 0x49, 0xae, 0x20, 0x41, 0x31,
    0x12, 0xe6, 0xfa, 0x4e, 0x89, 0xa9, 0x7e, 0xa2,
    0x0a, 0x9e, 0xee, 0xe6, 0x4b, 0x55, 0xd3, 0x9a,
    0x21, 0x92, 0x99, 0x2a, 0x27, 0x4f, 0xc1, 0xa8,
    0x36, 0xba, 0x3c, 0x23, 0xa3, 0xfe, 0xeb, 0xbd,
    0x45, 0x4d, 0x44, 0x23, 0x64, 0x3c, 0xe8, 0x0e,
    0x2a, 0x9a, 0xc9, 0x4f, 0xa5, 0x4c, 0xa4, 0x9f
  };
  
  uint8_t digest[64] = {0};
  sha512_Raw((const uint8_t*)"abc", 3, digest);
  
  assert_hex_equal(expected, digest, 64, "SHA-512 \"abc\"");
}

// Test: SHA-512 incremental
void test_sha512_incremental() {
  // SHA-512("abc") using incremental API
  const uint8_t expected[64] = {
    0xdd, 0xaf, 0x35, 0xa1, 0x93, 0x61, 0x7a, 0xba,
    0xcc, 0x41, 0x73, 0x49, 0xae, 0x20, 0x41, 0x31,
    0x12, 0xe6, 0xfa, 0x4e, 0x89, 0xa9, 0x7e, 0xa2,
    0x0a, 0x9e, 0xee, 0xe6, 0x4b, 0x55, 0xd3, 0x9a,
    0x21, 0x92, 0x99, 0x2a, 0x27, 0x4f, 0xc1, 0xa8,
    0x36, 0xba, 0x3c, 0x23, 0xa3, 0xfe, 0xeb, 0xbd,
    0x45, 0x4d, 0x44, 0x23, 0x64, 0x3c, 0xe8, 0x0e,
    0x2a, 0x9a, 0xc9, 0x4f, 0xa5, 0x4c, 0xa4, 0x9f
  };
  
  SHA512_CTX ctx;
  sha512_Init(&ctx);
  sha512_Update(&ctx, (const uint8_t*)"a", 1);
  sha512_Update(&ctx, (const uint8_t*)"b", 1);
  sha512_Update(&ctx, (const uint8_t*)"c", 1);
  
  uint8_t digest[64] = {0};
  sha512_Final(&ctx, digest);
  
  assert_hex_equal(expected, digest, 64, "SHA-512 incremental");
}

// Test: SHA-512 long string
void test_sha512_long_string() {
  // SHA-512("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
  const char* input = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  // Known hash: 204a8fc6dda82f0a0ced7beb8e08a41657c16ef468b228a8279be331a703c33596fd15c13b1b07f9aa1d3bea57789ca031ad85c7a71dd70354ec631238ca3445
  const uint8_t expected[64] = {
    0x20, 0x4a, 0x8f, 0xc6, 0xdd, 0xa8, 0x2f, 0x0a,
    0x0c, 0xed, 0x7b, 0xeb, 0x8e, 0x08, 0xa4, 0x16,
    0x57, 0xc1, 0x6e, 0xf4, 0x68, 0xb2, 0x28, 0xa8,
    0x27, 0x9b, 0xe3, 0x31, 0xa7, 0x03, 0xc3, 0x35,
    0x96, 0xfd, 0x15, 0xc1, 0x3b, 0x1b, 0x07, 0xf9,
    0xaa, 0x1d, 0x3b, 0xea, 0x57, 0x78, 0x9c, 0xa0,
    0x31, 0xad, 0x85, 0xc7, 0xa7, 0x1d, 0xd7, 0x03,
    0x54, 0xec, 0x63, 0x12, 0x38, 0xca, 0x34, 0x45
  };
  
  uint8_t digest[64] = {0};
  sha512_Raw((const uint8_t*)input, strlen(input), digest);
  
  assert_hex_equal(expected, digest, 64, "SHA-512 long string");
}

// Test: SHA3-256 empty string
void test_sha3_256_empty() {
  // Known test vector: SHA3-256("") = a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a
  const uint8_t expected[32] = {
    0xa7, 0xff, 0xc6, 0xf8, 0xbf, 0x1e, 0xd7, 0x66,
    0x51, 0xc1, 0x47, 0x56, 0xa0, 0x61, 0xd6, 0x62,
    0xf5, 0x80, 0xff, 0x4d, 0xe4, 0x3b, 0x49, 0xfa,
    0x82, 0xd8, 0x0a, 0x4b, 0x80, 0xf8, 0x43, 0x4a
  };
  
  uint8_t digest[32] = {0};
  sha3_256((const unsigned char*)"", 0, digest);
  
  assert_hex_equal(expected, digest, 32, "SHA3-256 empty string");
}

// Test: SHA3-256 "abc"
void test_sha3_256_abc() {
  // Known test vector: SHA3-256("abc") = 3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532
  const uint8_t expected[32] = {
    0x3a, 0x98, 0x5d, 0xa7, 0x4f, 0xe2, 0x25, 0xb2,
    0x04, 0x5c, 0x17, 0x2d, 0x6b, 0xd3, 0x90, 0xbd,
    0x85, 0x5f, 0x08, 0x6e, 0x3e, 0x9d, 0x52, 0x5b,
    0x46, 0xbf, 0xe2, 0x45, 0x11, 0x43, 0x15, 0x32
  };
  
  uint8_t digest[32] = {0};
  sha3_256((const unsigned char*)"abc", 3, digest);
  
  assert_hex_equal(expected, digest, 32, "SHA3-256 \"abc\"");
}

// Test: SHA3-256 incremental
void test_sha3_256_incremental() {
  // SHA3-256("abc") using incremental API
  const uint8_t expected[32] = {
    0x3a, 0x98, 0x5d, 0xa7, 0x4f, 0xe2, 0x25, 0xb2,
    0x04, 0x5c, 0x17, 0x2d, 0x6b, 0xd3, 0x90, 0xbd,
    0x85, 0x5f, 0x08, 0x6e, 0x3e, 0x9d, 0x52, 0x5b,
    0x46, 0xbf, 0xe2, 0x45, 0x11, 0x43, 0x15, 0x32
  };
  
  SHA3_CTX ctx;
  uint8_t digest[32] = {0};
  sha3_256_Init(&ctx);
  sha3_Update(&ctx, (const unsigned char*)"a", 1);
  sha3_Update(&ctx, (const unsigned char*)"b", 1);
  sha3_Update(&ctx, (const unsigned char*)"c", 1);
  sha3_Final(&ctx, digest);
  
  assert_hex_equal(expected, digest, 32, "SHA3-256 incremental");
}

// Test: SHA3-256 long string
void test_sha3_256_long_string() {
  // SHA3-256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
  const char* input = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  // Known hash: 41c0dba2a9d6240849100376a8235e2c82e1b9998a999e21db32dd97496d3376
  const uint8_t expected[32] = {
    0x41, 0xc0, 0xdb, 0xa2, 0xa9, 0xd6, 0x24, 0x08,
    0x49, 0x10, 0x03, 0x76, 0xa8, 0x23, 0x5e, 0x2c,
    0x82, 0xe1, 0xb9, 0x99, 0x8a, 0x99, 0x9e, 0x21,
    0xdb, 0x32, 0xdd, 0x97, 0x49, 0x6d, 0x33, 0x76
  };
  
  uint8_t digest[32] = {0};
  sha3_256((const unsigned char*)input, strlen(input), digest);
  
  assert_hex_equal(expected, digest, 32, "SHA3-256 long string");
}

// Test: SHA3-512 empty string
void test_sha3_512_empty() {
  // Known test vector: SHA3-512("") = a69f73cca23a9ac5c8b567dc185a756e97c982164fe25859e0d1dcc1475c80a615b2123af1f5f94c11e3e9402c3ac558f500199d95b6d3e301758586281dcd26
  const uint8_t expected[64] = {
    0xa6, 0x9f, 0x73, 0xcc, 0xa2, 0x3a, 0x9a, 0xc5,
    0xc8, 0xb5, 0x67, 0xdc, 0x18, 0x5a, 0x75, 0x6e,
    0x97, 0xc9, 0x82, 0x16, 0x4f, 0xe2, 0x58, 0x59,
    0xe0, 0xd1, 0xdc, 0xc1, 0x47, 0x5c, 0x80, 0xa6,
    0x15, 0xb2, 0x12, 0x3a, 0xf1, 0xf5, 0xf9, 0x4c,
    0x11, 0xe3, 0xe9, 0x40, 0x2c, 0x3a, 0xc5, 0x58,
    0xf5, 0x00, 0x19, 0x9d, 0x95, 0xb6, 0xd3, 0xe3,
    0x01, 0x75, 0x85, 0x86, 0x28, 0x1d, 0xcd, 0x26
  };
  
  uint8_t digest[64] = {0};
  sha3_512((const unsigned char*)"", 0, digest);
  
  assert_hex_equal(expected, digest, 64, "SHA3-512 empty string");
}

// Test: SHA3-512 "abc"
void test_sha3_512_abc() {
  // Known test vector: SHA3-512("abc") = b751850b1a57168a5693cd924b6b096e08f621827444f70d884f5d0240d2712e10e116e9192af3c91a7ec57647e3934057340b4cf408d5a56592f8274eec53f0
  const uint8_t expected[64] = {
    0xb7, 0x51, 0x85, 0x0b, 0x1a, 0x57, 0x16, 0x8a,
    0x56, 0x93, 0xcd, 0x92, 0x4b, 0x6b, 0x09, 0x6e,
    0x08, 0xf6, 0x21, 0x82, 0x74, 0x44, 0xf7, 0x0d,
    0x88, 0x4f, 0x5d, 0x02, 0x40, 0xd2, 0x71, 0x2e,
    0x10, 0xe1, 0x16, 0xe9, 0x19, 0x2a, 0xf3, 0xc9,
    0x1a, 0x7e, 0xc5, 0x76, 0x47, 0xe3, 0x93, 0x40,
    0x57, 0x34, 0x0b, 0x4c, 0xf4, 0x08, 0xd5, 0xa5,
    0x65, 0x92, 0xf8, 0x27, 0x4e, 0xec, 0x53, 0xf0
  };
  
  uint8_t digest[64] = {0};
  sha3_512((const unsigned char*)"abc", 3, digest);
  
  assert_hex_equal(expected, digest, 64, "SHA3-512 \"abc\"");
}

// Test: SHA3-512 incremental
void test_sha3_512_incremental() {
  // SHA3-512("abc") using incremental API
  const uint8_t expected[64] = {
    0xb7, 0x51, 0x85, 0x0b, 0x1a, 0x57, 0x16, 0x8a,
    0x56, 0x93, 0xcd, 0x92, 0x4b, 0x6b, 0x09, 0x6e,
    0x08, 0xf6, 0x21, 0x82, 0x74, 0x44, 0xf7, 0x0d,
    0x88, 0x4f, 0x5d, 0x02, 0x40, 0xd2, 0x71, 0x2e,
    0x10, 0xe1, 0x16, 0xe9, 0x19, 0x2a, 0xf3, 0xc9,
    0x1a, 0x7e, 0xc5, 0x76, 0x47, 0xe3, 0x93, 0x40,
    0x57, 0x34, 0x0b, 0x4c, 0xf4, 0x08, 0xd5, 0xa5,
    0x65, 0x92, 0xf8, 0x27, 0x4e, 0xec, 0x53, 0xf0
  };
  
  SHA3_CTX ctx;
  uint8_t digest[64] = {0};
  sha3_512_Init(&ctx);
  sha3_Update(&ctx, (const unsigned char*)"a", 1);
  sha3_Update(&ctx, (const unsigned char*)"b", 1);
  sha3_Update(&ctx, (const unsigned char*)"c", 1);
  sha3_Final(&ctx, digest);
  
  assert_hex_equal(expected, digest, 64, "SHA3-512 incremental");
}

// Test: SHA3-512 long string
void test_sha3_512_long_string() {
  // SHA3-512("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
  const char* input = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  // Known hash: 04a371e84ecfb5b8b77cb48610fca8182dd457ce6f326a0fd3d7ec2f1e91636dee691fbe0c985302ba1b0d8dc78c086346b533b49c030d99a27daf1139d6e75e
  // Verified with openssl sha3-512
  const uint8_t expected[64] = {
    0x04, 0xa3, 0x71, 0xe8, 0x4e, 0xcf, 0xb5, 0xb8,
    0xb7, 0x7c, 0xb4, 0x86, 0x10, 0xfc, 0xa8, 0x18,
    0x2d, 0xd4, 0x57, 0xce, 0x6f, 0x32, 0x6a, 0x0f,
    0xd3, 0xd7, 0xec, 0x2f, 0x1e, 0x91, 0x63, 0x6d,
    0xee, 0x69, 0x1f, 0xbe, 0x0c, 0x98, 0x53, 0x02,
    0xba, 0x1b, 0x0d, 0x8d, 0xc7, 0x8c, 0x08, 0x63,
    0x46, 0xb5, 0x33, 0xb4, 0x9c, 0x03, 0x0d, 0x99,
    0xa2, 0x7d, 0xaf, 0x11, 0x39, 0xd6, 0xe7, 0x5e
  };
  
  uint8_t digest[64] = {0};
  sha3_512((const unsigned char*)input, strlen(input), digest);
  
  assert_hex_equal(expected, digest, 64, "SHA3-512 long string");
}

#if USE_KECCAK
// Test: Keccak-256 empty string
void test_keccak_256_empty() {
  // Known test vector: Keccak-256("") = c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470
  const uint8_t expected[32] = {
    0xc5, 0xd2, 0x46, 0x01, 0x86, 0xf7, 0x23, 0x3c,
    0x92, 0x7e, 0x7d, 0xb2, 0xdc, 0xc7, 0x03, 0xc0,
    0xe5, 0x00, 0xb6, 0x53, 0xca, 0x82, 0x27, 0x3b,
    0x7b, 0xfa, 0xd8, 0x04, 0x5d, 0x85, 0xa4, 0x70
  };
  
  uint8_t digest[32] = {0};
  keccak_256((const unsigned char*)"", 0, digest);
  
  assert_hex_equal(expected, digest, 32, "Keccak-256 empty string");
}

// Test: Keccak-256 "abc"
void test_keccak_256_abc() {
  // Known test vector: Keccak-256("abc") = 4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45
  // Note: This is the actual Keccak-256 hash (not SHA3-256)
  const uint8_t expected[32] = {
    0x4e, 0x03, 0x65, 0x7a, 0xea, 0x45, 0xa9, 0x4f,
    0xc7, 0xd4, 0x7b, 0xa8, 0x26, 0xc8, 0xd6, 0x67,
    0xc0, 0xd1, 0xe6, 0xe3, 0x3a, 0x64, 0xa0, 0x36,
    0xec, 0x44, 0xf5, 0x8f, 0xa1, 0x2d, 0x6c, 0x45
  };
  
  uint8_t digest[32] = {0};
  keccak_256((const unsigned char*)"abc", 3, digest);
  
  assert_hex_equal(expected, digest, 32, "Keccak-256 \"abc\"");
}

// Test: Keccak-256 incremental
void test_keccak_256_incremental() {
  // Keccak-256("abc") using incremental API
  const uint8_t expected[32] = {
    0x4e, 0x03, 0x65, 0x7a, 0xea, 0x45, 0xa9, 0x4f,
    0xc7, 0xd4, 0x7b, 0xa8, 0x26, 0xc8, 0xd6, 0x67,
    0xc0, 0xd1, 0xe6, 0xe3, 0x3a, 0x64, 0xa0, 0x36,
    0xec, 0x44, 0xf5, 0x8f, 0xa1, 0x2d, 0x6c, 0x45
  };
  
  SHA3_CTX ctx;
  uint8_t digest[32] = {0};
  keccak_256_Init(&ctx);
  keccak_Update(&ctx, (const unsigned char*)"a", 1);
  keccak_Update(&ctx, (const unsigned char*)"b", 1);
  keccak_Update(&ctx, (const unsigned char*)"c", 1);
  keccak_Final(&ctx, digest);
  
  assert_hex_equal(expected, digest, 32, "Keccak-256 incremental");
}

// Test: Keccak-256 long string
void test_keccak_256_long_string() {
  // Keccak-256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
  const char* input = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  // Known hash: 45d3b367a6904e6e8d502ee04999a7c27647f91fa845d456525fd352ae3d7371
  const uint8_t expected[32] = {
    0x45, 0xd3, 0xb3, 0x67, 0xa6, 0x90, 0x4e, 0x6e,
    0x8d, 0x50, 0x2e, 0xe0, 0x49, 0x99, 0xa7, 0xc2,
    0x76, 0x47, 0xf9, 0x1f, 0xa8, 0x45, 0xd4, 0x56,
    0x52, 0x5f, 0xd3, 0x52, 0xae, 0x3d, 0x73, 0x71
  };
  
  uint8_t digest[32] = {0};
  keccak_256((const unsigned char*)input, strlen(input), digest);
  
  assert_hex_equal(expected, digest, 32, "Keccak-256 long string");
}
#endif

int main(void) {
  UNITY_BEGIN();

  // SHA-256 tests
  RUN_TEST(test_sha256_empty);
  RUN_TEST(test_sha256_abc);
  RUN_TEST(test_sha256_incremental);
  RUN_TEST(test_sha256_long_string);

  // SHA-512 tests
  RUN_TEST(test_sha512_empty);
  RUN_TEST(test_sha512_abc);
  RUN_TEST(test_sha512_incremental);
  RUN_TEST(test_sha512_long_string);

  // SHA3-256 tests
  RUN_TEST(test_sha3_256_empty);
  RUN_TEST(test_sha3_256_abc);
  RUN_TEST(test_sha3_256_incremental);
  RUN_TEST(test_sha3_256_long_string);

  // SHA3-512 tests
  RUN_TEST(test_sha3_512_empty);
  RUN_TEST(test_sha3_512_abc);
  RUN_TEST(test_sha3_512_incremental);
  RUN_TEST(test_sha3_512_long_string);

#if USE_KECCAK
  // Keccak-256 tests
  RUN_TEST(test_keccak_256_empty);
  RUN_TEST(test_keccak_256_abc);
  RUN_TEST(test_keccak_256_incremental);
  RUN_TEST(test_keccak_256_long_string);
#endif

  return UNITY_END();
}

