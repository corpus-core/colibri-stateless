/*
 * Copyright (c) 2025,2026 corpus.core GmbH
 * SPDX-License-Identifier: MIT
 *
 * Nitro signed-tail bytes and the arbFeedProof container built from an L2 header.
 */

#include "unity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef NO_CHAIN_ARB

void setUp(void) {}
void tearDown(void) {}

int main(void) {
  UNITY_BEGIN();
  return UNITY_END();
}

#else

#include "arb_prover.h"
#include "arb_verify.h"
#include "beacon_types.h"
#include "bytes.h"
#include "chains/arb/server/arb_preconf.h"
#include "chains/arb/server/feed_client.h"
#include "crypto.h"
#include "eip2935.h"
#include "el_header.h"
#include "eth_account.h"
#include "eth_verify.h"
#include "header_cache.h"
#include "json.h"
#include "rlp.h"
#include "ssz.h"

static const char* NITRO_MESSAGE =
    "{"
    "\"sequenceNumber\":12345,"
    "\"message\":{\"message\":{\"header\":{"
    "\"kind\":0,\"sender\":\"0x0000000000000000000000000000000000000000\","
    "\"blockNumber\":0,\"timestamp\":0,"
    "\"requestId\":\"0x0000000000000000000000000000000000000000000000000000000000000000\","
    "\"baseFeeL1\":\"0\"},\"l2Msg\":\"3q2+7w==\"},\"delayedMessagesRead\":3333},"
    "\"blockHash\":\"0x1111111111111111111111111111111111111111111111111111111111111111\","
    "\"signatureV2\":\"q6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6s=\","
    "\"blockMetadata\":\"3q2+rw==\""
    "}";

/* Same tail shape with a numeric baseFeeL1 and no requestId. Sequence is newer, so it becomes the head. */
static const char* FEE_MESSAGE =
    "{"
    "\"sequenceNumber\":12346,"
    "\"message\":{\"message\":{\"header\":{"
    "\"kind\":0,\"sender\":\"0x0000000000000000000000000000000000000000\","
    "\"blockNumber\":0,\"timestamp\":0,\"baseFeeL1\":256},\"l2Msg\":\"3q2+7w==\"},"
    "\"delayedMessagesRead\":3333},"
    "\"blockHash\":\"0x2222222222222222222222222222222222222222222222222222222222222222\","
    "\"signatureV2\":\"q6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6s=\","
    "\"blockMetadata\":\"3q2+rw==\""
    "}";

static const char* NITRO_TAIL = "deadbeaf0000000000000d05000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000deadbeef";
static const char* FEE_TAIL   = "deadbeaf0000000000000d05000000000000000000000000000000000000000000000000000000000000000000000000000100deadbeef";

void setUp(void) {}
void tearDown(void) {}

static bytes_t unhex(const char* hex) {
  size_t   n   = strlen(hex);
  uint8_t* raw = malloc(n / 2);
  int      got = hex_to_bytes(hex, (int) n, bytes(raw, (uint32_t) (n / 2)));
  TEST_ASSERT_EQUAL_INT(n / 2, got);
  return bytes(raw, (uint32_t) got);
}

static void copy_head(uint8_t hash[32], uint8_t sig[65], uint8_t** tail, uint32_t* tail_len, uint64_t* seq) {
  TEST_ASSERT_TRUE(arb_feed_copy_head(hash, sig, tail, tail_len, seq));
}

void test_nitro_signed_tail(void) {
  json_t message = json_parse((char*) NITRO_MESSAGE);
  TEST_ASSERT_EQUAL_INT(JSON_TYPE_OBJECT, message.type);
  TEST_ASSERT_TRUE(arb_feed_store_message(message));

  uint8_t  hash[32];
  uint8_t  sig[65];
  uint8_t* tail     = NULL;
  uint32_t tail_len = 0;
  uint64_t seq      = 0;
  copy_head(hash, sig, &tail, &tail_len, &seq);
  bytes_t expect = unhex(NITRO_TAIL);
  TEST_ASSERT_EQUAL_UINT64(12345, seq);
  TEST_ASSERT_EQUAL_UINT32(expect.len, tail_len);
  TEST_ASSERT_EQUAL_MEMORY(expect.data, tail, expect.len);
  TEST_ASSERT_EQUAL_UINT8(0x11, hash[0]);
  TEST_ASSERT_EQUAL_UINT8(0xab, sig[0]);
  free(expect.data);
  free(tail);

  const char* older =
      "{\"sequenceNumber\":100,\"blockHash\":\"0x3333333333333333333333333333333333333333333333333333333333333333\","
      "\"signatureV2\":\"q6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6s=\","
      "\"message\":{\"message\":{\"header\":{\"kind\":0,\"sender\":\"0x0000000000000000000000000000000000000000\","
      "\"blockNumber\":0,\"timestamp\":0},\"l2Msg\":\"3q2+7w==\"},\"delayedMessagesRead\":1}}";
  TEST_ASSERT_TRUE(arb_feed_store_message(json_parse((char*) older)));
  copy_head(hash, sig, &tail, &tail_len, &seq);
  TEST_ASSERT_EQUAL_UINT64(100, seq);
  free(tail);

  const char* unsigned_msg =
      "{\"sequenceNumber\":99999,\"blockHash\":\"0x4444444444444444444444444444444444444444444444444444444444444444\","
      "\"message\":{\"message\":{\"header\":{\"kind\":0,\"sender\":\"0x0000000000000000000000000000000000000000\","
      "\"blockNumber\":0,\"timestamp\":0},\"l2Msg\":\"3q2+7w==\"},\"delayedMessagesRead\":1}}";
  TEST_ASSERT_FALSE(arb_feed_store_message(json_parse((char*) unsigned_msg)));

  TEST_ASSERT_TRUE(arb_feed_store_message(json_parse((char*) FEE_MESSAGE)));
  copy_head(hash, sig, &tail, &tail_len, &seq);
  expect = unhex(FEE_TAIL);
  TEST_ASSERT_EQUAL_UINT64(12346, seq);
  TEST_ASSERT_EQUAL_UINT32(expect.len, tail_len);
  TEST_ASSERT_EQUAL_MEMORY(expect.data, tail, expect.len);
  free(expect.data);
  free(tail);

  const char* pinned =
      "{\"sequenceNumber\":-1,\"blockHash\":\"0x5555555555555555555555555555555555555555555555555555555555555555\","
      "\"signatureV2\":\"q6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6urq6s=\","
      "\"message\":{\"message\":{\"header\":{\"kind\":0,\"sender\":\"0x0000000000000000000000000000000000000000\","
      "\"blockNumber\":0,\"timestamp\":0},\"l2Msg\":\"3q2+7w==\"},\"delayedMessagesRead\":1}}";
  TEST_ASSERT_FALSE(arb_feed_store_message(json_parse((char*) pinned)));
  copy_head(hash, sig, &tail, &tail_len, &seq);
  TEST_ASSERT_EQUAL_UINT64(12346, seq);
  free(tail);
}

