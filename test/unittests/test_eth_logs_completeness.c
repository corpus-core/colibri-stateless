/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */
#include "c4_assert.h"

#include "beacon_types.h"
#include "bytes.h"
#include "crypto.h"
#include "el_header.h"
#include "eth_account.h"
#include "eth_bloom.h"
#include "eth_req.h"
#include "eth_tx.h"
#include "eth_verify.h"
#include "json.h"
#include "patricia.h"
#include "proof_logs_completeness.h"
#include "ssz.h"
#include "unity.h"
#include "verify.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// USDT token address, used as a representative filter address for bloom tests.
#define TEST_ADDRESS "0xdac17f958d2ee523a2206206994597c13d831ec7"

void setUp(void) {}
void tearDown(void) {}

// A block bloom that already contains the query bits cannot be proven negative:
// the query variant is a bit-subset, so a matching log might exist.
static void test_bloom_negative_subset_is_not_negative(void) {
  json_t  filter = json_parse("{\"address\":\"" TEST_ADDRESS "\"}");
  bytes_t q      = c4_eth_filter_query_blooms(filter);
  TEST_ASSERT_NOT_NULL(q.data);
  TEST_ASSERT_TRUE(q.len >= 256);
  // block bloom == query bloom -> the (only) variant is a subset -> NOT negative
  TEST_ASSERT_FALSE(c4_eth_bloom_negative(q, bytes(q.data, 256)));
  safe_free(q.data);
}

// An all-zero block bloom cannot contain the query address bits, so the block is
// provably free of any matching log (bloom-negative).
static void test_bloom_negative_zero_block_is_negative(void) {
  json_t  filter    = json_parse("{\"address\":\"" TEST_ADDRESS "\"}");
  bytes_t q         = c4_eth_filter_query_blooms(filter);
  uint8_t zero[256] = {0};
  TEST_ASSERT_TRUE(c4_eth_bloom_negative(q, bytes(zero, 256)));
  safe_free(q.data);
}

// Without any query variants the function must be conservative and return false
// (absence cannot be proven), otherwise a prover could drop all blocks.
static void test_bloom_negative_no_variants(void) {
  uint8_t zero[256] = {0};
  TEST_ASSERT_FALSE(c4_eth_bloom_negative(NULL_BYTES, bytes(zero, 256)));
}

// A block bloom of the wrong length must be rejected conservatively.
static void test_bloom_negative_bad_block_len(void) {
  json_t  filter          = json_parse("{\"address\":\"" TEST_ADDRESS "\"}");
  bytes_t q               = c4_eth_filter_query_blooms(filter);
  uint8_t short_bloom[10] = {0};
  TEST_ASSERT_FALSE(c4_eth_bloom_negative(q, bytes(short_bloom, 10)));
  safe_free(q.data);
}

// PAP mode: a `bloomFilter` array in the filter is used verbatim (no derivation
// from address/topics). The returned blooms must equal the provided entries.
static void test_query_blooms_pap_array(void) {
  json_t  filter_addr = json_parse("{\"address\":\"" TEST_ADDRESS "\"}");
  bytes_t q           = c4_eth_filter_query_blooms(filter_addr);
  TEST_ASSERT_NOT_NULL(q.data);

  buffer_t buf = {0};
  bprintf(&buf, "{\"bloomFilter\":[\"0x%x\"]}", bytes(q.data, 256));
  json_t  pap_filter = json_parse((char*) buf.data.data);
  bytes_t pap        = c4_eth_filter_query_blooms(pap_filter);

  TEST_ASSERT_EQUAL_INT(256, pap.len);
  TEST_ASSERT_EQUAL_MEMORY(q.data, pap.data, 256);

  safe_free(q.data);
  safe_free(pap.data);
  buffer_free(&buf);
}

