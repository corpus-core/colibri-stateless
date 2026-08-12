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

// Tests for ProgressiveContainer (EIP-7495) and ProgressiveList /
// ProgressiveBitlist (EIP-7916). All expected roots and gindices were
// generated with the reference implementation ethereum/remerkleable.

#include "bytes.h"
#include "c4_assert.h"
#include "json.h"
#include "ssz.h"
#include "state.h"
#include "unity.h"
#include <string.h>

void setUp(void) {
}

void tearDown(void) {
}

// ProgressiveList[uint64]
static const ssz_def_t UINT64_PROG_LIST = SSZ_PROG_LIST("uint64_list", ssz_uint64_def);

// ProgressiveList[Bytes32]
static const ssz_def_t BYTES32_PROG_LIST = SSZ_PROG_LIST("bytes32_list", ssz_bytes32);

// ProgressiveBitlist
static const ssz_def_t PROG_BIT_LIST = SSZ_PROG_BIT_LIST("bits");

// ProgressiveList[ByteList[64]] (dynamic / variable-size element type)
static const ssz_def_t BYTE_LIST_64       = SSZ_BYTES("data", 64);
static const ssz_def_t BYTELIST_PROG_LIST = SSZ_PROG_LIST("byte_lists", BYTE_LIST_64);

// EIP-7495 example: class Square(ProgressiveContainer(active_fields=[1, 0, 1]))
static const ssz_def_t SQUARE[] = {
    SSZ_UINT16("side"),
    SSZ_NONE,
    SSZ_UINT8("color"),
};
static const ssz_def_t SQUARE_CONTAINER = SSZ_PROG_CONTAINER("Square", SQUARE);

// EIP-7495 example: class Circle(ProgressiveContainer(active_fields=[0, 1, 1]))
static const ssz_def_t CIRCLE[] = {
    SSZ_NONE,
    SSZ_UINT16("radius"),
    SSZ_UINT8("color"),
};
static const ssz_def_t CIRCLE_CONTAINER = SSZ_PROG_CONTAINER("Circle", CIRCLE);

// class Nested(ProgressiveContainer(active_fields=[1, 1, 0, 0, 1])):
//   slot: uint64, root: Bytes32, values: ProgressiveList[uint64]
static const ssz_def_t NESTED[] = {
    SSZ_UINT64("slot"),
    SSZ_BYTES32("root"),
    SSZ_NONE,
    SSZ_NONE,
    SSZ_PROG_LIST("values", ssz_uint64_def),
};
static const ssz_def_t NESTED_CONTAINER = SSZ_PROG_CONTAINER("Nested", NESTED);

// serialization of Nested(slot=12345, root=0x11*32, values=[1..7]) created with remerkleable
static const char* NESTED_SER_HEX = "0x39300000000000001111111111111111111111111111111111111111111111111111111111111111"
                                    "2c000000"
                                    "0100000000000000020000000000000003000000000000000400000000000000"
                                    "050000000000000006000000000000000700000000000000";

static void check_root(ssz_ob_t ob, const char* expected, const char* msg) {
  bytes32_t root = {0};
  ssz_hash_tree_root(ob, root);
  ASSERT_HEX_STRING_EQUAL(expected, root, 32, msg);
}

static bytes_t from_hex(const char* hex, uint8_t* buf, size_t buf_size) {
  int len = hex_to_bytes(hex, -1, bytes(buf, (uint32_t) buf_size));
  TEST_ASSERT_GREATER_THAN_MESSAGE(0, len, "invalid hex string");
  return bytes(buf, (uint32_t) len);
}

// builds the serialization of ProgressiveList[uint64] with values 1..n into buf
static bytes_t uint64_list_data(buffer_t* buf, uint32_t n) {
  buffer_reset(buf);
  for (uint32_t i = 0; i < n; i++) {
    uint8_t tmp[8] = {0};
    uint64_to_le(tmp, i + 1);
    buffer_append(buf, bytes(tmp, 8));
  }
  return buf->data;
}

