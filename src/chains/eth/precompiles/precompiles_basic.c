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
#include "json.h"
#include "precompiles.h"

#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>

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
 * EIP-7951 P256VERIFY at address 0x0000…0100 (fixed gas 6900; invalid input → empty return).
 * Verification follows OpenSSL `pkeyutl -verify -rawin` for EC keys: `EVP_DigestVerify*` with a
 * NULL digest type so the 32-byte input is used as the ECDSA message representative (no extra
 * hash).
 */
static bool p256_digest_verify_openssl(const uint8_t digest[32], const uint8_t sig_r[32],
                                       const uint8_t sig_s[32], const uint8_t pub_x[32],
                                       const uint8_t pub_y[32]) {
  bool            ok = false;
  unsigned char   pub_uncomp[65];
  OSSL_PARAM_BLD* bld      = NULL;
  OSSL_PARAM*     params   = NULL;
  EVP_PKEY_CTX*   from_ctx = NULL;
  EVP_PKEY*       pkey     = NULL;
  ECDSA_SIG*      esig     = NULL;
  BIGNUM*         rr       = NULL;
  BIGNUM*         ss       = NULL;
  unsigned char   der_sig[144];
  unsigned char*  der_ptr = der_sig;
  int             der_len = 0;
  EVP_MD_CTX*     md_ctx  = NULL;
  EVP_PKEY_CTX*   op_ctx  = NULL;
  int             vr      = 0;

  pub_uncomp[0] = 0x04;
  memcpy(pub_uncomp + 1, pub_x, 32);
  memcpy(pub_uncomp + 33, pub_y, 32);

  bld = OSSL_PARAM_BLD_new();
  if (!bld) goto cleanup;
  if (!OSSL_PARAM_BLD_push_utf8_string(bld, OSSL_PKEY_PARAM_GROUP_NAME, "prime256v1", 0)) {
    goto cleanup;
  }
  if (!OSSL_PARAM_BLD_push_octet_string(bld, OSSL_PKEY_PARAM_PUB_KEY, pub_uncomp,
                                        sizeof(pub_uncomp))) {
    goto cleanup;
  }
  params = OSSL_PARAM_BLD_to_param(bld);
  OSSL_PARAM_BLD_free(bld);
  bld = NULL;

  from_ctx = EVP_PKEY_CTX_new_from_name(NULL, "EC", NULL);
  if (!from_ctx || EVP_PKEY_fromdata_init(from_ctx) <= 0) goto cleanup;
  if (EVP_PKEY_fromdata(from_ctx, &pkey, EVP_PKEY_PUBLIC_KEY, params) <= 0) goto cleanup;

  esig = ECDSA_SIG_new();
  rr   = BN_bin2bn(sig_r, 32, NULL);
  ss   = BN_bin2bn(sig_s, 32, NULL);
  if (!esig || !rr || !ss || !ECDSA_SIG_set0(esig, rr, ss)) goto cleanup;
  rr = NULL;
  ss = NULL;

  der_len = i2d_ECDSA_SIG(esig, NULL);
  if (der_len <= 0 || der_len > (int) sizeof(der_sig)) goto cleanup;
  der_ptr = der_sig;
  if (i2d_ECDSA_SIG(esig, &der_ptr) != der_len) goto cleanup;

  md_ctx = EVP_MD_CTX_new();
  op_ctx = EVP_PKEY_CTX_new_from_pkey(NULL, pkey, NULL);
  if (!md_ctx || !op_ctx) goto cleanup;
  EVP_MD_CTX_set_pkey_ctx(md_ctx, op_ctx);
  if (!EVP_DigestVerifyInit_ex(md_ctx, NULL, NULL, NULL, NULL, pkey, NULL)) goto cleanup;
  vr = EVP_DigestVerify(md_ctx, der_sig, (size_t) der_len, digest, 32);
  ok = vr == 1;

cleanup:
  BN_clear_free(rr);
  BN_clear_free(ss);
  ECDSA_SIG_free(esig);
  EVP_MD_CTX_free(md_ctx);
  EVP_PKEY_CTX_free(from_ctx);
  EVP_PKEY_free(pkey);
  OSSL_PARAM_free(params);
  OSSL_PARAM_BLD_free(bld);
  return ok;
}

static pre_result_t pre_p256verify(bytes_t input, buffer_t* output, uint64_t* gas_used) {
  *gas_used = 6900;
  buffer_reset(output);
  if (input.len != 160) return PRE_SUCCESS;

  if (!p256_digest_verify_openssl(input.data, input.data + 32, input.data + 64, input.data + 96,
                                  input.data + 128)) {
    return PRE_SUCCESS;
  }

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
