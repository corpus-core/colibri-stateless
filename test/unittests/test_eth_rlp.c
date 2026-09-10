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

#include "bytes.h"
#include "rlp.h"
#include "unity.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void assert_decode_item(bytes_t src, const uint8_t* expected, uint32_t expected_len) {
  bytes_t out = {0};
  TEST_ASSERT_EQUAL_INT(RLP_ITEM, rlp_decode(&src, 0, &out));
  TEST_ASSERT_EQUAL_UINT32(expected_len, out.len);
  if (expected_len)
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out.data, expected_len);
}

static void assert_encoded(buffer_t* buf, const uint8_t* expected, uint32_t expected_len) {
  TEST_ASSERT_EQUAL_UINT32(expected_len, buf->data.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, buf->data.data, expected_len);
}

void test_rlp_encode_decode_single_byte(void) {
  uint8_t  expected[] = {0x0f};
  buffer_t buf        = {0};
  uint8_t  val        = 0x0f;
  rlp_add_item(&buf, bytes(&val, 1));
  assert_encoded(&buf, expected, 1);
  assert_decode_item(buf.data, &val, 1);
  TEST_ASSERT_EQUAL_UINT64(15, rlp_get_uint64(buf.data, 0));
  buffer_free(&buf);
}

void test_rlp_encode_decode_empty_string(void) {
  uint8_t  expected[] = {0x80};
  buffer_t buf        = {0};
  rlp_add_item(&buf, NULL_BYTES);
  assert_encoded(&buf, expected, 1);

  bytes_t out = {0};
  TEST_ASSERT_EQUAL_INT(RLP_ITEM, rlp_decode(&buf.data, 0, &out));
  TEST_ASSERT_EQUAL_UINT32(0, out.len);
  TEST_ASSERT_EQUAL_UINT64(0, rlp_get_uint64(buf.data, 0));
  buffer_free(&buf);
}

void test_rlp_encode_decode_short_string(void) {
  uint8_t  expected[] = {0x83, 'd', 'o', 'g'};
  buffer_t buf        = {0};
  rlp_add_item(&buf, bytes("dog", 3));
  assert_encoded(&buf, expected, 4);
  assert_decode_item(buf.data, (const uint8_t*) "dog", 3);
  buffer_free(&buf);
}

void test_rlp_encode_decode_empty_list(void) {
  uint8_t  expected[] = {0xc0};
  buffer_t buf        = {0};
  rlp_add_list(&buf, NULL_BYTES);
  assert_encoded(&buf, expected, 1);

  bytes_t out = {0};
  TEST_ASSERT_EQUAL_INT(RLP_LIST, rlp_decode(&buf.data, 0, &out));
  TEST_ASSERT_EQUAL_UINT32(0, out.len);
  TEST_ASSERT_EQUAL_INT(0, rlp_decode(&out, -1, NULL));
  buffer_free(&buf);
}

void test_rlp_encode_decode_short_list(void) {
  // ["cat", "dog"] = c8 83 'c' 'a' 't' 83 'd' 'o' 'g'
  uint8_t  expected[] = {0xc8, 0x83, 'c', 'a', 't', 0x83, 'd', 'o', 'g'};
  buffer_t items      = {0};
  rlp_add_item(&items, bytes("cat", 3));
  rlp_add_item(&items, bytes("dog", 3));
  buffer_t buf = {0};
  rlp_add_list(&buf, items.data);
  assert_encoded(&buf, expected, 9);

  bytes_t list = {0};
  TEST_ASSERT_EQUAL_INT(RLP_LIST, rlp_decode(&buf.data, 0, &list));
  TEST_ASSERT_EQUAL_INT(2, rlp_decode(&list, -1, NULL));

  bytes_t item = {0};
  TEST_ASSERT_EQUAL_INT(RLP_ITEM, rlp_decode(&list, 0, &item));
  TEST_ASSERT_EQUAL_UINT32(3, item.len);
  TEST_ASSERT_EQUAL_MEMORY("cat", item.data, 3);
  TEST_ASSERT_EQUAL_INT(RLP_ITEM, rlp_decode(&list, 1, &item));
  TEST_ASSERT_EQUAL_MEMORY("dog", item.data, 3);
  TEST_ASSERT_EQUAL_INT(RLP_NOT_FOUND, rlp_decode(&list, 2, &item));

  buffer_free(&items);
  buffer_free(&buf);
}