static char* read_fixture(void) {
  FILE* f = fopen(ARB_BLOCK_FIXTURE, "rb");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, ARB_BLOCK_FIXTURE);
  TEST_ASSERT_EQUAL_INT(0, fseek(f, 0, SEEK_END));
  long n = ftell(f);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_INT(0, fseek(f, 0, SEEK_SET));
  char* buf = malloc((size_t) n + 1);
  TEST_ASSERT_EQUAL_INT(n, (int) fread(buf, 1, (size_t) n, f));
  buf[n] = 0;
  fclose(f);
  return buf;
}

static bytes_t proof_field(bytes_t proof, const char* name) {
  ssz_ob_t ob    = {.def = eth_ssz_verification_type(ETH_SSZ_ARB_FEED_PROOF), .bytes = proof};
  ssz_ob_t field = ssz_get(&ob, name);
  return field.bytes;
}

void test_arb_feed_proof_from_block(void) {
  char*   text      = read_fixture();
  json_t  rpc       = json_parse(text);
  json_t  hash_json = json_get(json_get(rpc, "result"), "hash");
  uint8_t hash[32];
  uint8_t sig[65];
  memset(sig, 0xab, sizeof(sig));
  TEST_ASSERT_TRUE(json_as_bytes32(hash_json, hash));
  uint8_t tail[45];
  memset(tail, 0x5a, sizeof(tail));

  char*   err   = NULL;
  bytes_t proof = arb_preconf_encode(rpc, bytes(hash, 32), bytes(sig, 65), bytes(tail, sizeof(tail)), &err);
  TEST_ASSERT_NULL_MESSAGE(err, err ? err : "");
  TEST_ASSERT_NOT_NULL(proof.data);

  bytes_t header = proof_field(proof, "elHeader");
  uint8_t got[32];
  keccak(header, got);
  TEST_ASSERT_EQUAL_MEMORY(hash, got, 32);

  bytes_t signature = proof_field(proof, "signature");
  TEST_ASSERT_EQUAL_UINT32(65, signature.len);
  TEST_ASSERT_EQUAL_UINT8(0xab, signature.data[0]);
  bytes_t signed_tail = proof_field(proof, "signedTail");
  TEST_ASSERT_EQUAL_UINT32(sizeof(tail), signed_tail.len);
  TEST_ASSERT_EQUAL_MEMORY(tail, signed_tail.data, sizeof(tail));

  buffer_t raw_json = {0};
  buffer_add_chars(&raw_json, "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"0x");
  for (uint32_t i = 0; i < header.len; i++) {
    char hex[3];
    snprintf(hex, sizeof(hex), "%02x", header.data[i]);
    buffer_add_chars(&raw_json, hex);
  }
  buffer_add_chars(&raw_json, "\"}");
  json_t  raw     = json_parse((char*) raw_json.data.data);
  char*   raw_err = NULL;
  bytes_t again   = arb_preconf_encode(raw, bytes(hash, 32), bytes(sig, 65), bytes(tail, sizeof(tail)), &raw_err);
  TEST_ASSERT_NULL_MESSAGE(raw_err, raw_err ? raw_err : "");
  TEST_ASSERT_TRUE(bytes_eq(proof, again));

  hash[0] ^= 0xff;
  char*   bad_err = NULL;
  bytes_t bad     = arb_preconf_encode(rpc, bytes(hash, 32), bytes(sig, 65), bytes(tail, sizeof(tail)), &bad_err);
  TEST_ASSERT_NULL(bad.data);
  TEST_ASSERT_NOT_NULL(bad_err);

  safe_free(bad_err);
  safe_free(again.data);
  safe_free(proof.data);
  buffer_free(&raw_json);
  free(text);
}

void test_arb_feed_proof_is_union_variant(void) {
  char*   text      = read_fixture();
  json_t  rpc       = json_parse(text);
  json_t  hash_json = json_get(json_get(rpc, "result"), "hash");
  uint8_t hash[32];
  uint8_t sig[65];
  memset(sig, 0xab, sizeof(sig));
  TEST_ASSERT_TRUE(json_as_bytes32(hash_json, hash));
  uint8_t tail[45];
  memset(tail, 0x5a, sizeof(tail));

  char*   err   = NULL;
  bytes_t proof = arb_preconf_encode(rpc, bytes(hash, 32), bytes(sig, 65), bytes(tail, sizeof(tail)), &err);
  TEST_ASSERT_NULL_MESSAGE(err, err ? err : "");

  eth_block_t block         = {0};
  block.proof_type          = C4_BLOCK_PROOF_TYPE_SEQUENCER;
  block.sequencer.payload   = proof;
  ssz_builder_t block_proof = ssz_builder_for_type(ETH_SSZ_VERIFY_BLOCK_PROOF);
  uint8_t       none_body   = 0;
  ssz_add_bytes(&block_proof, "body", bytes(&none_body, 1));
  TEST_ASSERT_TRUE(arb_add_feed_proof(NULL, &block_proof, &block, NULL));

  ssz_ob_t wrapped = ssz_builder_to_bytes(&block_proof);
  ssz_ob_t el      = ssz_get(&wrapped, "elProof");
  TEST_ASSERT_EQUAL_UINT32(proof.len, el.bytes.len);
  TEST_ASSERT_EQUAL_MEMORY(proof.data, el.bytes.data, proof.len);
  TEST_ASSERT_EQUAL_PTR(eth_ssz_verification_type(ETH_SSZ_ARB_FEED_PROOF), el.def);

  block.proof_type = C4_BLOCK_PROOF_TYPE_NONE;
  TEST_ASSERT_FALSE(arb_add_feed_proof(NULL, &block_proof, &block, NULL));

  safe_free(wrapped.bytes.data);
  safe_free(proof.data);
  free(text);
}

static const uint8_t ARB_ONE_POSTER[20] = {
    0xC1, 0xb6, 0x34, 0x85, 0x3C, 0xb3, 0x33, 0xD3, 0xaD, 0x86,
    0x63, 0x71, 0x5b, 0x08, 0xf4, 0x1A, 0x3A, 0xec, 0x47, 0xcc};

static bytes_t feed_proof_from(bytes_t header, bytes_t tail, bytes_t signature) {
  ssz_builder_t proof = ssz_builder_for_type(ETH_SSZ_ARB_FEED_PROOF);
  TEST_ASSERT_NOT_NULL(proof.def);
  ssz_add_bytes(&proof, "elHeader", header);
  const ssz_def_t* tail_union = ssz_get_def(proof.def, "signedTail");
  TEST_ASSERT_NOT_NULL(tail_union);
  ssz_builder_t tail_builder = ssz_builder_for_def(tail_union->def.container.elements + 1);
  buffer_append(&tail_builder.fixed, tail);
  ssz_add_builders(&proof, "signedTail", tail_builder);
  ssz_add_bytes(&proof, "signature", signature);
  return ssz_builder_to_bytes(&proof).bytes;
}

static void signer_of(bytes32_t digest, bytes_t signature, uint8_t address[20]) {
  uint8_t pub[64]  = {0};
  uint8_t hash[32] = {0};
  TEST_ASSERT_TRUE(secp256k1_recover(digest, signature, pub));
  keccak(bytes(pub, 64), hash);
  memcpy(address, hash + 12, 20);
}

void test_arb_feed_digest_matches_nitro_layout(void) {
  uint8_t hash[32];
  memset(hash, 0x11, sizeof(hash));
  bytes_t   tail = unhex(NITRO_TAIL);
  bytes32_t digest;
  arb_feed_digest(0xa4b1, 12345, hash, tail, digest);

  uint8_t     expected[32];
  const char* hex = "34bcd95539f8a978476e7340cc65a95c86a2e922b992019e81a1d5fa99a45d57";
  TEST_ASSERT_EQUAL_INT(32, hex_to_bytes(hex, 64, bytes(expected, 32)));
  TEST_ASSERT_EQUAL_MEMORY(expected, digest, 32);
  free(tail.data);
}

void test_arb_feed_rejects_other_signer(void) {
  char*   text   = read_fixture();
  json_t  rpc    = json_parse(text);
  json_t  result = json_get(rpc, "result");
  uint8_t hash[32];
  TEST_ASSERT_TRUE(json_as_bytes32(json_get(result, "hash"), hash));
  uint64_t number = json_as_uint64(json_get(result, "number"));
  TEST_ASSERT_TRUE(number > 22207817ull);

  uint8_t tail[45];
  memset(tail, 0x5a, sizeof(tail));
  uint8_t sk[32] = {0};
  sk[31]         = 1;
  bytes32_t digest;
  arb_feed_digest(42161, number - 22207817ull, hash, bytes(tail, sizeof(tail)), digest);
  uint8_t sig[65];
  TEST_ASSERT_TRUE(secp256k1_sign(sk, digest, sig));
  uint8_t address[20];
  signer_of(digest, bytes(sig, 65), address);
  TEST_ASSERT_NOT_EQUAL(0, memcmp(address, ARB_ONE_POSTER, 20));

  char*   err   = NULL;
  bytes_t proof = arb_preconf_encode(rpc, bytes(hash, 32), bytes(sig, 65), bytes(tail, sizeof(tail)), &err);
  TEST_ASSERT_NULL_MESSAGE(err, err ? err : "");
  ssz_ob_t ob = {.def = eth_ssz_verification_type(ETH_SSZ_ARB_FEED_PROOF), .bytes = proof};

  verify_ctx_t ctx = {0};
  ctx.chain_id     = C4_CHAIN_ARB_ONE;
  bytes_t   el     = NULL_BYTES;
  bytes32_t block_hash;
  TEST_ASSERT_EQUAL_INT(C4_ERROR, arb_verify_feed_proof(&ctx, ob, &el, block_hash));
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "invalid sequencer signature"));
  c4_state_free(&ctx.state);

  sig[64] -= 27;
  signer_of(digest, bytes(sig, 65), address);
  bytes_t v0 = feed_proof_from(proof_field(proof, "elHeader"), bytes(tail, sizeof(tail)), bytes(sig, 65));
  memset(&ctx, 0, sizeof(ctx));
  ctx.chain_id = C4_CHAIN_ARB_ONE;
  ob.bytes     = v0;
  TEST_ASSERT_EQUAL_INT(C4_ERROR, arb_verify_feed_proof(&ctx, ob, &el, block_hash));
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "invalid sequencer signature"));
  c4_state_free(&ctx.state);

  memset(&ctx, 0, sizeof(ctx));
  ctx.chain_id = 1;
  ob.bytes     = proof;
  TEST_ASSERT_EQUAL_INT(C4_ERROR, arb_verify_feed_proof(&ctx, ob, &el, block_hash));
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "chain not supported"));
  c4_state_free(&ctx.state);

  arb_register_block_proof_verify();
  memset(&ctx, 0, sizeof(ctx));
  ctx.chain_id = C4_CHAIN_ARB_ONE;
  ob.bytes     = proof;
  TEST_ASSERT_EQUAL_INT(C4_ERROR, c4_verify_block(&ctx, ob, &el, block_hash));
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "invalid sequencer signature"));
  c4_state_free(&ctx.state);

  safe_free(v0.data);
  safe_free(proof.data);
  free(text);
}

