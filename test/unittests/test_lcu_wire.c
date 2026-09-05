/*
 * Copyright (c) 2026 corpus.core
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

// Unit tests for src/chains/eth/verifier/lcu_wire.{h,c}:
//   - c4_eth_compute_fork_digest (ForkDigest per Beacon-API spec)
//   - c4_eth_fork_from_context   (ForkDigest -> fork_id, with fork_version fallback)
//   - c4_eth_walk_lcu_list       (Beacon-API `light_client/updates` framing)
//   - c4_eth_decode_bootstrap    (LightClientBootstrap size / offset detection)

#include "beacon_types.h"
#include "bytes.h"
#include "c4_assert.h"
#include "chains.h"
#include "lcu_wire.h"
#include "ssz.h"
#include "state.h"
#include "unity.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

// Digest at a fork's activation epoch. After Fulu this is the BPO-mixed
// digest (EIP-7892), not the raw ForkData root.
static bool digest_at_fork(chain_id_t chain, fork_id_t fork, uint8_t out[4]) {
  uint64_t epoch = c4_chain_fork_epoch(chain, fork);
  if (epoch == UINT64_MAX) return false;
  return c4_eth_compute_fork_digest(chain, epoch, out);
}

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

// Writes an LCU wire chunk into `dst` at offset `off`:
//   [8 bytes length = 4 + payload_len][4 bytes context][payload_len bytes payload]
// Returns the byte-position immediately after the chunk (so the caller can
// keep appending).
static uint32_t emit_chunk(uint8_t* dst, uint32_t off, const uint8_t context[4],
                           const uint8_t* payload, uint32_t payload_len) {
  const uint64_t length = 4u + (uint64_t) payload_len;
  uint64_to_le(dst + off, length);
  memcpy(dst + off + 8, context, 4);
  if (payload_len && payload) memcpy(dst + off + 12, payload, payload_len);
  return (uint32_t) (off + 12u + payload_len);
}

typedef struct {
  uint32_t  seen;
  fork_id_t forks[8];
  bool      stop_after_first;
} chunk_counter_t;

static bool count_chunks_cb(void* user, const c4_lcu_chunk_t* chunk) {
  chunk_counter_t* c = (chunk_counter_t*) user;
  if (c->seen < 8) c->forks[c->seen] = chunk->fork;
  c->seen++;
  if (c->stop_after_first) return false;
  return true;
}

// -----------------------------------------------------------------------------
// Fork-digest math
// -----------------------------------------------------------------------------

void test_compute_fork_digest_deterministic(void) {
  // Same (chain, fork) must yield the same digest.
  uint8_t a[4] = {0};
  uint8_t b[4] = {0};
  TEST_ASSERT_TRUE(digest_at_fork(C4_CHAIN_MAINNET, C4_FORK_DENEB, a));
  TEST_ASSERT_TRUE(digest_at_fork(C4_CHAIN_MAINNET, C4_FORK_DENEB, b));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(a, b, 4);
}

void test_compute_fork_digest_differs_per_fork(void) {
  uint8_t d_deneb[4]   = {0};
  uint8_t d_electra[4] = {0};
  uint8_t d_fulu[4]    = {0};
  TEST_ASSERT_TRUE(digest_at_fork(C4_CHAIN_MAINNET, C4_FORK_DENEB, d_deneb));
  TEST_ASSERT_TRUE(digest_at_fork(C4_CHAIN_MAINNET, C4_FORK_ELECTRA, d_electra));
  TEST_ASSERT_TRUE(digest_at_fork(C4_CHAIN_MAINNET, C4_FORK_FULU, d_fulu));
  // Any two distinct forks yield distinct digests -- collisions in the first
  // 4 bytes of hash_tree_root(ForkData) are cryptographically negligible for
  // scheduled Mainnet forks.
  TEST_ASSERT_NOT_EQUAL(0, memcmp(d_deneb, d_electra, 4));
  TEST_ASSERT_NOT_EQUAL(0, memcmp(d_deneb, d_fulu, 4));
  TEST_ASSERT_NOT_EQUAL(0, memcmp(d_electra, d_fulu, 4));
}

void test_compute_fork_digest_bpo_differs_from_fulu_activation(void) {
  // After Fulu, EIP-7892 mixes blob parameters into the digest. BPO2
  // (epoch 419072, 21 blobs) must not equal the digest at Fulu activation
  // (epoch 411392, Electra's 9-blob fallback). Both must still resolve to
  // C4_FORK_FULU -- this is what Lodestar emits on mainnet today.
  uint8_t d_fulu[4] = {0};
  uint8_t d_bpo2[4] = {0};
  TEST_ASSERT_TRUE(c4_eth_compute_fork_digest(C4_CHAIN_MAINNET, 411392ULL, d_fulu));
  TEST_ASSERT_TRUE(c4_eth_compute_fork_digest(C4_CHAIN_MAINNET, 419072ULL, d_bpo2));
  TEST_ASSERT_NOT_EQUAL(0, memcmp(d_fulu, d_bpo2, 4));
  TEST_ASSERT_EQUAL_INT((int) C4_FORK_FULU, (int) c4_eth_fork_from_context(C4_CHAIN_MAINNET, d_fulu));
  TEST_ASSERT_EQUAL_INT((int) C4_FORK_FULU, (int) c4_eth_fork_from_context(C4_CHAIN_MAINNET, d_bpo2));
}

void test_compute_fork_digest_bpo1_and_epoch_boundaries(void) {
  // BPO1 (epoch 412672, 15 blobs) is a distinct schedule entry, not just
  // "somewhere between Fulu and BPO2". An off-by-one in
  // `get_blob_parameters` (`epoch >= entry.epoch`) would collapse BPO1
  // into the 9-blob Fulu fallback or into BPO2.
  uint8_t d_fulu[4]     = {0};
  uint8_t d_pre_bpo1[4] = {0};
  uint8_t d_bpo1[4]     = {0};
  uint8_t d_pre_bpo2[4] = {0};
  uint8_t d_bpo2[4]     = {0};
  TEST_ASSERT_TRUE(c4_eth_compute_fork_digest(C4_CHAIN_MAINNET, 411392ULL, d_fulu));
  TEST_ASSERT_TRUE(c4_eth_compute_fork_digest(C4_CHAIN_MAINNET, 412671ULL, d_pre_bpo1));
  TEST_ASSERT_TRUE(c4_eth_compute_fork_digest(C4_CHAIN_MAINNET, 412672ULL, d_bpo1));
  TEST_ASSERT_TRUE(c4_eth_compute_fork_digest(C4_CHAIN_MAINNET, 419071ULL, d_pre_bpo2));
  TEST_ASSERT_TRUE(c4_eth_compute_fork_digest(C4_CHAIN_MAINNET, 419072ULL, d_bpo2));

  TEST_ASSERT_EQUAL_UINT8_ARRAY(d_fulu, d_pre_bpo1, 4);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(d_bpo1, d_pre_bpo2, 4);
  TEST_ASSERT_NOT_EQUAL(0, memcmp(d_fulu, d_bpo1, 4));
  TEST_ASSERT_NOT_EQUAL(0, memcmp(d_bpo1, d_bpo2, 4));
  TEST_ASSERT_EQUAL_INT((int) C4_FORK_FULU, (int) c4_eth_fork_from_context(C4_CHAIN_MAINNET, d_bpo1));
}

void test_compute_fork_digest_sepolia_bpo(void) {
  // Sepolia has its own CL BLOB_SCHEDULE (BPO1 274176 / BPO2 275712) and a
  // different genesis_validators_root. Copying mainnet BPO epochs here
  // would make 274176 and 275712 share the 9-blob Fulu fallback.
  uint8_t d_fulu[4] = {0};
  uint8_t d_bpo1[4] = {0};
  uint8_t d_bpo2[4] = {0};
  TEST_ASSERT_TRUE(c4_eth_compute_fork_digest(C4_CHAIN_SEPOLIA, 272640ULL, d_fulu));
  TEST_ASSERT_TRUE(c4_eth_compute_fork_digest(C4_CHAIN_SEPOLIA, 274176ULL, d_bpo1));
  TEST_ASSERT_TRUE(c4_eth_compute_fork_digest(C4_CHAIN_SEPOLIA, 275712ULL, d_bpo2));
  TEST_ASSERT_NOT_EQUAL(0, memcmp(d_fulu, d_bpo1, 4));
  TEST_ASSERT_NOT_EQUAL(0, memcmp(d_bpo1, d_bpo2, 4));
  TEST_ASSERT_EQUAL_INT((int) C4_FORK_FULU, (int) c4_eth_fork_from_context(C4_CHAIN_SEPOLIA, d_fulu));
  TEST_ASSERT_EQUAL_INT((int) C4_FORK_FULU, (int) c4_eth_fork_from_context(C4_CHAIN_SEPOLIA, d_bpo1));
  TEST_ASSERT_EQUAL_INT((int) C4_FORK_FULU, (int) c4_eth_fork_from_context(C4_CHAIN_SEPOLIA, d_bpo2));
}

void test_compute_fork_digest_gnosis_empty_cl_blob_schedule(void) {
  // Gnosis schedules Fulu but has no CL BPO table. After Fulu the mixin
  // is the constant Electra fallback (epoch, 9), so the digest must stay
  // stable across later epochs and still resolve to C4_FORK_FULU.
  const chain_spec_t* spec = c4_eth_get_chain_spec(C4_CHAIN_GNOSIS);
  TEST_ASSERT_NOT_NULL(spec);
  TEST_ASSERT_NULL(spec->cl_blob_schedule);

  uint64_t fulu = c4_chain_fork_epoch(C4_CHAIN_GNOSIS, C4_FORK_FULU);
  TEST_ASSERT_NOT_EQUAL(UINT64_MAX, fulu);

  uint8_t d_fulu[4]  = {0};
  uint8_t d_later[4] = {0};
  TEST_ASSERT_TRUE(c4_eth_compute_fork_digest(C4_CHAIN_GNOSIS, fulu, d_fulu));
  TEST_ASSERT_TRUE(c4_eth_compute_fork_digest(C4_CHAIN_GNOSIS, fulu + 10000ULL, d_later));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(d_fulu, d_later, 4);
  TEST_ASSERT_EQUAL_INT((int) C4_FORK_FULU, (int) c4_eth_fork_from_context(C4_CHAIN_GNOSIS, d_fulu));
}

void test_compute_fork_digest_rejects_unknown_chain(void) {
  uint8_t out[4] = {0xff, 0xff, 0xff, 0xff};
  TEST_ASSERT_FALSE(c4_eth_compute_fork_digest(CHAIN(999999), 0, out));
  // On failure `out` must be left untouched.
  TEST_ASSERT_EQUAL_UINT8(0xff, out[0]);
  TEST_ASSERT_EQUAL_UINT8(0xff, out[3]);
}

// -----------------------------------------------------------------------------
// Context -> fork resolution
// -----------------------------------------------------------------------------

void test_fork_from_context_digest_hits(void) {
  for (fork_id_t f = C4_FORK_DENEB; f <= C4_FORK_FULU; f++) {
    uint8_t digest[4] = {0};
    TEST_ASSERT_TRUE(digest_at_fork(C4_CHAIN_MAINNET, f, digest));
    TEST_ASSERT_EQUAL_INT((int) f, (int) c4_eth_fork_from_context(C4_CHAIN_MAINNET, digest));
  }
}

void test_fork_from_context_fork_version_fallback(void) {
  // Compat path: raw `fork_version` bytes must still resolve, so period-store
  // `lcu.ssz` written by older builds keeps parsing.
  const chain_spec_t* spec = c4_eth_get_chain_spec(C4_CHAIN_MAINNET);
  TEST_ASSERT_NOT_NULL(spec);
  uint8_t v[4] = {0};
  spec->fork_version_func(C4_CHAIN_MAINNET, C4_FORK_ELECTRA, v);
  TEST_ASSERT_EQUAL_INT((int) C4_FORK_ELECTRA,
                        (int) c4_eth_fork_from_context(C4_CHAIN_MAINNET, v));
}

void test_fork_from_context_rejects_unknown(void) {
  uint8_t junk[4] = {0xde, 0xad, 0xbe, 0xef};
  TEST_ASSERT_EQUAL_INT((int) C4_FORK_INVALID,
                        (int) c4_eth_fork_from_context(C4_CHAIN_MAINNET, junk));
}

// -----------------------------------------------------------------------------
// LCU list framing (structural walk; ssz_is_valid disabled so payload can be
// arbitrary bytes)
// -----------------------------------------------------------------------------

void test_walk_lcu_list_empty(void) {
  c4_state_t      state = {0};
  data_request_t  req   = {0};
  chunk_counter_t cnt   = {0};
  TEST_ASSERT_TRUE(c4_eth_walk_lcu_list(C4_CHAIN_MAINNET, NULL_BYTES, &state, &req,
                                        /*validate_ssz*/ false, count_chunks_cb, &cnt));
  TEST_ASSERT_EQUAL_UINT32(0, cnt.seen);
  TEST_ASSERT_TRUE(req.validated);
  TEST_ASSERT_NULL(state.error);
}

