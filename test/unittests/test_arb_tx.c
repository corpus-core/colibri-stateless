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

#include "unity.h"

#ifndef CHAIN_ARB

void setUp(void) {}
void tearDown(void) {}

int main(void) {
  UNITY_BEGIN();
  return UNITY_END();
}

#else

#include "beacon_types.h"
#include "bytes.h"
#include "eth_tx.h"
#include "rlp.h"
#include "ssz.h"
#include "state.h"
#include <stdio.h>
#include <string.h>

// common.HexToAddress("0xa4b05") and common.HexToAddress("0x6e")
static const uint8_t ARBOS[20]     = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x0a, 0x4b, 0x05};
static const uint8_t RETRYABLE[20] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x6e};

void setUp(void) {}
void tearDown(void) {}

static void add_filled(buffer_t* buf, uint32_t len, uint8_t fill) {
  uint8_t tmp[32] = {0};
  memset(tmp, fill, len);
  rlp_add_item(buf, bytes(tmp, len));
}

static void seal(buffer_t* buf, uint8_t type) {
  rlp_to_list(buf);
  buffer_splice(buf, 0, 0, bytes(&type, 1));
}

static ssz_ob_t decode_tx(bytes_t raw) {
  verify_ctx_t  ctx     = {0};
  ssz_builder_t builder = ssz_builder_for_type(ETH_SSZ_DATA_TX);
  bytes32_t     zero    = {0};
  bool          ok      = c4_write_tx_data_from_raw(&ctx, &builder, raw, zero, zero, 9, 3, 0, 0);
  TEST_ASSERT_NULL_MESSAGE(ctx.state.error, ctx.state.error ? ctx.state.error : "decode error");
  TEST_ASSERT_TRUE(ok);
  c4_state_free(&ctx.state);
  return ssz_builder_to_bytes(&builder);
}

static void assert_key(const char* json, const char* key, bool present) {
  char needle[80];
  snprintf(needle, sizeof(needle), "\"%s\"", key);
  if (present)
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(json, needle), json);
  else
    TEST_ASSERT_NULL_MESSAGE(strstr(json, needle), json);
}

static void assert_addr(ssz_ob_t tx, const char* field, const uint8_t addr[20]) {
  bytes_t got = ssz_get(&tx, field).bytes;
  TEST_ASSERT_EQUAL_UINT32(20, got.len);
  TEST_ASSERT_EQUAL_MEMORY(addr, got.data, 20);
}

static void assert_common_arb_json(const char* json) {
  assert_key(json, "v", true);
  assert_key(json, "r", true);
  assert_key(json, "s", true);
  assert_key(json, "yParity", false);
  assert_key(json, "accessList", false);
  assert_key(json, "maxPriorityFeePerGas", false);
  assert_key(json, "chainId", true);
  TEST_ASSERT_NOT_NULL(strstr(json, "\"v\":\"0x0\""));
  TEST_ASSERT_NOT_NULL(strstr(json, "\"r\":\"0x0\""));
  TEST_ASSERT_NOT_NULL(strstr(json, "\"s\":\"0x0\""));
}

void test_type2_mask_leaves_arb_bits_clear(void) {
  buffer_t payload = {0};
  uint8_t  to[20]  = {0};
  uint8_t  r[32]   = {0};
  uint8_t  s[32]   = {0};
  uint8_t  type    = 2;
  rlp_add_uint64(&payload, 0x1a6a8cc6eULL);
  rlp_add_uint64(&payload, 0);
  rlp_add_uint64(&payload, 1);
  rlp_add_uint64(&payload, 1);
  rlp_add_uint64(&payload, 21000);
  rlp_add_item(&payload, bytes(to, 20));
  rlp_add_uint64(&payload, 0);
  rlp_add_item(&payload, NULL_BYTES);
  rlp_add_list(&payload, NULL_BYTES);
  rlp_add_uint64(&payload, 0);
  hex_to_bytes("28ef61340bd939bc2195fe537567866003e1a15d3c71ff63e1590620aa636276", -1, bytes(r, 32));
  hex_to_bytes("67cbe9d8997f761aecb703304b3800ccf555c9f3dc64214b297fb1966a3b6d83", -1, bytes(s, 32));
  rlp_add_item(&payload, bytes(r, 32));
  rlp_add_item(&payload, bytes(s, 32));
  seal(&payload, type);

  ssz_ob_t tx   = decode_tx(payload.data);
  ssz_ob_t mask = ssz_get(&tx, "_optmask");
  TEST_ASSERT_EQUAL_UINT32(8, mask.bytes.len);
  uint64_t bits = bytes_as_le(mask.bytes);
  TEST_ASSERT_EQUAL_UINT64(0, bits >> 29);
  TEST_ASSERT_NOT_EQUAL(0, bits & TX_MAX_FEE_PER_GAS);
  TEST_ASSERT_NOT_EQUAL(0, bits & TX_CHAIN_ID);
  TEST_ASSERT_NOT_EQUAL(0, bits & TX_Y_PARITY);
  TEST_ASSERT_NOT_EQUAL(0, bits & TX_ACCESS_LIST);
  TEST_ASSERT_EQUAL_UINT64(0, bits & TX_AUTHORIZATION_LIST);
  TEST_ASSERT_EQUAL_UINT64(0, bits & TX_BLOCK_TIMESTAMP);
  char* json = ssz_dump_to_str(tx, false, true);
  assert_key(json, "requestId", false);
  assert_key(json, "maxSubmissionFee", false);
  assert_key(json, "ticketId", false);
  assert_key(json, "maxFeePerGas", true);
  assert_key(json, "chainId", true);
  safe_free(json);
  safe_free(tx.bytes.data);
  buffer_free(&payload);
}

