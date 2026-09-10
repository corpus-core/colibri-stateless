/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: MIT
 *
 * Server file-storage key sanitization (same rules as plugin.c).
 *
 * Safe-key set/del are not exercised here: ram_storage_set/del schedule
 * uv_fs_open / uv_fs_unlink on the default loop. Positive get of a
 * pre-created file is sync fopen only and is covered.
 */

#include "unity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HTTP_SERVER

#include "../../src/server/server.h"
#include "../../src/util/plugin.h"

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#define c4_mkdir(p) mkdir((p), 0755)
#define c4_rmdir(p) rmdir(p)
#else
#include <direct.h>
#include <io.h>
#include <windows.h>
#define unsetenv(name)                 _putenv_s(name, "")
#define setenv(name, value, overwrite) _putenv_s(name, value)
#define c4_mkdir(p)                    _mkdir(p)
#define c4_rmdir(p)                    _rmdir(p)
#endif

#define STATES_DIR "c4_server_storage_states_dir"
#define BAIT_FILE  "c4_server_escape_bait.txt"
#define BAIT_KEY   "../c4_server_escape_bait.txt"
#define BAIT_BODY  "SECRET-SHOULD-NOT-BE-READ"

static void write_file(const char* path, const char* contents) {
  FILE* f = fopen(path, "wb");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
  fwrite(contents, 1, strlen(contents), f);
  fclose(f);
}

static void assert_file_contents(const char* path, const char* expected) {
  FILE* f = fopen(path, "rb");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
  char   buf[256] = {0};
  size_t n        = fread(buf, 1, sizeof(buf) - 1, f);
  fclose(f);
  TEST_ASSERT_EQUAL_UINT32((uint32_t) strlen(expected), (uint32_t) n);
  TEST_ASSERT_EQUAL_STRING(expected, buf);
}

static void join_states(char* out, size_t out_len, const char* name) {
  snprintf(out, out_len, "%s/%s", STATES_DIR, name);
}

static void cleanup_states_dir(void) {
  char        path[256];
  const char* leftovers[] = {"c4_server_safe.txt", "...", "..foo", ".hidden"};
  for (size_t i = 0; i < sizeof(leftovers) / sizeof(leftovers[0]); i++) {
    join_states(path, sizeof(path), leftovers[i]);
    unlink(path);
  }
  c4_rmdir(STATES_DIR);
}

/* Rejected keys must not reach RAM cache or the filesystem. Unity aborts the
 * test function if is_safe() is unexpectedly true, so set() is never called
 * for a valid key (which would schedule uv_fs_open). */
static void assert_key_rejected(storage_plugin_t* plugin, bytes_t data, const char* key, const char* msg) {
  char* mutable_key = (char*) key;
  TEST_ASSERT_FALSE_MESSAGE(c4_storage_name_is_safe(key), msg);
  plugin->set(mutable_key, data);

  buffer_t buf = {0};
  TEST_ASSERT_FALSE_MESSAGE(plugin->get(mutable_key, &buf), msg);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, buf.data.len, msg);
  buffer_free(&buf);

  plugin->del(mutable_key);
}

void setUp(void) {
  unsetenv("C4_STATES_DIR");
  storage_plugin_t empty = {0};
  c4_set_storage_config(&empty);
  c4_clear_storage_cache();
  c4_init_server_storage();
}

void tearDown(void) {
  c4_clear_storage_cache();
  storage_plugin_t empty = {0};
  c4_set_storage_config(&empty);
  unsetenv("C4_STATES_DIR");
  unlink(BAIT_FILE);
  unlink("../c4_server_path_escape.txt");
  cleanup_states_dir();
}

