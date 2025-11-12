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
#include "ssz.h"
#include "unity.h"
void setUp(void) {
}

void tearDown(void) {
}

void test_json() {

  // simple parser
  buffer_t buffer = {0};
  json_t   json   = json_parse("{\"name\": \"John\", \"age\": 30}");
  TEST_ASSERT_EQUAL_STRING("John", json_as_string(json_get(json, "name"), &buffer));
  TEST_ASSERT_EQUAL_INT(30, json_as_uint32(json_get(json, "age")));

  // escaped strings
  json = json_parse("{\"name\": \"John\\\"\", \"age\": 30}");
  TEST_ASSERT_EQUAL_STRING("John\"", json_as_string(json_get(json, "name"), &buffer));

  buffer_reset(&buffer);
  TEST_ASSERT_EQUAL_STRING("{\"name\": \"John\\\"\"}", bprintf(&buffer, "{\"name\": \"%S\"}", "John\""));

  // validation (cached): valid case should pass and be cached
  json            = json_parse("{\"n\":5,\"b\":true,\"h\":\"0x12\"}");
  const char* err = json_validate_cached(json, "{n:uint,b:bool,h:bytes}", "json");
  TEST_ASSERT_NULL(err);
  // second call with same value+schema should hit the cache and also succeed
  err = json_validate_cached(json, "{n:uint,b:bool,h:bytes}", "json");
  TEST_ASSERT_NULL(err);

  // same value with different schema order: still valid (cache miss acceptable)
  err = json_validate_cached(json, "{b:bool,n:uint,h:bytes}", "json");
  TEST_ASSERT_NULL(err);

  // invalid hex should fail and not be cached as success
  json = json_parse("{\"h\":\"0xzz\"}");
  err  = json_validate_cached(json, "{h:bytes}", "json");
  TEST_ASSERT_NOT_NULL(err);
  safe_free((char*) err);

  // cleanup
  buffer_free(&buffer);
}

