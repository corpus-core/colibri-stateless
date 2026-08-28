/*
 * Copyright (c) 2026 corpus.core
 * SPDX-License-Identifier: MIT
 *
 * Tests for ETH_BLOCK_PROOF_UNION index 2 (sequencerProof) and the OP verify hook.
 */

#include "unity.h"
#include "util/bytes.h"
#include "util/chains.h"
#include "util/json.h"
#include "util/ssz.h"
#include <stdlib.h>
#include <string.h>

#ifndef NO_CHAIN_OP

#include "chains/eth/prover/eth_tools.h"
#include "chains/eth/ssz/beacon_types.h"
#include "chains/eth/verifier/el_header.h"
#include "chains/eth/verifier/eth_verify.h"
#include "chains/eth/verifier/header_cache.h"
#include "chains/op/prover/op_prover.h"
#include "chains/op/verifier/op_verify.h"
#include "prover/prover.h"
#include "util/crypto.h"

static const ssz_def_t EP_CONTAINER = SSZ_CONTAINER("payload", DENEP_EXECUTION_PAYLOAD);

#define TEST_BLOCK_NUMBER 42ull

static verify_ctx_t g_ctx;

void setUp(void) {
  memset(&g_ctx, 0, sizeof(g_ctx));
  g_ctx.chain_id = C4_CHAIN_OP_MAINNET;
  op_register_block_proof_verify();
#ifdef EL_HEADER_CACHE
  c4_header_cache_clear();
#endif
}

void tearDown(void) {
  if (g_ctx.flags & VERIFY_FLAG_FREE_DATA) {
    safe_free(g_ctx.data.bytes.data);
    g_ctx.data.bytes.data = NULL;
  }
  c4_state_free(&g_ctx.state);
#ifdef EL_HEADER_CACHE
  c4_header_cache_clear();
#endif
}

static void add_u64(ssz_builder_t* b, const char* name, uint64_t v) {
  uint8_t tmp[8];
  for (int i = 0; i < 8; i++) tmp[i] = (uint8_t) (v >> (8 * i));
  ssz_add_bytes(b, name, bytes(tmp, 8));
}

static ssz_ob_t build_deneb_payload(const uint8_t block_hash[32], uint64_t block_number) {
  uint8_t       z32[32]     = {0};
  uint8_t       z20[20]     = {0};
  uint8_t       bloom[256]  = {0};
  uint8_t       base_fee[32] = {0};
  ssz_builder_t b           = ssz_builder_for_def(&EP_CONTAINER);
  ssz_add_bytes(&b, "parentHash", bytes(z32, 32));
  ssz_add_bytes(&b, "feeRecipient", bytes(z20, 20));
  ssz_add_bytes(&b, "stateRoot", bytes(z32, 32));
  ssz_add_bytes(&b, "receiptsRoot", bytes(z32, 32));
  ssz_add_bytes(&b, "logsBloom", bytes(bloom, 256));
  ssz_add_bytes(&b, "prevRandao", bytes(z32, 32));
  add_u64(&b, "blockNumber", block_number);
  add_u64(&b, "gasLimit", 30000000ull);
  add_u64(&b, "gasUsed", 0);
  add_u64(&b, "timestamp", 1700000000ull);
  ssz_add_bytes(&b, "extraData", NULL_BYTES);
  ssz_add_bytes(&b, "baseFeePerGas", bytes(base_fee, 32));
  ssz_add_bytes(&b, "blockHash", bytes(block_hash, 32));
  ssz_add_bytes(&b, "transactions", NULL_BYTES);
  ssz_add_bytes(&b, "withdrawals", NULL_BYTES);
  add_u64(&b, "blobGasUsed", 0);
  add_u64(&b, "excessBlobGas", 0);
  return ssz_builder_to_bytes(&b);
}

static bytes_t wrap_preconf(ssz_ob_t ep) {
  bytes_t out = bytes(safe_malloc(32 + ep.bytes.len), 32 + ep.bytes.len);
  memset(out.data, 0xAB, 32);
  memcpy(out.data + 32, ep.bytes.data, ep.bytes.len);
  return out;
}

