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
#include "el_header.h"
#include "json.h"
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

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_nitro_signed_tail);
  RUN_TEST(test_arb_feed_proof_from_block);
  RUN_TEST(test_arb_feed_proof_is_union_variant);
  RUN_TEST(test_arb_feed_digest_matches_nitro_layout);
  RUN_TEST(test_arb_feed_rejects_other_signer);
  RUN_TEST(test_arb_feed_rejects_short_tail_and_prenitro);
  return UNITY_END();
}

#endif
