/*
 * Copyright (c) 2025,2026 corpus.core GmbH
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

#include "crypto.h"
#include "blst.h"
#include "bytes.h"
#include "logger.h"
#include "plugin.h"
#include "secp256k1.h"
#include "sha2.h"
#include "sha3.h"
#include <stdlib.h> // For malloc and free
#include <string.h>

// Portable alignment helpers (MSVC doesn't support GCC's __attribute__ syntax).
#if defined(_MSC_VER)
#define C4_ALIGNED16_PREFIX __declspec(align(16))
#define C4_ALIGNED16_SUFFIX
#else
#define C4_ALIGNED16_PREFIX
#define C4_ALIGNED16_SUFFIX __attribute__((aligned(16)))
#endif

// ECDSA recovery id adjustment constant
// Ethereum uses recovery ids 27/28, but the underlying library expects 0/1
#define ECDSA_RECOVERY_ID_OFFSET 27

// ---- SHA-256 with CPU instructions ------------------------------------------
// SSZ merkleization hashes thousands of 64-byte node pairs per beacon block, which makes sha256 the
// dominant cost when building proofs for uncached blocks. If the CPU offers SHA instructions (x86 SHA-NI,
// ARMv8 SHA2) we use them for the one-shot functions sha256() and sha256_merkle(); the support is
// detected once at runtime, so the same binary still runs on CPUs without them (and on WASM / embedded
// builds, where this block is not compiled at all) using the portable implementation in libs/crypto.
// Define C4_SHA256_NO_HW to disable it at compile time.
#if !defined(C4_SHA256_NO_HW) && defined(__GNUC__) && defined(__x86_64__)
#define C4_SHA256_HW        1
#define C4_SHA256_HW_TARGET __attribute__((target("sha,sse4.1,ssse3")))
#include <cpuid.h>
#include <immintrin.h>
#elif !defined(C4_SHA256_NO_HW) && defined(__GNUC__) && defined(__aarch64__) && defined(__ARM_FEATURE_SHA2)
#define C4_SHA256_HW 1 // enabled for the whole build (e.g. all Apple Silicon targets), no runtime check needed
#define C4_SHA256_HW_TARGET
#include <arm_neon.h>
#elif !defined(C4_SHA256_NO_HW) && defined(__GNUC__) && !defined(__clang__) && defined(__aarch64__) && defined(__linux__)
#define C4_SHA256_HW        1
#define C4_SHA256_HW_TARGET __attribute__((target("+crypto"))) // "+sha2" alone is rejected by the arm_neon.h of older GCC versions
#define C4_SHA256_HW_AUXV   1
#include <arm_neon.h>
#include <sys/auxv.h>
#ifndef HWCAP_SHA2
#define HWCAP_SHA2 (1 << 6)
#endif
#endif

#ifdef C4_SHA256_HW
static const uint32_t C4_SHA256_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

// -1 = not checked yet, 0 = not available, 1 = available.
// Written without a lock: concurrent first calls all detect and store the same value.
static int c4_sha256_hw_state = -1;

static int c4_sha256_hw_detect(void) {
#if defined(__x86_64__)
  unsigned int a = 0, b = 0, c = 0, d = 0;
  if (!__get_cpuid(1, &a, &b, &c, &d)) return 0;
  if (!(c & (1u << 9)) || !(c & (1u << 19))) return 0; // SSSE3 + SSE4.1
  if (!__get_cpuid_count(7, 0, &a, &b, &c, &d)) return 0;
  return (b & (1u << 29)) ? 1 : 0; // SHA
#elif defined(C4_SHA256_HW_AUXV)
  return (getauxval(AT_HWCAP) & HWCAP_SHA2) ? 1 : 0;
#else
  return 1;
#endif
}

// Runs the sha256 compression function over `blocks` consecutive 64-byte blocks.
C4_SHA256_HW_TARGET
static void c4_sha256_hw_compress(uint32_t state[8], const uint8_t* data, size_t blocks) {
#if defined(__x86_64__)
  const __m128i be_mask = _mm_set_epi64x(0x0c0d0e0f08090a0bULL, 0x0405060700010203ULL);
  // the SHA instructions expect the state as ABEF / CDGH
  __m128i tmp    = _mm_shuffle_epi32(_mm_loadu_si128((const __m128i*) &state[0]), 0xB1); // CDAB
  __m128i state1 = _mm_shuffle_epi32(_mm_loadu_si128((const __m128i*) &state[4]), 0x1B); // EFGH
  __m128i state0 = _mm_alignr_epi8(tmp, state1, 8);                                      // ABEF
  state1         = _mm_blend_epi16(state1, tmp, 0xF0);                                   // CDGH

  for (; blocks; blocks--, data += 64) {
    __m128i save0 = state0, save1 = state1, m[4];
    for (int j = 0; j < 4; j++) m[j] = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*) (data + 16 * j)), be_mask);

#pragma GCC unroll 16
    for (int i = 0; i < 16; i++) { // 16 groups of 4 rounds; m[i & 3] holds W[4i..4i+3]
      __m128i wk = _mm_add_epi32(m[i & 3], _mm_loadu_si128((const __m128i*) &C4_SHA256_K[4 * i]));
      state1     = _mm_sha256rnds2_epu32(state1, state0, wk);
      state0     = _mm_sha256rnds2_epu32(state0, state1, _mm_shuffle_epi32(wk, 0x0E));
      if (i >= 3 && i < 15) { // message schedule for the next group, replacing the oldest 4 words
        int     n = (i + 1) & 3;
        __m128i t = _mm_add_epi32(_mm_sha256msg1_epu32(m[n], m[(n + 1) & 3]), _mm_alignr_epi8(m[(n + 3) & 3], m[(n + 2) & 3], 4));
        m[n]      = _mm_sha256msg2_epu32(t, m[(n + 3) & 3]);
      }
    }
    state0 = _mm_add_epi32(state0, save0);
    state1 = _mm_add_epi32(state1, save1);
  }

  tmp    = _mm_shuffle_epi32(state0, 0x1B);    // FEBA
  state1 = _mm_shuffle_epi32(state1, 0xB1);    // DCHG
  state0 = _mm_blend_epi16(tmp, state1, 0xF0); // DCBA
  state1 = _mm_alignr_epi8(state1, tmp, 8);    // HGFE
  _mm_storeu_si128((__m128i*) &state[0], state0);
  _mm_storeu_si128((__m128i*) &state[4], state1);
#else
  uint32x4_t state0 = vld1q_u32(&state[0]); // ABCD
  uint32x4_t state1 = vld1q_u32(&state[4]); // EFGH

  for (; blocks; blocks--, data += 64) {
    uint32x4_t save0 = state0, save1 = state1, m[4];
    for (int j = 0; j < 4; j++) m[j] = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(data + 16 * j)));

#pragma GCC unroll 16
    for (int i = 0; i < 16; i++) { // 16 groups of 4 rounds; m[i & 3] holds W[4i..4i+3]
      uint32x4_t wk  = vaddq_u32(m[i & 3], vld1q_u32(&C4_SHA256_K[4 * i]));
      uint32x4_t tmp = state0;
      state0         = vsha256hq_u32(state0, state1, wk);
      state1         = vsha256h2q_u32(state1, tmp, wk);
      if (i >= 3 && i < 15) { // message schedule for the next group, replacing the oldest 4 words
        int n = (i + 1) & 3;
        m[n]  = vsha256su1q_u32(vsha256su0q_u32(m[n], m[(n + 1) & 3]), m[(n + 2) & 3], m[(n + 3) & 3]);
      }
    }
    state0 = vaddq_u32(state0, save0);
    state1 = vaddq_u32(state1, save1);
  }
  vst1q_u32(&state[0], state0);
  vst1q_u32(&state[4], state1);
#endif
}

// One-shot sha256 over (d1 || d2). Full blocks are hashed directly from the input, only the
// bytes around the seam between d1 and d2 and the padding go through the local block buffer.
static void c4_sha256_hw(const uint8_t* d1, size_t l1, const uint8_t* d2, size_t l2, uint8_t* out) {
  uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  uint8_t  block[128];
  uint64_t bits = ((uint64_t) l1 + (uint64_t) l2) * 8;
  size_t   full = l1 / 64;
  size_t   fill = l1 - full * 64; // bytes waiting in block
  if (full) c4_sha256_hw_compress(state, d1, full);
  if (fill) memcpy(block, d1 + full * 64, fill);

  if (fill && l2) { // complete the block started by d1 with the first bytes of d2
    size_t take = 64 - fill < l2 ? 64 - fill : l2;
    memcpy(block + fill, d2, take);
    fill += take;
    d2 += take;
    l2 -= take;
    if (fill == 64) {
      c4_sha256_hw_compress(state, block, 1);
      fill = 0;
    }
  }
  if (!fill && l2) { // (if fill is still set here, d2 is already consumed)
    full = l2 / 64;
    fill = l2 - full * 64;
    if (full) c4_sha256_hw_compress(state, d2, full);
    if (fill) memcpy(block, d2 + full * 64, fill);
  }

  // padding: 0x80, zeros and the message length in bits (big endian) in the last 8 bytes
  size_t total  = fill < 56 ? 64 : 128;
  block[fill++] = 0x80;
  memset(block + fill, 0, total - 8 - fill);
  for (int i = 0; i < 8; i++) block[total - 1 - i] = (uint8_t) (bits >> (8 * i));
  c4_sha256_hw_compress(state, block, total / 64);

  for (int i = 0; i < 8; i++) {
    out[4 * i]     = (uint8_t) (state[i] >> 24);
    out[4 * i + 1] = (uint8_t) (state[i] >> 16);
    out[4 * i + 2] = (uint8_t) (state[i] >> 8);
    out[4 * i + 3] = (uint8_t) state[i];
  }
}
#endif // C4_SHA256_HW

void sha256(bytes_t data, uint8_t* out) {
#ifdef C4_SHA256_HW
  if (c4_sha256_hw_state < 0) c4_sha256_hw_state = c4_sha256_hw_detect();
  if (c4_sha256_hw_state) {
    c4_sha256_hw(data.data, data.len, NULL, 0, out);
    return;
  }
#endif
  SHA256_CTX ctx;
  sha256_Init(&ctx);
  sha256_Update(&ctx, data.data, data.len);
  sha256_Final(&ctx, out);
}

void keccak(bytes_t data, uint8_t* out) {
  SHA3_CTX ctx;
  sha3_256_Init(&ctx);
  sha3_Update(&ctx, data.data, data.len);
  keccak_Final(&ctx, out);
}

void sha256_merkle(bytes_t data1, bytes_t data2, uint8_t* out) {
#ifdef C4_SHA256_HW
  if (c4_sha256_hw_state < 0) c4_sha256_hw_state = c4_sha256_hw_detect();
  if (c4_sha256_hw_state) {
    c4_sha256_hw(data1.data, data1.len, data2.data, data2.len, out); // out may alias data1/data2: it is written only after all input was consumed
    return;
  }
#endif
  SHA256_CTX ctx;
  sha256_Init(&ctx);
  sha256_Update(&ctx, data1.data, data1.len);
  sha256_Update(&ctx, data2.data, data2.len);
  sha256_Final(&ctx, out);
}

// BLS signature domain separation tag for Ethereum 2.0
static const uint8_t blst_dst[]   = "BLS_SIG_BLS12381G2_XMD:SHA-256_SSWU_RO_POP_";
static const size_t  blst_dst_len = sizeof(blst_dst) - 1;

#ifdef BLS_DESERIALIZE
typedef struct {
  const uint8_t*  compressed;
  blst_p1_affine* out;
  volatile int    failed;
} blst_deser_ctx_t;

static void blst_deser_body(int i, void* ctx) {
  blst_deser_ctx_t* c = (blst_deser_ctx_t*) ctx;
  if (!c || c->failed) return;
  if (blst_p1_uncompress(c->out + i, c->compressed + (size_t) i * BLS_PUBKEY_SIZE) != BLST_SUCCESS) {
    c->failed = 1;
  }
}

bytes_t blst_deserialize_p1_affine(uint8_t* compressed_pubkeys, int num_public_keys, uint8_t* out) {
  if (num_public_keys <= 0) return NULL_BYTES;

  blst_p1_affine* pubkeys = out ? (blst_p1_affine*) out : (blst_p1_affine*) safe_malloc(num_public_keys * sizeof(blst_p1_affine));

  c4_parallel_for_fn pf = c4_get_parallel_for();
  if (pf && num_public_keys >= 128) {
    blst_deser_ctx_t c = {.compressed = compressed_pubkeys, .out = pubkeys, .failed = 0};
    pf(0, num_public_keys, blst_deser_body, &c);
    if (c.failed) {
      if (!out) safe_free(pubkeys); // Only free if we allocated
      return NULL_BYTES;
    }
  }
  else {
    for (int i = 0; i < num_public_keys; i++) {
      if (blst_p1_uncompress(pubkeys + i, compressed_pubkeys + i * BLS_PUBKEY_SIZE) != BLST_SUCCESS) {
        if (!out) safe_free(pubkeys); // Only free if we allocated
        return NULL_BYTES;
      }
    }
  }
  return bytes((uint8_t*) pubkeys, num_public_keys * sizeof(blst_p1_affine));
}
#endif

#define MAX_AGGR_CHUNKS 8

typedef struct {
  const uint8_t* public_keys;
  int            num_public_keys;
  bytes_t        pubkeys_used;
  bool           deserialized;
  blst_p1        partial_sums[MAX_AGGR_CHUNKS];
  bool           chunk_has_value[MAX_AGGR_CHUNKS];
  int            num_chunks;
  volatile int   failed;
} blst_aggr_ctx_t;

static void blst_aggregate_body(int chunk_idx, void* ctx_ptr) {
  blst_aggr_ctx_t* ctx = (blst_aggr_ctx_t*) ctx_ptr;
  if (!ctx || ctx->failed) return;

  int start = (ctx->num_public_keys * chunk_idx) / ctx->num_chunks;
  int end   = (ctx->num_public_keys * (chunk_idx + 1)) / ctx->num_chunks;

  blst_p1 sum;
  bool    first = true;

  // We loop through the assigned range of keys
  for (int i = start; i < end; i++) {
    // Check bitmask
    if (ctx->pubkeys_used.data[i / 8] & (1 << (i % 8))) {
      blst_p1_affine pubkey_affine;

      if (ctx->deserialized) {
        pubkey_affine = ((blst_p1_affine*) ctx->public_keys)[i];
      }
      else {
        if (blst_p1_uncompress(&pubkey_affine, ctx->public_keys + i * BLS_PUBKEY_SIZE) != BLST_SUCCESS) {
          ctx->failed = 1;
          return;
        }
      }

      // Aggregate the public key
      // We use blst_p1_add_or_double_affine which is safe for adding a point to itself.
      // blst_p1_add_affine would be slightly faster but undefined if points are equal.
      // Since public keys in the sync committee are unique, blst_p1_add_affine is safe here.
      //
      // Note on blst_p1s_add: We intentionally do not use the multi-scalar addition
      // blst_p1s_add despite it using Pippenger's algorithm (approx 60-85% faster).
      // Reason: It uses alloca() for scratch space up to ~144KB (SCRATCH_LIMIT),
      // which causes stack overflows on ESP32 (typically 4-16KB stack per task).
      if (first) {
        blst_p1_from_affine(&sum, &pubkey_affine);
        first = false;
      }
      else
        blst_p1_add_affine(&sum, &sum, &pubkey_affine);
    }
  }

  ctx->chunk_has_value[chunk_idx] = !first;
  if (!first)
    ctx->partial_sums[chunk_idx] = sum;
}

bool blst_verify(bytes32_t       message_hash,
                 bls_signature_t signature,
                 uint8_t*        public_keys,
                 int             num_public_keys,
                 bytes_t         pubkeys_used,
                 bool            deserialized) {
  // Input validation
  if (num_public_keys <= 0) return false;
  if (pubkeys_used.data == NULL) return false;

  // Validate bitmask length: must be ceil(num_public_keys / 8)
  if (pubkeys_used.len != (num_public_keys + 7) / 8) return false;

  // Step 1: Aggregate the public keys according to the bitmask
  blst_p2_affine sig;
  blst_p1_affine pubkey_aggregated;
  blst_p1        pubkey_sum;
  bool           first_key = true;

  c4_parallel_for_fn pf = c4_get_parallel_for();

// Enable Pippenger optimization for platforms with large stacks (WebAssembly, Android, iOS, Desktop)
// We avoid it on embedded devices (e.g. ESP32) because blst_p1s_add uses ~144KB stack (alloca).
// If C4_SMALL_STACK is defined or EMBEDDED is defined, we also disable it.
#if (defined(__EMSCRIPTEN__) || defined(__ANDROID__) || defined(__APPLE__) || defined(__linux__) || defined(_WIN32)) && !defined(C4_SMALL_STACK) && !defined(EMBEDDED) && !defined(ESP_PLATFORM)
#define C4_USE_PIPPENGER 1
#endif

#if defined(C4_USE_PIPPENGER)
  // Optimization: Use Pippenger's algorithm (blst_p1s_add) for single-threaded speedup (O(N/logN) vs O(N))
  // Note: Requires sufficient stack size (~45-144KB scratch + overhead).
  if (deserialized && num_public_keys > 0) {
    // Create array of pointers to active keys
    // We allocate on heap to be safe regarding pointer array size,
    // although scratch space of blst_p1s_add will still be on stack.
    const blst_p1_affine** p_ptrs = (const blst_p1_affine**) safe_malloc(num_public_keys * sizeof(blst_p1_affine*));
    int                    count  = 0;
    for (int i = 0; i < num_public_keys; i++) {
      if (pubkeys_used.data[i / 8] & (1 << (i % 8)))
        p_ptrs[count++] = &((const blst_p1_affine*) public_keys)[i];
    }

    if (count > 0) {
      blst_p1s_add(&pubkey_sum, p_ptrs, count);
      first_key = false;
    }

    safe_free(p_ptrs);

    // If we successfully used Pippenger, skip the standard loop
    goto skip_standard_aggregation;
  }
#endif

  // Use parallel aggregation if available and worth it (e.g. > 128 keys)
  if (pf && num_public_keys >= 128) {
    blst_aggr_ctx_t ctx;
    ctx.public_keys     = public_keys;
    ctx.num_public_keys = num_public_keys;
    ctx.pubkeys_used    = pubkeys_used;
    ctx.deserialized    = deserialized;
    ctx.num_chunks      = MAX_AGGR_CHUNKS; // Use fixed number of chunks for simplicity
    ctx.failed          = 0;

    // Run parallel aggregation
    pf(0, ctx.num_chunks, blst_aggregate_body, &ctx);

    if (ctx.failed) return false;

    // Merge partial sums
    for (int i = 0; i < ctx.num_chunks; i++) {
      if (ctx.chunk_has_value[i]) {
        if (first_key) {
          pubkey_sum = ctx.partial_sums[i];
          first_key  = false;
        }
        else
          blst_p1_add_or_double(&pubkey_sum, &pubkey_sum, &ctx.partial_sums[i]);
      }
    }
  }
  else {
    // Serial fallback
    blst_aggr_ctx_t ctx;
    ctx.public_keys     = public_keys;
    ctx.num_public_keys = num_public_keys;
    ctx.pubkeys_used    = pubkeys_used;
    ctx.deserialized    = deserialized;
    ctx.num_chunks      = 1;
    ctx.failed          = 0;

    blst_aggregate_body(0, &ctx);

    if (ctx.failed) return false;
    if (ctx.chunk_has_value[0]) {
      pubkey_sum = ctx.partial_sums[0];
      first_key  = false;
    }
  }

#if defined(C4_USE_PIPPENGER)
skip_standard_aggregation:
#endif

  // Ensure at least one public key was aggregated
  if (first_key) return false;

  blst_p1_to_affine(&pubkey_aggregated, &pubkey_sum);

  // Step 2: Deserialize the signature
  // Signature is provided in compressed form (96 bytes). Use uncompress, not deserialize (expects 192 bytes).
  if (blst_p2_uncompress(&sig, signature) != BLST_SUCCESS) return false;

  // Step 3: Perform pairing verification
  // Verify that e(pubkey, H(message)) == e(G1, signature)

  // Use "one-shot" core verify to avoid manual context management and malloc
  // NOTE: blst_core_verify_pk_in_g1 performs subgroup checks (pk_grpchk=true, sig_grpchk=true)
  // which are redundant because we validated the inputs during deserialization (blst_p1/p2_uncompress).
  // However, it avoids manual pairing context management.
  //
  // To optimize further (skip redundant checks), we manually manage the pairing context
  // on the stack (approx 4KB) and disable the group checks.

#define C4_PAIRING_CTX_SIZE (4u * 1024u)
  static C4_ALIGNED16_PREFIX uint8_t pairing_ctx_buf[C4_PAIRING_CTX_SIZE] C4_ALIGNED16_SUFFIX;
  blst_pairing*                      ctx  = (blst_pairing*) (void*) pairing_ctx_buf;
  size_t                             need = blst_pairing_sizeof();

  if (need > sizeof(pairing_ctx_buf)) {
    // Should not happen as sizeof(pairing) is ~3.2KB
    return false;
  }

  // Init pairing context
  blst_pairing_init(ctx, true, blst_dst, blst_dst_len);

  // Aggregate PK and Signature
  // using blst_pairing_chk_n_aggr_pk_in_g1 allows setting group check flags.
  // We pass false for both checks because we already validated them during uncompress.
  BLST_ERROR err = blst_pairing_chk_n_aggr_pk_in_g1(ctx,
                                                    &pubkey_aggregated,
                                                    false, // pk_grpchk = false (optimized)
                                                    &sig,
                                                    false,                      // sig_grpchk = false (optimized)
                                                    message_hash, BYTES32_SIZE, // message
                                                    NULL, 0);                   // aug

  if (err != BLST_SUCCESS)
    return false;

  blst_pairing_commit(ctx);
  return blst_pairing_finalverify(ctx, NULL);
}

bool secp256k1_recover(const bytes32_t digest, bytes_t signature, uint8_t* pubkey) {
  // Input validation
  if (signature.data == NULL) return false;

  // Signature must be exactly 65 bytes (r || s || v)
  if (signature.len != SECP256K1_SIGNATURE_SIZE) return false;

  uint8_t pub[SECP256K1_SIGNATURE_SIZE] = {0};

  // Recover public key from signature
  // The recovery id (v) is adjusted by subtracting the Ethereum offset (27)
  uint8_t recovery_id = signature.data[64];
  if (recovery_id >= ECDSA_RECOVERY_ID_OFFSET) {
    recovery_id -= ECDSA_RECOVERY_ID_OFFSET;
  }

  if (ecdsa_recover_pub_from_sig(&secp256k1, pub, signature.data, digest, recovery_id))
    return false;

  // Copy the uncompressed public key (skip the 0x04 prefix byte)
  memcpy(pubkey, pub + 1, SECP256K1_PUBKEY_SIZE);
  return true;
}

bool secp256k1_sign(const bytes32_t sk, const bytes32_t digest, uint8_t* signature) {
  // Sign the digest
  // signature format: r (32 bytes) || s (32 bytes) || v (1 byte recovery id)
  int result = ecdsa_sign_digest(&secp256k1, sk, digest, signature, signature + 64, NULL);

  // Adjust recovery id to Ethereum format (add 27)
  signature[64] += ECDSA_RECOVERY_ID_OFFSET;

  return result == 0; // 0 indicates success
}
