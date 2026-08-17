/*
 * Unit tests for the verifier-side verified header cache (EL_HEADER_CACHE)
 * and the blockHash variant of c4_verify_block.
 */

#include "unity.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "chains/eth/prover/beacon.h"
#include "chains/eth/prover/eth_tools.h"
#include "chains/eth/verifier/el_header.h"
#include "chains/eth/verifier/eth_verify.h"
#include "chains/eth/verifier/header_cache.h"
#include "crypto.h"
#include "util/state.h"

void setUp(void) {
  c4_header_cache_clear();
}

void tearDown(void) {
  c4_header_cache_clear();
}

#ifdef EL_HEADER_CACHE

#define TEST_CHAIN_ID ((chain_id_t) 1)

// fills a deterministic pseudo block hash for entry i
static void make_hash(bytes32_t out, uint64_t i) {
  memset(out, 0, 32);
  memcpy(out, &i, sizeof(uint64_t));
  out[31] = 0xbb;
}

// creates a small deterministic pseudo RLP header payload for entry i
static bytes_t make_header(uint8_t* buf, uint32_t len, uint64_t i) {
  for (uint32_t n = 0; n < len; n++) buf[n] = (uint8_t) (i + n);
  return bytes(buf, len);
}

void test_put_and_get_el_header_roundtrip(void) {
  bytes32_t hash = {0};
  uint8_t   hdr_buf[64];
  make_hash(hash, 1);
  bytes_t hdr = make_header(hdr_buf, sizeof(hdr_buf), 1);

  TEST_ASSERT_FALSE(c4_header_cache_has_el_header(TEST_CHAIN_ID, hash));

  c4_header_cache_put_el_header(TEST_CHAIN_ID, 100, hash, hdr);

  bytes_t cached = c4_header_cache_get_el_header(TEST_CHAIN_ID, hash);
  TEST_ASSERT_NOT_NULL(cached.data);
  TEST_ASSERT_EQUAL_UINT32(hdr.len, cached.len);
  TEST_ASSERT_EQUAL_MEMORY(hdr.data, cached.data, hdr.len);
  // the getter returns an owned copy, never the caller's buffer
  TEST_ASSERT_TRUE(cached.data != hdr.data);
  safe_free(cached.data);

  // a different chain id must miss
  TEST_ASSERT_FALSE(c4_header_cache_has_el_header((chain_id_t) 11155111, hash));
}

void test_merge_header_data_and_el_header(void) {
  bytes32_t hash = {0};
  uint8_t   hdr_buf[32];
  uint8_t   data_buf[16] = {1, 2, 3, 4};
  make_hash(hash, 2);

  // first the SSZ header_data (hybrid header path), then the RLP header for the same block
  c4_header_cache_put(TEST_CHAIN_ID, 200, hash, (ssz_ob_t) {.bytes = bytes(data_buf, sizeof(data_buf))});
  c4_header_cache_put_el_header(TEST_CHAIN_ID, 200, hash, make_header(hdr_buf, sizeof(hdr_buf), 2));

  // both must live on the same entry
  const verified_header_entry_t* entry = c4_header_cache_get_by_hash(TEST_CHAIN_ID, hash);
  TEST_ASSERT_NOT_NULL(entry);
  TEST_ASSERT_EQUAL_UINT64(200, entry->block_number);
  TEST_ASSERT_NOT_NULL(entry->header_data.bytes.data);
  TEST_ASSERT_NOT_NULL(entry->el_header.data);
  TEST_ASSERT_TRUE(c4_header_cache_has_el_header(TEST_CHAIN_ID, hash));
  TEST_ASSERT_EQUAL_PTR(entry, c4_header_cache_get_by_number(TEST_CHAIN_ID, 200));
}

void test_reorg_resets_entry(void) {
  bytes32_t hash_a = {0}, hash_b = {0};
  uint8_t   hdr_buf[32];
  uint8_t   data_buf[16] = {9, 9, 9};
  make_hash(hash_a, 3);
  make_hash(hash_b, 4);

  c4_header_cache_put(TEST_CHAIN_ID, 300, hash_a, (ssz_ob_t) {.bytes = bytes(data_buf, sizeof(data_buf))});
  c4_header_cache_put_el_header(TEST_CHAIN_ID, 300, hash_a, make_header(hdr_buf, sizeof(hdr_buf), 3));

  // same block number, different hash (reorg): all fields of the old block must be dropped
  c4_header_cache_put_el_header(TEST_CHAIN_ID, 300, hash_b, make_header(hdr_buf, sizeof(hdr_buf), 4));

  TEST_ASSERT_FALSE(c4_header_cache_has_el_header(TEST_CHAIN_ID, hash_a));
  TEST_ASSERT_TRUE(c4_header_cache_has_el_header(TEST_CHAIN_ID, hash_b));
  // header_data belonged to the old block and must be gone
  TEST_ASSERT_NULL(c4_header_cache_get_by_number(TEST_CHAIN_ID, 300));
}