void test_arb_deposit(void) {
  buffer_t buf = {0};
  uint8_t  from[20];
  memset(from, 0x11, 20);
  rlp_add_uint64(&buf, 42161);
  add_filled(&buf, 32, 0x22);
  rlp_add_item(&buf, bytes(from, 20));
  add_filled(&buf, 20, 0x33);
  rlp_add_uint64(&buf, 7);
  seal(&buf, 0x64);

  ssz_ob_t tx = decode_tx(buf.data);
  TEST_ASSERT_EQUAL_UINT64(0x64, ssz_get_uint64(&tx, "type"));
  TEST_ASSERT_EQUAL_UINT64(0, ssz_get_uint64(&tx, "nonce"));
  TEST_ASSERT_EQUAL_UINT64(0, ssz_get_uint64(&tx, "gas"));
  TEST_ASSERT_EQUAL_UINT64(0, ssz_get_uint64(&tx, "gasPrice"));
  TEST_ASSERT_EQUAL_UINT64(42161, ssz_get_uint64(&tx, "chainId"));
  TEST_ASSERT_EQUAL_UINT32(0, ssz_get(&tx, "input").bytes.len);
  assert_addr(tx, "from", from);
  uint8_t to[20];
  memset(to, 0x33, 20);
  assert_addr(tx, "to", to);
  char* json = ssz_dump_to_str(tx, false, true);
  assert_common_arb_json(json);
  assert_key(json, "requestId", true);
  assert_key(json, "maxFeePerGas", false);
  assert_key(json, "ticketId", false);
  TEST_ASSERT_NOT_NULL(strstr(json, "\"value\":\"0x7\""));
  safe_free(json);
  safe_free(tx.bytes.data);
  buffer_free(&buf);
}

void test_arb_unsigned_and_contract(void) {
  buffer_t buf = {0};
  uint8_t  from[20];
  memset(from, 0x44, 20);
  rlp_add_uint64(&buf, 42161);
  rlp_add_item(&buf, bytes(from, 20));
  rlp_add_uint64(&buf, 9);
  rlp_add_uint64(&buf, 100);
  rlp_add_uint64(&buf, 21000);
  rlp_add_item(&buf, NULL_BYTES);
  rlp_add_uint64(&buf, 5);
  rlp_add_item(&buf, bytes((uint8_t*) "\xab", 1));
  seal(&buf, 0x65);

  ssz_ob_t tx = decode_tx(buf.data);
  TEST_ASSERT_EQUAL_UINT64(9, ssz_get_uint64(&tx, "nonce"));
  TEST_ASSERT_EQUAL_UINT64(21000, ssz_get_uint64(&tx, "gas"));
  TEST_ASSERT_EQUAL_UINT64(100, ssz_get_uint64(&tx, "gasPrice"));
  TEST_ASSERT_EQUAL_UINT32(0, ssz_get(&tx, "to").bytes.len);
  TEST_ASSERT_EQUAL_UINT8(0xab, ssz_get(&tx, "input").bytes.data[0]);
  char* json = ssz_dump_to_str(tx, false, true);
  assert_common_arb_json(json);
  assert_key(json, "maxFeePerGas", false);
  assert_key(json, "requestId", false);
  TEST_ASSERT_NOT_NULL(strstr(json, "\"to\":null"));
  safe_free(json);
  safe_free(tx.bytes.data);
  buffer_free(&buf);

  buffer_t contract = {0};
  rlp_add_uint64(&contract, 42161);
  add_filled(&contract, 32, 0x55);
  rlp_add_item(&contract, bytes(from, 20));
  rlp_add_uint64(&contract, 100);
  rlp_add_uint64(&contract, 21000);
  add_filled(&contract, 20, 0x66);
  rlp_add_uint64(&contract, 5);
  rlp_add_item(&contract, NULL_BYTES);
  seal(&contract, 0x66);
  tx   = decode_tx(contract.data);
  json = ssz_dump_to_str(tx, false, true);
  TEST_ASSERT_EQUAL_UINT64(0, ssz_get_uint64(&tx, "nonce"));
  TEST_ASSERT_EQUAL_UINT64(100, ssz_get_uint64(&tx, "gasPrice"));
  assert_key(json, "requestId", true);
  assert_key(json, "maxFeePerGas", true);
  TEST_ASSERT_NOT_NULL(strstr(json, "\"maxFeePerGas\":\"0x64\""));
  safe_free(json);
  safe_free(tx.bytes.data);
  buffer_free(&contract);
}

