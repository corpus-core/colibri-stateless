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
#include "ecdsa.h"
#include "json.h"
#include "nist256p1.h"
#include "precompiles.h"

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#include "ripemd160.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef INTX
#include "intx_c_api.h"
#endif

typedef pre_result_t (*precompile_func_t)(bytes_t input, buffer_t* output, uint64_t* gas_used);

#define PRECOMPILE_FN_COUNT 20 // Updated count based on new array size (0x13 + 1)
#define data_word_size(x)   ((x + 31) / 32)

#ifdef PRECOMPILED_BN128
#include "precompiles_ec.c"
// EIP-197 BN128 pairing precompile
#include "precompiles_ec_pairing.c"
#endif
// BLS12-381 (EIP-2537) precompiles
#include "precompiles_bls.c"
// EIP-4844 point evaluation precompile
#ifdef PRECOMPILED_KZG
#include "precompiles_kzg.c"
#endif
// EIP-152 Blake2f precompile
#include "precompiles_blake2.c"

static pre_result_t pre_ecrecover(bytes_t input, buffer_t* output, uint64_t* gas_used) {
  *gas_used = 3000;
  if (input.len != 128) return PRE_SUCCESS;
  bytes_t hash       = bytes_slice(input, 0, 32);
  uint8_t v          = input.data[63];
  uint8_t sig[65]    = {0};
  uint8_t pubkey[64] = {0};
  memcpy(sig, input.data + 64, 64); // copy r s
  sig[64] = v > 28 ? (v % 2 ? 27 : 28) : v;

  if (!secp256k1_recover(hash.data, bytes(sig, 65), pubkey)) return PRE_SUCCESS;

  keccak(bytes(pubkey, 64), sig);
  memset(sig, 0, 12);
  buffer_reset(output);
  buffer_append(output, bytes(sig, 32));
  return PRE_SUCCESS;
}

static pre_result_t pre_sha256(bytes_t input, buffer_t* output, uint64_t* gas_used) {
  buffer_reset(output);
  buffer_append(output, bytes(NULL, 32));
  sha256(input, output->data.data);
  *gas_used = 60 + 12 * data_word_size(input.len);
  return PRE_SUCCESS;
}
#ifdef PRECOMPILED_RIPEMD160
static pre_result_t pre_ripemd160(bytes_t input, buffer_t* output, uint64_t* gas_used) {
  buffer_reset(output);
  buffer_append(output, bytes(NULL, 20));
  ripemd160(input.data, input.len, output->data.data);
  *gas_used = 600 + 120 * data_word_size(input.len);
  return PRE_SUCCESS;
}
#endif
static pre_result_t pre_identity(bytes_t input, buffer_t* output, uint64_t* gas_used) {
  buffer_reset(output);
  buffer_append(output, input);
  *gas_used = 15 + 3 * data_word_size(input.len);
  return PRE_SUCCESS;
}

/**
 * EIP-7951 P256VERIFY at address 0x0000…0100.
 *
 * Input (exactly 160 bytes): `hash(32) || r(32) || s(32) || qx(32) || qy(32)`.
 * Gas is a fixed 6900 charged unconditionally (also on malformed input).
 * On success the precompile returns 32 bytes `0x00…01`; on any failure
 * (wrong length, signature mismatch, off-curve key, out-of-range r/s) it
 * returns empty output but never reverts, matching the EIP's no-revert
 * semantics.
 *
 * Verification uses Trezor's `ecdsa_verify_digest(&nist256p1, …)`, which
 * validates the public key (on-curve, not identity), checks `r, s ∈ [1,n-1]`,
 * rejects `R == ∞`, and compares `r ≡ R.x (mod n)`.
 */
static pre_result_t pre_p256verify(bytes_t input, buffer_t* output, uint64_t* gas_used) {
  *gas_used = 6900;
  buffer_reset(output);
  if (input.len != 160) return PRE_SUCCESS;

  uint8_t pub[65];
  pub[0] = 0x04;
  memcpy(pub + 1, input.data + 96, 64); // qx || qy

  const uint8_t* sig    = input.data + 32;  // r || s
  const uint8_t* digest = input.data;       // 32-byte message digest

  if (ecdsa_verify_digest(&nist256p1, pub, sig, digest) != 0) return PRE_SUCCESS;

  static const uint8_t ok_out[32] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
  buffer_append(output, bytes(ok_out, 32));
  return PRE_SUCCESS;
}

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#ifdef INTX