void test_prog_list_uint64_roots(void) {
  buffer_t buf = {0};
  struct {
    uint32_t    len;
    const char* root;
  } vectors[] = {
      {0, "0xf5a5fd42d16a20302798ef6ed309979b43003d2320d9f0e8ea9831a92759fb4b"},
      {1, "0x905efb51c2764c2c7a4efb0548e372569df06db82115c3b1896c186632f3fe5b"},
      {4, "0x95a2f252ed2659ccf75e8821f05757c4663fce68e89d0290abf5c33d772935ae"},
      {5, "0x29918e0447260511bc5be0f7dbb9817201e16e30c56af228b9cb931a16e8799d"},
      {21, "0xed360c03ecbdfbb6f4b1cf5d9cbf6887038423e31121700797de968a9969aaed"},
      {100, "0x3fea5b85e30e0416810839a91ea3767e65b04890709db74997109f50213b3375"},
  };
  for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
    ssz_ob_t ob = ssz_ob(UINT64_PROG_LIST, uint64_list_data(&buf, vectors[i].len));
    TEST_ASSERT_TRUE_MESSAGE(ssz_is_valid(ob, true, NULL), "progressive uint64 list must be valid");
    TEST_ASSERT_EQUAL_UINT32(vectors[i].len, ssz_len(ob));
    check_root(ob, vectors[i].root, "invalid root for ProgressiveList[uint64]");
  }
  buffer_free(&buf);
}

void test_prog_list_bytes32_roots(void) {
  buffer_t buf = {0};
  struct {
    uint32_t    len;
    const char* root;
  } vectors[] = {
      {0, "0xf5a5fd42d16a20302798ef6ed309979b43003d2320d9f0e8ea9831a92759fb4b"},
      {1, "0x905efb51c2764c2c7a4efb0548e372569df06db82115c3b1896c186632f3fe5b"},
      {3, "0x8b9e13c85c24b0073f9b226ee291c1ff181f3652f42d2bcaeb26b3c302ec6004"},
      {6, "0x76d03915aa777c431f6534cbd136b8f185b5df884546f52a8caa5db69ab49845"},
  };
  for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
    buffer_reset(&buf);
    for (uint32_t n = 0; n < vectors[i].len; n++) {
      uint8_t chunk[32] = {0};
      chunk[0]          = (uint8_t) (n + 1);
      buffer_append(&buf, bytes(chunk, 32));
    }
    ssz_ob_t ob = ssz_ob(BYTES32_PROG_LIST, buf.data);
    TEST_ASSERT_TRUE_MESSAGE(ssz_is_valid(ob, true, NULL), "progressive bytes32 list must be valid");
    TEST_ASSERT_EQUAL_UINT32(vectors[i].len, ssz_len(ob));
    check_root(ob, vectors[i].root, "invalid root for ProgressiveList[Bytes32]");
  }
  buffer_free(&buf);
}

void test_prog_bitlist_roots(void) {
  buffer_t buf = {0};
  struct {
    uint32_t    len;
    const char* root;
  } vectors[] = {
      {0, "0xf5a5fd42d16a20302798ef6ed309979b43003d2320d9f0e8ea9831a92759fb4b"},
      {3, "0x0a0a7cdb38b02d404eae74c9036e2727e4b9de617e77a4d6f3795a0777a034ab"},
      {255, "0xc3362ae9ebd383368b0b2a19d830219a39c34a482f2975536c82e16b39128702"},
      {256, "0x25f01ef233dd44d2615671507b9a90483b7823f384453d6be67e24f0f6bfb0c6"},
      {999, "0xe17f86a4e4e5fb491efbb1071c9c4e92c86464ac4f1385b5c4bd7a4a02a2fb6d"},
  };
  for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
    uint32_t len = vectors[i].len;
    buffer_reset(&buf);
    buffer_append(&buf, bytes(NULL, (len + 1 + 7) / 8)); // zeroed bytes incl. sentinel bit
    for (uint32_t n = 0; n < len; n++) {
      if (n % 3 == 0) buf.data.data[n >> 3] |= (uint8_t) (1 << (n & 7));
    }
    buf.data.data[len >> 3] |= (uint8_t) (1 << (len & 7)); // sentinel bit
    ssz_ob_t ob = ssz_ob(PROG_BIT_LIST, buf.data);
    TEST_ASSERT_TRUE_MESSAGE(ssz_is_valid(ob, true, NULL), "progressive bit list must be valid");
    TEST_ASSERT_EQUAL_UINT32(len, ssz_len(ob));
    check_root(ob, vectors[i].root, "invalid root for ProgressiveBitlist");
  }
  buffer_free(&buf);
}