// A malformed PAP bloom entry (wrong length) must not be treated as valid, so
// the verifier cannot be tricked into a wrong negativity decision.
static void test_query_blooms_pap_malformed(void) {
  json_t  filter = json_parse("{\"bloomFilter\":[\"0x1234\"]}");
  bytes_t pap    = c4_eth_filter_query_blooms(filter);
  TEST_ASSERT_NULL(pap.data);
  TEST_ASSERT_EQUAL_INT(0, pap.len);
}

// Builds a structurally well-shaped LogsCompletenessProof with `header_count` raw EL headers
// (i.e. a range of header_count+1 blocks), a zeroed blockHash block-proof and an empty blocks
// list. The claim (range) comes from the request, so the proof carries no range endpoints; this
// helper only exercises the structural guards that run *before* chain/anchor verification.
static ssz_ob_t build_completeness_proof(uint32_t header_count) {
  const ssz_def_t* def = eth_ssz_verification_type(ETH_SSZ_VERIFY_LOGS_COMPLETENESS_PROOF);
  TEST_ASSERT_NOT_NULL(def);

  ssz_builder_t b = ssz_builder_for_def(def);

  // block: zeroed blockHash variant (selector 0 of ETH_BLOCK_PROOF_UNION + 32-byte hash).
  // Structural guards under test run before c4_verify_block, so the content is irrelevant.
  uint8_t block_hash_union[33] = {0};
  ssz_add_bytes(&b, "block", bytes(block_hash_union, sizeof(block_hash_union)));

  // headers list: header_count empty RLP placeholders (fromBlock .. toBlock-1)
  const ssz_def_t* headers_def = ssz_get_def(def, "headers");
  ssz_builder_t    hlist       = ssz_builder_for_def(headers_def);
  for (uint32_t i = 0; i < header_count; i++)
    ssz_add_dynamic_list_bytes(&hlist, header_count, NULL_BYTES);
  ssz_add_builders(&b, "headers", hlist);

  ssz_add_bytes(&b, "blocks", NULL_BYTES); // empty list
  return ssz_builder_to_bytes(&b);
}

static bool run_verify_ex(ssz_ob_t proof, json_t args, bytes_t cached_hdr, const uint8_t* cached_hash,
                          uint64_t min_latest_ts, c4_state_t* state_out) {
  verify_ctx_t ctx        = {0};
  ctx.chain_id            = C4_CHAIN_MAINNET;
  ctx.proof               = proof;
  ctx.args                = args;
  ctx.min_latest_block_ts = min_latest_ts;

  // Hybrid/blockHash path: c4_verify_block resolves a cached EL header by hash without a CL proof.
  // That lets us unit-test parentHash / receiptsRoot / bloom / freshness after the anchor is trusted.
  if (cached_hdr.data && cached_hash) {
    data_request_t* snap = safe_calloc(1, sizeof(data_request_t));
    snap->chain_id       = C4_CHAIN_MAINNET;
    snap->type           = C4_DATA_TYPE_CACHE;
    snap->encoding       = C4_DATA_ENCODING_SSZ;
    snap->response       = bytes_dup(cached_hdr);
    memcpy(snap->id, cached_hash, 32);
    c4_state_add_request(&ctx.state, snap);
  }

  bool ok = verify_logs_completeness(&ctx);
  if (ok && (ctx.flags & VERIFY_FLAG_FREE_DATA))
    safe_free(ctx.data.bytes.data);
  *state_out = ctx.state;
  return ok;
}

static bool run_verify(ssz_ob_t proof, json_t args, c4_state_t* state_out) {
  return run_verify_ex(proof, args, NULL_BYTES, NULL, 0, state_out);
}

static void assert_verify_error(bool ok, c4_state_t* st, const char* needle) {
  TEST_ASSERT_FALSE_MESSAGE(ok, "expected verification to fail");
  TEST_ASSERT_NOT_NULL_MESSAGE(st->error, "expected an error message");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(st->error, needle), st->error);
}

