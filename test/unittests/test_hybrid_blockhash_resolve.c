/*
 * Copyright 2026 corpus.core
 * SPDX-License-Identifier: MIT
 *
 * Tests for the `blockHash`-variant resolver in
 * `src/chains/eth/prover/beacon_header.c`. In hybrid mode the remote prover may
 * omit the block proof and only reference the block by hash when it thinks the
 * client's `header_cache` already holds the verified header (last_block_hash
 * advertisement or server-side cache hit). The resolver recovers the RLP header
 * from either a `C4_DATA_TYPE_CACHE` snapshot already attached to the prover
 * ctx (created by `verify_block_by_blockhash`) or from the process-global
 * `header_cache`, pinning a fresh copy as a snapshot so downstream code sees a
 * borrow that survives for the lifetime of the ctx.
 *
 * The tests drive the private helper through a test-only wrapper
 * (`c4_hybrid_test_resolve_block_hash`, declared in `beacon.h` under
 * `#ifdef TEST`). No RPC, no fixture data.
 */

#include "chains/eth/prover/beacon.h"
#include "chains/eth/verifier/header_cache.h"
#include "prover/prover.h"
#include "util/bytes.h"
#include "util/state.h"
#include "unity.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef EL_HEADER_CACHE

// Distinct test values so mix-ups show up in a diff.
#define TEST_CHAIN 1
static const uint8_t HASH[32] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
                                 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
                                 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
                                 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};

// A short synthetic "RLP header" -- the resolver treats the bytes opaquely; it
// only cares that they end up borrowed correctly.
static const uint8_t HEADER_BYTES[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

static prover_ctx_t g_ctx;

void setUp(void) {
  memset(&g_ctx, 0, sizeof(g_ctx));
  g_ctx.chain_id = TEST_CHAIN;
  c4_header_cache_clear();
}

void tearDown(void) {
  c4_state_free(&g_ctx.state);
  c4_header_cache_clear();
}

// Path A: verifier ran, attached the header as a C4_DATA_TYPE_CACHE snapshot to
// ctx->state. The resolver must return the snapshot's bytes as a *borrow* and
// must not clone them into a second snapshot.
void test_resolve_uses_existing_state_snapshot(void) {
  data_request_t* snapshot = safe_calloc(1, sizeof(data_request_t));
  snapshot->chain_id       = TEST_CHAIN;
  snapshot->type           = C4_DATA_TYPE_CACHE;
  snapshot->response       = bytes_dup(bytes((uint8_t*) HEADER_BYTES, sizeof(HEADER_BYTES)));
  memcpy(snapshot->id, HASH, 32);
  c4_state_add_request(&g_ctx.state, snapshot);

  uint8_t*    snapshot_data = snapshot->response.data;
  bytes_t     el_header     = NULL_BYTES;
  c4_status_t status        = c4_hybrid_test_resolve_block_hash(&g_ctx, HASH, &el_header);

  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, status);
  TEST_ASSERT_EQUAL_PTR(snapshot_data, el_header.data); // borrow, no copy
  TEST_ASSERT_EQUAL_UINT32(sizeof(HEADER_BYTES), el_header.len);

  // No additional snapshot must have appeared: `c4_state_get_data_request_by_id`
  // still finds the one we set up and it is unique in the list.
  data_request_t* found = c4_state_get_data_request_by_id(&g_ctx.state, (uint8_t*) HASH);
  TEST_ASSERT_EQUAL_PTR(snapshot, found);
  uint32_t count = 0;
  for (data_request_t* r = g_ctx.state.requests; r; r = r->next) count++;
  TEST_ASSERT_EQUAL_UINT32(1, count);
}

// Path B: no snapshot in state yet, but the header cache still holds the
// verified header (fresh call after a preceding header-only fetch). The
// resolver must copy the header out of the cache and pin it as a snapshot on
// the ctx so downstream code (and later re-entries) find it.
void test_resolve_copies_from_header_cache(void) {
  c4_header_cache_put(TEST_CHAIN, /*block_number=*/42, HASH,
                      bytes((uint8_t*) HEADER_BYTES, sizeof(HEADER_BYTES)), NULL);

  bytes_t     el_header = NULL_BYTES;
  c4_status_t status    = c4_hybrid_test_resolve_block_hash(&g_ctx, HASH, &el_header);

  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, status);
  TEST_ASSERT_NOT_NULL(el_header.data);
  TEST_ASSERT_EQUAL_UINT32(sizeof(HEADER_BYTES), el_header.len);
  TEST_ASSERT_EQUAL_MEMORY(HEADER_BYTES, el_header.data, sizeof(HEADER_BYTES));

  // Snapshot must now live on the ctx state under id=block_hash, and el_header
  // must point at the snapshot's response (owned by the state, not by us).
  data_request_t* snapshot = c4_state_get_data_request_by_id(&g_ctx.state, (uint8_t*) HASH);
  TEST_ASSERT_NOT_NULL(snapshot);
  TEST_ASSERT_EQUAL_INT(C4_DATA_TYPE_CACHE, snapshot->type);
  TEST_ASSERT_EQUAL_PTR(snapshot->response.data, el_header.data);
}

// Path B, second call: once the snapshot exists it must be reused. Clearing
// the header cache after the first resolve makes any accidental re-lookup fail
// -- if the resolver falls back to the cache instead of using the snapshot we
// see it immediately.
void test_resolve_reuses_snapshot_across_calls(void) {
  c4_header_cache_put(TEST_CHAIN, 42, HASH,
                      bytes((uint8_t*) HEADER_BYTES, sizeof(HEADER_BYTES)), NULL);

  bytes_t first  = NULL_BYTES;
  bytes_t second = NULL_BYTES;
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, c4_hybrid_test_resolve_block_hash(&g_ctx, HASH, &first));

  c4_header_cache_clear();

  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, c4_hybrid_test_resolve_block_hash(&g_ctx, HASH, &second));
  TEST_ASSERT_EQUAL_PTR(first.data, second.data);
  TEST_ASSERT_EQUAL_UINT32(first.len, second.len);
}

// Path C: neither snapshot nor header cache -- a genuine miss (the advertised
// block was evicted between fetch and response). Must return C4_ERROR and set
// an error message on the ctx state instead of silently producing an empty
// header (the old NULL_BYTES fall-through would crash `keccak` downstream).
void test_resolve_cache_miss_is_error(void) {
  bytes_t     el_header = NULL_BYTES;
  c4_status_t status    = c4_hybrid_test_resolve_block_hash(&g_ctx, HASH, &el_header);

  TEST_ASSERT_EQUAL_INT(C4_ERROR, status);
  TEST_ASSERT_NULL(el_header.data);
  TEST_ASSERT_NOT_NULL(g_ctx.state.error);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_resolve_uses_existing_state_snapshot);
  RUN_TEST(test_resolve_copies_from_header_cache);
  RUN_TEST(test_resolve_reuses_snapshot_across_calls);
  RUN_TEST(test_resolve_cache_miss_is_error);
  return UNITY_END();
}

#else // !EL_HEADER_CACHE

void setUp(void) {}
void tearDown(void) {}

int main(void) {
  UNITY_BEGIN();
  // The resolver's `blockHash`-branch is only reachable when the header cache is
  // compiled in; skip the whole suite otherwise so embedded builds stay green.
  return UNITY_END();
}

#endif
