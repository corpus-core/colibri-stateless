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

#include "beacon_types.h"
#include "bytes.h"
#include "chains/arb/server/arb_preconf.h"
#include "chains/arb/server/feed_client.h"
#include "crypto.h"
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

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_nitro_signed_tail);
  RUN_TEST(test_arb_feed_proof_from_block);
  return UNITY_END();
}

#endif
