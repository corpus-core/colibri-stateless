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

#include "btc_client.h"
#include "btc_hash.h"
#include "btc_merkle.h"
#include "btc_proof.h"
#include "btc_target.h"
#include "btc_tx.h"
#include "btc_types.h"
#include "btc_verify.h"
#include "plugin.h"
#include "unity.h"
#include <stdio.h>
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

static bytes_t g_stored;
static char    g_store_key[80];

/**
 * Copies the one blob written by the target-cache storage test.
 *
 * @param key storage key
 * @param buffer destination
 * @return false when the key does not match the stored blob
 */
static bool test_store_get(char* key, buffer_t* buffer) {
  if (!key || strcmp(key, g_store_key) != 0 || !g_stored.data) return false;
  return buffer_append(buffer, g_stored) == g_stored.len;
}

/**
 * Replaces the blob kept by the target-cache storage test.
 *
 * @param key storage key
 * @param value blob to keep
 */
static void test_store_set(char* key, bytes_t value) {
  safe_free(g_stored.data);
  g_stored = bytes(safe_malloc(value.len), value.len);
  memcpy(g_stored.data, value.data, value.len);
  snprintf(g_store_key, sizeof(g_store_key), "%s", key);
}

/** Drops the blob kept by the target-cache storage test. @param key unused */
static void test_store_del(char* key) {
  (void) key;
  safe_free(g_stored.data);
  g_stored = NULL_BYTES;
}

void test_target_retarget_cache_and_jumps(void) {
  uint8_t             limit[32];
  uint8_t             hash[32];
  uint8_t             display[32];
  uint8_t             expect[32];
  uint8_t             easy[80];
  uint8_t             headers[160];
  uint32_t            next  = 0;
  uint32_t            found = 0;
  uint32_t            bits  = 0;
  uint32_t            heights[2];
  const ssz_def_t*    request;
  btc_target_cache_t  cache;
  btc_target_limits_t tight;
  buffer_t            blob = {0};
  storage_plugin_t    saved;
  storage_plugin_t    fake;
  bytes_t             checkpoint = btc_mainnet_checkpoint_header();

  TEST_ASSERT_TRUE(btc_target_from_bits(BTC_MAINNET_POW_BITS, limit));
  TEST_ASSERT_TRUE(btc_target_retarget(0x1d00ffffu, 1231006505u, 1233061996u, limit, &next));
  TEST_ASSERT_EQUAL_HEX32(0x1d00ffffu, next);
  TEST_ASSERT_TRUE(btc_target_retarget(0x1d00ffffu, 1261130161u, 1262152739u, limit, &next));
  TEST_ASSERT_EQUAL_HEX32(0x1d00d86au, next);
  TEST_ASSERT_TRUE(btc_target_retarget(BTC_MAINNET_POW_BITS, 0, BTC_TARGET_TIMESPAN * 8u, limit, &next));
  TEST_ASSERT_EQUAL_HEX32(BTC_MAINNET_POW_BITS, next);

  TEST_ASSERT_EQUAL_UINT32(416, btc_dap(BTC_MAINNET_CHECKPOINT_HEIGHT));
  TEST_ASSERT_TRUE(btc_header_check(checkpoint, hash));
  btc_hash_reverse(hash, display);
  load_hex("0000000000000000000320283a032748cef8227873ff4872689bf23f1cda83a5", expect, sizeof(expect));
  TEST_ASSERT_EQUAL_MEMORY(expect, display, 32);

  btc_target_cache_init(&cache);
  TEST_ASSERT_EQUAL_UINT32(1, cache.count);
  TEST_ASSERT_TRUE(btc_target_authorize(&cache, NULL, BTC_MAINNET_CHECKPOINT_HEIGHT, 0x17034219u));
  TEST_ASSERT_FALSE(btc_target_authorize(&cache, NULL, BTC_MAINNET_CHECKPOINT_HEIGHT, 0x17034218u));
  TEST_ASSERT_TRUE(btc_target_authorize(&cache, NULL, BTC_MAINNET_CHECKPOINT_HEIGHT + BTC_EPOCH_LENGTH, 0x170d0864u));
  TEST_ASSERT_FALSE(btc_target_authorize(&cache, NULL, BTC_MAINNET_CHECKPOINT_HEIGHT + BTC_EPOCH_LENGTH, 0x17104a7du));
  TEST_ASSERT_FALSE(btc_target_authorize(&cache, NULL, BTC_MAINNET_CHECKPOINT_HEIGHT + BTC_EPOCH_LENGTH, 0x1700d086u));
  TEST_ASSERT_TRUE(btc_target_authorize(&cache, NULL, BTC_MAINNET_CHECKPOINT_HEIGHT + 2u * BTC_EPOCH_LENGTH, 0x17039581u));
  TEST_ASSERT_FALSE(btc_target_authorize(&cache, NULL, BTC_MAINNET_CHECKPOINT_HEIGHT + 2u * BTC_EPOCH_LENGTH, 0x17039dd9u));
  TEST_ASSERT_TRUE(btc_target_authorize(&cache, NULL, BTC_MAINNET_CHECKPOINT_HEIGHT + 2u * BTC_EPOCH_LENGTH, 0x1700d086u));
  TEST_ASSERT_TRUE(btc_target_authorize(&cache, NULL, BTC_MAINNET_CHECKPOINT_HEIGHT + 20u * BTC_EPOCH_LENGTH, 0x17034219u));
  TEST_ASSERT_FALSE(btc_target_authorize(&cache, NULL, BTC_MAINNET_CHECKPOINT_HEIGHT + 21u * BTC_EPOCH_LENGTH, 0x17034219u));

  TEST_ASSERT_TRUE(btc_target_accept_header(&cache, NULL, BTC_MAINNET_CHECKPOINT_HEIGHT, checkpoint));
  TEST_ASSERT_FALSE(btc_target_accept_header(&cache, NULL, BTC_MAINNET_CHECKPOINT_HEIGHT + BTC_EPOCH_LENGTH, checkpoint));
  fill_easy_header(easy, NULL);
  TEST_ASSERT_FALSE(btc_target_accept_header(&cache, NULL, BTC_MAINNET_CHECKPOINT_HEIGHT + BTC_EPOCH_LENGTH, bytes(easy, 80)));

  tight.max_daps    = 20;
  tight.max_percent = 10;
  heights[0]        = (416u + 20u) * BTC_EPOCH_LENGTH;
  heights[1]        = (416u + 10u) * BTC_EPOCH_LENGTH;
  memcpy(headers, checkpoint.data, 80);
  memcpy(headers + 80, checkpoint.data, 80);
  TEST_ASSERT_FALSE(btc_target_accept_path(&cache, &tight, heights, bytes(headers, 160), 2));
  TEST_ASSERT_EQUAL_UINT32(1, cache.count);

  heights[0] = (416u + 20u) * BTC_EPOCH_LENGTH;
  heights[1] = (416u + 30u) * BTC_EPOCH_LENGTH;
  TEST_ASSERT_TRUE(btc_target_authorize(&cache, &tight, heights[0], 0x17034219u));
  TEST_ASSERT_TRUE(btc_target_cache_put(&cache, btc_dap(heights[0]), 0x17034219u));
  TEST_ASSERT_TRUE(btc_target_authorize(&cache, &tight, heights[1], 0x17034219u));
  TEST_ASSERT_TRUE(btc_target_cache_put(&cache, btc_dap(heights[1]), 0x17034219u));
  TEST_ASSERT_TRUE(btc_target_cache_find(&cache, btc_dap(heights[1]), &bits, &found));
  TEST_ASSERT_EQUAL_UINT32(btc_dap(heights[1]), found);
  TEST_ASSERT_EQUAL_HEX32(0x17034219u, bits);

  TEST_ASSERT_TRUE(btc_target_cache_encode(&cache, &blob));
  TEST_ASSERT_TRUE(btc_target_cache_decode(blob.data, &cache));
  blob.data.data[12] ^= 0x01;
  TEST_ASSERT_FALSE(btc_target_cache_decode(blob.data, &cache));
  TEST_ASSERT_EQUAL_HEX32(0x17034219u, cache.entries[0].bits);

  memset(&saved, 0, sizeof(saved));
  c4_get_storage_config(&saved);
  memset(&fake, 0, sizeof(fake));
  fake.get             = test_store_get;
  fake.set             = test_store_set;
  fake.del             = test_store_del;
  fake.max_sync_states = 1;
  c4_set_storage_config(&fake);
  btc_target_cache_init(&cache);
  TEST_ASSERT_TRUE(btc_target_cache_store(C4_CHAIN_BTC_MAINNET, &cache));
  TEST_ASSERT_TRUE(btc_target_cache_load(C4_CHAIN_BTC_MAINNET, &cache));
  TEST_ASSERT_EQUAL_UINT32(1, cache.count);
  g_stored.data[12] ^= 0x01;
  TEST_ASSERT_FALSE(btc_target_cache_load(C4_CHAIN_BTC_MAINNET, &cache));
  TEST_ASSERT_EQUAL_UINT32(416, cache.entries[0].dap);
  TEST_ASSERT_EQUAL_HEX32(0x17034219u, cache.entries[0].bits);
  c4_set_storage_config(&saved);
  safe_free(g_stored.data);
  g_stored = NULL_BYTES;
  buffer_free(&blob);

  request = btc_ssz_verification_type(BTC_SSZ_VERIFY_REQUEST);
  TEST_ASSERT_NOT_NULL(request);
  TEST_ASSERT_EQUAL_UINT32(4, request->def.container.len);
  TEST_ASSERT_EQUAL_INT(SSZ_TYPE_UNION, request->def.container.elements[2].type);
  TEST_ASSERT_EQUAL_UINT32(2, request->def.container.elements[2].def.container.len);
  TEST_ASSERT_EQUAL_STRING("NONE", request->def.container.elements[2].def.container.elements[0].name);
  TEST_ASSERT_EQUAL_STRING("BtcHeaderProof", request->def.container.elements[2].def.container.elements[1].name);
  TEST_ASSERT_EQUAL_INT(SSZ_TYPE_UNION, request->def.container.elements[3].type);
  TEST_ASSERT_EQUAL_UINT32(2, request->def.container.elements[3].def.container.len);
  TEST_ASSERT_EQUAL_STRING("BtcEpochPath", request->def.container.elements[3].def.container.elements[1].name);
}

