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

#include "beacon_types.h"
#include "bytes.h"
#include "c4_assert.h"
#include "crypto.h"
#include "eth_verify.h"
#include "ssz.h"
#include "sync_committee.h"
#include "unity.h"
#include <string.h>

#ifdef USE_CHECKPOINTZ

// Local copy of the ETH_CHECKPOINT_PROOF container (the canonical definition in
// `verify_proof_types.h` is not self-contained -- it references TU-local statics
// from `verify_types.c`). SSZ field order and types must stay byte-for-byte in
// sync with the production container.
static const ssz_def_t WSP_TEST_CHECKPOINT_PROOF[] = {
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER),
    SSZ_BYTE_VECTOR("aggregate_pubkey", 48),
    SSZ_LIST("proof", ssz_bytes32, 16)};
static const ssz_def_t WSP_TEST_CHECKPOINT_PROOF_CONTAINER = SSZ_CONTAINER("CheckpointProof", WSP_TEST_CHECKPOINT_PROOF);

void setUp(void) {
  reset_local_filecache();
}

void tearDown(void) {
  reset_local_filecache();
}

// Build a minimal valid-shaped CheckpointProof SSZ blob suitable for negative tests.
// The header / branch / aggregate values are dummies -- the merkle proof will
// not match for these, but we use this only to exercise structural validation
// and to ensure the helper compiles and links against the public API.
static ssz_ob_t build_dummy_checkpoint_proof(uint64_t slot, uint8_t fill_byte) {
  bytes32_t pubkey_parent_root = {0};
  bytes32_t pubkey_state_root  = {0};
  bytes32_t pubkey_body_root   = {0};
  memset(pubkey_parent_root, fill_byte, 32);
  memset(pubkey_state_root, fill_byte, 32);
  memset(pubkey_body_root, fill_byte, 32);

  ssz_def_t     beacon_block_header_def = SSZ_CONTAINER("BeaconBlockHeader", BEACON_BLOCK_HEADER);
  ssz_builder_t header_b                = ssz_builder_for_def(&beacon_block_header_def);
  ssz_add_uint64(&header_b, slot);
  ssz_add_uint64(&header_b, 0);                                    // proposerIndex
  ssz_add_bytes(&header_b, "parentRoot", bytes(pubkey_parent_root, 32));
  ssz_add_bytes(&header_b, "stateRoot", bytes(pubkey_state_root, 32));
  ssz_add_bytes(&header_b, "bodyRoot", bytes(pubkey_body_root, 32));
  ssz_ob_t header_ob = ssz_builder_to_bytes(&header_b);

  uint8_t aggregate[48] = {0};
  memset(aggregate, fill_byte, 48);

  // 5-element merkle branch (Deneb depth), all dummy.
  uint8_t branch_buf[5 * 32] = {0};
  memset(branch_buf, fill_byte, sizeof(branch_buf));

  ssz_builder_t cp = ssz_builder_for_def(&WSP_TEST_CHECKPOINT_PROOF_CONTAINER);
  ssz_add_bytes(&cp, "header", header_ob.bytes);
  ssz_add_bytes(&cp, "aggregate_pubkey", bytes(aggregate, 48));
  ssz_add_bytes(&cp, "proof", bytes(branch_buf, sizeof(branch_buf)));
  ssz_ob_t cp_ob = ssz_builder_to_bytes(&cp);

  safe_free(header_ob.bytes.data);
  return cp_ob;
}

// Structural malformations must be rejected before any merkle work happens.
void test_checkpoint_proof_rejects_null_def(void) {
  verify_ctx_t ctx = {0};
  ctx.chain_id     = C4_CHAIN_MAINNET;
  bytes32_t pubkeys_root = {0};

  ssz_ob_t bad = {.bytes = NULL_BYTES, .def = NULL};
  c4_status_t st = c4_verify_checkpoint_proof(&ctx, bad, pubkeys_root);
  TEST_ASSERT_EQUAL_INT(C4_ERROR, st);
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  c4_state_free(&ctx.state);
}

void test_checkpoint_proof_rejects_wrong_type(void) {
  verify_ctx_t ctx = {0};
  ctx.chain_id     = C4_CHAIN_MAINNET;
  bytes32_t pubkeys_root = {0};

  // a NONE-typed ssz_ob_t must not be accepted as a CheckpointProof container
  ssz_ob_t bad = {.bytes = NULL_BYTES, .def = &ssz_none};
  c4_status_t st = c4_verify_checkpoint_proof(&ctx, bad, pubkeys_root);
  TEST_ASSERT_EQUAL_INT(C4_ERROR, st);
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  c4_state_free(&ctx.state);
}

// Well-formed structure but the merkle branch does not hash back to header.stateRoot.
// The helper must reject before issuing any checkpointz request (i.e. no pending
// data_request is enqueued on ctx->state).
void test_checkpoint_proof_rejects_mismatched_merkle(void) {
  verify_ctx_t ctx = {0};
  ctx.chain_id     = C4_CHAIN_MAINNET;
  bytes32_t pubkeys_root = {0};
  memset(pubkeys_root, 0xAB, 32);

  ssz_ob_t cp = build_dummy_checkpoint_proof(/*slot=*/4000000, /*fill=*/0x42);

  c4_status_t st = c4_verify_checkpoint_proof(&ctx, cp, pubkeys_root);
  TEST_ASSERT_EQUAL_INT(C4_ERROR, st);
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  TEST_ASSERT_NULL_MESSAGE(c4_state_get_pending_request(&ctx.state), "no checkpointz request should be issued before the merkle proof matches");

  safe_free(cp.bytes.data);
  c4_state_free(&ctx.state);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_checkpoint_proof_rejects_null_def);
  RUN_TEST(test_checkpoint_proof_rejects_wrong_type);
  RUN_TEST(test_checkpoint_proof_rejects_mismatched_merkle);
  return UNITY_END();
}

#else  // USE_CHECKPOINTZ

void setUp(void) {}
void tearDown(void) {}

void test_skipped_no_checkpointz(void) {
  TEST_IGNORE_MESSAGE("USE_CHECKPOINTZ disabled at build time");
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_skipped_no_checkpointz);
  return UNITY_END();
}

#endif  // USE_CHECKPOINTZ
