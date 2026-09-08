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

// datei: test_addiere.c
#include "beacon_types.h"
#include "bytes.h"
#include "c4_assert.h"
#include "chains.h"
#include "eth_verify.h"
#include "ssz.h"
#include "unity.h"
#include <string.h>

void setUp(void) {
}

void tearDown(void) {
  c4_register_block_proof_verify(C4_CHAIN_TYPE_ETHEREUM, NULL);
}

static c4_status_t dummy_accept_block(verify_ctx_t* ctx, ssz_ob_t block, bytes_t* el_header, bytes32_t block_hash) {
  (void) ctx;
  (void) block;
  (void) el_header;
  (void) block_hash;
  return C4_SUCCESS;
}

static ssz_ob_t build_witness_block_proof(bytes_t el_header, uint32_t n, const uint8_t (*addrs)[20], const uint8_t (*sigs)[65]) {
  ssz_builder_t    proof    = ssz_builder_for_type(ETH_SSZ_WITNESS_HEADER_PROOF);
  const ssz_def_t* list_def = ssz_get_def(proof.def, "witnesses");
  TEST_ASSERT_NOT_NULL(list_def);

  ssz_add_bytes(&proof, "elHeader", el_header);
  ssz_builder_t list = ssz_builder_for_def(list_def);
  for (uint32_t i = 0; i < n; i++) {
    ssz_builder_t w = ssz_builder_for_def(list_def->def.vector.type);
    ssz_add_bytes(&w, "address", bytes((uint8_t*) addrs[i], 20));
    ssz_add_bytes(&w, "signature", bytes((uint8_t*) sigs[i], 65));
    ssz_add_dynamic_list_builders(&list, (int) n, w);
  }
  ssz_add_builders(&proof, "witnesses", list);
  return ssz_builder_to_bytes(&proof);
}

void test_ssz() {
  buffer_t buf       = {0};
  buffer_t tmp       = {0};
  bytes_t  data      = read_testdata("body.ssz");
  ssz_ob_t ssz       = {.def = eth_ssz_type_for_fork(ETH_SSZ_SIGNED_BEACON_BLOCK_CONTAINER, C4_FORK_DENEB, C4_CHAIN_MAINNET), .bytes = data};
  json_t   json      = json_parse(bprintf(&buf, "%z\n", ssz));
  json_t   signature = json_get(json, "signature");
  char*    sig       = json_as_string(signature, &tmp);
  TEST_ASSERT_EQUAL_STRING("0xb54bfc2475721ef6377a50017bb94064272a8d9190a055d032c5c4fe28d26c7c4fc5864778df1eebe9b943372e2e52ae068776ce8aec4c1bcf4d9dda5a72fd86e3d13e7b3b5dfe8ce9a59ec91e62f576d9d7ea8bba10c90bd6d5ff6c506fbecc", sig);
  buffer_free(&tmp);
  buffer_free(&buf);
  safe_free(data.data);
}

void test_request_proofs_union_order(void) {
  static const char* expected[] = {
      "NONE",
      "AccountProof",
      "TransactionProof",
      "ReceiptProof",
      "LogsProof",
      "LogsCompletenessProof",
      "CallProof",
      "BlockProof",
      "BlockReceiptsProof",
      "SyncProof",
  };
  const ssz_def_t* request      = eth_ssz_verification_type(ETH_SSZ_VERIFY_REQUEST);
  const ssz_def_t* proofs       = ssz_get_def(request, "proof");
  const ssz_def_t* logs         = eth_ssz_verification_type(ETH_SSZ_VERIFY_LOGS_PROOF);
  const ssz_def_t* completeness = eth_ssz_verification_type(ETH_SSZ_VERIFY_LOGS_COMPLETENESS_PROOF);
  const ssz_def_t* call         = eth_ssz_verification_type(ETH_SSZ_VERIFY_CALL_PROOF);
  const ssz_def_t* block        = eth_ssz_verification_type(ETH_SSZ_VERIFY_BLOCK_PROOF);
  const ssz_def_t* receipts     = eth_ssz_verification_type(ETH_SSZ_VERIFY_BLOCK_RECEIPTS_PROOF);
  const ssz_def_t* sync         = eth_ssz_verification_type(ETH_SSZ_VERIFY_SYNC_PROOF);

  TEST_ASSERT_NOT_NULL(proofs);
  TEST_ASSERT_EQUAL_INT(SSZ_TYPE_UNION, proofs->type);
  TEST_ASSERT_EQUAL_INT(10, proofs->def.container.len);
  for (int i = 0; i < 10; i++)
    TEST_ASSERT_EQUAL_STRING(expected[i], proofs->def.container.elements[i].name);

  TEST_ASSERT_NOT_NULL(logs);
  TEST_ASSERT_NOT_NULL(completeness);
  TEST_ASSERT_NOT_NULL(call);
  TEST_ASSERT_NOT_NULL(block);
  TEST_ASSERT_NOT_NULL(receipts);
  TEST_ASSERT_NOT_NULL(sync);
  TEST_ASSERT_EQUAL_PTR_MESSAGE(logs + 1, completeness, "LogsCompletenessProof must sit next to LogsProof");
  TEST_ASSERT_EQUAL_PTR_MESSAGE(completeness + 1, call, "CallProof must follow LogsCompletenessProof");
  TEST_ASSERT_EQUAL_PTR_MESSAGE(call + 1, block, "BlockProof must follow CallProof");
  TEST_ASSERT_EQUAL_PTR_MESSAGE(receipts + 1, sync, "SyncProof must be the last request-proof variant");
}