/**
 * Appends a little-endian uint32 to a growable buffer.
 *
 * @param buf destination
 * @param value integer to append
 */
static void append_le32(buffer_t* buf, uint32_t value) {
  uint8_t raw[4];
  raw[0] = (uint8_t) value;
  raw[1] = (uint8_t) (value >> 8);
  raw[2] = (uint8_t) (value >> 16);
  raw[3] = (uint8_t) (value >> 24);
  TEST_ASSERT_EQUAL_UINT32(4, buffer_append(buf, bytes(raw, 4)));
}

/**
 * Replaces `buf` with a target-cache blob.
 *
 * The layout matches `btc_target_cache_encode`: 4-byte magic, little-endian count, then
 * `count` little-endian `dap`/`bits` pairs.
 *
 * @param buf destination
 * @param magic four magic bytes
 * @param daps epoch numbers, unused when `count` is 0
 * @param bits compact targets, unused when `count` is 0
 * @param count number of pairs
 */
static void write_cache_blob(buffer_t* buf, const uint8_t magic[4], const uint32_t* daps, const uint32_t* bits, uint32_t count) {
  uint32_t i;
  buffer_reset(buf);
  TEST_ASSERT_EQUAL_UINT32(4, buffer_append(buf, bytes((uint8_t*) magic, 4)));
  append_le32(buf, count);
  for (i = 0; i < count; i++) {
    append_le32(buf, daps[i]);
    append_le32(buf, bits[i]);
  }
}

/**
 * Seeds a cache whose checkpoint bits differ from the compiled header.
 *
 * Epoch 10 carries `0x01020304`. The checkpoint epoch keeps dap 416 but its bits are
 * `0x11111111`, so a later successful write is visible. The checkpoint bits are written
 * directly because `btc_target_cache_put` rejects any other value for that epoch.
 *
 * @param cache receives the sentinel
 */
static void seed_sentinel_cache(btc_target_cache_t* cache) {
  btc_target_cache_init(cache);
  TEST_ASSERT_TRUE(btc_target_cache_put(cache, 10, 0x01020304u));
  cache->entries[1].bits = 0x11111111u;
}

/**
 * Checks that a rejected operation left the sentinel cache untouched.
 *
 * @param cache cache under test
 */
static void assert_sentinel_cache(const btc_target_cache_t* cache) {
  TEST_ASSERT_EQUAL_UINT32(2, cache->count);
  TEST_ASSERT_EQUAL_UINT32(10, cache->entries[0].dap);
  TEST_ASSERT_EQUAL_HEX32(0x01020304u, cache->entries[0].bits);
  TEST_ASSERT_EQUAL_UINT32(btc_dap(BTC_MAINNET_CHECKPOINT_HEIGHT), cache->entries[1].dap);
  TEST_ASSERT_EQUAL_HEX32(0x11111111u, cache->entries[1].bits);
}

void test_target_retarget_clamps_negative_span(void) {
  uint8_t  limit[32];
  uint32_t next = 0x11111111u;

  TEST_ASSERT_TRUE(btc_target_from_bits(BTC_MAINNET_POW_BITS, limit));
  TEST_ASSERT_FALSE(btc_target_retarget(BTC_MAINNET_POW_BITS, 0, BTC_TARGET_TIMESPAN, NULL, &next));
  TEST_ASSERT_EQUAL_HEX32(0x11111111u, next);
  TEST_ASSERT_FALSE(btc_target_retarget(BTC_MAINNET_POW_BITS, 0, BTC_TARGET_TIMESPAN, limit, NULL));
  TEST_ASSERT_FALSE(btc_target_retarget(0, 0, BTC_TARGET_TIMESPAN, limit, &next));
  TEST_ASSERT_EQUAL_HEX32(0x11111111u, next);

  // pow-limit / 4. 0xffff<<208 divided by 4 is 0xffff<<206, compact 0x1c3fffc0.
  TEST_ASSERT_TRUE(btc_target_retarget(BTC_MAINNET_POW_BITS, 0, BTC_TARGET_TIMESPAN / 4u, limit, &next));
  TEST_ASSERT_EQUAL_HEX32(0x1c3fffc0u, next);
  TEST_ASSERT_TRUE(btc_target_retarget(BTC_MAINNET_POW_BITS, 0, BTC_TARGET_TIMESPAN / 4u - 1u, limit, &next));
  TEST_ASSERT_EQUAL_HEX32(0x1c3fffc0u, next);
  TEST_ASSERT_TRUE_MESSAGE(btc_target_retarget(BTC_MAINNET_POW_BITS, 1000u, 999u, limit, &next), "negative span");
  TEST_ASSERT_EQUAL_HEX32(0x1c3fffc0u, next);
  TEST_ASSERT_TRUE(btc_target_retarget(BTC_MAINNET_POW_BITS, 0xffffffffu, 0, limit, &next));
  TEST_ASSERT_EQUAL_HEX32(0x1c3fffc0u, next);
}