typedef struct {
  bytes_t   preconf;
  bytes_t   header;
  ssz_ob_t  body;
  bytes32_t hash;
} matching_el_t;

static void matching_el_free(matching_el_t* m) {
  safe_free(m->preconf.data);
  safe_free(m->header.data);
  safe_free(m->body.bytes.data);
  memset(m, 0, sizeof(*m));
}

static matching_el_t build_matching_el(uint64_t block_number) {
  matching_el_t out          = {0};
  uint8_t       dummy_hash[32] = {0};
  dummy_hash[0]              = 0x11;
  ssz_ob_t draft             = build_deneb_payload(dummy_hash, block_number);
  TEST_ASSERT_TRUE(ssz_is_valid(draft, true, NULL));

  eth_el_header_ctx_t ectx = {0};
  ectx.execution_payload   = draft;
  ectx.fork                = C4_FORK_DENEB;
  memcpy(ectx.parent_root, "\xAB\xAB\xAB\xAB\xAB\xAB\xAB\xAB\xAB\xAB\xAB\xAB\xAB\xAB\xAB\xAB"
                           "\xAB\xAB\xAB\xAB\xAB\xAB\xAB\xAB\xAB\xAB\xAB\xAB\xAB\xAB\xAB\xAB",
         32);
  bytes_t header = NULL_BYTES;
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, eth_el_header_build_from_ep(&header, &ectx));
  TEST_ASSERT_NOT_NULL(header.data);
  keccak(header, out.hash);
  safe_free(header.data);
  safe_free(draft.bytes.data);

  ssz_ob_t matched = build_deneb_payload(out.hash, block_number);
  out.preconf      = wrap_preconf(matched);
  safe_free(matched.bytes.data);

  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, op_el_from_preconf_bytes(NULL, out.preconf, &out.header, &out.body, out.hash));
  TEST_ASSERT_NOT_NULL(out.header.data);
  TEST_ASSERT_NOT_NULL(out.body.bytes.data);
  return out;
}

static ssz_ob_t build_block_hash_proof(const uint8_t block_hash[32]) {
  ssz_builder_t proof = ssz_builder_for_type(ETH_SSZ_VERIFY_BLOCK_PROOF);
  uint8_t       none  = 0;
  ssz_add_bytes(&proof, "body", bytes(&none, 1));
  uint8_t block_union[33] = {0};
  memcpy(block_union + 1, block_hash, 32);
  ssz_add_bytes(&proof, "block", bytes(block_union, sizeof(block_union)));
  return ssz_builder_to_bytes(&proof);
}

static ssz_ob_t build_sequencer_proof(bytes_t payload, bool uncompressed, bytes_t signature) {
  ssz_builder_t    seq  = ssz_builder_for_type(ETH_SSZ_SEQUENCER_PROOF);
  const ssz_def_t* pdef = ssz_get_def(seq.def, "payload");
  TEST_ASSERT_NOT_NULL(pdef);
  ssz_builder_t payload_builder = ssz_builder_for_def(pdef->def.container.elements + (uncompressed ? 1 : 0));
  buffer_append(&payload_builder.fixed, payload);
  ssz_add_builders(&seq, "payload", payload_builder);
  ssz_add_bytes(&seq, "signature", signature);
  return ssz_builder_to_bytes(&seq);
}

void test_sequencer_proof_union_index(void) {
  const ssz_def_t* def = eth_ssz_verification_type(ETH_SSZ_SEQUENCER_PROOF);
  TEST_ASSERT_NOT_NULL(def);
  TEST_ASSERT_EQUAL_STRING("sequencerProof", def->name);
}

void test_block_proof_union_index_2_is_sequencer(void) {
  const ssz_def_t* proof = eth_ssz_verification_type(ETH_SSZ_VERIFY_BLOCK_PROOF);
  TEST_ASSERT_NOT_NULL(proof);
  const ssz_def_t* block = ssz_get_def(proof, "block");
  TEST_ASSERT_NOT_NULL(block);
  TEST_ASSERT_EQUAL_INT(SSZ_TYPE_UNION, block->type);
  TEST_ASSERT_EQUAL_INT(3, block->def.container.len);
  TEST_ASSERT_EQUAL_STRING("blockHash", block->def.container.elements[0].name);
  TEST_ASSERT_EQUAL_STRING("clProof", block->def.container.elements[1].name);
  TEST_ASSERT_EQUAL_STRING("sequencerProof", block->def.container.elements[2].name);
}

