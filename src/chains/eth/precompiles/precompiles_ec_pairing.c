/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */

#include "../bn254/bn254.h"
#include "bytes.h"
#include "precompiles.h"
#include <stdlib.h>
#include <string.h>

static pre_result_t pre_ec_pairing(bytes_t input, buffer_t* output, uint64_t* gas_used) {
  if (input.len % 192 != 0) {
    return PRE_INVALID_INPUT;
  }

  size_t num_pairs = input.len / 192;
  *gas_used        = 45000 + num_pairs * 34000;

  // Allocate arrays for batch processing
  bn254_g1_t* Ps = (bn254_g1_t*) malloc(num_pairs * sizeof(bn254_g1_t));
  bn254_g2_t* Qs = (bn254_g2_t*) malloc(num_pairs * sizeof(bn254_g2_t));

  if (num_pairs > 0 && (!Ps || !Qs)) {
    free(Ps);
    free(Qs);
    return PRE_ERROR;
  }

  for (size_t i = 0; i < num_pairs; i++) {
    const uint8_t* chunk = input.data + i * 192;

    // Parse P (G1) - 64 bytes
    if (!bn254_g1_from_bytes_be(&Ps[i], chunk)) {
      free(Ps);
      free(Qs);
      return PRE_INVALID_INPUT;
    }

    // Parse Q (G2) - 128 bytes
    if (!bn254_g2_from_bytes_eth(&Qs[i], chunk + 64)) {
      free(Ps);
      free(Qs);
      return PRE_INVALID_INPUT;
    }
  }

  // Perform batch pairing check
  // This function returns true if product of pairings is 1
  bool success = bn254_pairing_batch_check(Ps, Qs, num_pairs);

  free(Ps);
  free(Qs);

  buffer_reset(output);
  buffer_grow(output, 32);
  memset(output->data.data, 0, 32);
  output->data.data[31] = success ? 1 : 0;
  output->data.len      = 32;

  return PRE_SUCCESS;
}
