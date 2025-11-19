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

#include "bytes.h"
#include "crypto.h"
#include "intx_c_api.h"
#include "json.h"
#include "precompiles.h"
#include "ripemd160.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// BN128 (Alt-BN128) modulus in big-endian format
// p = 21888242871839275222246405745257275088696311157297823662689037894645226208583
static const uint8_t BN128_MODULUS[] = {
    0x30, 0x64, 0x4e, 0x72, 0xe1, 0x31, 0xa0, 0x29, 0xb8, 0x50, 0x45, 0xb6, 0x81, 0x81, 0x58, 0x5d,
    0x97, 0x81, 0x6a, 0x91, 0x68, 0x71, 0xca, 0x8d, 0x3c, 0x20, 0x8c, 0x16, 0xd8, 0x7c, 0xfd, 0x03};

// BN128 curve parameter: b = 3
static const uint8_t BN128_B[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03};

// Helper function to initialize uint256_t from a byte array
static void uint256_from_be_bytes(uint256_t* result, const uint8_t* bytes, size_t len) {
  memset(result->bytes, 0, 32);
  if (len <= 32) {
    memcpy(result->bytes + (32 - len), bytes, len);
  }
  else {
    memcpy(result->bytes, bytes + (len - 32), 32);
  }
}

// Helper function to copy uint256_t to a byte array in big-endian format
static void uint256_to_be_bytes(const uint256_t* value, uint8_t* bytes, size_t len) {
  // Find first non-zero byte
  size_t start_idx = 0;
  while (start_idx < 32 && value->bytes[start_idx] == 0) {
    start_idx++;
  }

  // If all zeros, set a single zero byte
  if (start_idx == 32) {
    if (len > 0) bytes[len - 1] = 0;
    return;
  }

  // Copy non-zero bytes
  size_t data_len = 32 - start_idx;
  if (data_len <= len) {
    memcpy(bytes + (len - data_len), value->bytes + start_idx, data_len);
    memset(bytes, 0, len - data_len);
  }
  else {
    memcpy(bytes, value->bytes + (start_idx + data_len - len), len);
  }
}

// Check if a point is on the BN128 curve: y^2 = x^3 + 3 (mod p)
static bool is_on_curve(const uint256_t* x, const uint256_t* y, const uint256_t* modulus) {
  // Calculate right side: x^3 + 3 (mod p)
  uint256_t x_squared, x_cubed, right_side, b;
  intx_init(&x_squared);
  intx_init(&x_cubed);
  intx_init(&right_side);
  uint256_from_be_bytes(&b, BN128_B, 32);

  // x^2 mod p
  intx_mul(&x_squared, x, x);
  intx_mod(&x_squared, &x_squared, modulus);

  // x^3 mod p
  intx_mul(&x_cubed, &x_squared, x);
  intx_mod(&x_cubed, &x_cubed, modulus);

  // x^3 + 3 mod p
  intx_add(&right_side, &x_cubed, &b);
  intx_mod(&right_side, &right_side, modulus);

  // Calculate left side: y^2 (mod p)
  uint256_t y_squared, left_side;
  intx_init(&y_squared);
  intx_init(&left_side);

  // y^2 mod p
  intx_mul(&y_squared, y, y);
  intx_mod(&left_side, &y_squared, modulus);

  // Check if left_side == right_side
  return intx_eq(&left_side, &right_side);
}

