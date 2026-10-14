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

#ifndef CHAIN_BTC

int main(void) {
  return 0;
}

#else

#include "btc_hash.h"
#include "btc_merkle.h"
#include "btc_tx.h"
#include "unity.h"
#include <string.h>

// Mainnet genesis and block 1. Display hashes and the genesis txid are independent of this code.
static const char* GENESIS_HEADER = "0100000000000000000000000000000000000000000000000000000000000000000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a29ab5f49ffff001d1dac2b7c";
static const char* BLOCK1_HEADER  = "010000006fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d6190000000000982051fd1e4ba744bbbe680e1fee14677ba1a3c3540bf7b1cdb606e857233e0e61bc6649ffff001d01e36299";
static const char* GENESIS_TX     = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4d04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73ffffffff0100f2052a01000000434104678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5fac00000000";
static const char* SEGWIT_TX      = "010000000001010000000000000000000000000000000000000000000000000000000000000000ffffffff00ffffffff010000000000000000000120111111111111111111111111111111111111111111111111111111111111111100000000";

static const char* GENESIS_DISPLAY = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f";
static const char* BLOCK1_DISPLAY  = "00000000839a8e6886ab5951d76f411475428afc90947ee320161bbf18eb6048";
static const char* GENESIS_TXID    = "3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a";
static const char* SEGWIT_TXID     = "e177c42f05a659138e28087326b687504d123c5834e21a49f306a24eabd2b72f";
static const char* SEGWIT_WTXID    = "5962afc251d00f73effb88978292abec05f00ab6ae94a015085345d21cbe2e63";

// Three leaves A || B || C and the parent hashes, computed outside this library.
static const char* MERKLE_ROOT3   = "ab43aed5049351eb898a6c18a37ebff803ba5b0104347980616ac975a22d3c0d";
static const char* MERKLE_HAB     = "01c9f464780a1b6af4eb400fe2f2896cfb2169f5a65701439e4c2c4e213903ef";
static const char* MERKLE_HCC     = "cfc07871b2cf4b35a3c625c1eb0fc963bd0b18d7b6a44d8f4c0ed35f90d36b50";
static const char* MERKLE_AA      = "94b4244b8ffaf7d006b3c43722a2ce391998e44eea9ceecbcdbaf53d0addad98";
static const char* WITNESS_COMMIT = "69c5a5fc944f979128d8623f47df6e532790d453223e06347e95fe8a6016b9a3";

void setUp(void) {}
void tearDown(void) {}

/**
 * Decodes a hex string into a caller buffer.
 *
 * @param hex hex text
 * @param dst destination
 * @param cap capacity of dst
 * @return the decoded slice
 */
static bytes_t load_hex(const char* hex, uint8_t* dst, uint32_t cap) {
  int n = hex_to_bytes(hex, -1, bytes(dst, cap));
  TEST_ASSERT_GREATER_THAN_INT(0, n);
  return bytes(dst, (uint32_t) n);
}

/**
 * Fills an 80-byte header with a synthetic target that every hash meets.
 *
 * `0x2100ffff` sets the top 16 bits, so the proof-of-work check does not depend on the header bytes.
 *
 * @param header 80-byte destination
 * @param parent previous hash, or NULL for zeros
 */
static void fill_easy_header(uint8_t header[80], const uint8_t parent[32]) {
  memset(header, 0, 80);
  header[0] = 1;
  if (parent) memcpy(header + BTC_HEADER_PARENT, parent, 32);
  header[BTC_HEADER_BITS]     = 0xff;
  header[BTC_HEADER_BITS + 1] = 0xff;
  header[BTC_HEADER_BITS + 3] = 0x21;
}