void test_prog_container_eip7495_examples(void) {
  // Square and Circle serialize identically (only active fields), but hash differently
  uint8_t  data[] = {0x42, 0x00, 0x01};
  ssz_ob_t square = ssz_ob(SQUARE_CONTAINER, bytes(data, sizeof(data)));
  ssz_ob_t circle = ssz_ob(CIRCLE_CONTAINER, bytes(data, sizeof(data)));

  c4_state_t state = {0};
  TEST_ASSERT_TRUE_MESSAGE(ssz_is_valid(square, true, &state), state.error);
  TEST_ASSERT_TRUE_MESSAGE(ssz_is_valid(circle, true, &state), state.error);

  TEST_ASSERT_EQUAL_UINT64(0x42, ssz_get_uint64(&square, "side"));
  TEST_ASSERT_EQUAL_UINT64(1, ssz_get_uint64(&square, "color"));
  TEST_ASSERT_EQUAL_UINT64(0x42, ssz_get_uint64(&circle, "radius"));
  TEST_ASSERT_EQUAL_UINT64(1, ssz_get_uint64(&circle, "color"));

  check_root(square, "0x5d5c127e27e9862d9aacb13609cd9e936514fbe38e97dba278f0a83b553e57a0", "invalid Square root");
  check_root(circle, "0xcba0f15b6779f3f88f268311ae29faf0ba2e021c9f4fa4c91208161f563b1554", "invalid Circle root");
}

void test_prog_container_nested(void) {
  uint8_t  raw[128] = {0};
  bytes_t  data     = from_hex(NESTED_SER_HEX, raw, sizeof(raw));
  ssz_ob_t ob       = ssz_ob(NESTED_CONTAINER, data);

  c4_state_t state = {0};
  TEST_ASSERT_TRUE_MESSAGE(ssz_is_valid(ob, true, &state), state.error);

  TEST_ASSERT_EQUAL_UINT64(12345, ssz_get_uint64(&ob, "slot"));
  ssz_ob_t values = ssz_get(&ob, "values");
  TEST_ASSERT_EQUAL_UINT32(7, ssz_len(values));
  TEST_ASSERT_EQUAL_UINT64(7, ssz_uint64(ssz_at(values, 6)));

  check_root(values, "0xc1fbaac1b247e8871eb128eadd040aafbc9ef97ffaa1a7e68e75b376817b0072", "invalid values root");
  check_root(ob, "0x7db6e5f1c5c575348b6d520336dcfa85f02753f8c3e410140045f5bbaa11941b", "invalid Nested root");
}

void test_prog_container_from_json(void) {
  json_t json = json_parse("{\"slot\": 12345,"
                           "\"root\": \"0x1111111111111111111111111111111111111111111111111111111111111111\","
                           "\"values\": [1,2,3,4,5,6,7]}");

  c4_state_t state = {0};
  ssz_ob_t   ob    = ssz_from_json(json, &NESTED_CONTAINER, &state);
  TEST_ASSERT_NULL_MESSAGE(state.error, state.error);

  uint8_t raw[128] = {0};
  bytes_t expected = from_hex(NESTED_SER_HEX, raw, sizeof(raw));
  TEST_ASSERT_EQUAL_UINT32(expected.len, ob.bytes.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected.data, ob.bytes.data, expected.len, "serialization must match remerkleable");

  check_root(ob, "0x7db6e5f1c5c575348b6d520336dcfa85f02753f8c3e410140045f5bbaa11941b", "invalid Nested root from json");
  safe_free(ob.bytes.data);
}