void test_lru_touch_protects_from_eviction(void) {
  bytes32_t hash = {0};
  uint8_t   hdr_buf[16];

  // fill the cache completely (entries 0 .. HEADER_CACHE_SIZE-1)
  for (uint64_t i = 0; i < HEADER_CACHE_SIZE; i++) {
    make_hash(hash, 1000 + i);
    c4_header_cache_put_el_header(TEST_CHAIN_ID, 1000 + i, hash, make_header(hdr_buf, sizeof(hdr_buf), i));
  }

  // touch the oldest entry (LRU-touch on hit)
  make_hash(hash, 1000);
  TEST_ASSERT_TRUE(c4_header_cache_has_el_header(TEST_CHAIN_ID, hash));

  // inserting one more evicts the least recently used entry, which is now entry 1001
  make_hash(hash, 5000);
  c4_header_cache_put_el_header(TEST_CHAIN_ID, 5000, hash, make_header(hdr_buf, sizeof(hdr_buf), 99));

  make_hash(hash, 1000); // touched entry survived
  TEST_ASSERT_TRUE(c4_header_cache_has_el_header(TEST_CHAIN_ID, hash));
  make_hash(hash, 1001); // untouched oldest entry was evicted
  TEST_ASSERT_FALSE(c4_header_cache_has_el_header(TEST_CHAIN_ID, hash));
  make_hash(hash, 5000); // new entry is present
  TEST_ASSERT_TRUE(c4_header_cache_has_el_header(TEST_CHAIN_ID, hash));
}

// the union variant def used by ETH_BLOCK_PROOF_UNION for cache-only references
static const ssz_def_t BLOCK_HASH_VARIANT = SSZ_BYTES32("blockHash");

void test_verify_block_blockhash_variant(void) {
  bytes32_t hash = {0};
  uint8_t   hdr_buf[48];
  make_hash(hash, 7);
  bytes_t hdr = make_header(hdr_buf, sizeof(hdr_buf), 7);

  verify_ctx_t ctx = {0};
  ctx.chain_id     = TEST_CHAIN_ID;

  ssz_ob_t  block      = {.def = &BLOCK_HASH_VARIANT, .bytes = bytes(hash, 32)};
  bytes_t   el_header  = {0};
  bytes32_t block_hash = {0};

  // cache miss: must fail with a clear error (docking point for the future fallback)
  TEST_ASSERT_EQUAL(C4_ERROR, c4_verify_block(&ctx, block, &el_header, block_hash));
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  c4_state_free(&ctx.state);

  // after the header was verified and cached, the same proof must succeed
  c4_header_cache_put_el_header(TEST_CHAIN_ID, 700, hash, hdr);
  memset(&ctx, 0, sizeof(ctx));
  ctx.chain_id = TEST_CHAIN_ID;
  TEST_ASSERT_EQUAL(C4_SUCCESS, c4_verify_block(&ctx, block, &el_header, block_hash));
  TEST_ASSERT_EQUAL_UINT32(hdr.len, el_header.len);
  TEST_ASSERT_EQUAL_MEMORY(hdr.data, el_header.data, hdr.len);
  TEST_ASSERT_EQUAL_MEMORY(hash, block_hash, 32);

  // the copy is attached to the state as a C4_DATA_TYPE_CACHE snapshot (freed with the ctx)
  data_request_t* snapshot = c4_state_get_data_request_by_id(&ctx.state, hash);
  TEST_ASSERT_NOT_NULL(snapshot);
  TEST_ASSERT_EQUAL(C4_DATA_TYPE_CACHE, snapshot->type);
  TEST_ASSERT_EQUAL_PTR(snapshot->response.data, el_header.data);

  // a second verification within the same ctx must reuse the snapshot instead of copying again
  bytes_t el_header2 = {0};
  TEST_ASSERT_EQUAL(C4_SUCCESS, c4_verify_block(&ctx, block, &el_header2, block_hash));
  TEST_ASSERT_EQUAL_PTR(el_header.data, el_header2.data);

  c4_state_free(&ctx.state);
}

// -- Additional cache API edge cases --