void test_walk_lcu_list_one_chunk_marks_validated(void) {
  uint8_t digest[4] = {0};
  TEST_ASSERT_TRUE(digest_at_fork(C4_CHAIN_MAINNET, C4_FORK_ELECTRA, digest));

  uint8_t buf[64] = {0};
  uint8_t payload[16];
  memset(payload, 0xab, sizeof(payload));
  uint32_t end = emit_chunk(buf, 0, digest, payload, sizeof(payload));

  c4_state_t      state = {0};
  data_request_t  req   = {0};
  chunk_counter_t cnt   = {0};
  TEST_ASSERT_TRUE(c4_eth_walk_lcu_list(C4_CHAIN_MAINNET, bytes(buf, end), &state, &req,
                                        /*validate_ssz*/ false, count_chunks_cb, &cnt));
  TEST_ASSERT_EQUAL_UINT32(1, cnt.seen);
  TEST_ASSERT_EQUAL_INT((int) C4_FORK_ELECTRA, (int) cnt.forks[0]);
  TEST_ASSERT_TRUE(req.validated);
}

void test_walk_lcu_list_two_chunks(void) {
  uint8_t digest_e[4] = {0};
  uint8_t digest_f[4] = {0};
  TEST_ASSERT_TRUE(digest_at_fork(C4_CHAIN_MAINNET, C4_FORK_ELECTRA, digest_e));
  TEST_ASSERT_TRUE(digest_at_fork(C4_CHAIN_MAINNET, C4_FORK_FULU, digest_f));

  uint8_t  buf[128] = {0};
  uint8_t  p1[8]    = {0x11};
  uint8_t  p2[4]    = {0x22};
  uint32_t off      = 0;
  off               = emit_chunk(buf, off, digest_e, p1, sizeof(p1));
  off               = emit_chunk(buf, off, digest_f, p2, sizeof(p2));

  c4_state_t      state = {0};
  data_request_t  req   = {0};
  chunk_counter_t cnt   = {0};
  TEST_ASSERT_TRUE(c4_eth_walk_lcu_list(C4_CHAIN_MAINNET, bytes(buf, off), &state, &req,
                                        /*validate_ssz*/ false, count_chunks_cb, &cnt));
  TEST_ASSERT_EQUAL_UINT32(2, cnt.seen);
  TEST_ASSERT_EQUAL_INT((int) C4_FORK_ELECTRA, (int) cnt.forks[0]);
  TEST_ASSERT_EQUAL_INT((int) C4_FORK_FULU, (int) cnt.forks[1]);
  TEST_ASSERT_TRUE(req.validated);
}