void test_target_cache_decode_rejects_malformed(void) {
  btc_target_cache_t cache;
  buffer_t           blob     = {0};
  uint8_t            magic[4] = {'B', 'T', 'C', '1'};
  uint32_t           daps[4];
  uint32_t           bits[4];
  uint32_t           checkpoint = btc_dap(BTC_MAINNET_CHECKPOINT_HEIGHT);

  daps[0] = 10;
  bits[0] = 0x01020304u;
  daps[1] = checkpoint;
  bits[1] = 0x17034219u;
  write_cache_blob(&blob, magic, daps, bits, 2);
  TEST_ASSERT_TRUE(btc_target_cache_decode(blob.data, &cache));
  TEST_ASSERT_EQUAL_UINT32(2, cache.count);
  TEST_ASSERT_EQUAL_UINT32(10, cache.entries[0].dap);
  TEST_ASSERT_EQUAL_HEX32(0x01020304u, cache.entries[0].bits);
  TEST_ASSERT_EQUAL_UINT32(checkpoint, cache.entries[1].dap);
  TEST_ASSERT_EQUAL_HEX32(0x17034219u, cache.entries[1].bits);

  seed_sentinel_cache(&cache);

  magic[0] = 'X';
  write_cache_blob(&blob, magic, daps, bits, 2);
  TEST_ASSERT_FALSE_MESSAGE(btc_target_cache_decode(blob.data, &cache), "bad magic");
  assert_sentinel_cache(&cache);
  magic[0] = 'B';

  daps[0] = checkpoint;
  bits[0] = 0x17034219u;
  daps[1] = checkpoint;
  bits[1] = 0x17034219u;
  write_cache_blob(&blob, magic, daps, bits, 2);
  TEST_ASSERT_FALSE_MESSAGE(btc_target_cache_decode(blob.data, &cache), "duplicate checkpoint");
  assert_sentinel_cache(&cache);

  daps[0] = 10;
  bits[0] = 0x1d00ffffu;
  daps[1] = checkpoint;
  bits[1] = 0x17034219u;
  daps[2] = 500;
  bits[2] = 0x1d00ffffu;
  daps[3] = 500;
  bits[3] = 0x1d00ffffu;
  write_cache_blob(&blob, magic, daps, bits, 4);
  TEST_ASSERT_FALSE_MESSAGE(btc_target_cache_decode(blob.data, &cache), "duplicate epoch");
  assert_sentinel_cache(&cache);

  daps[0] = checkpoint + 1u;
  bits[0] = 0x1d00ffffu;
  daps[1] = checkpoint;
  bits[1] = 0x17034219u;
  write_cache_blob(&blob, magic, daps, bits, 2);
  TEST_ASSERT_FALSE_MESSAGE(btc_target_cache_decode(blob.data, &cache), "decreasing daps");
  assert_sentinel_cache(&cache);

  daps[0] = 10;
  bits[0] = 0x1d00ffffu;
  daps[1] = 11;
  bits[1] = 0x1d00ffffu;
  write_cache_blob(&blob, magic, daps, bits, 2);
  TEST_ASSERT_FALSE_MESSAGE(btc_target_cache_decode(blob.data, &cache), "missing checkpoint");
  assert_sentinel_cache(&cache);

  daps[0] = checkpoint;
  bits[0] = 0x17034219u;
  write_cache_blob(&blob, magic, daps, bits, 1);
  append_le32(&blob, 0);
  append_le32(&blob, 0);
  TEST_ASSERT_FALSE_MESSAGE(btc_target_cache_decode(blob.data, &cache), "trailing bytes");
  assert_sentinel_cache(&cache);

  write_cache_blob(&blob, magic, daps, bits, 1);
  blob.data.data[4] = 2;
  TEST_ASSERT_FALSE_MESSAGE(btc_target_cache_decode(blob.data, &cache), "count exceeds blob");
  assert_sentinel_cache(&cache);

  write_cache_blob(&blob, magic, NULL, NULL, 0);
  TEST_ASSERT_FALSE_MESSAGE(btc_target_cache_decode(blob.data, &cache), "count 0");
  assert_sentinel_cache(&cache);

  TEST_ASSERT_FALSE(btc_target_cache_decode(bytes(magic, 4), &cache));
  assert_sentinel_cache(&cache);
  TEST_ASSERT_FALSE(btc_target_cache_decode(NULL_BYTES, &cache));
  assert_sentinel_cache(&cache);
  TEST_ASSERT_FALSE(btc_target_cache_decode(blob.data, NULL));
  buffer_free(&blob);
}

void test_target_accept_path_count_and_checkpoint(void) {
  btc_target_cache_t cache;
  uint8_t            headers[160];
  uint32_t           heights[2];
  uint32_t           one;
  bytes_t            checkpoint = btc_mainnet_checkpoint_header();

  seed_sentinel_cache(&cache);
  one = BTC_MAINNET_CHECKPOINT_HEIGHT;

  TEST_ASSERT_FALSE_MESSAGE(btc_target_accept_path(&cache, NULL, &one, checkpoint, 0), "count 0");
  assert_sentinel_cache(&cache);
  TEST_ASSERT_FALSE_MESSAGE(btc_target_accept_path(&cache, NULL, &one, checkpoint, BTC_TARGET_PATH_LIMIT + 1u), "count 41");
  assert_sentinel_cache(&cache);
  TEST_ASSERT_FALSE_MESSAGE(btc_target_accept_path(&cache, NULL, NULL, checkpoint, 1), "null heights");
  assert_sentinel_cache(&cache);
  TEST_ASSERT_FALSE_MESSAGE(btc_target_accept_path(&cache, NULL, &one, bytes(checkpoint.data, BTC_HEADER_SIZE - 1u), 1), "short header");
  assert_sentinel_cache(&cache);

  heights[0] = BTC_MAINNET_CHECKPOINT_HEIGHT;
  heights[1] = BTC_MAINNET_CHECKPOINT_HEIGHT;
  memcpy(headers, checkpoint.data, BTC_HEADER_SIZE);
  memcpy(headers + BTC_HEADER_SIZE, checkpoint.data, BTC_HEADER_SIZE);
  TEST_ASSERT_FALSE_MESSAGE(btc_target_accept_path(&cache, NULL, heights, bytes(headers, sizeof(headers)), 2), "same epoch");
  assert_sentinel_cache(&cache);

  TEST_ASSERT_TRUE(btc_target_accept_path(&cache, NULL, &one, checkpoint, 1));
  TEST_ASSERT_EQUAL_UINT32(2, cache.count);
  TEST_ASSERT_EQUAL_UINT32(10, cache.entries[0].dap);
  TEST_ASSERT_EQUAL_HEX32(0x01020304u, cache.entries[0].bits);
  TEST_ASSERT_EQUAL_UINT32(btc_dap(BTC_MAINNET_CHECKPOINT_HEIGHT), cache.entries[1].dap);
  TEST_ASSERT_EQUAL_HEX32(0x17034219u, cache.entries[1].bits);
}

void test_target_authorize_rejects_huge_percent(void) {
  btc_target_cache_t  cache;
  btc_target_limits_t limits;

  btc_target_limits_default(NULL);
  memset(&limits, 0x5a, sizeof(limits));
  btc_target_limits_default(&limits);
  TEST_ASSERT_EQUAL_UINT32(BTC_TARGET_MAX_DAPS, limits.max_daps);
  TEST_ASSERT_EQUAL_UINT32(BTC_TARGET_MAX_PERCENT, limits.max_percent);

  btc_target_cache_init(&cache);
  limits.max_daps    = BTC_TARGET_MAX_DAPS;
  limits.max_percent = 1000000u;
  TEST_ASSERT_TRUE(btc_target_authorize(&cache, &limits, BTC_MAINNET_CHECKPOINT_HEIGHT, 0x17034219u));
  limits.max_percent = 1000001u;
  TEST_ASSERT_FALSE(btc_target_authorize(&cache, &limits, BTC_MAINNET_CHECKPOINT_HEIGHT, 0x17034219u));
  TEST_ASSERT_EQUAL_UINT32(1, cache.count);
  TEST_ASSERT_EQUAL_HEX32(0x17034219u, cache.entries[0].bits);
}

