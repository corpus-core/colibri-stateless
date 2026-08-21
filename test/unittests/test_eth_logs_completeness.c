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
// Unrelated address whose bloom bits do not overlap TEST_ADDRESS (used to keep a skipped receipt's logIndex).
#define OTHER_ADDRESS "0x1111111111111111111111111111111111111111"

// Union selectors for ETH_COMPLETENESS_BLOCK_UNION.
#define COMPLETENESS_BLOCK_NONE 0
#define COMPLETENESS_BLOCK_FULL 1

#define PINNED_RANGE_ARGS  "[{\"address\":\"" TEST_ADDRESS "\",\"fromBlock\":\"0xff\",\"toBlock\":\"0x100\"}]"
#define PINNED_SINGLE_ARGS "[{\"address\":\"" TEST_ADDRESS "\",\"fromBlock\":\"0x100\",\"toBlock\":\"0x100\"}]"

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

static const ssz_def_t* completeness_proof_def(void) {
  const ssz_def_t* def = eth_ssz_verification_type(ETH_SSZ_VERIFY_LOGS_COMPLETENESS_PROOF);
  TEST_ASSERT_NOT_NULL(def);
  return def;
}

// LogsCompletenessProof:
//   block   — ETH_BLOCK_PROOF_UNION; tests use the blockHash variant
//   headers — raw RLP EL headers for fromBlock .. toBlock-1 (parentHash chain)
//   blocks  — per-block payload, selector 0 = NONE, 1 = FullReceipts
static ssz_ob_t build_proof(const uint8_t* anchor_hash, bytes_t* headers, uint32_t header_count,
                            bytes_t* block_elems, uint32_t block_count) {
  const ssz_def_t* def = completeness_proof_def();
  ssz_builder_t    b   = ssz_builder_for_def(def);

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

// Structural skeleton used by the DoS / shape guards that run before chain reconstruction.
// `header_count` empty RLP placeholders yield a derived range of header_count+1 blocks;
// the blocks list is left empty so those tests can assert the block-count mismatch.
static ssz_ob_t build_completeness_proof(uint32_t header_count) {
  uint8_t zhash[32] = {0};
  if (header_count == 0)
    return build_proof(zhash, NULL, 0, NULL, 0);

  bytes_t* headers = safe_calloc(header_count, sizeof(bytes_t));
  ssz_ob_t proof   = build_proof(zhash, headers, header_count, NULL, 0);
  safe_free(headers);
  return proof;
}

// Encodes selector 1 (FullReceipts). Field order must match ETH_COMPLETENESS_FULL_RECEIPTS:
// receipts, transactionProof, txs. Empty lists are encoded as NULL_BYTES.
static bytes_t encode_full_receipts(bytes_t* receipts, uint32_t n_receipts, bytes_t tx_proof, bytes_t txs) {
  const ssz_def_t* def       = completeness_proof_def();
  const ssz_def_t* union_def = ssz_get_def(def, "blocks")->def.vector.type;
  const ssz_def_t* full_def  = union_def->def.container.elements + COMPLETENESS_BLOCK_FULL;

  ssz_builder_t    v            = ssz_builder_for_def(full_def);
  const ssz_def_t* receipts_def = ssz_get_def(full_def, "receipts");
  ssz_builder_t    rlist        = ssz_builder_for_def(receipts_def);
  for (uint32_t i = 0; i < n_receipts; i++)
    ssz_add_dynamic_list_bytes(&rlist, n_receipts, receipts[i]);
  ssz_add_builders(&v, "receipts", rlist);
  ssz_add_bytes(&v, "transactionProof", tx_proof);
  ssz_add_bytes(&v, "txs", txs);

  ssz_ob_t vbytes = ssz_builder_to_bytes(&v);
  buffer_t elem   = {0};
  uint8_t  sel    = COMPLETENESS_BLOCK_FULL;
  buffer_append(&elem, bytes(&sel, 1));
  buffer_append(&elem, vbytes.bytes);
  safe_free(vbytes.bytes.data);
  return elem.data;
}

static bytes_t encode_none_block(void) {
  uint8_t sel = COMPLETENESS_BLOCK_NONE;
  return bytes_dup(bytes(&sel, 1));
}

// RLP-serializes a type-2 receipt. `log_address` NULL yields empty logs; otherwise one log at that address.
// `rlp_buf` owns the returned bytes (c4_serialize_receipt resets the buffer).
static bytes_t make_receipt_rlp_for(bytes_t logs_bloom, const char* log_address, buffer_t* rlp_buf) {
  buffer_t rjson = {0};
  if (log_address)
    bprintf(&rjson,
            "{\"type\":\"0x2\",\"status\":\"0x1\",\"cumulativeGasUsed\":\"0x1\","
            "\"logsBloom\":\"0x%x\",\"logs\":[{\"address\":\"%s\",\"topics\":[],\"data\":\"0x\"}]}",
            logs_bloom, log_address);
  else
    bprintf(&rjson,
            "{\"type\":\"0x2\",\"status\":\"0x1\",\"cumulativeGasUsed\":\"0x1\","
            "\"logsBloom\":\"0x%x\",\"logs\":[]}",
            logs_bloom);
  json_t  receipt_json = json_parse((char*) rjson.data.data);
  bytes_t rlp          = c4_serialize_receipt(receipt_json, rlp_buf);
  buffer_free(&rjson);
  return rlp;
}

static bytes_t make_receipt_rlp(bytes_t logs_bloom, bool with_matching_log, buffer_t* rlp_buf) {
  return make_receipt_rlp_for(logs_bloom, with_matching_log ? TEST_ADDRESS : NULL, rlp_buf);
}

static void receipts_root_of(bytes_t* receipts, uint32_t n, bytes32_t out) {
  node_t*   trie     = NULL;
  bytes32_t path_tmp = {0};
  buffer_t  path_buf = stack_buffer(path_tmp);
  for (uint32_t i = 0; i < n; i++)
    patricia_set_value(&trie, c4_eth_create_tx_path(i, &path_buf), receipts[i]);
  if (trie) {
    memcpy(out, patricia_get_root(trie).data, 32);
    patricia_node_free(trie);
  }
  else
    memcpy(out, EMPTY_ROOT_HASH, 32);
}

// Builds a multi-path MPT proof of `raw_tx` at `idx` and writes the trie root to `tx_root_out`.
static ssz_ob_t make_tx_proof(bytes_t raw_tx, uint32_t idx, bytes32_t tx_root_out) {
  node_t*       trie     = NULL;
  bytes32_t     path_tmp = {0};
  buffer_t      path_buf = stack_buffer(path_tmp);
  mpt_builder_t builder  = {0};
  patricia_set_value(&trie, c4_eth_create_tx_path(idx, &path_buf), raw_tx);
  memcpy(tx_root_out, patricia_get_root(trie).data, 32);
  mpt_builder_init(&builder, trie);
  mpt_builder_add_proof(&builder, c4_eth_create_tx_path(idx, &path_buf));
  ssz_ob_t proof = mpt_builder_finish(&builder);
  patricia_node_free(trie);
  return proof;
}

// One matching TEST_ADDRESS log at tx index 0, bound by an MPT proof against transactionsRoot.
// Caller owns `rbuf`, `tx_proof`, and the returned FullReceipts bytes.
static bytes_t encode_matching_full_receipts(bytes_t query_bloom, buffer_t* rbuf, ssz_ob_t* tx_proof,
                                             bytes32_t receipts_root, bytes32_t tx_root, bytes32_t tx_hash) {
  bytes_t receipt = make_receipt_rlp(query_bloom, true, rbuf);
  receipts_root_of(&receipt, 1, receipts_root);

  uint8_t raw_tx_bytes[64];
  memset(raw_tx_bytes, 0xaa, sizeof(raw_tx_bytes));
  bytes_t raw_tx = bytes(raw_tx_bytes, sizeof(raw_tx_bytes));
  *tx_proof      = make_tx_proof(raw_tx, 0, tx_root);
  keccak(raw_tx, tx_hash);

  uint8_t tx0[4];
  uint32_to_le(tx0, 0);
  return encode_full_receipts(&receipt, 1, tx_proof->bytes, bytes(tx0, 4));
}

static bool run_verify_ex(ssz_ob_t proof, json_t args, bytes_t cached_hdr, const uint8_t* cached_hash,
                          uint64_t min_latest_ts, c4_state_t* state_out, ssz_ob_t* data_out) {
  verify_ctx_t ctx        = {0};
  ctx.chain_id            = C4_CHAIN_MAINNET;
  ctx.proof               = proof;
  ctx.args                = args;
  ctx.min_latest_block_ts = min_latest_ts;

  // blockHash variant: c4_verify_block resolves a cached EL header by hash without a CL proof.
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
  if (ok && data_out)
    *data_out = ctx.data;
  else if (ok && (ctx.flags & VERIFY_FLAG_FREE_DATA))
    safe_free(ctx.data.bytes.data);
  *state_out = ctx.state;
  return ok;
}

static bool run_verify(ssz_ob_t proof, json_t args, c4_state_t* state_out) {
  return run_verify_ex(proof, args, NULL_BYTES, NULL, 0, state_out, NULL);
}

static void assert_verify_error(bool ok, c4_state_t* st, const char* needle) {
  TEST_ASSERT_FALSE_MESSAGE(ok, "expected verification to fail");
  TEST_ASSERT_NOT_NULL_MESSAGE(st->error, "expected an error message");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(st->error, needle), st->error);
}

