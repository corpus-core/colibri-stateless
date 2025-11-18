/*
 * Tests for uv_util.c (asynchronous multi-file read/write)
 */

#include "unity.h"

#ifndef HTTP_SERVER
int main(void) {
  fprintf(stderr, "test_uv_util: Skipped (HTTP_SERVER not enabled)\n");
  return 0;
}
#endif

#ifndef NO_LIBUV
#include "server/uv_util.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define MKDIR(p) mkdir(p, 0755)
#else
#include <windows.h>
static int win_mkdir(const char* path) {
  if (CreateDirectoryA(path, NULL)) return 0;
  DWORD err = GetLastError();
  return (err == ERROR_ALREADY_EXISTS) ? 0 : -1;
}
#define MKDIR(p) win_mkdir(p)
#endif

#ifndef NO_LIBUV

static volatile int g_done = 0;

static void run_loop_until_done(unsigned int max_iters) {
  for (unsigned int i = 0; i < max_iters && !g_done; i++) {
    uv_run(uv_default_loop(), UV_RUN_NOWAIT);
#ifndef _WIN32
    usleep(1000);
#else
    Sleep(1);
#endif
  }
}

static void reset_done(void) {
  g_done = 0;
}

static void read_cb(void* user_data, file_data_t* files, int num_files) {
  (void) user_data;
  // Verification is in the test body via user_data if needed
  // Just signal completion; tests will inspect files in place
  c4_file_data_array_free(files, num_files, 1);
  g_done = 1;
}

static void write_cb(void* user_data, file_data_t* files, int num_files) {
  (void) user_data;
  // Do not free file data buffers here (owned by caller), only the container
  c4_file_data_array_free(files, num_files, 0);
  g_done = 1;
}

static void ensure_dir(const char* path) {
  // naive mkdir -p for one level (sufficient for our test layout)
  MKDIR(path);
}

typedef struct {
  file_data_t* out;
  int          n;
} capture_t;

static void read_cb_capture(void* user_data, file_data_t* rfiles, int n) {
  capture_t* c = (capture_t*) user_data;
  c->out       = rfiles;
  c->n         = n;
  g_done       = 1;
}

void setUp(void) {}
void tearDown(void) {}

// Test 1: read multiple files (one present with content, one missing, one empty)
void test_uv_util_read_multi(void) {
  reset_done();

  char base_dir[512];
  snprintf(base_dir, sizeof(base_dir), "%s/uv_util_read", TESTDATA_DIR);
  ensure_dir(base_dir);

  char path_a[512], path_b[512], path_c[512];
  snprintf(path_a, sizeof(path_a), "%s/A.bin", base_dir);
  snprintf(path_b, sizeof(path_b), "%s/B.bin", base_dir); // will be missing
  snprintf(path_c, sizeof(path_c), "%s/C.bin", base_dir); // empty file

  // Prepare A with content
  {
    FILE* f = fopen(path_a, "wb");
    TEST_ASSERT_NOT_NULL(f);
    const char* msg = "hello";
    fwrite(msg, 1, strlen(msg), f);
    fclose(f);
  }
  // Prepare C as empty existing file
  {
    FILE* f = fopen(path_c, "wb");
    TEST_ASSERT_NOT_NULL(f);
    fclose(f);
  }

  file_data_t* files = (file_data_t*) safe_calloc(3, sizeof(file_data_t));
  files[0].path      = strdup(path_a);
  files[0].offset    = 0;
  files[0].limit     = 0; // all
  files[1].path      = strdup(path_b);
  files[1].offset    = 0;
  files[1].limit     = 0;
  files[2].path      = strdup(path_c);
  files[2].offset    = 0;
  files[2].limit     = 0;

  int rc = c4_read_files_uv(NULL, read_cb, files, 3);
  TEST_ASSERT_EQUAL(0, rc);
  // The util makes a heap copy; free our temporary container only
  safe_free(files);

  run_loop_until_done(1000);
  TEST_ASSERT_TRUE(g_done);
  // We cannot access 'files' here; the callback received its own copy and freed by caller.
  // Re-run a second read to inspect results within the callback context by capturing user_data.

  reset_done();
  capture_t cap = {0};

  file_data_t* files2 = (file_data_t*) safe_calloc(3, sizeof(file_data_t));
  files2[0].path      = strdup(path_a);
  files2[0].offset    = 0;
  files2[0].limit     = 0;
  files2[1].path      = strdup(path_b);
  files2[1].offset    = 0;
  files2[1].limit     = 0;
  files2[2].path      = strdup(path_c);
  files2[2].offset    = 0;
  files2[2].limit     = 0;

  rc = c4_read_files_uv(&cap, read_cb_capture, files2, 3);
  TEST_ASSERT_EQUAL(0, rc);
  safe_free(files2);

  run_loop_until_done(1000);
  TEST_ASSERT_TRUE(g_done);
  TEST_ASSERT_EQUAL(3, cap.n);
  // A: data == "hello"
  TEST_ASSERT_NULL(cap.out[0].error);
  TEST_ASSERT_EQUAL(5, cap.out[0].data.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY("hello", cap.out[0].data.data, 5);
  // B: missing -> error set, no data
  TEST_ASSERT_NOT_NULL(cap.out[1].error);
  TEST_ASSERT_EQUAL(0, cap.out[1].data.len);
  // C: empty existing -> data.len == 0, no error
  TEST_ASSERT_NULL(cap.out[2].error);
  TEST_ASSERT_EQUAL(0, cap.out[2].data.len);

  c4_file_data_array_free(cap.out, cap.n, 1);
}

// Test 2: write multiple files (one valid path, one invalid nested directory)
void test_uv_util_write_multi(void) {
  reset_done();

  char base_dir[512];
  snprintf(base_dir, sizeof(base_dir), "%s/uv_util_write", TESTDATA_DIR);
  ensure_dir(base_dir);

  char path_ok[512], path_bad[512];
  snprintf(path_ok, sizeof(path_ok), "%s/out1.bin", base_dir);
  snprintf(path_bad, sizeof(path_bad), "%s/missing/sub/out2.bin", base_dir); // parent doesn't exist

  file_data_t* files = (file_data_t*) safe_calloc(2, sizeof(file_data_t));
  files[0].path      = strdup(path_ok);
  files[0].offset    = 0;
  files[0].limit     = 0;
  const char* msg    = "payload-123";
  files[0].data      = bytes((uint8_t*) msg, (uint32_t) strlen(msg));

  files[1].path   = strdup(path_bad);
  files[1].offset = 0;
  files[1].limit  = 0;
  files[1].data   = bytes((uint8_t*) "x", 1);

  int rc = c4_write_files_uv(NULL, write_cb, files, 2, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  TEST_ASSERT_EQUAL(0, rc);
  // util copies array; free our container only
  safe_free(files);

  run_loop_until_done(1000);
  TEST_ASSERT_TRUE(g_done);

  // Verify OK file exists and content matches
  {
    FILE* f = fopen(path_ok, "rb");
    TEST_ASSERT_NOT_NULL(f);
    char   buf[64] = {0};
    size_t n       = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    TEST_ASSERT_EQUAL(strlen(msg), n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(msg, buf, n);
  }
  // Verify bad file not created
  {
    FILE* f = fopen(path_bad, "rb");
    TEST_ASSERT_NULL(f);
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_uv_util_read_multi);
  RUN_TEST(test_uv_util_write_multi);
  return UNITY_END();
}

#else
int main(void) {
  fprintf(stderr, "test_uv_util: Skipped (libuv not available)\n");
  return 0;
}
#endif