void test_target_cache_capacity_and_find(void) {
  btc_target_cache_t cache;
  uint32_t           bits  = 0xabcdefu;
  uint32_t           found = 9;
  uint32_t           dap;
  uint32_t           i;
  uint32_t           checkpoint = btc_dap(BTC_MAINNET_CHECKPOINT_HEIGHT);

  TEST_ASSERT_FALSE(btc_target_cache_find(NULL, checkpoint, &bits, &found));
  TEST_ASSERT_EQUAL_HEX32(0xabcdefu, bits);
  TEST_ASSERT_EQUAL_UINT32(9, found);
  TEST_ASSERT_FALSE(btc_target_cache_find(NULL, checkpoint, NULL, NULL));
  TEST_ASSERT_FALSE(btc_target_cache_put(NULL, 1, 1));

  btc_target_cache_init(&cache);
  TEST_ASSERT_TRUE(btc_target_cache_put(&cache, checkpoint + 4u, 0x22222222u));
  TEST_ASSERT_TRUE(btc_target_cache_find(&cache, checkpoint + 3u, &bits, &found));
  TEST_ASSERT_EQUAL_UINT32(checkpoint + 4u, found);
  TEST_ASSERT_EQUAL_HEX32(0x22222222u, bits);
  TEST_ASSERT_TRUE(btc_target_cache_find(&cache, checkpoint + 2u, &bits, &found));
  TEST_ASSERT_EQUAL_UINT32(checkpoint, found);
  TEST_ASSERT_EQUAL_HEX32(0x17034219u, bits);
  TEST_ASSERT_FALSE(btc_target_cache_put(&cache, checkpoint, 0x1d00ffffu));
  TEST_ASSERT_EQUAL_HEX32(0x17034219u, cache.entries[0].bits);

  btc_target_cache_init(&cache);
  TEST_ASSERT_TRUE(btc_target_cache_put(&cache, checkpoint + 4u, 0x1700d086u));
  TEST_ASSERT_TRUE(btc_target_cache_find(&cache, checkpoint + 2u, &bits, &found));
  TEST_ASSERT_EQUAL_UINT32(checkpoint + 4u, found);
  TEST_ASSERT_EQUAL_HEX32(0x1700d086u, bits);

  btc_target_cache_init(&cache);
  for (dap = 0; dap < BTC_TARGET_CACHE_MAX; dap++) {
    if (dap == checkpoint) continue;
    TEST_ASSERT_TRUE(btc_target_cache_put(&cache, dap, 0x1d00ffffu));
  }
  TEST_ASSERT_EQUAL_UINT32(BTC_TARGET_CACHE_MAX, cache.count);
  for (i = 1; i < cache.count; i++)
    TEST_ASSERT_TRUE(cache.entries[i].dap > cache.entries[i - 1].dap);
  TEST_ASSERT_FALSE(btc_target_cache_put(&cache, BTC_TARGET_CACHE_MAX, 0x1d00ffffu));
  TEST_ASSERT_EQUAL_UINT32(BTC_TARGET_CACHE_MAX, cache.count);
  for (i = 0; i < cache.count; i++)
    TEST_ASSERT_TRUE(cache.entries[i].dap != BTC_TARGET_CACHE_MAX);
  TEST_ASSERT_TRUE(btc_target_cache_put(&cache, 0, 0x33333333u));
  TEST_ASSERT_EQUAL_UINT32(BTC_TARGET_CACHE_MAX, cache.count);
  TEST_ASSERT_TRUE(btc_target_cache_find(&cache, 0, &bits, &found));
  TEST_ASSERT_EQUAL_UINT32(0, found);
  TEST_ASSERT_EQUAL_HEX32(0x33333333u, bits);
  TEST_ASSERT_TRUE(btc_target_cache_find(&cache, checkpoint, &bits, &found));
  TEST_ASSERT_EQUAL_UINT32(checkpoint, found);
  TEST_ASSERT_EQUAL_HEX32(0x17034219u, bits);
}

void test_epoch_path_ssz_shape(void) {
  const ssz_def_t* request = btc_ssz_verification_type(BTC_SSZ_VERIFY_REQUEST);
  const ssz_def_t* proof;
  const ssz_def_t* sync_data;
  const ssz_def_t* path;
  const ssz_def_t* steps;
  const ssz_def_t* step;
  const ssz_def_t* finals;

  TEST_ASSERT_NULL(btc_ssz_verification_type((btc_ssz_type_t) 0));
  TEST_ASSERT_NOT_NULL(request);
  proof = &request->def.container.elements[2];
  TEST_ASSERT_EQUAL_STRING("proof", proof->name);
  TEST_ASSERT_EQUAL_INT(SSZ_TYPE_UNION, proof->type);
  TEST_ASSERT_EQUAL_UINT32(2, proof->def.container.len);
  TEST_ASSERT_EQUAL_STRING("NONE", proof->def.container.elements[0].name);
  TEST_ASSERT_EQUAL_STRING("BtcHeaderProof", proof->def.container.elements[1].name);
  TEST_ASSERT_EQUAL_STRING("BtcHeader", request->def.container.elements[1].def.container.elements[1].name);
  sync_data = &request->def.container.elements[3];
  TEST_ASSERT_EQUAL_STRING("sync_data", sync_data->name);
  TEST_ASSERT_EQUAL_INT(SSZ_TYPE_UNION, sync_data->type);
  TEST_ASSERT_EQUAL_UINT32(2, sync_data->def.container.len);
  path = &sync_data->def.container.elements[1];
  TEST_ASSERT_EQUAL_STRING("BtcEpochPath", path->name);
  TEST_ASSERT_EQUAL_UINT32(1, path->def.container.len);
  steps = &path->def.container.elements[0];
  TEST_ASSERT_EQUAL_STRING("steps", steps->name);
  TEST_ASSERT_EQUAL_INT(SSZ_TYPE_LIST, steps->type);
  TEST_ASSERT_EQUAL_UINT32(BTC_TARGET_PATH_LIMIT, steps->def.vector.len);
  step = steps->def.vector.type;
  TEST_ASSERT_NOT_NULL(step);
  TEST_ASSERT_EQUAL_STRING("BtcEpochStep", step->name);
  TEST_ASSERT_EQUAL_UINT32(3, step->def.container.len);
  TEST_ASSERT_EQUAL_STRING("height", step->def.container.elements[0].name);
  TEST_ASSERT_EQUAL_INT(SSZ_TYPE_UINT, step->def.container.elements[0].type);
  TEST_ASSERT_EQUAL_UINT32(4, step->def.container.elements[0].def.uint.len);
  TEST_ASSERT_EQUAL_STRING("header", step->def.container.elements[1].name);
  TEST_ASSERT_EQUAL_INT(SSZ_TYPE_VECTOR, step->def.container.elements[1].type);
  TEST_ASSERT_EQUAL_UINT32(BTC_HEADER_SIZE, step->def.container.elements[1].def.vector.len);
  finals = &step->def.container.elements[2];
  TEST_ASSERT_EQUAL_STRING("final_headers", finals->name);
  TEST_ASSERT_EQUAL_INT(SSZ_TYPE_LIST, finals->type);
  TEST_ASSERT_EQUAL_UINT32(64, finals->def.vector.len);
  TEST_ASSERT_NOT_NULL(finals->def.vector.type);
  TEST_ASSERT_EQUAL_STRING("header", finals->def.vector.type->name);
  TEST_ASSERT_EQUAL_UINT32(BTC_HEADER_SIZE, finals->def.vector.type->def.vector.len);
}