void test_prog_gindex(void) {
  // chunk gindices of a progressive list (element index == chunk index for Bytes32),
  // expected values from remerkleable to_gindex_progressive
  struct {
    int      idx;
    gindex_t gindex;
  } vectors[] = {{0, 4}, {1, 40}, {2, 41}, {4, 43}, {5, 352}, {20, 367}, {21, 2944}, {84, 3007}, {85, 24064}, {100, 24079}};
  for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++)
    TEST_ASSERT_EQUAL_UINT64(vectors[i].gindex, ssz_gindex(&BYTES32_PROG_LIST, 1, vectors[i].idx));

  // field gindices of a progressive container (chunk index == field position incl. SSZ_NONE slots)
  TEST_ASSERT_EQUAL_UINT64(4, ssz_gindex(&NESTED_CONTAINER, 1, "slot"));    // chunk 0
  TEST_ASSERT_EQUAL_UINT64(40, ssz_gindex(&NESTED_CONTAINER, 1, "root"));   // chunk 1
  TEST_ASSERT_EQUAL_UINT64(43, ssz_gindex(&NESTED_CONTAINER, 1, "values")); // chunk 4

  // combined path into the nested progressive list (values[0..3] are packed in chunk 0)
  TEST_ASSERT_EQUAL_UINT64(172, ssz_gindex(&NESTED_CONTAINER, 2, "values", 0));

  // unknown fields and inactive slots must not resolve
  TEST_ASSERT_EQUAL_UINT64(0, ssz_gindex(&NESTED_CONTAINER, 1, "unknown"));
  TEST_ASSERT_EQUAL_UINT64(0, ssz_gindex(&NESTED_CONTAINER, 1, "NONE"));
}

void test_prog_proof_roundtrip(void) {
  uint8_t  raw[128] = {0};
  bytes_t  data     = from_hex(NESTED_SER_HEX, raw, sizeof(raw));
  ssz_ob_t ob       = ssz_ob(NESTED_CONTAINER, data);

  bytes32_t expected_root = {0};
  ssz_hash_tree_root(ob, expected_root);

  // single proof for the "root" field (gindex 40)
  gindex_t  gindex     = ssz_gindex(&NESTED_CONTAINER, 1, "root");
  bytes32_t root_hash  = {0};
  bytes_t   proof      = ssz_create_proof(ob, root_hash, gindex);
  bytes32_t leaf       = {0};
  bytes32_t proof_root = {0};
  memset(leaf, 0x11, 32);
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected_root, root_hash, 32, "root of proof creation must match hash_tree_root");
  ssz_verify_single_merkle_proof(proof, leaf, gindex, proof_root);
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected_root, proof_root, 32, "verified single proof must match the root");
  safe_free(proof.data);

  // multi proof for slot, root and values[0] (packed chunk with values 1..4)
  gindex_t gindexes[] = {
      ssz_gindex(&NESTED_CONTAINER, 1, "slot"),
      ssz_gindex(&NESTED_CONTAINER, 1, "root"),
      ssz_gindex(&NESTED_CONTAINER, 2, "values", 0)};
  bytes_t multi_proof = ssz_create_multi_proof(ob, root_hash, 3, gindexes[0], gindexes[1], gindexes[2]);
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected_root, root_hash, 32, "root of multi proof creation must match hash_tree_root");

  uint8_t leafes[96] = {0};
  uint64_to_le(leafes, 12345);            // slot
  memset(leafes + 32, 0x11, 32);          // root
  for (uint64_t i = 0; i < 4; i++)        // first chunk of values: 1..4 packed
    uint64_to_le(leafes + 64 + i * 8, i + 1);

  memset(proof_root, 0, 32);
  TEST_ASSERT_TRUE_MESSAGE(ssz_verify_multi_merkle_proof(multi_proof, bytes(leafes, sizeof(leafes)), gindexes, proof_root), "multi proof verification failed");
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected_root, proof_root, 32, "verified multi proof must match the root");
  safe_free(multi_proof.data);
}