static uint64_t calculate_gas_for_modexp(uint32_t l_base, uint32_t l_exp, uint32_t l_mod, bytes_t b_exp) {

  uint64_t max_len = l_base > l_mod ? l_base : l_mod;
  uint32_t words   = (max_len + 7) / 8;

  // EIP-7883: minimum complexity 16; for inputs > 32 bytes use 2 * words^2
  uint64_t multiplication_complexity = 16;
  if (max_len > 32) multiplication_complexity = 2ULL * words * words;

  uint32_t iteration_count = 0;

  if (l_exp <= 32 && bytes_all_zero(b_exp)) {
    iteration_count = 0;
  }
  else if (l_exp <= 32) {
    uint32_t bit_length = 0;
    for (int i = 0; i < (int) b_exp.len; i++) {
      if (b_exp.data[i] != 0) {
        uint8_t byte = b_exp.data[i];
        for (int j = 7; j >= 0; j--) {
          if ((byte >> j) & 1) {
            bit_length = i * 8 + j + 1;
            break;
          }
        }
        if (bit_length > 0) break;
      }
    }
    iteration_count = bit_length > 0 ? bit_length - 1 : 0;
  }
  else {
    // EIP-7883: multiplier changed from 8 to 16
    uint32_t base_count = 16 * (l_exp - 32);

    uint32_t bit_length     = 0;
    uint32_t bytes_to_check = l_exp > 64 ? 32 : l_exp - 32;
    for (int i = 0; i < (int) bytes_to_check; i++) {
      if (b_exp.data[i] != 0) {
        uint8_t byte = b_exp.data[i];
        for (int j = 7; j >= 0; j--) {
          if ((byte >> j) & 1) {
            bit_length = i * 8 + j + 1;
            break;
          }
        }
        if (bit_length > 0) break;
      }
    }

    iteration_count = base_count + (bit_length > 0 ? bit_length - 1 : 0);
  }

  uint32_t final_iteration_count = iteration_count > 0 ? iteration_count : 1;

  // EIP-7883: division by 3 removed, minimum raised from 200 to 500
  uint64_t dynamic_gas = multiplication_complexity * final_iteration_count;
  if (dynamic_gas < 500) dynamic_gas = 500;

  return dynamic_gas;
}

static pre_result_t pre_modexp(bytes_t input, buffer_t* output, uint64_t* gas_used) {
  uint32_t l_base = (uint32_t) bytes_as_be(bytes_slice(input, 24, 8));
  uint32_t l_exp  = (uint32_t) bytes_as_be(bytes_slice(input, 32 + 24, 8));
  uint32_t l_mod  = (uint32_t) bytes_as_be(bytes_slice(input, 64 + 24, 8));

  // EIP-7823: each input length must not exceed 1024 bytes
  if (l_base > 1024 || l_exp > 1024 || l_mod > 1024) return PRE_INVALID_INPUT;

  if (input.len < 96 + l_base + l_exp + l_mod) return PRE_INVALID_INPUT;

  bytes_t b_base = bytes(input.data + 96, l_base);
  bytes_t b_exp  = bytes(input.data + 96 + l_base, l_exp);
  bytes_t b_mod  = bytes(input.data + 96 + l_base + l_exp, l_mod);

  *gas_used = calculate_gas_for_modexp(l_base, l_exp, l_mod, b_exp);

  // Initialize intx variables
  intx_uint256_t base, exp, mod, result;
  intx_init(&result);

  // Convert input bytes to intx
  intx_from_bytes(&base, b_base);
  intx_from_bytes(&exp, b_exp);
  intx_from_bytes(&mod, b_mod);

  // Perform modular exponentiation
  intx_modexp(&result, &base, &exp, &mod);

  // Find first non-zero byte
  int start_idx = 0;
  while (start_idx < 32 && result.bytes[start_idx] == 0) {
    start_idx++;
  }

  // If all zeros, output a single zero byte
  size_t result_len = (start_idx == 32) ? 1 : 32 - start_idx;

  // Set output buffer
  output->data.len = 0;
  buffer_grow(output, result_len);
  buffer_append(output, bytes(result.bytes + ((start_idx == 32) ? 31 : start_idx), result_len));

  return PRE_SUCCESS;
}
#endif

const precompile_func_t precompile_fn[] = {
    pre_ecrecover, // 0x01
    pre_sha256,    // 0x02
#ifdef PRECOMPILED_RIPEMD160
    pre_ripemd160, // 0x03
#else
    NULL, // 0x03
#endif
    pre_identity, // 0x04
#if defined(INTX) && defined(PRECOMPILED_BN128)
    pre_modexp,     // 0x05
    pre_ec_add,     // 0x06
    pre_ec_mul,     // 0x07
    pre_ec_pairing, // 0x08
#else
    NULL, // 0x05
    NULL, // 0x06
    NULL, // 0x07
    NULL, // 0x08
#endif
    pre_blake2f, // 0x09
#ifdef PRECOMPILED_KZG
    pre_point_evaluation, // 0x0a
#else
    NULL, // 0x0a
#endif
    // 0x0b - 0x11 BLS12-381 (EIP-2537)
    pre_bls12_g1add,         // 0x0b
    pre_bls12_g1msm,         // 0x0c
    pre_bls12_g2add,         // 0x0d
    pre_bls12_g2msm,         // 0x0e
    pre_bls12_pairing_check, // 0x0f
    pre_bls12_map_fp_to_g1,  // 0x10
    pre_bls12_map_fp2_to_g2, // 0x11
};

pre_result_t eth_execute_precompile(const uint8_t* address, const bytes_t input, buffer_t* output, uint64_t* gas_used) {
  if (!bytes_all_zero(bytes(address, 18))) return PRE_INVALID_ADDRESS;
  if (address[18] == 0x00) {
    if (address[19] == 0 || address[19] > PRECOMPILE_FN_COUNT) return PRE_INVALID_ADDRESS;
    precompile_func_t fn = precompile_fn[address[19] - 1];
    if (fn == NULL) return PRE_NOT_SUPPORTED;
    return fn(input, output, gas_used);
  }
  if (address[18] == 0x01 && address[19] == 0x00) return pre_p256verify(input, output, gas_used);
  return PRE_INVALID_ADDRESS;
}