void test_set_execution_merges_into_existing_entry(void) {
  bytes32_t hash = {0};
  uint8_t   data_buf[16] = {1, 2, 3};
  uint8_t   exec_buf[24] = {7, 7, 7};
  make_hash(hash, 20);

  c4_header_cache_put(TEST_CHAIN_ID, 2000, hash, (ssz_ob_t) {.bytes = bytes(data_buf, sizeof(data_buf))});
  c4_header_cache_set_execution(TEST_CHAIN_ID, 2000, hash, (ssz_ob_t) {.bytes = bytes(exec_buf, sizeof(exec_buf))});

  const verified_header_entry_t* entry = c4_header_cache_get_by_number(TEST_CHAIN_ID, 2000);
  TEST_ASSERT_NOT_NULL(entry);
  TEST_ASSERT_EQUAL_UINT32(sizeof(exec_buf), entry->execution.bytes.len);
  TEST_ASSERT_EQUAL_MEMORY(exec_buf, entry->execution.bytes.data, sizeof(exec_buf));
  // the cache must own a copy, not the caller's buffer
  TEST_ASSERT_TRUE(entry->execution.bytes.data != exec_buf);

  // replacing the execution payload must keep the entry consistent (old one is freed)
  uint8_t exec_buf2[8] = {9, 9};
  c4_header_cache_set_execution(TEST_CHAIN_ID, 2000, hash, (ssz_ob_t) {.bytes = bytes(exec_buf2, sizeof(exec_buf2))});
  entry = c4_header_cache_get_by_number(TEST_CHAIN_ID, 2000);
  TEST_ASSERT_NOT_NULL(entry);
  TEST_ASSERT_EQUAL_UINT32(sizeof(exec_buf2), entry->execution.bytes.len);
  TEST_ASSERT_EQUAL_MEMORY(exec_buf2, entry->execution.bytes.data, sizeof(exec_buf2));
}

void test_set_execution_without_entry_is_noop(void) {
  bytes32_t hash        = {0};
  uint8_t   exec_buf[8] = {1};
  make_hash(hash, 26);
  // no entry for this block number exists: must not create one and must not crash
  c4_header_cache_set_execution(TEST_CHAIN_ID, 999999, hash, (ssz_ob_t) {.bytes = bytes(exec_buf, sizeof(exec_buf))});
  TEST_ASSERT_NULL(c4_header_cache_get_by_number(TEST_CHAIN_ID, 999999));
}

void test_set_execution_requires_matching_hash(void) {
  bytes32_t hash = {0}, other_hash = {0};
  uint8_t   data_buf[8]  = {1};
  uint8_t   exec_buf[16] = {5, 5, 5};
  make_hash(hash, 27);
  make_hash(other_hash, 28);

  c4_header_cache_put(TEST_CHAIN_ID, 2700, hash, (ssz_ob_t) {.bytes = bytes(data_buf, sizeof(data_buf))});

  // same block number but a different hash (stale entry after a same-height reorg):
  // the payload must NOT be attached to the mismatching entry
  c4_header_cache_set_execution(TEST_CHAIN_ID, 2700, other_hash, (ssz_ob_t) {.bytes = bytes(exec_buf, sizeof(exec_buf))});

  const verified_header_entry_t* entry = c4_header_cache_get_by_number(TEST_CHAIN_ID, 2700);
  TEST_ASSERT_NOT_NULL(entry);
  TEST_ASSERT_NULL(entry->execution.bytes.data);
}

void test_put_overwrites_existing_payloads(void) {
  bytes32_t hash = {0};
  uint8_t   data_a[8] = {1, 1, 1};
  uint8_t   data_b[12] = {2, 2, 2};
  uint8_t   hdr_a[16];
  uint8_t   hdr_b[24];
  make_hash(hash, 21);

  c4_header_cache_put(TEST_CHAIN_ID, 2100, hash, (ssz_ob_t) {.bytes = bytes(data_a, sizeof(data_a))});
  c4_header_cache_put(TEST_CHAIN_ID, 2100, hash, (ssz_ob_t) {.bytes = bytes(data_b, sizeof(data_b))});

  const verified_header_entry_t* entry = c4_header_cache_get_by_number(TEST_CHAIN_ID, 2100);
  TEST_ASSERT_NOT_NULL(entry);
  TEST_ASSERT_EQUAL_UINT32(sizeof(data_b), entry->header_data.bytes.len);
  TEST_ASSERT_EQUAL_MEMORY(data_b, entry->header_data.bytes.data, sizeof(data_b));

  c4_header_cache_put_el_header(TEST_CHAIN_ID, 2100, hash, make_header(hdr_a, sizeof(hdr_a), 1));
  bytes_t hdr_second = make_header(hdr_b, sizeof(hdr_b), 2);
  c4_header_cache_put_el_header(TEST_CHAIN_ID, 2100, hash, hdr_second);

  bytes_t cached = c4_header_cache_get_el_header(TEST_CHAIN_ID, hash);
  TEST_ASSERT_EQUAL_UINT32(hdr_second.len, cached.len);
  TEST_ASSERT_EQUAL_MEMORY(hdr_second.data, cached.data, hdr_second.len);
  safe_free(cached.data);
}