void test_arb_feed_rejects_short_tail_and_prenitro(void) {
  char*   text = read_fixture();
  json_t  rpc  = json_parse(text);
  uint8_t hash[32];
  TEST_ASSERT_TRUE(json_as_bytes32(json_get(json_get(rpc, "result"), "hash"), hash));
  uint8_t tail[45];
  uint8_t sig[65];
  memset(tail, 0x5a, sizeof(tail));
  memset(sig, 0xab, sizeof(sig));
  char*   err   = NULL;
  bytes_t proof = arb_preconf_encode(rpc, bytes(hash, 32), bytes(sig, 65), bytes(tail, sizeof(tail)), &err);
  TEST_ASSERT_NULL_MESSAGE(err, err ? err : "");

  bytes_t      header      = proof_field(proof, "elHeader");
  bytes_t      short_proof = feed_proof_from(header, bytes(tail, 44), bytes(sig, 65));
  verify_ctx_t ctx         = {0};
  ctx.chain_id             = C4_CHAIN_ARB_ONE;
  ssz_ob_t  ob             = {.def = eth_ssz_verification_type(ETH_SSZ_ARB_FEED_PROOF), .bytes = short_proof};
  bytes_t   el             = NULL_BYTES;
  bytes32_t block_hash;
  TEST_ASSERT_EQUAL_INT(C4_ERROR, arb_verify_feed_proof(&ctx, ob, &el, block_hash));
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "signed tail too short"));
  c4_state_free(&ctx.state);
  safe_free(short_proof.data);
  safe_free(proof.data);

  char* number = strstr(text, "\"number\":\"0x1f4b4bb3\"");
  TEST_ASSERT_NOT_NULL(number);
  const char* replacement = "\"number\":\"0x1\"";
  size_t      old_len     = strlen("\"number\":\"0x1f4b4bb3\"");
  size_t      new_len     = strlen(replacement);
  memcpy(number, replacement, new_len);
  memmove(number + new_len, number + old_len, strlen(number + old_len) + 1);
  rpc            = json_parse(text);
  json_t  result = json_get(rpc, "result");
  bytes_t built  = NULL_BYTES;
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, eth_el_header_build_from_json(NULL, &built, C4_FORK_BELLATRIX, result));
  keccak(built, hash);
  safe_free(built.data);
  err   = NULL;
  proof = arb_preconf_encode(rpc, bytes(hash, 32), bytes(sig, 65), bytes(tail, sizeof(tail)), &err);
  TEST_ASSERT_NULL_MESSAGE(err, err ? err : "");
  ob.bytes = proof;
  memset(&ctx, 0, sizeof(ctx));
  ctx.chain_id = C4_CHAIN_ARB_ONE;
  TEST_ASSERT_EQUAL_INT(C4_ERROR, arb_verify_feed_proof(&ctx, ob, &el, block_hash));
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "block is before the nitro genesis"));
  c4_state_free(&ctx.state);
  safe_free(proof.data);
  free(text);
}