void test_target_and_genesis_pow(void) {
  uint8_t header_buf[80];
  uint8_t hash[32];
  uint8_t display[32];
  uint8_t expect[32];
  uint8_t target[32];
  uint8_t flipped[80];
  bytes_t header = load_hex(GENESIS_HEADER, header_buf, sizeof(header_buf));

  TEST_ASSERT_TRUE(btc_target_from_bits(0x1d00ffffu, target));
  TEST_ASSERT_EQUAL_UINT8(0xff, target[26]);
  TEST_ASSERT_EQUAL_UINT8(0xff, target[27]);
  TEST_ASSERT_EQUAL_UINT8(0x00, target[28]);
  TEST_ASSERT_EQUAL_UINT8(0x00, target[31]);

  memset(target, 0xab, sizeof(target));
  TEST_ASSERT_FALSE(btc_target_from_bits(0, target));
  TEST_ASSERT_EACH_EQUAL_UINT8(0, target, 32);
  TEST_ASSERT_FALSE(btc_target_from_bits(0x1d80ffffu, target));
  TEST_ASSERT_FALSE(btc_target_from_bits(0x23000001u, target));

  TEST_ASSERT_TRUE(btc_target_from_bits(0x207fffffu, target));
  TEST_ASSERT_EQUAL_UINT8(0xff, target[29]);
  TEST_ASSERT_EQUAL_UINT8(0xff, target[30]);
  TEST_ASSERT_EQUAL_UINT8(0x7f, target[31]);

  TEST_ASSERT_TRUE(btc_target_from_bits(0x1d00ffffu, target));
  TEST_ASSERT_TRUE(btc_header_check(header, hash));
  load_hex(GENESIS_DISPLAY, expect, sizeof(expect));
  btc_hash_reverse(hash, display);
  TEST_ASSERT_EQUAL_MEMORY(expect, display, 32);
  btc_hash_reverse(display, display);
  TEST_ASSERT_EQUAL_MEMORY(hash, display, 32);
  TEST_ASSERT_TRUE(btc_hash_cmp(hash, target) < 0);
  TEST_ASSERT_EQUAL_INT(0, btc_hash_cmp(hash, hash));

  memset(target, 0xff, sizeof(target));
  TEST_ASSERT_TRUE(btc_hash_cmp(target, hash) > 0);

  memcpy(flipped, header.data, 80);
  flipped[79] ^= 0x01;
  TEST_ASSERT_FALSE(btc_header_check(bytes(flipped, 80), hash));
  TEST_ASSERT_FALSE(btc_header_check(bytes(header.data, 79), hash));
  TEST_ASSERT_FALSE(btc_header_check(bytes(NULL, 0), hash));
}

void test_header_chain_links_parent(void) {
  uint8_t genesis_buf[80];
  uint8_t block1_buf[80];
  uint8_t chain[160];
  uint8_t first[32];
  uint8_t last[32];
  uint8_t display[32];
  uint8_t expect[32];
  uint8_t easy[160];
  uint8_t parent[32];
  bytes_t genesis = load_hex(GENESIS_HEADER, genesis_buf, sizeof(genesis_buf));
  bytes_t block1  = load_hex(BLOCK1_HEADER, block1_buf, sizeof(block1_buf));

  TEST_ASSERT_TRUE(btc_header_check(block1, last));
  load_hex(BLOCK1_DISPLAY, expect, sizeof(expect));
  btc_hash_reverse(last, display);
  TEST_ASSERT_EQUAL_MEMORY(expect, display, 32);
  btc_header_check(genesis, parent);
  TEST_ASSERT_EQUAL_MEMORY(parent, block1.data + BTC_HEADER_PARENT, 32);

  memcpy(chain, genesis.data, 80);
  memcpy(chain + 80, block1.data, 80);
  TEST_ASSERT_TRUE(btc_check_header_chain(bytes(chain, 160), first, last));
  TEST_ASSERT_EQUAL_MEMORY(parent, first, 32);
  load_hex(BLOCK1_DISPLAY, expect, sizeof(expect));
  btc_hash_reverse(last, display);
  TEST_ASSERT_EQUAL_MEMORY(expect, display, 32);
  TEST_ASSERT_FALSE(btc_check_header_chain(bytes(chain, 159), NULL, NULL));
  TEST_ASSERT_FALSE(btc_check_header_chain(bytes(NULL, 0), NULL, NULL));

  chain[80 + 79] ^= 0x01;
  TEST_ASSERT_FALSE(btc_check_header_chain(bytes(chain, 160), NULL, NULL));

  fill_easy_header(easy, NULL);
  fill_easy_header(easy + 80, NULL);
  TEST_ASSERT_TRUE(btc_header_check(bytes(easy, 80), parent));
  TEST_ASSERT_TRUE(btc_header_check(bytes(easy + 80, 80), last));
  TEST_ASSERT_FALSE(btc_check_header_chain(bytes(easy, 160), NULL, NULL));
  memcpy(easy + 80 + BTC_HEADER_PARENT, parent, 32);
  TEST_ASSERT_TRUE(btc_check_header_chain(bytes(easy, 160), first, first));
  btc_header_check(bytes(easy + 80, 80), last);
  TEST_ASSERT_EQUAL_MEMORY(last, first, 32);

  {
    uint8_t three[240];
    uint8_t hash0[32];
    uint8_t hash1[32];
    fill_easy_header(three, NULL);
    TEST_ASSERT_TRUE(btc_header_check(bytes(three, 80), hash0));
    fill_easy_header(three + 80, hash0);
    TEST_ASSERT_TRUE(btc_header_check(bytes(three + 80, 80), hash1));
    fill_easy_header(three + 160, hash1);
    TEST_ASSERT_TRUE(btc_check_header_chain(bytes(three, 240), NULL, NULL));
    memcpy(three + 160 + BTC_HEADER_PARENT, hash0, 32);
    TEST_ASSERT_TRUE(btc_header_check(bytes(three + 160, 80), hash1));
    TEST_ASSERT_FALSE(btc_check_header_chain(bytes(three, 240), NULL, NULL));
  }
}