void test_rejects_invalid_arguments(void) {
  bytes32_t hash = {0};
  uint8_t   data_buf[8] = {1};
  make_hash(hash, 22);

  // NULL hash / NULL payloads / empty el_header must all be ignored
  c4_header_cache_put(TEST_CHAIN_ID, 2200, NULL, (ssz_ob_t) {.bytes = bytes(data_buf, sizeof(data_buf))});
  c4_header_cache_put(TEST_CHAIN_ID, 2200, hash, (ssz_ob_t) {0});
  c4_header_cache_put_el_header(TEST_CHAIN_ID, 2200, hash, NULL_BYTES);
  c4_header_cache_put_el_header(TEST_CHAIN_ID, 2200, hash, bytes(data_buf, 0));
  c4_header_cache_set_execution(TEST_CHAIN_ID, 2200, hash, (ssz_ob_t) {0});
  c4_header_cache_set_execution(TEST_CHAIN_ID, 2200, NULL, (ssz_ob_t) {.bytes = bytes(data_buf, sizeof(data_buf))});

  TEST_ASSERT_NULL(c4_header_cache_get_by_number(TEST_CHAIN_ID, 2200));
  TEST_ASSERT_NULL(c4_header_cache_get_by_hash(TEST_CHAIN_ID, hash));
  TEST_ASSERT_FALSE(c4_header_cache_has_el_header(TEST_CHAIN_ID, hash));
}

void test_lookups_require_matching_payload(void) {
  bytes32_t hash_el = {0}, hash_data = {0};
  uint8_t   hdr_buf[16];
  uint8_t   data_buf[8] = {5};
  make_hash(hash_el, 23);
  make_hash(hash_data, 24);

  // entry with only the RLP el_header: get_by_number/get_by_hash must miss (they promise header_data)
  c4_header_cache_put_el_header(TEST_CHAIN_ID, 2300, hash_el, make_header(hdr_buf, sizeof(hdr_buf), 1));
  TEST_ASSERT_NULL(c4_header_cache_get_by_number(TEST_CHAIN_ID, 2300));
  TEST_ASSERT_NULL(c4_header_cache_get_by_hash(TEST_CHAIN_ID, hash_el));
  TEST_ASSERT_TRUE(c4_header_cache_has_el_header(TEST_CHAIN_ID, hash_el));

  // entry with only header_data: get_el_header must miss
  c4_header_cache_put(TEST_CHAIN_ID, 2400, hash_data, (ssz_ob_t) {.bytes = bytes(data_buf, sizeof(data_buf))});
  TEST_ASSERT_FALSE(c4_header_cache_has_el_header(TEST_CHAIN_ID, hash_data));
  TEST_ASSERT_NOT_NULL(c4_header_cache_get_by_number(TEST_CHAIN_ID, 2400));
}

void test_clear_resets_cache(void) {
  bytes32_t hash = {0};
  uint8_t   hdr_buf[16];
  uint8_t   data_buf[8] = {3};
  make_hash(hash, 25);

  c4_header_cache_put(TEST_CHAIN_ID, 2500, hash, (ssz_ob_t) {.bytes = bytes(data_buf, sizeof(data_buf))});
  c4_header_cache_put_el_header(TEST_CHAIN_ID, 2500, hash, make_header(hdr_buf, sizeof(hdr_buf), 1));

  c4_header_cache_clear();

  TEST_ASSERT_NULL(c4_header_cache_get_by_number(TEST_CHAIN_ID, 2500));
  TEST_ASSERT_NULL(c4_header_cache_get_by_hash(TEST_CHAIN_ID, hash));
  TEST_ASSERT_FALSE(c4_header_cache_has_el_header(TEST_CHAIN_ID, hash));

  // the cache must remain fully usable after a clear
  c4_header_cache_put_el_header(TEST_CHAIN_ID, 2500, hash, make_header(hdr_buf, sizeof(hdr_buf), 2));
  TEST_ASSERT_TRUE(c4_header_cache_has_el_header(TEST_CHAIN_ID, hash));
}

void test_full_cache_put_merges_without_eviction(void) {
  bytes32_t hash = {0};
  uint8_t   hdr_buf[16];
  uint8_t   data_buf[8] = {4};

  for (uint64_t i = 0; i < HEADER_CACHE_SIZE; i++) {
    make_hash(hash, 3000 + i);
    c4_header_cache_put_el_header(TEST_CHAIN_ID, 3000 + i, hash, make_header(hdr_buf, sizeof(hdr_buf), i));
  }

  // adding header_data for an already cached block must merge, not evict anything
  make_hash(hash, 3000 + 5);
  c4_header_cache_put(TEST_CHAIN_ID, 3000 + 5, hash, (ssz_ob_t) {.bytes = bytes(data_buf, sizeof(data_buf))});

  const verified_header_entry_t* entry = c4_header_cache_get_by_number(TEST_CHAIN_ID, 3000 + 5);
  TEST_ASSERT_NOT_NULL(entry);
  TEST_ASSERT_NOT_NULL(entry->el_header.data); // merged: el_header survived the put

  for (uint64_t i = 0; i < HEADER_CACHE_SIZE; i++) {
    make_hash(hash, 3000 + i);
    TEST_ASSERT_TRUE_MESSAGE(c4_header_cache_has_el_header(TEST_CHAIN_ID, hash),
                             "an entry was evicted although the put should have merged");
  }
}

