/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */

#include "bytes.h"
#include "precompiles.h"
#include "../bn254/bn254.h"
#include <string.h>

// ECADD (0x06)
pre_result_t pre_ec_add(bytes_t input, buffer_t* output, uint64_t* gas_used) {
  *gas_used = 150;

  // Input must be 128 bytes (x1, y1, x2, y2)
  uint8_t input_buf[128];
  memset(input_buf, 0, 128);
  if (input.len > 0) {
    memcpy(input_buf, input.data, input.len < 128 ? input.len : 128);
  }

  bn254_g1_t p1, p2, p3;
  
  if (!bn254_g1_from_bytes_be(&p1, input_buf)) return PRE_INVALID_INPUT;
  if (!bn254_g1_from_bytes_be(&p2, input_buf + 64)) return PRE_INVALID_INPUT;

  bn254_g1_add(&p3, &p1, &p2);

  output->data.len = 0;
  buffer_grow(output, 64);
  bn254_g1_to_bytes(&p3, output->data.data);
  output->data.len = 64;

  return PRE_SUCCESS;
}

// Scalar multiplication on the BN128 curve: s * (x,y) = (x',y')
pre_result_t pre_ec_mul(bytes_t input, buffer_t* output, uint64_t* gas_used) {
  *gas_used = 6000; // Gas cost of EC mul operation (EIP-1108)

  // Input: x, y, scalar (96 bytes minimum effectively, usually padded)
  // If input < 96 bytes, treated as zero padded? 
  // Standard says input is padded to 96 bytes (32x3) if shorter.
  
  uint8_t input_buf[96];
  memset(input_buf, 0, 96);
  if (input.len > 0) {
      memcpy(input_buf, input.data, input.len < 96 ? input.len : 96);
  }

  bn254_g1_t p;
  if (!bn254_g1_from_bytes_be(&p, input_buf)) return PRE_INVALID_INPUT;

  uint256_t scalar;
  // Scalar is at offset 64
  // We need to load it into uint256_t
  // Assuming bn254 header includes intx or we can use memcpy if layout matches
  // bn254_fp_t is uint256_t alias.
  memcpy(scalar.bytes, input_buf + 64, 32);

  bn254_g1_t res;
  bn254_g1_mul(&res, &p, &scalar);

  output->data.len = 0;
  buffer_grow(output, 64);
  bn254_g1_to_bytes(&res, output->data.data);
  output->data.len = 64;

  return PRE_SUCCESS;
}