void test_hash256_parts_match_contiguous_header(void) {
  uint8_t header_buf[80];
  uint8_t whole[32];
  uint8_t split[32];
  bytes_t header = load_hex(GENESIS_HEADER, header_buf, sizeof(header_buf));
  bytes_t parts[2];

  btc_hash256(header, whole);
  parts[0] = bytes(header.data, 40);
  parts[1] = bytes(header.data + 40, 40);
  TEST_ASSERT_TRUE(btc_hash256_parts(parts, 2, split));
  TEST_ASSERT_EQUAL_MEMORY(whole, split, 32);

  parts[0] = bytes(NULL, 4);
  TEST_ASSERT_FALSE(btc_hash256_parts(parts, 1, split));
  TEST_ASSERT_FALSE(btc_hash256_parts(NULL, 1, split));
}

void test_merkle_rejects_duplicate_and_early_match(void) {
  uint8_t  leaves[96];
  uint8_t  root[32];
  uint8_t  expect[32];
  uint8_t  sibling[64];
  uint8_t  pair[64];
  bool     mutated = false;
  uint32_t i;

  for (i = 0; i < 32; i++) leaves[i] = (uint8_t) i;
  for (i = 0; i < 32; i++) leaves[32 + i] = (uint8_t) (32 + i);
  memset(leaves + 64, 0xab, 32);

  TEST_ASSERT_TRUE(btc_merkle_root(leaves, 3, root, &mutated));
  TEST_ASSERT_FALSE(mutated);
  load_hex(MERKLE_ROOT3, expect, sizeof(expect));
  TEST_ASSERT_EQUAL_MEMORY(expect, root, 32);

  load_hex(MERKLE_HCC, expect, sizeof(expect));
  memcpy(sibling, leaves + 32, 32);
  memcpy(sibling + 32, expect, 32);
  TEST_ASSERT_TRUE(btc_merkle_verify(leaves, 0, 3, bytes(sibling, 64), root));

  load_hex(MERKLE_HAB, expect, sizeof(expect));
  memcpy(sibling, leaves + 64, 32);
  memcpy(sibling + 32, expect, 32);
  TEST_ASSERT_TRUE(btc_merkle_verify(leaves + 64, 2, 3, bytes(sibling, 64), root));

  sibling[0] ^= 0x01;
  TEST_ASSERT_FALSE(btc_merkle_verify(leaves + 64, 2, 3, bytes(sibling, 64), root));

  // One sibling already produces the intermediate parent. That must not count as the root.
  load_hex(MERKLE_HAB, expect, sizeof(expect));
  TEST_ASSERT_FALSE(btc_merkle_verify(leaves, 0, 3, bytes(leaves + 32, 32), expect));

  memcpy(pair, leaves, 32);
  memcpy(pair + 32, leaves, 32);
  TEST_ASSERT_TRUE(btc_merkle_root(pair, 2, root, &mutated));
  TEST_ASSERT_TRUE(mutated);
  load_hex(MERKLE_AA, expect, sizeof(expect));
  TEST_ASSERT_EQUAL_MEMORY(expect, root, 32);
  TEST_ASSERT_FALSE(btc_merkle_verify(leaves, 0, 2, bytes(leaves, 32), root));

  TEST_ASSERT_TRUE(btc_merkle_verify(leaves, 0, 1, bytes(NULL, 0), leaves));
  TEST_ASSERT_FALSE(btc_merkle_verify(leaves, 0, 1, bytes(leaves, 32), leaves));
  TEST_ASSERT_FALSE(btc_merkle_verify(leaves, 3, 3, bytes(NULL, 0), root));
  TEST_ASSERT_FALSE(btc_merkle_verify(leaves, 0, 0, bytes(NULL, 0), root));
  TEST_ASSERT_FALSE(btc_merkle_verify(leaves, 0, 2, bytes(leaves, 31), root));
  TEST_ASSERT_FALSE(btc_merkle_root(NULL, 1, root, NULL));

  mutated = true;
  TEST_ASSERT_TRUE(btc_merkle_root(leaves, 1, root, &mutated));
  TEST_ASSERT_FALSE(mutated);
  TEST_ASSERT_EQUAL_MEMORY(leaves, root, 32);

  // [A,B,C] and [A,B,C,C] share a root. The extra C is a real duplicate, not padding.
  {
    uint8_t four[128];
    uint8_t proof[64];
    memcpy(four, leaves, 96);
    memcpy(four + 96, leaves + 64, 32);
    TEST_ASSERT_TRUE(btc_merkle_root(four, 4, root, &mutated));
    TEST_ASSERT_TRUE(mutated);
    load_hex(MERKLE_ROOT3, expect, sizeof(expect));
    TEST_ASSERT_EQUAL_MEMORY(expect, root, 32);
    memcpy(proof, leaves + 64, 32);
    load_hex(MERKLE_HAB, proof + 32, 32);
    TEST_ASSERT_FALSE(btc_merkle_verify(leaves + 64, 3, 4, bytes(proof, 64), root));
  }

  // Duplicate parents: [A,B,A,B] has distinct leaves but two identical internal nodes.
  {
    uint8_t doubled[128];
    memcpy(doubled, leaves, 64);
    memcpy(doubled + 64, leaves, 64);
    TEST_ASSERT_TRUE(btc_merkle_root(doubled, 4, root, &mutated));
    TEST_ASSERT_TRUE(mutated);
  }
}