void test_rlp_to_list_wraps_encoded_items(void) {
  buffer_t buf = {0};
  rlp_add_item(&buf, bytes("cat", 3));
  rlp_add_item(&buf, bytes("dog", 3));
  rlp_to_list(&buf);

  uint8_t expected[] = {0xc8, 0x83, 'c', 'a', 't', 0x83, 'd', 'o', 'g'};
  assert_encoded(&buf, expected, 9);
  buffer_free(&buf);
}

void test_rlp_nested_list_yellow_paper(void) {
  // [ [], [[]], [ [], [[]] ] ] = c7 c0 c1 c0 c3 c0 c1 c0
  uint8_t expected[] = {0xc7, 0xc0, 0xc1, 0xc0, 0xc3, 0xc0, 0xc1, 0xc0};

  buffer_t empty = {0};
  rlp_add_list(&empty, NULL_BYTES); // []

  buffer_t one = {0};
  rlp_add_list(&one, empty.data); // [[]]

  buffer_t inner = {0};
  buffer_append(&inner, empty.data);
  buffer_append(&inner, one.data);
  buffer_t two = {0};
  rlp_add_list(&two, inner.data); // [ [], [[]] ]

  buffer_t payload = {0};
  buffer_append(&payload, empty.data);
  buffer_append(&payload, one.data);
  buffer_append(&payload, two.data);
  buffer_t buf = {0};
  rlp_add_list(&buf, payload.data);

  assert_encoded(&buf, expected, 8);

  bytes_t list = {0};
  TEST_ASSERT_EQUAL_INT(RLP_LIST, rlp_decode(&buf.data, 0, &list));
  TEST_ASSERT_EQUAL_INT(3, rlp_decode(&list, -1, NULL));

  bytes_t item = {0};
  TEST_ASSERT_EQUAL_INT(RLP_LIST, rlp_decode(&list, 0, &item));
  TEST_ASSERT_EQUAL_UINT32(0, item.len);
  TEST_ASSERT_EQUAL_INT(RLP_LIST, rlp_decode(&list, 1, &item));
  TEST_ASSERT_EQUAL_INT(1, rlp_decode(&item, -1, NULL));

  buffer_free(&empty);
  buffer_free(&one);
  buffer_free(&inner);
  buffer_free(&two);
  buffer_free(&payload);
  buffer_free(&buf);
}

void test_rlp_encode_decode_long_string(void) {
  uint8_t data[56];
  memset(data, 'a', sizeof(data));

  buffer_t buf = {0};
  rlp_add_item(&buf, bytes(data, sizeof(data)));
  TEST_ASSERT_EQUAL_UINT32(58, buf.data.len);
  TEST_ASSERT_EQUAL_UINT8(0xb8, buf.data.data[0]);
  TEST_ASSERT_EQUAL_UINT8(56, buf.data.data[1]);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(data, buf.data.data + 2, 56);

  assert_decode_item(buf.data, data, 56);
  buffer_free(&buf);
}