void test_walk_lcu_list_truncated_header(void) {
  // Buffer with only 8 bytes -- shorter than the required 12-byte prefix.
  uint8_t buf[8] = {0};
  uint64_to_le(buf, 4);
  c4_state_t     state = {0};
  data_request_t req   = {0};
  TEST_ASSERT_FALSE(c4_eth_walk_lcu_list(C4_CHAIN_MAINNET, bytes(buf, sizeof(buf)), &state, &req,
                                         /*validate_ssz*/ false, count_chunks_cb, NULL));
  TEST_ASSERT_FALSE(req.validated);
  TEST_ASSERT_NOT_NULL(state.error);
  c4_state_free(&state);
}

void test_walk_lcu_list_length_below_context(void) {
  // length = 3 is invalid because the mandatory 4-byte context alone requires
  // length >= 4. This is the framing check we cannot express with a payload
  // of size 0 (which would still declare length == 4).
  uint8_t buf[12] = {0};
  uint64_to_le(buf, 3);
  c4_state_t     state = {0};
  data_request_t req   = {0};
  TEST_ASSERT_FALSE(c4_eth_walk_lcu_list(C4_CHAIN_MAINNET, bytes(buf, sizeof(buf)), &state, &req,
                                         /*validate_ssz*/ false, count_chunks_cb, NULL));
  TEST_ASSERT_FALSE(req.validated);
  c4_state_free(&state);
}