void test_genesis_transaction_and_bip34(void) {
  uint8_t  raw_buf[256];
  uint8_t  txid[32];
  uint8_t  wtxid[32];
  uint8_t  expect[32];
  uint8_t  trailing[256];
  bytes_t  prev;
  bytes_t  script;
  uint32_t prev_index = 0;
  uint32_t sequence   = 0;
  uint32_t height     = 1;
  uint64_t value      = 0;
  btc_tx_t tx;
  bytes_t  raw = load_hex(GENESIS_TX, raw_buf, sizeof(raw_buf));

  TEST_ASSERT_TRUE(btc_tx_parse(raw, &tx));
  TEST_ASSERT_FALSE(tx.segwit);
  TEST_ASSERT_EQUAL_UINT32(1, tx.version);
  TEST_ASSERT_EQUAL_UINT32(1, tx.input_count);
  TEST_ASSERT_EQUAL_UINT32(1, tx.output_count);
  TEST_ASSERT_EQUAL_UINT32(0, tx.locktime);
  TEST_ASSERT_TRUE(btc_tx_id(&tx, txid));
  TEST_ASSERT_TRUE(btc_tx_wtxid(&tx, wtxid));
  load_hex(GENESIS_TXID, expect, sizeof(expect));
  TEST_ASSERT_EQUAL_MEMORY(expect, txid, 32);
  TEST_ASSERT_EQUAL_MEMORY(txid, wtxid, 32);

  TEST_ASSERT_TRUE(btc_merkle_verify(txid, 0, 1, bytes(NULL, 0), txid));
  TEST_ASSERT_TRUE(btc_tx_input(&tx, 0, &prev, &prev_index, &script, &sequence));
  TEST_ASSERT_EQUAL_UINT32(0xffffffffu, prev_index);
  TEST_ASSERT_EQUAL_UINT32(0xffffffffu, sequence);
  TEST_ASSERT_TRUE(btc_bip34_height(script, &height));
  TEST_ASSERT_EQUAL_UINT32(486604799u, height);
  TEST_ASSERT_TRUE(btc_tx_output(&tx, 0, &value, &script));
  TEST_ASSERT_EQUAL_UINT64(5000000000ull, value);
  TEST_ASSERT_EQUAL_UINT32(67, script.len);
  TEST_ASSERT_FALSE(btc_tx_witness_commitment(&tx, &script));

  memcpy(trailing, raw.data, raw.len);
  trailing[raw.len] = 0;
  TEST_ASSERT_FALSE(btc_tx_parse(bytes(trailing, raw.len + 1), &tx));
  TEST_ASSERT_NULL(tx.raw.data);
}