static void test_completeness_rejects_oversized_range(void) {
  ssz_ob_t   proof = build_completeness_proof(4096); // count 4097 > 4096
  json_t     args  = json_parse("[{\"fromBlock\":\"0x0\",\"toBlock\":\"0x1388\"}]");
  c4_state_t st    = {0};
  assert_verify_error(run_verify(proof, args, &st), &st, "completeness range too large");
  c4_state_free(&st);
  safe_free(proof.bytes.data);
}

static void test_completeness_rejects_missing_filter(void) {
  ssz_ob_t   proof = build_completeness_proof(0);
  json_t     args  = json_parse("[]");
  c4_state_t st    = {0};
  assert_verify_error(run_verify(proof, args, &st), &st, "requires a filter object");
  c4_state_free(&st);
  safe_free(proof.bytes.data);
}

static void test_completeness_rejects_block_count_mismatch(void) {
  ssz_ob_t   proof = build_completeness_proof(0); // count 1, but blocks list is empty
  json_t     args  = json_parse("[{\"fromBlock\":\"0x100\",\"toBlock\":\"0x100\"}]");
  c4_state_t st    = {0};
  assert_verify_error(run_verify(proof, args, &st), &st, "block count mismatch");
  c4_state_free(&st);
  safe_free(proof.bytes.data);
}