void test_walk_lcu_list_length_exceeds_buffer(void) {
  // Declare an oversized chunk that would overrun the buffer.
  uint8_t buf[32] = {0};
  uint64_to_le(buf, 999);
  c4_state_t     state = {0};
  data_request_t req   = {0};
  TEST_ASSERT_FALSE(c4_eth_walk_lcu_list(C4_CHAIN_MAINNET, bytes(buf, sizeof(buf)), &state, &req,
                                         /*validate_ssz*/ false, count_chunks_cb, NULL));
  TEST_ASSERT_FALSE(req.validated);
  c4_state_free(&state);
}

void test_walk_lcu_list_length_uint64_wrap(void) {
  // Hostile length near UINT64_MAX used to wrap `pos + 8 + length` so the
  // bounds check passed, `chunk_len` truncated to 0, and the walker looped.
  uint8_t buf[16] = {0};
  uint64_to_le(buf, UINT64_MAX - 7ULL);
  c4_state_t     state = {0};
  data_request_t req   = {0};
  TEST_ASSERT_FALSE(c4_eth_walk_lcu_list(C4_CHAIN_MAINNET, bytes(buf, sizeof(buf)), &state, &req,
                                         /*validate_ssz*/ false, NULL, NULL));
  TEST_ASSERT_FALSE(req.validated);
  TEST_ASSERT_NOT_NULL(state.error);
  c4_state_free(&state);
}

void test_walk_lcu_list_tail_garbage(void) {
  // Two-chunk framing where the declared length undershoots the buffer -- the
  // remaining tail bytes would not tile.
  uint8_t digest[4] = {0};
  TEST_ASSERT_TRUE(digest_at_fork(C4_CHAIN_MAINNET, C4_FORK_ELECTRA, digest));
  uint8_t  buf[64] = {0};
  uint8_t  p[4]    = {0};
  uint32_t off     = emit_chunk(buf, 0, digest, p, sizeof(p));
  // Append 3 uncovered tail bytes.
  off += 3;
  c4_state_t     state = {0};
  data_request_t req   = {0};
  TEST_ASSERT_FALSE(c4_eth_walk_lcu_list(C4_CHAIN_MAINNET, bytes(buf, off), &state, &req,
                                         /*validate_ssz*/ false, count_chunks_cb, NULL));
  TEST_ASSERT_FALSE(req.validated);
  c4_state_free(&state);
}

void test_walk_lcu_list_unknown_context(void) {
  // Framing valid, but the 4-byte context matches no scheduled fork on the
  // chain. Framing pass still marks `validated`; the semantic pass reports
  // an error and returns false.
  uint8_t  buf[32]    = {0};
  uint8_t  junk[4]    = {0xde, 0xad, 0xbe, 0xef};
  uint8_t  payload[8] = {0};
  uint32_t off        = emit_chunk(buf, 0, junk, payload, sizeof(payload));

  c4_state_t     state = {0};
  data_request_t req   = {0};
  TEST_ASSERT_FALSE(c4_eth_walk_lcu_list(C4_CHAIN_MAINNET, bytes(buf, off), &state, &req,
                                         /*validate_ssz*/ false, count_chunks_cb, NULL));
  // Framing pass succeeded before the context resolution failed, so the
  // request is marked validated even though the walker aborted.
  TEST_ASSERT_TRUE(req.validated);
  TEST_ASSERT_NOT_NULL(state.error);
  c4_state_free(&state);
}

void test_walk_lcu_list_cb_stop_after_first(void) {
  uint8_t digest[4] = {0};
  TEST_ASSERT_TRUE(digest_at_fork(C4_CHAIN_MAINNET, C4_FORK_ELECTRA, digest));
  uint8_t  buf[64] = {0};
  uint8_t  p[4]    = {0};
  uint32_t off     = 0;
  off              = emit_chunk(buf, off, digest, p, sizeof(p));
  off              = emit_chunk(buf, off, digest, p, sizeof(p));

  c4_state_t      state = {0};
  data_request_t  req   = {0};
  chunk_counter_t cnt   = {.stop_after_first = true};
  // Callback returns false -> walker returns false, but framing has already
  // marked `validated`.
  TEST_ASSERT_FALSE(c4_eth_walk_lcu_list(C4_CHAIN_MAINNET, bytes(buf, off), &state, &req,
                                         /*validate_ssz*/ false, count_chunks_cb, &cnt));
  TEST_ASSERT_EQUAL_UINT32(1, cnt.seen);
  TEST_ASSERT_TRUE(req.validated);
}

// -----------------------------------------------------------------------------
// Bootstrap decoder
// -----------------------------------------------------------------------------