// ProgressiveList with dynamic elements: serialization uses an offset table,
// roots generated with remerkleable ProgressiveList[ByteList[64]]
void test_prog_list_dynamic_elements(void) {
  uint8_t    raw[128] = {0};
  c4_state_t state    = {0};

  // empty list
  ssz_ob_t empty = ssz_ob(BYTELIST_PROG_LIST, bytes(NULL, 0));
  TEST_ASSERT_TRUE_MESSAGE(ssz_is_valid(empty, true, &state), state.error);
  TEST_ASSERT_EQUAL_UINT32(0, ssz_len(empty));
  check_root(empty, "0xf5a5fd42d16a20302798ef6ed309979b43003d2320d9f0e8ea9831a92759fb4b", "invalid root for empty ProgressiveList[ByteList]");

  // one empty element: just the offset table [4]
  ssz_ob_t one = ssz_ob(BYTELIST_PROG_LIST, from_hex("0x04000000", raw, sizeof(raw)));
  TEST_ASSERT_TRUE_MESSAGE(ssz_is_valid(one, true, &state), state.error);
  TEST_ASSERT_EQUAL_UINT32(1, ssz_len(one));
  TEST_ASSERT_EQUAL_UINT32(0, ssz_at(one, 0).bytes.len);
  check_root(one, "0xd5786f98b33ca4dc786bee109fcfa06b1488babf60ee5de52c9009f3a363c062", "invalid root for [b\"\"]");

  // trailing empty element: last offset equals the total length
  ssz_ob_t trailing = ssz_ob(BYTELIST_PROG_LIST, from_hex("0x0800000009000000ab", raw, sizeof(raw)));
  TEST_ASSERT_TRUE_MESSAGE(ssz_is_valid(trailing, true, &state), state.error);
  TEST_ASSERT_EQUAL_UINT32(2, ssz_len(trailing));
  TEST_ASSERT_EQUAL_UINT32(1, ssz_at(trailing, 0).bytes.len);
  TEST_ASSERT_EQUAL_UINT32(0, ssz_at(trailing, 1).bytes.len);
  check_root(trailing, "0x4451a6c8b543058ab9423502ac72034676d33fbc2954841fcdc07d5b9e6c336b", "invalid root for [b\"\\xab\", b\"\"]");

  // three elements with lengths 0, 3 and 40
  ssz_ob_t three = ssz_ob(BYTELIST_PROG_LIST,
                          from_hex("0x0c0000000c0000000f00000001020342424242424242424242424242424242"
                                   "424242424242424242424242424242424242424242424242",
                                   raw, sizeof(raw)));
  TEST_ASSERT_TRUE_MESSAGE(ssz_is_valid(three, true, &state), state.error);
  TEST_ASSERT_EQUAL_UINT32(3, ssz_len(three));
  TEST_ASSERT_EQUAL_UINT32(0, ssz_at(three, 0).bytes.len);
  uint8_t  expected_el1[] = {1, 2, 3};
  ssz_ob_t el1            = ssz_at(three, 1);
  TEST_ASSERT_EQUAL_UINT32(3, el1.bytes.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_el1, el1.bytes.data, 3);
  ssz_ob_t el2 = ssz_at(three, 2);
  TEST_ASSERT_EQUAL_UINT32(40, el2.bytes.len);
  for (int i = 0; i < 40; i++) TEST_ASSERT_EQUAL_UINT8(0x42, el2.bytes.data[i]);
  TEST_ASSERT_EQUAL_UINT32(0, ssz_at(three, 3).bytes.len); // out of range
  check_root(three, "0x0756182e42abfe2c2e07f28a4e8b5c86036aac706ec1c45c30cb200180dce43b", "invalid root for three byte lists");

  // six elements with lengths 0..5 (offset table of 24 bytes)
  ssz_ob_t six = ssz_ob(BYTELIST_PROG_LIST,
                        from_hex("0x1800000018000000190000001b0000001e00000022000000010202030303040404040505050505",
                                 raw, sizeof(raw)));
  TEST_ASSERT_TRUE_MESSAGE(ssz_is_valid(six, true, &state), state.error);
  TEST_ASSERT_EQUAL_UINT32(6, ssz_len(six));
  TEST_ASSERT_EQUAL_UINT32(5, ssz_at(six, 5).bytes.len);
  TEST_ASSERT_EQUAL_UINT8(5, ssz_at(six, 5).bytes.data[0]);
  check_root(six, "0x181fdcf5d4587082e43ca6e5649a5e9920d794b5f316e5ee84521475e6fede14", "invalid root for six byte lists");
}