static void test_completeness_rejects_unsupported_toblock(void) {
  bytes_t    none      = encode_none_block();
  uint8_t    zhash[32] = {0};
  bytes_t    blocks[1] = {none};
  ssz_ob_t   proof     = build_proof(zhash, NULL, 0, blocks, 1);
  json_t     args      = json_parse("[{\"fromBlock\":\"0x1\",\"toBlock\":\"safe\"}]");
  c4_state_t st        = {0};
  assert_verify_error(run_verify(proof, args, &st), &st, "toBlock tag not yet supported");
  c4_state_free(&st);
  safe_free(proof.bytes.data);
  safe_free(none.data);
}

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

static void test_completeness_rejects_uncached_blockhash(void) {
  bytes_t    none      = encode_none_block();
  uint8_t    zhash[32] = {0};
  bytes_t    blocks[1] = {none};
  ssz_ob_t   proof     = build_proof(zhash, NULL, 0, blocks, 1);
  json_t     args      = json_parse(PINNED_SINGLE_ARGS);
  c4_state_t st        = {0};
  assert_verify_error(run_verify(proof, args, &st), &st, "not found in the verifier cache");
  c4_state_free(&st);
  safe_free(proof.bytes.data);
  safe_free(none.data);
}

static void test_completeness_rejects_anchor_hash_mismatch(void) {
  bytes_t   hdr  = make_el_header(0x100, NULL, NULL, NULL, NULL_BYTES, 1);
  bytes32_t hash = {0};
  bytes32_t wrong;
  keccak(hdr, hash);
  memset(wrong, 0xcd, 32);

  bytes_t    none      = encode_none_block();
  bytes_t    blocks[1] = {none};
  ssz_ob_t   proof     = build_proof(wrong, NULL, 0, blocks, 1);
  json_t     args      = json_parse(PINNED_SINGLE_ARGS);
  c4_state_t st        = {0};
  // Cache is keyed by the (wrong) union hash so c4_verify_block succeeds; reconstruct then checks keccak(header).
  assert_verify_error(run_verify_ex(proof, args, hdr, wrong, 0, &st, NULL), &st, "anchor header hash mismatch");
  c4_state_free(&st);
  safe_free(proof.bytes.data);
  safe_free(none.data);
  safe_free(hdr.data);
}

static void test_completeness_rejects_parent_hash_mismatch(void) {
  bytes_t older = make_el_header(0xff, NULL, NULL, NULL, NULL_BYTES, 1);
  uint8_t wrong_parent[32];
  memset(wrong_parent, 0xab, 32);
  bytes_t   newer      = make_el_header(0x100, wrong_parent, NULL, NULL, NULL_BYTES, 2);
  bytes32_t newer_hash = {0};
  keccak(newer, newer_hash);

  bytes_t    none       = encode_none_block();
  bytes_t    headers[1] = {older};
  bytes_t    blocks[2]  = {none, none};
  ssz_ob_t   proof      = build_proof(newer_hash, headers, 1, blocks, 2);
  json_t     args       = json_parse(PINNED_RANGE_ARGS);
  c4_state_t st         = {0};
  assert_verify_error(run_verify_ex(proof, args, newer, newer_hash, 0, &st, NULL), &st, "parentHash mismatch");
  c4_state_free(&st);
  safe_free(proof.bytes.data);
  safe_free(none.data);
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

  bytes_t    none       = encode_none_block();
  bytes_t    headers[1] = {older};
  bytes_t    blocks[2]  = {none, none};
  ssz_ob_t   proof      = build_proof(newer_hash, headers, 1, blocks, 2);
  json_t     args       = json_parse(PINNED_RANGE_ARGS);
  c4_state_t st         = {0};
  assert_verify_error(run_verify_ex(proof, args, newer, newer_hash, 0, &st, NULL), &st, "blockNumber sequence has a gap");
  c4_state_free(&st);
  safe_free(proof.bytes.data);
  safe_free(none.data);
  safe_free(older.data);
  safe_free(newer.data);
}