void test_decode_bootstrap_recorded_deneb(void) {
  bytes_t data = read_testdata(
      "eth_getBlockByNumber1/eth_v1_beacon_light_client_bootstrap_"
      "0x46d52dc3db74df14187554fb7afd5cc4af4558e3c8ca185e409955d6ac148.ssz");
  TEST_ASSERT_NOT_NULL_MESSAGE(data.data,
                               "Deneb bootstrap fixture is missing from test data");

  ssz_ob_t       out   = {0};
  c4_state_t     state = {0};
  data_request_t req   = {0};
  bool           ok    = c4_eth_decode_bootstrap(C4_CHAIN_MAINNET, data, &state, &req, &out);
  TEST_ASSERT_TRUE_MESSAGE(ok, state.error ? state.error : "Deneb bootstrap must decode");
  TEST_ASSERT_NOT_NULL(out.def);
  TEST_ASSERT_TRUE(req.validated);
  safe_free(data.data);
  c4_state_free(&state);
}

void test_decode_bootstrap_recorded_electra(void) {
  bytes_t data = read_testdata(
      "eth_call_authorization_list/eth_v1_beacon_light_client_bootstrap_"
      "0x625c1e521cedccc30c1f696a205cdad18859eb7b06fd9f848e1e5434bba19.ssz");
  TEST_ASSERT_NOT_NULL_MESSAGE(data.data,
                               "Electra bootstrap fixture is missing from test data");
  // Sanity: the fixture's leading offset must equal the Electra fixed-size
  // constant, otherwise the discriminator is stale.
  TEST_ASSERT_EQUAL_UINT32(C4_ETH_ELECTRA_BOOTSTRAP_FIXED_SIZE, uint32_from_le(data.data));

  ssz_ob_t       out   = {0};
  c4_state_t     state = {0};
  data_request_t req   = {0};
  bool           ok    = c4_eth_decode_bootstrap(C4_CHAIN_MAINNET, data, &state, &req, &out);
  TEST_ASSERT_TRUE_MESSAGE(ok, state.error ? state.error : "Electra bootstrap must decode");
  TEST_ASSERT_NOT_NULL(out.def);
  TEST_ASSERT_TRUE(req.validated);
  safe_free(data.data);
  c4_state_free(&state);
}

void test_decode_bootstrap_rejects_short(void) {
  uint8_t    buf[16] = {0};
  ssz_ob_t   out     = {0};
  c4_state_t state   = {0};
  TEST_ASSERT_FALSE(
      c4_eth_decode_bootstrap(C4_CHAIN_MAINNET, bytes(buf, sizeof(buf)), &state, NULL, &out));
  TEST_ASSERT_NOT_NULL(state.error);
  c4_state_free(&state);
}

void test_decode_bootstrap_rejects_bogus_offset(void) {
  // Buffer large enough to look like a Deneb/Electra bootstrap, but the
  // leading offset value points to neither fork's fixed-portion size.
  uint8_t buf[26000] = {0};
  uint32_to_le(buf, 12345); // definitely not a valid header offset
  ssz_ob_t   out   = {0};
  c4_state_t state = {0};
  TEST_ASSERT_FALSE(
      c4_eth_decode_bootstrap(C4_CHAIN_MAINNET, bytes(buf, sizeof(buf)), &state, NULL, &out));
  TEST_ASSERT_NOT_NULL(state.error);
  c4_state_free(&state);
}

// -----------------------------------------------------------------------------
// NULL-parameter and edge-case behaviour of the public API. These are cheap
// but load-bearing: every caller under `src/chains/eth/` relies on the helpers
// tolerating a NULL `req` (the walker is invoked from paths that don't have a
// backing `data_request_t`) and on `decode_bootstrap` refusing NULL `out`
// instead of dereferencing it.
// -----------------------------------------------------------------------------

void test_compute_fork_digest_null_out(void) {
  TEST_ASSERT_FALSE(c4_eth_compute_fork_digest(C4_CHAIN_MAINNET, 0, NULL));
}

void test_fork_from_context_null_context(void) {
  TEST_ASSERT_EQUAL_INT((int) C4_FORK_INVALID,
                        (int) c4_eth_fork_from_context(C4_CHAIN_MAINNET, NULL));
}

void test_fork_from_context_fork_version_fallback_fulu(void) {
  // Extend the fallback-path coverage beyond Electra: Fulu's raw `fork_version`
  // must also resolve so any period-store `lcu.ssz` written by an older build
  // that used the raw version instead of the ForkDigest keeps parsing after
  // the migration.
  const chain_spec_t* spec = c4_eth_get_chain_spec(C4_CHAIN_MAINNET);
  TEST_ASSERT_NOT_NULL(spec);
  uint8_t v[4] = {0};
  spec->fork_version_func(C4_CHAIN_MAINNET, C4_FORK_FULU, v);
  TEST_ASSERT_EQUAL_INT((int) C4_FORK_FULU,
                        (int) c4_eth_fork_from_context(C4_CHAIN_MAINNET, v));
}

void test_walk_lcu_list_null_data_nonzero_len(void) {
  // Non-zero `len` with a NULL `data` pointer must be rejected explicitly --
  // an unchecked NULL deref would be a memory-safety bug. This is a distinct
  // code path from the empty-buffer case (which succeeds).
  bytes_t        bogus = {.len = 4, .data = NULL};
  c4_state_t     state = {0};
  data_request_t req   = {0};
  TEST_ASSERT_FALSE(c4_eth_walk_lcu_list(C4_CHAIN_MAINNET, bogus, &state, &req,
                                         /*validate_ssz*/ false, NULL, NULL));
  TEST_ASSERT_FALSE(req.validated);
  TEST_ASSERT_NOT_NULL(state.error);
  c4_state_free(&state);
}