void test_reorg_in_full_cache_reuses_freed_slot(void) {
  bytes32_t hash = {0}, new_hash = {0};
  uint8_t   hdr_buf[16];

  for (uint64_t i = 0; i < HEADER_CACHE_SIZE; i++) {
    make_hash(hash, 4000 + i);
    c4_header_cache_put_el_header(TEST_CHAIN_ID, 4000 + i, hash, make_header(hdr_buf, sizeof(hdr_buf), i));
  }

  // reorg on one block in a full cache: the reset slot must be reused instead of
  // evicting an unrelated (LRU) entry
  make_hash(new_hash, 9000);
  c4_header_cache_put_el_header(TEST_CHAIN_ID, 4000 + 7, new_hash, make_header(hdr_buf, sizeof(hdr_buf), 99));

  make_hash(hash, 4000 + 7); // old hash of the reorged block is gone
  TEST_ASSERT_FALSE(c4_header_cache_has_el_header(TEST_CHAIN_ID, hash));
  TEST_ASSERT_TRUE(c4_header_cache_has_el_header(TEST_CHAIN_ID, new_hash));

  for (uint64_t i = 0; i < HEADER_CACHE_SIZE; i++) {
    if (i == 7) continue;
    make_hash(hash, 4000 + i);
    TEST_ASSERT_TRUE_MESSAGE(c4_header_cache_has_el_header(TEST_CHAIN_ID, hash),
                             "an unrelated entry was evicted during a reorg replacement");
  }
}

void test_latest_block_hash_returns_newest_entry(void) {
  bytes32_t hash = {0}, latest = {0};
  uint8_t   hdr_buf[16];
  uint8_t   data_buf[8] = {1};

  // empty cache: no hash to advertise
  TEST_ASSERT_FALSE(c4_header_cache_latest_block_hash(TEST_CHAIN_ID, latest));

  // insert out of order: the highest block number must win, not the most recently used
  make_hash(hash, 52);
  c4_header_cache_put_el_header(TEST_CHAIN_ID, 5200, hash, make_header(hdr_buf, sizeof(hdr_buf), 2));
  make_hash(hash, 51);
  c4_header_cache_put_el_header(TEST_CHAIN_ID, 5100, hash, make_header(hdr_buf, sizeof(hdr_buf), 1));

  // an entry without el_header must be ignored even if its block number is higher
  make_hash(hash, 53);
  c4_header_cache_put(TEST_CHAIN_ID, 5300, hash, (ssz_ob_t) {.bytes = bytes(data_buf, sizeof(data_buf))});

  TEST_ASSERT_TRUE(c4_header_cache_latest_block_hash(TEST_CHAIN_ID, latest));
  make_hash(hash, 52);
  TEST_ASSERT_EQUAL_MEMORY(hash, latest, 32);

  // a different chain must not see these entries
  TEST_ASSERT_FALSE(c4_header_cache_latest_block_hash((chain_id_t) 11155111, latest));
}

// -- c4_verify_block: blockHash union variant validation --

void test_verify_block_rejects_invalid_hash_length(void) {
  uint8_t short_hash[31] = {0};

  verify_ctx_t ctx = {0};
  ctx.chain_id     = TEST_CHAIN_ID;

  ssz_ob_t  block      = {.def = &BLOCK_HASH_VARIANT, .bytes = bytes(short_hash, sizeof(short_hash))};
  bytes_t   el_header  = {0};
  bytes32_t block_hash = {0};

  TEST_ASSERT_EQUAL(C4_ERROR, c4_verify_block(&ctx, block, &el_header, block_hash));
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  c4_state_free(&ctx.state);
}

// -- eth_add_block_proof: blockHash union variant emission --

// minimal container mirroring the "block" union field of the tx/receipt proofs
static const ssz_def_t TEST_BLOCK_UNION[]  = {SSZ_BYTES32("blockHash")};
static const ssz_def_t TEST_PROOF_FIELDS[] = {SSZ_UNION("block", TEST_BLOCK_UNION)};
static const ssz_def_t TEST_PROOF_DEF      = SSZ_CONTAINER("TestProof", TEST_PROOF_FIELDS);

// runs eth_add_block_proof and asserts the emitted union variant is blockHash with the expected hash
static void assert_blockhash_variant(prover_ctx_t* ctx, beacon_block_t* block_data, const uint8_t* expected_hash) {
  ssz_builder_t builder = ssz_builder_for_def(&TEST_PROOF_DEF);
  eth_add_block_proof(ctx, &builder, block_data, NULL);

  // ssz_get resolves union fields directly: def points to the selected variant
  ssz_ob_t ob      = ssz_builder_to_bytes(&builder);
  ssz_ob_t variant = ssz_get(&ob, "block");
  TEST_ASSERT_NOT_NULL(variant.def);
  TEST_ASSERT_EQUAL_STRING("blockHash", variant.def->name);
  TEST_ASSERT_EQUAL_UINT32(32, variant.bytes.len);
  TEST_ASSERT_EQUAL_MEMORY(expected_hash, variant.bytes.data, 32);
  safe_free(ob.bytes.data);
}