void test_arb_feed_nests_eip2935(void) {
  TEST_ASSERT_EQUAL_UINT64(ETH_EIP2935_HISTORY_WINDOW, eth_eip2935_history_window(1));
  TEST_ASSERT_EQUAL_UINT64(ARB_EIP2935_HISTORY_WINDOW, eth_eip2935_history_window(C4_CHAIN_ARB_ONE));
  bytes32_t eth_slot = {0};
  bytes32_t arb_slot = {0};
  eth_eip2935_slot(8192, eth_slot);
  eth_eip2935_slot_for(8192, ARB_EIP2935_HISTORY_WINDOW, arb_slot);
  TEST_ASSERT_TRUE(memcmp(eth_slot, arb_slot, 32) != 0);

  uint8_t header[8];
  memset(header, 0x11, sizeof(header));
  uint8_t tail[45];
  memset(tail, 0x5a, sizeof(tail));
  uint8_t sig[65];
  memset(sig, 0xab, sizeof(sig));
  bytes_t proof = feed_proof_from(bytes(header, sizeof(header)), bytes(tail, sizeof(tail)), bytes(sig, sizeof(sig)));

  uint8_t     older[4] = {0xc0, 0x01, 0x02, 0x03};
  const char* raw      = "{\"accountProof\":[],\"storageProof\":[{\"key\":\"0x01\",\"proof\":[]}]}";
  json_t      history  = json_parse(raw);
  TEST_ASSERT_EQUAL_INT(JSON_TYPE_OBJECT, history.type);

  eth_block_t block                     = {0};
  block.proof_type                      = C4_BLOCK_PROOF_TYPE_SEQUENCER;
  block.sequencer.payload               = proof;
  block.el_header                       = bytes(older, sizeof(older));
  block.sequencer.history.newer_header  = bytes(header, sizeof(header));
  block.sequencer.history.history_proof = history;

  ssz_builder_t block_proof = ssz_builder_for_type(ETH_SSZ_VERIFY_BLOCK_PROOF);
  uint8_t       none_body   = 0;
  ssz_add_bytes(&block_proof, "body", bytes(&none_body, 1));
  TEST_ASSERT_TRUE(arb_add_feed_proof(NULL, &block_proof, &block, NULL));

  ssz_ob_t wrapped = ssz_builder_to_bytes(&block_proof);
  ssz_ob_t el      = ssz_get(&wrapped, "elProof");
  TEST_ASSERT_NOT_NULL(el.def);
  TEST_ASSERT_EQUAL_STRING(ETH_SSZ_NAME_EIP2935, el.def->name);
  ssz_ob_t older_header = ssz_get(&el, "elHeader");
  TEST_ASSERT_EQUAL_UINT32(sizeof(older), older_header.bytes.len);
  TEST_ASSERT_EQUAL_MEMORY(older, older_header.bytes.data, sizeof(older));
  ssz_ob_t account = ssz_get(&el, "account");
  ssz_ob_t inner   = ssz_get(&account, "elProof");
  TEST_ASSERT_EQUAL_PTR(eth_ssz_verification_type(ETH_SSZ_ARB_FEED_PROOF), inner.def);
  TEST_ASSERT_EQUAL_UINT32(proof.len, inner.bytes.len);
  TEST_ASSERT_EQUAL_MEMORY(proof.data, inner.bytes.data, proof.len);

  safe_free(wrapped.bytes.data);
  safe_free(proof.data);
}

/** `block_number % window` as a 32-byte big-endian word, including a cleared prefix. */
static void assert_history_slot(uint64_t number, uint64_t window, uint64_t index) {
  bytes32_t slot   = {0};
  bytes32_t expect = {0};
  memset(slot, 0xee, sizeof(slot));
  eth_eip2935_slot_for(number, window, slot);
  uint64_to_be(expect + 24, index);
  TEST_ASSERT_EQUAL_MEMORY(expect, slot, 32);
}

void test_arb_eip2935_slot_modulus(void) {
  // The window follows the chain type. OP and EEZ stay on the Ethereum ring;
  // a chain id whose high byte is Arbitrum uses the ArbOS ring even when it is not One.
  TEST_ASSERT_EQUAL_UINT64(ETH_EIP2935_HISTORY_WINDOW, eth_eip2935_history_window(C4_CHAIN_OP_MAINNET));
  TEST_ASSERT_EQUAL_UINT64(ETH_EIP2935_HISTORY_WINDOW, eth_eip2935_history_window(C4_CHAIN_EEZ_ROLLUPX));
  TEST_ASSERT_EQUAL_UINT64(ARB_EIP2935_HISTORY_WINDOW, eth_eip2935_history_window(CHAIN_ID(C4_CHAIN_TYPE_ARBITRUM, 42170)));

  assert_history_slot(0, ARB_EIP2935_HISTORY_WINDOW, 0);
  assert_history_slot(ARB_EIP2935_HISTORY_WINDOW - 1, ARB_EIP2935_HISTORY_WINDOW, ARB_EIP2935_HISTORY_WINDOW - 1);
  assert_history_slot(ARB_EIP2935_HISTORY_WINDOW, ARB_EIP2935_HISTORY_WINDOW, 0);
  assert_history_slot(ARB_EIP2935_HISTORY_WINDOW + 1, ARB_EIP2935_HISTORY_WINDOW, 1);

  // 393168 is a multiple of 8191, so the wrap itself looks like Ethereum.
  // The last Arbitrum slot (393167) is not 8190, which is what `% 8191` would store.
  bytes32_t eth_slot = {0};
  bytes32_t arb_slot = {0};
  eth_eip2935_slot(ARB_EIP2935_HISTORY_WINDOW - 1, eth_slot);
  eth_eip2935_slot_for(ARB_EIP2935_HISTORY_WINDOW - 1, ARB_EIP2935_HISTORY_WINDOW, arb_slot);
  TEST_ASSERT_TRUE(memcmp(eth_slot, arb_slot, 32) != 0);

  // A zero window is the Ethereum ring, so the modulus stays defined.
  bytes32_t fallback = {0};
  bytes32_t eth      = {0};
  eth_eip2935_slot_for(8192, 0, fallback);
  eth_eip2935_slot(8192, eth);
  TEST_ASSERT_EQUAL_MEMORY(eth, fallback, 32);
  eth_eip2935_slot_for(8191, 0, fallback);
  TEST_ASSERT_TRUE(bytes_all_zero(bytes(fallback, 32)));
}

static void assert_el_block_rejected(const char* tag, bool with_body, const char* expected) {
  prover_ctx_t ctx    = {0};
  json_t       block  = tag ? json_parse((char*) tag) : (json_t) {0};
  eth_block_t  out    = {0};
  c4_status_t  status = arb_get_el_block(&ctx, block, &out, with_body);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_ERROR, status, expected);
  TEST_ASSERT_NOT_NULL_MESSAGE(ctx.state.error, expected);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(ctx.state.error, expected), ctx.state.error);
  TEST_ASSERT_NULL_MESSAGE(ctx.state.requests, expected);
  TEST_ASSERT_NULL(out.el_header.data);
  c4_state_free(&ctx.state);
}