void test_segwit_ids_and_witness_commitment(void) {
  uint8_t  raw_buf[128];
  uint8_t  txid[32];
  uint8_t  wtxid[32];
  uint8_t  expect[32];
  uint8_t  built[256];
  uint8_t  commit_a[32];
  uint8_t  commit_b[32];
  uint8_t  root[32];
  uint8_t  bad_nonce[32];
  bytes_t  item;
  bytes_t  nonce;
  bytes_t  commitment;
  uint32_t height = 0;
  uint32_t n      = 0;
  btc_tx_t tx;
  bytes_t  raw = load_hex(SEGWIT_TX, raw_buf, sizeof(raw_buf));

  TEST_ASSERT_TRUE(btc_tx_parse(raw, &tx));
  TEST_ASSERT_TRUE(tx.segwit);
  TEST_ASSERT_TRUE(btc_tx_id(&tx, txid));
  TEST_ASSERT_TRUE(btc_tx_wtxid(&tx, wtxid));
  load_hex(SEGWIT_TXID, expect, sizeof(expect));
  TEST_ASSERT_EQUAL_MEMORY(expect, txid, 32);
  load_hex(SEGWIT_WTXID, expect, sizeof(expect));
  TEST_ASSERT_EQUAL_MEMORY(expect, wtxid, 32);
  TEST_ASSERT_TRUE(btc_tx_witness_item(&tx, 0, 0, &item));
  TEST_ASSERT_EQUAL_UINT32(32, item.len);
  TEST_ASSERT_EQUAL_UINT8(0x11, item.data[0]);
  TEST_ASSERT_FALSE(btc_tx_witness_item(&tx, 0, 1, &item));

  memset(commit_a, 0x11, sizeof(commit_a));
  load_hex(WITNESS_COMMIT, commit_b, sizeof(commit_b));
  memset(root, 0x22, sizeof(root));
  built[n++] = 1;
  built[n++] = 0;
  built[n++] = 0;
  built[n++] = 0;
  built[n++] = 0;
  built[n++] = 1;
  built[n++] = 1;
  memset(built + n, 0, 32);
  n += 32;
  built[n++] = 0xff;
  built[n++] = 0xff;
  built[n++] = 0xff;
  built[n++] = 0xff;
  built[n++] = 4;
  built[n++] = 0x03;
  built[n++] = 0xfc;
  built[n++] = 0x79;
  built[n++] = 0x03;
  built[n++] = 0xff;
  built[n++] = 0xff;
  built[n++] = 0xff;
  built[n++] = 0xff;
  built[n++] = 2;
  memset(built + n, 0, 8);
  n += 8;
  built[n++] = 39;
  built[n++] = 0x6a;
  built[n++] = 0x24;
  built[n++] = 0xaa;
  built[n++] = 0x21;
  built[n++] = 0xa9;
  built[n++] = 0xed;
  memcpy(built + n, commit_a, 32);
  n += 32;
  built[n++] = 0x00;
  memset(built + n, 0, 8);
  n += 8;
  built[n++] = 38;
  built[n++] = 0x6a;
  built[n++] = 0x24;
  built[n++] = 0xaa;
  built[n++] = 0x21;
  built[n++] = 0xa9;
  built[n++] = 0xed;
  memcpy(built + n, commit_b, 32);
  n += 32;
  built[n++] = 1;
  built[n++] = 32;
  memset(built + n, 0x33, 32);
  n += 32;
  built[n++] = 0;
  built[n++] = 0;
  built[n++] = 0;
  built[n++] = 0;

  TEST_ASSERT_TRUE(btc_tx_parse(bytes(built, n), &tx));
  TEST_ASSERT_TRUE(btc_tx_input(&tx, 0, NULL, NULL, &item, NULL));
  TEST_ASSERT_TRUE(btc_bip34_height(item, &height));
  TEST_ASSERT_EQUAL_UINT32(227836u, height);
  TEST_ASSERT_TRUE(btc_tx_coinbase_nonce(&tx, &nonce));
  TEST_ASSERT_EQUAL_UINT32(32, nonce.len);
  TEST_ASSERT_EQUAL_UINT8(0x33, nonce.data[0]);
  TEST_ASSERT_TRUE(btc_tx_witness_commitment(&tx, &commitment));
  TEST_ASSERT_EQUAL_MEMORY(commit_b, commitment.data, 32);
  TEST_ASSERT_TRUE(btc_witness_commitment_matches(commitment, root, nonce.data));
  memcpy(bad_nonce, nonce.data, 32);
  bad_nonce[0] ^= 0x01;
  TEST_ASSERT_FALSE(btc_witness_commitment_matches(commitment, root, bad_nonce));

  built[n - 37] = 31;
  memmove(built + (n - 37) + 32, built + (n - 37) + 33, 4);
  TEST_ASSERT_TRUE(btc_tx_parse(bytes(built, n - 1), &tx));
  TEST_ASSERT_FALSE(btc_tx_coinbase_nonce(&tx, &nonce));
}