void test_verify_block_without_hook_rejects_unknown_variant(void) {
  c4_register_block_proof_verify(C4_CHAIN_TYPE_OP, NULL);
  ssz_ob_t  block      = {.def = eth_ssz_verification_type(ETH_SSZ_SEQUENCER_PROOF), .bytes = NULL_BYTES};
  bytes_t   el_header  = NULL_BYTES;
  bytes32_t block_hash = {0};
  TEST_ASSERT_EQUAL_INT(C4_ERROR, c4_verify_block(&g_ctx, block, &el_header, block_hash));
  TEST_ASSERT_NOT_NULL(g_ctx.state.error);
}

void test_unsupported_chain(void) {
  g_ctx.chain_id       = CHAIN_ID(C4_CHAIN_TYPE_OP, 12345);
  uint8_t   sig[65]    = {0};
  uint8_t   payload[4] = {1, 2, 3, 4};
  ssz_ob_t  proof      = build_sequencer_proof(bytes(payload, sizeof(payload)), true, bytes(sig, 65));
  bytes_t   el_header  = NULL_BYTES;
  bytes32_t block_hash = {0};
  TEST_ASSERT_EQUAL_INT(C4_ERROR, op_verify_sequencer_proof(&g_ctx, proof, &el_header, block_hash));
  TEST_ASSERT_NOT_NULL(g_ctx.state.error);
  TEST_ASSERT_NOT_NULL(strstr(g_ctx.state.error, "chain not supported"));
  safe_free(proof.bytes.data);
}

