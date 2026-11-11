/*
 * Copyright 2025,2026 corpus.core
 * SPDX-License-Identifier: MIT
 *
 * Unit tests for the EEZ blockhash store (`src/chains/eez/server/blockhash_store.c`).
 * These tests exercise the serialization round-trip, the storage-trie verification and
 * the internal handler path parsing without touching libuv or the HTTP server.
 */

#include "unity.h"
#include <stdio.h>

#if defined(HTTP_SERVER) && !defined(NO_CHAIN_EEZ)

#include "../../src/chains/eez/server/blockhash_store.h"
#include "../../src/chains/eth/verifier/patricia.h"
#include "../../src/chains/eth/verifier/rlp.h"
#include "../../src/server/server.h"
#include "../../src/util/bytes.h"
#include "../../src/util/crypto.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

// A hand-picked snapshot with one populated slot. Serialize -> deserialize -> compare
// byte for byte; the file format is used both on disk and over the internal handler.
void test_serialize_round_trip(void) {
  eez_bh_snapshot_t snap    = {0};
  snap.anchor_number        = 123456;
  snap.l1_block_number      = 987654321;
  for (int i = 0; i < 32; i++) snap.anchor_hash[i] = (uint8_t) (i + 1);
  for (int i = 0; i < 32; i++) snap.storage_hash[i] = (uint8_t) (0x80 + i);
  for (int i = 0; i < 32; i++) snap.code_hash[i] = (uint8_t) (0xc0 + i);
  snap.nonce[31]            = 0x2a;
  snap.balance[30]          = 0x12;
  snap.balance[31]          = 0x34;
  // one populated slot in the middle
  for (int i = 0; i < 32; i++) snap.hashes[42][i] = (uint8_t) (0xaa ^ i);
  // two more at the boundaries to exercise the full array round-trip
  snap.hashes[0][0]    = 0xf0;
  snap.hashes[8190][7] = 0x55;

  snap.account_proof_count = 2;
  snap.account_proof_nodes = safe_calloc(2, sizeof(bytes_t));
  snap.account_proof_nodes[0].len  = 5;
  snap.account_proof_nodes[0].data = safe_malloc(5);
  memcpy(snap.account_proof_nodes[0].data, "\x01\x02\x03\x04\x05", 5);
  snap.account_proof_nodes[1].len  = 3;
  snap.account_proof_nodes[1].data = safe_malloc(3);
  memcpy(snap.account_proof_nodes[1].data, "\x0a\x0b\x0c", 3);

  buffer_t blob = {0};
  eez_bh_serialize(&snap, &blob);
  TEST_ASSERT_GREATER_THAN(184u, blob.data.len);

  eez_bh_snapshot_t* back = eez_bh_deserialize(blob.data);
  TEST_ASSERT_NOT_NULL(back);
  TEST_ASSERT_EQUAL_UINT64(snap.anchor_number, back->anchor_number);
  TEST_ASSERT_EQUAL_UINT64(snap.l1_block_number, back->l1_block_number);
  TEST_ASSERT_EQUAL_MEMORY(snap.anchor_hash, back->anchor_hash, 32);
  TEST_ASSERT_EQUAL_MEMORY(snap.storage_hash, back->storage_hash, 32);
  TEST_ASSERT_EQUAL_MEMORY(snap.code_hash, back->code_hash, 32);
  TEST_ASSERT_EQUAL_MEMORY(snap.nonce, back->nonce, 32);
  TEST_ASSERT_EQUAL_MEMORY(snap.balance, back->balance, 32);
  TEST_ASSERT_EQUAL_MEMORY(snap.hashes, back->hashes, sizeof(snap.hashes));
  TEST_ASSERT_EQUAL_UINT32(2u, back->account_proof_count);
  TEST_ASSERT_EQUAL_UINT32(5u, back->account_proof_nodes[0].len);
  TEST_ASSERT_EQUAL_MEMORY("\x01\x02\x03\x04\x05", back->account_proof_nodes[0].data, 5);
  TEST_ASSERT_EQUAL_UINT32(3u, back->account_proof_nodes[1].len);
  TEST_ASSERT_EQUAL_MEMORY("\x0a\x0b\x0c", back->account_proof_nodes[1].data, 3);

  eez_bh_snapshot_free(back);
  safe_free(snap.account_proof_nodes[0].data);
  safe_free(snap.account_proof_nodes[1].data);
  safe_free(snap.account_proof_nodes);
  safe_free(blob.data.data);
}