void test_arb_get_el_block_with_body_requests_the_head(void) {
  prover_ctx_t ctx    = {0};
  json_t       block  = json_parse("\"latest\"");
  eth_block_t  out    = {0};
  c4_status_t  status = arb_get_el_block(&ctx, block, &out, true);
  TEST_ASSERT_EQUAL_INT(C4_PENDING, status);
  TEST_ASSERT_NOT_NULL(ctx.state.requests);
  TEST_ASSERT_NULL(ctx.state.error);
  c4_state_free(&ctx.state);
}

void test_arb_get_el_block_rejects_before_fetch(void) {
  assert_el_block_rejected("\"pending\"", true, "only latest, a block number or a block hash is supported");
  assert_el_block_rejected("\"pending\"", false, "only latest, a block number or a block hash is supported");
  assert_el_block_rejected("\"earliest\"", false, "only latest, a block number or a block hash is supported");
  assert_el_block_rejected("\"safe\"", false, "only latest, a block number or a block hash is supported");
  assert_el_block_rejected("\"finalized\"", false, "only latest, a block number or a block hash is supported");
  assert_el_block_rejected("\"LATEST\"", false, "only latest, a block number or a block hash is supported");
  assert_el_block_rejected("\"\"", false, "only latest, a block number or a block hash is supported");
  assert_el_block_rejected("123", false, "only latest, a block number or a block hash is supported");
  assert_el_block_rejected("null", false, "only latest, a block number or a block hash is supported");
  assert_el_block_rejected(NULL, false, "only latest, a block number or a block hash is supported");
  assert_el_block_rejected("\"0x\"", false, "only latest, a block number or a block hash is supported");
  assert_el_block_rejected("\"0xzz\"", false, "only latest, a block number or a block hash is supported");
  assert_el_block_rejected("\"0x1234567890abcdef0\"", false, "only latest, a block number or a block hash is supported");
}

void test_arb_feed_rejects_history_without_proof(void) {
  uint8_t header[8];
  memset(header, 0x11, sizeof(header));
  uint8_t tail[45];
  memset(tail, 0x5a, sizeof(tail));
  uint8_t sig[65];
  memset(sig, 0xab, sizeof(sig));
  bytes_t proof = feed_proof_from(bytes(header, sizeof(header)), bytes(tail, sizeof(tail)), bytes(sig, sizeof(sig)));

  eth_block_t block                    = {0};
  block.proof_type                     = C4_BLOCK_PROOF_TYPE_SEQUENCER;
  block.sequencer.payload              = proof;
  block.sequencer.history.newer_header = bytes(header, sizeof(header));

  ssz_builder_t block_proof = ssz_builder_for_type(ETH_SSZ_VERIFY_BLOCK_PROOF);
  uint8_t       none_body   = 0;
  ssz_add_bytes(&block_proof, "body", bytes(&none_body, 1));
  TEST_ASSERT_FALSE(arb_add_feed_proof(NULL, &block_proof, &block, NULL));

  block.sequencer.history.history_proof = json_parse("[]");
  TEST_ASSERT_EQUAL_INT(JSON_TYPE_ARRAY, block.sequencer.history.history_proof.type);
  TEST_ASSERT_FALSE(arb_add_feed_proof(NULL, &block_proof, &block, NULL));

  block.sequencer.payload              = NULL_BYTES;
  block.sequencer.history.newer_header = NULL_BYTES;
  TEST_ASSERT_FALSE(arb_add_feed_proof(NULL, &block_proof, &block, NULL));

  ssz_builder_free(&block_proof);
  safe_free(proof.data);
}

#define ARB_FIXTURE_NUMBER 1000000ull

static bool contains(bytes_t haystack, const char* needle) {
  size_t n = needle ? strlen(needle) : 0;
  if (!needle || !haystack.data || haystack.len < n) return false;
  for (uint32_t i = 0; i + n <= haystack.len; i++)
    if (memcmp(haystack.data + i, needle, n) == 0) return true;
  return false;
}

static json_t quantity_tag(uint64_t n, char* storage, size_t cap) {
  snprintf(storage, cap, "\"0x%llx\"", (unsigned long long) n);
  return json_parse(storage);
}

/** Bellatrix header (16 fields). A withdrawals root selects the Capella layout (17 fields). */
static bytes_t fixture_header(uint64_t number, const uint8_t* withdrawals_root) {
  uint8_t  z32[32]    = {0};
  uint8_t  z20[20]    = {0};
  uint8_t  bloom[256] = {0};
  buffer_t json       = {0};
  bprintf(&json,
          "{\"parentHash\":\"0x%x\",\"miner\":\"0x%x\",\"stateRoot\":\"0x%x\","
          "\"transactionsRoot\":\"0x%x\",\"receiptsRoot\":\"0x%x\",\"logsBloom\":\"0x%x\","
          "\"number\":\"0x%lx\",\"gasLimit\":\"0x1\",\"gasUsed\":\"0x0\",\"timestamp\":\"0x1\","
          "\"extraData\":\"0x\",\"mixHash\":\"0x%x\",\"baseFeePerGas\":\"0x1\"",
          bytes(z32, 32), bytes(z20, 20), bytes(z32, 32),
          bytes((uint8_t*) EMPTY_ROOT_HASH, 32), bytes(z32, 32), bytes(bloom, 256),
          number, bytes(z32, 32));
  if (withdrawals_root)
    bprintf(&json, ",\"withdrawalsRoot\":\"0x%x\"", bytes((uint8_t*) withdrawals_root, 32));
  bprintf(&json, "}");

  json_t  block  = json_parse((char*) json.data.data);
  bytes_t header = NULL_BYTES;
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, eth_el_header_build_from_json(NULL, &header, withdrawals_root ? C4_FORK_CAPELLA : C4_FORK_BELLATRIX, block));
  buffer_free(&json);

  bytes_t payload = header;
  TEST_ASSERT_EQUAL_INT(RLP_LIST, rlp_decode(&payload, 0, &payload));
  TEST_ASSERT_EQUAL_INT(withdrawals_root ? 17 : 16, (int) rlp_decode(&payload, -1, NULL));
  TEST_ASSERT_TRUE_MESSAGE(payload.len >= 256, "header payload must need a multi-byte RLP length");
  TEST_ASSERT_EQUAL_UINT64(number, eth_el_header_get_uint64(header, EL_BLOCK_NUMBER));
  if (withdrawals_root)
    TEST_ASSERT_EQUAL_UINT32(32, eth_el_header_get(header, EL_WITHDRAWALS_ROOT).len);
  else
    TEST_ASSERT_TRUE(eth_el_header_get(header, EL_WITHDRAWALS_ROOT).len != 32);
  return header;
}

static bytes_t make_feed(bytes_t header) {
  uint8_t tail[45];
  uint8_t sig[65];
  memset(tail, 0x5a, sizeof(tail));
  memset(sig, 0xab, sizeof(sig));
  return feed_proof_from(header, bytes(tail, sizeof(tail)), bytes(sig, sizeof(sig)));
}