void test_prog_list_dynamic_from_json(void) {
  json_t json = json_parse("[\"0x\",\"0x010203\","
                           "\"0x42424242424242424242424242424242424242424242424242424242424242424242424242424242\"]");

  c4_state_t state = {0};
  ssz_ob_t   ob    = ssz_from_json(json, &BYTELIST_PROG_LIST, &state);
  TEST_ASSERT_NULL_MESSAGE(state.error, state.error);

  uint8_t raw[128] = {0};
  bytes_t expected = from_hex("0x0c0000000c0000000f00000001020342424242424242424242424242424242"
                              "424242424242424242424242424242424242424242424242",
                              raw, sizeof(raw));
  TEST_ASSERT_EQUAL_UINT32(expected.len, ob.bytes.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected.data, ob.bytes.data, expected.len, "serialization must match remerkleable");
  check_root(ob, "0x0756182e42abfe2c2e07f28a4e8b5c86036aac706ec1c45c30cb200180dce43b", "invalid root from json");
  safe_free(ob.bytes.data);
}

// hostile / malformed bytes must be rejected by ssz_is_valid
void test_prog_list_invalid_bytes(void) {
  uint8_t    raw[16] = {0};
  c4_state_t state   = {0};

  struct {
    const char* hex;
    const char* msg;
  } vectors[] = {
      {"0x040000", "offset table shorter than one offset must be invalid"},
      {"0x05000000aabbccdd", "misaligned first offset must be invalid"},
      {"0x08000000", "first offset beyond total length must be invalid"},
      {"0x00000000aabbccdd", "first offset inside the offset table must be invalid"},
      {"0x0800000005000000aabb", "decreasing offsets must be invalid"},
  };
  for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
    ssz_ob_t ob = ssz_ob(BYTELIST_PROG_LIST, from_hex(vectors[i].hex, raw, sizeof(raw)));
    TEST_ASSERT_FALSE_MESSAGE(ssz_is_valid(ob, true, &state), vectors[i].msg);
    c4_state_free(&state);
    state = (c4_state_t) {0};
  }

  // fixed-size elements: total length must be a multiple of the element size
  ssz_ob_t truncated = ssz_ob(UINT64_PROG_LIST, from_hex("0x01000000000000", raw, sizeof(raw)));
  TEST_ASSERT_FALSE_MESSAGE(ssz_is_valid(truncated, true, &state), "length not a multiple of the element size must be invalid");
  c4_state_free(&state);
}

void test_prog_bitlist_invalid_bytes(void) {
  c4_state_t state = {0};

  // empty bytes: even an empty progressive bit list requires the sentinel byte 0x01
  ssz_ob_t empty = ssz_ob(PROG_BIT_LIST, bytes(NULL, 0));
  TEST_ASSERT_FALSE_MESSAGE(ssz_is_valid(empty, true, &state), "empty bytes must be invalid for a progressive bit list");
  c4_state_free(&state);
  state = (c4_state_t) {0};

  // last byte zero: the sentinel bit is missing
  uint8_t  no_sentinel[2] = {0x05, 0x00};
  ssz_ob_t ob             = ssz_ob(PROG_BIT_LIST, bytes(no_sentinel, sizeof(no_sentinel)));
  TEST_ASSERT_FALSE_MESSAGE(ssz_is_valid(ob, true, &state), "missing sentinel bit must be invalid");
  c4_state_free(&state);
  TEST_ASSERT_EQUAL_UINT32(0, ssz_len(empty)); // ssz_len must not read out of bounds for empty bytes
}

