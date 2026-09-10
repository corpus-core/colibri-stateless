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

#include "../../src/util/bytes.h"
#include "../../src/util/plugin.h"
#include "unity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <unistd.h>
#else
#include <windows.h>
#endif

#ifdef _WIN32
#define setenv(name, value, overwrite) _putenv_s(name, value)
#define unsetenv(name)                 _putenv_s(name, "")
#endif

void setUp(void) {
  // Reset storage config before each test
  storage_plugin_t empty = {0};
  c4_set_storage_config(&empty);
}

void tearDown(void) {
  // Cleanup any test files
  unlink("test_plugin_file.txt");
}

// Test 1: Get default storage config
void test_get_default_storage_config(void) {
  storage_plugin_t plugin = {0};
  c4_get_storage_config(&plugin);

  // Should have default max_sync_states
  TEST_ASSERT_EQUAL(3, plugin.max_sync_states);

#if defined(FILE_STORAGE) || defined(MEMORY_STORAGE)
  // Should have default storage handlers set
  TEST_ASSERT_NOT_NULL(plugin.get);
  TEST_ASSERT_NOT_NULL(plugin.set);
  TEST_ASSERT_NOT_NULL(plugin.del);
#endif
}

// Test 2: Set and get custom config
void test_set_custom_storage_config(void) {
  storage_plugin_t custom = {0};
  custom.max_sync_states  = 5;

  c4_set_storage_config(&custom);

  storage_plugin_t retrieved = {0};
  c4_get_storage_config(&retrieved);

  TEST_ASSERT_EQUAL(5, retrieved.max_sync_states);
}

// Test 3: Set config with zero max_sync_states (should default to 3)
void test_set_config_defaults_max_sync_states(void) {
  storage_plugin_t custom = {0};
  custom.max_sync_states  = 0;

  c4_set_storage_config(&custom);

  storage_plugin_t retrieved = {0};
  c4_get_storage_config(&retrieved);

  // Should default to 3 when set to 0
  TEST_ASSERT_EQUAL(3, retrieved.max_sync_states);
}

void test_storage_name_is_safe(void) {
  TEST_ASSERT_FALSE(c4_storage_name_is_safe(NULL));
  TEST_ASSERT_FALSE(c4_storage_name_is_safe(""));
  TEST_ASSERT_FALSE(c4_storage_name_is_safe("."));
  TEST_ASSERT_FALSE(c4_storage_name_is_safe(".."));
  TEST_ASSERT_FALSE(c4_storage_name_is_safe("../x"));
  TEST_ASSERT_FALSE(c4_storage_name_is_safe("a/b"));
  TEST_ASSERT_FALSE(c4_storage_name_is_safe("a\\b"));
  TEST_ASSERT_FALSE(c4_storage_name_is_safe("/abs"));
  TEST_ASSERT_TRUE(c4_storage_name_is_safe("states_1"));
  TEST_ASSERT_TRUE(c4_storage_name_is_safe("..."));
  TEST_ASSERT_TRUE(c4_storage_name_is_safe("..foo"));
  TEST_ASSERT_TRUE(c4_storage_name_is_safe(".hidden"));
}

#ifdef FILE_STORAGE
// Test 4: File storage - write and read
void test_file_storage_write_and_read(void) {
  storage_plugin_t plugin = {0};
  c4_get_storage_config(&plugin);

  // Write test data
  const char* test_data = "Hello, Plugin Test!";
  bytes_t     data      = {.data = (uint8_t*) test_data, .len = strlen(test_data)};

  plugin.set("test_plugin_file.txt", data);

  // Read it back
  buffer_t read_buffer = {0};
  bool     success     = plugin.get("test_plugin_file.txt", &read_buffer);

  TEST_ASSERT_TRUE(success);
  TEST_ASSERT_EQUAL(strlen(test_data), read_buffer.data.len);
  TEST_ASSERT_EQUAL_MEMORY(test_data, read_buffer.data.data, strlen(test_data));

  buffer_free(&read_buffer);
}