void test_arb_retry(void) {
  buffer_t buf = {0};
  uint8_t  from[20];
  uint8_t  refund[20];
  memset(from, 0x10, 20);
  memset(refund, 0x20, 20);
  rlp_add_uint64(&buf, 42161);
  rlp_add_uint64(&buf, 4);
  rlp_add_item(&buf, bytes(from, 20));
  rlp_add_uint64(&buf, 8);
  rlp_add_uint64(&buf, 30000);
  rlp_add_item(&buf, NULL_BYTES);
  rlp_add_uint64(&buf, 1);
  rlp_add_item(&buf, bytes((uint8_t*) "\x01\x02", 2));
  add_filled(&buf, 32, 0x77);
  rlp_add_item(&buf, bytes(refund, 20));
  rlp_add_uint64(&buf, 9);
  rlp_add_uint64(&buf, 3);
  seal(&buf, 0x68);

  ssz_ob_t tx   = decode_tx(buf.data);
  char*    json = ssz_dump_to_str(tx, false, true);
  assert_common_arb_json(json);
  assert_key(json, "ticketId", true);
  assert_key(json, "refundTo", true);
  assert_key(json, "maxRefund", true);
  assert_key(json, "submissionFeeRefund", true);
  assert_key(json, "maxFeePerGas", true);
  assert_key(json, "requestId", false);
  assert_key(json, "maxSubmissionFee", false);
  assert_addr(tx, "refundTo", refund);
  TEST_ASSERT_EQUAL_UINT64(4, ssz_get_uint64(&tx, "nonce"));
  TEST_ASSERT_EQUAL_UINT32(2, ssz_get(&tx, "input").bytes.len);
  safe_free(json);
  safe_free(tx.bytes.data);
  buffer_free(&buf);
}

static void assert_submit_abi(bytes_t input, const uint8_t request[32], const uint8_t refund[20], const uint8_t beneficiary[20], const uint8_t* retry_to, const uint8_t* data, uint32_t data_len) {
  uint32_t padded = ((data_len + 31) / 32) * 32;
  TEST_ASSERT_EQUAL_UINT32(4 + 12 * 32 + padded, input.len);
  TEST_ASSERT_EQUAL_UINT8(0xc9, input.data[0]);
  TEST_ASSERT_EQUAL_UINT8(0xf9, input.data[1]);
  TEST_ASSERT_EQUAL_UINT8(0x5d, input.data[2]);
  TEST_ASSERT_EQUAL_UINT8(0x32, input.data[3]);
  TEST_ASSERT_EQUAL_MEMORY(request, input.data + 4, 32);
  TEST_ASSERT_EQUAL_UINT8(1, input.data[4 + 32 + 31]);
  TEST_ASSERT_EQUAL_UINT8(2, input.data[4 + 2 * 32 + 31]);
  TEST_ASSERT_EQUAL_UINT8(3, input.data[4 + 3 * 32 + 31]);
  TEST_ASSERT_EQUAL_UINT8(4, input.data[4 + 4 * 32 + 31]);
  TEST_ASSERT_EQUAL_UINT8(5, input.data[4 + 5 * 32 + 31]);
  TEST_ASSERT_EQUAL_UINT8(6, input.data[4 + 6 * 32 + 31]);
  TEST_ASSERT_EQUAL_MEMORY(refund, input.data + 4 + 7 * 32 + 12, 20);
  TEST_ASSERT_EQUAL_MEMORY(beneficiary, input.data + 4 + 8 * 32 + 12, 20);
  if (retry_to)
    TEST_ASSERT_EQUAL_MEMORY(retry_to, input.data + 4 + 9 * 32 + 12, 20);
  else {
    uint8_t zeros[20] = {0};
    TEST_ASSERT_EQUAL_MEMORY(zeros, input.data + 4 + 9 * 32 + 12, 20);
  }
  TEST_ASSERT_EQUAL_UINT8(0x01, input.data[4 + 10 * 32 + 30]);
  TEST_ASSERT_EQUAL_UINT8(0x60, input.data[4 + 10 * 32 + 31]);
  uint64_t got_len = 0;
  for (int i = 0; i < 32; i++) got_len = (got_len << 8) | input.data[4 + 11 * 32 + i];
  TEST_ASSERT_EQUAL_UINT64(data_len, got_len);
  if (data_len) TEST_ASSERT_EQUAL_MEMORY(data, input.data + 4 + 12 * 32, data_len);
}

