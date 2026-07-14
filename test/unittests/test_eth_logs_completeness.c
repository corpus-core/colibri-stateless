/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */
#include "c4_assert.h"

#include "beacon_types.h"
#include "bytes.h"
#include "eth_bloom.h"
#include "eth_verify.h"
#include "json.h"
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
  json_t  filter         = json_parse("{\"address\":\"" TEST_ADDRESS "\"}");
  bytes_t q              = c4_eth_filter_query_blooms(filter);
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

// Builds a structurally well-shaped LogsCompletenessProof with `header_count` ProofHeaders (i.e. a
// range of header_count+1 blocks), a zeroed signature_proof header_proof and an empty blocks list.
// The claim (range) now comes from the request, so the proof carries no range endpoints; this helper
// only exercises the structural guards that run *before* chain/anchor verification.
static ssz_ob_t build_completeness_proof(uint32_t header_count) {
  const ssz_def_t* def = eth_ssz_verification_type(ETH_SSZ_VERIFY_LOGS_COMPLETENESS_PROOF);
  TEST_ASSERT_NOT_NULL(def);

  uint8_t       header[112] = {0}; // slot(8)+proposerIndex(8)+parentRoot(32)+stateRoot(32)+bodyRoot(32)
  ssz_builder_t b           = ssz_builder_for_def(def);
  ssz_add_bytes(&b, "header", bytes(header, sizeof(header)));

  // headers list: header_count ProofHeaders (each slot8+proposerIndex8+stateRoot32+bodyRoot32)
  const ssz_def_t* headers_def = ssz_get_def(def, "headers");
  const ssz_def_t* ph_def      = headers_def->def.vector.type;
  ssz_builder_t    hlist       = ssz_builder_for_def(headers_def);
  for (uint32_t i = 0; i < header_count; i++) {
    uint8_t       z8[8] = {0}, z32a[32] = {0}, z32b[32] = {0}, z8b[8] = {0};
    ssz_builder_t ph = ssz_builder_for_def(ph_def);
    ssz_add_bytes(&ph, "slot", bytes(z8, 8));
    ssz_add_bytes(&ph, "proposerIndex", bytes(z8b, 8));
    ssz_add_bytes(&ph, "stateRoot", bytes(z32a, 32));
    ssz_add_bytes(&ph, "bodyRoot", bytes(z32b, 32));
    ssz_add_dynamic_list_builders(&hlist, header_count, ph);
  }
  ssz_add_builders(&b, "headers", hlist);

  // header_proof: zeroed signature_proof variant (index 0 of ETH_HEADER_PROOFS_UNION)
  const ssz_def_t* hp_field = ssz_get_def(def, "header_proof");
  ssz_builder_t    sp       = ssz_builder_for_def(hp_field->def.container.elements + 0);
  uint8_t          bits[64] = {0}, sig[96] = {0};
  ssz_add_bytes(&sp, "sync_committee_bits", bytes(bits, sizeof(bits)));
  ssz_add_bytes(&sp, "sync_committee_signature", bytes(sig, sizeof(sig)));
  ssz_add_builders(&b, "header_proof", sp);

  // tag_proof: `none` variant (index 0 of ETH_STATE_BLOCK_UNION) + empty branch. The structural
  // guards under test run before tag_proof verification, so its content is irrelevant here.
  uint8_t none = 0;
  ssz_add_bytes(&b, "tag_proof", bytes(&none, 1));
  ssz_add_bytes(&b, "tag_proof_branch", NULL_BYTES);

  ssz_add_bytes(&b, "blocks", NULL_BYTES); // empty list
  return ssz_builder_to_bytes(&b);
}

static bool run_verify(ssz_ob_t proof, json_t args, c4_state_t* state_out) {
  verify_ctx_t ctx = {0};
  ctx.chain_id     = C4_CHAIN_MAINNET;
  ctx.proof        = proof;
  ctx.args         = args;
  bool ok          = verify_logs_completeness(&ctx);
  *state_out       = ctx.state;
  return ok;
}

// A range wider than the SSZ list capacity must be rejected up front (DoS guard), before the chain
// is reconstructed or the anchor signature is checked.
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
  return UNITY_END();
}