void test_add_block_proof_blockhash_variant_for_last_block_hash(void) {
  bytes32_t hash = {0};
  make_hash(hash, 30);

  prover_ctx_t ctx = {0};
  ctx.chain_id     = TEST_CHAIN_ID;
  memcpy(ctx.last_block_hash, hash, 32);

  beacon_block_t block_data = {0};
  memcpy(block_data.el_block_hash, hash, 32);

  assert_blockhash_variant(&ctx, &block_data, hash);
  c4_state_free(&ctx.state);
}

void test_add_block_proof_blockhash_variant_for_hybrid_cache_hit(void) {
  bytes32_t hash = {0};
  uint8_t   hdr_buf[32];
  make_hash(hash, 31);

  // hybrid mode: the verifier cache already holds the verified header for this block
  c4_header_cache_put_el_header(TEST_CHAIN_ID, 3100, hash, make_header(hdr_buf, sizeof(hdr_buf), 1));

  prover_ctx_t ctx = {0};
  ctx.chain_id     = TEST_CHAIN_ID;
  ctx.flags        = C4_PROVER_FLAG_HYBRID; // last_block_hash stays zero: only the cache justifies the variant

  beacon_block_t block_data = {0};
  memcpy(block_data.el_block_hash, hash, 32);

  assert_blockhash_variant(&ctx, &block_data, hash);
  c4_state_free(&ctx.state);
}

// -- c4_hybrid_ensure_el_header --

// minimal execution payload stand-in with exactly the fields the function reads
static const ssz_def_t FAKE_EXECUTION_FIELDS[] = {
    SSZ_UINT64("blockNumber"),
    SSZ_BYTES32("blockHash"),
};
static const ssz_def_t FAKE_EXECUTION_DEF = SSZ_CONTAINER("FakeExecution", FAKE_EXECUTION_FIELDS);

static ssz_ob_t make_fake_execution(uint8_t* buf40, uint64_t block_number, const uint8_t* block_hash) {
  memset(buf40, 0, 40);
  for (int i = 0; i < 8; i++) buf40[i] = (uint8_t) (block_number >> (8 * i));
  memcpy(buf40 + 8, block_hash, 32);
  return (ssz_ob_t) {.def = &FAKE_EXECUTION_DEF, .bytes = bytes(buf40, 40)};
}

// a deterministic minimal block JSON; fields not present are RLP-encoded as zero/empty,
// which is fine since the tests derive the expected hash from the very same JSON.
static char BLOCK_JSON[] = "{\"parentHash\":\"0x1111111111111111111111111111111111111111111111111111111111111111\","
                           "\"feeRecipient\":\"0x2222222222222222222222222222222222222222\","
                           "\"stateRoot\":\"0x3333333333333333333333333333333333333333333333333333333333333333\","
                           "\"blockNumber\":\"0x64\",\"gasLimit\":\"0x1c9c380\",\"gasUsed\":\"0x5208\","
                           "\"timestamp\":\"0x66aabbcc\",\"extraData\":\"0x636f6c69627269\","
                           "\"prevRandao\":\"0x4444444444444444444444444444444444444444444444444444444444444444\","
                           "\"baseFeePerGas\":\"0x3b9aca00\"}";

// builds the RLP header for BLOCK_JSON the same way the prover does and returns keccak(rlp)
static bytes_t expected_header_for_block_json(bytes32_t out_hash) {
  c4_state_t tmp_state = {0};
  bytes_t    el_header = {0};
  TEST_ASSERT_EQUAL(C4_SUCCESS, eth_el_header_build_from_json(&tmp_state, &el_header, C4_FORK_DENEB, json_parse(BLOCK_JSON)));
  TEST_ASSERT_NOT_NULL(el_header.data);
  keccak(el_header, out_hash);
  c4_state_free(&tmp_state);
  return el_header;
}

// sets a JSON-RPC response on the pending request (heap copy incl. terminating NUL)
static void set_json_response(data_request_t* req, const char* result_json) {
  char resp[1024];
  int  len = snprintf(resp, sizeof(resp), "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":%s}", result_json);
  TEST_ASSERT_TRUE(len > 0 && len < (int) sizeof(resp));
  req->response = bytes_dup(bytes((uint8_t*) resp, (uint32_t) len + 1));
}