void test_arb_submit_retryable_bit40(void) {
  buffer_t buf = {0};
  uint8_t  from[20];
  uint8_t  refund[20];
  uint8_t  beneficiary[20];
  uint8_t  retry_to[20];
  uint8_t  request[32];
  uint8_t  data[2] = {0xab, 0xcd};
  memset(from, 0x01, 20);
  memset(refund, 0x22, 20);
  memset(beneficiary, 0x33, 20);
  memset(retry_to, 0x44, 20);
  memset(request, 0x11, 32);
  rlp_add_uint64(&buf, 42161);
  rlp_add_item(&buf, bytes(request, 32));
  rlp_add_item(&buf, bytes(from, 20));
  rlp_add_uint64(&buf, 1);
  rlp_add_uint64(&buf, 2);
  rlp_add_uint64(&buf, 4);
  rlp_add_uint64(&buf, 5);
  rlp_add_item(&buf, bytes(retry_to, 20));
  rlp_add_uint64(&buf, 3);
  rlp_add_item(&buf, bytes(beneficiary, 20));
  rlp_add_uint64(&buf, 6);
  rlp_add_item(&buf, bytes(refund, 20));
  rlp_add_item(&buf, bytes(data, 2));
  seal(&buf, 0x69);

  ssz_ob_t tx   = decode_tx(buf.data);
  ssz_ob_t mask = ssz_get(&tx, "_optmask");
  uint64_t bits = bytes_as_le(mask.bytes);
  TEST_ASSERT_NOT_EQUAL(0, bits & TX_MAX_SUBMISSION_FEE);
  TEST_ASSERT_NOT_EQUAL(0, bits & ((uint64_t) 1 << 40));
  TEST_ASSERT_EQUAL_UINT64(0, ssz_get_uint64(&tx, "nonce"));
  TEST_ASSERT_EQUAL_UINT64(0, ssz_get_uint64(&tx, "value"));
  TEST_ASSERT_EQUAL_UINT64(4, ssz_get_uint64(&tx, "gasPrice"));
  TEST_ASSERT_EQUAL_UINT64(5, ssz_get_uint64(&tx, "gas"));
  assert_addr(tx, "to", RETRYABLE);
  assert_addr(tx, "from", from);
  assert_addr(tx, "refundTo", refund);
  assert_addr(tx, "beneficiary", beneficiary);
  assert_addr(tx, "retryTo", retry_to);
  assert_submit_abi(ssz_get(&tx, "input").bytes, request, refund, beneficiary, retry_to, data, 2);
  char* json = ssz_dump_to_str(tx, false, true);
  assert_common_arb_json(json);
  assert_key(json, "maxSubmissionFee", true);
  assert_key(json, "requestId", true);
  assert_key(json, "l1BaseFee", true);
  assert_key(json, "depositValue", true);
  assert_key(json, "retryTo", true);
  assert_key(json, "retryValue", true);
  assert_key(json, "retryData", true);
  assert_key(json, "beneficiary", true);
  assert_key(json, "refundTo", true);
  assert_key(json, "maxFeePerGas", true);
  TEST_ASSERT_NOT_NULL(strstr(json, "\"maxSubmissionFee\":\"0x6\""));
  TEST_ASSERT_NOT_NULL(strstr(json, "\"retryData\":\"0xabcd\""));
  safe_free(json);
  safe_free(tx.bytes.data);
  buffer_free(&buf);
}

void test_arb_submit_nil_retry_to(void) {
  buffer_t buf = {0};
  uint8_t  from[20];
  uint8_t  refund[20];
  uint8_t  beneficiary[20];
  uint8_t  request[32];
  memset(from, 0x01, 20);
  memset(refund, 0x22, 20);
  memset(beneficiary, 0x33, 20);
  memset(request, 0x11, 32);
  rlp_add_uint64(&buf, 42161);
  rlp_add_item(&buf, bytes(request, 32));
  rlp_add_item(&buf, bytes(from, 20));
  rlp_add_uint64(&buf, 1);
  rlp_add_uint64(&buf, 2);
  rlp_add_uint64(&buf, 4);
  rlp_add_uint64(&buf, 5);
  rlp_add_item(&buf, NULL_BYTES);
  rlp_add_uint64(&buf, 3);
  rlp_add_item(&buf, bytes(beneficiary, 20));
  rlp_add_uint64(&buf, 6);
  rlp_add_item(&buf, bytes(refund, 20));
  rlp_add_item(&buf, NULL_BYTES);
  seal(&buf, 0x69);

  ssz_ob_t tx   = decode_tx(buf.data);
  uint64_t bits = bytes_as_le(ssz_get(&tx, "_optmask").bytes);
  TEST_ASSERT_EQUAL_UINT64(0, bits & TX_RETRY_TO);
  TEST_ASSERT_NOT_EQUAL(0, bits & TX_RETRY_DATA);
  assert_submit_abi(ssz_get(&tx, "input").bytes, request, refund, beneficiary, NULL, NULL, 0);
  char* json = ssz_dump_to_str(tx, false, true);
  assert_key(json, "retryTo", false);
  assert_key(json, "retryData", true);
  TEST_ASSERT_NOT_NULL(strstr(json, "\"retryData\":\"0x\""));
  safe_free(json);
  safe_free(tx.bytes.data);
  buffer_free(&buf);
}