static void load_arb_head(prover_ctx_t* ctx, bytes_t header) {
  memset(ctx, 0, sizeof(*ctx));
  ctx->chain_id   = C4_CHAIN_ARB_ONE;
  json_t      tag = json_parse("\"latest\"");
  eth_block_t out = {0};
  TEST_ASSERT_EQUAL_INT(C4_PENDING, arb_get_el_block(ctx, tag, &out, false));
  data_request_t* req = c4_state_get_pending_request(&ctx->state);
  TEST_ASSERT_NOT_NULL(req);
  TEST_ASSERT_NOT_NULL(req->url);
  TEST_ASSERT_NOT_NULL(strstr(req->url, "arb_preconf/latest"));
  req->response = make_feed(header);
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, arb_get_el_block(ctx, tag, &out, false));
  TEST_ASSERT_NULL(c4_state_get_pending_request(&ctx->state));
  TEST_ASSERT_NULL(out.el_body.bytes.data);
  TEST_ASSERT_EQUAL_UINT64(eth_el_header_get_uint64(header, EL_BLOCK_NUMBER), out.slot);
}

static bytes_t rlp_string_list(uint32_t item_len) {
  uint8_t* item = safe_calloc(1, item_len);
  memset(item, 0x11, item_len);
  buffer_t buf = {0};
  rlp_add_item(&buf, bytes(item, item_len));
  rlp_to_list(&buf);
  safe_free(item);
  return buf.data;
}

static bytes_t rlp_string(uint32_t len) {
  uint8_t* item = safe_calloc(1, len);
  memset(item, 0x22, len);
  buffer_t buf = {0};
  rlp_add_item(&buf, bytes(item, len));
  safe_free(item);
  return buf.data;
}

static bytes_t empty_rlp_list(void) {
  static uint8_t byte = 0xc0;
  return bytes(&byte, 1);
}

/** One withdrawal `[7, 8, 0xcd.., 9]` wrapped as a withdrawals list. */
static bytes_t withdrawal_rlp_list(void) {
  uint8_t  addr[20];
  buffer_t item = {0};
  buffer_t list = {0};
  memset(addr, 0xcd, sizeof(addr));
  rlp_add_uint64(&item, 7);
  rlp_add_uint64(&item, 8);
  rlp_add_item(&item, bytes(addr, sizeof(addr)));
  rlp_add_uint64(&item, 9);
  rlp_to_list(&item);
  buffer_append(&list, item.data);
  rlp_to_list(&list);
  buffer_free(&item);
  return list.data;
}

static bytes_t join_rlp_list(bytes_t* items, int n) {
  buffer_t buf = {0};
  for (int i = 0; i < n; i++) buffer_append(&buf, items[i]);
  rlp_to_list(&buf);
  return buf.data;
}

static bytes_t json_rpc_hex(bytes_t raw) {
  buffer_t buf = {0};
  bprintf(&buf, "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"0x%x\"}", raw);
  return buf.data;
}

static data_request_t* request_latest_body(prover_ctx_t* ctx, bytes_t header, const uint8_t hash[32]) {
  load_arb_head(ctx, header);
  eth_block_t out = {0};
  TEST_ASSERT_EQUAL_INT(C4_PENDING, arb_get_el_block(ctx, json_parse("\"latest\""), &out, true));
  data_request_t* req = c4_state_get_pending_request(&ctx->state);
  TEST_ASSERT_NOT_NULL(req);
  TEST_ASSERT_TRUE_MESSAGE(contains(req->payload, "debug_getRawBlock"), "signed head body is debug_getRawBlock");
  TEST_ASSERT_FALSE_MESSAGE(contains(req->payload, "eth_getProof"), "the signed head is not an EIP-2935 proof");
  buffer_t hex = {0};
  bprintf(&hex, "%x", bytes((uint8_t*) hash, 32));
  TEST_ASSERT_TRUE_MESSAGE(contains(req->payload, (char*) hex.data.data), "raw block is fetched for the signed hash");
  buffer_free(&hex);
  return req;
}

/**
 * A pre-Shanghai raw block is header, transactions and uncles. The body stored for it
 * must still carry an empty withdrawals list, and a block that already has one must
 * keep that list. The header bytes are what the feed signed, so a different header is rejected.
 */
void test_arb_raw_block_body_follows_the_signed_header(void) {
  bytes_t   header = fixture_header(ARB_FIXTURE_NUMBER, NULL);
  bytes32_t hash   = {0};
  keccak(header, hash);

  bytes_t uncles         = rlp_string_list(80);
  bytes_t uncles_payload = uncles;
  TEST_ASSERT_EQUAL_INT(RLP_LIST, rlp_decode(&uncles_payload, 0, &uncles_payload));
  TEST_ASSERT_TRUE(uncles_payload.len >= 56 && uncles_payload.len < 256);

  bytes_t items[4];
  items[0]            = header;
  items[1]            = empty_rlp_list();
  items[2]            = uncles;
  bytes_t         raw = join_rlp_list(items, 3);
  prover_ctx_t    ctx = {0};
  data_request_t* req = request_latest_body(&ctx, header, hash);
  req->response       = json_rpc_hex(raw);

  eth_block_t out = {0};
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, arb_get_el_block(&ctx, json_parse("\"latest\""), &out, true), ctx.state.error ? ctx.state.error : "body");
  TEST_ASSERT_NULL(c4_state_get_pending_request(&ctx.state));
  TEST_ASSERT_EQUAL_MEMORY(hash, out.el_block_hash, 32);
  TEST_ASSERT_NOT_NULL(out.el_body.def);
  TEST_ASSERT_EQUAL_STRING("content", out.el_body.def->name);
  TEST_ASSERT_EQUAL_UINT32(0, ssz_len(ssz_get(&out.el_body, "transactions")));
  TEST_ASSERT_EQUAL_UINT32(0, ssz_len(ssz_get(&out.el_body, "withdrawals")));
  bytes32_t withdrawal_root = {0};
  eth_get_withdrawals_root(withdrawal_root, ssz_get(&out.el_body, "withdrawals"));
  TEST_ASSERT_EQUAL_MEMORY(EMPTY_ROOT_HASH, withdrawal_root, 32);
  TEST_ASSERT_TRUE(eth_el_header_get(out.el_header, EL_WITHDRAWALS_ROOT).len != 32);

  eth_block_t again = {0};
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, arb_get_el_block(&ctx, json_parse("\"latest\""), &again, true));
  TEST_ASSERT_NULL(c4_state_get_pending_request(&ctx.state));
  TEST_ASSERT_EQUAL_MEMORY(out.el_body.bytes.data, again.el_body.bytes.data, out.el_body.bytes.len);
  c4_state_free(&ctx.state);
  safe_free(raw.data);
  safe_free(uncles.data);
  items[2] = empty_rlp_list();

  bytes_t flipped = bytes_dup(header);
  flipped.data[flipped.len - 1] ^= 0xff;
  items[0] = flipped;
  raw      = join_rlp_list(items, 3);
  memset(&ctx, 0, sizeof(ctx));
  req           = request_latest_body(&ctx, header, hash);
  req->response = json_rpc_hex(raw);
  memset(&out, 0, sizeof(out));
  TEST_ASSERT_EQUAL_INT(C4_ERROR, arb_get_el_block(&ctx, json_parse("\"latest\""), &out, true));
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "raw block does not match the proven block hash"));
  TEST_ASSERT_NULL(out.el_body.bytes.data);
  c4_state_free(&ctx.state);
  safe_free(raw.data);
  safe_free(flipped.data);

  bytes_t bogus = rlp_string(60);
  items[0]      = bogus;
  items[1]      = empty_rlp_list();
  items[2]      = empty_rlp_list();
  raw           = join_rlp_list(items, 3);
  memset(&ctx, 0, sizeof(ctx));
  req           = request_latest_body(&ctx, header, hash);
  req->response = json_rpc_hex(raw);
  memset(&out, 0, sizeof(out));
  TEST_ASSERT_EQUAL_INT(C4_ERROR, arb_get_el_block(&ctx, json_parse("\"latest\""), &out, true));
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "raw block is not a header, transactions and uncles"));
  c4_state_free(&ctx.state);
  safe_free(raw.data);
  safe_free(bogus.data);

  items[0] = header;
  items[1] = empty_rlp_list();
  raw      = join_rlp_list(items, 2);
  memset(&ctx, 0, sizeof(ctx));
  req           = request_latest_body(&ctx, header, hash);
  req->response = json_rpc_hex(raw);
  memset(&out, 0, sizeof(out));
  TEST_ASSERT_EQUAL_INT(C4_ERROR, arb_get_el_block(&ctx, json_parse("\"latest\""), &out, true));
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "Invalid RLP withdrawals"));
  c4_state_free(&ctx.state);
  safe_free(raw.data);

  bytes_t withdrawals = withdrawal_rlp_list();
  items[0]            = header;
  items[1]            = empty_rlp_list();
  items[2]            = empty_rlp_list();
  items[3]            = withdrawals;
  raw                 = join_rlp_list(items, 4);
  memset(&ctx, 0, sizeof(ctx));
  req           = request_latest_body(&ctx, header, hash);
  req->response = json_rpc_hex(raw);
  memset(&out, 0, sizeof(out));
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, arb_get_el_block(&ctx, json_parse("\"latest\""), &out, true), ctx.state.error ? ctx.state.error : "4-item");
  ssz_ob_t wds = ssz_get(&out.el_body, "withdrawals");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, ssz_len(wds), "a withdrawals list already present must not be replaced by an empty one");
  ssz_ob_t w = ssz_at(wds, 0);
  TEST_ASSERT_EQUAL_UINT64(7, ssz_get_uint64(&w, "index"));
  TEST_ASSERT_EQUAL_UINT64(8, ssz_get_uint64(&w, "validatorIndex"));
  TEST_ASSERT_EQUAL_UINT64(9, ssz_get_uint64(&w, "amount"));
  uint8_t addr[20];
  memset(addr, 0xcd, sizeof(addr));
  TEST_ASSERT_EQUAL_MEMORY(addr, ssz_get(&w, "address").bytes.data, 20);
  c4_state_free(&ctx.state);
  safe_free(raw.data);
  safe_free(withdrawals.data);
  safe_free(header.data);
}