void test_verify_list_kinds(void) {
  const ssz_def_t* logs_proof = eth_ssz_verification_type(ETH_SSZ_VERIFY_LOGS_PROOF);
  const ssz_def_t* eth_logs   = eth_ssz_verification_type(ETH_SSZ_DATA_LOGS);
  TEST_ASSERT_NOT_NULL(logs_proof);
  TEST_ASSERT_NOT_NULL(eth_logs);
  TEST_ASSERT_EQUAL_STRING("LogsProof", logs_proof->name);
  TEST_ASSERT_EQUAL_STRING("EthLogs", eth_logs->name);
  TEST_ASSERT_EQUAL_INT_MESSAGE(SSZ_TYPE_PROG_LIST, logs_proof->type,
                                "LogsProof must be a progressive list (no encoding cap)");
  TEST_ASSERT_EQUAL_INT_MESSAGE(SSZ_TYPE_PROG_LIST, eth_logs->type,
                                "EthLogs must be a progressive list (no encoding cap)");

  const ssz_def_t* witnesses = ssz_get_def(eth_ssz_verification_type(ETH_SSZ_WITNESS_HEADER_PROOF), "witnesses");
  TEST_ASSERT_NOT_NULL(witnesses);
  TEST_ASSERT_EQUAL_INT_MESSAGE(SSZ_TYPE_LIST, witnesses->type, "witnesses stay a capped SSZ list");
  TEST_ASSERT_EQUAL_UINT32(16, witnesses->def.vector.len);

  const ssz_def_t* signatures = ssz_get_def(eth_ssz_verification_type(ETH_SSZ_VERIFY_ZK_SYNCDATA), "signatures");
  TEST_ASSERT_NOT_NULL(signatures);
  TEST_ASSERT_EQUAL_INT_MESSAGE(SSZ_TYPE_LIST, signatures->type, "ZK signatures stay a capped SSZ list");
  TEST_ASSERT_EQUAL_UINT32(16, signatures->def.vector.len);

  const ssz_def_t* cl_header_proof = ssz_get_def(eth_ssz_verification_type(ETH_SSZ_CL_HEADER_PROOF), "clHeaderProof");
  TEST_ASSERT_NOT_NULL(cl_header_proof);
  TEST_ASSERT_EQUAL_INT(SSZ_TYPE_UNION, cl_header_proof->type);
  TEST_ASSERT_TRUE(cl_header_proof->def.container.len > 2);
  const ssz_def_t* header_chain = cl_header_proof->def.container.elements + 2;
  TEST_ASSERT_EQUAL_STRING("headerChain", header_chain->name);
  const ssz_def_t* headers = ssz_get_def(header_chain, "headers");
  TEST_ASSERT_NOT_NULL(headers);
  TEST_ASSERT_EQUAL_INT_MESSAGE(SSZ_TYPE_LIST, headers->type, "headerChain.headers stay a capped SSZ list");
  TEST_ASSERT_EQUAL_UINT32(128, headers->def.vector.len);
}