void test_arb_internal(void) {
  buffer_t buf     = {0};
  uint8_t  data[2] = {0xde, 0xad};
  rlp_add_uint64(&buf, 42161);
  rlp_add_item(&buf, bytes(data, 2));
  seal(&buf, 0x6a);

  ssz_ob_t tx   = decode_tx(buf.data);
  char*    json = ssz_dump_to_str(tx, false, true);
  assert_common_arb_json(json);
  assert_addr(tx, "from", ARBOS);
  assert_addr(tx, "to", ARBOS);
  TEST_ASSERT_EQUAL_UINT64(0, ssz_get_uint64(&tx, "nonce"));
  TEST_ASSERT_EQUAL_UINT64(0, ssz_get_uint64(&tx, "gas"));
  TEST_ASSERT_EQUAL_UINT64(0, ssz_get_uint64(&tx, "gasPrice"));
  TEST_ASSERT_EQUAL_UINT64(0, ssz_get_uint64(&tx, "value"));
  TEST_ASSERT_EQUAL_UINT32(2, ssz_get(&tx, "input").bytes.len);
  TEST_ASSERT_EQUAL_MEMORY(data, ssz_get(&tx, "input").bytes.data, 2);
  assert_key(json, "requestId", false);
  assert_key(json, "maxFeePerGas", false);
  assert_key(json, "maxSubmissionFee", false);
  safe_free(json);
  safe_free(tx.bytes.data);
  buffer_free(&buf);
}

void test_arb_rejects_bad_widths_and_unknown_types(void) {
  buffer_t buf     = {0};
  uint8_t  wide[9] = {1, 0, 0, 0, 0, 0, 0, 0, 1};
  uint8_t  from[20];
  memset(from, 0x44, 20);
  rlp_add_uint64(&buf, 42161);
  rlp_add_item(&buf, bytes(from, 20));
  rlp_add_uint64(&buf, 1);
  rlp_add_item(&buf, bytes(wide, 9));
  rlp_add_uint64(&buf, 1);
  rlp_add_item(&buf, NULL_BYTES);
  rlp_add_uint64(&buf, 0);
  rlp_add_item(&buf, NULL_BYTES);
  seal(&buf, 0x65);
  verify_ctx_t  ctx     = {0};
  ssz_builder_t builder = ssz_builder_for_type(ETH_SSZ_DATA_TX);
  bytes32_t     zero    = {0};
  TEST_ASSERT_FALSE(c4_write_tx_data_from_raw(&ctx, &builder, buf.data, zero, zero, 1, 1, 0, 0));
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  c4_state_free(&ctx.state);
  ssz_builder_free(&builder);
  buffer_free(&buf);

  uint8_t raw_gap[] = {0x67, 0xc0};
  ctx               = (verify_ctx_t) {0};
  builder           = ssz_builder_for_type(ETH_SSZ_DATA_TX);
  TEST_ASSERT_FALSE(c4_write_tx_data_from_raw(&ctx, &builder, bytes(raw_gap, 2), zero, zero, 1, 1, 0, 0));
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "unsupported transaction type 0x67"));
  c4_state_free(&ctx.state);
  ssz_builder_free(&builder);

  uint8_t raw_classic[] = {0x78, 0xc0};
  ctx                   = (verify_ctx_t) {0};
  builder               = ssz_builder_for_type(ETH_SSZ_DATA_TX);
  TEST_ASSERT_FALSE(c4_write_tx_data_from_raw(&ctx, &builder, bytes(raw_classic, 2), zero, zero, 1, 1, 0, 0));
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "unsupported transaction type 0x78"));
  c4_state_free(&ctx.state);
  ssz_builder_free(&builder);
}