static void test_completeness_rejects_missing_el_header(void) {
  bytes_t   newer      = make_el_header(0x100, NULL, NULL, NULL, NULL_BYTES, 2);
  bytes32_t newer_hash = {0};
  keccak(newer, newer_hash);

  bytes_t    none       = encode_none_block();
  bytes_t    headers[1] = {NULL_BYTES};
  bytes_t    blocks[2]  = {none, none};
  ssz_ob_t   proof      = build_proof(newer_hash, headers, 1, blocks, 2);
  json_t     args       = json_parse(PINNED_RANGE_ARGS);
  c4_state_t st         = {0};
  assert_verify_error(run_verify_ex(proof, args, newer, newer_hash, 0, &st, NULL), &st, "missing execution header");
  c4_state_free(&st);
  safe_free(proof.bytes.data);
  safe_free(none.data);
  safe_free(newer.data);
}

static void test_completeness_rejects_oversized_el_header(void) {
  bytes_t   newer      = make_el_header(0x100, NULL, NULL, NULL, NULL_BYTES, 2);
  bytes32_t newer_hash = {0};
  keccak(newer, newer_hash);

  uint8_t too_big[2049];
  memset(too_big, 0x11, sizeof(too_big));
  bytes_t    none       = encode_none_block();
  bytes_t    headers[1] = {bytes(too_big, sizeof(too_big))};
  bytes_t    blocks[2]  = {none, none};
  ssz_ob_t   proof      = build_proof(newer_hash, headers, 1, blocks, 2);
  json_t     args       = json_parse(PINNED_RANGE_ARGS);
  c4_state_t st         = {0};
  assert_verify_error(run_verify_ex(proof, args, newer, newer_hash, 0, &st, NULL), &st, "execution header too large");
  c4_state_free(&st);
  safe_free(proof.bytes.data);
  safe_free(none.data);
  safe_free(newer.data);
}

// Empty receipts rebuild to EMPTY_ROOT_HASH; a different header receiptsRoot must be rejected.
static void test_completeness_rejects_receipts_root_mismatch(void) {
  uint8_t bogus_root[32];
  memset(bogus_root, 0x11, 32);
  bytes_t   hdr  = make_el_header(0x100, NULL, bogus_root, NULL, NULL_BYTES, 1);
  bytes32_t hash = {0};
  keccak(hdr, hash);

  bytes_t    full      = encode_full_receipts(NULL, 0, NULL_BYTES, NULL_BYTES);
  bytes_t    blocks[1] = {full};
  ssz_ob_t   proof     = build_proof(hash, NULL, 0, blocks, 1);
  json_t     args      = json_parse(PINNED_SINGLE_ARGS);
  c4_state_t st        = {0};
  assert_verify_error(run_verify_ex(proof, args, hdr, hash, 0, &st, NULL), &st, "receiptsRoot mismatch");
  c4_state_free(&st);
  safe_free(proof.bytes.data);
  safe_free(full.data);
  safe_free(hdr.data);
}

// NONE is only valid if the query bits are absent from the (proven) header logsBloom.
static void test_completeness_rejects_bloom_positive_as_negative(void) {
  json_t  filter = json_parse("{\"address\":\"" TEST_ADDRESS "\"}");
  bytes_t q      = c4_eth_filter_query_blooms(filter);
  TEST_ASSERT_TRUE(q.len >= 256);

  bytes_t   hdr  = make_el_header(0x100, NULL, NULL, NULL, bytes(q.data, 256), 1);
  bytes32_t hash = {0};
  keccak(hdr, hash);

  bytes_t    none      = encode_none_block();
  bytes_t    blocks[1] = {none};
  ssz_ob_t   proof     = build_proof(hash, NULL, 0, blocks, 1);
  json_t     args      = json_parse(PINNED_SINGLE_ARGS);
  c4_state_t st        = {0};
  assert_verify_error(run_verify_ex(proof, args, hdr, hash, 0, &st, NULL), &st, "claimed bloom-negative");
  c4_state_free(&st);
  safe_free(proof.bytes.data);
  safe_free(none.data);
  safe_free(hdr.data);
  safe_free(q.data);
}

static void test_completeness_rejects_stale_latest(void) {
  bytes_t   hdr  = make_el_header(0x100, NULL, NULL, NULL, NULL_BYTES, 1);
  bytes32_t hash = {0};
  keccak(hdr, hash);

  bytes_t    none      = encode_none_block();
  bytes_t    blocks[1] = {none};
  ssz_ob_t   proof     = build_proof(hash, NULL, 0, blocks, 1);
  json_t     args      = json_parse("[{\"address\":\"" TEST_ADDRESS "\",\"fromBlock\":\"0x100\",\"toBlock\":\"latest\"}]");
  c4_state_t st        = {0};
  assert_verify_error(run_verify_ex(proof, args, hdr, hash, 1000, &st, NULL), &st, "proof for latest too old");
  c4_state_free(&st);
  safe_free(proof.bytes.data);
  safe_free(none.data);
  safe_free(hdr.data);
}