void test_server_storage_rejects_path_separators(void) {
  storage_plugin_t plugin = {0};
  c4_get_storage_config(&plugin);
  TEST_ASSERT_NOT_NULL(plugin.get);
  TEST_ASSERT_NOT_NULL(plugin.set);
  TEST_ASSERT_NOT_NULL(plugin.del);

  const char* payload = "should-not-be-written";
  bytes_t     data    = {.data = (uint8_t*) payload, .len = (uint32_t) strlen(payload)};
  const char* escaped = "../c4_server_path_escape.txt";
  unlink(escaped);

  struct {
    const char* key;
    const char* msg;
  } cases[] = {
      {escaped, "parent traversal"},
      {"nested/c4_server_path_escape.txt", "forward slash"},
      {"nested\\c4_server_path_escape.txt", "backslash"},
      {"a/b\\c", "mixed separators slash-backslash"},
      {"a\\b/c", "mixed separators backslash-slash"},
      {"/c4_server_abs_escape.txt", "absolute path"},
      {"./x", "dot-slash prefix"},
      {"foo/../bar", "embedded parent traversal"},
      {"foo/", "trailing slash"},
      {"..\\x", "windows parent traversal"},
      {"..", "dot-dot"},
      {".", "dot"},
      {"", "empty"},
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    assert_key_rejected(&plugin, data, cases[i].key, cases[i].msg);

  assert_key_rejected(&plugin, data, NULL, "NULL key");

  FILE* f = fopen(escaped, "rb");
  TEST_ASSERT_NULL(f);
  if (f) fclose(f);
  unlink(escaped);
}

void test_server_storage_get_del_do_not_touch_existing_escape_files(void) {
  storage_plugin_t plugin = {0};
  c4_get_storage_config(&plugin);

  const char* payload = "poison";
  bytes_t     data    = {.data = (uint8_t*) payload, .len = (uint32_t) strlen(payload)};

  unlink(BAIT_FILE);
  write_file(BAIT_FILE, BAIT_BODY);

  /* set/get/del of the traversal key must not overwrite, read, or delete bait. */
  TEST_ASSERT_FALSE(c4_storage_name_is_safe(BAIT_KEY));
  plugin.set(BAIT_KEY, data);

  buffer_t buf = {0};
  TEST_ASSERT_FALSE(plugin.get(BAIT_KEY, &buf));
  TEST_ASSERT_EQUAL_UINT32(0, buf.data.len);
  buffer_free(&buf);

  plugin.del(BAIT_KEY);

  assert_file_contents(BAIT_FILE, BAIT_BODY);
  unlink(BAIT_FILE);
}

void test_server_storage_states_dir_rejects_parent_escape(void) {
  storage_plugin_t plugin = {0};
  c4_get_storage_config(&plugin);

  cleanup_states_dir();
  c4_mkdir(STATES_DIR);
  setenv("C4_STATES_DIR", STATES_DIR, 1);

  unlink(BAIT_FILE);
  write_file(BAIT_FILE, BAIT_BODY);

  const char* payload = "poison";
  bytes_t     data    = {.data = (uint8_t*) payload, .len = (uint32_t) strlen(payload)};

  /* With C4_STATES_DIR set, get_file_path concatenates base + "/" + key.
   * "../bait" would resolve to cwd/bait if sanitization were skipped. */
  assert_key_rejected(&plugin, data, BAIT_KEY, "states dir parent escape");
  assert_key_rejected(&plugin, data, "./x", "states dir dot-slash");
  assert_key_rejected(&plugin, data, "nested/x", "states dir nested");

  assert_file_contents(BAIT_FILE, BAIT_BODY);
  unlink(BAIT_FILE);
  cleanup_states_dir();
  unsetenv("C4_STATES_DIR");
}

void test_server_storage_get_allows_safe_basenames_from_disk(void) {
  storage_plugin_t plugin = {0};
  c4_get_storage_config(&plugin);

  cleanup_states_dir();
  c4_mkdir(STATES_DIR);
  setenv("C4_STATES_DIR", STATES_DIR, 1);

  struct {
    const char* name;
    const char* body;
  } files[] = {
      {"c4_server_safe.txt", "ok-safe"},
      {"...", "ok-triple-dot"},
      {"..foo", "ok-dotdot-foo"},
      {".hidden", "ok-hidden"},
  };

  char path[256];
  for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
    TEST_ASSERT_TRUE_MESSAGE(c4_storage_name_is_safe(files[i].name), files[i].name);
    join_states(path, sizeof(path), files[i].name);
    unlink(path);
    write_file(path, files[i].body);
  }

  for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
    buffer_t buf = {0};
    TEST_ASSERT_TRUE_MESSAGE(plugin.get((char*) files[i].name, &buf), files[i].name);
    TEST_ASSERT_EQUAL_UINT32((uint32_t) strlen(files[i].body), buf.data.len);
    TEST_ASSERT_EQUAL_MEMORY(files[i].body, buf.data.data, strlen(files[i].body));
    buffer_free(&buf);
  }

  /* Second get hits RAM cache after the file is gone (still no uv write). */
  join_states(path, sizeof(path), files[0].name);
  unlink(path);
  {
    buffer_t cached = {0};
    TEST_ASSERT_TRUE(plugin.get((char*) files[0].name, &cached));
    TEST_ASSERT_EQUAL_UINT32((uint32_t) strlen(files[0].body), cached.data.len);
    TEST_ASSERT_EQUAL_MEMORY(files[0].body, cached.data.data, strlen(files[0].body));
    buffer_free(&cached);
  }

  cleanup_states_dir();
  unsetenv("C4_STATES_DIR");
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_server_storage_rejects_path_separators);
  RUN_TEST(test_server_storage_get_del_do_not_touch_existing_escape_files);
  RUN_TEST(test_server_storage_states_dir_rejects_parent_escape);
  RUN_TEST(test_server_storage_get_allows_safe_basenames_from_disk);
  return UNITY_END();
}

#else // !HTTP_SERVER

int main(void) {
  fprintf(stderr, "test_server_storage: Skipped (HTTP_SERVER not enabled)\n");
  return 0;
}

#endif // HTTP_SERVER