// Helper function to calculate modular inverse using Extended Euclidean Algorithm
static bool uint256_mod_inverse(uint256_t* result, const uint256_t* a, const uint256_t* modulus) {
  // Check if a is zero
  if (intx_is_zero(a)) {
    intx_init(result);
    return false;
  }

  uint256_t r0, r1, t0, t1, q, tmp1, tmp2;

  // Initialize variables
  intx_init(&r0);
  intx_init(&r1);
  intx_init(&t0);
  intx_init(&t1);
  intx_init(&q);
  intx_init(&tmp1);
  intx_init(&tmp2);

  // r0 = modulus, r1 = a
  memcpy(&r0, modulus, sizeof(uint256_t));
  memcpy(&r1, a, sizeof(uint256_t));

  // t0 = 0, t1 = 1
  intx_init(&t0);
  intx_init_value(&t1, 1);

  while (!intx_is_zero(&r1)) {
    // q = r0 / r1
    intx_div(&q, &r0, &r1);

    // Update r: (r0, r1) = (r1, r0 % r1)
    intx_mod(&tmp1, &r0, &r1); // tmp1 = r0 % r1
    memcpy(&r0, &r1, sizeof(uint256_t));
    memcpy(&r1, &tmp1, sizeof(uint256_t));

    // Update t: t_new = t0 - q * t1 (mod modulus)
    intx_mul(&tmp2, &q, &t1);
    intx_mod(&tmp2, &tmp2, modulus); // tmp2 = (q * t1) % modulus

    // t_new = t0 - tmp2 (mod modulus)
    if (intx_lt(&t0, &tmp2)) {
      // t0 < tmp2, so result would be negative.
      // t_new = t0 + modulus - tmp2
      intx_sub(&tmp1, modulus, &tmp2);
      intx_add(&tmp1, &t0, &tmp1);
      // Check for overflow (unlikely with BN128 modulus < 2^254)
      // But to be safe, we can mod again if needed, but (t0 + modulus - tmp2) < 2*modulus
      // If it exceeds 256 bits, intx_add wraps.
      // BN128 modulus is ~254 bits. 2*modulus fits in 256 bits.
      // So simple add is fine.
    }
    else {
      // t0 >= tmp2
      intx_sub(&tmp1, &t0, &tmp2);
    }

    // Shift t
    memcpy(&t0, &t1, sizeof(uint256_t));
    memcpy(&t1, &tmp1, sizeof(uint256_t));
  }

  // If r0 > 1, a is not invertible
  uint256_t one;
  intx_init_value(&one, 1);
  if (!intx_eq(&r0, &one)) {
    intx_init(result);
    return false;
  }

  // Result is t0
  intx_mod(result, &t0, modulus);
  return true;
}

// Helper function for point addition (P3 = P1 + P2)
static bool ec_add(uint256_t* x3, uint256_t* y3, const uint256_t* x1, const uint256_t* y1, const uint256_t* x2, const uint256_t* y2, const uint256_t* modulus) {
  // Check if P1 is infinity (0, 0) - simplified check
  if (intx_is_zero(x1) && intx_is_zero(y1)) {
    *x3 = *x2;
    *y3 = *y2;
    return true;
  }

  // Check if P2 is infinity (0, 0)
  if (intx_is_zero(x2) && intx_is_zero(y2)) {
    *x3 = *x1;
    *y3 = *y1;
    return true;
  }

  // Check if P1 = -P2
  bool x_eq = intx_eq(x1, x2);

  if (x_eq) {
    uint256_t neg_y2;
    intx_init(&neg_y2);
    intx_sub(&neg_y2, modulus, y2);

    bool y_eq_neg = intx_eq(y1, &neg_y2);

    if (y_eq_neg) {
      intx_init(x3);
      intx_init(y3);
      return true;
    }
  }

  uint256_t s, s_num, s_den, s_den_inv, temp;
  intx_init(&s);
  intx_init(&s_num);
  intx_init(&s_den);
  intx_init(&s_den_inv);
  intx_init(&temp);

  if (intx_eq(x1, x2)) {
    // Point doubling
    // s = (3*x1^2) / (2*y1)
    uint256_t two, three;
    intx_init_value(&two, 2);
    intx_init_value(&three, 3);

    intx_mul(&temp, x1, x1);
    intx_mod(&temp, &temp, modulus);
    intx_mul(&s_num, &three, &temp);
    intx_mod(&s_num, &s_num, modulus);

    intx_mul(&s_den, &two, y1);
    intx_mod(&s_den, &s_den, modulus);
  }
  else {
    // Point addition
    // s = (y2 - y1) / (x2 - x1)
    intx_sub(&s_num, y2, y1);
    intx_mod(&s_num, &s_num, modulus);

    intx_sub(&s_den, x2, x1);
    intx_mod(&s_den, &s_den, modulus);
  }

  if (!uint256_mod_inverse(&s_den_inv, &s_den, modulus)) {
    return false;
  }

  intx_mul(&s, &s_num, &s_den_inv);
  intx_mod(&s, &s, modulus);

  // x3 = s^2 - x1 - x2
  intx_mul(&temp, &s, &s);
  intx_mod(&temp, &temp, modulus);
  intx_sub(x3, &temp, x1);
  intx_mod(x3, x3, modulus);
  intx_sub(x3, x3, x2);
  intx_mod(x3, x3, modulus);

  // y3 = s*(x1 - x3) - y1
  intx_sub(&temp, x1, x3);
  intx_mod(&temp, &temp, modulus);
  intx_mul(y3, &s, &temp);
  intx_mod(y3, y3, modulus);
  intx_sub(y3, y3, y1);
  intx_mod(y3, y3, modulus);

  return true;
}