static void test_completeness_rejects_range_binding_mismatch(void) {
  bytes_t   hdr  = make_el_header(0x100, NULL, NULL, NULL, NULL_BYTES, 1);
  bytes32_t hash = {0};
  keccak(hdr, hash);

  bytes_t    none      = encode_none_block();
  bytes_t    blocks[1] = {none};
  ssz_ob_t   proof     = build_proof(hash, NULL, 0, blocks, 1);
  json_t     args      = json_parse("[{\"address\":\"" TEST_ADDRESS "\",\"fromBlock\":\"0x200\",\"toBlock\":\"0x200\"}]");
  c4_state_t st        = {0};
  assert_verify_error(run_verify_ex(proof, args, hdr, hash, 0, &st, NULL), &st, "does not match requested block range");
  c4_state_free(&st);
  safe_free(proof.bytes.data);
  safe_free(none.data);
  safe_free(hdr.data);
}

// parentHash chain of two bloom-negative headers, anchored via the blockHash cache snapshot.
static void test_completeness_accepts_chained_bloom_negative(void) {
  bytes_t   older      = make_el_header(0xff, NULL, NULL, NULL, NULL_BYTES, 1);
  bytes32_t older_hash = {0};
  keccak(older, older_hash);
  bytes_t   newer      = make_el_header(0x100, older_hash, NULL, NULL, NULL_BYTES, 2);
  bytes32_t newer_hash = {0};
  keccak(newer, newer_hash);

  bytes_t    none       = encode_none_block();
  bytes_t    headers[1] = {older};
  bytes_t    blocks[2]  = {none, none};
  ssz_ob_t   proof      = build_proof(newer_hash, headers, 1, blocks, 2);
  json_t     args       = json_parse(PINNED_RANGE_ARGS);
  c4_state_t st         = {0};
  TEST_ASSERT_TRUE_MESSAGE(run_verify_ex(proof, args, newer, newer_hash, 0, &st, NULL),
                           st.error ? st.error : "chained bloom-negative completeness should verify");
  TEST_ASSERT_NULL(st.error);
  c4_state_free(&st);
  safe_free(proof.bytes.data);
  safe_free(none.data);
  safe_free(older.data);
  safe_free(newer.data);
}

// Empty FullReceipts with the empty-trie receiptsRoot: the receipts path is exercised
// without requiring a matching transaction.
static void test_completeness_accepts_empty_full_receipts(void) {
  bytes_t   hdr  = make_el_header(0x100, NULL, NULL, NULL, NULL_BYTES, 1);
  bytes32_t hash = {0};
  keccak(hdr, hash);

  bytes_t    full      = encode_full_receipts(NULL, 0, NULL_BYTES, NULL_BYTES);
  bytes_t    blocks[1] = {full};
  ssz_ob_t   proof     = build_proof(hash, NULL, 0, blocks, 1);
  json_t     args      = json_parse(PINNED_SINGLE_ARGS);
  c4_state_t st        = {0};
  ssz_ob_t   data      = {0};
  TEST_ASSERT_TRUE_MESSAGE(run_verify_ex(proof, args, hdr, hash, 0, &st, &data),
                           st.error ? st.error : "empty FullReceipts should verify");
  TEST_ASSERT_NULL(st.error);
  TEST_ASSERT_EQUAL_UINT32(0, ssz_len(data));
  c4_state_free(&st);
  safe_free(data.bytes.data);
  safe_free(proof.bytes.data);
  safe_free(full.data);
  safe_free(hdr.data);
}

// A bloom-positive receipt without a matching tx index is an incomplete proof.
static void test_completeness_rejects_missing_match_tx(void) {
  json_t  filter = json_parse("{\"address\":\"" TEST_ADDRESS "\"}");
  bytes_t q      = c4_eth_filter_query_blooms(filter);
  TEST_ASSERT_TRUE(q.len >= 256);

  buffer_t  rbuf    = {0};
  bytes_t   receipt = make_receipt_rlp(bytes(q.data, 256), false, &rbuf);
  bytes32_t rr      = {0};
  receipts_root_of(&receipt, 1, rr);

  bytes_t   hdr  = make_el_header(0x100, NULL, rr, NULL, bytes(q.data, 256), 1);
  bytes32_t hash = {0};
  keccak(hdr, hash);

  bytes_t    full      = encode_full_receipts(&receipt, 1, NULL_BYTES, NULL_BYTES);
  bytes_t    blocks[1] = {full};
  ssz_ob_t   proof     = build_proof(hash, NULL, 0, blocks, 1);
  json_t     args      = json_parse(PINNED_SINGLE_ARGS);
  c4_state_t st        = {0};
  assert_verify_error(run_verify_ex(proof, args, hdr, hash, 0, &st, NULL), &st, "without provided transaction");
  c4_state_free(&st);
  safe_free(proof.bytes.data);
  safe_free(full.data);
  safe_free(hdr.data);
  buffer_free(&rbuf);
  safe_free(q.data);
}

