#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Server configure tests
 */
#ifdef HTTP_SERVER
#include "../../src/server/server.h"
#include "unity.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// Declarations
void        c4_configure(int argc, char* argv[]);
const char* c4_get_config_file_path();
int         c4_save_config_file(const char* updates);

static char tmpdir[512];

static void make_tmpdir(void) {
  snprintf(tmpdir, sizeof(tmpdir), "/tmp/c4_cfg_test_%d", getpid());
  mkdir(tmpdir, 0700);
}

static char* write_file(const char* dir, const char* name, const char* content) {
  static char path[1024];
  snprintf(path, sizeof(path), "%s/%s", dir, name);
  FILE* f = fopen(path, "w");
  if (!f) return NULL;
  fputs(content, f);
  fclose(f);
  return path;
}

void setUp(void) {
  make_tmpdir();
}

void tearDown(void) {
  // Best-effort cleanup
  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpdir);
  system(cmd);
}

// Test 1: --help prints usage without exiting (guarded in TEST builds)
void test_configure_help_no_exit(void) {
  // Redirect stderr to a temp file
  char* help_path = write_file(tmpdir, "help.txt", "");
  FILE* f         = freopen(help_path, "w", stderr);
  TEST_ASSERT_NOT_NULL(f);

  char* argv[] = {"prog", "--help"};
  c4_configure(2, argv);

  // Restore stderr
  fflush(stderr);
  freopen("/dev/tty", "w", stderr);

  // Read file
  FILE* rf = fopen(help_path, "r");
  TEST_ASSERT_NOT_NULL(rf);
  char   buf[4096];
  size_t n = fread(buf, 1, sizeof(buf) - 1, rf);
  buf[n]   = 0;
  fclose(rf);

  TEST_ASSERT_NOT_EQUAL(0, n);
  TEST_ASSERT_NOT_NULL(strstr(buf, "Usage:"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "--config"));
}

// Test 2: Env vs Arg precedence
void test_configure_env_vs_arg_precedence(void) {
  setenv("HOST", "1.2.3.4", 1);
  char* argv[] = {"prog", "--host", "0.0.0.0"};
  c4_configure(3, argv);
  TEST_ASSERT_NOT_NULL(http_server.host);
  TEST_ASSERT_EQUAL_STRING("0.0.0.0", http_server.host);
  unsetenv("HOST");
}

// Test 3: Config file load and c4_get_config_file_path
void test_configure_load_config_file(void) {
  char* cfg_path = write_file(tmpdir, "server.conf", "BEACON=https://example-beacon/\nPORT=18090\n");
  TEST_ASSERT_NOT_NULL(cfg_path);

  char* argv[] = {"prog", "--config", cfg_path};
  c4_configure(3, argv);

  const char* loaded = c4_get_config_file_path();
  TEST_ASSERT_NOT_NULL(loaded);
  TEST_ASSERT_EQUAL_STRING(cfg_path, loaded);
  TEST_ASSERT_NOT_NULL(http_server.beacon_nodes);
  TEST_ASSERT_EQUAL_STRING("https://example-beacon/", http_server.beacon_nodes);
  TEST_ASSERT_TRUE(http_server.port == 18090);
}

// Test 4: Save config updates
void test_configure_save_updates(void) {
  char* cfg_path = write_file(tmpdir, "server.conf", "PORT=8090\nWEB_UI_ENABLED=0\n");
  TEST_ASSERT_NOT_NULL(cfg_path);
  char* argv[] = {"prog", "--config", cfg_path};
  c4_configure(3, argv);

  int rc = c4_save_config_file("PORT=12345\nWEB_UI_ENABLED=1\n");
  TEST_ASSERT_EQUAL(0, rc);

  // Verify file contains updates
  FILE* f = fopen(cfg_path, "r");
  TEST_ASSERT_NOT_NULL(f);
  char   buf[2048];
  size_t n = fread(buf, 1, sizeof(buf) - 1, f);
  buf[n]   = 0;
  fclose(f);
  TEST_ASSERT_NOT_NULL(strstr(buf, "PORT=12345"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "WEB_UI_ENABLED=1"));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_configure_help_no_exit);
  RUN_TEST(test_configure_env_vs_arg_precedence);
  RUN_TEST(test_configure_load_config_file);
  RUN_TEST(test_configure_save_updates);
  return UNITY_END();
}

#else // !HTTP_SERVER

// Stub main when HTTP_SERVER is not enabled
int main(void) {
  fprintf(stderr, "test_server_configure: Skipped (HTTP_SERVER not enabled)\n");
  return 0;
}

#endif // HTTP_SERVER