// Dummy Deneb EL header. Missing hash fields become zeros; receipts/tx roots default to the empty trie.
static bytes_t make_el_header(uint64_t number, const uint8_t* parent_hash, const uint8_t* receipts_root,
                              const uint8_t* tx_root, bytes_t logs_bloom, uint64_t timestamp) {
  uint8_t zero_bloom[256] = {0};
  uint8_t parent[32]      = {0};
  uint8_t rr[32];
  uint8_t tr[32];
  memcpy(rr, receipts_root ? receipts_root : EMPTY_ROOT_HASH, 32);
  memcpy(tr, tx_root ? tx_root : EMPTY_ROOT_HASH, 32);
  if (parent_hash) memcpy(parent, parent_hash, 32);
  if (!logs_bloom.data) logs_bloom = bytes(zero_bloom, 256);

  buffer_t json_buf = {0};
  bprintf(&json_buf,
          "{\"parentHash\":\"0x%x\",\"receiptsRoot\":\"0x%x\",\"transactionsRoot\":\"0x%x\","
          "\"logsBloom\":\"0x%x\",\"number\":\"0x%lx\",\"timestamp\":\"0x%lx\"}",
          bytes(parent, 32), bytes(rr, 32), bytes(tr, 32), logs_bloom, number, timestamp);
  json_t  block = json_parse((char*) json_buf.data.data);
  bytes_t hdr   = {0};
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, eth_el_header_build_from_json(NULL, &hdr, C4_FORK_DENEB, block));
  buffer_free(&json_buf);
  return hdr;
}

static ssz_ob_t build_proof(const uint8_t* anchor_hash, bytes_t* headers, uint32_t header_count,
                            bytes_t* block_elems, uint32_t block_count) {
  const ssz_def_t* def = eth_ssz_verification_type(ETH_SSZ_VERIFY_LOGS_COMPLETENESS_PROOF);
  TEST_ASSERT_NOT_NULL(def);
  ssz_builder_t b = ssz_builder_for_def(def);

  uint8_t block_union[33] = {0}; // selector 0 = blockHash
  memcpy(block_union + 1, anchor_hash, 32);
  ssz_add_bytes(&b, "block", bytes(block_union, sizeof(block_union)));

  if (header_count == 0)
    ssz_add_bytes(&b, "headers", NULL_BYTES);
  else {
    const ssz_def_t* headers_def = ssz_get_def(def, "headers");
    ssz_builder_t    hlist       = ssz_builder_for_def(headers_def);
    for (uint32_t i = 0; i < header_count; i++)
      ssz_add_dynamic_list_bytes(&hlist, header_count, headers[i]);
    ssz_add_builders(&b, "headers", hlist);
  }

  const ssz_def_t* blocks_def = ssz_get_def(def, "blocks");
  ssz_builder_t    blist      = ssz_builder_for_def(blocks_def);
  for (uint32_t i = 0; i < block_count; i++)
    ssz_add_dynamic_list_bytes(&blist, block_count, block_elems[i]);
  ssz_add_builders(&b, "blocks", blist);
  return ssz_builder_to_bytes(&b);
}

static bytes_t encode_full_receipts(bytes_t receipt, bool has_receipt) {
  const ssz_def_t* def        = eth_ssz_verification_type(ETH_SSZ_VERIFY_LOGS_COMPLETENESS_PROOF);
  const ssz_def_t* blocks_def = ssz_get_def(def, "blocks");
  const ssz_def_t* union_def  = blocks_def->def.vector.type;
  const ssz_def_t* full_def   = union_def->def.container.elements + 1;

  ssz_builder_t v = ssz_builder_for_def(full_def);
  if (has_receipt) {
    const ssz_def_t* receipts_def = ssz_get_def(full_def, "receipts");
    ssz_builder_t    rlist        = ssz_builder_for_def(receipts_def);
    ssz_add_dynamic_list_bytes(&rlist, 1, receipt);
    ssz_add_builders(&v, "receipts", rlist);
  }
  else
    ssz_add_bytes(&v, "receipts", NULL_BYTES);
  ssz_add_bytes(&v, "txs", NULL_BYTES);

  ssz_ob_t vbytes = ssz_builder_to_bytes(&v);
  buffer_t elem   = {0};
  uint8_t  sel    = 1; // FullReceipts
  buffer_append(&elem, bytes(&sel, 1));
  buffer_append(&elem, vbytes.bytes);
  safe_free(vbytes.bytes.data);
  return elem.data;
}