/**
 * The history window is decided from the signed head before any `eth_getProof`.
 * Distance `window` is still served; one block further is not. The head number
 * and its hash are the feed itself, including an upper-case quantity.
 */
void test_arb_history_window_is_checked_before_fetch(void) {
  TEST_ASSERT_TRUE(ARB_FIXTURE_NUMBER > ARB_EIP2935_HISTORY_WINDOW);
  bytes_t   header = fixture_header(ARB_FIXTURE_NUMBER, NULL);
  bytes32_t hash   = {0};
  keccak(header, hash);
  uint64_t edge    = ARB_FIXTURE_NUMBER - ARB_EIP2935_HISTORY_WINDOW;
  uint64_t outside = edge - 1;
  char     tagbuf[80];

  prover_ctx_t ctx = {0};
  load_arb_head(&ctx, header);
  eth_block_t out = {0};
  TEST_ASSERT_EQUAL_INT(C4_ERROR, arb_get_el_block(&ctx, quantity_tag(ARB_FIXTURE_NUMBER + 1, tagbuf, sizeof(tagbuf)), &out, false));
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "newer than the signed head"));
  TEST_ASSERT_NULL(c4_state_get_pending_request(&ctx.state));
  c4_state_free(&ctx.state);

  memset(&ctx, 0, sizeof(ctx));
  load_arb_head(&ctx, header);
  memset(&out, 0, sizeof(out));
  TEST_ASSERT_EQUAL_INT(C4_ERROR, arb_get_el_block(&ctx, quantity_tag(outside, tagbuf, sizeof(tagbuf)), &out, true));
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "outside the EIP-2935 history window"));
  TEST_ASSERT_NULL_MESSAGE(c4_state_get_pending_request(&ctx.state), "a block outside the window must not request a proof");
  c4_state_free(&ctx.state);

  memset(&ctx, 0, sizeof(ctx));
  load_arb_head(&ctx, header);
  memset(&out, 0, sizeof(out));
  TEST_ASSERT_EQUAL_INT(C4_ERROR, arb_get_el_block(&ctx, quantity_tag(0, tagbuf, sizeof(tagbuf)), &out, false));
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "outside the EIP-2935 history window"));
  TEST_ASSERT_NULL(c4_state_get_pending_request(&ctx.state));
  c4_state_free(&ctx.state);

  memset(&ctx, 0, sizeof(ctx));
  load_arb_head(&ctx, header);
  memset(&out, 0, sizeof(out));
  TEST_ASSERT_EQUAL_INT(C4_PENDING, arb_get_el_block(&ctx, quantity_tag(edge, tagbuf, sizeof(tagbuf)), &out, true));
  TEST_ASSERT_NULL(ctx.state.error);
  data_request_t* req = c4_state_get_pending_request(&ctx.state);
  TEST_ASSERT_NOT_NULL(req);
  TEST_ASSERT_TRUE_MESSAGE(contains(req->payload, "eth_getProof"), "the oldest served block still needs the history proof");
  TEST_ASSERT_FALSE(contains(req->payload, "debug_getRawBlock"));
  bytes32_t slot = {0};
  eth_eip2935_slot_for(edge, ARB_EIP2935_HISTORY_WINDOW, slot);
  buffer_t slot_hex = {0};
  bprintf(&slot_hex, "%x", bytes(slot, 32));
  TEST_ASSERT_TRUE_MESSAGE(contains(req->payload, (char*) slot_hex.data.data), "the history slot uses the Arbitrum window");
  buffer_free(&slot_hex);
  c4_state_free(&ctx.state);

  memset(&ctx, 0, sizeof(ctx));
  load_arb_head(&ctx, header);
  memset(&out, 0, sizeof(out));
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, arb_get_el_block(&ctx, json_parse("\"0xF4240\""), &out, false));
  TEST_ASSERT_NULL(c4_state_get_pending_request(&ctx.state));
  TEST_ASSERT_EQUAL_MEMORY(hash, out.el_block_hash, 32);
  TEST_ASSERT_NULL(out.el_body.bytes.data);
  c4_state_free(&ctx.state);

  buffer_t hash_json = {0};
  bprintf(&hash_json, "\"0x%x\"", bytes(hash, 32));
  memset(&ctx, 0, sizeof(ctx));
  load_arb_head(&ctx, header);
  memset(&out, 0, sizeof(out));
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, arb_get_el_block(&ctx, json_parse((char*) hash_json.data.data), &out, false));
  TEST_ASSERT_NULL(c4_state_get_pending_request(&ctx.state));
  TEST_ASSERT_EQUAL_MEMORY(hash, out.el_block_hash, 32);
  c4_state_free(&ctx.state);
  buffer_free(&hash_json);

  memset(&ctx, 0, sizeof(ctx));
  load_arb_head(&ctx, header);
  memset(&out, 0, sizeof(out));
  TEST_ASSERT_EQUAL_INT(C4_ERROR, arb_get_el_block(&ctx, json_parse("\"0xffffffffffffffff\""), &out, false));
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "newer than the signed head"));
  TEST_ASSERT_NULL(c4_state_get_pending_request(&ctx.state));
  c4_state_free(&ctx.state);
  safe_free(header.data);
}

