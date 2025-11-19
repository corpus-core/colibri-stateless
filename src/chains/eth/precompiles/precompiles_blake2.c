/*
 * Copyright (c) 2025 corpus.core
 *
 * SPDX-License-Identifier: MIT
 */

#include "blake2_common.h"
#include "precompiles.h"
#include <stdint.h>
#include <string.h>

static const uint8_t blake2b_sigma[10][16] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
    {11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4},
    {7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8},
    {9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13},
    {2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9},
    {12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11},
    {13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10},
    {6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5},
    {10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0},
};

static const uint64_t blake2b_IV[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
    0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
    0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};

#define G(r, i, a, b, c, d)                     \
  do {                                          \
    a = a + b + m[blake2b_sigma[r][2 * i + 0]]; \
    d = rotr64(d ^ a, 32);                      \
    c = c + d;                                  \
    b = rotr64(b ^ c, 24);                      \
    a = a + b + m[blake2b_sigma[r][2 * i + 1]]; \
    d = rotr64(d ^ a, 16);                      \
    c = c + d;                                  \
    b = rotr64(b ^ c, 63);                      \
  } while (0)

#define ROUND(r)                       \
  do {                                 \
    G(r, 0, v[0], v[4], v[8], v[12]);  \
    G(r, 1, v[1], v[5], v[9], v[13]);  \
    G(r, 2, v[2], v[6], v[10], v[14]); \
    G(r, 3, v[3], v[7], v[11], v[15]); \
    G(r, 4, v[0], v[5], v[10], v[15]); \
    G(r, 5, v[1], v[6], v[11], v[12]); \
    G(r, 6, v[2], v[7], v[8], v[13]);  \
    G(r, 7, v[3], v[4], v[9], v[14]);  \
  } while (0)

static pre_result_t pre_blake2f(bytes_t input, buffer_t* output, uint64_t* gas_used) {
  if (input.len != 213) return PRE_INVALID_INPUT;

  // Parse input
  // [0:4] rounds (big-endian)
  uint32_t rounds = (uint32_t) bytes_as_be(bytes_slice(input, 0, 4));

  // [4:68] h (state vector, 8 8-byte little-endian)
  uint64_t h[8];
  for (int i = 0; i < 8; i++) {
    h[i] = load64(input.data + 4 + i * 8);
  }

  // [68:196] m (message block vector, 16 8-byte little-endian)
  uint64_t m[16];
  for (int i = 0; i < 16; i++) {
    m[i] = load64(input.data + 68 + i * 8);
  }

  // [196:212] t (offset counters, 2 8-byte little-endian)
  uint64_t t[2];
  t[0] = load64(input.data + 196);
  t[1] = load64(input.data + 204);

  // [212] f (final block indicator flag)
  uint8_t f_flag = input.data[212];
  if (f_flag != 0 && f_flag != 1) return PRE_INVALID_INPUT;

  // Gas calculation
  *gas_used = rounds;

  // Initialize working vector v
  uint64_t v[16];
  for (int i = 0; i < 8; i++) {
    v[i] = h[i];
  }
  for (int i = 0; i < 8; i++) {
    v[i + 8] = blake2b_IV[i];
  }

  v[12] ^= t[0];
  v[13] ^= t[1];

  if (f_flag) {
    v[14] = ~v[14];
  }

  // Compress
  for (uint32_t i = 0; i < rounds; i++) {
    int r = i % 10;
    ROUND(r);
  }

  // Update state vector h
  for (int i = 0; i < 8; i++) {
    h[i] ^= v[i] ^ v[i + 8];
  }

  // Output
  buffer_reset(output);
  buffer_grow(output, 64);
  for (int i = 0; i < 8; i++) {
    store64(output->data.data + i * 8, h[i]);
  }
  output->data.len = 64;

  return PRE_SUCCESS;
}

#undef G
#undef ROUND