void test_prog_container_invalid_bytes(void) {
  uint8_t    raw[128] = {0};
  bytes_t    data     = from_hex(NESTED_SER_HEX, raw, sizeof(raw));
  c4_state_t state    = {0};

  // truncated fixed part (fixed length of Nested is 8 + 32 + 4 = 44 bytes)
  ssz_ob_t truncated = ssz_ob(NESTED_CONTAINER, bytes(data.data, 43));
  TEST_ASSERT_FALSE_MESSAGE(ssz_is_valid(truncated, true, &state), "truncated progressive container must be invalid");
  c4_state_free(&state);
  state = (c4_state_t) {0};

  // offset of the dynamic "values" field pointing beyond the total length
  uint8_t bad_offset[44];
  memcpy(bad_offset, data.data, 44);
  bad_offset[40] = 0xff; // offset = 0xff > 44
  TEST_ASSERT_FALSE_MESSAGE(ssz_is_valid(ssz_ob(NESTED_CONTAINER, bytes(bad_offset, sizeof(bad_offset))), true, &state), "offset beyond total length must be invalid");
  c4_state_free(&state);
  state = (c4_state_t) {0};

  // offset pointing into the fixed part
  memcpy(bad_offset, data.data, 44);
  bad_offset[40] = 8; // offset = 8 < 44 (end of fixed part)
  TEST_ASSERT_FALSE_MESSAGE(ssz_is_valid(ssz_ob(NESTED_CONTAINER, bytes(bad_offset, sizeof(bad_offset))), true, &state), "offset into the fixed part must be invalid");
  c4_state_free(&state);
  state = (c4_state_t) {0};

  // fully fixed progressive container (Square, 3 bytes): wrong lengths must be rejected
  uint8_t square_data[4] = {0x42, 0x00, 0x01, 0x00};
  TEST_ASSERT_FALSE_MESSAGE(ssz_is_valid(ssz_ob(SQUARE_CONTAINER, bytes(square_data, 2)), true, &state), "too short fixed progressive container must be invalid");
  c4_state_free(&state);
  state = (c4_state_t) {0};
  TEST_ASSERT_FALSE_MESSAGE(ssz_is_valid(ssz_ob(SQUARE_CONTAINER, bytes(square_data, 4)), true, &state), "too long fixed progressive container must be invalid");
  c4_state_free(&state);
}

// JSON dump of a progressive container must skip inactive SSZ_NONE slots
void test_prog_container_dump(void) {
  uint8_t  raw[128] = {0};
  bytes_t  data     = from_hex(NESTED_SER_HEX, raw, sizeof(raw));
  ssz_ob_t ob       = ssz_ob(NESTED_CONTAINER, data);

  char* json_str = ssz_dump_to_str(ob, false, false);
  TEST_ASSERT_NOT_NULL(json_str);
  TEST_ASSERT_NULL_MESSAGE(strstr(json_str, "NONE"), "inactive slots must not appear in the JSON dump");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(json_str, "\"slot\""), "active field slot missing in dump");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(json_str, "\"root\""), "active field root missing in dump");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(json_str, "\"values\""), "active field values missing in dump");

  // the dump must be valid JSON with the expected values
  json_t parsed = json_parse(json_str);
  TEST_ASSERT_EQUAL_INT_MESSAGE(JSON_TYPE_OBJECT, parsed.type, "dump must be a valid JSON object");
  TEST_ASSERT_EQUAL_UINT64(12345, json_get_uint64(parsed, "slot"));
  TEST_ASSERT_EQUAL_INT(JSON_TYPE_ARRAY, json_get(parsed, "values").type);
  safe_free(json_str);
}

// single and multi proofs for chunks in a higher progressive subtree
// (chunk 5 is the first chunk of the 16-leaf subtree after the 1+4 chunks)
void test_prog_proof_subtree_boundary(void) {
  // ProgressiveList[Bytes32] with 6 elements (chunk i holds first byte i+1)
  buffer_t buf = {0};
  for (uint32_t n = 0; n < 6; n++) {
    uint8_t chunk[32] = {0};
    chunk[0]          = (uint8_t) (n + 1);
    buffer_append(&buf, bytes(chunk, 32));
  }
  ssz_ob_t ob = ssz_ob(BYTES32_PROG_LIST, buf.data);

  bytes32_t expected_root = {0};
  ssz_hash_tree_root(ob, expected_root);

  // single proof for element 5 (gindex 352, crossing the 4->16 subtree boundary)
  gindex_t  gindex = ssz_gindex(&BYTES32_PROG_LIST, 1, 5);
  bytes32_t root_hash, leaf = {0}, proof_root = {0};
  TEST_ASSERT_EQUAL_UINT64(352, gindex);
  bytes_t proof = ssz_create_proof(ob, root_hash, gindex);
  leaf[0]       = 6;
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected_root, root_hash, 32, "root of proof creation must match hash_tree_root");
  ssz_verify_single_merkle_proof(proof, leaf, gindex, proof_root);
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected_root, proof_root, 32, "verified single proof for chunk 5 must match the root");
  safe_free(proof.data);

  // multi proof spanning both subtrees: element 0 (gindex 4), element 4 (gindex 43) and element 5 (gindex 352)
  gindex_t gindexes[]  = {4, 43, 352};
  bytes_t  multi_proof = ssz_create_multi_proof(ob, root_hash, 3, gindexes[0], gindexes[1], gindexes[2]);
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected_root, root_hash, 32, "root of multi proof creation must match hash_tree_root");

  uint8_t leafes[96] = {0};
  leafes[0]          = 1; // element 0
  leafes[32]         = 5; // element 4
  leafes[64]         = 6; // element 5
  memset(proof_root, 0, 32);
  TEST_ASSERT_TRUE_MESSAGE(ssz_verify_multi_merkle_proof(multi_proof, bytes(leafes, sizeof(leafes)), gindexes, proof_root), "multi proof verification failed");
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected_root, proof_root, 32, "verified multi proof across subtrees must match the root");
  safe_free(multi_proof.data);
  buffer_free(&buf);
}