// A range wider than VERIFY_LOGS_COMPLETENESS_MAX_BLOCKS must be rejected up front
// (DoS guard), before the chain is reconstructed or the anchor is verified.
static void test_completeness_rejects_oversized_range(void) {
  ssz_ob_t   proof = build_completeness_proof(4096); // count 4097 > 4096
  json_t     args  = json_parse("[{\"fromBlock\":\"0x0\",\"toBlock\":\"0x1388\"}]");
  c4_state_t st    = {0};
  TEST_ASSERT_FALSE(run_verify(proof, args, &st));
  TEST_ASSERT_NOT_NULL(st.error);
  c4_state_free(&st);
  safe_free(proof.bytes.data);
}

// A missing filter object must be rejected (completeness needs the requested range as its claim).
static void test_completeness_rejects_missing_filter(void) {
  ssz_ob_t   proof = build_completeness_proof(0); // single-block range
  json_t     args  = json_parse("[]");
  c4_state_t st    = {0};
  TEST_ASSERT_FALSE(run_verify(proof, args, &st));
  TEST_ASSERT_NOT_NULL(st.error);
  c4_state_free(&st);
  safe_free(proof.bytes.data);
}

// The blocks list length must equal the block count derived from the header chain (headers+1).
static void test_completeness_rejects_block_count_mismatch(void) {
  ssz_ob_t   proof = build_completeness_proof(0); // count 1, but blocks list is empty
  json_t     args  = json_parse("[{\"fromBlock\":\"0x100\",\"toBlock\":\"0x100\"}]");
  c4_state_t st    = {0};
  TEST_ASSERT_FALSE(run_verify(proof, args, &st));
  TEST_ASSERT_NOT_NULL(st.error);
  c4_state_free(&st);
  safe_free(proof.bytes.data);
}

// `safe`/`finalized` are rejected before c4_verify_block (checkpoint binding is not implemented).
static void test_completeness_rejects_unsupported_toblock(void) {
  uint8_t    none      = 0;
  uint8_t    zhash[32] = {0};
  bytes_t    blocks[1] = {bytes(&none, 1)};
  ssz_ob_t   proof     = build_proof(zhash, NULL, 0, blocks, 1);
  json_t     args      = json_parse("[{\"fromBlock\":\"0x1\",\"toBlock\":\"safe\"}]");
  c4_state_t st        = {0};
  assert_verify_error(run_verify(proof, args, &st), &st, "toBlock tag not yet supported");
  c4_state_free(&st);
  safe_free(proof.bytes.data);
}

// Prover-side twin of the verifier guard: `safe` is rejected before any RPC fetch.
static void test_prover_rejects_safe_toblock(void) {
  prover_ctx_t* ctx = c4_prover_create("eth_getLogs",
                                       "[{\"fromBlock\":\"0x1\",\"toBlock\":\"safe\"}]",
                                       C4_CHAIN_MAINNET, C4_PROVER_FLAG_LOGS_COMPLETENESS);
  TEST_ASSERT_NOT_NULL(ctx);
  TEST_ASSERT_EQUAL_INT(C4_ERROR, c4_proof_logs_completeness(ctx));
  TEST_ASSERT_NOT_NULL(ctx->state.error);
  TEST_ASSERT_NOT_NULL(strstr(ctx->state.error, "pinned toBlock or 'latest'"));
  c4_prover_free(ctx);
}

#define PINNED_RANGE_ARGS  "[{\"address\":\"" TEST_ADDRESS "\",\"fromBlock\":\"0xff\",\"toBlock\":\"0x100\"}]"
#define PINNED_SINGLE_ARGS "[{\"address\":\"" TEST_ADDRESS "\",\"fromBlock\":\"0x100\",\"toBlock\":\"0x100\"}]"