// txs claims index 0 but transactionProof cannot prove it against transactionsRoot.
static void test_completeness_rejects_invalid_tx_proof(void) {
  json_t  filter = json_parse("{\"address\":\"" TEST_ADDRESS "\"}");
  bytes_t q      = c4_eth_filter_query_blooms(filter);
  TEST_ASSERT_TRUE(q.len >= 256);

  buffer_t  rbuf    = {0};
  bytes_t   receipt = make_receipt_rlp(bytes(q.data, 256), false, &rbuf);
  bytes32_t rr      = {0};
  receipts_root_of(&receipt, 1, rr);

  bytes_t   hdr  = make_el_header(0x100, NULL, rr, NULL, bytes(q.data, 256), 1);
  bytes32_t hash = {0};
  keccak(hdr, hash);

  uint8_t tx0[4];
  uint32_to_le(tx0, 0);
  bytes_t    full      = encode_full_receipts(&receipt, 1, NULL_BYTES, bytes(tx0, 4));
  bytes_t    blocks[1] = {full};
  ssz_ob_t   proof     = build_proof(hash, NULL, 0, blocks, 1);
  json_t     args      = json_parse(PINNED_SINGLE_ARGS);
  c4_state_t st        = {0};
  assert_verify_error(run_verify_ex(proof, args, hdr, hash, 0, &st, NULL), &st, "invalid transaction proof");
  c4_state_free(&st);
  safe_free(proof.bytes.data);
  safe_free(full.data);
  safe_free(hdr.data);
  buffer_free(&rbuf);
  safe_free(q.data);
}

// FullReceipts with one matching log: receiptsRoot + transactionsRoot come from the EL header,
// the raw tx is bound via a Patricia proof, and the reconstructed log is filtered by address.
static void test_completeness_accepts_full_receipts_matching_log(void) {
  json_t  filter = json_parse("{\"address\":\"" TEST_ADDRESS "\"}");
  bytes_t q      = c4_eth_filter_query_blooms(filter);
  TEST_ASSERT_TRUE(q.len >= 256);

  buffer_t  rbuf    = {0};
  bytes_t   receipt = make_receipt_rlp(bytes(q.data, 256), true, &rbuf);
  bytes32_t rr      = {0};
  receipts_root_of(&receipt, 1, rr);

  // Leaf nodes shorter than 32 bytes are embedded, not hashed, and would be omitted from the
  // multi-proof. Use a payload large enough that the serialized leaf is hashed.
  uint8_t raw_tx_bytes[64];
  memset(raw_tx_bytes, 0xaa, sizeof(raw_tx_bytes));
  bytes_t   raw_tx   = bytes(raw_tx_bytes, sizeof(raw_tx_bytes));
  bytes32_t tx_root  = {0};
  ssz_ob_t  tx_proof = make_tx_proof(raw_tx, 0, tx_root);
  TEST_ASSERT_TRUE_MESSAGE(ssz_len(tx_proof) > 0, "expected a hashed MPT node in the tx proof");
  bytes32_t expected_txh = {0};
  keccak(raw_tx, expected_txh);

  bytes_t   hdr  = make_el_header(0x100, NULL, rr, tx_root, bytes(q.data, 256), 1);
  bytes32_t hash = {0};
  keccak(hdr, hash);

  uint8_t tx0[4];
  uint32_to_le(tx0, 0);
  bytes_t    full      = encode_full_receipts(&receipt, 1, tx_proof.bytes, bytes(tx0, 4));
  bytes_t    blocks[1] = {full};
  ssz_ob_t   proof     = build_proof(hash, NULL, 0, blocks, 1);
  json_t     args      = json_parse(PINNED_SINGLE_ARGS);
  c4_state_t st        = {0};
  ssz_ob_t   data      = {0};
  TEST_ASSERT_TRUE_MESSAGE(run_verify_ex(proof, args, hdr, hash, 0, &st, &data),
                           st.error ? st.error : "matching-log FullReceipts should verify");
  TEST_ASSERT_NULL(st.error);
  TEST_ASSERT_EQUAL_UINT32(1, ssz_len(data));

  ssz_ob_t log = ssz_at(data, 0);
  uint8_t  expected_addr[20];
  TEST_ASSERT_EQUAL_INT(20, hex_to_bytes(TEST_ADDRESS, -1, bytes(expected_addr, 20)));
  TEST_ASSERT_EQUAL_UINT32(20, ssz_get(&log, "address").bytes.len);
  TEST_ASSERT_EQUAL_MEMORY(expected_addr, ssz_get(&log, "address").bytes.data, 20);
  TEST_ASSERT_EQUAL_UINT64(0x100, ssz_get_uint64(&log, "blockNumber"));
  TEST_ASSERT_EQUAL_UINT32(0, ssz_get_uint32(&log, "transactionIndex"));
  TEST_ASSERT_EQUAL_UINT32(0, ssz_get_uint32(&log, "logIndex"));
  TEST_ASSERT_EQUAL_MEMORY(hash, ssz_get(&log, "blockHash").bytes.data, 32);
  TEST_ASSERT_EQUAL_MEMORY(expected_txh, ssz_get(&log, "transactionHash").bytes.data, 32);

  c4_state_free(&st);
  safe_free(data.bytes.data);
  safe_free(proof.bytes.data);
  safe_free(full.data);
  safe_free(tx_proof.bytes.data);
  safe_free(hdr.data);
  buffer_free(&rbuf);
  safe_free(q.data);
}