void test_walk_lcu_list_null_req(void) {
  // Every helper documents `req` as optional. The walker is invoked from paths
  // that don't have a backing request (e.g. the WSP chain-of-trust re-scan in
  // `sync_committee_state.c`), so a NULL `req` on the happy path must not
  // crash and must not affect the return value.
  uint8_t digest[4] = {0};
  TEST_ASSERT_TRUE(digest_at_fork(C4_CHAIN_MAINNET, C4_FORK_ELECTRA, digest));
  uint8_t buf[64] = {0};
  uint8_t payload[8];
  memset(payload, 0xcd, sizeof(payload));
  uint32_t end = emit_chunk(buf, 0, digest, payload, sizeof(payload));

  c4_state_t      state = {0};
  chunk_counter_t cnt   = {0};
  TEST_ASSERT_TRUE(c4_eth_walk_lcu_list(C4_CHAIN_MAINNET, bytes(buf, end), &state,
                                        /*req*/ NULL, /*validate_ssz*/ false,
                                        count_chunks_cb, &cnt));
  TEST_ASSERT_EQUAL_UINT32(1, cnt.seen);
  TEST_ASSERT_EQUAL_INT((int) C4_FORK_ELECTRA, (int) cnt.forks[0]);
  TEST_ASSERT_NULL(state.error);
}

void test_walk_lcu_list_null_cb(void) {
  // `cb == NULL` is a documented mode for framing-only validation. Two chunks
  // with valid framing and known contexts must return true.
  uint8_t digest[4] = {0};
  TEST_ASSERT_TRUE(digest_at_fork(C4_CHAIN_MAINNET, C4_FORK_DENEB, digest));
  uint8_t  buf[64] = {0};
  uint8_t  p[4]    = {0};
  uint32_t off     = 0;
  off              = emit_chunk(buf, off, digest, p, sizeof(p));
  off              = emit_chunk(buf, off, digest, p, sizeof(p));

  c4_state_t     state = {0};
  data_request_t req   = {0};
  TEST_ASSERT_TRUE(c4_eth_walk_lcu_list(C4_CHAIN_MAINNET, bytes(buf, off), &state, &req,
                                        /*validate_ssz*/ false, /*cb*/ NULL, NULL));
  TEST_ASSERT_TRUE(req.validated);
  TEST_ASSERT_NULL(state.error);
}

void test_walk_lcu_list_null_state_on_error(void) {
  // `state == NULL` is documented as valid ("may be NULL to suppress"). The
  // framing-error path must not attempt to write into a NULL state pointer.
  uint8_t buf[8] = {0};
  uint64_to_le(buf, 4);
  data_request_t req = {0};
  TEST_ASSERT_FALSE(c4_eth_walk_lcu_list(C4_CHAIN_MAINNET, bytes(buf, sizeof(buf)),
                                         /*state*/ NULL, &req,
                                         /*validate_ssz*/ false, NULL, NULL));
  TEST_ASSERT_FALSE(req.validated);
}

void test_walk_lcu_list_zero_payload_chunk(void) {
  // Chunk with `length == 4` (context only, empty payload) is valid framing.
  // The walker must count it and resolve its fork; whether an empty payload
  // is a valid LCU-SSZ is intentionally not checked here (validate_ssz=false).
  uint8_t digest[4] = {0};
  TEST_ASSERT_TRUE(digest_at_fork(C4_CHAIN_MAINNET, C4_FORK_ELECTRA, digest));
  uint8_t buf[12] = {0};
  uint64_to_le(buf, 4); // length = 4 (context only)
  memcpy(buf + 8, digest, 4);

  c4_state_t      state = {0};
  data_request_t  req   = {0};
  chunk_counter_t cnt   = {0};
  TEST_ASSERT_TRUE(c4_eth_walk_lcu_list(C4_CHAIN_MAINNET, bytes(buf, sizeof(buf)), &state, &req,
                                        /*validate_ssz*/ false, count_chunks_cb, &cnt));
  TEST_ASSERT_EQUAL_UINT32(1, cnt.seen);
  TEST_ASSERT_EQUAL_INT((int) C4_FORK_ELECTRA, (int) cnt.forks[0]);
  TEST_ASSERT_TRUE(req.validated);
}

// -----------------------------------------------------------------------------
// Bootstrap: NULL-`out`, NULL-`req`, and chain-schedule cross-check.
// -----------------------------------------------------------------------------

void test_decode_bootstrap_null_out(void) {
  uint8_t    buf[16] = {0};
  c4_state_t state   = {0};
  TEST_ASSERT_FALSE(c4_eth_decode_bootstrap(C4_CHAIN_MAINNET, bytes(buf, sizeof(buf)),
                                            &state, NULL, /*out*/ NULL));
  // The NULL-out check happens before any error is recorded on state -- a
  // caller passing NULL out is a programmer error, not a data error.
  c4_state_free(&state);
}