// the same block as BLOCK_JSON but with the field names a standard eth_getBlockByHash
// response uses (miner/mixHash/number instead of feeRecipient/prevRandao/blockNumber)
static char RPC_BLOCK_JSON[] = "{\"parentHash\":\"0x1111111111111111111111111111111111111111111111111111111111111111\","
                               "\"miner\":\"0x2222222222222222222222222222222222222222\","
                               "\"stateRoot\":\"0x3333333333333333333333333333333333333333333333333333333333333333\","
                               "\"number\":\"0x64\",\"gasLimit\":\"0x1c9c380\",\"gasUsed\":\"0x5208\","
                               "\"timestamp\":\"0x66aabbcc\",\"extraData\":\"0x636f6c69627269\","
                               "\"mixHash\":\"0x4444444444444444444444444444444444444444444444444444444444444444\","
                               "\"baseFeePerGas\":\"0x3b9aca00\"}";

void test_el_header_build_maps_rpc_field_names(void) {
  bytes32_t hash     = {0};
  bytes_t   expected = expected_header_for_block_json(hash);

  c4_state_t st  = {0};
  bytes_t    hdr = {0};
  TEST_ASSERT_EQUAL(C4_SUCCESS, eth_el_header_build_from_json(&st, &hdr, C4_FORK_DENEB, json_parse(RPC_BLOCK_JSON)));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected.len, hdr.len,
                                   "RPC naming (miner/mixHash/number) must produce the identical RLP header");
  TEST_ASSERT_EQUAL_MEMORY(expected.data, hdr.data, expected.len);

  safe_free(hdr.data);
  safe_free(expected.data);
  c4_state_free(&st);
}

void test_hybrid_ensure_el_header_cache_hit_short_circuits(void) {
  bytes32_t hash = {0};
  uint8_t   hdr_buf[32];
  uint8_t   exec_buf[40];
  make_hash(hash, 40);

  c4_header_cache_put_el_header(TEST_CHAIN_ID, 4000, hash, make_header(hdr_buf, sizeof(hdr_buf), 1));

  prover_ctx_t ctx = {0};
  ctx.chain_id     = TEST_CHAIN_ID;
  ctx.flags        = C4_PROVER_FLAG_HYBRID;

  ssz_ob_t execution = make_fake_execution(exec_buf, 4000, hash);
  TEST_ASSERT_EQUAL(C4_SUCCESS, c4_hybrid_ensure_el_header(&ctx, execution));
  // no RPC request may be issued on a cache hit
  TEST_ASSERT_NULL(c4_state_get_pending_request(&ctx.state));
  c4_state_free(&ctx.state);
}

void test_hybrid_ensure_el_header_requires_block_hash(void) {
  // execution payload without a blockHash field must be rejected
  static const ssz_def_t NO_HASH_FIELDS[] = {SSZ_UINT64("blockNumber")};
  static const ssz_def_t NO_HASH_DEF      = SSZ_CONTAINER("NoHash", NO_HASH_FIELDS);

  uint8_t  buf8[8]   = {0};
  ssz_ob_t execution = {.def = &NO_HASH_DEF, .bytes = bytes(buf8, 8)};

  prover_ctx_t ctx = {0};
  ctx.chain_id     = TEST_CHAIN_ID;

  TEST_ASSERT_EQUAL(C4_ERROR, c4_hybrid_ensure_el_header(&ctx, execution));
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  c4_state_free(&ctx.state);
}

void test_hybrid_ensure_el_header_fetches_and_caches_verified_header(void) {
  bytes32_t hash         = {0};
  uint8_t   exec_buf[40] = {0};
  bytes_t   expected_hdr = expected_header_for_block_json(hash);

  prover_ctx_t ctx = {0};
  ctx.chain_id     = TEST_CHAIN_ID;
  ctx.flags        = C4_PROVER_FLAG_HYBRID;

  ssz_ob_t execution = make_fake_execution(exec_buf, 100, hash);

  // cache miss: the block must be requested via eth_getBlockByHash for the verified hash
  TEST_ASSERT_EQUAL(C4_PENDING, c4_hybrid_ensure_el_header(&ctx, execution));
  data_request_t* req = c4_state_get_pending_request(&ctx.state);
  TEST_ASSERT_NOT_NULL(req);
  TEST_ASSERT_NOT_NULL(req->payload.data);
  TEST_ASSERT_NOT_NULL(strstr((char*) req->payload.data, "eth_getBlockByHash"));
  char expected_params[90];
  {
    buffer_t b = stack_buffer(expected_params);
    bprintf(&b, "[\"0x%x\",false]", bytes(hash, 32));
  }
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr((char*) req->payload.data, expected_params),
                               "the request must query the verified block hash");

  // answer with the block whose rebuilt RLP header matches the verified hash
  set_json_response(req, BLOCK_JSON);
  TEST_ASSERT_EQUAL_MESSAGE(C4_SUCCESS, c4_hybrid_ensure_el_header(&ctx, execution),
                            ctx.state.error ? ctx.state.error : "expected C4_SUCCESS");

  bytes_t cached = c4_header_cache_get_el_header(TEST_CHAIN_ID, hash);
  TEST_ASSERT_NOT_NULL(cached.data);
  TEST_ASSERT_EQUAL_UINT32(expected_hdr.len, cached.len);
  TEST_ASSERT_EQUAL_MEMORY(expected_hdr.data, cached.data, expected_hdr.len);

  safe_free(cached.data);
  safe_free(expected_hdr.data);
  c4_state_free(&ctx.state);
}