static bytes_t make_receipt(buffer_t* buf, uint8_t type) {
  uint8_t bloom[256] = {0};
  rlp_add_uint64(buf, 1);
  rlp_add_uint64(buf, 50);
  rlp_add_item(buf, bytes(bloom, 256));
  rlp_add_list(buf, NULL_BYTES);
  seal(buf, type);
  return buf->data;
}

void test_arb_receipt_l1_block_and_base_fee(void) {
  buffer_t tx = {0};
  rlp_add_uint64(&tx, 42161);
  rlp_add_item(&tx, bytes((uint8_t*) "\x01", 1));
  seal(&tx, 0x6a);
  buffer_t receipt = {0};
  make_receipt(&receipt, 0x6a);

  verify_ctx_t  ctx     = {0};
  ssz_builder_t builder = ssz_builder_for_type(ETH_SSZ_DATA_RECEIPT);
  bytes32_t     zero    = {0};
  uint64_t      cum     = 0;
  uint32_t      logs    = 0;
  ctx.chain_id          = C4_CHAIN_ARB_ONE;
  TEST_ASSERT_TRUE(c4_write_receipt_data_from_raw(&ctx, &builder, tx.data, receipt.data, zero, 10, 1, 100, 0, 0, 42, &cum, &logs));
  ssz_ob_t rec  = ssz_builder_to_bytes(&builder);
  char*    json = ssz_dump_to_str(rec, false, true);
  TEST_ASSERT_NOT_NULL(strstr(json, "\"effectiveGasPrice\":\"0x64\""));
  TEST_ASSERT_NOT_NULL(strstr(json, "\"l1BlockNumber\":\"0x2a\""));
  assert_key(json, "gasUsedForL1", false);
  assert_key(json, "timeboosted", false);
  assert_addr(rec, "from", ARBOS);
  assert_addr(rec, "to", ARBOS);
  assert_key(json, "contractAddress", false);
  TEST_ASSERT_TRUE_MESSAGE(c4_tx_verify_receipt_data(&ctx, rec, zero, 10, 1, tx.data, receipt.data), ctx.state.error ? ctx.state.error : "receipt verify failed");
  safe_free(json);
  safe_free(rec.bytes.data);
  c4_state_free(&ctx.state);

  ctx          = (verify_ctx_t) {0};
  ctx.chain_id = C4_CHAIN_MAINNET;
  builder      = ssz_builder_for_type(ETH_SSZ_DATA_RECEIPT);
  cum          = 0;
  logs         = 0;
  TEST_ASSERT_TRUE(c4_write_receipt_data_from_raw(&ctx, &builder, tx.data, receipt.data, zero, 10, 1, 100, 0, 0, 42, &cum, &logs));
  rec  = ssz_builder_to_bytes(&builder);
  json = ssz_dump_to_str(rec, false, true);
  TEST_ASSERT_NOT_NULL(strstr(json, "\"effectiveGasPrice\":\"0x0\""));
  assert_key(json, "l1BlockNumber", false);
  safe_free(json);
  safe_free(rec.bytes.data);
  buffer_free(&tx);
  buffer_free(&receipt);
}

static bytes_t nitro_header(buffer_t* header, const uint8_t mix[32]) {
  for (int i = 0; i < 13; i++) rlp_add_item(header, NULL_BYTES);
  rlp_add_item(header, bytes((uint8_t*) mix, 32));
  rlp_to_list(header);
  return header->data;
}