void test_decode_bootstrap_null_req_recorded_deneb(void) {
  // Re-runs the Deneb happy path with `req == NULL` to exercise the branch
  // that skips setting `req->validated`.
  bytes_t data = read_testdata(
      "eth_getBlockByNumber1/eth_v1_beacon_light_client_bootstrap_"
      "0x46d52dc3db74df14187554fb7afd5cc4af4558e3c8ca185e409955d6ac148.ssz");
  TEST_ASSERT_NOT_NULL_MESSAGE(data.data, "Deneb bootstrap fixture missing");

  ssz_ob_t   out   = {0};
  c4_state_t state = {0};
  bool       ok    = c4_eth_decode_bootstrap(C4_CHAIN_MAINNET, data, &state, /*req*/ NULL, &out);
  TEST_ASSERT_TRUE_MESSAGE(ok, state.error ? state.error : "must decode without req");
  TEST_ASSERT_NOT_NULL(out.def);
  safe_free(data.data);
  c4_state_free(&state);
}

void test_decode_bootstrap_gloas_size_rejected_on_mainnet(void) {
  // A blob whose size matches the Gloas bootstrap (25472 B) but sent to
  // Mainnet (where Gloas is NOT scheduled) must be rejected by the chain-
  // schedule cross-check BEFORE `ssz_is_valid` is invoked. This defends
  // against a beacon node accidentally serving a Gloas-shaped blob to a
  // pre-Gloas chain.
  uint8_t*   buf   = safe_calloc(1, C4_ETH_GLOAS_BOOTSTRAP_SIZE);
  ssz_ob_t   out   = {0};
  c4_state_t state = {0};
  TEST_ASSERT_FALSE(c4_eth_decode_bootstrap(C4_CHAIN_MAINNET,
                                            bytes(buf, C4_ETH_GLOAS_BOOTSTRAP_SIZE),
                                            &state, /*req*/ NULL, &out));
  TEST_ASSERT_NOT_NULL_MESSAGE(state.error, "must record schedule-check error");
  // Sanity: `out` must remain untouched on failure so callers can't mistake
  // a stale/uninitialised view for a valid one.
  TEST_ASSERT_NULL(out.def);
  safe_free(buf);
  c4_state_free(&state);
}

// -----------------------------------------------------------------------------
// validate_ssz=true against the recorded Mainnet LCU fixture and the slot /
// context-digest cross-check. The fixture belongs to `trusted_block1` (period
// 1392) so it is guaranteed to be present.
// -----------------------------------------------------------------------------

// Real Beacon-API list fixture: an LE uint64 length + 4-byte ForkDigest +
// SSZ LCU payload for exactly one update.
#define LCU_FIXTURE_PATH \
  "trusted_block1/eth_v1_beacon_light_client_updates_start_period_1392_count_1.ssz"

// The LCU container's fixed portion is
//   attestedHeader offset(4) + nextSyncCommittee(24624) + nextSyncCommitteeBranch(5*32)
// + finalizedHeader offset(4) + finalityBranch(6*32) + syncAggregate(160) + signatureSlot(8)
// = 25152 bytes for the Deneb layout that this fixture uses. The first 4 bytes
// of the payload therefore encode 25152 as the attestedHeader offset (verified
// in `test_walk_lcu_list_validate_ssz_ok`), which is where `beacon.slot` lives.
#define LCU_FIXTURE_ATTESTED_HEADER_OFFSET 25152u

void test_walk_lcu_list_validate_ssz_ok(void) {
  bytes_t data = read_testdata(LCU_FIXTURE_PATH);
  TEST_ASSERT_NOT_NULL_MESSAGE(data.data, "LCU fixture missing");

  // Sanity: the fixture context resolves to a scheduled Mainnet fork (i.e.
  // Deneb or Electra depending on when the fixture was captured). If this
  // assertion breaks, the fixture was regenerated and the test's other
  // assumptions probably need revisiting.
  fork_id_t fork_from_ctx = c4_eth_fork_from_context(C4_CHAIN_MAINNET, data.data + 8);
  TEST_ASSERT_TRUE_MESSAGE(fork_from_ctx == C4_FORK_DENEB || fork_from_ctx == C4_FORK_ELECTRA,
                           "fixture must belong to Deneb or Electra");

  // Sanity: our slot-mutation offset only makes sense for the Deneb layout.
  // Fail early with a helpful message if the fixture was regenerated as
  // Electra (branch depths change -> 25184 fixed portion instead of 25152).
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(LCU_FIXTURE_ATTESTED_HEADER_OFFSET,
                                   uint32_from_le(data.data + 12),
                                   "fixture layout drifted; refresh LCU_FIXTURE_ATTESTED_HEADER_OFFSET");

  c4_state_t      state = {0};
  data_request_t  req   = {0};
  chunk_counter_t cnt   = {0};
  bool            ok    = c4_eth_walk_lcu_list(C4_CHAIN_MAINNET, data, &state, &req,
                                               /*validate_ssz*/ true, count_chunks_cb, &cnt);
  TEST_ASSERT_TRUE_MESSAGE(ok, state.error ? state.error : "walker must accept real fixture");
  TEST_ASSERT_EQUAL_UINT32(1, cnt.seen);
  TEST_ASSERT_EQUAL_INT((int) fork_from_ctx, (int) cnt.forks[0]);
  TEST_ASSERT_TRUE(req.validated);
  safe_free(data.data);
  c4_state_free(&state);
}