void test_rlp_encode_decode_long_list(void) {
  // 56 empty strings → 56-byte list payload, long-list prefix 0xf8 0x38
  buffer_t items = {0};
  for (int i = 0; i < 56; i++)
    rlp_add_item(&items, NULL_BYTES);

  buffer_t buf = {0};
  rlp_add_list(&buf, items.data);
  TEST_ASSERT_EQUAL_UINT32(58, buf.data.len);
  TEST_ASSERT_EQUAL_UINT8(0xf8, buf.data.data[0]);
  TEST_ASSERT_EQUAL_UINT8(56, buf.data.data[1]);

  bytes_t list = {0};
  TEST_ASSERT_EQUAL_INT(RLP_LIST, rlp_decode(&buf.data, 0, &list));
  TEST_ASSERT_EQUAL_UINT32(56, list.len);
  TEST_ASSERT_EQUAL_INT(56, rlp_decode(&list, -1, NULL));

  bytes_t item = {0};
  TEST_ASSERT_EQUAL_INT(RLP_ITEM, rlp_decode(&list, 0, &item));
  TEST_ASSERT_EQUAL_UINT32(0, item.len);
  TEST_ASSERT_EQUAL_INT(RLP_ITEM, rlp_decode(&list, 55, &item));
  TEST_ASSERT_EQUAL_INT(RLP_NOT_FOUND, rlp_decode(&list, 56, &item));

  buffer_free(&items);
  buffer_free(&buf);
}

void test_rlp_add_uint_and_uint64(void) {
  buffer_t buf = {0};
  rlp_add_uint64(&buf, 0);
  uint8_t zero[] = {0x80};
  assert_encoded(&buf, zero, 1);
  buffer_free(&buf);

  buf = (buffer_t) {0};
  rlp_add_uint64(&buf, 15);
  uint8_t fifteen[] = {0x0f};
  assert_encoded(&buf, fifteen, 1);
  buffer_free(&buf);

  buf = (buffer_t) {0};
  rlp_add_uint64(&buf, 1024);
  uint8_t k[] = {0x82, 0x04, 0x00};
  assert_encoded(&buf, k, 3);
  TEST_ASSERT_EQUAL_UINT64(1024, rlp_get_uint64(buf.data, 0));
  buffer_free(&buf);

  uint8_t raw[] = {0x00, 0x00, 0x01};
  buf           = (buffer_t) {0};
  rlp_add_uint(&buf, bytes(raw, 3));
  uint8_t stripped[] = {0x01};
  assert_encoded(&buf, stripped, 1);
  buffer_free(&buf);
}

void test_rlp_decode_index_on_concatenated_items(void) {
  buffer_t buf = {0};
  rlp_add_item(&buf, bytes("a", 1));
  rlp_add_item(&buf, bytes("bb", 2));
  rlp_add_uint64(&buf, 7);

  TEST_ASSERT_EQUAL_INT(3, rlp_decode(&buf.data, -1, NULL));

  bytes_t item = {0};
  TEST_ASSERT_EQUAL_INT(RLP_ITEM, rlp_decode(&buf.data, 0, &item));
  TEST_ASSERT_EQUAL_MEMORY("a", item.data, 1);
  TEST_ASSERT_EQUAL_INT(RLP_ITEM, rlp_decode(&buf.data, 1, &item));
  TEST_ASSERT_EQUAL_MEMORY("bb", item.data, 2);
  TEST_ASSERT_EQUAL_UINT64(7, rlp_get_uint64(buf.data, 2));
  TEST_ASSERT_EQUAL_INT(RLP_NOT_FOUND, rlp_decode(&buf.data, 3, &item));
  buffer_free(&buf);
}

void test_rlp_truncated_short_string(void) {
  uint8_t raw[] = {0x83, 'd', 'o'}; // claims 3 payload bytes
  bytes_t src   = bytes(raw, sizeof(raw));
  bytes_t out   = {0};
  TEST_ASSERT_EQUAL_INT(RLP_OUT_OF_RANGE, rlp_decode(&src, 0, &out));
  TEST_ASSERT_EQUAL_INT(RLP_OUT_OF_RANGE, rlp_decode(&src, -1, NULL));
}

void test_rlp_truncated_short_list(void) {
  uint8_t raw[] = {0xc3, 0x80}; // claims 3 payload bytes
  bytes_t src   = bytes(raw, sizeof(raw));
  bytes_t out   = {0};
  TEST_ASSERT_EQUAL_INT(RLP_OUT_OF_RANGE, rlp_decode(&src, 0, &out));
}