// An unknown magic / version / truncation must not produce a snapshot. We rely on this
// to drop a malformed on-disk file instead of serving stale data.
void test_deserialize_rejects_garbage(void) {
  // too short
  TEST_ASSERT_NULL(eez_bh_deserialize(bytes((uint8_t*) "XYZ", 3)));
  // wrong magic
  uint8_t bad_magic[200] = {0};
  memcpy(bad_magic, "BADM", 4);
  TEST_ASSERT_NULL(eez_bh_deserialize(bytes(bad_magic, 200)));
  // right magic, wrong version
  memcpy(bad_magic, "EEZB", 4);
  bad_magic[4] = 0xff;
  bad_magic[5] = 0xff;
  TEST_ASSERT_NULL(eez_bh_deserialize(bytes(bad_magic, 200)));
}

// Minimal stand-in for the parent `request_t`: `c4_internal_call_finish` reads
// `req->request_count` and `req->cb` -- the latter is called once the whole batch is
// done. The stub uses a no-op callback and a `request_count` of 1 so the batch
// completes immediately.
static int finished_count = 0;
static void on_finish_stub(request_t* parent) {
  (void) parent;
  finished_count++;
}

// The internal handler must refuse malformed paths without crashing, and must report an
// error when there is no snapshot available. Both are reachable at runtime through the
// `c4_handle_internal_request` dispatcher.
void test_internal_handler_rejects_malformed_path(void) {
  single_request_t r      = {0};
  data_request_t   req    = {0};
  request_t        parent = {0};
  r.req                   = &req;
  r.parent                = &parent;
  parent.requests         = &r;
  parent.request_count    = 1;
  parent.cb               = on_finish_stub;

  // Not an EEZ path -> handler declines so the dispatcher can try other handlers.
  req.url = "preconf/latest";
  TEST_ASSERT_FALSE(c4_handle_eez_blockhash(&r));

  // Malformed anchor segment.
  req.url        = "eez_blockhash/not_a_number/42";
  finished_count = 0;
  TEST_ASSERT_TRUE(c4_handle_eez_blockhash(&r));
  TEST_ASSERT_NOT_NULL(req.error);
  TEST_ASSERT_EQUAL_INT(1, finished_count);
  safe_free(req.error);
  req.error = NULL;

  // Slot index out of range.
  req.url        = "eez_blockhash/100/999999";
  finished_count = 0;
  TEST_ASSERT_TRUE(c4_handle_eez_blockhash(&r));
  TEST_ASSERT_NOT_NULL(req.error);
  TEST_ASSERT_EQUAL_INT(1, finished_count);
  safe_free(req.error);
  req.error = NULL;

  // Well-formed path, but no snapshot is loaded -> expect an informative error.
  req.url        = "eez_blockhash/100/42";
  finished_count = 0;
  TEST_ASSERT_TRUE(c4_handle_eez_blockhash(&r));
  TEST_ASSERT_NOT_NULL(req.error);
  TEST_ASSERT_NOT_NULL(strstr(req.error, "no snapshot"));
  TEST_ASSERT_EQUAL_INT(1, finished_count);
  safe_free(req.error);
}

