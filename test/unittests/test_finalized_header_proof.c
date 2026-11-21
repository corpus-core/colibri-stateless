/*
 * Copyright (c) 2025,2026 corpus.core GmbH
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

#include "beacon.h"
#include "beacon_types.h"
#include "bytes.h"
#include "eth_verify.h"
#include "historic_proof.h"
#include "ssz.h"
#include "state.h"
#include "sync_committee.h"
#include "unity.h"
#include <stdint.h>
#include <string.h>

static const ssz_def_t ssz_bytes_1024 = SSZ_BYTES("Bytes", 1073741824);
#include "verify_proof_types.h"

static const ssz_def_t HEADER_CONTAINER    = SSZ_CONTAINER("BeaconBlockHeader", BEACON_BLOCK_HEADER);
static const ssz_def_t CL_BLOCK_PROOF      = SSZ_CONTAINER(ETH_SSZ_NAME_CL_PROOF, ETH_CL_HEADER_PROOF);
static const ssz_def_t SYNC_AGG_CONTAINER  = SSZ_CONTAINER("syncAggregate", SYNC_AGGREGATE);
static const ssz_def_t FINALITY_CHUNKS     = SSZ_VECTOR("chunks", ssz_bytes32, 128);
static const ssz_def_t CHECKPOINT_FIELDS[] = {
    SSZ_BIT_VECTOR("sync_committee_bits", 512),
    SSZ_BYTE_VECTOR("sync_committee_signature", 96),
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER),
    SSZ_PROG_LIST("proof", ssz_bytes32),
};
static const ssz_def_t CHECKPOINT_PROOF = SSZ_CONTAINER("signatureFinalized", CHECKPOINT_FIELDS);

void setUp(void) {}
void tearDown(void) {}

static ssz_ob_t build_checkpoint_proof(bytes_t bits, bytes_t attested, bytes_t branch) {
  uint8_t       signature[96] = {0};
  ssz_builder_t builder       = ssz_builder_for_def(&CHECKPOINT_PROOF);
  ssz_add_bytes(&builder, "sync_committee_bits", bits);
  ssz_add_bytes(&builder, "sync_committee_signature", bytes(signature, sizeof(signature)));
  ssz_add_bytes(&builder, "header", attested);
  ssz_add_bytes(&builder, "proof", branch);
  return ssz_builder_to_bytes(&builder);
}

static void fill_bits(uint8_t bits[64], uint32_t count) {
  memset(bits, 0, 64);
  for (uint32_t i = 0; i < count && i < 512; i++)
    bits[i / 8] |= (uint8_t) (1u << (i % 8));
}

static uint64_t mainnet_electra_slot(void) {
  const chain_spec_t* spec = c4_eth_get_chain_spec(C4_CHAIN_MAINNET);
  TEST_ASSERT_NOT_NULL(spec);
  return slot_for_epoch(spec->fork_epochs[C4_FORK_ELECTRA - 1], spec);
}

// Merkle branch whose single leaf is `leaf` at `gindex`. `gindex` must be a depth-7
// index (Electra/Fulu finalized root is 169) so it fits FINALITY_CHUNKS.
static bytes_t finalized_branch(bytes_t leaf, gindex_t gindex, bytes32_t state_root) {
  uint32_t depth = 0;
  for (gindex_t g = gindex >> 1; g; g >>= 1) depth++;
  TEST_ASSERT_EQUAL_UINT32(7, depth);
  uint32_t index = (uint32_t) (gindex - ((gindex_t) 1 << depth));
  TEST_ASSERT_TRUE(index < 128);
  TEST_ASSERT_EQUAL_UINT32(32, leaf.len);
  TEST_ASSERT_EQUAL_UINT64(gindex, ssz_gindex(&FINALITY_CHUNKS, 1, (int) index));

  uint8_t* chunks = safe_calloc(128 * 32, 1);
  TEST_ASSERT_NOT_NULL(chunks);
  memcpy(chunks + (size_t) index * 32, leaf.data, 32);
  ssz_ob_t tree = {.def = &FINALITY_CHUNKS, .bytes = bytes(chunks, 128 * 32)};
  ssz_hash_tree_root(tree, state_root);
  bytes_t proof = ssz_create_proof(tree, state_root, gindex);
  safe_free(chunks);
  TEST_ASSERT_EQUAL_UINT32(depth * 32, proof.len);
  return proof;
}

static ssz_builder_t zero_variant(const ssz_def_t* def) {
  ssz_builder_t    builder  = {.def = def};
  const ssz_def_t* elements = ssz_container_elements(def);
  uint32_t         count    = ssz_container_len(def);
  for (uint32_t i = 0; i < count; i++)
    ssz_add_bytes(&builder, elements[i].name, NULL_BYTES);
  return builder;
}

static ssz_ob_t cl_proof_with_header_variant(ssz_builder_t variant) {
  ssz_builder_t cl = ssz_builder_for_def(&CL_BLOCK_PROOF);
  ssz_add_bytes(&cl, "elHeader", NULL_BYTES);
  ssz_add_bytes(&cl, "clHeader", NULL_BYTES);
  ssz_add_bytes(&cl, "blockhashBranch", NULL_BYTES);
  ssz_add_uint64(&cl, 0);
  ssz_add_builders(&cl, "clHeaderProof", variant);
  return ssz_builder_to_bytes(&cl);
}

static ssz_builder_t fulu_execution_header(const ssz_def_t* header_def) {
  ssz_builder_t b = {.def = ssz_get_def(header_def, "execution")};
  ssz_add_bytes(&b, "parentHash", NULL_BYTES);
  ssz_add_bytes(&b, "feeRecipient", NULL_BYTES);
  ssz_add_bytes(&b, "stateRoot", NULL_BYTES);
  ssz_add_bytes(&b, "receiptsRoot", NULL_BYTES);
  ssz_add_bytes(&b, "logsBloom", NULL_BYTES);
  ssz_add_bytes(&b, "prevRandao", NULL_BYTES);
  ssz_add_uint64(&b, 1);
  ssz_add_uint64(&b, 1);
  ssz_add_uint64(&b, 1);
  ssz_add_uint64(&b, 1);
  ssz_add_bytes(&b, "extraData", NULL_BYTES);
  ssz_add_uint256(&b, NULL_BYTES);
  ssz_add_bytes(&b, "blockHash", NULL_BYTES);
  ssz_add_bytes(&b, "transactionsRoot", NULL_BYTES);
  ssz_add_bytes(&b, "withdrawalsRoot", NULL_BYTES);
  ssz_add_uint64(&b, 0);
  ssz_add_uint64(&b, 0);
  return b;
}

static ssz_builder_t fulu_light_client_header(const ssz_def_t* header_def) {
  ssz_builder_t beacon = {.def = ssz_get_def(header_def, "beacon")};
  ssz_add_uint64(&beacon, 1);
  ssz_add_uint64(&beacon, 0);
  ssz_add_bytes(&beacon, "parentRoot", NULL_BYTES);
  ssz_add_bytes(&beacon, "stateRoot", NULL_BYTES);
  ssz_add_bytes(&beacon, "bodyRoot", NULL_BYTES);

  ssz_builder_t header = {.def = header_def};
  ssz_add_builders(&header, "beacon", beacon);
  ssz_add_builders(&header, "execution", fulu_execution_header(header_def));
  ssz_add_bytes(&header, "executionBranch", NULL_BYTES);
  return header;
}

static verify_ctx_t finalized_balance_ctx(chain_id_t chain_id) {
  verify_ctx_t ctx = {0};
  ctx.method       = "eth_getBalance";
  ctx.args         = json_parse("[\"0x0000000000000000000000000000000000000000\",\"finalized\"]");
  ctx.chain_id     = chain_id;
  return ctx;
}

void test_signature_finalized_is_union_selector_4(void) {
  TEST_ASSERT_EQUAL_INT(5, (int) (sizeof(ETH_HEADER_PROOFS_UNION) / sizeof(ETH_HEADER_PROOFS_UNION[0])));
  TEST_ASSERT_EQUAL_STRING(ETH_SSZ_NAME_SIGNATURE, ETH_HEADER_PROOFS_UNION[0].name);
  TEST_ASSERT_EQUAL_STRING(ETH_SSZ_NAME_CHECKPOINT, ETH_HEADER_PROOFS_UNION[3].name);
  TEST_ASSERT_EQUAL_STRING(ETH_SSZ_NAME_SIGNATURE_FINALIZED, ETH_HEADER_PROOFS_UNION[4].name);
}

void test_version_gate_keeps_legacy_clients_on_signature_proof(void) {
  TEST_ASSERT_FALSE(c4_prover_uses_signature_finalized(0));
  TEST_ASSERT_FALSE(c4_prover_uses_signature_finalized(c4_version_number(1, 1, 27)));
  TEST_ASSERT_FALSE(c4_prover_uses_signature_finalized(c4_version_number(3, 0, 0)));
  TEST_ASSERT_FALSE(c4_prover_uses_signature_finalized(c4_version_number(3, 1, 1)));
  TEST_ASSERT_TRUE(c4_prover_uses_signature_finalized(c4_version_number(3, 1, 2)));
}

void test_finality_update_fork_from_body(void) {
  uint8_t fulu[401] = {0};
  fulu[0]           = 0x90;
  fulu[1]           = 0x01;
  TEST_ASSERT_EQUAL_INT(C4_FORK_FULU, c4_eth_finality_update_fork(bytes(fulu, sizeof(fulu))));

  uint8_t preamble_only[C4_FULU_LIGHT_CLIENT_FINALITY_UPDATE_FIXED] = {0};
  preamble_only[0]                                                  = 0x90;
  preamble_only[1]                                                  = 0x01;
  TEST_ASSERT_EQUAL_INT(C4_FORK_INVALID, c4_eth_finality_update_fork(bytes(preamble_only, sizeof(preamble_only))));

  uint8_t gloas[C4_GLOAS_LIGHT_CLIENT_FINALITY_UPDATE_SIZE] = {0};
  gloas[0]                                                  = 0x90;
  gloas[1]                                                  = 0x01;
  TEST_ASSERT_EQUAL_INT(C4_FORK_GLOAS, c4_eth_finality_update_fork(bytes(gloas, sizeof(gloas))));

  uint8_t short_body[3] = {0};
  TEST_ASSERT_EQUAL_INT(C4_FORK_INVALID, c4_eth_finality_update_fork(bytes(short_body, sizeof(short_body))));
  TEST_ASSERT_EQUAL_INT(C4_FORK_INVALID, c4_eth_finality_update_fork(NULL_BYTES));
}

void test_gloas_finality_update_serializes_to_fixed_size(void) {
  const ssz_def_t* def = eth_get_light_client_finality_update(C4_FORK_GLOAS);
  TEST_ASSERT_NOT_NULL(def);
  TEST_ASSERT_NULL(eth_get_light_client_finality_update(C4_FORK_ELECTRA));

  uint8_t       header[496]    = {0};
  uint8_t       branch[288]    = {0};
  uint8_t       aggregate[160] = {0};
  ssz_builder_t builder        = ssz_builder_for_def(def);
  ssz_add_bytes(&builder, "attestedHeader", bytes(header, sizeof(header)));
  ssz_add_bytes(&builder, "finalizedHeader", bytes(header, sizeof(header)));
  ssz_add_bytes(&builder, "finalityBranch", bytes(branch, sizeof(branch)));
  ssz_add_bytes(&builder, "syncAggregate", bytes(aggregate, sizeof(aggregate)));
  ssz_add_uint64(&builder, 1);
  ssz_ob_t encoded = ssz_builder_to_bytes(&builder);
  TEST_ASSERT_EQUAL_UINT32(C4_GLOAS_LIGHT_CLIENT_FINALITY_UPDATE_SIZE, encoded.bytes.len);
  TEST_ASSERT_EQUAL_INT(C4_FORK_GLOAS, c4_eth_finality_update_fork(encoded.bytes));
  safe_free(encoded.bytes.data);
}

void test_supermajority_threshold_is_two_thirds(void) {
  uint8_t bits[64];
  fill_bits(bits, 341);
  TEST_ASSERT_FALSE(c4_sync_committee_supermajority(bytes(bits, 64)));
  fill_bits(bits, 342);
  TEST_ASSERT_TRUE(c4_sync_committee_supermajority(bytes(bits, 64)));
  TEST_ASSERT_FALSE(c4_sync_committee_supermajority(bytes(bits, 63)));
  TEST_ASSERT_FALSE(c4_sync_committee_supermajority(NULL_BYTES));
  bytes_t no_ptr = {.data = NULL, .len = 64};
  TEST_ASSERT_FALSE(c4_sync_committee_supermajority(no_ptr));
}

void test_checkpoint_merkle_is_checked_before_participation(void) {
  uint8_t leaf_raw[112]     = {0};
  uint8_t attested_raw[112] = {0};
  uint8_t bits[64]          = {0};
  leaf_raw[0]               = 1;
  ssz_ob_t leaf             = {.def = &HEADER_CONTAINER, .bytes = bytes(leaf_raw, sizeof(leaf_raw))};
  ssz_ob_t proof            = build_checkpoint_proof(bytes(bits, 64), bytes(attested_raw, sizeof(attested_raw)), NULL_BYTES);

  verify_ctx_t ctx   = {0};
  ctx.chain_id       = C4_CHAIN_MAINNET;
  c4_status_t status = c4_verify_signed_checkpoint_header(&ctx, leaf, proof, 1);
  TEST_ASSERT_EQUAL_INT(C4_ERROR, status);
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "invalid state root for checkpoint header proof"));
  c4_state_free(&ctx.state);
  safe_free(proof.bytes.data);
}

void test_checkpoint_header_requires_supermajority(void) {
  uint8_t leaf_raw[112]     = {0};
  uint8_t attested_raw[112] = {0};
  uint8_t bits[64]          = {0};
  leaf_raw[0]               = 1;
  ssz_ob_t  leaf            = {.def = &HEADER_CONTAINER, .bytes = bytes(leaf_raw, sizeof(leaf_raw))};
  bytes32_t root            = {0};
  ssz_hash_tree_root(leaf, root);
  // BeaconBlockHeader: slot 8, proposer 8, parentRoot 32, stateRoot 32, bodyRoot 32.
  memcpy(attested_raw + 48, root, 32);
  ssz_ob_t proof = build_checkpoint_proof(bytes(bits, 64), bytes(attested_raw, sizeof(attested_raw)), NULL_BYTES);

  verify_ctx_t ctx   = {0};
  ctx.chain_id       = C4_CHAIN_MAINNET;
  c4_status_t status = c4_verify_signed_checkpoint_header(&ctx, leaf, proof, 1);
  TEST_ASSERT_EQUAL_INT(C4_ERROR, status);
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "insufficient sync committee participation"));
  c4_state_free(&ctx.state);
  safe_free(proof.bytes.data);
}

void test_finalized_tag_uses_to_block_for_logs(void) {
  verify_ctx_t ctx         = {0};
  char         balance[]   = "[\"0x0000000000000000000000000000000000000000\",\"finalized\"]";
  char         safe[]      = "[\"0x0000000000000000000000000000000000000000\",\"safe\"]";
  char         logs_from[] = "[{\"fromBlock\":\"finalized\",\"toBlock\":\"latest\"}]";
  char         logs_to[]   = "[{\"fromBlock\":\"latest\",\"toBlock\":\"finalized\"}]";

  ctx.method = "eth_getBalance";
  ctx.args   = json_parse(balance);
  TEST_ASSERT_TRUE(eth_request_is_finalized(&ctx));
  ctx.args = json_parse(safe);
  TEST_ASSERT_FALSE(eth_request_is_finalized(&ctx));

  ctx.method = "eth_getLogs";
  ctx.args   = json_parse(logs_from);
  TEST_ASSERT_FALSE(eth_request_is_finalized(&ctx));
  ctx.args = json_parse(logs_to);
  TEST_ASSERT_TRUE(eth_request_is_finalized(&ctx));

  ctx.method = "eth_getBlockByHash";
  ctx.args   = json_parse(balance);
  TEST_ASSERT_FALSE(eth_request_is_finalized(&ctx));

  ctx.method = "eth_getBlockByNumber";
  ctx.args   = json_parse("[\"finalized\", false]");
  TEST_ASSERT_TRUE(eth_request_is_finalized(&ctx));
  ctx.args = json_parse("[\"latest\", false]");
  TEST_ASSERT_FALSE(eth_request_is_finalized(&ctx));

  ctx.method = "eth_getStorageAt";
  ctx.args   = json_parse("[\"0x0000000000000000000000000000000000000000\",\"0x0\",\"finalized\"]");
  TEST_ASSERT_TRUE(eth_request_is_finalized(&ctx));
  ctx.args = json_parse("[\"0x0000000000000000000000000000000000000000\",\"finalized\",\"latest\"]");
  TEST_ASSERT_FALSE(eth_request_is_finalized(&ctx));

  ctx.method = "eth_verifyLogs";
  ctx.args   = json_parse("[{\"fromBlock\":\"finalized\"}]");
  TEST_ASSERT_FALSE(eth_request_is_finalized(&ctx));
  ctx.args = json_parse("[{\"toBlock\":\"finalized\"}]");
  TEST_ASSERT_TRUE(eth_request_is_finalized(&ctx));

  ctx.method = "eth_getBalance";
  ctx.args   = json_parse("[\"0x0000000000000000000000000000000000000000\"]");
  TEST_ASSERT_FALSE(eth_request_is_finalized(&ctx));
  ctx.method = NULL;
  TEST_ASSERT_FALSE(eth_request_is_finalized(&ctx));
  TEST_ASSERT_FALSE(eth_request_is_finalized(NULL));
}

void test_finalized_requirement_is_outermost_only(void) {
  ssz_def_t    def    = {.name = ETH_SSZ_NAME_BLOCK_HASH, .type = SSZ_TYPE_VECTOR};
  ssz_ob_t     block  = {.def = &def};
  bytes_t      header = {0};
  bytes32_t    hash   = {0};
  verify_ctx_t ctx    = {0};
  char         args[] = "[\"0x0000000000000000000000000000000000000000\",\"finalized\"]";

  ctx.method   = "eth_getBalance";
  ctx.args     = json_parse(args);
  ctx.chain_id = C4_CHAIN_MAINNET;

  c4_status_t status = c4_verify_block(&ctx, block, &header, hash);
  TEST_ASSERT_EQUAL_INT(C4_ERROR, status);
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "signatureFinalized"));
  TEST_ASSERT_EQUAL_INT(0, ctx.flags & VERIFY_FLAG_INNER_BLOCK);
  c4_state_free(&ctx.state);
  ctx.state = (c4_state_t) {0};

  ctx.flags |= VERIFY_FLAG_INNER_BLOCK;
  status = c4_verify_block(&ctx, block, &header, hash);
  TEST_ASSERT_EQUAL_INT(C4_ERROR, status);
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  TEST_ASSERT_NULL(strstr(ctx.state.error, "signatureFinalized"));
  TEST_ASSERT_NOT_EQUAL(0, ctx.flags & VERIFY_FLAG_INNER_BLOCK);
  c4_state_free(&ctx.state);
}

void test_fulu_finality_update_classifies_from_its_preamble(void) {
  const ssz_def_t* def = eth_get_light_client_finality_update(C4_FORK_FULU);
  TEST_ASSERT_NOT_NULL(def);
  TEST_ASSERT_NULL(eth_get_light_client_finality_update(C4_FORK_DENEB));
  TEST_ASSERT_NULL(eth_get_light_client_finality_update(C4_FORK_PHASE0));
  TEST_ASSERT_NULL(eth_get_light_client_finality_update(C4_FORK_INVALID));

  const ssz_def_t* header = ssz_get_def(def, "attestedHeader");
  TEST_ASSERT_NOT_NULL(header);
  ssz_builder_t builder = ssz_builder_for_def(def);
  ssz_add_builders(&builder, "attestedHeader", fulu_light_client_header(header));
  ssz_add_builders(&builder, "finalizedHeader", fulu_light_client_header(header));
  ssz_add_bytes(&builder, "finalityBranch", NULL_BYTES);
  ssz_builder_t aggregate = {.def = ssz_get_def(def, "syncAggregate")};
  ssz_add_bytes(&aggregate, "syncCommitteeBits", NULL_BYTES);
  ssz_add_bytes(&aggregate, "syncCommitteeSignature", NULL_BYTES);
  ssz_add_builders(&builder, "syncAggregate", aggregate);
  ssz_add_uint64(&builder, 1);
  ssz_ob_t encoded = ssz_builder_to_bytes(&builder);

  c4_state_t state = {0};
  TEST_ASSERT_TRUE_MESSAGE(ssz_is_valid(encoded, true, &state), state.error ? state.error : "invalid fulu finality update");
  c4_state_free(&state);
  TEST_ASSERT_TRUE(encoded.bytes.len > C4_FULU_LIGHT_CLIENT_FINALITY_UPDATE_FIXED);
  TEST_ASSERT_NOT_EQUAL(C4_GLOAS_LIGHT_CLIENT_FINALITY_UPDATE_SIZE, encoded.bytes.len);
  TEST_ASSERT_EQUAL_UINT32(C4_FULU_LIGHT_CLIENT_FINALITY_UPDATE_FIXED, uint32_from_le(encoded.bytes.data));
  TEST_ASSERT_EQUAL_INT(C4_FORK_FULU, c4_eth_finality_update_fork(encoded.bytes));

  encoded.bytes.data[0] ^= 0xff;
  TEST_ASSERT_EQUAL_INT(C4_FORK_INVALID, c4_eth_finality_update_fork(encoded.bytes));
  safe_free(encoded.bytes.data);

  uint8_t* near = safe_calloc(1, C4_GLOAS_LIGHT_CLIENT_FINALITY_UPDATE_SIZE + 1);
  TEST_ASSERT_NOT_NULL(near);
  uint32_to_le(near, C4_FULU_LIGHT_CLIENT_FINALITY_UPDATE_FIXED);
  TEST_ASSERT_EQUAL_INT(C4_FORK_FULU, c4_eth_finality_update_fork(bytes(near, C4_GLOAS_LIGHT_CLIENT_FINALITY_UPDATE_SIZE - 1)));
  TEST_ASSERT_EQUAL_INT(C4_FORK_GLOAS, c4_eth_finality_update_fork(bytes(near, C4_GLOAS_LIGHT_CLIENT_FINALITY_UPDATE_SIZE)));
  TEST_ASSERT_EQUAL_INT(C4_FORK_FULU, c4_eth_finality_update_fork(bytes(near, C4_GLOAS_LIGHT_CLIENT_FINALITY_UPDATE_SIZE + 1)));
  bytes_t no_data = {.data = NULL, .len = C4_GLOAS_LIGHT_CLIENT_FINALITY_UPDATE_SIZE};
  TEST_ASSERT_EQUAL_INT(C4_FORK_INVALID, c4_eth_finality_update_fork(no_data));
  safe_free(near);
}

void test_merkle_is_checked_even_when_participation_passes(void) {
  uint8_t leaf_raw[112]     = {0};
  uint8_t attested_raw[112] = {0};
  uint8_t bits[64]          = {0};
  leaf_raw[0]               = 1;
  fill_bits(bits, 342);
  ssz_ob_t leaf  = {.def = &HEADER_CONTAINER, .bytes = bytes(leaf_raw, sizeof(leaf_raw))};
  ssz_ob_t proof = build_checkpoint_proof(bytes(bits, 64), bytes(attested_raw, sizeof(attested_raw)), NULL_BYTES);

  verify_ctx_t ctx   = {0};
  ctx.chain_id       = C4_CHAIN_MAINNET;
  c4_status_t status = c4_verify_signed_checkpoint_header(&ctx, leaf, proof, 1);
  TEST_ASSERT_EQUAL_INT(C4_ERROR, status);
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "invalid state root for checkpoint header proof"));
  TEST_ASSERT_NULL(strstr(ctx.state.error, "insufficient sync committee participation"));
  c4_state_free(&ctx.state);
  safe_free(proof.bytes.data);
}

void test_supermajority_reaches_the_signature_check(void) {
  uint64_t electra_slot      = mainnet_electra_slot();
  gindex_t gindex            = c4_finalized_root_gindex(C4_CHAIN_MAINNET, electra_slot);
  uint8_t  leaf_raw[112]     = {0};
  uint8_t  attested_raw[112] = {0};
  uint8_t  bits[64]          = {0};
  leaf_raw[80]               = 7;
  fill_bits(bits, 342);
  ssz_ob_t  leaf       = {.def = &HEADER_CONTAINER, .bytes = bytes(leaf_raw, sizeof(leaf_raw))};
  bytes32_t leaf_root  = {0};
  bytes32_t state_root = {0};
  ssz_hash_tree_root(leaf, leaf_root);
  bytes_t branch = finalized_branch(bytes(leaf_root, 32), gindex, state_root);
  uint64_to_le(attested_raw, UINT64_MAX);
  memcpy(attested_raw + 48, state_root, 32);
  ssz_ob_t proof = build_checkpoint_proof(bytes(bits, 64), bytes(attested_raw, sizeof(attested_raw)), branch);

  verify_ctx_t ctx   = {0};
  ctx.chain_id       = C4_CHAIN_MAINNET;
  c4_status_t status = c4_verify_signed_checkpoint_header(&ctx, leaf, proof, gindex);
  TEST_ASSERT_EQUAL_INT(C4_ERROR, status);
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "slot is missing in beacon header"));
  c4_state_free(&ctx.state);
  safe_free(branch.data);
  safe_free(proof.bytes.data);
}

void test_gindex_zero_is_rejected_before_the_branch(void) {
  uint8_t bits[64] = {0};
  fill_bits(bits, 342);
  uint8_t  raw[112] = {0};
  ssz_ob_t leaf     = {.def = &HEADER_CONTAINER, .bytes = bytes(raw, sizeof(raw))};
  ssz_ob_t proof    = build_checkpoint_proof(bytes(bits, 64), bytes(raw, sizeof(raw)), NULL_BYTES);

  verify_ctx_t ctx   = {0};
  ctx.chain_id       = C4_CHAIN_MAINNET;
  c4_status_t status = c4_verify_signed_checkpoint_header(&ctx, leaf, proof, 0);
  TEST_ASSERT_EQUAL_INT(C4_ERROR, status);
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "invalid gindex for checkpoint header proof"));
  c4_state_free(&ctx.state);
  safe_free(proof.bytes.data);
}

void test_verify_header_binds_finalized_gindex_to_attested_slot(void) {
  uint64_t electra_slot = mainnet_electra_slot();
  gindex_t electra_g    = c4_finalized_root_gindex(C4_CHAIN_MAINNET, electra_slot);
  gindex_t earlier_g    = c4_finalized_root_gindex(C4_CHAIN_MAINNET, electra_slot - 1);
  TEST_ASSERT_NOT_EQUAL(0, electra_g);
  TEST_ASSERT_NOT_EQUAL(electra_g, earlier_g);

  uint8_t leaf_raw[112] = {0};
  leaf_raw[80]          = 7;
  ssz_ob_t  leaf        = {.def = &HEADER_CONTAINER, .bytes = bytes(leaf_raw, sizeof(leaf_raw))};
  bytes32_t leaf_root   = {0};
  bytes32_t state_root  = {0};
  ssz_hash_tree_root(leaf, leaf_root);
  bytes_t branch = finalized_branch(bytes(leaf_root, 32), electra_g, state_root);

  uint8_t attested_raw[112] = {0};
  uint8_t bits[64]          = {0};
  uint64_to_le(attested_raw, electra_slot);
  memcpy(attested_raw + 48, state_root, 32);

  ssz_builder_t variant = {.def = &ETH_HEADER_PROOFS_UNION[4]};
  ssz_add_bytes(&variant, "sync_committee_bits", bytes(bits, 64));
  ssz_add_bytes(&variant, "sync_committee_signature", NULL_BYTES);
  ssz_add_bytes(&variant, "header", bytes(attested_raw, sizeof(attested_raw)));
  ssz_add_bytes(&variant, "proof", branch);
  ssz_ob_t     block = cl_proof_with_header_variant(variant);
  verify_ctx_t ctx   = {0};
  ctx.chain_id       = C4_CHAIN_MAINNET;
  c4_status_t status = c4_verify_header(&ctx, leaf, block);
  TEST_ASSERT_EQUAL_INT(C4_ERROR, status);
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "insufficient sync committee participation"));
  c4_state_free(&ctx.state);
  safe_free(block.bytes.data);

  memset(attested_raw, 0, sizeof(attested_raw));
  memcpy(attested_raw + 48, state_root, 32);
  variant = (ssz_builder_t) {.def = &ETH_HEADER_PROOFS_UNION[4]};
  ssz_add_bytes(&variant, "sync_committee_bits", bytes(bits, 64));
  ssz_add_bytes(&variant, "sync_committee_signature", NULL_BYTES);
  ssz_add_bytes(&variant, "header", bytes(attested_raw, sizeof(attested_raw)));
  ssz_add_bytes(&variant, "proof", branch);
  block     = cl_proof_with_header_variant(variant);
  ctx.state = (c4_state_t) {0};
  status    = c4_verify_header(&ctx, leaf, block);
  TEST_ASSERT_EQUAL_INT(C4_ERROR, status);
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "invalid state root for checkpoint header proof"));
  c4_state_free(&ctx.state);
  safe_free(block.bytes.data);
  safe_free(branch.data);
}

void test_signature_variant_does_not_satisfy_finalized(void) {
  ssz_ob_t     block  = cl_proof_with_header_variant(zero_variant(&ETH_HEADER_PROOFS_UNION[0]));
  verify_ctx_t ctx    = finalized_balance_ctx(C4_CHAIN_MAINNET);
  bytes_t      header = {0};
  bytes32_t    hash   = {0};
  c4_status_t  status = c4_verify_block(&ctx, block, &header, hash);
  TEST_ASSERT_EQUAL_INT(C4_ERROR, status);
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "requires a signatureFinalized proof"));
  TEST_ASSERT_EQUAL_INT(0, ctx.flags & VERIFY_FLAG_INNER_BLOCK);
  c4_state_free(&ctx.state);
  safe_free(block.bytes.data);
}

void test_signature_finalized_variant_passes_the_outer_gate(void) {
  ssz_ob_t     block  = cl_proof_with_header_variant(zero_variant(&ETH_HEADER_PROOFS_UNION[4]));
  verify_ctx_t ctx    = finalized_balance_ctx(C4_CHAIN_MAINNET);
  bytes_t      header = {0};
  bytes32_t    hash   = {0};
  c4_status_t  status = c4_verify_block(&ctx, block, &header, hash);
  TEST_ASSERT_EQUAL_INT(C4_ERROR, status);
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  TEST_ASSERT_NULL(strstr(ctx.state.error, "requires a signatureFinalized proof"));
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "cl proof"));
  TEST_ASSERT_EQUAL_INT(0, ctx.flags & VERIFY_FLAG_INNER_BLOCK);
  c4_state_free(&ctx.state);
  safe_free(block.bytes.data);
}

void test_finalized_tag_follows_the_consensus_layer(void) {
  ssz_def_t    def    = {.name = ETH_SSZ_NAME_BLOCK_HASH, .type = SSZ_TYPE_VECTOR};
  ssz_ob_t     block  = {.def = &def};
  bytes_t      header = {0};
  bytes32_t    hash   = {0};
  verify_ctx_t ctx    = finalized_balance_ctx(C4_CHAIN_ARB_ONE);

  c4_status_t status = c4_verify_block(&ctx, block, &header, hash);
  TEST_ASSERT_EQUAL_INT(C4_ERROR, status);
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "invalid block hash length"));
  TEST_ASSERT_NULL(strstr(ctx.state.error, "signatureFinalized"));
  c4_state_free(&ctx.state);

  ctx    = finalized_balance_ctx(C4_CHAIN_OP_MAINNET);
  status = c4_verify_block(&ctx, block, &header, hash);
  TEST_ASSERT_EQUAL_INT(C4_ERROR, status);
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "requires a signatureFinalized proof"));
  c4_state_free(&ctx.state);

  ctx.method   = "eth_getBalance";
  ctx.args     = json_parse("[\"0x0000000000000000000000000000000000000000\",\"safe\"]");
  ctx.chain_id = C4_CHAIN_MAINNET;
  ctx.state    = (c4_state_t) {0};
  status       = c4_verify_block(&ctx, block, &header, hash);
  TEST_ASSERT_EQUAL_INT(C4_ERROR, status);
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "invalid block hash length"));
  TEST_ASSERT_NULL(strstr(ctx.state.error, "signatureFinalized"));
  TEST_ASSERT_EQUAL_INT(0, ctx.flags & VERIFY_FLAG_INNER_BLOCK);
  c4_state_free(&ctx.state);
}

void test_header_proof_builder_emits_signature_finalized(void) {
  TEST_ASSERT_EQUAL_INT(4, HISTORIC_PROOF_FINALIZED);
  TEST_ASSERT_EQUAL_STRING(ETH_SSZ_NAME_SIGNATURE_FINALIZED, ETH_HEADER_PROOFS_UNION[HISTORIC_PROOF_FINALIZED].name);

  uint8_t bits[64];
  uint8_t signature[96];
  uint8_t header[112];
  uint8_t branch[32 * 7];
  fill_bits(bits, 342);
  memset(signature, 0xab, sizeof(signature));
  memset(header, 0x11, sizeof(header));
  memset(branch, 0x22, sizeof(branch));

  ssz_builder_t aggregate = ssz_builder_for_def(&SYNC_AGG_CONTAINER);
  ssz_add_bytes(&aggregate, "syncCommitteeBits", bytes(bits, sizeof(bits)));
  ssz_add_bytes(&aggregate, "syncCommitteeSignature", bytes(signature, sizeof(signature)));

  blockroot_proof_t block_proof = {0};
  block_proof.type              = HISTORIC_PROOF_FINALIZED;
  block_proof.sync_aggregate    = ssz_builder_to_bytes(&aggregate);
  block_proof.proof_header      = bytes(header, sizeof(header));
  block_proof.historic_proof    = bytes(branch, sizeof(branch));

  ssz_builder_t builder = ssz_builder_for_def(&CL_BLOCK_PROOF);
  ssz_add_bytes(&builder, "elHeader", NULL_BYTES);
  ssz_add_bytes(&builder, "clHeader", NULL_BYTES);
  ssz_add_bytes(&builder, "blockhashBranch", NULL_BYTES);
  ssz_add_uint64(&builder, 1);
  eth_block_t block_data = {0};
  ssz_add_header_proof(&builder, &block_data, block_proof);
  ssz_ob_t encoded = ssz_builder_to_bytes(&builder);

  ssz_ob_t variant = ssz_get(&encoded, "clHeaderProof");
  TEST_ASSERT_NOT_NULL(variant.def);
  TEST_ASSERT_EQUAL_STRING(ETH_SSZ_NAME_SIGNATURE_FINALIZED, variant.def->name);
  TEST_ASSERT_EQUAL_MEMORY(bits, ssz_get(&variant, "sync_committee_bits").bytes.data, sizeof(bits));
  TEST_ASSERT_EQUAL_MEMORY(signature, ssz_get(&variant, "sync_committee_signature").bytes.data, sizeof(signature));
  TEST_ASSERT_EQUAL_MEMORY(header, ssz_get(&variant, "header").bytes.data, sizeof(header));
  TEST_ASSERT_EQUAL_UINT32(sizeof(branch), ssz_get(&variant, "proof").bytes.len);
  TEST_ASSERT_EQUAL_MEMORY(branch, ssz_get(&variant, "proof").bytes.data, sizeof(branch));
  safe_free(encoded.bytes.data);
  safe_free(block_proof.sync_aggregate.bytes.data);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_signature_finalized_is_union_selector_4);
  RUN_TEST(test_version_gate_keeps_legacy_clients_on_signature_proof);
  RUN_TEST(test_finality_update_fork_from_body);
  RUN_TEST(test_gloas_finality_update_serializes_to_fixed_size);
  RUN_TEST(test_supermajority_threshold_is_two_thirds);
  RUN_TEST(test_checkpoint_merkle_is_checked_before_participation);
  RUN_TEST(test_checkpoint_header_requires_supermajority);
  RUN_TEST(test_finalized_tag_uses_to_block_for_logs);
  RUN_TEST(test_finalized_requirement_is_outermost_only);
  RUN_TEST(test_fulu_finality_update_classifies_from_its_preamble);
  RUN_TEST(test_merkle_is_checked_even_when_participation_passes);
  RUN_TEST(test_supermajority_reaches_the_signature_check);
  RUN_TEST(test_gindex_zero_is_rejected_before_the_branch);
  RUN_TEST(test_verify_header_binds_finalized_gindex_to_attested_slot);
  RUN_TEST(test_signature_variant_does_not_satisfy_finalized);
  RUN_TEST(test_signature_finalized_variant_passes_the_outer_gate);
  RUN_TEST(test_finalized_tag_follows_the_consensus_layer);
  RUN_TEST(test_header_proof_builder_emits_signature_finalized);
  return UNITY_END();
}