void test_rlp_truncated_long_string_prefix(void) {
  uint8_t raw[] = {0xb8}; // 1 length byte required, none present
  bytes_t src   = bytes(raw, sizeof(raw));
  bytes_t out   = {0};
  TEST_ASSERT_EQUAL_INT(RLP_OUT_OF_RANGE, rlp_decode(&src, 0, &out));
  TEST_ASSERT_EQUAL_INT(RLP_OUT_OF_RANGE, rlp_decode(&src, -1, NULL));
  TEST_ASSERT_EQUAL_UINT64(0, rlp_get_uint64(src, 0));
}

void test_rlp_truncated_long_string_len_bytes(void) {
  uint8_t raw[] = {0xb9, 0x01}; // 2 length bytes required
  bytes_t src   = bytes(raw, sizeof(raw));
  bytes_t out   = {0};
  TEST_ASSERT_EQUAL_INT(RLP_OUT_OF_RANGE, rlp_decode(&src, 0, &out));
}

void test_rlp_truncated_long_string_payload(void) {
  uint8_t raw[] = {0xb8, 0x40, 0x00}; // claims 64 payload bytes
  bytes_t src   = bytes(raw, sizeof(raw));
  bytes_t out   = {0};
  TEST_ASSERT_EQUAL_INT(RLP_OUT_OF_RANGE, rlp_decode(&src, 0, &out));
}

void test_rlp_truncated_long_list_prefix(void) {
  uint8_t raw[] = {0xf8};
  bytes_t src   = bytes(raw, sizeof(raw));
  bytes_t out   = {0};
  TEST_ASSERT_EQUAL_INT(RLP_OUT_OF_RANGE, rlp_decode(&src, 0, &out));
  TEST_ASSERT_EQUAL_INT(RLP_OUT_OF_RANGE, rlp_decode(&src, -1, NULL));
}

void test_rlp_truncated_long_list_len_bytes(void) {
  uint8_t raw[] = {0xf9, 0x00};
  bytes_t src   = bytes(raw, sizeof(raw));
  bytes_t out   = {0};
  TEST_ASSERT_EQUAL_INT(RLP_OUT_OF_RANGE, rlp_decode(&src, 0, &out));
}

void test_rlp_truncated_long_list_payload(void) {
  uint8_t raw[] = {0xf8, 0x38, 0xc0}; // claims 56 payload bytes
  bytes_t src   = bytes(raw, sizeof(raw));
  bytes_t out   = {0};
  TEST_ASSERT_EQUAL_INT(RLP_OUT_OF_RANGE, rlp_decode(&src, 0, &out));
}

void test_rlp_long_len_eight_bytes_truncated_prefix(void) {
  uint8_t raw[8] = {0xbf, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07}; // needs 8 length bytes
  bytes_t src    = bytes(raw, sizeof(raw));
  bytes_t out    = {0};
  TEST_ASSERT_EQUAL_INT(RLP_OUT_OF_RANGE, rlp_decode(&src, 0, &out));
}