void test_witness_block_proof_type(void) {
  const ssz_def_t* cl      = eth_ssz_verification_type(ETH_SSZ_CL_HEADER_PROOF);
  const ssz_def_t* seq     = eth_ssz_verification_type(ETH_SSZ_SEQUENCER_PROOF);
  const ssz_def_t* witness = eth_ssz_verification_type(ETH_SSZ_WITNESS_HEADER_PROOF);

  TEST_ASSERT_NOT_NULL(cl);
  TEST_ASSERT_NOT_NULL(seq);
  TEST_ASSERT_NOT_NULL(witness);
  TEST_ASSERT_EQUAL_STRING("clProof", cl->name);
  TEST_ASSERT_EQUAL_STRING("sequencerProof", seq->name);
  TEST_ASSERT_EQUAL_STRING("witnessProof", witness->name);
  TEST_ASSERT_EQUAL_PTR_MESSAGE(cl + 1, seq, "sequencerProof must stay at ETH_EL_PROOF_UNION index 2");
  TEST_ASSERT_EQUAL_PTR_MESSAGE(seq + 1, witness, "witnessProof must be ETH_EL_PROOF_UNION index 3");
  TEST_ASSERT_NOT_NULL(ssz_get_def(witness, "elHeader"));

  const ssz_def_t* witnesses = ssz_get_def(witness, "witnesses");
  TEST_ASSERT_NOT_NULL(witnesses);
  TEST_ASSERT_EQUAL_INT(SSZ_TYPE_LIST, witnesses->type);
  TEST_ASSERT_EQUAL_UINT32(16, witnesses->def.vector.len);
  TEST_ASSERT_NOT_NULL(witnesses->def.vector.type);
  TEST_ASSERT_EQUAL_STRING("BlockhashWitness", witnesses->def.vector.type->name);

  const ssz_def_t* address   = ssz_get_def(witnesses->def.vector.type, "address");
  const ssz_def_t* signature = ssz_get_def(witnesses->def.vector.type, "signature");
  TEST_ASSERT_NOT_NULL(address);
  TEST_ASSERT_NOT_NULL(signature);
  TEST_ASSERT_EQUAL_INT(SSZ_TYPE_VECTOR, address->type);
  TEST_ASSERT_EQUAL_INT(SSZ_TYPE_VECTOR, signature->type);
  TEST_ASSERT_EQUAL_UINT32(20, address->def.vector.len);
  TEST_ASSERT_EQUAL_UINT32(65, signature->def.vector.len);
}

static const ssz_def_t* proof_container(eth_ssz_type_t type) {
  const ssz_def_t* def = eth_ssz_verification_type(type);
  TEST_ASSERT_NOT_NULL(def);
  if (def->type == SSZ_TYPE_LIST || def->type == SSZ_TYPE_PROG_LIST)
    def = def->def.vector.type;
  TEST_ASSERT_NOT_NULL(def);
  return def;
}

static void assert_el_proof_field(eth_ssz_type_t type) {
  const ssz_def_t* def = proof_container(type);
  TEST_ASSERT_NOT_NULL_MESSAGE(ssz_get_def(def, "elProof"), def->name);
  TEST_ASSERT_NULL_MESSAGE(ssz_get_def(def, "block"), def->name);
}