void test_invalid_zstd(void) {
  uint8_t   sig[65]    = {0};
  uint8_t   payload[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
  ssz_ob_t  proof      = build_sequencer_proof(bytes(payload, sizeof(payload)), false, bytes(sig, 65));
  bytes_t   el_header  = NULL_BYTES;
  bytes32_t block_hash = {0};
  TEST_ASSERT_EQUAL_INT(C4_ERROR, op_verify_sequencer_proof(&g_ctx, proof, &el_header, block_hash));
  TEST_ASSERT_NOT_NULL(g_ctx.state.error);
  safe_free(proof.bytes.data);
}

void test_invalid_signature(void) {
  uint8_t  sig[65] = {0};
  uint8_t  raw[64] = {0};
  raw[0]           = 1;
  ssz_ob_t  proof      = build_sequencer_proof(bytes(raw, sizeof(raw)), true, bytes(sig, 65));
  bytes_t   el_header  = NULL_BYTES;
  bytes32_t block_hash = {0};
  TEST_ASSERT_EQUAL_INT(C4_ERROR, op_verify_sequencer_proof(&g_ctx, proof, &el_header, block_hash));
  TEST_ASSERT_NOT_NULL(g_ctx.state.error);
  TEST_ASSERT_TRUE(strstr(g_ctx.state.error, "invalid sequencer signature") != NULL ||
                   strstr(g_ctx.state.error, "invalid execution payload") != NULL ||
                   strstr(g_ctx.state.error, "preconf payload too short") != NULL);
  safe_free(proof.bytes.data);
}

void test_op_request_type_is_eth(void) {
  const ssz_def_t* op  = c4_op_get_request_type(C4_CHAIN_TYPE_OP);
  const ssz_def_t* eth = eth_ssz_verification_type(ETH_SSZ_VERIFY_REQUEST);
  TEST_ASSERT_EQUAL_PTR(eth, op);
}

void test_preconf_too_short(void) {
  uint8_t  raw[32] = {0};
  bytes_t  header  = NULL_BYTES;
  ssz_ob_t body    = {0};
  bytes32_t hash   = {0};
  TEST_ASSERT_EQUAL_INT(C4_ERROR, op_el_from_preconf_bytes(&g_ctx.state, bytes(raw, sizeof(raw)), &header, &body, hash));
  TEST_ASSERT_NOT_NULL(g_ctx.state.error);
  TEST_ASSERT_NOT_NULL(strstr(g_ctx.state.error, "preconf payload too short"));
}

void test_keccak_mismatch(void) {
  uint8_t  dummy[32] = {0};
  dummy[0]           = 0xDE;
  dummy[1]           = 0xAD;
  ssz_ob_t ep        = build_deneb_payload(dummy, TEST_BLOCK_NUMBER);
  TEST_ASSERT_TRUE_MESSAGE(ssz_is_valid(ep, true, NULL), "synthetic Deneb payload must be valid SSZ");
  bytes_t  preconf = wrap_preconf(ep);
  bytes_t  header  = NULL_BYTES;
  ssz_ob_t body    = {0};
  bytes32_t hash   = {0};
  TEST_ASSERT_EQUAL_INT(C4_ERROR, op_el_from_preconf_bytes(&g_ctx.state, preconf, &header, &body, hash));
  TEST_ASSERT_NOT_NULL(g_ctx.state.error);
  TEST_ASSERT_NOT_NULL(strstr(g_ctx.state.error, "blockHash does not match keccak"));
  TEST_ASSERT_NULL(header.data);
  TEST_ASSERT_NULL(body.bytes.data);
  safe_free(ep.bytes.data);
  safe_free(preconf.data);
}

void test_keccak_match_deneb(void) {
  matching_el_t el = build_matching_el(TEST_BLOCK_NUMBER);
  TEST_ASSERT_EQUAL_UINT64(TEST_BLOCK_NUMBER, eth_el_header_get_uint64(el.header, EL_BLOCK_NUMBER));
  bytes32_t recomputed = {0};
  keccak(el.header, recomputed);
  TEST_ASSERT_EQUAL_MEMORY(el.hash, recomputed, 32);
  matching_el_free(&el);
}

#ifdef EL_HEADER_CACHE

void test_blockhash_followup_after_cached_sequencer_header(void) {
  matching_el_t el = build_matching_el(TEST_BLOCK_NUMBER);
  c4_header_cache_put(g_ctx.chain_id, TEST_BLOCK_NUMBER, el.hash, el.header, &el.body);

  static const ssz_def_t hash_def = SSZ_BYTES32("blockHash");
  ssz_ob_t               block    = {.def = &hash_def, .bytes = bytes(el.hash, 32)};
  bytes_t                header   = NULL_BYTES;
  bytes32_t              out_hash = {0};
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, c4_verify_block(&g_ctx, block, &header, out_hash));
  TEST_ASSERT_EQUAL_MEMORY(el.hash, out_hash, 32);
  TEST_ASSERT_EQUAL_UINT32(el.header.len, header.len);
  TEST_ASSERT_EQUAL_MEMORY(el.header.data, header.data, el.header.len);
  matching_el_free(&el);
}

void test_verify_block_proof_body_from_cache(void) {
  matching_el_t el = build_matching_el(TEST_BLOCK_NUMBER);
  c4_header_cache_put(g_ctx.chain_id, TEST_BLOCK_NUMBER, el.hash, el.header, &el.body);

  g_ctx.proof  = build_block_hash_proof(el.hash);
  g_ctx.method = "eth_getBlockByNumber";
  g_ctx.args   = json_parse("[\"0x2a\",false]");

  TEST_ASSERT_TRUE_MESSAGE(verify_block_proof(&g_ctx), g_ctx.state.error ? g_ctx.state.error : "verify_block_proof failed");
  TEST_ASSERT_TRUE(g_ctx.success);
  TEST_ASSERT_NOT_NULL(g_ctx.data.bytes.data);

  safe_free(g_ctx.proof.bytes.data);
  g_ctx.proof.bytes.data = NULL;
  matching_el_free(&el);
}

