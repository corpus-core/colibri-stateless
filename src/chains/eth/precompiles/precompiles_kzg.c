/*
 * Copyright (c) 2025 corpus.core
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PRECOMPILES_KZG_C
#define PRECOMPILES_KZG_C

#include "blst.h"
#include "bytes.h"
#include "precompiles.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// EIP-4844 point evaluation precompile (address 0x0a)
// Input (192 bytes):
// [0:32]   versioned_hash
// [32:64]  x (Fr, big-endian)
// [64:96]  y (Fr, big-endian)
// [96:144] commitment (G1, 48-byte compressed)
// [144:192] proof (G1, 48-byte compressed)
//
// Output (64 bytes on success):
// [0:32]   FIELD_ELEMENTS_PER_BLOB (4096) big-endian
// [32:64]  BLS_MODULUS big-endian
//
// Gas: 50000

static const uint8_t BLS_MODULUS_BE[32] = {
    // 0x73eda753299d7d483339d80809a1d80553bda402fffe5bfeffffffff00000001 (Fr)
    0x73, 0xed, 0xa7, 0x53, 0x29, 0x9d, 0x7d, 0x48, 0x33, 0x39, 0xd8, 0x08, 0x09, 0xa1, 0xd8, 0x05,
    0x53, 0xbd, 0xa4, 0x02, 0xff, 0xfe, 0x5b, 0xfe, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x01};

static inline bool be32_is_canonical_fr(const uint8_t be[32]) {
  // Return true iff be < BLS_MODULUS_BE (big-endian compare)
  for (int i = 0; i < 32; i++) {
    if (be[i] < BLS_MODULUS_BE[i]) return true;
    if (be[i] > BLS_MODULUS_BE[i]) return false;
  }
  return false; // equal to modulus is non-canonical
}

// Lazy-loaded G2^tau (monomial, power 1) from trusted setup if available.
// For portability, we support loading from environment CKZG_TRUSTED_SETUP or default path.
static bool           g2_tau_loaded = false;
static blst_p2_affine g2_tau_affine;

// Optional embedded trusted-setup support:
// If ETH_PRECOMPILE_EMBED is defined at build time, we include a generated
// header that provides the 96-byte compressed G2^tau point as:
//   static const unsigned char KZG_G2_TAU_COMPRESSED[96];
#ifdef ETH_PRECOMPILE_EMBED
#include "trusted_setup_embed.h"
static bool load_g2_tau_from_embed(void) {
  BLST_ERROR e = blst_p2_uncompress(&g2_tau_affine, KZG_G2_TAU_COMPRESSED);
  if (e != BLST_SUCCESS) return false;
  if (!blst_p2_affine_on_curve(&g2_tau_affine) || !blst_p2_affine_in_g2(&g2_tau_affine)) return false;
  return true;
}
#endif

static bool hex_to_bytes_stack(const char* hex, uint8_t* out, size_t out_len) {
  size_t n = strlen(hex);
  if (n != out_len * 2) return false;
  int wrote = hex_to_bytes(hex, (int) n, bytes(out, (uint32_t) out_len));
  return wrote == (int) out_len;
}

static bool load_g2_tau_from_file(const char* path) {
  // File format (trusted_setup.txt):
  // line 1: NUM_G1 (decimal)
  // line 2: NUM_G2 (decimal)
  // next NUM_G1 lines: G1 points (48-byte compressed hex)
  // next NUM_G2 lines: G2 points (96-byte compressed hex), where index 0 is G2, index 1 is G2^tau
  FILE* f = fopen(path, "r");
  if (!f) return false;
  char line[256];
  // read NUM_G1
  if (!fgets(line, sizeof(line), f)) {
    fclose(f);
    return false;
  }
  long num_g1 = strtol(line, NULL, 10);
  if (num_g1 <= 0) {
    fclose(f);
    return false;
  }
  // read NUM_G2
  if (!fgets(line, sizeof(line), f)) {
    fclose(f);
    return false;
  }
  long num_g2 = strtol(line, NULL, 10);
  if (num_g2 < 2) {
    fclose(f);
    return false;
  }
  // skip G1 lines
  for (long i = 0; i < num_g1; i++) {
    if (!fgets(line, sizeof(line), f)) {
      fclose(f);
      return false;
    }
  }
  // read G2[0] (generator)
  if (!fgets(line, sizeof(line), f)) {
    fclose(f);
    return false;
  }
  // read G2[1] (tau^1)
  if (!fgets(line, sizeof(line), f)) {
    fclose(f);
    return false;
  }
  // strip newline
  size_t len = strlen(line);
  while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) { line[--len] = 0; }
  uint8_t comp[96];
  if (!hex_to_bytes_stack(line, comp, sizeof(comp))) {
    fclose(f);
    return false;
  }
  fclose(f);
  BLST_ERROR e = blst_p2_uncompress(&g2_tau_affine, comp);
  if (e != BLST_SUCCESS) return false;
  if (!blst_p2_affine_on_curve(&g2_tau_affine) || !blst_p2_affine_in_g2(&g2_tau_affine)) return false;
  return true;
}

static bool ensure_g2_tau_loaded(void) {
  if (g2_tau_loaded) return true;
#ifdef ETH_PRECOMPILE_EMBED
  if (load_g2_tau_from_embed()) {
    g2_tau_loaded = true;
    return true;
  }
#endif
  const char* path = getenv("CKZG_TRUSTED_SETUP");
  if (path == NULL) path = "build/c-kzg-4844/src/trusted_setup.txt";
  if (!load_g2_tau_from_file(path)) return false;
  g2_tau_loaded = true;
  return true;
}

// Public setter to inject the compressed G2^tau point at runtime (e.g., in WASM).
// This allows lazy, on-demand fetching of the trusted setup bytes from the host.
// @param comp96: 96-byte compressed G2^tau
// @return true on success, false on invalid encoding or group mismatch
bool precompiles_kzg_set_trusted_setup_g2_tau(const uint8_t* comp96) {
  if (!comp96) return false;
  BLST_ERROR e = blst_p2_uncompress(&g2_tau_affine, comp96);
  if (e != BLST_SUCCESS) return false;
  if (!blst_p2_affine_on_curve(&g2_tau_affine) || !blst_p2_affine_in_g2(&g2_tau_affine)) return false;
  g2_tau_loaded = true;
  return true;
}

static inline void be_write_u32(uint8_t out[32], uint32_t v) {
  memset(out, 0, 32);
  out[28] = (uint8_t) ((v >> 24) & 0xff);
  out[29] = (uint8_t) ((v >> 16) & 0xff);
  out[30] = (uint8_t) ((v >> 8) & 0xff);
  out[31] = (uint8_t) (v & 0xff);
}

static pre_result_t pre_point_evaluation(bytes_t input, buffer_t* output, uint64_t* gas_used) {
  *gas_used = 50000;
  if (input.len != 192) return PRE_INVALID_INPUT;

  bytes_t vhash      = bytes_slice(input, 0, 32);
  bytes_t x_be       = bytes_slice(input, 32, 32);
  bytes_t y_be       = bytes_slice(input, 64, 32);
  bytes_t commitment = bytes_slice(input, 96, 48);
  bytes_t proof      = bytes_slice(input, 144, 48);

  // versioned_hash[0] must be 0x01
  if (vhash.len != 32 || vhash.data[0] != 0x01) return PRE_INVALID_INPUT;

  // Canonical x,y
  if (!be32_is_canonical_fr(x_be.data) || !be32_is_canonical_fr(y_be.data)) return PRE_INVALID_INPUT;

  // Check versioned hash matches commitment
  uint8_t chash[32] = {0};
  sha256(commitment, chash);
  if (memcmp(&vhash.data[1], &chash[1], 31) != 0) return PRE_INVALID_INPUT;

  // Load G2^tau
  if (!ensure_g2_tau_loaded()) return PRE_INVALID_INPUT;

  // Decode commitment and proof (G1 compressed)
  blst_p1_affine C_aff, W_aff;
  BLST_ERROR     e1 = blst_p1_uncompress(&C_aff, commitment.data);
  BLST_ERROR     e2 = blst_p1_uncompress(&W_aff, proof.data);
  if (e1 != BLST_SUCCESS || e2 != BLST_SUCCESS) return PRE_INVALID_INPUT;
  if (!blst_p1_affine_on_curve(&C_aff) || !blst_p1_affine_in_g1(&C_aff)) return PRE_INVALID_INPUT;
  if (!blst_p1_affine_on_curve(&W_aff) || !blst_p1_affine_in_g1(&W_aff)) return PRE_INVALID_INPUT;

  // Compute A = C - y*G1
  blst_p1 A, tmp1;
  blst_p1_from_affine(&A, &C_aff);
  blst_p1_mult(&tmp1, blst_p1_generator(), y_be.data, 256);
  blst_p1_cneg(&tmp1, true); // -y*G1
  blst_p1_add(&A, &A, &tmp1);
  blst_p1_affine A_aff;
  blst_p1_to_affine(&A_aff, &A);

  // Compute Q = x*G2 - G2^tau
  blst_p2 Q, tmp2;
  blst_p2_mult(&Q, blst_p2_generator(), x_be.data, 256);
  blst_p2_from_affine(&tmp2, &g2_tau_affine);
  blst_p2_cneg(&tmp2, true); // -G2^tau
  blst_p2_add(&Q, &Q, &tmp2);
  blst_p2_affine Q_aff;
  blst_p2_to_affine(&Q_aff, &Q);

  // Pairings: e(A, G2) * e(W, Q) == 1
  blst_fp12             f     = {0};
  const blst_p2_affine* Qs[2] = {blst_p2_affine_generator(), &Q_aff};
  const blst_p1_affine* Ps[2] = {&A_aff, &W_aff};
  blst_miller_loop_n(&f, Qs, Ps, 2);
  blst_final_exp(&f, &f);
  if (!blst_fp12_is_one(&f)) return PRE_INVALID_INPUT;

  // Success: return constants
  buffer_reset(output);
  buffer_grow(output, 64);
  output->data.len = 64;
  be_write_u32(output->data.data, 4096);
  memcpy(output->data.data + 32, BLS_MODULUS_BE, 32);
  return PRE_SUCCESS;
}

#endif