/**
 * Builds one `BtcEpochPath` step and appends it to `steps`.
 *
 * @param steps list builder for `BtcEpochStep`
 * @param index element index, used as the SSZ offset base
 * @param height claimed block height
 * @param header 80-byte header
 * @param final_header optional second header, or NULL
 */
static void add_epoch_step(ssz_builder_t* steps, uint32_t index, uint32_t height, bytes_t header, const uint8_t* final_header) {
  const ssz_def_t* step_def = steps->def->def.vector.type;
  ssz_builder_t    step     = ssz_builder_for_def(step_def);
  ssz_builder_t    finals   = ssz_builder_for_def(&step_def->def.container.elements[2]);
  ssz_add_uint32(&step, height);
  ssz_add_bytes(&step, "header", header);
  if (final_header)
    ssz_add_dynamic_list_bytes(&finals, 0, bytes((uint8_t*) final_header, BTC_HEADER_SIZE));
  ssz_add_builders(&step, "final_headers", finals);
  ssz_add_dynamic_list_builders(steps, (int) index, step);
}

void test_client_state_and_epoch_path(void) {
  btc_client_state_t state;
  btc_client_state_t loaded;
  btc_target_cache_t cache;
  buffer_t           blob = {0};
  storage_plugin_t   saved;
  storage_plugin_t   fake;
  const ssz_def_t*   request;
  const ssz_def_t*   sync_def;
  const ssz_def_t*   path_def;
  ssz_builder_t      steps;
  ssz_builder_t      path_builder;
  ssz_ob_t           path;
  ssz_ob_t           sync_data;
  verify_ctx_t       ctx;
  uint8_t            hash[32];
  bytes_t            checkpoint = btc_mainnet_checkpoint_header();
  uint8_t            genesis[80];

  btc_client_state_set_checkpoint(&state);
  TEST_ASSERT_EQUAL_UINT32(BTC_MAINNET_CHECKPOINT_HEIGHT, state.height);
  TEST_ASSERT_TRUE(btc_header_check(checkpoint, hash));
  TEST_ASSERT_EQUAL_MEMORY(hash, state.hash, 32);
  TEST_ASSERT_TRUE(btc_client_state_encode(&state, &blob));
  TEST_ASSERT_EQUAL_UINT32(BTC_CLIENT_STATE_SIZE, blob.data.len);
  TEST_ASSERT_EQUAL_UINT8(BTC_CLIENT_STATE_TAG, blob.data.data[0]);
  memset(&loaded, 0, sizeof(loaded));
  TEST_ASSERT_TRUE(btc_client_state_decode(blob.data, &loaded));
  TEST_ASSERT_EQUAL_UINT32(state.height, loaded.height);
  TEST_ASSERT_EQUAL_MEMORY(state.hash, loaded.hash, 32);

  blob.data.data[5] ^= 0x01;
  TEST_ASSERT_FALSE(btc_client_state_decode(blob.data, &loaded));
  TEST_ASSERT_EQUAL_MEMORY(state.hash, loaded.hash, 32);
  blob.data.data[0] = 0x01;
  blob.data.data[5] ^= 0x01;
  TEST_ASSERT_FALSE(btc_client_state_decode(blob.data, &loaded));

  state.height = BTC_MAINNET_CHECKPOINT_HEIGHT + BTC_EPOCH_LENGTH;
  TEST_ASSERT_TRUE(btc_client_state_encode(&state, &blob));
  memset(&saved, 0, sizeof(saved));
  c4_get_storage_config(&saved);
  memset(&fake, 0, sizeof(fake));
  fake.get             = test_store_get;
  fake.set             = test_store_set;
  fake.del             = test_store_del;
  fake.max_sync_states = 1;
  c4_set_storage_config(&fake);
  TEST_ASSERT_FALSE(btc_client_state_load(C4_CHAIN_BTC_MAINNET, &loaded));
  TEST_ASSERT_EQUAL_UINT32(BTC_MAINNET_CHECKPOINT_HEIGHT, loaded.height);
  TEST_ASSERT_TRUE(btc_client_state_store(C4_CHAIN_BTC_MAINNET, &state));
  TEST_ASSERT_TRUE(strncmp(g_store_key, "states_", 7) == 0);
  TEST_ASSERT_TRUE(btc_client_state_load(C4_CHAIN_BTC_MAINNET, &loaded));
  TEST_ASSERT_EQUAL_UINT32(state.height, loaded.height);
  TEST_ASSERT_EQUAL_MEMORY(state.hash, loaded.hash, 32);
  g_stored.data[0] = 0x00;
  TEST_ASSERT_FALSE(btc_client_state_load(C4_CHAIN_BTC_MAINNET, &loaded));
  TEST_ASSERT_EQUAL_UINT32(BTC_MAINNET_CHECKPOINT_HEIGHT, loaded.height);
  c4_set_storage_config(&saved);
  safe_free(g_stored.data);
  g_stored = NULL_BYTES;
  buffer_free(&blob);

  request   = btc_ssz_verification_type(BTC_SSZ_VERIFY_REQUEST);
  sync_def  = &request->def.container.elements[3];
  path_def  = &sync_def->def.container.elements[1];
  sync_data = (ssz_ob_t) {.def = &sync_def->def.container.elements[0], .bytes = NULL_BYTES};
  btc_target_cache_init(&cache);
  TEST_ASSERT_TRUE(btc_sync_accept_epoch_path(sync_data, &cache, NULL));
  TEST_ASSERT_EQUAL_UINT32(1, cache.count);

  steps = ssz_builder_for_def(&path_def->def.container.elements[0]);
  add_epoch_step(&steps, 0, BTC_MAINNET_CHECKPOINT_HEIGHT, checkpoint, NULL);
  ssz_builder_fix_list_offsets(&steps, 1);
  path_builder = ssz_builder_for_def(path_def);
  ssz_add_builders(&path_builder, "steps", steps);
  path = ssz_builder_to_bytes(&path_builder);
  TEST_ASSERT_TRUE(btc_sync_accept_epoch_path(path, &cache, NULL));
  TEST_ASSERT_EQUAL_UINT32(1, cache.count);
  TEST_ASSERT_EQUAL_HEX32(0x17034219u, cache.entries[0].bits);

  memset(&saved, 0, sizeof(saved));
  c4_get_storage_config(&saved);
  memset(&fake, 0, sizeof(fake));
  fake.get             = test_store_get;
  fake.set             = test_store_set;
  fake.del             = test_store_del;
  fake.max_sync_states = 1;
  c4_set_storage_config(&fake);
  memset(&ctx, 0, sizeof(ctx));
  ctx.chain_id  = C4_CHAIN_BTC_MAINNET;
  ctx.sync_data = path;
  TEST_ASSERT_TRUE(c4_btc_verify(&ctx));
  TEST_ASSERT_TRUE(ctx.success);
  TEST_ASSERT_NULL(ctx.state.error);
  TEST_ASSERT_TRUE(strncmp(g_store_key, "btc_target_", 11) == 0);
  TEST_ASSERT_TRUE(btc_target_cache_load(C4_CHAIN_BTC_MAINNET, &cache));
  TEST_ASSERT_EQUAL_HEX32(0x17034219u, cache.entries[0].bits);
  safe_free(path.bytes.data);

  load_hex("0100000000000000000000000000000000000000000000000000000000000000000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a29ab5f49ffff001d1dac2b7c", genesis, sizeof(genesis));
  steps = ssz_builder_for_def(&path_def->def.container.elements[0]);
  add_epoch_step(&steps, 0, BTC_MAINNET_CHECKPOINT_HEIGHT, checkpoint, genesis);
  ssz_builder_fix_list_offsets(&steps, 1);
  path_builder = ssz_builder_for_def(path_def);
  ssz_add_builders(&path_builder, "steps", steps);
  path = ssz_builder_to_bytes(&path_builder);
  TEST_ASSERT_FALSE(btc_sync_accept_epoch_path(path, &cache, NULL));
  TEST_ASSERT_EQUAL_UINT32(1, cache.count);
  memset(&ctx, 0, sizeof(ctx));
  ctx.chain_id  = C4_CHAIN_BTC_MAINNET;
  ctx.sync_data = path;
  TEST_ASSERT_TRUE(c4_btc_verify(&ctx));
  TEST_ASSERT_FALSE(ctx.success);
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  safe_free(ctx.state.error);
  TEST_ASSERT_TRUE(btc_target_cache_load(C4_CHAIN_BTC_MAINNET, &cache));
  TEST_ASSERT_EQUAL_HEX32(0x17034219u, cache.entries[0].bits);
  c4_set_storage_config(&saved);
  safe_free(g_stored.data);
  g_stored = NULL_BYTES;
  safe_free(path.bytes.data);
}