static void test_completeness_rejects_parent_hash_mismatch(void) {
  bytes_t older = make_el_header(0xff, NULL, NULL, NULL, NULL_BYTES, 1);
  uint8_t wrong_parent[32];
  memset(wrong_parent, 0xab, 32);
  bytes_t   newer      = make_el_header(0x100, wrong_parent, NULL, NULL, NULL_BYTES, 2);
  bytes32_t newer_hash = {0};
  keccak(newer, newer_hash);

  uint8_t    none       = 0;
  bytes_t    headers[1] = {older};
  bytes_t    blocks[2]  = {bytes(&none, 1), bytes(&none, 1)};
  ssz_ob_t   proof      = build_proof(newer_hash, headers, 1, blocks, 2);
  json_t     args       = json_parse(PINNED_RANGE_ARGS);
  c4_state_t st         = {0};
  assert_verify_error(run_verify_ex(proof, args, newer, newer_hash, 0, &st), &st, "parentHash mismatch");
  c4_state_free(&st);
  safe_free(proof.bytes.data);
  safe_free(older.data);
  safe_free(newer.data);
}

static void test_completeness_rejects_blocknumber_gap(void) {
  bytes_t   older      = make_el_header(0xfe, NULL, NULL, NULL, NULL_BYTES, 1);
  bytes32_t older_hash = {0};
  keccak(older, older_hash);
  bytes_t   newer      = make_el_header(0x100, older_hash, NULL, NULL, NULL_BYTES, 2);
  bytes32_t newer_hash = {0};
  keccak(newer, newer_hash);

  uint8_t    none       = 0;
  bytes_t    headers[1] = {older};
  bytes_t    blocks[2]  = {bytes(&none, 1), bytes(&none, 1)};
  ssz_ob_t   proof      = build_proof(newer_hash, headers, 1, blocks, 2);
  json_t     args       = json_parse(PINNED_RANGE_ARGS);
  c4_state_t st         = {0};
  assert_verify_error(run_verify_ex(proof, args, newer, newer_hash, 0, &st), &st, "blockNumber sequence has a gap");
  c4_state_free(&st);
  safe_free(proof.bytes.data);
  safe_free(older.data);
  safe_free(newer.data);
}

static void test_completeness_rejects_missing_el_header(void) {
  bytes_t   newer      = make_el_header(0x100, NULL, NULL, NULL, NULL_BYTES, 2);
  bytes32_t newer_hash = {0};
  keccak(newer, newer_hash);

  uint8_t    none       = 0;
  bytes_t    headers[1] = {NULL_BYTES};
  bytes_t    blocks[2]  = {bytes(&none, 1), bytes(&none, 1)};
  ssz_ob_t   proof      = build_proof(newer_hash, headers, 1, blocks, 2);
  json_t     args       = json_parse(PINNED_RANGE_ARGS);
  c4_state_t st         = {0};
  assert_verify_error(run_verify_ex(proof, args, newer, newer_hash, 0, &st), &st, "missing execution header");
  c4_state_free(&st);
  safe_free(proof.bytes.data);
  safe_free(newer.data);
}

// Empty receipts rebuild to EMPTY_ROOT_HASH; a different header receiptsRoot must be rejected.
static void test_completeness_rejects_receipts_root_mismatch(void) {
  uint8_t bogus_root[32];
  memset(bogus_root, 0x11, 32);
  bytes_t   hdr  = make_el_header(0x100, NULL, bogus_root, NULL, NULL_BYTES, 1);
  bytes32_t hash = {0};
  keccak(hdr, hash);

  bytes_t    full      = encode_full_receipts(NULL_BYTES, false);
  bytes_t    blocks[1] = {full};
  ssz_ob_t   proof     = build_proof(hash, NULL, 0, blocks, 1);
  json_t     args      = json_parse(PINNED_SINGLE_ARGS);
  c4_state_t st        = {0};
  assert_verify_error(run_verify_ex(proof, args, hdr, hash, 0, &st), &st, "receiptsRoot mismatch");
  c4_state_free(&st);
  safe_free(proof.bytes.data);
  safe_free(full.data);
  safe_free(hdr.data);
}

