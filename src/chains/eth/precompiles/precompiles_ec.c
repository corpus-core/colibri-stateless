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

// secp256k1 modulus in big-endian format
static const uint8_t SECP256K1_MODULUS[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFC, 0x2F};

// secp256k1 curve parameter: b = 7
static const uint8_t SECP256K1_B[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07};

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

// Check if a point is on the secp256k1 curve: y^2 = x^3 + 7 (mod p)
static bool is_on_curve(const uint256_t* x, const uint256_t* y, const uint256_t* modulus) {
  // Calculate right side: x^3 + 7 (mod p)
  uint256_t x_squared, x_cubed, right_side, b;
  intx_init(&x_squared);
  intx_init(&x_cubed);
  intx_init(&right_side);
  uint256_from_be_bytes(&b, SECP256K1_B, 32);

  // x^2 mod p
  intx_mul(&x_squared, x, x);
  intx_mod(&x_squared, &x_squared, modulus);

  // x^3 mod p
  intx_mul(&x_cubed, &x_squared, x);
  intx_mod(&x_cubed, &x_cubed, modulus);

  // x^3 + 7 mod p
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
  // Check if a is 0
  if (intx_is_zero(a)) {
    intx_init(result);
    return false;
  }

  uint256_t r0, r1, s0, s1, t0, t1, q, tmp1, tmp2;

  // Initialize variables for extended Euclidean algorithm
  intx_init(&r0);
  intx_init(&r1);
  intx_init(&s0);
  intx_init(&s1);
  intx_init(&t0);
  intx_init(&t1);
  intx_init(&q);
  intx_init(&tmp1);
  intx_init(&tmp2);

  // r0 = modulus, r1 = a
  intx_add(&r0, modulus, NULL);
  intx_add(&r1, a, NULL);

  // s0 = 1, s1 = 0
  intx_init_value(&s0, 1);
  intx_init(&s1);

  // t0 = 0, t1 = 1
  intx_init(&t0);
  intx_init_value(&t1, 1);

  while (!intx_is_zero(&r1)) {
    // q = r0 / r1
    intx_div(&q, &r0, &r1);

    // (r0, r1) = (r1, r0 - q * r1)
    intx_add(&tmp1, &r1, NULL);
    intx_mul(&tmp2, &q, &r1);
    intx_sub(&r0, &r0, &tmp2);
    intx_add(&r1, &tmp1, NULL);

    // (s0, s1) = (s1, s0 - q * s1)
    intx_add(&tmp1, &s1, NULL);
    intx_mul(&tmp2, &q, &s1);
    intx_sub(&s0, &s0, &tmp2);
    intx_add(&s1, &tmp1, NULL);

    // (t0, t1) = (t1, t0 - q * t1)
    intx_add(&tmp1, &t1, NULL);
    intx_mul(&tmp2, &q, &t1);
    intx_sub(&t0, &t0, &tmp2);
    intx_add(&t1, &tmp1, NULL);
  }

  // If r0 > 1, a is not invertible
  uint256_t one;
  intx_init_value(&one, 1);
  if (!intx_eq(&r0, &one)) {
    intx_init(result);
    return false;
  }

  // Make sure s0 is positive
  if (t0.bytes[0] & 0x80) {
    intx_add(&t0, &t0, modulus);
  }

  // Return the result
  intx_mod(result, &t0, modulus);
  return true;
}