void test_el_proof_and_header_proof_names(void) {
  assert_el_proof_field(ETH_SSZ_VERIFY_ACCOUNT_PROOF);
  assert_el_proof_field(ETH_SSZ_VERIFY_TRANSACTION_PROOF);
  assert_el_proof_field(ETH_SSZ_VERIFY_RECEIPT_PROOF);
  assert_el_proof_field(ETH_SSZ_VERIFY_LOGS_PROOF);
  assert_el_proof_field(ETH_SSZ_VERIFY_LOGS_COMPLETENESS_PROOF);
  assert_el_proof_field(ETH_SSZ_VERIFY_CALL_PROOF);
  assert_el_proof_field(ETH_SSZ_VERIFY_BLOCK_PROOF);
  assert_el_proof_field(ETH_SSZ_VERIFY_BLOCK_RECEIPTS_PROOF);

  const ssz_def_t* completeness = eth_ssz_verification_type(ETH_SSZ_VERIFY_LOGS_COMPLETENESS_PROOF);
  const ssz_def_t* blocks       = ssz_get_def(completeness, "blocks");
  TEST_ASSERT_NOT_NULL(blocks);
  TEST_ASSERT_NOT_NULL(blocks->def.vector.type);
  TEST_ASSERT_EQUAL_STRING_MESSAGE("block", blocks->def.vector.type->name,
                                   "completeness inner union must stay block, not elProof");

  const ssz_def_t* cl = eth_ssz_verification_type(ETH_SSZ_CL_HEADER_PROOF);
  TEST_ASSERT_NOT_NULL(ssz_get_def(cl, "clHeaderProof"));
  TEST_ASSERT_NULL_MESSAGE(ssz_get_def(cl, "headerProof"), "CL field must be clHeaderProof, not headerProof");
  TEST_ASSERT_EQUAL_STRING("blockHash", (cl - 1)->name);

  const ssz_def_t* cl_header_proof = ssz_get_def(cl, "clHeaderProof");
  TEST_ASSERT_EQUAL_INT(SSZ_TYPE_UNION, cl_header_proof->type);
  TEST_ASSERT_EQUAL_INT(4, cl_header_proof->def.container.len);
  TEST_ASSERT_EQUAL_STRING("signature", cl_header_proof->def.container.elements[0].name);
  TEST_ASSERT_EQUAL_STRING("historic", cl_header_proof->def.container.elements[1].name);
  TEST_ASSERT_EQUAL_STRING("headerChain", cl_header_proof->def.container.elements[2].name);
  TEST_ASSERT_EQUAL_STRING("checkpoint", cl_header_proof->def.container.elements[3].name);

  const ssz_def_t* checkpoint = eth_ssz_verification_type(ETH_SSZ_VERIFY_CHECKPOINT_PROOF);
  TEST_ASSERT_NOT_NULL(checkpoint);
  TEST_ASSERT_EQUAL_STRING("checkpoint", checkpoint->name);
  TEST_ASSERT_EQUAL_PTR(cl_header_proof->def.container.elements + 3, checkpoint);

  const ssz_def_t* bootstrap_deneb = eth_get_light_client_bootstrap(C4_FORK_DENEB);
  TEST_ASSERT_NOT_NULL(bootstrap_deneb);
  TEST_ASSERT_EQUAL_STRING_MESSAGE("CheckpointProof", (bootstrap_deneb + 2)->name,
                                   "bootstrap union index 3 must stay CheckpointProof");
}

void test_witness_block_proof_not_implemented(void) {
  ssz_ob_t     block      = {.def = eth_ssz_verification_type(ETH_SSZ_WITNESS_HEADER_PROOF), .bytes = NULL_BYTES};
  verify_ctx_t ctx        = {0};
  bytes_t      el_header  = {0};
  bytes32_t    block_hash = {0};

  TEST_ASSERT_EQUAL_INT(C4_ERROR, c4_verify_block(&ctx, block, &el_header, block_hash));
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  TEST_ASSERT_EQUAL_STRING("witnessProof is not implemented yet", ctx.state.error);
  c4_state_free(&ctx.state);
}

void test_witness_block_proof_encode_roundtrip(void) {
  uint8_t header[] = {0xc0, 0x01, 0x02};
  uint8_t addrs[1][20];
  uint8_t sigs[1][65];
  memset(addrs, 0x11, sizeof(addrs));
  memset(sigs, 0x22, sizeof(sigs));
  sigs[0][64] = 27;

  ssz_ob_t proof = build_witness_block_proof(bytes(header, sizeof(header)), 1, addrs, sigs);
  TEST_ASSERT_TRUE_MESSAGE(ssz_is_valid(proof, true, NULL), "encoded witnessProof must be valid SSZ");
  TEST_ASSERT_EQUAL_STRING("witnessProof", proof.def->name);

  ssz_ob_t el = ssz_get(&proof, "elHeader");
  TEST_ASSERT_EQUAL_UINT32(sizeof(header), el.bytes.len);
  TEST_ASSERT_EQUAL_MEMORY(header, el.bytes.data, sizeof(header));

  ssz_ob_t witnesses = ssz_get(&proof, "witnesses");
  TEST_ASSERT_EQUAL_UINT32(1, ssz_len(witnesses));
  ssz_ob_t w = ssz_at(witnesses, 0);
  TEST_ASSERT_EQUAL_MEMORY(addrs[0], ssz_get(&w, "address").bytes.data, 20);
  TEST_ASSERT_EQUAL_MEMORY(sigs[0], ssz_get(&w, "signature").bytes.data, 65);
  TEST_ASSERT_EQUAL_UINT32(65, ssz_get(&w, "signature").bytes.len);

  ssz_builder_t parent = ssz_builder_for_type(ETH_SSZ_VERIFY_BLOCK_PROOF);
  uint8_t       none   = 0;
  ssz_add_bytes(&parent, "body", bytes(&none, 1));
  ssz_add_ob(&parent, "elProof", proof);
  ssz_ob_t block_proof = ssz_builder_to_bytes(&parent);
  TEST_ASSERT_TRUE(ssz_is_valid(block_proof, true, NULL));
  ssz_ob_t block = ssz_get(&block_proof, "elProof");
  TEST_ASSERT_EQUAL_STRING("witnessProof", block.def->name);

  safe_free(block_proof.bytes.data);
  safe_free(proof.bytes.data);
}