// proof for a dynamic element of a progressive list: the leaf is the
// hash_tree_root of the element (from remerkleable)
void test_prog_proof_dynamic_element(void) {
  uint8_t  raw[128] = {0};
  ssz_ob_t ob       = ssz_ob(BYTELIST_PROG_LIST,
                             from_hex("0x0c0000000c0000000f00000001020342424242424242424242424242424242"
                                      "424242424242424242424242424242424242424242424242",
                                      raw, sizeof(raw)));

  bytes32_t expected_root = {0};
  ssz_hash_tree_root(ob, expected_root);

  gindex_t gindex = ssz_gindex(&BYTELIST_PROG_LIST, 1, 2);
  TEST_ASSERT_EQUAL_UINT64(41, gindex);

  bytes32_t root_hash, leaf = {0}, proof_root = {0};
  bytes_t   proof = ssz_create_proof(ob, root_hash, gindex);
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected_root, root_hash, 32, "root of proof creation must match hash_tree_root");

  // leaf = hash_tree_root of the ByteList[64] element (incl. its own length mix-in)
  uint8_t leaf_raw[32] = {0};
  memcpy(leaf, from_hex("0x00adc194d6762a8691a5f37e44f3a2bd6ed7a66b5fb9d096f8fa23d6694f667e", leaf_raw, sizeof(leaf_raw)).data, 32);
  ssz_verify_single_merkle_proof(proof, leaf, gindex, proof_root);
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected_root, proof_root, 32, "verified proof for a dynamic element must match the root");
  safe_free(proof.data);
}

void test_prog_container_invalid_defs(void) {
  // active_fields must not end in 0 -> last element must not be SSZ_NONE
  static const ssz_def_t TRAILING_NONE[] = {
      SSZ_UINT64("value"),
      SSZ_NONE,
  };
  static const ssz_def_t TRAILING_NONE_CONTAINER = SSZ_PROG_CONTAINER("TrailingNone", TRAILING_NONE);

  uint8_t    data[8] = {0};
  c4_state_t state   = {0};
  TEST_ASSERT_FALSE_MESSAGE(ssz_is_valid(ssz_ob(TRAILING_NONE_CONTAINER, bytes(data, sizeof(data))), false, &state), "trailing SSZ_NONE must be invalid");
  c4_state_free(&state);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_prog_list_uint64_roots);
  RUN_TEST(test_prog_list_bytes32_roots);
  RUN_TEST(test_prog_bitlist_roots);
  RUN_TEST(test_prog_container_eip7495_examples);
  RUN_TEST(test_prog_container_nested);
  RUN_TEST(test_prog_container_from_json);
  RUN_TEST(test_prog_gindex);
  RUN_TEST(test_prog_proof_roundtrip);
  RUN_TEST(test_prog_container_invalid_defs);
  RUN_TEST(test_prog_list_dynamic_elements);
  RUN_TEST(test_prog_list_dynamic_from_json);
  RUN_TEST(test_prog_list_invalid_bytes);
  RUN_TEST(test_prog_bitlist_invalid_bytes);
  RUN_TEST(test_prog_container_invalid_bytes);
  RUN_TEST(test_prog_container_dump);
  RUN_TEST(test_prog_proof_subtree_boundary);
  RUN_TEST(test_prog_proof_dynamic_element);
  return UNITY_END();
}