/**
 * Builds a checkpoint header proof with `nfinal` following headers.
 *
 * @param proof_def `BtcHeaderProof` definition
 * @param header checkpoint header
 * @param finals following headers, 80 bytes each
 * @param nfinal number of following headers
 * @param height claimed block height
 * @param tx_count transaction count committed by the coinbase merkle proof
 * @param siblings internal-order merkle siblings, 32 bytes each
 * @param nsibling number of siblings
 * @param coinbase coinbase transaction, or an empty slice when the checkpoint hash fixes the height
 * @return the serialized proof; the caller frees `bytes.data`
 */
static ssz_ob_t build_checkpoint_proof(const ssz_def_t* proof_def, bytes_t header, const uint8_t finals[][80], uint32_t nfinal, uint32_t height, uint32_t tx_count, const uint8_t siblings[][32], uint32_t nsibling, bytes_t coinbase) {
  ssz_builder_t proof  = ssz_builder_for_def(proof_def);
  ssz_builder_t list   = ssz_builder_for_def(&proof_def->def.container.elements[2]);
  ssz_builder_t merkle = ssz_builder_for_def(&proof_def->def.container.elements[4]);
  ssz_builder_t coin   = ssz_builder_for_def(&proof_def->def.container.elements[5]);
  uint32_t      i;
  ssz_add_uint32(&proof, height);
  ssz_add_bytes(&proof, "header", header);
  for (i = 0; i < nfinal; i++)
    ssz_add_dynamic_list_bytes(&list, 0, bytes((uint8_t*) finals[i], BTC_HEADER_SIZE));
  ssz_add_builders(&proof, "final_headers", list);
  ssz_add_uint32(&proof, tx_count);
  for (i = 0; i < nsibling; i++)
    ssz_add_dynamic_list_bytes(&merkle, 0, bytes((uint8_t*) siblings[i], 32));
  ssz_add_builders(&proof, "merkle", merkle);
  buffer_append(&coin.fixed, coinbase);
  ssz_add_builders(&proof, "coinbase", coin);
  return ssz_builder_to_bytes(&proof);
}