// NONE (bloom-negative) is only valid if the query bits are absent from the header logsBloom.
static void test_completeness_rejects_bloom_positive_as_negative(void) {
  json_t  filter = json_parse("{\"address\":\"" TEST_ADDRESS "\"}");
  bytes_t q      = c4_eth_filter_query_blooms(filter);
  TEST_ASSERT_TRUE(q.len >= 256);

  bytes_t   hdr  = make_el_header(0x100, NULL, NULL, NULL, bytes(q.data, 256), 1);
  bytes32_t hash = {0};
  keccak(hdr, hash);

  uint8_t    none      = 0;
  bytes_t    blocks[1] = {bytes(&none, 1)};
  ssz_ob_t   proof     = build_proof(hash, NULL, 0, blocks, 1);
  json_t     args      = json_parse(PINNED_SINGLE_ARGS);
  c4_state_t st        = {0};
  assert_verify_error(run_verify_ex(proof, args, hdr, hash, 0, &st), &st, "claimed bloom-negative");
  c4_state_free(&st);
  safe_free(proof.bytes.data);
  safe_free(hdr.data);
  safe_free(q.data);
}

// Open-ended `latest` must fail closed when the host supplied a freshness lower bound.
static void test_completeness_rejects_stale_latest(void) {
  bytes_t   hdr  = make_el_header(0x100, NULL, NULL, NULL, NULL_BYTES, 1);
  bytes32_t hash = {0};
  keccak(hdr, hash);

  uint8_t    none      = 0;
  bytes_t    blocks[1] = {bytes(&none, 1)};
  ssz_ob_t   proof     = build_proof(hash, NULL, 0, blocks, 1);
  json_t     args      = json_parse("[{\"address\":\"" TEST_ADDRESS "\",\"fromBlock\":\"0x100\",\"toBlock\":\"latest\"}]");
  c4_state_t st        = {0};
  assert_verify_error(run_verify_ex(proof, args, hdr, hash, 1000, &st), &st, "proof for latest too old");
  c4_state_free(&st);
  safe_free(proof.bytes.data);
  safe_free(hdr.data);
}

static void test_completeness_rejects_range_binding_mismatch(void) {
  bytes_t   hdr  = make_el_header(0x100, NULL, NULL, NULL, NULL_BYTES, 1);
  bytes32_t hash = {0};
  keccak(hdr, hash);

  uint8_t    none      = 0;
  bytes_t    blocks[1] = {bytes(&none, 1)};
  ssz_ob_t   proof     = build_proof(hash, NULL, 0, blocks, 1);
  json_t     args      = json_parse("[{\"address\":\"" TEST_ADDRESS "\",\"fromBlock\":\"0x200\",\"toBlock\":\"0x200\"}]");
  c4_state_t st        = {0};
  assert_verify_error(run_verify_ex(proof, args, hdr, hash, 0, &st), &st, "does not match requested block range");
  c4_state_free(&st);
  safe_free(proof.bytes.data);
  safe_free(hdr.data);
}

// A bloom-positive receipt without a matching CompletenessTx is an incomplete proof.
// Chained dummy headers + bloom-negative NONE: exercises reconstruct_el_chain success
// without a CL signature (anchor comes from the blockHash cache snapshot).
static void test_completeness_accepts_chained_bloom_negative(void) {
  bytes_t   older      = make_el_header(0xff, NULL, NULL, NULL, NULL_BYTES, 1);
  bytes32_t older_hash = {0};
  keccak(older, older_hash);
  bytes_t   newer      = make_el_header(0x100, older_hash, NULL, NULL, NULL_BYTES, 2);
  bytes32_t newer_hash = {0};
  keccak(newer, newer_hash);

  uint8_t    none       = 0;
  bytes_t    headers[1] = {older};
  bytes_t    blocks[2]  = {bytes(&none, 1), bytes(&none, 1)};
  ssz_ob_t   proof      = build_proof(newer_hash, headers, 1, blocks, 2);
  json_t     args       = json_parse(PINNED_RANGE_ARGS);
  c4_state_t st         = {0};
  TEST_ASSERT_TRUE_MESSAGE(run_verify_ex(proof, args, newer, newer_hash, 0, &st),
                           st.error ? st.error : "chained bloom-negative completeness should verify");
  TEST_ASSERT_NULL(st.error);
  c4_state_free(&st);
  safe_free(proof.bytes.data);
  safe_free(older.data);
  safe_free(newer.data);
}