void test_verify_block_proof_missing_body_without_cache(void) {
  matching_el_t el = build_matching_el(TEST_BLOCK_NUMBER);
  c4_header_cache_put(g_ctx.chain_id, TEST_BLOCK_NUMBER, el.hash, el.header, NULL);

  g_ctx.proof  = build_block_hash_proof(el.hash);
  g_ctx.method = "eth_getBlockByNumber";
  g_ctx.args   = json_parse("[\"0x2a\",false]");

  TEST_ASSERT_FALSE(verify_block_proof(&g_ctx));
  TEST_ASSERT_NOT_NULL(g_ctx.state.error);
  TEST_ASSERT_NOT_NULL(strstr(g_ctx.state.error, "missing body for block proof"));

  safe_free(g_ctx.proof.bytes.data);
  g_ctx.proof.bytes.data = NULL;
  matching_el_free(&el);
}

void test_verify_block_proof_cached_body_root_mismatch(void) {
  matching_el_t el = build_matching_el(TEST_BLOCK_NUMBER);

  ssz_builder_t body_b = ssz_builder_for_type(ETH_SSZ_EL_BLOCK_CONTENT);
  ssz_builder_t txs    = ssz_builder_for_def(ssz_get_def(body_b.def, "transactions"));
  uint8_t       raw_tx[] = {0x01, 0x02, 0x03};
  ssz_add_dynamic_list_bytes(&txs, 1, bytes(raw_tx, sizeof(raw_tx)));
  ssz_add_builders(&body_b, "transactions", txs);
  ssz_add_bytes(&body_b, "withdrawals", NULL_BYTES);
  ssz_ob_t bad_body = ssz_builder_to_bytes(&body_b);

  c4_header_cache_put(g_ctx.chain_id, TEST_BLOCK_NUMBER, el.hash, el.header, &bad_body);
  safe_free(bad_body.bytes.data);

  g_ctx.proof  = build_block_hash_proof(el.hash);
  g_ctx.method = "eth_getBlockByNumber";
  g_ctx.args   = json_parse("[\"0x2a\",false]");

  TEST_ASSERT_FALSE(verify_block_proof(&g_ctx));
  TEST_ASSERT_NOT_NULL(g_ctx.state.error);
  TEST_ASSERT_NOT_NULL(strstr(g_ctx.state.error, "invalid transaction root"));

  safe_free(g_ctx.proof.bytes.data);
  g_ctx.proof.bytes.data = NULL;
  matching_el_free(&el);
}

void test_op_get_el_block_from_header_cache(void) {
  matching_el_t el = build_matching_el(TEST_BLOCK_NUMBER);
  c4_header_cache_put(g_ctx.chain_id, TEST_BLOCK_NUMBER, el.hash, el.header, &el.body);

  prover_ctx_t pctx = {0};
  pctx.chain_id     = g_ctx.chain_id;
  memcpy(pctx.last_block_hash, el.hash, 32);

  static const char hex[] = "0123456789abcdef";
  char              tag[69];
  tag[0] = '"';
  tag[1] = '0';
  tag[2] = 'x';
  for (int i = 0; i < 32; i++) {
    tag[3 + i * 2] = hex[el.hash[i] >> 4];
    tag[4 + i * 2] = hex[el.hash[i] & 0x0f];
  }
  tag[67] = '"';
  tag[68] = '\0';

  eth_block_t out = {0};
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, op_get_el_block(&pctx, json_parse(tag), &out, true));
  TEST_ASSERT_EQUAL_INT(C4_BLOCK_PROOF_TYPE_NONE, out.proof_type);
  TEST_ASSERT_EQUAL_MEMORY(el.hash, out.el_block_hash, 32);
  TEST_ASSERT_NOT_NULL(out.el_header.data);
  TEST_ASSERT_NOT_NULL(out.el_body.bytes.data);
  TEST_ASSERT_NULL(out.sequencer.payload.data);

  c4_state_free(&pctx.state);
  matching_el_free(&el);
}

#endif /* EL_HEADER_CACHE */