// Anchor size is checked before keccak; an oversized cached header must not enter the parentHash walk.
static void test_completeness_rejects_oversized_anchor_header(void) {
  uint8_t too_big[2049];
  memset(too_big, 0x11, sizeof(too_big));
  bytes_t   hdr  = bytes(too_big, sizeof(too_big));
  bytes32_t hash = {0};
  keccak(hdr, hash);

  bytes_t    none      = encode_none_block();
  bytes_t    blocks[1] = {none};
  ssz_ob_t   proof     = build_proof(hash, NULL, 0, blocks, 1);
  json_t     args      = json_parse(PINNED_SINGLE_ARGS);
  c4_state_t st        = {0};
  assert_verify_error(run_verify_ex(proof, args, hdr, hash, 0, &st, NULL), &st, "invalid anchor execution header");
  c4_state_free(&st);
  safe_free(proof.bytes.data);
  safe_free(none.data);
}

// txs is a list of matching indices; more entries than receipts is structurally impossible.
static void test_completeness_rejects_too_many_txs(void) {
  bytes_t   hdr  = make_el_header(0x100, NULL, NULL, NULL, NULL_BYTES, 1);
  bytes32_t hash = {0};
  keccak(hdr, hash);

  uint8_t tx0[4];
  uint32_to_le(tx0, 0);
  bytes_t    full      = encode_full_receipts(NULL, 0, NULL_BYTES, bytes(tx0, 4));
  bytes_t    blocks[1] = {full};
  ssz_ob_t   proof     = build_proof(hash, NULL, 0, blocks, 1);
  json_t     args      = json_parse(PINNED_SINGLE_ARGS);
  c4_state_t st        = {0};
  assert_verify_error(run_verify_ex(proof, args, hdr, hash, 0, &st, NULL), &st, "too many txs");
  c4_state_free(&st);
  safe_free(proof.bytes.data);
  safe_free(full.data);
  safe_free(hdr.data);
}

// Selector 2 is not a member of ETH_COMPLETENESS_BLOCK_UNION (NONE=0, FullReceipts=1).
static void test_completeness_rejects_unknown_block_selector(void) {
  bytes_t   hdr  = make_el_header(0x100, NULL, NULL, NULL, NULL_BYTES, 1);
  bytes32_t hash = {0};
  keccak(hdr, hash);

  uint8_t    bad_sel   = 2;
  bytes_t    blocks[1] = {bytes(&bad_sel, 1)};
  ssz_ob_t   proof     = build_proof(hash, NULL, 0, blocks, 1);
  json_t     args      = json_parse(PINNED_SINGLE_ARGS);
  c4_state_t st        = {0};
  assert_verify_error(run_verify_ex(proof, args, hdr, hash, 0, &st, NULL), &st, "invalid logs completeness proof");
  c4_state_free(&st);
  safe_free(proof.bytes.data);
  safe_free(hdr.data);
}

// Bloom-negative receipts inside FullReceipts need no tx, but their logs still advance logIndex.
static void test_completeness_skips_bloom_negative_receipt(void) {
  json_t  filter = json_parse("{\"address\":\"" TEST_ADDRESS "\"}");
  bytes_t q      = c4_eth_filter_query_blooms(filter);
  TEST_ASSERT_TRUE(q.len >= 256);

  uint8_t   zero_bloom[256] = {0};
  buffer_t  rbuf0           = {0};
  buffer_t  rbuf1           = {0};
  bytes_t   rec0            = make_receipt_rlp_for(bytes(zero_bloom, 256), OTHER_ADDRESS, &rbuf0);
  bytes_t   rec1            = make_receipt_rlp_for(bytes(q.data, 256), TEST_ADDRESS, &rbuf1);
  bytes_t   receipts[2]     = {rec0, rec1};
  bytes32_t rr              = {0};
  receipts_root_of(receipts, 2, rr);

  uint8_t raw_tx_bytes[64];
  memset(raw_tx_bytes, 0xbb, sizeof(raw_tx_bytes));
  bytes_t   raw_tx       = bytes(raw_tx_bytes, sizeof(raw_tx_bytes));
  bytes32_t tx_root      = {0};
  ssz_ob_t  tx_proof     = make_tx_proof(raw_tx, 1, tx_root);
  bytes32_t expected_txh = {0};
  keccak(raw_tx, expected_txh);

  bytes_t   hdr  = make_el_header(0x100, NULL, rr, tx_root, bytes(q.data, 256), 1);
  bytes32_t hash = {0};
  keccak(hdr, hash);

  uint8_t tx1[4];
  uint32_to_le(tx1, 1);
  bytes_t    full      = encode_full_receipts(receipts, 2, tx_proof.bytes, bytes(tx1, 4));
  bytes_t    blocks[1] = {full};
  ssz_ob_t   proof     = build_proof(hash, NULL, 0, blocks, 1);
  json_t     args      = json_parse(PINNED_SINGLE_ARGS);
  c4_state_t st        = {0};
  ssz_ob_t   data      = {0};
  TEST_ASSERT_TRUE_MESSAGE(run_verify_ex(proof, args, hdr, hash, 0, &st, &data),
                           st.error ? st.error : "FullReceipts with a skipped receipt should verify");
  TEST_ASSERT_NULL(st.error);
  TEST_ASSERT_EQUAL_UINT32(1, ssz_len(data));

  ssz_ob_t log = ssz_at(data, 0);
  TEST_ASSERT_EQUAL_UINT32(1, ssz_get_uint32(&log, "transactionIndex"));
  TEST_ASSERT_EQUAL_UINT32(1, ssz_get_uint32(&log, "logIndex"));
  TEST_ASSERT_EQUAL_MEMORY(expected_txh, ssz_get(&log, "transactionHash").bytes.data, 32);

  c4_state_free(&st);
  safe_free(data.bytes.data);
  safe_free(proof.bytes.data);
  safe_free(full.data);
  safe_free(tx_proof.bytes.data);
  safe_free(hdr.data);
  buffer_free(&rbuf0);
  buffer_free(&rbuf1);
  safe_free(q.data);
}