void test_buffer() {
  // Test: buffer_append mit dynamischer Allokation
  buffer_t buf   = {0};
  bytes_t  data1 = bytes("Hello", 5);
  bytes_t  data2 = bytes(" World", 6);

  TEST_ASSERT_EQUAL_UINT32(5, buffer_append(&buf, data1));
  TEST_ASSERT_EQUAL_UINT32(5, buf.data.len);
  TEST_ASSERT_EQUAL_MEMORY("Hello", buf.data.data, 5);

  TEST_ASSERT_EQUAL_UINT32(6, buffer_append(&buf, data2));
  TEST_ASSERT_EQUAL_UINT32(11, buf.data.len);
  TEST_ASSERT_EQUAL_MEMORY("Hello World", buf.data.data, 11);

  buffer_free(&buf);

  // Test: buffer mit initialer Größe
  buffer_t buf2 = buffer_for_size(100);
  TEST_ASSERT_EQUAL_INT32(100, buf2.allocated);
  TEST_ASSERT_EQUAL_UINT32(0, buf2.data.len);
  buffer_append(&buf2, bytes("Test", 4));
  TEST_ASSERT_EQUAL_UINT32(4, buf2.data.len);
  buffer_free(&buf2);

  // Test: Fixed-size buffer (stack buffer)
  uint8_t  stack_data[10];
  buffer_t stack_buf = stack_buffer(stack_data);
  TEST_ASSERT_TRUE(stack_buf.allocated < 0);

  buffer_append(&stack_buf, bytes("12345", 5));
  TEST_ASSERT_EQUAL_UINT32(5, stack_buf.data.len);

  // Sollte auf max Größe begrenzt werden
  buffer_append(&stack_buf, bytes("67890ABCDE", 10));
  TEST_ASSERT_EQUAL_UINT32(10, stack_buf.data.len);
  TEST_ASSERT_EQUAL_MEMORY("1234567890", stack_buf.data.data, 10);

  // Test: buffer_splice - Einfügen
  buffer_t buf3 = {0};
  buffer_append(&buf3, bytes("HelloWorld", 10));
  buffer_splice(&buf3, 5, 0, bytes(" ", 1));
  TEST_ASSERT_EQUAL_UINT32(11, buf3.data.len);
  TEST_ASSERT_EQUAL_MEMORY("Hello World", buf3.data.data, 11);
  buffer_free(&buf3);

  // Test: buffer_splice - Ersetzen
  buffer_t buf4 = {0};
  buffer_append(&buf4, bytes("Hello World", 11));
  buffer_splice(&buf4, 6, 5, bytes("C", 1));
  TEST_ASSERT_EQUAL_UINT32(7, buf4.data.len);
  TEST_ASSERT_EQUAL_MEMORY("Hello C", buf4.data.data, 7);
  buffer_free(&buf4);

  // Test: buffer_splice - Löschen
  buffer_t buf5 = {0};
  buffer_append(&buf5, bytes("Hello World", 11));
  buffer_splice(&buf5, 5, 6, bytes(NULL, 0));
  TEST_ASSERT_EQUAL_UINT32(5, buf5.data.len);
  TEST_ASSERT_EQUAL_MEMORY("Hello", buf5.data.data, 5);
  buffer_free(&buf5);

  // Test: buffer_add_chars
  buffer_t buf6 = {0};
  buffer_add_chars(&buf6, "Test");
  TEST_ASSERT_EQUAL_UINT32(4, buf6.data.len);
  TEST_ASSERT_EQUAL_STRING("Test", buffer_as_string(buf6));
  buffer_free(&buf6);

  // Test: bprintf %S - Escaped String
  buffer_t buf7 = {0};
  bprintf(&buf7, "%S", "Hello \"World\"\n");
  TEST_ASSERT_EQUAL_STRING("Hello \\\"World\\\"\\n", buffer_as_string(buf7));
  buffer_free(&buf7);

  // Test: bprintf %S mit Kontrollzeichen
  buffer_t buf8 = {0};
  bprintf(&buf8, "%S", "Tab:\tBackslash:\\Quote:\"");
  TEST_ASSERT_EQUAL_STRING("Tab:\\tBackslash:\\\\Quote:\\\"", buffer_as_string(buf8));
  buffer_free(&buf8);

  // Test: buffer_add_be
  buffer_t buf10 = {0};
  buffer_add_be(&buf10, 0x12345678, 4);
  TEST_ASSERT_EQUAL_UINT32(4, buf10.data.len);
  TEST_ASSERT_EQUAL_HEX8(0x12, buf10.data.data[0]);
  TEST_ASSERT_EQUAL_HEX8(0x34, buf10.data.data[1]);
  TEST_ASSERT_EQUAL_HEX8(0x56, buf10.data.data[2]);
  TEST_ASSERT_EQUAL_HEX8(0x78, buf10.data.data[3]);
  buffer_free(&buf10);

  // Test: buffer_add_le
  buffer_t buf11 = {0};
  buffer_add_le(&buf11, 0x12345678, 4);
  TEST_ASSERT_EQUAL_UINT32(4, buf11.data.len);
  TEST_ASSERT_EQUAL_HEX8(0x78, buf11.data.data[0]);
  TEST_ASSERT_EQUAL_HEX8(0x56, buf11.data.data[1]);
  TEST_ASSERT_EQUAL_HEX8(0x34, buf11.data.data[2]);
  TEST_ASSERT_EQUAL_HEX8(0x12, buf11.data.data[3]);
  buffer_free(&buf11);

  // Test: buffer_add_bytes
  buffer_t buf12 = {0};
  buffer_add_bytes(&buf12, 5, 0x01, 0x02, 0x03, 0x04, 0x05);
  TEST_ASSERT_EQUAL_UINT32(5, buf12.data.len);
  TEST_ASSERT_EQUAL_HEX8(0x01, buf12.data.data[0]);
  TEST_ASSERT_EQUAL_HEX8(0x05, buf12.data.data[4]);
  buffer_free(&buf12);

  // Test: buffer_append mit NULL data (sollte Nullen schreiben)
  buffer_t buf13 = {0};
  buffer_append(&buf13, bytes(NULL, 5));
  TEST_ASSERT_EQUAL_UINT32(5, buf13.data.len);
  TEST_ASSERT_TRUE(bytes_all_zero(buf13.data));
  buffer_free(&buf13);

  // Test: buffer_reset
  buffer_t buf14 = {0};
  buffer_append(&buf14, bytes("Test", 4));
  buffer_reset(&buf14);
  TEST_ASSERT_EQUAL_UINT32(0, buf14.data.len);
  TEST_ASSERT_NOT_NULL(buf14.data.data); // Data sollte noch allokiert sein
  buffer_free(&buf14);
}