void test_rlp_huge_long_len_does_not_overflow(void) {
  uint8_t raw[9] = {0xbf, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  bytes_t src    = bytes(raw, sizeof(raw));
  bytes_t out    = {0};
  TEST_ASSERT_EQUAL_INT(RLP_OUT_OF_RANGE, rlp_decode(&src, 0, &out));

  uint8_t list_raw[9] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  bytes_t list_src    = bytes(list_raw, sizeof(list_raw));
  TEST_ASSERT_EQUAL_INT(RLP_OUT_OF_RANGE, rlp_decode(&list_src, 0, &out));
}

void test_rlp_truncated_prefix_on_skip_path(void) {
  // Valid single byte, then a truncated long-string prefix (OOB if skip does not bound-check).
  uint8_t raw[] = {0x01, 0xb8};
  bytes_t src   = bytes(raw, sizeof(raw));
  bytes_t out   = {0};
  TEST_ASSERT_EQUAL_INT(RLP_ITEM, rlp_decode(&src, 0, &out));
  TEST_ASSERT_EQUAL_UINT8(0x01, out.data[0]);
  TEST_ASSERT_EQUAL_INT(RLP_OUT_OF_RANGE, rlp_decode(&src, 1, &out));
  TEST_ASSERT_EQUAL_INT(RLP_OUT_OF_RANGE, rlp_decode(&src, -1, NULL));
}

void test_rlp_empty_input(void) {
  bytes_t src = NULL_BYTES;
  bytes_t out = {0};
  TEST_ASSERT_EQUAL_INT(RLP_NOT_FOUND, rlp_decode(&src, 0, &out));
  TEST_ASSERT_EQUAL_INT(0, rlp_decode(&src, -1, NULL));
  TEST_ASSERT_EQUAL_UINT64(0, rlp_get_uint64(src, 0));
}

void test_rlp_encode_decode_max_short_string(void) {
  uint8_t data[55];
  memset(data, 'c', sizeof(data));

  buffer_t buf = {0};
  rlp_add_item(&buf, bytes(data, sizeof(data)));
  TEST_ASSERT_EQUAL_UINT32(56, buf.data.len);
  TEST_ASSERT_EQUAL_UINT8(0xb7, buf.data.data[0]);
  assert_decode_item(buf.data, data, 55);
  buffer_free(&buf);
}

void test_rlp_encode_decode_two_byte_long_string(void) {
  uint8_t data[256];
  memset(data, 'b', sizeof(data));

  buffer_t buf = {0};
  rlp_add_item(&buf, bytes(data, sizeof(data)));
  TEST_ASSERT_EQUAL_UINT32(259, buf.data.len);
  TEST_ASSERT_EQUAL_UINT8(0xb9, buf.data.data[0]);
  TEST_ASSERT_EQUAL_UINT8(0x01, buf.data.data[1]);
  TEST_ASSERT_EQUAL_UINT8(0x00, buf.data.data[2]);
  assert_decode_item(buf.data, data, 256);
  buffer_free(&buf);
}

void test_rlp_skip_valid_long_items(void) {
  uint8_t data[56];
  memset(data, 'a', sizeof(data));

  buffer_t buf = {0};
  rlp_add_item(&buf, bytes(data, sizeof(data)));
  uint8_t next = 0x05;
  rlp_add_item(&buf, bytes(&next, 1));

  TEST_ASSERT_EQUAL_INT(2, rlp_decode(&buf.data, -1, NULL));
  bytes_t item = {0};
  TEST_ASSERT_EQUAL_INT(RLP_ITEM, rlp_decode(&buf.data, 1, &item));
  TEST_ASSERT_EQUAL_UINT32(1, item.len);
  TEST_ASSERT_EQUAL_UINT8(0x05, item.data[0]);
  TEST_ASSERT_EQUAL_UINT64(5, rlp_get_uint64(buf.data, 1));
  buffer_free(&buf);

  buffer_t items = {0};
  for (int i = 0; i < 56; i++)
    rlp_add_item(&items, NULL_BYTES);
  buf = (buffer_t) {0};
  rlp_add_list(&buf, items.data);
  next = 0x01;
  rlp_add_item(&buf, bytes(&next, 1));

  TEST_ASSERT_EQUAL_INT(2, rlp_decode(&buf.data, -1, NULL));
  TEST_ASSERT_EQUAL_INT(RLP_ITEM, rlp_decode(&buf.data, 1, &item));
  TEST_ASSERT_EQUAL_UINT8(0x01, item.data[0]);
  buffer_free(&items);
  buffer_free(&buf);
}

void test_rlp_truncated_short_on_skip_path(void) {
  uint8_t raw[] = {0x01, 0x83, 'd', 'o'}; // second item claims 3 payload bytes
  bytes_t src   = bytes(raw, sizeof(raw));
  bytes_t out   = {0};
  TEST_ASSERT_EQUAL_INT(RLP_ITEM, rlp_decode(&src, 0, &out));
  TEST_ASSERT_EQUAL_INT(RLP_OUT_OF_RANGE, rlp_decode(&src, 1, &out));
  TEST_ASSERT_EQUAL_INT(RLP_OUT_OF_RANGE, rlp_decode(&src, -1, NULL));
}

void test_rlp_truncated_long_list_prefix_on_skip_path(void) {
  uint8_t raw[] = {0x01, 0xf8};
  bytes_t src   = bytes(raw, sizeof(raw));
  bytes_t out   = {0};
  TEST_ASSERT_EQUAL_INT(RLP_ITEM, rlp_decode(&src, 0, &out));
  TEST_ASSERT_EQUAL_INT(RLP_OUT_OF_RANGE, rlp_decode(&src, 1, &out));
  TEST_ASSERT_EQUAL_INT(RLP_OUT_OF_RANGE, rlp_decode(&src, -1, NULL));
}

void test_rlp_get_uint64_rejects_non_item(void) {
  buffer_t list = {0};
  rlp_add_list(&list, NULL_BYTES);
  TEST_ASSERT_EQUAL_UINT64(0, rlp_get_uint64(list.data, 0));
  buffer_free(&list);

  uint8_t nine[9];
  memset(nine, 0xff, sizeof(nine));
  buffer_t buf = {0};
  rlp_add_item(&buf, bytes(nine, sizeof(nine)));
  TEST_ASSERT_EQUAL_UINT64(0, rlp_get_uint64(buf.data, 0));
  buffer_free(&buf);

  buf = (buffer_t) {0};
  rlp_add_uint64(&buf, 0xffffffffffffffffULL);
  TEST_ASSERT_EQUAL_UINT64(0xffffffffffffffffULL, rlp_get_uint64(buf.data, 0));
  buffer_free(&buf);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_rlp_encode_decode_single_byte);
  RUN_TEST(test_rlp_encode_decode_empty_string);
  RUN_TEST(test_rlp_encode_decode_short_string);
  RUN_TEST(test_rlp_encode_decode_empty_list);
  RUN_TEST(test_rlp_encode_decode_short_list);
  RUN_TEST(test_rlp_to_list_wraps_encoded_items);
  RUN_TEST(test_rlp_nested_list_yellow_paper);
  RUN_TEST(test_rlp_encode_decode_long_string);
  RUN_TEST(test_rlp_encode_decode_long_list);
  RUN_TEST(test_rlp_add_uint_and_uint64);
  RUN_TEST(test_rlp_decode_index_on_concatenated_items);
  RUN_TEST(test_rlp_truncated_short_string);
  RUN_TEST(test_rlp_truncated_short_list);
  RUN_TEST(test_rlp_truncated_long_string_prefix);
  RUN_TEST(test_rlp_truncated_long_string_len_bytes);
  RUN_TEST(test_rlp_truncated_long_string_payload);
  RUN_TEST(test_rlp_truncated_long_list_prefix);
  RUN_TEST(test_rlp_truncated_long_list_len_bytes);
  RUN_TEST(test_rlp_truncated_long_list_payload);
  RUN_TEST(test_rlp_long_len_eight_bytes_truncated_prefix);
  RUN_TEST(test_rlp_huge_long_len_does_not_overflow);
  RUN_TEST(test_rlp_truncated_prefix_on_skip_path);
  RUN_TEST(test_rlp_empty_input);
  RUN_TEST(test_rlp_encode_decode_max_short_string);
  RUN_TEST(test_rlp_encode_decode_two_byte_long_string);
  RUN_TEST(test_rlp_skip_valid_long_items);
  RUN_TEST(test_rlp_truncated_short_on_skip_path);
  RUN_TEST(test_rlp_truncated_long_list_prefix_on_skip_path);
  RUN_TEST(test_rlp_get_uint64_rejects_non_item);
  return UNITY_END();
}