void test_bip34_script_numbers(void) {
  uint8_t  script[8];
  uint32_t height = 99;

  script[0] = 0x51;
  TEST_ASSERT_TRUE(btc_bip34_height(bytes(script, 1), &height));
  TEST_ASSERT_EQUAL_UINT32(1, height);
  script[0] = 0x00;
  TEST_ASSERT_TRUE(btc_bip34_height(bytes(script, 1), &height));
  TEST_ASSERT_EQUAL_UINT32(0, height);
  script[0] = 0x60;
  TEST_ASSERT_TRUE(btc_bip34_height(bytes(script, 1), &height));
  TEST_ASSERT_EQUAL_UINT32(16, height);

  memcpy(script, "\x03\xfc\x79\x03", 4);
  TEST_ASSERT_TRUE(btc_bip34_height(bytes(script, 4), &height));
  TEST_ASSERT_EQUAL_UINT32(227836u, height);
  memcpy(script, "\x05\x00\x00\x00\x80\x00", 6);
  TEST_ASSERT_TRUE(btc_bip34_height(bytes(script, 6), &height));
  TEST_ASSERT_EQUAL_UINT32(0x80000000u, height);

  memcpy(script, "\x01\x00", 2);
  TEST_ASSERT_FALSE(btc_bip34_height(bytes(script, 2), &height));
  memcpy(script, "\x02\x01\x00", 3);
  TEST_ASSERT_FALSE(btc_bip34_height(bytes(script, 3), &height));
  memcpy(script, "\x01\x81", 2);
  TEST_ASSERT_FALSE(btc_bip34_height(bytes(script, 2), &height));
  memcpy(script, "\x4c\x03\xfc\x79\x03", 5);
  TEST_ASSERT_FALSE(btc_bip34_height(bytes(script, 5), &height));
  memcpy(script, "\x4d\x03\x00\xfc\x79\x03", 6);
  TEST_ASSERT_FALSE(btc_bip34_height(bytes(script, 6), &height));
  memcpy(script, "\x4e\x03\x00\x00\x00\xfc\x79\x03", 8);
  TEST_ASSERT_FALSE(btc_bip34_height(bytes(script, 8), &height));
  memcpy(script, "\x05\x01\x00\x00\x00\x01", 6);
  TEST_ASSERT_FALSE(btc_bip34_height(bytes(script, 6), &height));
  script[0] = 0x4f;
  TEST_ASSERT_FALSE(btc_bip34_height(bytes(script, 1), &height));
  TEST_ASSERT_FALSE(btc_bip34_height(bytes(NULL, 0), &height));
}