static ssz_ob_t sample_withdrawals(void) {
  ssz_builder_t content = ssz_builder_for_type(ETH_SSZ_EL_BLOCK_CONTENT);
  ssz_builder_t wds     = ssz_builder_for_def(ssz_get_def(content.def, "withdrawals"));
  ssz_builder_free(&content);
  ssz_builder_t w = ssz_builder_for_def(&DENEP_WITHDRAWAL_CONTAINER);
  uint8_t       addr[20];
  memset(addr, 0xcd, sizeof(addr));
  ssz_add_uint64(&w, 7);
  ssz_add_uint64(&w, 8);
  ssz_add_bytes(&w, "address", bytes(addr, sizeof(addr)));
  ssz_add_uint64(&w, 9);
  ssz_add_dynamic_list_builders(&wds, 1, w);
  return ssz_builder_to_bytes(&wds);
}

static ssz_ob_t block_proof_with_body(const uint8_t hash[32], bytes_t withdrawals) {
  ssz_builder_t proof   = ssz_builder_for_type(ETH_SSZ_VERIFY_BLOCK_PROOF);
  ssz_builder_t content = ssz_builder_for_def(ssz_get_def(ssz_get_def(proof.def, "body"), "content"));
  TEST_ASSERT_NOT_NULL(content.def);
  ssz_add_bytes(&content, "transactions", NULL_BYTES);
  ssz_add_bytes(&content, "withdrawals", withdrawals);
  ssz_add_builders(&proof, "body", content);
  uint8_t selector[33] = {0};
  memcpy(selector + 1, hash, 32);
  ssz_add_bytes(&proof, "elProof", bytes(selector, sizeof(selector)));
  return ssz_builder_to_bytes(&proof);
}

#ifdef EL_HEADER_CACHE
static void assert_body_against_header(chain_id_t chain, bytes_t header, const uint8_t hash[32], bytes_t withdrawals, bool accept) {
  c4_header_cache_clear();
  uint64_t number = eth_el_header_get_uint64(header, EL_BLOCK_NUMBER);
  c4_header_cache_put(chain, number, hash, header, NULL);

  verify_ctx_t ctx = {0};
  ctx.chain_id     = chain;
  ctx.method       = "eth_getBlockByNumber";
  char args[64];
  snprintf(args, sizeof(args), "[\"0x%llx\",false]", (unsigned long long) number);
  ctx.args  = json_parse(args);
  ctx.proof = block_proof_with_body(hash, withdrawals);

  bool ok = verify_block_proof(&ctx);
  if (accept) {
    TEST_ASSERT_TRUE_MESSAGE(ok, ctx.state.error ? ctx.state.error : "verify_block_proof rejected the withdrawals list");
    TEST_ASSERT_EQUAL_UINT32(withdrawals.len == 0 ? 0 : 1, ssz_len(ssz_get(&ctx.data, "withdrawals")));
  }
  else {
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_NOT_NULL(ctx.state.error);
    TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "invalid withdrawal root"));
  }
  if (ctx.flags & VERIFY_FLAG_FREE_DATA) safe_free(ctx.data.bytes.data);
  safe_free(ctx.proof.bytes.data);
  c4_state_free(&ctx.state);
  c4_header_cache_clear();
}
#endif

/**
 * A header without `withdrawalsRoot` accepts only the empty withdrawals list.
 * A list whose merkle matches `withdrawalsRoot` is still accepted when the header has one.
 */
void test_verify_block_withdrawals_without_root(void) {
#ifdef EL_HEADER_CACHE
  bytes_t   pre_shanghai = fixture_header(ARB_FIXTURE_NUMBER, NULL);
  bytes32_t hash         = {0};
  keccak(pre_shanghai, hash);
  ssz_ob_t  withdrawals = sample_withdrawals();
  bytes32_t merkle      = {0};
  eth_get_withdrawals_root(merkle, withdrawals);
  TEST_ASSERT_TRUE(memcmp(merkle, EMPTY_ROOT_HASH, 32) != 0);

  assert_body_against_header(C4_CHAIN_ARB_ONE, pre_shanghai, hash, NULL_BYTES, true);
  assert_body_against_header(C4_CHAIN_MAINNET, pre_shanghai, hash, NULL_BYTES, true);
  assert_body_against_header(C4_CHAIN_ARB_ONE, pre_shanghai, hash, withdrawals.bytes, false);
  assert_body_against_header(C4_CHAIN_MAINNET, pre_shanghai, hash, withdrawals.bytes, false);

  bytes_t   shanghai      = fixture_header(ARB_FIXTURE_NUMBER, merkle);
  bytes32_t shanghai_hash = {0};
  keccak(shanghai, shanghai_hash);
  assert_body_against_header(C4_CHAIN_MAINNET, shanghai, shanghai_hash, withdrawals.bytes, true);
  assert_body_against_header(C4_CHAIN_ARB_ONE, shanghai, shanghai_hash, withdrawals.bytes, true);

  safe_free(withdrawals.bytes.data);
  safe_free(pre_shanghai.data);
  safe_free(shanghai.data);
#else
  TEST_IGNORE_MESSAGE("EL_HEADER_CACHE is required to resolve a blockHash proof");
#endif
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_nitro_signed_tail);
  RUN_TEST(test_arb_feed_proof_from_block);
  RUN_TEST(test_arb_feed_proof_is_union_variant);
  RUN_TEST(test_arb_feed_digest_matches_nitro_layout);
  RUN_TEST(test_arb_feed_rejects_other_signer);
  RUN_TEST(test_arb_feed_rejects_short_tail_and_prenitro);
  RUN_TEST(test_arb_feed_nests_eip2935);
  RUN_TEST(test_arb_eip2935_slot_modulus);
  RUN_TEST(test_arb_get_el_block_with_body_requests_the_head);
  RUN_TEST(test_arb_get_el_block_rejects_before_fetch);
  RUN_TEST(test_arb_feed_rejects_history_without_proof);
  RUN_TEST(test_arb_raw_block_body_follows_the_signed_header);
  RUN_TEST(test_arb_history_window_is_checked_before_fetch);
  RUN_TEST(test_verify_block_withdrawals_without_root);
  return UNITY_END();
}

#endif