// FullReceipts on the older header + NONE on the anchor: blockHash/number must come from hashes[i],
// not from the cached toBlock. Catches blocks[] vs headers[] misalignment on the parentHash chain.
static void test_completeness_accepts_mixed_none_and_full(void) {
  json_t  filter = json_parse("{\"address\":\"" TEST_ADDRESS "\"}");
  bytes_t q      = c4_eth_filter_query_blooms(filter);
  TEST_ASSERT_TRUE(q.len >= 256);

  buffer_t  rbuf         = {0};
  ssz_ob_t  tx_proof     = {0};
  bytes32_t rr           = {0};
  bytes32_t tx_root      = {0};
  bytes32_t expected_txh = {0};
  bytes_t   full         = encode_matching_full_receipts(bytes(q.data, 256), &rbuf, &tx_proof, rr, tx_root, expected_txh);

  bytes_t   older      = make_el_header(0xff, NULL, rr, tx_root, bytes(q.data, 256), 1);
  bytes32_t older_hash = {0};
  keccak(older, older_hash);
  bytes_t   newer      = make_el_header(0x100, older_hash, NULL, NULL, NULL_BYTES, 2);
  bytes32_t newer_hash = {0};
  keccak(newer, newer_hash);

  bytes_t    none       = encode_none_block();
  bytes_t    headers[1] = {older};
  bytes_t    blocks[2]  = {full, none};
  ssz_ob_t   proof      = build_proof(newer_hash, headers, 1, blocks, 2);
  json_t     args       = json_parse(PINNED_RANGE_ARGS);
  c4_state_t st         = {0};
  ssz_ob_t   data       = {0};
  TEST_ASSERT_TRUE_MESSAGE(run_verify_ex(proof, args, newer, newer_hash, 0, &st, &data),
                           st.error ? st.error : "mixed NONE+FullReceipts chain should verify");
  TEST_ASSERT_NULL(st.error);
  TEST_ASSERT_EQUAL_UINT32(1, ssz_len(data));

  ssz_ob_t log = ssz_at(data, 0);
  TEST_ASSERT_EQUAL_UINT64(0xff, ssz_get_uint64(&log, "blockNumber"));
  TEST_ASSERT_EQUAL_MEMORY(older_hash, ssz_get(&log, "blockHash").bytes.data, 32);
  TEST_ASSERT_EQUAL_MEMORY(expected_txh, ssz_get(&log, "transactionHash").bytes.data, 32);

  c4_state_free(&st);
  safe_free(data.bytes.data);
  safe_free(proof.bytes.data);
  safe_free(full.data);
  safe_free(none.data);
  safe_free(tx_proof.bytes.data);
  safe_free(older.data);
  safe_free(newer.data);
  buffer_free(&rbuf);
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
  RUN_TEST(test_completeness_rejects_uncached_blockhash);
  RUN_TEST(test_completeness_rejects_anchor_hash_mismatch);
  RUN_TEST(test_completeness_rejects_parent_hash_mismatch);
  RUN_TEST(test_completeness_rejects_blocknumber_gap);
  RUN_TEST(test_completeness_rejects_missing_el_header);
  RUN_TEST(test_completeness_rejects_oversized_el_header);
  RUN_TEST(test_completeness_rejects_oversized_anchor_header);
  RUN_TEST(test_completeness_rejects_receipts_root_mismatch);
  RUN_TEST(test_completeness_rejects_bloom_positive_as_negative);
  RUN_TEST(test_completeness_rejects_stale_latest);
  RUN_TEST(test_completeness_rejects_range_binding_mismatch);
  RUN_TEST(test_completeness_accepts_chained_bloom_negative);
  RUN_TEST(test_completeness_accepts_empty_full_receipts);
  RUN_TEST(test_completeness_rejects_missing_match_tx);
  RUN_TEST(test_completeness_rejects_invalid_tx_proof);
  RUN_TEST(test_completeness_rejects_too_many_txs);
  RUN_TEST(test_completeness_rejects_unknown_block_selector);
  RUN_TEST(test_completeness_accepts_full_receipts_matching_log);
  RUN_TEST(test_completeness_skips_bloom_negative_receipt);
  RUN_TEST(test_completeness_accepts_mixed_none_and_full);
  return UNITY_END();
}