void test_walk_lcu_list_validate_ssz_slot_mismatch(void) {
  // Take a real Mainnet Deneb LCU, keep the SSZ layout & context intact, and
  // only mutate `attestedHeader.beacon.slot` to a slot that maps to Electra
  // on Mainnet (Electra activates at epoch 364032 = slot 11649024). The
  // walker must:
  //   - pass framing (unchanged),
  //   - pass `ssz_is_valid` (mutating an 8-byte scalar leaves SSZ structure
  //     intact),
  //   - reject on the slot-vs-context-digest cross-check.
  bytes_t data = read_testdata(LCU_FIXTURE_PATH);
  TEST_ASSERT_NOT_NULL_MESSAGE(data.data, "LCU fixture missing");

  fork_id_t fork_from_ctx = c4_eth_fork_from_context(C4_CHAIN_MAINNET, data.data + 8);
  // Skip the test if the fixture was regenerated as a non-Deneb layout --
  // the mutation offset would be wrong and we'd silently corrupt a length
  // prefix. The other tests still cover the happy path in that case.
  if (fork_from_ctx != C4_FORK_DENEB) {
    safe_free(data.data);
    TEST_IGNORE_MESSAGE("fixture is not Deneb; slot-mutation offset would drift");
    return;
  }
  TEST_ASSERT_EQUAL_UINT32(LCU_FIXTURE_ATTESTED_HEADER_OFFSET,
                           uint32_from_le(data.data + 12));

  // Absolute byte offset of `attestedHeader.beacon.slot` inside the fixture:
  //   8 (wire length) + 4 (context) + attestedHeader offset (25152) + 0 (slot
  //   is the first field of BeaconBlockHeader).
  const uint32_t slot_offset =
      C4_LCU_WIRE_PREFIX_SIZE + LCU_FIXTURE_ATTESTED_HEADER_OFFSET;
  TEST_ASSERT_TRUE_MESSAGE(slot_offset + 8 <= data.len, "slot offset out of bounds");

  // Slot 12_800_000 -> epoch 400_000. On Mainnet Electra activates at epoch
  // 364_032 and Fulu at 411_392, so this slot is unambiguously Electra.
  const uint64_t electra_slot = 12800000ULL;
  uint64_to_le(data.data + slot_offset, electra_slot);

  c4_state_t      state = {0};
  data_request_t  req   = {0};
  chunk_counter_t cnt   = {0};
  bool            ok    = c4_eth_walk_lcu_list(C4_CHAIN_MAINNET, data, &state, &req,
                                               /*validate_ssz*/ true, count_chunks_cb, &cnt);
  TEST_ASSERT_FALSE_MESSAGE(ok, "walker must reject slot/context fork mismatch");
  // Framing succeeded, so `validated` must have been set before the pass-2
  // slot-check fired.
  TEST_ASSERT_TRUE_MESSAGE(req.validated,
                           "framing pass must have marked the request validated");
  TEST_ASSERT_NOT_NULL(state.error);
  // The callback must not have been invoked -- the cross-check runs before it.
  TEST_ASSERT_EQUAL_UINT32(0, cnt.seen);
  safe_free(data.data);
  c4_state_free(&state);
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_compute_fork_digest_deterministic);
  RUN_TEST(test_compute_fork_digest_differs_per_fork);
  RUN_TEST(test_compute_fork_digest_bpo_differs_from_fulu_activation);
  RUN_TEST(test_compute_fork_digest_bpo1_and_epoch_boundaries);
  RUN_TEST(test_compute_fork_digest_sepolia_bpo);
  RUN_TEST(test_compute_fork_digest_gnosis_empty_cl_blob_schedule);
  RUN_TEST(test_compute_fork_digest_rejects_unknown_chain);

  RUN_TEST(test_fork_from_context_digest_hits);
  RUN_TEST(test_fork_from_context_fork_version_fallback);
  RUN_TEST(test_fork_from_context_rejects_unknown);

  RUN_TEST(test_walk_lcu_list_empty);
  RUN_TEST(test_walk_lcu_list_one_chunk_marks_validated);
  RUN_TEST(test_walk_lcu_list_two_chunks);
  RUN_TEST(test_walk_lcu_list_truncated_header);
  RUN_TEST(test_walk_lcu_list_length_below_context);
  RUN_TEST(test_walk_lcu_list_length_exceeds_buffer);
  RUN_TEST(test_walk_lcu_list_length_uint64_wrap);
  RUN_TEST(test_walk_lcu_list_tail_garbage);
  RUN_TEST(test_walk_lcu_list_unknown_context);
  RUN_TEST(test_walk_lcu_list_cb_stop_after_first);

  RUN_TEST(test_decode_bootstrap_recorded_deneb);
  RUN_TEST(test_decode_bootstrap_recorded_electra);
  RUN_TEST(test_decode_bootstrap_rejects_short);
  RUN_TEST(test_decode_bootstrap_rejects_bogus_offset);

  // NULL / edge-case coverage
  RUN_TEST(test_compute_fork_digest_null_out);
  RUN_TEST(test_fork_from_context_null_context);
  RUN_TEST(test_fork_from_context_fork_version_fallback_fulu);
  RUN_TEST(test_walk_lcu_list_null_data_nonzero_len);
  RUN_TEST(test_walk_lcu_list_null_req);
  RUN_TEST(test_walk_lcu_list_null_cb);
  RUN_TEST(test_walk_lcu_list_null_state_on_error);
  RUN_TEST(test_walk_lcu_list_zero_payload_chunk);
  RUN_TEST(test_decode_bootstrap_null_out);
  RUN_TEST(test_decode_bootstrap_null_req_recorded_deneb);
  RUN_TEST(test_decode_bootstrap_gloas_size_rejected_on_mainnet);

  // Real-fixture SSZ validation & slot-vs-context cross-check
  RUN_TEST(test_walk_lcu_list_validate_ssz_ok);
  RUN_TEST(test_walk_lcu_list_validate_ssz_slot_mismatch);

  return UNITY_END();
}