void test_op_add_sequencer_proof_requires_payload(void) {
  prover_ctx_t      pctx     = {0};
  eth_block_t       block    = {0};
  blockroot_proof_t historic = {0};
  ssz_builder_t     parent   = ssz_builder_for_type(ETH_SSZ_VERIFY_BLOCK_PROOF);
  uint8_t           none     = 0;
  ssz_add_bytes(&parent, "body", bytes(&none, 1));
  TEST_ASSERT_FALSE(op_add_sequencer_proof(&pctx, &parent, &block, &historic));
  ssz_builder_free(&parent);
}

void test_op_add_sequencer_proof_writes_union_index_2(void) {
  prover_ctx_t      pctx     = {0};
  eth_block_t       block    = {0};
  blockroot_proof_t historic = {0};
  uint8_t           payload[] = {1, 2, 3, 4};
  uint8_t           sig[65]   = {0};
  sig[64]                     = 27;
  block.proof_type          = C4_BLOCK_PROOF_TYPE_SEQUENCER;
  block.sequencer.payload   = bytes(payload, sizeof(payload));
  block.sequencer.signature = bytes(sig, 65);

  ssz_builder_t parent = ssz_builder_for_type(ETH_SSZ_VERIFY_BLOCK_PROOF);
  uint8_t       none   = 0;
  ssz_add_bytes(&parent, "body", bytes(&none, 1));
  TEST_ASSERT_TRUE(op_add_sequencer_proof(&pctx, &parent, &block, &historic));
  ssz_ob_t proof = ssz_builder_to_bytes(&parent);
  ssz_ob_t ublock = ssz_get(&proof, "block");
  TEST_ASSERT_NOT_NULL(ublock.def);
  TEST_ASSERT_EQUAL_STRING("sequencerProof", ublock.def->name);
  safe_free(proof.bytes.data);
}

void test_op_method_type_pending_unproofable(void) {
  TEST_ASSERT_EQUAL_INT(METHOD_UNPROOFABLE,
                        c4_get_method_type(C4_CHAIN_OP_MAINNET, "eth_getBlockByNumber", json_parse("[\"pending\",false]"), 0));
  TEST_ASSERT_EQUAL_INT(METHOD_UNPROOFABLE,
                        c4_get_method_type(C4_CHAIN_OP_MAINNET, "eth_getBalance", json_parse("[\"0xabc\",\"earliest\"]"), 0));
  TEST_ASSERT_EQUAL_INT(METHOD_PROOFABLE,
                        c4_get_method_type(C4_CHAIN_OP_MAINNET, "eth_getBlockByNumber", json_parse("[\"0x2a\",false]"), 0));
  TEST_ASSERT_EQUAL_INT(METHOD_LOCAL,
                        c4_get_method_type(C4_CHAIN_OP_MAINNET, "eth_chainId", json_parse("[]"), 0));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_sequencer_proof_union_index);
  RUN_TEST(test_block_proof_union_index_2_is_sequencer);
  RUN_TEST(test_verify_block_without_hook_rejects_unknown_variant);
  RUN_TEST(test_unsupported_chain);
  RUN_TEST(test_invalid_zstd);
  RUN_TEST(test_invalid_signature);
  RUN_TEST(test_op_request_type_is_eth);
  RUN_TEST(test_preconf_too_short);
  RUN_TEST(test_keccak_mismatch);
  RUN_TEST(test_keccak_match_deneb);
#ifdef EL_HEADER_CACHE
  RUN_TEST(test_blockhash_followup_after_cached_sequencer_header);
  RUN_TEST(test_verify_block_proof_body_from_cache);
  RUN_TEST(test_verify_block_proof_missing_body_without_cache);
  RUN_TEST(test_verify_block_proof_cached_body_root_mismatch);
  RUN_TEST(test_op_get_el_block_from_header_cache);
#endif
  RUN_TEST(test_op_add_sequencer_proof_requires_payload);
  RUN_TEST(test_op_add_sequencer_proof_writes_union_index_2);
  RUN_TEST(test_op_method_type_pending_unproofable);
  return UNITY_END();
}

#else

void setUp(void) {}
void tearDown(void) {}

int main(void) {
  UNITY_BEGIN();
  return UNITY_END();
}

#endif