void test_parser_rejects_noncanonical_and_empty_witness(void) {
  uint8_t  raw[128];
  uint32_t n = 0;
  btc_tx_t tx;

  raw[n++] = 1;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 1;
  memset(raw + n, 0, 32);
  n += 32;
  raw[n++] = 0xff;
  raw[n++] = 0xff;
  raw[n++] = 0xff;
  raw[n++] = 0xff;
  raw[n++] = 0xfd;
  raw[n++] = 0x01;
  raw[n++] = 0x00;
  raw[n++] = 0x61;
  raw[n++] = 0xff;
  raw[n++] = 0xff;
  raw[n++] = 0xff;
  raw[n++] = 0xff;
  raw[n++] = 1;
  memset(raw + n, 0, 8);
  n += 8;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  TEST_ASSERT_FALSE(btc_tx_parse(bytes(raw, n), &tx));

  n        = 0;
  raw[n++] = 1;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 1;
  raw[n++] = 1;
  memset(raw + n, 0, 32);
  n += 32;
  raw[n++] = 0xff;
  raw[n++] = 0xff;
  raw[n++] = 0xff;
  raw[n++] = 0xff;
  raw[n++] = 0;
  raw[n++] = 0xff;
  raw[n++] = 0xff;
  raw[n++] = 0xff;
  raw[n++] = 0xff;
  raw[n++] = 1;
  memset(raw + n, 0, 8);
  n += 8;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  TEST_ASSERT_FALSE(btc_tx_parse(bytes(raw, n), &tx));

  raw[0] = 1;
  raw[1] = 0;
  raw[2] = 0;
  raw[3] = 0;
  raw[4] = 0;
  raw[5] = 3;
  TEST_ASSERT_FALSE(btc_tx_parse(bytes(raw, 6), &tx));
  TEST_ASSERT_FALSE(btc_tx_parse(bytes(NULL, 0), &tx));
  TEST_ASSERT_FALSE(btc_tx_parse(bytes(raw, 4), NULL));
}

/**
 * Appends one legacy input with an empty scriptSig.
 *
 * @param raw destination
 * @param n current length, updated
 */
static void append_empty_input(uint8_t* raw, uint32_t* n) {
  memset(raw + *n, 0, 32);
  *n += 32;
  raw[(*n)++] = 0xff;
  raw[(*n)++] = 0xff;
  raw[(*n)++] = 0xff;
  raw[(*n)++] = 0xff;
  raw[(*n)++] = 0;
  raw[(*n)++] = 0xff;
  raw[(*n)++] = 0xff;
  raw[(*n)++] = 0xff;
  raw[(*n)++] = 0xff;
}

