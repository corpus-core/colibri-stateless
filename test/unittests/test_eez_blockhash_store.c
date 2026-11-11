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

// Three additional structural failure modes the watcher relies on when discarding a
// corrupted on-disk snapshot: missing hashes tail, over-claimed proof count, and a
// proof-node length that would run past the blob. Each must return NULL and must not
// leak the partially constructed snapshot.
void test_deserialize_structural_failures(void) {
  // Case A: valid header, 0 proof nodes, but the 8191 * 32 hashes tail is missing.
  // Hits the `off + sizeof(bytes32_t) * EEZ_BH_SLOT_COUNT != blob.len` branch.
  uint8_t header_only[184] = {0};
  memcpy(header_only, "EEZB", 4);
  header_only[4] = 0x01; // version LE = 1
  TEST_ASSERT_NULL(eez_bh_deserialize(bytes(header_only, sizeof(header_only))));

  // Case B: proof_count = 1 but zero bytes follow the 184-byte header, so the length
  // prefix read must fail. Hits the `off + 2 > blob.len` branch.
  uint8_t header_count[184] = {0};
  memcpy(header_count, "EEZB", 4);
  header_count[4]   = 0x01;
  header_count[182] = 0x01; // count LE = 1
  TEST_ASSERT_NULL(eez_bh_deserialize(bytes(header_count, sizeof(header_count))));

  // Case C: proof_count = 1 and we have the 2-byte length prefix, but the claimed node
  // length overruns the blob. Hits the `off + nlen > blob.len` branch.
  uint8_t header_overrun[184 + 2 + 3] = {0};
  memcpy(header_overrun, "EEZB", 4);
  header_overrun[4]   = 0x01;
  header_overrun[182] = 0x01;
  // claim a 100-byte node even though only 3 bytes of payload follow
  header_overrun[184] = 0x64;
  header_overrun[185] = 0x00;
  TEST_ASSERT_NULL(eez_bh_deserialize(bytes(header_overrun, sizeof(header_overrun))));
}

// NULL-safety contract of the public API. Both calls must be no-ops; a caller holding a
// freed or never-initialized snapshot pointer must not crash.
void test_null_safety(void) {
  eez_bh_serialize(NULL, NULL);
  buffer_t buf = {0};
  eez_bh_serialize(NULL, &buf); // must not append anything and must not crash
  TEST_ASSERT_EQUAL_UINT32(0, buf.data.len);
  buffer_free(&buf);
  eez_bh_snapshot_free(NULL);
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
  req.error = NULL;

  // Slot index exactly at `EEZ_BH_SLOT_COUNT` (8191) must be rejected as out of range.
  // The existing "999999" case already exercises a very large value; this one checks the
  // boundary to catch an off-by-one in the `>=` comparison.
  req.url        = "eez_blockhash/100/8191";
  finished_count = 0;
  TEST_ASSERT_TRUE(c4_handle_eez_blockhash(&r));
  TEST_ASSERT_NOT_NULL(req.error);
  TEST_ASSERT_NOT_NULL(strstr(req.error, "slot"));
  TEST_ASSERT_EQUAL_INT(1, finished_count);
  safe_free(req.error);
  req.error = NULL;

  // Highest valid slot index (`EEZ_BH_SLOT_COUNT - 1` == 8190) must pass parsing and
  // then fall through to the "no snapshot" error because `g_state.active` is NULL.
  // This confirms the boundary parses correctly rather than being rejected as slot
  // out-of-range.
  req.url        = "eez_blockhash/100/8190";
  finished_count = 0;
  TEST_ASSERT_TRUE(c4_handle_eez_blockhash(&r));
  TEST_ASSERT_NOT_NULL(req.error);
  TEST_ASSERT_NOT_NULL(strstr(req.error, "no snapshot"));
  TEST_ASSERT_EQUAL_INT(1, finished_count);
  safe_free(req.error);
  req.error = NULL;

  // Missing slot segment entirely: "eez_blockhash/100" has no '/' after the anchor and
  // must be rejected as malformed anchor path.
  req.url        = "eez_blockhash/100";
  finished_count = 0;
  TEST_ASSERT_TRUE(c4_handle_eez_blockhash(&r));
  TEST_ASSERT_NOT_NULL(req.error);
  TEST_ASSERT_EQUAL_INT(1, finished_count);
  safe_free(req.error);
  req.error = NULL;

  // Trailing garbage in the slot segment ("8/x") must be rejected -- strtoull will
  // leave `slot_end` pointing at 'x', failing `*slot_end != '\0'`.
  req.url        = "eez_blockhash/100/8x";
  finished_count = 0;
  TEST_ASSERT_TRUE(c4_handle_eez_blockhash(&r));
  TEST_ASSERT_NOT_NULL(req.error);
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

// A fresh EEZ chain commits before the EIP-2935 contract has any populated slot. Its
// `storageHash` is the Ethereum empty-trie root `keccak256(rlp(""))`. The watcher must
// accept that snapshot without building any trie node -- otherwise the first anchor on
// a fresh rollup never publishes. Both the positive and negative cases hit the `!any`
// branch of `eez_bh_build_and_verify_trie`, which is unreachable in the round-trip test.
void test_empty_trie_matches_empty_root(void) {
  bytes32_t hashes[EEZ_BH_SLOT_COUNT];
  memset(hashes, 0, sizeof(hashes));

  // Ethereum's canonical empty-trie root: keccak256(rlp("")). Hard-coded in the
  // Yellow Paper and matches the constant in `blockhash_store.c`.
  const uint8_t EMPTY_TRIE_ROOT[32] = {
      0x56, 0xe8, 0x1f, 0x17, 0x1b, 0xcc, 0x55, 0xa6,
      0xff, 0x83, 0x45, 0xe6, 0x92, 0xc0, 0xf8, 0x6e,
      0x5b, 0x48, 0xe0, 0x1b, 0x99, 0x6c, 0xad, 0xc0,
      0x01, 0x62, 0x2f, 0xb5, 0xe3, 0x63, 0xb4, 0x21};

  // Positive: all-zero hashes with the empty-trie root is treated as a valid trie.
  node_t*   trie = (node_t*) 0xdeadbeef; // sentinel: must be reset to NULL by the API
  bytes32_t root;
  memcpy(root, EMPTY_TRIE_ROOT, 32);
  TEST_ASSERT_TRUE(eez_bh_build_and_verify_trie(hashes, root, &trie));
  // The empty-trie branch does NOT return a trie (there is nothing to build), only
  // `true`. Callers that need a trie must check both.
  TEST_ASSERT_NULL(trie);

  // Negative: all-zero hashes with any other root must be rejected.
  bytes32_t wrong_root;
  memcpy(wrong_root, EMPTY_TRIE_ROOT, 32);
  wrong_root[0] ^= 0x01;
  trie = (node_t*) 0xdeadbeef;
  TEST_ASSERT_FALSE(eez_bh_build_and_verify_trie(hashes, wrong_root, &trie));
  TEST_ASSERT_NULL(trie);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_serialize_round_trip);
  RUN_TEST(test_deserialize_rejects_garbage);
  RUN_TEST(test_deserialize_structural_failures);
  RUN_TEST(test_null_safety);
  RUN_TEST(test_internal_handler_rejects_malformed_path);
  RUN_TEST(test_trie_build_and_verify_round_trip);
  RUN_TEST(test_empty_trie_matches_empty_root);
  return UNITY_END();
}

#else
int main(void) {
  fprintf(stderr, "test_eez_blockhash_store: Skipped (HTTP_SERVER or CHAIN_EEZ not enabled)\n");
  return 0;
}
#endif