void test_arb_receipt_proof_gains_l1_block_number(void) {
  buffer_t tx = {0};
  rlp_add_uint64(&tx, 42161);
  rlp_add_item(&tx, bytes((uint8_t*) "\x01", 1));
  seal(&tx, 0x6a);
  buffer_t receipt = {0};
  make_receipt(&receipt, 0x6a);

  verify_ctx_t  ctx     = {0};
  ssz_builder_t builder = ssz_builder_for_type(ETH_SSZ_DATA_RECEIPT);
  bytes32_t     zero    = {0};
  uint64_t      cum     = 0;
  uint32_t      logs    = 0;
  ctx.chain_id          = C4_CHAIN_ARB_ONE;
  TEST_ASSERT_TRUE(c4_write_receipt_data_from_raw(&ctx, &builder, tx.data, receipt.data, zero, 10, 0, 100, 0, 0, 1, &cum, &logs));
  ssz_ob_t rec = ssz_builder_to_bytes(&builder);

  uint32_to_le(rec.bytes.data, uint32_from_le(rec.bytes.data) & ~RCPT_L1_BLOCK_NUMBER);
  ssz_ob_t slot = ssz_get(&rec, "l1BlockNumber");
  TEST_ASSERT_EQUAL_UINT32(8, slot.bytes.len);
  memset(slot.bytes.data, 0, slot.bytes.len);
  char* json = ssz_dump_to_str(rec, false, true);
  assert_key(json, "l1BlockNumber", false);
  safe_free(json);

  uint8_t mix[32] = {0};
  mix[8]          = 0x01;
  mix[9]          = 0x02;
  mix[10]         = 0x03;
  mix[11]         = 0x04;
  mix[12]         = 0x05;
  mix[13]         = 0x06;
  mix[14]         = 0x07;
  mix[15]         = 0x08;
  buffer_t header = {0};
  ctx.data        = rec;
  TEST_ASSERT_TRUE(c4_arb_receipt_apply_l1_block_number(&ctx, nitro_header(&header, mix)));
  safe_free(rec.bytes.data);
  rec  = ctx.data;
  json = ssz_dump_to_str(rec, false, true);
  TEST_ASSERT_NOT_NULL(strstr(json, "\"l1BlockNumber\":\"0x102030405060708\""));
  assert_addr(rec, "to", ARBOS);
  safe_free(json);

  slot         = ssz_get(&rec, "l1BlockNumber");
  size_t   pos = (size_t) (slot.bytes.data - rec.bytes.data);
  uint32_t len = slot.bytes.len;
  memmove(rec.bytes.data + pos, rec.bytes.data + pos + len, rec.bytes.len - pos - len);
  rec.bytes.len -= len;
  const ssz_def_t* elements = ssz_container_elements(rec.def);
  uint32_t         count    = ssz_container_len(rec.def);
  uint32_t         cursor   = 0;
  for (uint32_t i = 0; i < count; i++) {
    if (ssz_is_dynamic(elements + i) && cursor + 4 <= pos) {
      uint32_t off = uint32_from_le(rec.bytes.data + cursor);
      if (off >= pos) uint32_to_le(rec.bytes.data + cursor, off - len);
    }
    cursor += ssz_fixed_length(elements + i);
  }
  uint32_to_le(rec.bytes.data, uint32_from_le(rec.bytes.data) & ~RCPT_L1_BLOCK_NUMBER);
  ctx.data = rec;
  TEST_ASSERT_TRUE(c4_arb_receipt_apply_l1_block_number(&ctx, header.data));
  rec  = ctx.data;
  json = ssz_dump_to_str(rec, false, true);
  TEST_ASSERT_NOT_NULL(strstr(json, "\"l1BlockNumber\":\"0x102030405060708\""));
  assert_addr(rec, "from", ARBOS);
  assert_addr(rec, "to", ARBOS);
  safe_free(json);
  safe_free(rec.bytes.data);
  buffer_free(&header);
  buffer_free(&tx);
  buffer_free(&receipt);
}

void test_arb_header_l1_block_number(void) {
  buffer_t header  = {0};
  uint8_t  mix[32] = {0};
  mix[8]           = 0x01;
  mix[9]           = 0x02;
  mix[10]          = 0x03;
  mix[11]          = 0x04;
  mix[12]          = 0x05;
  mix[13]          = 0x06;
  mix[14]          = 0x07;
  mix[15]          = 0x08;
  for (int i = 0; i < 13; i++) rlp_add_item(&header, NULL_BYTES);
  rlp_add_item(&header, bytes(mix, 32));
  rlp_to_list(&header);
  TEST_ASSERT_EQUAL_UINT64(0x0102030405060708ULL, c4_arb_header_l1_block_number(header.data));
  buffer_free(&header);

  buffer_t short_header = {0};
  for (int i = 0; i < 13; i++) rlp_add_item(&short_header, NULL_BYTES);
  rlp_add_item(&short_header, bytes(mix, 8));
  rlp_to_list(&short_header);
  TEST_ASSERT_EQUAL_UINT64(0, c4_arb_header_l1_block_number(short_header.data));
  buffer_free(&short_header);
}

void test_arb_unsigned_receipt_contract_address(void) {
  buffer_t tx = {0};
  uint8_t  from[20];
  memset(from, 0x44, 20);
  rlp_add_uint64(&tx, 42161);
  rlp_add_item(&tx, bytes(from, 20));
  rlp_add_uint64(&tx, 9);
  rlp_add_uint64(&tx, 100);
  rlp_add_uint64(&tx, 21000);
  rlp_add_item(&tx, NULL_BYTES);
  rlp_add_uint64(&tx, 0);
  rlp_add_item(&tx, NULL_BYTES);
  seal(&tx, 0x65);
  buffer_t receipt = {0};
  make_receipt(&receipt, 0x65);

  verify_ctx_t  ctx     = {0};
  ssz_builder_t builder = ssz_builder_for_type(ETH_SSZ_DATA_RECEIPT);
  bytes32_t     zero    = {0};
  uint64_t      cum     = 0;
  uint32_t      logs    = 0;
  ctx.chain_id          = C4_CHAIN_ARB_ONE;
  TEST_ASSERT_TRUE(c4_write_receipt_data_from_raw(&ctx, &builder, tx.data, receipt.data, zero, 3, 0, 7, 0, 0, 1, &cum, &logs));
  ssz_ob_t rec  = ssz_builder_to_bytes(&builder);
  char*    json = ssz_dump_to_str(rec, false, true);
  assert_key(json, "contractAddress", true);
  TEST_ASSERT_NOT_NULL(strstr(json, "\"to\":null"));
  TEST_ASSERT_NOT_NULL(strstr(json, "\"effectiveGasPrice\":\"0x7\""));
  TEST_ASSERT_EQUAL_UINT32(20, ssz_get(&rec, "contractAddress").bytes.len);
  safe_free(json);
  safe_free(rec.bytes.data);
  buffer_free(&tx);
  buffer_free(&receipt);
}