void test_le_be() {
  // Test: uint16_from_le
  uint8_t data16le[] = {0x34, 0x12};
  TEST_ASSERT_EQUAL_HEX16(0x1234, uint16_from_le(data16le));

  // Test: uint32_from_le
  uint8_t data32le[] = {0x78, 0x56, 0x34, 0x12};
  TEST_ASSERT_EQUAL_HEX32(0x12345678, uint32_from_le(data32le));

  // Test: uint32_from_le mit unaligned Pointer
  uint8_t unaligned_data[] = {0xFF, 0x78, 0x56, 0x34, 0x12};
  TEST_ASSERT_EQUAL_HEX32(0x12345678, uint32_from_le(unaligned_data + 1));

  // Test: uint64_from_le
  uint8_t data64le[] = {0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11};
  TEST_ASSERT_EQUAL_HEX64(0x1122334455667788ULL, uint64_from_le(data64le));

  // Test: uint64_from_be
  uint8_t data64be[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
  TEST_ASSERT_EQUAL_HEX64(0x1122334455667788ULL, uint64_from_be(data64be));

  // Test: uint64_to_be
  uint8_t result64be[8];
  uint64_to_be(result64be, 0x1122334455667788ULL);
  TEST_ASSERT_EQUAL_HEX8(0x11, result64be[0]);
  TEST_ASSERT_EQUAL_HEX8(0x22, result64be[1]);
  TEST_ASSERT_EQUAL_HEX8(0x88, result64be[7]);

  // Test: uint64_to_le
  uint8_t result64le[8];
  uint64_to_le(result64le, 0x1122334455667788ULL);
  TEST_ASSERT_EQUAL_HEX8(0x88, result64le[0]);
  TEST_ASSERT_EQUAL_HEX8(0x77, result64le[1]);
  TEST_ASSERT_EQUAL_HEX8(0x11, result64le[7]);

  // Test: uint32_to_le
  uint8_t result32le[4];
  uint32_to_le(result32le, 0x12345678);
  TEST_ASSERT_EQUAL_HEX8(0x78, result32le[0]);
  TEST_ASSERT_EQUAL_HEX8(0x56, result32le[1]);
  TEST_ASSERT_EQUAL_HEX8(0x34, result32le[2]);
  TEST_ASSERT_EQUAL_HEX8(0x12, result32le[3]);

  // Test: bytes_as_le
  uint8_t bytes_le[] = {0x01, 0x02, 0x03, 0x04};
  TEST_ASSERT_EQUAL_HEX64(0x04030201ULL, bytes_as_le(bytes(bytes_le, 4)));

  // Test: bytes_as_le mit 1 Byte
  uint8_t single_byte[] = {0xAB};
  TEST_ASSERT_EQUAL_HEX64(0xAB, bytes_as_le(bytes(single_byte, 1)));

  // Test: bytes_as_be
  uint8_t bytes_be[] = {0x01, 0x02, 0x03, 0x04};
  TEST_ASSERT_EQUAL_HEX64(0x01020304ULL, bytes_as_be(bytes(bytes_be, 4)));

  // Test: bytes_as_be mit 8 Bytes
  uint8_t bytes_be8[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
  TEST_ASSERT_EQUAL_HEX64(0x1122334455667788ULL, bytes_as_be(bytes(bytes_be8, 8)));

  // Test: Edge Case - Zero-Werte
  uint8_t zeros[8] = {0};
  TEST_ASSERT_EQUAL_HEX64(0, uint64_from_le(zeros));
  TEST_ASSERT_EQUAL_HEX64(0, uint64_from_be(zeros));

  // Test: Edge Case - Max-Werte
  uint8_t max_val[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  TEST_ASSERT_EQUAL_HEX64(0xFFFFFFFFFFFFFFFFULL, uint64_from_le(max_val));
  TEST_ASSERT_EQUAL_HEX64(0xFFFFFFFFFFFFFFFFULL, uint64_from_be(max_val));

  // Test: Roundtrip uint64_to_le -> uint64_from_le
  uint8_t  roundtrip[8];
  uint64_t original = 0xABCDEF0123456789ULL;
  uint64_to_le(roundtrip, original);
  TEST_ASSERT_EQUAL_HEX64(original, uint64_from_le(roundtrip));

  // Test: Roundtrip uint64_to_be -> uint64_from_be
  uint64_to_be(roundtrip, original);
  TEST_ASSERT_EQUAL_HEX64(original, uint64_from_be(roundtrip));
}

void test_bprintf() {
  buffer_t buf = {0};

  // Test: %s - String
  buffer_reset(&buf);
  bprintf(&buf, "Hello %s", "World");
  TEST_ASSERT_EQUAL_STRING("Hello World", buffer_as_string(buf));

  // Test: %S - Escaped String
  buffer_reset(&buf);
  bprintf(&buf, "%S", "Hello \"World\"");
  TEST_ASSERT_EQUAL_STRING("Hello \\\"World\\\"", buffer_as_string(buf));

  // Test: %x - Bytes as Hex
  buffer_reset(&buf);
  uint8_t hex_data[] = {0xDE, 0xAD, 0xBE, 0xEF};
  bprintf(&buf, "%x", bytes(hex_data, 4));
  TEST_ASSERT_EQUAL_STRING("deadbeef", buffer_as_string(buf));

  // Test: %u - Bytes as Hex ohne leading zeros
  buffer_reset(&buf);
  uint8_t hex_data2[] = {0x00, 0x00, 0x12, 0x34};
  bprintf(&buf, "%u", bytes(hex_data2, 4));
  TEST_ASSERT_EQUAL_STRING("1234", buffer_as_string(buf));

  // Test: %u - Edge Case: nur Nullen (sollte "0" bleiben)
  buffer_reset(&buf);
  uint8_t zeros[] = {0x00, 0x00};
  bprintf(&buf, "%u", bytes(zeros, 2));
  TEST_ASSERT_EQUAL_STRING("0", buffer_as_string(buf));

  // Test: %l - uint64_t als Dezimalzahl
  buffer_reset(&buf);
  bprintf(&buf, "%l", 1234567890123456789ULL);
  TEST_ASSERT_EQUAL_STRING("1234567890123456789", buffer_as_string(buf));

  // Test: %lx - uint64_t als Hex
  buffer_reset(&buf);
  bprintf(&buf, "0x%lx", 0xDEADBEEFCAFEBABEULL);
  TEST_ASSERT_EQUAL_STRING("0xdeadbeefcafebabe", buffer_as_string(buf));

  // Test: %d - uint32_t als Dezimalzahl
  buffer_reset(&buf);
  bprintf(&buf, "%d", 42u);
  TEST_ASSERT_EQUAL_STRING("42", buffer_as_string(buf));

  // Test: %dx - uint32_t als Hex
  buffer_reset(&buf);
  bprintf(&buf, "0x%dx", 0xABCDu);
  TEST_ASSERT_EQUAL_STRING("0xabcd", buffer_as_string(buf));

  // Test: %c - Einzelnes Zeichen
  buffer_reset(&buf);
  bprintf(&buf, "%c%c%c", 'A', 'B', 'C');
  TEST_ASSERT_EQUAL_STRING("ABC", buffer_as_string(buf));

  // Test: Kombinierte Formatierung
  buffer_reset(&buf);
  bprintf(&buf, "Number: %d, Hex: %dx, String: %s", 255u, 0xFFu, "test");
  TEST_ASSERT_EQUAL_STRING("Number: 255, Hex: ff, String: test", buffer_as_string(buf));

  // Test: Mehrere %s hintereinander
  buffer_reset(&buf);
  bprintf(&buf, "%s %s %s", "One", "Two", "Three");
  TEST_ASSERT_EQUAL_STRING("One Two Three", buffer_as_string(buf));

  // Test: Text ohne Format-Specifier
  buffer_reset(&buf);
  bprintf(&buf, "Just plain text");
  TEST_ASSERT_EQUAL_STRING("Just plain text", buffer_as_string(buf));

  // Test: Leerer String
  buffer_reset(&buf);
  bprintf(&buf, "");
  TEST_ASSERT_EQUAL_STRING("", buffer_as_string(buf));

  // Test: Mix von allem
  buffer_reset(&buf);
  uint8_t mix_data[] = {0x12, 0x34};
  bprintf(&buf, "Str:%s Num:%d Hex:%x Char:%c", "test", 123u, bytes(mix_data, 2), '!');
  TEST_ASSERT_EQUAL_STRING("Str:test Num:123 Hex:1234 Char:!", buffer_as_string(buf));

  // Test: %l mit Zero
  buffer_reset(&buf);
  bprintf(&buf, "%l", 0ULL);
  TEST_ASSERT_EQUAL_STRING("0", buffer_as_string(buf));

  // Test: %d mit Zero
  buffer_reset(&buf);
  bprintf(&buf, "%d", 0u);
  TEST_ASSERT_EQUAL_STRING("0", buffer_as_string(buf));

  // Test: %S mit Kontrollzeichen
  buffer_reset(&buf);
  bprintf(&buf, "%S", "\t\n\r\b\f");
  TEST_ASSERT_EQUAL_STRING("\\t\\n\\r\\b\\f", buffer_as_string(buf));

  // Test: Escaped Backslash
  buffer_reset(&buf);
  bprintf(&buf, "%S", "C:\\path\\to\\file");
  TEST_ASSERT_EQUAL_STRING("C:\\\\path\\\\to\\\\file", buffer_as_string(buf));

  buffer_free(&buf);
}

void test_bprintf_extended() {
  buffer_t buf = {0};

  // Test: %f - Double with default precision (false = remove trailing zeros)
  buffer_reset(&buf);
  bprintf(&buf, "%f", 3.14159);
  TEST_ASSERT_EQUAL_STRING("3.14159", buffer_as_string(buf));

  // Test: %f - Double with trailing zeros removed
  buffer_reset(&buf);
  bprintf(&buf, "%f", 10.0);
  TEST_ASSERT_EQUAL_STRING("10", buffer_as_string(buf));

  // Test: %r - Raw bytes as string
  buffer_reset(&buf);
  uint8_t raw_data[] = {'H', 'e', 'l', 'l', 'o'};
  bprintf(&buf, "%r", bytes(raw_data, 5));
  TEST_ASSERT_EQUAL_STRING("Hello", buffer_as_string(buf));

  // Test: %% - Escape percent sign
  buffer_reset(&buf);
  bprintf(&buf, "100%% complete");
  TEST_ASSERT_EQUAL_STRING("100% complete", buffer_as_string(buf));

  // Test: Edge Case - Single % at end of string
  buffer_reset(&buf);
  bprintf(&buf, "test%");
  TEST_ASSERT_EQUAL_STRING("test%", buffer_as_string(buf));

  // Test: Edge Case - Unknown format specifier (should be ignored/printed as-is)
  buffer_reset(&buf);
  bprintf(&buf, "test%_");
  // Should output "test" and stop at unknown format
  TEST_ASSERT_TRUE(buf.data.len >= 4);

  // Test: Edge Case - Multiple %% in sequence
  buffer_reset(&buf);
  bprintf(&buf, "%%d %%s %%l");
  TEST_ASSERT_EQUAL_STRING("%d %s %l", buffer_as_string(buf));

  buffer_free(&buf);
}

void test_bprintf_json_ssz() {
  buffer_t buf = {0};

  // Test: %j - JSON without quotes for strings
  buffer_reset(&buf);
  bprintf(&buf, "value:%j", json_parse("\"val\""));
  TEST_ASSERT_EQUAL_STRING("value:val", buffer_as_string(buf));

  // Test: %J - JSON with quotes for strings
  buffer_reset(&buf);
  bprintf(&buf, "value:%J", json_parse("\"val\""));
  TEST_ASSERT_EQUAL_STRING("value:\"val\"", buffer_as_string(buf));

  // Test: %j - JSON with number (no quotes)
  buffer_reset(&buf);
  bprintf(&buf, "value:%j", json_parse("5"));
  TEST_ASSERT_EQUAL_STRING("value:5", buffer_as_string(buf));

  // Test: %j - JSON with array
  buffer_reset(&buf);
  bprintf(&buf, "value:%j", json_parse("[]"));
  TEST_ASSERT_EQUAL_STRING("value:[]", buffer_as_string(buf));

  // Test: %J - JSON with object
  buffer_reset(&buf);
  bprintf(&buf, "data:%J", json_parse("{\"key\":\"value\"}"));
  TEST_ASSERT_EQUAL_STRING("data:{\"key\":\"value\"}", buffer_as_string(buf));

  // Test: %z - SSZ as decimal number
  ssz_def_t def = SSZ_UINT("test", 4); // uint32_t
  uint8_t   data[4];
  uint32_to_le(data, 15);
  ssz_ob_t ob = {.def = &def, .bytes = bytes(data, 4)};

  buffer_reset(&buf);
  bprintf(&buf, "%z", ob);
  TEST_ASSERT_EQUAL_STRING("15", buffer_as_string(buf));

  // Test: %Z - SSZ as hex without leading zeros
  buffer_reset(&buf);
  bprintf(&buf, "%Z", ob);
  TEST_ASSERT_EQUAL_STRING("\"0xf\"", buffer_as_string(buf));

  // Test: %z - SSZ with larger number
  uint32_to_le(data, 255);
  ob.bytes = bytes(data, 4);
  buffer_reset(&buf);
  bprintf(&buf, "%z", ob);
  TEST_ASSERT_EQUAL_STRING("255", buffer_as_string(buf));

  // Test: %Z - SSZ with larger number
  buffer_reset(&buf);
  bprintf(&buf, "%Z", ob);
  TEST_ASSERT_EQUAL_STRING("\"0xff\"", buffer_as_string(buf));

  buffer_free(&buf);
}

void test_sbprintf() {
  // Test: sbprintf with stack buffer
  char name[32];
  sbprintf(name, "test_%d", 123u);
  TEST_ASSERT_EQUAL_STRING("test_123", name);

  // Test: sbprintf with multiple format specifiers
  char key[64];
  sbprintf(key, "chain_%l_period_%d", 1234567890ULL, 42u);
  TEST_ASSERT_EQUAL_STRING("chain_1234567890_period_42", key);

  // Test: sbprintf with hex
  char hex_key[32];
  sbprintf(hex_key, "0x%lx", 0xDEADBEEFULL);
  TEST_ASSERT_EQUAL_STRING("0xdeadbeef", hex_key);

  // Test: sbprintf - Buffer limit enforcement
  char small[10];
  memset(small, 'X', sizeof(small)); // Fill with X to detect overflow
  small[9] = '\0';                   // Ensure null terminator at end
  sbprintf(small, "This is a very long string that should be truncated");
  // Should be truncated to fit in buffer (9 chars + null terminator max)
  size_t actual_len = strlen(small);
  // If actual_len > 9, we have a buffer overflow!
  TEST_ASSERT_TRUE_MESSAGE(actual_len <= 9, "Buffer overflow detected");
  // Check that we didn't write past the buffer
  TEST_ASSERT_TRUE(actual_len < sizeof(small));

  // Test: sbprintf - Exact fit
  char exact[6];
  sbprintf(exact, "12345");
  TEST_ASSERT_EQUAL_STRING("12345", exact);

  // Test: sbprintf with bytes
  char    hex_buf[20];
  uint8_t data[] = {0xAB, 0xCD, 0xEF};
  sbprintf(hex_buf, "%x", bytes(data, 3));
  TEST_ASSERT_EQUAL_STRING("abcdef", hex_buf);

  // Test: sbprintf - Multiple writes to same buffer
  char reused[32];
  sbprintf(reused, "first_%d", 1u);
  TEST_ASSERT_EQUAL_STRING("first_1", reused);
  sbprintf(reused, "second_%d", 2u);
  TEST_ASSERT_EQUAL_STRING("second_2", reused);

  // Test: sbprintf with empty string
  char empty[16];
  sbprintf(empty, "");
  TEST_ASSERT_EQUAL_STRING("", empty);

  // Test: sbprintf - Boundary test with numbers
  char num_buf[20]; // Use 20-byte buffer to hold full number + guard bytes
  char guard_before = 'G';
  char guard_after  = 'G';
  memset(num_buf, 0, sizeof(num_buf));
  sbprintf(num_buf, "%l", 1234567890123456ULL); // 16-digit number
  // This should fit exactly in a 17-byte buffer (16 digits + null)
  TEST_ASSERT_EQUAL_STRING("1234567890123456", num_buf);
  TEST_ASSERT_EQUAL_UINT32(16, strlen(num_buf));
  // Verify no buffer overflow occurred
  TEST_ASSERT_EQUAL_CHAR('G', guard_before);
  TEST_ASSERT_EQUAL_CHAR('G', guard_after);
}

void test_fbprintf() {
  // Test: fbprintf to stderr (just ensure it doesn't crash)
  fbprintf(stderr, "Test message: %d\n", 123u);

  // Test: fbprintf with multiple format types
  fbprintf(stdout, "String: %s, Number: %l, Hex: 0x%lx\n", "test", 42ULL, 0xDEADULL);

  // Test: fbprintf with bytes
  uint8_t data[] = {0x01, 0x02, 0x03};
  fbprintf(stdout, "Data: %x\n", bytes(data, 3));

  // Note: We can't easily test the actual output without capturing stdout/stderr,
  // but at least we verify these calls don't crash
}

void test_bytes_helpers() {
  // Test: bytes_eq - gleiche bytes
  uint8_t data1[] = {0x01, 0x02, 0x03, 0x04};
  uint8_t data2[] = {0x01, 0x02, 0x03, 0x04};
  TEST_ASSERT_TRUE(bytes_eq(bytes(data1, 4), bytes(data2, 4)));

  // Test: bytes_eq - unterschiedliche bytes
  uint8_t data3[] = {0x01, 0x02, 0x03, 0x05};
  TEST_ASSERT_FALSE(bytes_eq(bytes(data1, 4), bytes(data3, 4)));

  // Test: bytes_eq - unterschiedliche Längen
  TEST_ASSERT_FALSE(bytes_eq(bytes(data1, 4), bytes(data1, 3)));

  // Test: bytes_eq - leere bytes
  TEST_ASSERT_TRUE(bytes_eq(NULL_BYTES, NULL_BYTES));

  // Test: bytes_all_equal - alle Nullen
  uint8_t zeros[5] = {0, 0, 0, 0, 0};
  TEST_ASSERT_TRUE(bytes_all_equal(bytes(zeros, 5), 0));
  TEST_ASSERT_TRUE(bytes_all_zero(bytes(zeros, 5)));

  // Test: bytes_all_equal - nicht alle gleich
  uint8_t mixed[] = {0xFF, 0xFF, 0xFE, 0xFF};
  TEST_ASSERT_FALSE(bytes_all_equal(bytes(mixed, 4), 0xFF));

  // Test: bytes_all_equal - alle gleich (nicht null)
  uint8_t all_ff[] = {0xFF, 0xFF, 0xFF};
  TEST_ASSERT_TRUE(bytes_all_equal(bytes(all_ff, 3), 0xFF));

  // Test: bytes_dup - duplizieren
  uint8_t original[] = {0xAA, 0xBB, 0xCC, 0xDD};
  bytes_t dup        = bytes_dup(bytes(original, 4));
  TEST_ASSERT_EQUAL_UINT32(4, dup.len);
  TEST_ASSERT_EQUAL_MEMORY(original, dup.data, 4);
  TEST_ASSERT_NOT_EQUAL(original, dup.data); // Unterschiedliche Adressen
  safe_free(dup.data);

  // Test: bytes_remove_leading_zeros
  uint8_t with_zeros[] = {0x00, 0x00, 0x12, 0x34};
  bytes_t no_zeros     = bytes_remove_leading_zeros(bytes(with_zeros, 4));
  TEST_ASSERT_EQUAL_UINT32(2, no_zeros.len);
  TEST_ASSERT_EQUAL_HEX8(0x12, no_zeros.data[0]);
  TEST_ASSERT_EQUAL_HEX8(0x34, no_zeros.data[1]);

  // Test: bytes_remove_leading_zeros - nur Nullen (sollte 1 Byte behalten)
  uint8_t only_zeros[] = {0x00, 0x00, 0x00};
  bytes_t result       = bytes_remove_leading_zeros(bytes(only_zeros, 3));
  TEST_ASSERT_EQUAL_UINT32(1, result.len);
  TEST_ASSERT_EQUAL_HEX8(0x00, result.data[0]);

  // Test: bytes_remove_leading_zeros - keine führenden Nullen
  uint8_t no_leading[] = {0x12, 0x34, 0x56};
  bytes_t unchanged    = bytes_remove_leading_zeros(bytes(no_leading, 3));
  TEST_ASSERT_EQUAL_UINT32(3, unchanged.len);
  TEST_ASSERT_EQUAL_HEX8(0x12, unchanged.data[0]);

  // Test: hex_to_bytes - normale Konvertierung
  uint8_t hex_result[4];
  int     len = hex_to_bytes("12345678", -1, bytes(hex_result, 4));
  TEST_ASSERT_EQUAL_INT(4, len);
  TEST_ASSERT_EQUAL_HEX8(0x12, hex_result[0]);
  TEST_ASSERT_EQUAL_HEX8(0x34, hex_result[1]);
  TEST_ASSERT_EQUAL_HEX8(0x56, hex_result[2]);
  TEST_ASSERT_EQUAL_HEX8(0x78, hex_result[3]);

  // Test: hex_to_bytes - mit 0x Prefix
  uint8_t hex_result2[2];
  len = hex_to_bytes("0xABCD", -1, bytes(hex_result2, 2));
  TEST_ASSERT_EQUAL_INT(2, len);
  TEST_ASSERT_EQUAL_HEX8(0xAB, hex_result2[0]);
  TEST_ASSERT_EQUAL_HEX8(0xCD, hex_result2[1]);

  // Test: hex_to_bytes - ungerade Länge
  uint8_t hex_result3[2];
  len = hex_to_bytes("ABC", -1, bytes(hex_result3, 2));
  TEST_ASSERT_EQUAL_INT(2, len);
  TEST_ASSERT_EQUAL_HEX8(0x0A, hex_result3[0]);
  TEST_ASSERT_EQUAL_HEX8(0xBC, hex_result3[1]);

  // Test: hex_to_bytes - Groß- und Kleinschreibung
  uint8_t hex_result4[2];
  len = hex_to_bytes("AbCd", -1, bytes(hex_result4, 2));
  TEST_ASSERT_EQUAL_INT(2, len);
  TEST_ASSERT_EQUAL_HEX8(0xAB, hex_result4[0]);
  TEST_ASSERT_EQUAL_HEX8(0xCD, hex_result4[1]);

  // Test: hex_to_bytes - leerer String
  uint8_t hex_result5[1];
  len = hex_to_bytes("", 0, bytes(hex_result5, 1));
  TEST_ASSERT_EQUAL_INT(0, len);

  // Test: bytes_slice
  uint8_t slice_data[] = {0x11, 0x22, 0x33, 0x44, 0x55};
  bytes_t sliced       = bytes_slice(bytes(slice_data, 5), 1, 3);
  TEST_ASSERT_EQUAL_UINT32(3, sliced.len);
  TEST_ASSERT_EQUAL_HEX8(0x22, sliced.data[0]);
  TEST_ASSERT_EQUAL_HEX8(0x33, sliced.data[1]);
  TEST_ASSERT_EQUAL_HEX8(0x44, sliced.data[2]);

  // Test: NULL_BYTES
  bytes_t null_bytes = NULL_BYTES;
  TEST_ASSERT_NULL(null_bytes.data);
  TEST_ASSERT_EQUAL_UINT32(0, null_bytes.len);
}

void test_buffer_growth() {
  // Test: Automatisches Wachstum des Buffers
  buffer_t buf = {0};

  // Mehrere kleine Appends, um Wachstumsstrategie zu testen
  for (int i = 0; i < 100; i++) {
    buffer_append(&buf, bytes("X", 1));
  }
  TEST_ASSERT_EQUAL_UINT32(100, buf.data.len);
  TEST_ASSERT_TRUE(buf.allocated >= 100);

  // Überprüfen, dass alle Daten korrekt sind
  for (int i = 0; i < 100; i++) {
    TEST_ASSERT_EQUAL_HEX8('X', buf.data.data[i]);
  }

  buffer_free(&buf);

  // Test: Großer initialer Append
  buffer_t buf2 = {0};
  uint8_t  large_data[1000];
  memset(large_data, 0xAA, 1000);
  buffer_append(&buf2, bytes(large_data, 1000));
  TEST_ASSERT_EQUAL_UINT32(1000, buf2.data.len);
  TEST_ASSERT_TRUE(bytes_all_equal(buf2.data, 0xAA));
  buffer_free(&buf2);

  // Test: Buffer mit vorallokierter Größe
  buffer_t buf3 = buffer_for_size(50);
  TEST_ASSERT_EQUAL_INT32(50, buf3.allocated);
  buffer_append(&buf3, bytes("Test", 4));
  TEST_ASSERT_EQUAL_INT32(50, buf3.allocated); // Sollte nicht gewachsen sein

  // Jetzt über die initiale Größe hinausgehen
  uint8_t extra_data[100];
  memset(extra_data, 0xBB, 100);
  buffer_append(&buf3, bytes(extra_data, 100));
  TEST_ASSERT_EQUAL_UINT32(104, buf3.data.len);
  TEST_ASSERT_TRUE(buf3.allocated >= 104);
  buffer_free(&buf3);
}

void test_edge_cases() {
  // Test: Leerer buffer_append
  buffer_t buf = {0};
  TEST_ASSERT_EQUAL_UINT32(0, buffer_append(&buf, NULL_BYTES));
  TEST_ASSERT_EQUAL_UINT32(0, buf.data.len);

  // Test: buffer_add_chars mit NULL
  buffer_add_chars(&buf, NULL);
  TEST_ASSERT_EQUAL_UINT32(0, buf.data.len);

  // Test: bprintf %S mit NULL
  bprintf(&buf, "%S", (const char*) NULL);
  TEST_ASSERT_EQUAL_UINT32(0, buf.data.len);

  buffer_free(&buf);

  // Test: bytes mit length 0
  uint8_t data[]   = {0x01, 0x02};
  bytes_t zero_len = bytes(data, 0);
  TEST_ASSERT_EQUAL_UINT32(0, zero_len.len);

  // Test: bytes_eq mit verschiedenen Kombinationen
  TEST_ASSERT_TRUE(bytes_eq(bytes(NULL, 0), bytes(NULL, 0)));
  TEST_ASSERT_TRUE(bytes_eq(bytes(data, 0), bytes(data, 0)));

  // Test: hex_to_bytes mit ungültigen Zeichen (sollte -1 zurückgeben)
  uint8_t invalid_result[2];
  int     len = hex_to_bytes("GHIJ", -1, bytes(invalid_result, 2));
  TEST_ASSERT_EQUAL_INT(-1, len);

  // Test: hex_to_bytes mit zu kleinem Buffer (sollte -1 zurückgeben)
  uint8_t small_buffer[1];
  len = hex_to_bytes("12345678", -1, bytes(small_buffer, 1));
  TEST_ASSERT_EQUAL_INT(-1, len);

  // Test: bprintf mit sehr langen Zahlen
  buffer_t buf2 = {0};
  bprintf(&buf2, "%l", 0xFFFFFFFFFFFFFFFFULL);
  TEST_ASSERT_EQUAL_STRING("18446744073709551615", buffer_as_string(buf2));
  buffer_free(&buf2);

  // Test: buffer_splice am Anfang
  buffer_t buf3 = {0};
  buffer_append(&buf3, bytes("World", 5));
  buffer_splice(&buf3, 0, 0, bytes("Hello ", 6));
  buffer_append(&buf3, bytes("", 1)); // Null-Terminator hinzufügen
  buf3.data.len--;
  TEST_ASSERT_EQUAL_STRING("Hello World", buffer_as_string(buf3));
  buffer_free(&buf3);

  // Test: buffer_splice am Ende
  buffer_t buf4 = {0};
  buffer_append(&buf4, bytes("Hello", 5));
  buffer_splice(&buf4, 5, 0, bytes(" World", 6));
  buffer_append(&buf4, bytes("", 1)); // Null-Terminator hinzufügen
  buf4.data.len--;
  TEST_ASSERT_EQUAL_STRING("Hello World", buffer_as_string(buf4));
  buffer_free(&buf4);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_json);
  RUN_TEST(test_bprintf);
  RUN_TEST(test_bprintf_extended);
  RUN_TEST(test_bprintf_json_ssz);
  RUN_TEST(test_sbprintf);
  RUN_TEST(test_fbprintf);
  RUN_TEST(test_le_be);
  RUN_TEST(test_buffer);
  RUN_TEST(test_bytes_helpers);
  RUN_TEST(test_buffer_growth);
  RUN_TEST(test_edge_cases);
  return UNITY_END();
}