// Test 5: File storage - delete
void test_file_storage_delete(void) {
  storage_plugin_t plugin = {0};
  c4_get_storage_config(&plugin);

  // Write test file
  const char* test_data = "Temporary data";
  bytes_t     data      = {.data = (uint8_t*) test_data, .len = strlen(test_data)};
  plugin.set("test_plugin_file.txt", data);

  // Verify it exists
  buffer_t read_buffer = {0};
  TEST_ASSERT_TRUE(plugin.get("test_plugin_file.txt", &read_buffer));
  buffer_free(&read_buffer);

  // Delete it
  plugin.del("test_plugin_file.txt");

  // Verify it's gone
  buffer_t read_after_delete = {0};
  bool     exists            = plugin.get("test_plugin_file.txt", &read_after_delete);
  TEST_ASSERT_FALSE(exists);
  buffer_free(&read_after_delete);
}

// Test 6: File storage - read non-existent file
void test_file_storage_read_nonexistent(void) {
  storage_plugin_t plugin = {0};
  c4_get_storage_config(&plugin);

  buffer_t read_buffer = {0};
  bool     success     = plugin.get("nonexistent_file_12345.txt", &read_buffer);

  TEST_ASSERT_FALSE(success);
  buffer_free(&read_buffer);
}

// Test 7: File storage with C4_STATES_DIR environment variable
void test_file_storage_with_states_dir(void) {
  // Set temporary directory
  setenv("C4_STATES_DIR", "/tmp", 1);

  storage_plugin_t plugin = {0};
  c4_get_storage_config(&plugin);

  // Write test data
  const char* test_data = "Directory test";
  bytes_t     data      = {.data = (uint8_t*) test_data, .len = strlen(test_data)};

  plugin.set("test_plugin_states_dir.txt", data);

  // Read it back
  buffer_t read_buffer = {0};
  bool     success     = plugin.get("test_plugin_states_dir.txt", &read_buffer);

  TEST_ASSERT_TRUE(success);
  TEST_ASSERT_EQUAL(strlen(test_data), read_buffer.data.len);

  buffer_free(&read_buffer);

  // Cleanup
  plugin.del("test_plugin_states_dir.txt");
  unsetenv("C4_STATES_DIR");
}

// Test 8: File storage rejects path separators and "." / ".."
void test_file_storage_rejects_path_separators(void) {
  char* saved_dir = state_data_dir;
  state_data_dir  = ".";

  storage_plugin_t plugin = {0};
  c4_get_file_storage_plugin(&plugin);

  const char* payload = "should-not-be-written";
  bytes_t     data    = {.data = (uint8_t*) payload, .len = (uint32_t) strlen(payload)};
  const char* escaped = "../c4_plugin_path_escape.txt";
  unlink(escaped);

  plugin.set(escaped, data);
  plugin.set("nested/c4_plugin_path_escape.txt", data);
  plugin.set("nested\\c4_plugin_path_escape.txt", data);
  plugin.set("a/b\\c", data);
  plugin.set("a\\b/c", data);
  plugin.set("/c4_plugin_abs_escape.txt", data);
  plugin.set("..", data);
  plugin.set(".", data);
  plugin.set("", data);
  plugin.set(NULL, data);

  FILE* f = fopen(escaped, "rb");
  TEST_ASSERT_NULL(f);
  if (f) fclose(f);
  unlink(escaped);

  buffer_t buf = {0};
  TEST_ASSERT_FALSE(plugin.get(escaped, &buf));
  TEST_ASSERT_FALSE(plugin.get("nested/file.txt", &buf));
  TEST_ASSERT_FALSE(plugin.get("nested\\file.txt", &buf));
  TEST_ASSERT_FALSE(plugin.get("a/b\\c", &buf));
  TEST_ASSERT_FALSE(plugin.get("a\\b/c", &buf));
  TEST_ASSERT_FALSE(plugin.get("/c4_plugin_abs_escape.txt", &buf));
  TEST_ASSERT_FALSE(plugin.get("..", &buf));
  TEST_ASSERT_FALSE(plugin.get(".", &buf));
  TEST_ASSERT_FALSE(plugin.get("", &buf));
  TEST_ASSERT_FALSE(plugin.get(NULL, &buf));
  buffer_free(&buf);

  plugin.del(escaped);
  plugin.del("nested/file.txt");
  plugin.del("");
  plugin.del(NULL);

  plugin.set("test_plugin_file.txt", data);
  TEST_ASSERT_TRUE(plugin.get("test_plugin_file.txt", &buf));
  TEST_ASSERT_EQUAL((uint32_t) strlen(payload), buf.data.len);
  buffer_free(&buf);
  plugin.del("test_plugin_file.txt");

  state_data_dir = saved_dir;
}

