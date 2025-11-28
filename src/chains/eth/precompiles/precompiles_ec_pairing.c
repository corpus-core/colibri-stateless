/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */

#include "bytes.h"
#include "precompiles.h"
#include "../bn254/bn254.h"
#include <string.h>

static pre_result_t pre_ec_pairing(bytes_t input, buffer_t* output, uint64_t* gas_used) {
  if (input.len % 192 != 0) {
    return PRE_INVALID_INPUT;
  }

  size_t num_pairs = input.len / 192;
  *gas_used        = 45000 + num_pairs * 34000;

  bn254_fp12_t result;
  memset(&result, 0, sizeof(bn254_fp12_t));
  result.c0.c0.c0.bytes[31] = 1; // Initialize to 1

  for (size_t i = 0; i < num_pairs; i++) {
    const uint8_t* chunk = input.data + i * 192;
    
    // Parse P (G1) - 64 bytes
    bn254_g1_t P;
    if (!bn254_g1_from_bytes_be(&P, chunk)) return PRE_INVALID_INPUT;

    // Parse Q (G2) - 128 bytes
    bn254_g2_t Q;
    // EIP-197 says Q is encoded as (x, y) where x, y are Fp2 elements.
    // x = x1*i + x0 => [x1 (32)][x0 (32)]
    // y = y1*i + y0 => [y1 (32)][y0 (32)]
    // So input is [P_x][P_y] [Q_x1][Q_x0][Q_y1][Q_y0]
    // My bn254_g2_from_bytes_eth handles this order.
    if (!bn254_g2_from_bytes_eth(&Q, chunk + 64)) return PRE_INVALID_INPUT;

    bn254_fp12_t miller_res;
    bn254_miller_loop(&miller_res, &P, &Q);
    bn254_fp12_mul(&result, &result, &miller_res);
  }

  bn254_final_exponentiation(&result, &result);

  int is_one = bn254_fp12_is_one(&result);

  buffer_reset(output);
  buffer_grow(output, 32);
  memset(output->data.data, 0, 32);
  output->data.data[31] = is_one ? 1 : 0;
  output->data.len      = 32;

  return PRE_SUCCESS;
}