void test_hybrid_ensure_el_header_rejects_mismatching_rpc_block(void) {
  bytes32_t wrong_hash   = {0};
  uint8_t   exec_buf[40] = {0};
  memset(wrong_hash, 0xaa, 32); // no block JSON hashes to this

  prover_ctx_t ctx = {0};
  ctx.chain_id     = TEST_CHAIN_ID;
  ctx.flags        = C4_PROVER_FLAG_HYBRID;

  ssz_ob_t execution = make_fake_execution(exec_buf, 100, wrong_hash);

  TEST_ASSERT_EQUAL(C4_PENDING, c4_hybrid_ensure_el_header(&ctx, execution));
  data_request_t* req = c4_state_get_pending_request(&ctx.state);
  TEST_ASSERT_NOT_NULL(req);

  // the RPC (untrusted) returns a block whose header does not hash to the verified block hash
  set_json_response(req, BLOCK_JSON);
  TEST_ASSERT_EQUAL(C4_ERROR, c4_hybrid_ensure_el_header(&ctx, execution));
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(ctx.state.error, "does not match"),
                               "error must report the block hash mismatch");
  // nothing may be cached from the mismatching response
  TEST_ASSERT_FALSE(c4_header_cache_has_el_header(TEST_CHAIN_ID, wrong_hash));
  c4_state_free(&ctx.state);
}

void test_hybrid_ensure_el_header_handles_missing_block(void) {
  bytes32_t hash         = {0};
  uint8_t   exec_buf[40] = {0};
  make_hash(hash, 42);

  prover_ctx_t ctx = {0};
  ctx.chain_id     = TEST_CHAIN_ID;
  ctx.flags        = C4_PROVER_FLAG_HYBRID;

  ssz_ob_t execution = make_fake_execution(exec_buf, 4200, hash);

  TEST_ASSERT_EQUAL(C4_PENDING, c4_hybrid_ensure_el_header(&ctx, execution));
  data_request_t* req = c4_state_get_pending_request(&ctx.state);
  TEST_ASSERT_NOT_NULL(req);

  set_json_response(req, "null"); // RPC does not know the block
  TEST_ASSERT_EQUAL(C4_ERROR, c4_hybrid_ensure_el_header(&ctx, execution));
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  c4_state_free(&ctx.state);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_put_and_get_el_header_roundtrip);
  RUN_TEST(test_merge_header_data_and_el_header);
  RUN_TEST(test_reorg_resets_entry);
  RUN_TEST(test_lru_touch_protects_from_eviction);
  RUN_TEST(test_verify_block_blockhash_variant);
  RUN_TEST(test_set_execution_merges_into_existing_entry);
  RUN_TEST(test_set_execution_without_entry_is_noop);
  RUN_TEST(test_set_execution_requires_matching_hash);
  RUN_TEST(test_put_overwrites_existing_payloads);
  RUN_TEST(test_rejects_invalid_arguments);
  RUN_TEST(test_lookups_require_matching_payload);
  RUN_TEST(test_clear_resets_cache);
  RUN_TEST(test_full_cache_put_merges_without_eviction);
  RUN_TEST(test_reorg_in_full_cache_reuses_freed_slot);
  RUN_TEST(test_latest_block_hash_returns_newest_entry);
  RUN_TEST(test_verify_block_rejects_invalid_hash_length);
  RUN_TEST(test_add_block_proof_blockhash_variant_for_last_block_hash);
  RUN_TEST(test_add_block_proof_blockhash_variant_for_hybrid_cache_hit);
  RUN_TEST(test_el_header_build_maps_rpc_field_names);
  RUN_TEST(test_hybrid_ensure_el_header_cache_hit_short_circuits);
  RUN_TEST(test_hybrid_ensure_el_header_requires_block_hash);
  RUN_TEST(test_hybrid_ensure_el_header_fetches_and_caches_verified_header);
  RUN_TEST(test_hybrid_ensure_el_header_rejects_mismatching_rpc_block);
  RUN_TEST(test_hybrid_ensure_el_header_handles_missing_block);
  return UNITY_END();
}

#else // !EL_HEADER_CACHE

int main(void) {
  fprintf(stderr, "test_el_header_cache: Skipped (EL_HEADER_CACHE not enabled)\n");
  return 0;
}

#endif