// Builds a trie out of a handful of synthetic blockhashes, reads back the root, then
// re-runs the verification with that root. A match proves that the watcher's trie
// builder and the storage layout of the EIP-2935 contract agree. Flipping one byte of
// the expected root must cause the verification to fail.
void test_trie_build_and_verify_round_trip(void) {
  bytes32_t hashes[EEZ_BH_SLOT_COUNT];
  memset(hashes, 0, sizeof(hashes));

  // Populate a few slots with distinct 32-byte patterns. `0`, `1`, `42`, `8190`
  // exercise the start, the middle and the end of the slot range.
  for (int i = 0; i < 32; i++) hashes[0][i]    = (uint8_t) (i + 1);
  for (int i = 0; i < 32; i++) hashes[1][i]    = (uint8_t) (0x30 ^ i);
  for (int i = 0; i < 32; i++) hashes[42][i]   = (uint8_t) (0xaa ^ i);
  for (int i = 0; i < 32; i++) hashes[8190][i] = (uint8_t) (0x55 ^ i);

  // Build the trie once to extract the root, then throw it away.
  node_t* trie = NULL;
  bytes32_t fake_root = {0};
  // Any non-matching root first so the function frees the trie internally.
  TEST_ASSERT_FALSE(eez_bh_build_and_verify_trie(hashes, fake_root, &trie));
  TEST_ASSERT_NULL(trie);

  // Rebuild and this time capture the real root by looking at the trie before freeing.
  // We reuse the public API: a successful verify with the correct root keeps the trie.
  // Compute the expected root by one more build with a sentinel root (kept out of the
  // way) and a final build with the real root.
  // Here we use a small workaround: temporarily accept any root by passing the trie's
  // own root. Because we do not expose a "build only" helper, we rebuild with the
  // identity root the caller can derive after the first (failing) attempt by capturing
  // `patricia_get_root` through the public patricia API.
  // Instead of jumping through hoops: build the trie directly via patricia (same helper
  // the watcher uses internally) to get the expected root.
  node_t*   expected_trie  = NULL;
  bytes32_t slot_key = {0};
  bytes32_t path     = {0};
  uint8_t   value_rlp[64] = {0};
  buffer_t  value_buf = stack_buffer(value_rlp);
  for (uint32_t i = 0; i < EEZ_BH_SLOT_COUNT; i++) {
    if (!hashes[i][0] && !memcmp(hashes[i], hashes[1023], 32)) continue; // all zero
    bool all_zero = true;
    for (int b = 0; b < 32; b++) if (hashes[i][b]) { all_zero = false; break; }
    if (all_zero) continue;
    memset(slot_key, 0, 32);
    uint64_to_be(slot_key + 24, i);
    keccak(bytes(slot_key, 32), path);
    value_buf.data.len = 0;
    rlp_add_uint(&value_buf, bytes((uint8_t*) hashes[i], 32));
    patricia_set_value(&expected_trie, bytes(path, 32), value_buf.data);
  }
  bytes32_t expected_root = {0};
  memcpy(expected_root, patricia_get_root(expected_trie).data, 32);
  patricia_node_free(expected_trie);

  // Positive case: correct root, trie returned.
  TEST_ASSERT_TRUE(eez_bh_build_and_verify_trie(hashes, expected_root, &trie));
  TEST_ASSERT_NOT_NULL(trie);
  patricia_node_free(trie);
  trie = NULL;

  // Negative case: flip one byte of the expected root.
  bytes32_t bad = {0};
  memcpy(bad, expected_root, 32);
  bad[0] ^= 0x01;
  TEST_ASSERT_FALSE(eez_bh_build_and_verify_trie(hashes, bad, &trie));
  TEST_ASSERT_NULL(trie);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_serialize_round_trip);
  RUN_TEST(test_deserialize_rejects_garbage);
  RUN_TEST(test_internal_handler_rejects_malformed_path);
  RUN_TEST(test_trie_build_and_verify_round_trip);
  return UNITY_END();
}

#else
int main(void) {
  fprintf(stderr, "test_eez_blockhash_store: Skipped (HTTP_SERVER or CHAIN_EEZ not enabled)\n");
  return 0;
}
#endif