// Test 9: File storage allows basenames that only look like traversal ("...", "..foo")
void test_file_storage_allows_dot_prefix_basenames(void) {
  char* saved_dir = state_data_dir;
  state_data_dir  = ".";

  storage_plugin_t plugin = {0};
  c4_get_file_storage_plugin(&plugin);

  const char* payload = "safe-dot-name";
  bytes_t     data    = {.data = (uint8_t*) payload, .len = (uint32_t) strlen(payload)};
  const char* names[] = {"...", "..foo"};

  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    unlink(names[i]);
    plugin.set(names[i], data);

    buffer_t buf = {0};
    TEST_ASSERT_TRUE_MESSAGE(plugin.get(names[i], &buf), names[i]);
    TEST_ASSERT_EQUAL_UINT32((uint32_t) strlen(payload), buf.data.len);
    TEST_ASSERT_EQUAL_MEMORY(payload, buf.data.data, strlen(payload));
    buffer_free(&buf);

    plugin.del(names[i]);
    FILE* f = fopen(names[i], "rb");
    TEST_ASSERT_NULL(f);
    if (f) fclose(f);
    unlink(names[i]);
  }

  state_data_dir = saved_dir;
}
#endif

#ifdef MEMORY_STORAGE
// Memory storage - write and read
void test_memory_storage_write_and_read(void) {
  storage_plugin_t plugin = {0};
  c4_get_storage_config(&plugin);

  const char* test_data = "Hello, Memory Storage!";
  bytes_t     data      = {.data = (uint8_t*) test_data, .len = strlen(test_data)};

  plugin.set("memory_key_1", data);

  buffer_t read_buffer = {0};
  bool     success     = plugin.get("memory_key_1", &read_buffer);

  TEST_ASSERT_TRUE(success);
  TEST_ASSERT_EQUAL(strlen(test_data), read_buffer.data.len);
  TEST_ASSERT_EQUAL_MEMORY(test_data, read_buffer.data.data, strlen(test_data));

  buffer_free(&read_buffer);
}

// Memory storage - delete
void test_memory_storage_delete(void) {
  storage_plugin_t plugin = {0};
  c4_get_storage_config(&plugin);

  const char* test_data = "Temporary data";
  bytes_t     data      = {.data = (uint8_t*) test_data, .len = strlen(test_data)};
  plugin.set("memory_key_del", data);

  buffer_t read_buffer = {0};
  TEST_ASSERT_TRUE(plugin.get("memory_key_del", &read_buffer));
  buffer_free(&read_buffer);

  plugin.del("memory_key_del");

  buffer_t read_after_delete = {0};
  bool     exists            = plugin.get("memory_key_del", &read_after_delete);
  TEST_ASSERT_FALSE(exists);
  buffer_free(&read_after_delete);
}

// Memory storage - read non-existent key
void test_memory_storage_read_nonexistent(void) {
  storage_plugin_t plugin = {0};
  c4_get_storage_config(&plugin);

  buffer_t read_buffer = {0};
  bool     success     = plugin.get("nonexistent_memory_key_12345", &read_buffer);

  TEST_ASSERT_FALSE(success);
  buffer_free(&read_buffer);
}
#endif

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_get_default_storage_config);
  RUN_TEST(test_set_custom_storage_config);
  RUN_TEST(test_set_config_defaults_max_sync_states);
  RUN_TEST(test_storage_name_is_safe);

#ifdef FILE_STORAGE
  RUN_TEST(test_file_storage_write_and_read);
  RUN_TEST(test_file_storage_delete);
  RUN_TEST(test_file_storage_read_nonexistent);
  RUN_TEST(test_file_storage_with_states_dir);
  RUN_TEST(test_file_storage_rejects_path_separators);
  RUN_TEST(test_file_storage_allows_dot_prefix_basenames);
#endif

#ifdef MEMORY_STORAGE
  RUN_TEST(test_memory_storage_write_and_read);
  RUN_TEST(test_memory_storage_delete);
  RUN_TEST(test_memory_storage_read_nonexistent);
#endif

  return UNITY_END();
}