// Point addition on the secp256k1 curve: (x1,y1) + (x2,y2) = (x3,y3)
pre_result_t pre_ec_add(bytes_t input, buffer_t* output, uint64_t* gas_used) {
  *gas_used = 500; // Gas cost of EC add operation

  // Check input length
  if (input.len < 128) return PRE_INVALID_INPUT;

  // Extract coordinates of points
  uint256_t x1, y1, x2, y2, modulus;
  intx_init(&x1);
  intx_init(&y1);
  intx_init(&x2);
  intx_init(&y2);
  intx_init(&modulus);

  uint256_from_be_bytes(&x1, input.data, 32);
  uint256_from_be_bytes(&y1, input.data + 32, 32);
  uint256_from_be_bytes(&x2, input.data + 64, 32);
  uint256_from_be_bytes(&y2, input.data + 96, 32);
  uint256_from_be_bytes(&modulus, SECP256K1_MODULUS, 32);

  // Allocate result buffer
  output->data.len = 0;
  buffer_grow(output, 64);
  uint8_t* result = output->data.data;

  // Check if both points are zero (point at infinity)
  if (intx_is_zero(&x1) && intx_is_zero(&y1) && intx_is_zero(&x2) && intx_is_zero(&y2)) {
    // Result is also point at infinity
    memset(result, 0, 64);
    return PRE_SUCCESS;
  }

  // Validate that both points are on the curve
  if (!is_on_curve(&x1, &y1, &modulus) || !is_on_curve(&x2, &y2, &modulus)) {
    return PRE_INVALID_INPUT;
  }

  uint256_t x3, y3;
  intx_init(&x3);
  intx_init(&y3);

  // Check if P1 = -P2 (negation in elliptic curves: (x,-y) is the negation of (x,y))
  if (intx_eq(&x1, &x2)) {
    uint256_t neg_y2;
    intx_init(&neg_y2);
    intx_sub(&neg_y2, &modulus, &y2); // Calculate -y2 = p - y2

    if (intx_eq(&y1, &neg_y2)) {
      // P1 + (-P1) = O (point at infinity)
      memset(result, 0, 64);
      return PRE_SUCCESS;
    }

    // Equal x coordinates and y coordinates means doubling the point
    // Point doubling formula for y≠0:
    // s = (3*x1^2) / (2*y1)
    // x3 = s^2 - 2*x1
    // y3 = s*(x1 - x3) - y1

    uint256_t two, three, s, s_num, s_den, s_den_inv, temp;
    intx_init(&two);
    intx_init(&three);
    intx_init(&s);
    intx_init(&s_num);
    intx_init(&s_den);
    intx_init(&s_den_inv);
    intx_init(&temp);

    intx_init_value(&two, 2);
    intx_init_value(&three, 3);

    // Calculate s numerator: 3*x1^2
    intx_mul(&temp, &x1, &x1);
    intx_mod(&temp, &temp, &modulus);
    intx_mul(&s_num, &three, &temp);
    intx_mod(&s_num, &s_num, &modulus);

    // Calculate s denominator: 2*y1
    intx_mul(&s_den, &two, &y1);
    intx_mod(&s_den, &s_den, &modulus);

    // Calculate modular inverse of denominator
    if (!uint256_mod_inverse(&s_den_inv, &s_den, &modulus)) {
      return PRE_ERROR; // Modular inverse does not exist
    }

    // Calculate s = s_num * s_den_inv (mod p)
    intx_mul(&s, &s_num, &s_den_inv);
    intx_mod(&s, &s, &modulus);

    // Calculate x3 = s^2 - 2*x1
    intx_mul(&temp, &s, &s);
    intx_mod(&temp, &temp, &modulus);
    intx_mul(&x3, &two, &x1);
    intx_sub(&x3, &temp, &x3);
    intx_mod(&x3, &x3, &modulus);

    // Calculate y3 = s*(x1 - x3) - y1
    intx_sub(&temp, &x1, &x3);
    intx_mod(&temp, &temp, &modulus);
    intx_mul(&y3, &s, &temp);
    intx_mod(&y3, &y3, &modulus);
    intx_sub(&y3, &y3, &y1);
    intx_mod(&y3, &y3, &modulus);
  }
  else {
    // Different x coordinates - use the standard addition formula
    // s = (y2 - y1) / (x2 - x1)
    // x3 = s^2 - x1 - x2
    // y3 = s*(x1 - x3) - y1

    uint256_t s, s_num, s_den, s_den_inv, temp;
    intx_init(&s);
    intx_init(&s_num);
    intx_init(&s_den);
    intx_init(&s_den_inv);
    intx_init(&temp);

    // Calculate s numerator: y2 - y1
    intx_sub(&s_num, &y2, &y1);
    intx_mod(&s_num, &s_num, &modulus);

    // Calculate s denominator: x2 - x1
    intx_sub(&s_den, &x2, &x1);
    intx_mod(&s_den, &s_den, &modulus);

    // Calculate modular inverse of denominator
    if (!uint256_mod_inverse(&s_den_inv, &s_den, &modulus)) {
      return PRE_ERROR; // Modular inverse does not exist
    }

    // Calculate s = s_num * s_den_inv (mod p)
    intx_mul(&s, &s_num, &s_den_inv);
    intx_mod(&s, &s, &modulus);

    // Calculate x3 = s^2 - x1 - x2
    intx_mul(&temp, &s, &s);
    intx_mod(&temp, &temp, &modulus);
    intx_sub(&x3, &temp, &x1);
    intx_mod(&x3, &x3, &modulus);
    intx_sub(&x3, &x3, &x2);
    intx_mod(&x3, &x3, &modulus);

    // Calculate y3 = s*(x1 - x3) - y1
    intx_sub(&temp, &x1, &x3);
    intx_mod(&temp, &temp, &modulus);
    intx_mul(&y3, &s, &temp);
    intx_mod(&y3, &y3, &modulus);
    intx_sub(&y3, &y3, &y1);
    intx_mod(&y3, &y3, &modulus);
  }

  // Output the resulting point
  uint256_to_be_bytes(&x3, result, 32);
  uint256_to_be_bytes(&y3, result + 32, 32);

  return PRE_SUCCESS;
}

// TODO: Implement pre_ec_mul
pre_result_t pre_ec_mul(bytes_t input, buffer_t* output, uint64_t* gas_used) {
  *gas_used = 40000; // Gas cost of EC multiplication

  // Implementation similar to pre_ec_add but using scalar multiplication
  // For a complete implementation, would implement scalar multiplication algorithm

  output->data.len = 0;
  buffer_grow(output, 64);
  memset(output->data.data, 0, 64);

  return PRE_ERROR; // Indicating that the implementation is incomplete
}