void test_header_proof_finality(void) {
  static const char* final_hex[6] = {
      "04002020a583da1c3ff29b687248ff737822f8ce4827033a282003000000000000000000bcc07f8618b7b063f833100724e2b40d6ee9dfa78087bfbe5d3441058a63de380e082366194203176d9026cc",
      "00207e2f6324db68f7433cc1087eeb41f4f3137907a3d5758ab4010000000000000000009ebac463e21126b1fb9789de7737f0b1978baec71cd54c46b20ea32480f8117184092366194203177cd0b8a5",
      "0000002aa9a07587b54fcc25e10c5b60cee13efb566b6273ccc0020000000000000000009b1720b434d104f11e807d5e009c006ec8d74be2a0c3bb63765ac8ca99e9ae2d190b236619420317cb26b50c",
      "00404e2819b1349be0faff65e300e9c4d2defe9d26b91c67e8cf010000000000000000008a97b043cdc09695a5984b4382e3a9cbbeeacf88ea188430155797cca3801927520d2366194203172a04eaf5",
      "00a00020367d271bb55cdd319e34cefd17d8578d451f4b27588402000000000000000000be471b97946eb1d8cb4f004782a8378e448757796f0a6e0215afd087767ff4ca961323661942031723406c85",
      "00e0ff2f79d012bca116f2026697dde719cd18d0adace3c00e7b02000000000000000000c7df6d4ef5265e273c2b2d7548096b7d7c620dd03fea913b9afcba2cf227c7ae6b15236619420317fd2d68d1",
  };
  uint8_t          finals[6][80];
  uint8_t          broken[6][80];
  const ssz_def_t* request;
  const ssz_def_t* proof_def;
  ssz_ob_t         proof;
  verify_ctx_t     ctx;
  storage_plugin_t saved;
  storage_plugin_t fake;
  json_t           args;
  char             method[] = "getblockheader";
  bytes_t          checkpoint;
  uint32_t         i;

  for (i = 0; i < 6; i++)
    load_hex(final_hex[i], finals[i], 80);
  checkpoint = btc_mainnet_checkpoint_header();
  request    = btc_ssz_verification_type(BTC_SSZ_VERIFY_REQUEST);
  proof_def  = &request->def.container.elements[2].def.container.elements[1];
  args       = json_parse("[\"0000000000000000000320283a032748cef8227873ff4872689bf23f1cda83a5\", false]");

  TEST_ASSERT_EQUAL_INT(METHOD_PROOFABLE, c4_btc_get_method_type(C4_CHAIN_BTC_MAINNET, method, args, 0));
  TEST_ASSERT_EQUAL_INT(METHOD_PROOFABLE, c4_btc_get_method_type(C4_CHAIN_BTC_MAINNET, method, json_parse("[\"00\", 0]"), 0));
  TEST_ASSERT_EQUAL_INT(METHOD_NOT_SUPPORTED, c4_btc_get_method_type(C4_CHAIN_BTC_MAINNET, method, json_parse("[\"00\"]"), 0));
  TEST_ASSERT_EQUAL_INT(METHOD_NOT_SUPPORTED, c4_btc_get_method_type(C4_CHAIN_BTC_MAINNET, method, json_parse("[\"00\", true]"), 0));
  TEST_ASSERT_EQUAL_INT(METHOD_NOT_SUPPORTED, c4_btc_get_method_type(C4_CHAIN_BTC_MAINNET, method, json_parse("[\"00\", 1]"), 0));
  TEST_ASSERT_EQUAL_INT(METHOD_NOT_SUPPORTED, c4_btc_get_method_type(C4_CHAIN_BTC_MAINNET, method, json_parse("[\"00\", false, 1]"), 0));
  TEST_ASSERT_EQUAL_INT(METHOD_NOT_SUPPORTED, c4_btc_get_method_type(C4_CHAIN_BTC_MAINNET, "getblock", args, 0));
  TEST_ASSERT_EQUAL_INT(METHOD_NOT_SUPPORTED, c4_btc_get_method_type(C4_CHAIN_BTC_TESTNET, method, args, 0));

  memset(&saved, 0, sizeof(saved));
  c4_get_storage_config(&saved);
  memset(&fake, 0, sizeof(fake));
  fake.get             = test_store_get;
  fake.set             = test_store_set;
  fake.del             = test_store_del;
  fake.max_sync_states = 1;
  c4_set_storage_config(&fake);

  proof = build_checkpoint_proof(proof_def, checkpoint, finals, 5, BTC_MAINNET_CHECKPOINT_HEIGHT, 0, NULL, 0, NULL_BYTES);
  memset(&ctx, 0, sizeof(ctx));
  ctx.chain_id = C4_CHAIN_BTC_MAINNET;
  ctx.method   = method;
  ctx.args     = args;
  ctx.proof    = proof;
  TEST_ASSERT_TRUE(c4_btc_verify(&ctx));
  TEST_ASSERT_FALSE(ctx.success);
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  safe_free(ctx.state.error);
  TEST_ASSERT_NULL(g_stored.data);
  safe_free(proof.bytes.data);

  proof = build_checkpoint_proof(proof_def, checkpoint, finals, 6, BTC_MAINNET_CHECKPOINT_HEIGHT, 0, NULL, 0, NULL_BYTES);
  memset(&ctx, 0, sizeof(ctx));
  ctx.chain_id = C4_CHAIN_BTC_MAINNET;
  ctx.method   = method;
  ctx.proof    = proof;
  ctx.args     = json_parse("[]");
  TEST_ASSERT_TRUE(c4_btc_verify(&ctx));
  TEST_ASSERT_FALSE(ctx.success);
  safe_free(ctx.state.error);
  ctx.state.error = NULL;
  ctx.args        = json_parse("[\"0000000000000000000000000000000000000000000000000000000000000001\", false]");
  TEST_ASSERT_TRUE(c4_btc_verify(&ctx));
  TEST_ASSERT_FALSE(ctx.success);
  safe_free(ctx.state.error);
  ctx.state.error = NULL;
  ctx.args        = json_parse("[\"0000000000000000000320283a032748cef8227873ff4872689bf23f1cda83a5\"]");
  TEST_ASSERT_TRUE(c4_btc_verify(&ctx));
  TEST_ASSERT_FALSE(ctx.success);
  safe_free(ctx.state.error);
  ctx.state.error = NULL;
  ctx.args        = json_parse("[\"0000000000000000000320283a032748cef8227873ff4872689bf23f1cda83a5\", 1]");
  TEST_ASSERT_TRUE(c4_btc_verify(&ctx));
  TEST_ASSERT_FALSE(ctx.success);
  safe_free(ctx.state.error);
  ctx.state.error = NULL;
  TEST_ASSERT_NULL(g_stored.data);
  ctx.args = args;
  TEST_ASSERT_TRUE(c4_btc_verify(&ctx));
  TEST_ASSERT_TRUE(ctx.success);
  TEST_ASSERT_NULL(ctx.state.error);
  TEST_ASSERT_EQUAL_UINT32(BTC_HEADER_SIZE, ctx.data.bytes.len);
  TEST_ASSERT_EQUAL_MEMORY(checkpoint.data, ctx.data.bytes.data, BTC_HEADER_SIZE);
  TEST_ASSERT_TRUE(ctx.flags & VERIFY_FLAG_FREE_DATA);
  safe_free(ctx.data.bytes.data);
  ctx.data       = (ssz_ob_t) {0};
  ctx.flags      = 0;
  ctx.data.def   = &request->def.container.elements[1].def.container.elements[1];
  ctx.data.bytes = bytes(finals[0], BTC_HEADER_SIZE);
  TEST_ASSERT_TRUE(c4_btc_verify(&ctx));
  TEST_ASSERT_FALSE(ctx.success);
  safe_free(ctx.state.error);
  ctx.state.error = NULL;
  ctx.data.bytes  = checkpoint;
  TEST_ASSERT_TRUE(c4_btc_verify(&ctx));
  TEST_ASSERT_TRUE(ctx.success);
  TEST_ASSERT_EQUAL_MEMORY(checkpoint.data, ctx.data.bytes.data, BTC_HEADER_SIZE);
  TEST_ASSERT_FALSE(ctx.flags & VERIFY_FLAG_FREE_DATA);
  ctx.args = json_parse("[\"0000000000000000000320283a032748cef8227873ff4872689bf23f1cda83a5\", 0]");
  ctx.data = (ssz_ob_t) {0};
  TEST_ASSERT_TRUE(c4_btc_verify(&ctx));
  TEST_ASSERT_TRUE(ctx.success);
  safe_free(ctx.data.bytes.data);
  TEST_ASSERT_TRUE(strncmp(g_store_key, "btc_target_", 11) == 0);
  safe_free(proof.bytes.data);

  {
    static const char* sibling_hex[12] = {
        "9ec667ead0ba5ef32fef31c21271328751851e7cc166f754dae64b004b5fb82b",
        "5b45735da8e6beb9110b93508eb4d93c0389906c53b0dbc067332368d8bf3d9e",
        "a0385f320f3a0247e04e371c788a9afea2879ddf498489551dbeac70e4414490",
        "5b0090d3743bdb905c0520d5976c7152e047dec1b4971598cfc3fae93802e67e",
        "e5910bfb3cd3faa5e7593ee88b3fcab3de7512e7e5a2e7e93b2b06d871542c1d",
        "a9ddff55b6cf51a47e0a51ddda339e6c1591a1add6cff25bc0f9e31639803744",
        "1ea2f399d6a56ca5a8c3eb1afd56512aab5dddecf356701a36905c73aea92466",
        "61b61c347bccb138b1b73b89ea5659acd6d45ab33b78a71993cd7ce26c9299c2",
        "72e1f0b021ac81d3c3b9909432a4122519c5f900f1383b8982f60a30f861b624",
        "32f91667de5cc8074d5e2eff9e4d1924807a1d773b773cad174fc3c34702ce0d",
        "2bbb2a97546cbea55097292ba7756f2142c667c87fb0cd4b3e66e39a47790db6",
        "0a310ba044ed34509e5e19d6327d2e925466870803b67bdb5e74787ff34abb3c",
    };
    static const char* coin_hex =
        "010000000001010000000000000000000000000000000000000000000000000000000000000000ffffffff600340d10c192f5669614254432f4d696e65642062792062757a7a3132302f2cfabe6d6d144b553283a6e1a150c9989428c0695e3a1bef7d482ed1f829bbe25897fd37dc10000000000000001058a4c9000cc3a31889b38ae08249000000000000ffffffff03fb80e4f2000000001976a914536ffa992491508dca0354e52f32a3a7a679a53a88ac00000000000000002b6a2952534b424c4f434b3a52e15efafb3e2cf6dc2fc0e6bde5cb1d7d2143f1e089bd874e6b7913005fb2a00000000000000000266a24aa21a9ed88601d3d03ccce017fe2131c4c95a7292e4372983148e62996bb5e2de0e4d1d80120000000000000000000000000000000000000000000000000000000000000000000000000";
    uint8_t siblings[12][32];
    uint8_t coin_buf[316];
    bytes_t coinbase;
    for (i = 0; i < 12; i++)
      load_hex(sibling_hex[i], siblings[i], 32);
    coinbase = load_hex(coin_hex, coin_buf, sizeof(coin_buf));
    proof    = build_checkpoint_proof(proof_def, checkpoint, finals, 6, BTC_MAINNET_CHECKPOINT_HEIGHT, 3050, siblings, 12, coinbase);
    memset(&ctx, 0, sizeof(ctx));
    ctx.chain_id = C4_CHAIN_BTC_MAINNET;
    ctx.method   = method;
    ctx.args     = args;
    ctx.proof    = proof;
    TEST_ASSERT_TRUE(c4_btc_verify(&ctx));
    TEST_ASSERT_TRUE(ctx.success);
    TEST_ASSERT_NULL(ctx.state.error);
    safe_free(ctx.data.bytes.data);
    safe_free(proof.bytes.data);

    siblings[0][0] ^= 0x01;
    proof = build_checkpoint_proof(proof_def, checkpoint, finals, 6, BTC_MAINNET_CHECKPOINT_HEIGHT, 3050, siblings, 12, coinbase);
    memset(&ctx, 0, sizeof(ctx));
    ctx.chain_id = C4_CHAIN_BTC_MAINNET;
    ctx.method   = method;
    ctx.args     = args;
    ctx.proof    = proof;
    TEST_ASSERT_TRUE(c4_btc_verify(&ctx));
    TEST_ASSERT_FALSE(ctx.success);
    safe_free(ctx.state.error);
    safe_free(proof.bytes.data);
    siblings[0][0] ^= 0x01;

    proof = build_checkpoint_proof(proof_def, checkpoint, finals, 6, BTC_MAINNET_CHECKPOINT_HEIGHT, 1, siblings, 12, coinbase);
    memset(&ctx, 0, sizeof(ctx));
    ctx.chain_id = C4_CHAIN_BTC_MAINNET;
    ctx.method   = method;
    ctx.args     = args;
    ctx.proof    = proof;
    TEST_ASSERT_TRUE(c4_btc_verify(&ctx));
    TEST_ASSERT_FALSE(ctx.success);
    safe_free(ctx.state.error);
    safe_free(proof.bytes.data);

    proof = build_checkpoint_proof(proof_def, checkpoint, finals, 6, BTC_MAINNET_CHECKPOINT_HEIGHT, 3050, siblings, 12, NULL_BYTES);
    memset(&ctx, 0, sizeof(ctx));
    ctx.chain_id = C4_CHAIN_BTC_MAINNET;
    ctx.method   = method;
    ctx.args     = args;
    ctx.proof    = proof;
    TEST_ASSERT_TRUE(c4_btc_verify(&ctx));
    TEST_ASSERT_FALSE(ctx.success);
    safe_free(ctx.state.error);
    safe_free(proof.bytes.data);

    proof = build_checkpoint_proof(proof_def, checkpoint, finals, 6, BTC_MAINNET_CHECKPOINT_HEIGHT, 0, NULL, 0, coinbase);
    memset(&ctx, 0, sizeof(ctx));
    ctx.chain_id = C4_CHAIN_BTC_MAINNET;
    ctx.method   = method;
    ctx.args     = args;
    ctx.proof    = proof;
    TEST_ASSERT_TRUE(c4_btc_verify(&ctx));
    TEST_ASSERT_FALSE(ctx.success);
    safe_free(ctx.state.error);
    safe_free(proof.bytes.data);

    coin_buf[45] ^= 0x01;
    proof = build_checkpoint_proof(proof_def, checkpoint, finals, 6, BTC_MAINNET_CHECKPOINT_HEIGHT, 3050, siblings, 12, bytes(coin_buf, sizeof(coin_buf)));
    memset(&ctx, 0, sizeof(ctx));
    ctx.chain_id = C4_CHAIN_BTC_MAINNET;
    ctx.method   = method;
    ctx.args     = args;
    ctx.proof    = proof;
    TEST_ASSERT_TRUE(c4_btc_verify(&ctx));
    TEST_ASSERT_FALSE(ctx.success);
    safe_free(ctx.state.error);
    safe_free(proof.bytes.data);
  }

  memcpy(broken, finals, sizeof(broken));
  broken[2][BTC_HEADER_PARENT] ^= 0x01;
  proof = build_checkpoint_proof(proof_def, checkpoint, broken, 6, BTC_MAINNET_CHECKPOINT_HEIGHT, 0, NULL, 0, NULL_BYTES);
  memset(&ctx, 0, sizeof(ctx));
  ctx.chain_id = C4_CHAIN_BTC_MAINNET;
  ctx.method   = method;
  ctx.args     = args;
  ctx.proof    = proof;
  TEST_ASSERT_TRUE(c4_btc_verify(&ctx));
  TEST_ASSERT_FALSE(ctx.success);
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  safe_free(ctx.state.error);
  safe_free(proof.bytes.data);
  {
    btc_target_cache_t cache;
    TEST_ASSERT_TRUE(btc_target_cache_load(C4_CHAIN_BTC_MAINNET, &cache));
    TEST_ASSERT_EQUAL_HEX32(0x17034219u, cache.entries[0].bits);
  }

  proof = build_checkpoint_proof(proof_def, checkpoint, finals, 6, 840001u, 0, NULL, 0, NULL_BYTES);
  memset(&ctx, 0, sizeof(ctx));
  ctx.chain_id = C4_CHAIN_BTC_MAINNET;
  ctx.method   = method;
  ctx.args     = args;
  ctx.proof    = proof;
  TEST_ASSERT_TRUE(c4_btc_verify(&ctx));
  TEST_ASSERT_FALSE(ctx.success);
  safe_free(ctx.state.error);
  safe_free(proof.bytes.data);

  memcpy(broken, finals, sizeof(broken));
  broken[5][BTC_HEADER_BITS]     = 0xff;
  broken[5][BTC_HEADER_BITS + 1] = 0xff;
  broken[5][BTC_HEADER_BITS + 2] = 0x00;
  broken[5][BTC_HEADER_BITS + 3] = 0x21;
  proof                          = build_checkpoint_proof(proof_def, checkpoint, broken, 6, BTC_MAINNET_CHECKPOINT_HEIGHT, 0, NULL, 0, NULL_BYTES);
  memset(&ctx, 0, sizeof(ctx));
  ctx.chain_id = C4_CHAIN_BTC_MAINNET;
  ctx.method   = method;
  ctx.args     = args;
  ctx.proof    = proof;
  TEST_ASSERT_TRUE(c4_btc_verify(&ctx));
  TEST_ASSERT_FALSE(ctx.success);
  safe_free(ctx.state.error);
  safe_free(proof.bytes.data);
  {
    btc_target_cache_t cache;
    TEST_ASSERT_TRUE(btc_target_cache_load(C4_CHAIN_BTC_MAINNET, &cache));
    TEST_ASSERT_EQUAL_HEX32(0x17034219u, cache.entries[0].bits);
  }

  proof = build_checkpoint_proof(proof_def, checkpoint, finals, 6, BTC_MAINNET_CHECKPOINT_HEIGHT, 0, NULL, 0, NULL_BYTES);
  memset(&ctx, 0, sizeof(ctx));
  ctx.chain_id = C4_CHAIN_BTC_MAINNET;
  ctx.method   = "getblock";
  ctx.args     = args;
  ctx.proof    = proof;
  TEST_ASSERT_TRUE(c4_btc_verify(&ctx));
  TEST_ASSERT_FALSE(ctx.success);
  safe_free(ctx.state.error);
  ctx.state.error = NULL;
  ctx.chain_id    = C4_CHAIN_BTC_TESTNET;
  ctx.method      = method;
  TEST_ASSERT_TRUE(c4_btc_verify(&ctx));
  TEST_ASSERT_FALSE(ctx.success);
  safe_free(ctx.state.error);
  ctx.state.error = NULL;
  ctx.chain_id    = C4_CHAIN_MAINNET;
  TEST_ASSERT_FALSE(c4_btc_verify(&ctx));
  fake.set = NULL;
  c4_set_storage_config(&fake);
  ctx.chain_id = C4_CHAIN_BTC_MAINNET;
  TEST_ASSERT_TRUE(c4_btc_verify(&ctx));
  TEST_ASSERT_FALSE(ctx.success);
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  safe_free(ctx.state.error);
  safe_free(ctx.data.bytes.data);
  safe_free(proof.bytes.data);

  c4_set_storage_config(&saved);
  safe_free(g_stored.data);
  g_stored = NULL_BYTES;
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
  RUN_TEST(test_target_retarget_cache_and_jumps);
  RUN_TEST(test_target_retarget_clamps_negative_span);
  RUN_TEST(test_target_cache_decode_rejects_malformed);
  RUN_TEST(test_target_accept_path_count_and_checkpoint);
  RUN_TEST(test_target_authorize_rejects_huge_percent);
  RUN_TEST(test_target_cache_capacity_and_find);
  RUN_TEST(test_epoch_path_ssz_shape);
  RUN_TEST(test_client_state_and_epoch_path);
  RUN_TEST(test_header_proof_finality);
  return UNITY_END();
}

#endif