static void test_completeness_rejects_missing_match_tx(void) {
  json_t  filter = json_parse("{\"address\":\"" TEST_ADDRESS "\"}");
  bytes_t q      = c4_eth_filter_query_blooms(filter);
  TEST_ASSERT_TRUE(q.len >= 256);

  buffer_t rjson = {0};
  bprintf(&rjson,
          "{\"type\":\"0x2\",\"status\":\"0x1\",\"cumulativeGasUsed\":\"0x1\",\"logsBloom\":\"0x%x\",\"logs\":[]}",
          bytes(q.data, 256));
  json_t   receipt_json = json_parse((char*) rjson.data.data);
  buffer_t rbuf         = {0};
  bytes_t  receipt_rlp  = c4_serialize_receipt(receipt_json, &rbuf);

  node_t*   trie     = NULL;
  bytes32_t path_tmp = {0};
  buffer_t  path_buf = stack_buffer(path_tmp);
  patricia_set_value(&trie, c4_eth_create_tx_path(0, &path_buf), receipt_rlp);
  bytes32_t receipts_root = {0};
  memcpy(receipts_root, patricia_get_root(trie).data, 32);
  patricia_node_free(trie);

  bytes_t   hdr  = make_el_header(0x100, NULL, receipts_root, NULL, bytes(q.data, 256), 1);
  bytes32_t hash = {0};
  keccak(hdr, hash);

  bytes_t    full      = encode_full_receipts(receipt_rlp, true);
  bytes_t    blocks[1] = {full};
  ssz_ob_t   proof     = build_proof(hash, NULL, 0, blocks, 1);
  json_t     args      = json_parse(PINNED_SINGLE_ARGS);
  c4_state_t st        = {0};
  assert_verify_error(run_verify_ex(proof, args, hdr, hash, 0, &st), &st, "without provided transaction");
  c4_state_free(&st);
  safe_free(proof.bytes.data);
  safe_free(full.data);
  safe_free(hdr.data);
  buffer_free(&rbuf);
  buffer_free(&rjson);
  safe_free(q.data);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_bloom_negative_subset_is_not_negative);
  RUN_TEST(test_bloom_negative_zero_block_is_negative);
  RUN_TEST(test_bloom_negative_no_variants);
  RUN_TEST(test_bloom_negative_bad_block_len);
  RUN_TEST(test_query_blooms_pap_array);
  RUN_TEST(test_query_blooms_pap_malformed);
  RUN_TEST(test_completeness_rejects_oversized_range);
  RUN_TEST(test_completeness_rejects_missing_filter);
  RUN_TEST(test_completeness_rejects_block_count_mismatch);
  RUN_TEST(test_completeness_rejects_unsupported_toblock);
  RUN_TEST(test_prover_rejects_safe_toblock);
  RUN_TEST(test_completeness_rejects_parent_hash_mismatch);
  RUN_TEST(test_completeness_rejects_blocknumber_gap);
  RUN_TEST(test_completeness_rejects_missing_el_header);
  RUN_TEST(test_completeness_rejects_receipts_root_mismatch);
  RUN_TEST(test_completeness_rejects_bloom_positive_as_negative);
  RUN_TEST(test_completeness_rejects_stale_latest);
  RUN_TEST(test_completeness_rejects_range_binding_mismatch);
  RUN_TEST(test_completeness_accepts_chained_bloom_negative);
  RUN_TEST(test_completeness_rejects_missing_match_tx);
  return UNITY_END();
}