void test_witness_block_proof_empty_list(void) {
  uint8_t  header[] = {0xc0};
  ssz_ob_t proof    = build_witness_block_proof(bytes(header, sizeof(header)), 0, NULL, NULL);
  TEST_ASSERT_TRUE_MESSAGE(ssz_is_valid(proof, true, NULL), "empty witnesses list must be valid SSZ");
  TEST_ASSERT_EQUAL_UINT32(0, ssz_len(ssz_get(&proof, "witnesses")));

  verify_ctx_t ctx        = {0};
  bytes_t      el_header  = {0};
  bytes32_t    block_hash = {0};
  TEST_ASSERT_EQUAL_INT(C4_ERROR, c4_verify_block(&ctx, proof, &el_header, block_hash));
  TEST_ASSERT_EQUAL_STRING("witnessProof is not implemented yet", ctx.state.error);

  c4_state_free(&ctx.state);
  safe_free(proof.bytes.data);
}

void test_witness_block_proof_max_16(void) {
  uint8_t header[] = {0xc0};
  uint8_t addrs[17][20];
  uint8_t sigs[17][65];
  memset(addrs, 0, sizeof(addrs));
  memset(sigs, 0, sizeof(sigs));
  for (int i = 0; i < 17; i++) addrs[i][0] = (uint8_t) (i + 1);

  ssz_ob_t ok = build_witness_block_proof(bytes(header, sizeof(header)), 16, addrs, sigs);
  TEST_ASSERT_TRUE_MESSAGE(ssz_is_valid(ok, true, NULL), "16 witnesses must be within the list cap");
  TEST_ASSERT_EQUAL_UINT32(16, ssz_len(ssz_get(&ok, "witnesses")));

  ssz_ob_t   too_many = build_witness_block_proof(bytes(header, sizeof(header)), 17, addrs, sigs);
  c4_state_t state    = {0};
  TEST_ASSERT_FALSE_MESSAGE(ssz_is_valid(too_many, true, &state), "17 witnesses must exceed the list cap");
  TEST_ASSERT_NOT_NULL(state.error);

  c4_state_free(&state);
  safe_free(ok.bytes.data);
  safe_free(too_many.bytes.data);
}

void test_witness_block_proof_rejects_after_hook(void) {
  uint8_t  header[] = {0xc0};
  ssz_ob_t proof    = build_witness_block_proof(bytes(header, sizeof(header)), 0, NULL, NULL);
  TEST_ASSERT_TRUE(ssz_is_valid(proof, true, NULL));

  c4_register_block_proof_verify(C4_CHAIN_TYPE_ETHEREUM, dummy_accept_block);

  verify_ctx_t ctx     = {0};
  ctx.chain_id         = C4_CHAIN_MAINNET;
  bytes_t   el_header  = {0};
  bytes32_t block_hash = {0};
  TEST_ASSERT_EQUAL_INT(C4_ERROR, c4_verify_block(&ctx, proof, &el_header, block_hash));
  TEST_ASSERT_EQUAL_STRING_MESSAGE("witnessProof is not implemented yet", ctx.state.error,
                                   "witnessProof must be rejected before chain hooks");

  c4_register_block_proof_verify(C4_CHAIN_TYPE_ETHEREUM, NULL);
  c4_state_free(&ctx.state);
  safe_free(proof.bytes.data);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_ssz);
  RUN_TEST(test_request_proofs_union_order);
  RUN_TEST(test_verify_list_kinds);
  RUN_TEST(test_witness_block_proof_type);
  RUN_TEST(test_el_proof_and_header_proof_names);
  RUN_TEST(test_witness_block_proof_not_implemented);
  RUN_TEST(test_witness_block_proof_encode_roundtrip);
  RUN_TEST(test_witness_block_proof_empty_list);
  RUN_TEST(test_witness_block_proof_max_16);
  RUN_TEST(test_witness_block_proof_rejects_after_hook);
  return UNITY_END();
}