void test_arb_receipt_rejects_bad_to_and_nonce(void) {
  uint8_t       from[20];
  uint8_t       short_to[19];
  uint8_t       wide_nonce[9] = {1, 0, 0, 0, 0, 0, 0, 0, 1};
  verify_ctx_t  ctx           = {0};
  ssz_builder_t builder       = {0};
  bytes32_t     zero          = {0};
  uint64_t      cum           = 0;
  uint32_t      logs          = 0;
  memset(from, 0x44, 20);
  memset(short_to, 0x55, 19);

  buffer_t tx = {0};
  rlp_add_uint64(&tx, 42161);
  rlp_add_item(&tx, bytes(from, 20));
  rlp_add_uint64(&tx, 1);
  rlp_add_uint64(&tx, 1);
  rlp_add_uint64(&tx, 1);
  rlp_add_item(&tx, bytes(short_to, 19));
  rlp_add_uint64(&tx, 0);
  rlp_add_item(&tx, NULL_BYTES);
  seal(&tx, 0x65);
  buffer_t receipt = {0};
  make_receipt(&receipt, 0x65);
  ctx.chain_id = C4_CHAIN_ARB_ONE;
  builder      = ssz_builder_for_type(ETH_SSZ_DATA_RECEIPT);
  TEST_ASSERT_FALSE(c4_write_receipt_data_from_raw(&ctx, &builder, tx.data, receipt.data, zero, 1, 0, 1, 0, 0, 0, &cum, &logs));
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "invalid arbitrum tx to"));
  c4_state_free(&ctx.state);
  ssz_builder_free(&builder);
  buffer_free(&tx);
  buffer_free(&receipt);

  buffer_t nonce_tx = {0};
  rlp_add_uint64(&nonce_tx, 42161);
  rlp_add_item(&nonce_tx, bytes(from, 20));
  rlp_add_item(&nonce_tx, bytes(wide_nonce, 9));
  rlp_add_uint64(&nonce_tx, 1);
  rlp_add_uint64(&nonce_tx, 1);
  rlp_add_item(&nonce_tx, NULL_BYTES);
  rlp_add_uint64(&nonce_tx, 0);
  rlp_add_item(&nonce_tx, NULL_BYTES);
  seal(&nonce_tx, 0x65);
  buffer_t nonce_receipt = {0};
  make_receipt(&nonce_receipt, 0x65);
  ctx          = (verify_ctx_t) {0};
  ctx.chain_id = C4_CHAIN_ARB_ONE;
  builder      = ssz_builder_for_type(ETH_SSZ_DATA_RECEIPT);
  cum          = 0;
  logs         = 0;
  TEST_ASSERT_FALSE(c4_write_receipt_data_from_raw(&ctx, &builder, nonce_tx.data, nonce_receipt.data, zero, 1, 0, 1, 0, 0, 0, &cum, &logs));
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "arbitrum nonce exceeds uint64"));
  c4_state_free(&ctx.state);
  ssz_builder_free(&builder);
  buffer_free(&nonce_tx);
  buffer_free(&nonce_receipt);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_type2_mask_leaves_arb_bits_clear);
  RUN_TEST(test_arb_deposit);
  RUN_TEST(test_arb_unsigned_and_contract);
  RUN_TEST(test_arb_retry);
  RUN_TEST(test_arb_submit_retryable_bit40);
  RUN_TEST(test_arb_submit_nil_retry_to);
  RUN_TEST(test_arb_internal);
  RUN_TEST(test_arb_rejects_bad_widths_and_unknown_types);
  RUN_TEST(test_arb_receipt_l1_block_and_base_fee);
  RUN_TEST(test_arb_receipt_proof_gains_l1_block_number);
  RUN_TEST(test_arb_header_l1_block_number);
  RUN_TEST(test_arb_unsigned_receipt_contract_address);
  RUN_TEST(test_arb_receipt_rejects_bad_to_and_nonce);
  return UNITY_END();
}

#endif