// ECADD (0x06)
pre_result_t pre_ec_add(bytes_t input, buffer_t* output, uint64_t* gas_used) {
  *gas_used = 150;

  // Input must be 128 bytes (x1, y1, x2, y2)
  // If input is shorter, it is padded with zeros.
  // We can copy to a buffer or handle reading carefully.
  // For simplicity, let's copy to a 128-byte buffer.
  uint8_t input_buf[128];
  memset(input_buf, 0, 128);
  if (input.len > 0) {
    memcpy(input_buf, input.data, input.len < 128 ? input.len : 128);
  }

  uint256_t p1_x, p1_y, p2_x, p2_y, p3_x, p3_y, modulus;
  uint256_from_be_bytes(&p1_x, input_buf, 32);
  uint256_from_be_bytes(&p1_y, input_buf + 32, 32);
  uint256_from_be_bytes(&p2_x, input_buf + 64, 32);
  uint256_from_be_bytes(&p2_y, input_buf + 96, 32);
  uint256_from_be_bytes(&modulus, BN128_MODULUS, 32);

  // Allocate result buffer
  output->data.len = 0;
  buffer_grow(output, 64);
  uint8_t* result = output->data.data;

  // Check if points are on curve
  // (0,0) is valid (infinity)
  bool p1_inf = intx_is_zero(&p1_x) && intx_is_zero(&p1_y);
  if (!p1_inf && !is_on_curve(&p1_x, &p1_y, &modulus)) return PRE_INVALID_INPUT;

  bool p2_inf = intx_is_zero(&p2_x) && intx_is_zero(&p2_y);
  if (!p2_inf && !is_on_curve(&p2_x, &p2_y, &modulus)) return PRE_INVALID_INPUT;

  if (!ec_add(&p3_x, &p3_y, &p1_x, &p1_y, &p2_x, &p2_y, &modulus)) {
    return PRE_ERROR;
  }

  uint256_to_be_bytes(&p3_x, result, 32);
  uint256_to_be_bytes(&p3_y, result + 32, 32);

  output->data.len = 64;

  return PRE_SUCCESS;
}

// Scalar multiplication on the BN128 curve: s * (x,y) = (x',y')
pre_result_t pre_ec_mul(bytes_t input, buffer_t* output, uint64_t* gas_used) {
  *gas_used = 6000; // Gas cost of EC mul operation (EIP-1108)

  if (input.len < 96) return PRE_INVALID_INPUT;

  uint256_t x, y, s, modulus;
  intx_init(&x);
  intx_init(&y);
  intx_init(&s);
  intx_init(&modulus);

  uint256_from_be_bytes(&x, input.data, 32);
  uint256_from_be_bytes(&y, input.data + 32, 32);
  uint256_from_be_bytes(&s, input.data + 64, 32);
  uint256_from_be_bytes(&modulus, BN128_MODULUS, 32);

  // Allocate result buffer
  output->data.len = 0;
  buffer_grow(output, 64);
  uint8_t* result = output->data.data;

  bool p_inf = intx_is_zero(&x) && intx_is_zero(&y);
  if (!p_inf && !is_on_curve(&x, &y, &modulus)) return PRE_INVALID_INPUT;

  // Result accumulator (start at infinity)
  uint256_t rx, ry;
  intx_init(&rx);
  intx_init(&ry);

  // Double-and-add algorithm
  // Iterate from MSB to LSB of scalar s
  // Since intx doesn't give easy bit access, we can iterate bytes then bits
  // Or just use intx_shl/shr if available, or check bits manually.
  // s is 32 bytes.

  // Current point P = (x, y)
  uint256_t cx = x;
  uint256_t cy = y;

  // We'll iterate from LSB to MSB for standard double-and-add
  for (int i = 0; i < 32; i++) {
    // Process byte i (from end of array, which is LSB in big-endian if we read it right?
    // Wait, intx internal rep is little endian usually, but we loaded from big endian bytes.
    // Let's assume we can check bits of s.
    // Actually, let's just use the bytes from the input directly for the scalar to be safe about order.
    uint8_t byte = input.data[64 + 31 - i]; // Start from last byte (LSB)

    for (int j = 0; j < 8; j++) {
      if ((byte >> j) & 1) {
        if (!ec_add(&rx, &ry, &rx, &ry, &cx, &cy, &modulus)) return PRE_ERROR;
      }
      if (!ec_add(&cx, &cy, &cx, &cy, &cx, &cy, &modulus)) return PRE_ERROR;
    }
  }

  uint256_to_be_bytes(&rx, result, 32);
  uint256_to_be_bytes(&ry, result + 32, 32);

  output->data.len = 64;

  return PRE_SUCCESS;
}