void test_compact_size_and_segwit_shape(void) {
  uint8_t  raw[400];
  uint8_t  txid[32];
  uint8_t  wtxid[32];
  uint32_t n = 0;
  btc_tx_t tx;
  bytes_t  script;
  bytes_t  nonce;

  n        = 0;
  raw[n++] = 1;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 1;
  memset(raw + n, 0, 32);
  n += 32;
  raw[n++] = 0xff;
  raw[n++] = 0xff;
  raw[n++] = 0xff;
  raw[n++] = 0xff;
  raw[n++] = 0xfe;
  raw[n++] = 0x01;
  raw[n++] = 0x00;
  raw[n++] = 0x00;
  raw[n++] = 0x00;
  raw[n++] = 0x61;
  raw[n++] = 0xff;
  raw[n++] = 0xff;
  raw[n++] = 0xff;
  raw[n++] = 0xff;
  raw[n++] = 1;
  memset(raw + n, 0, 8);
  n += 8;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  TEST_ASSERT_FALSE(btc_tx_parse(bytes(raw, n), &tx));

  n        = 0;
  raw[n++] = 1;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 1;
  memset(raw + n, 0, 32);
  n += 32;
  raw[n++] = 0xff;
  raw[n++] = 0xff;
  raw[n++] = 0xff;
  raw[n++] = 0xff;
  raw[n++] = 0xff;
  raw[n++] = 0x01;
  raw[n++] = 0x00;
  raw[n++] = 0x00;
  raw[n++] = 0x00;
  raw[n++] = 0x00;
  raw[n++] = 0x00;
  raw[n++] = 0x00;
  raw[n++] = 0x00;
  raw[n++] = 0x61;
  raw[n++] = 0xff;
  raw[n++] = 0xff;
  raw[n++] = 0xff;
  raw[n++] = 0xff;
  raw[n++] = 1;
  memset(raw + n, 0, 8);
  n += 8;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  TEST_ASSERT_FALSE(btc_tx_parse(bytes(raw, n), &tx));

  n        = 0;
  raw[n++] = 1;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 1;
  memset(raw + n, 0, 32);
  n += 32;
  raw[n++] = 0xff;
  raw[n++] = 0xff;
  raw[n++] = 0xff;
  raw[n++] = 0xff;
  raw[n++] = 0xfd;
  raw[n++] = 0xfd;
  raw[n++] = 0x00;
  memset(raw + n, 0x61, 253);
  n += 253;
  raw[n++] = 0xff;
  raw[n++] = 0xff;
  raw[n++] = 0xff;
  raw[n++] = 0xff;
  raw[n++] = 1;
  memset(raw + n, 0, 8);
  n += 8;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  TEST_ASSERT_TRUE(btc_tx_parse(bytes(raw, n), &tx));
  TEST_ASSERT_TRUE(btc_tx_input(&tx, 0, NULL, NULL, &script, NULL));
  TEST_ASSERT_EQUAL_UINT32(253, script.len);

  // Flag 2 would be a complete segwit transaction if any non-zero flag were accepted.
  n        = 0;
  raw[n++] = 1;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 2;
  raw[n++] = 1;
  append_empty_input(raw, &n);
  raw[n++] = 1;
  memset(raw + n, 0, 8);
  n += 8;
  raw[n++] = 0;
  raw[n++] = 1;
  raw[n++] = 4;
  raw[n++] = 0xde;
  raw[n++] = 0xad;
  raw[n++] = 0xbe;
  raw[n++] = 0xef;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  TEST_ASSERT_FALSE(btc_tx_parse(bytes(raw, n), &tx));

  // The first witness stack may be empty when a later input carries the witness.
  n        = 0;
  raw[n++] = 1;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 1;
  raw[n++] = 2;
  append_empty_input(raw, &n);
  append_empty_input(raw, &n);
  raw[n++] = 1;
  memset(raw + n, 0, 8);
  n += 8;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 1;
  raw[n++] = 4;
  raw[n++] = 0x11;
  raw[n++] = 0x22;
  raw[n++] = 0x33;
  raw[n++] = 0x44;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  TEST_ASSERT_TRUE(btc_tx_parse(bytes(raw, n), &tx));
  TEST_ASSERT_TRUE(tx.segwit);
  TEST_ASSERT_EQUAL_UINT32(2, tx.input_count);
  TEST_ASSERT_TRUE(btc_tx_id(&tx, txid));
  TEST_ASSERT_TRUE(btc_tx_wtxid(&tx, wtxid));
  TEST_ASSERT_FALSE(memcmp(txid, wtxid, 32) == 0);
  TEST_ASSERT_FALSE(btc_tx_coinbase_nonce(&tx, &nonce));

  // A commitment may be followed by an ordinary output. Two witness items are not a nonce.
  n        = 0;
  raw[n++] = 1;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 1;
  raw[n++] = 1;
  append_empty_input(raw, &n);
  raw[n++] = 2;
  memset(raw + n, 0, 8);
  n += 8;
  raw[n++] = 39;
  raw[n++] = 0x6a;
  raw[n++] = 0x24;
  raw[n++] = 0xaa;
  raw[n++] = 0x21;
  raw[n++] = 0xa9;
  raw[n++] = 0xed;
  memset(raw + n, 0x44, 32);
  n += 32;
  raw[n++] = 0x99;
  memset(raw + n, 0, 8);
  n += 8;
  raw[n++] = 0;
  raw[n++] = 2;
  raw[n++] = 32;
  memset(raw + n, 0x33, 32);
  n += 32;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  raw[n++] = 0;
  TEST_ASSERT_TRUE(btc_tx_parse(bytes(raw, n), &tx));
  TEST_ASSERT_TRUE(btc_tx_witness_commitment(&tx, &script));
  TEST_ASSERT_EQUAL_UINT32(32, script.len);
  TEST_ASSERT_EQUAL_UINT8(0x44, script.data[0]);
  TEST_ASSERT_EQUAL_UINT8(0x44, script.data[31]);
  TEST_ASSERT_FALSE(btc_tx_coinbase_nonce(&tx, &nonce));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_target_and_genesis_pow);
  RUN_TEST(test_header_chain_links_parent);
  RUN_TEST(test_hash256_parts_match_contiguous_header);
  RUN_TEST(test_merkle_rejects_duplicate_and_early_match);
  RUN_TEST(test_genesis_transaction_and_bip34);
  RUN_TEST(test_segwit_ids_and_witness_commitment);
  RUN_TEST(test_bip34_script_numbers);
  RUN_TEST(test_parser_rejects_noncanonical_and_empty_witness);
  RUN_TEST(test_compact_size_and_segwit_shape);
  return UNITY_END();
}

#endif
