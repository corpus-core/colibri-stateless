/*
 * Copyright (c) 2025,2026 corpus.core GmbH
 * SPDX-License-Identifier: MIT
 *
 * EIP-197 BN128 pairing check (address 0x08). Input is a concatenation of 192-byte
 * `(G1 || G2_eth)` pairs; output is 32-byte boolean (last byte 0 or 1).
 */

#include "../bn254/bn254.h"
#include "bytes.h"
#include "precompiles.h"
#include <string.h>

/**
 * @brief BN128 pairing product check (`precompile` 0x08).
 *
 * Gas: `45000 + 34000 * num_pairs`. Returns `PRE_INVALID_INPUT` when `input.len % 192 != 0`.
 *
 * @param gas_used computed pairing gas on success
 */
static pre_result_t pre_ec_pairing(bytes_t input, buffer_t* output, uint64_t* gas_used, uint64_t gas_limit) {
  (void) gas_limit;
  if (input.len % 192 != 0) {
    return PRE_INVALID_INPUT;
  }

  size_t num_pairs = input.len / 192;
  *gas_used        = 45000 + num_pairs * 34000;

  // malloc is only 16-byte aligned. These points contain 32-byte intx limbs,
  // and GCC copies them with aligned vector moves.
  bn254_g1_t* Ps = NULL;
  bn254_g2_t* Qs = NULL;
  if (num_pairs > 0) {
    Ps = (bn254_g1_t*) safe_aligned_alloc(c4_alignof(bn254_g1_t), num_pairs * sizeof(bn254_g1_t));
    Qs = (bn254_g2_t*) safe_aligned_alloc(c4_alignof(bn254_g2_t), num_pairs * sizeof(bn254_g2_t));
  }

  for (size_t i = 0; i < num_pairs; i++) {
    const uint8_t* chunk = input.data + i * 192;

    // Parse P (G1) - 64 bytes
    if (!bn254_g1_from_bytes_be(&Ps[i], chunk)) {
      safe_aligned_free(Ps);
      safe_aligned_free(Qs);
      return PRE_INVALID_INPUT;
    }

    // Parse Q (G2) - 128 bytes
    if (!bn254_g2_from_bytes_eth(&Qs[i], chunk + 64)) {
      safe_aligned_free(Ps);
      safe_aligned_free(Qs);
      return PRE_INVALID_INPUT;
    }
  }

  // Perform batch pairing check
  // This function returns true if product of pairings is 1
  bool success = bn254_pairing_batch_check(Ps, Qs, num_pairs);

  safe_aligned_free(Ps);
  safe_aligned_free(Qs);

  buffer_reset(output);
  buffer_grow(output, 32);
  memset(output->data.data, 0, 32);
  output->data.data[31] = success ? 1 : 0;
  output->data.len      = 32;

  return PRE_SUCCESS;
}
