#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Server configure tests
 */
#ifdef HTTP_SERVER
#include "../../src/server/io/configure.h"
#include "../../src/server/server.h"
#include "unity.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifndef _WIN32
#include <unistd.h>
#else
#include "../../src/util/win_compat.h"
#include <direct.h>
#include <process.h>
#define getpid      _getpid
#define mkdir(p, m) _mkdir(p)
#endif

// Declarations
void        c4_configure(int argc, char* argv[]);
const char* c4_get_config_file_path();
int         c4_save_config_file(const char* updates);

static char tmpdir[512];

static void make_tmpdir(void) {
#ifdef _WIN32
  const char* base = getenv("TEMP");
  if (!base || !*base) base = ".";
  snprintf(tmpdir, sizeof(tmpdir), "%s/c4_cfg_test_%d", base, getpid());
  mkdir(tmpdir, 0700);
#else
  snprintf(tmpdir, sizeof(tmpdir), "/tmp/c4_cfg_test_%d", getpid());
  mkdir(tmpdir, 0700);
#endif
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
  unsetenv("C4_TEST_INT");
  unsetenv("C4_TEST_U64");
  unsetenv("PORT");
  unsetenv("HOST");
}

void tearDown(void) {
  unsetenv("C4_TEST_INT");
  unsetenv("C4_TEST_U64");
  unsetenv("PORT");
  unsetenv("HOST");
  // Best-effort cleanup
  char cmd[1024];
#ifdef _WIN32
  snprintf(cmd, sizeof(cmd), "rmdir /s /q \"%s\"", tmpdir);
#else
  snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpdir);
#endif
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
#ifdef _WIN32
  freopen("CON", "w", stderr);
#else
  freopen("/dev/tty", "w", stderr);
#endif

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
  FILE* f2 = fopen(cfg_path, "r");
  TEST_ASSERT_NOT_NULL(f2);
  char   buf[2048];
  size_t n = fread(buf, 1, sizeof(buf) - 1, f2);
  buf[n]   = 0;
  fclose(f2);
  TEST_ASSERT_NOT_NULL(strstr(buf, "PORT=12345"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "WEB_UI_ENABLED=1"));
}

static void unset_test_int_env(void) {
  unsetenv("C4_TEST_INT");
  unsetenv("C4_TEST_U64");
  unsetenv("PORT");
}

// conf_int via CLI: overflow must not wrap into a value inside [min, max].
void test_conf_int_overflow_does_not_wrap(void) {
  unset_test_int_env();
  int   target = 42;
  char* argv[] = {"prog", "--test_int=4295032830"}; // 2^32 + 65534 would wrap to 65534
  c4_init_config(2, argv);
  TEST_ASSERT_EQUAL_INT(1, conf_int(&target, "C4_TEST_INT", "test_int", 0, "test int", 1, 65535));
  TEST_ASSERT_EQUAL_INT(42, target);
}

void test_conf_int_overflow_large_decimal(void) {
  unset_test_int_env();
  int   target = 42;
  char* argv[] = {"prog", "--test_int=9999999999"};
  c4_init_config(2, argv);
  TEST_ASSERT_EQUAL_INT(1, conf_int(&target, "C4_TEST_INT", "test_int", 0, "test int", 1, 65535));
  TEST_ASSERT_EQUAL_INT(42, target);
}

void test_conf_int_overflow_via_env(void) {
  unset_test_int_env();
  setenv("C4_TEST_INT", "4294967297", 1); // 2^32 + 1 would wrap to 1
  int   target = 42;
  char* argv[] = {"prog"};
  c4_init_config(1, argv);
  TEST_ASSERT_EQUAL_INT(1, conf_int(&target, "C4_TEST_INT", "test_int", 0, "test int", 1, 65535));
  TEST_ASSERT_EQUAL_INT(42, target);
  unset_test_int_env();
}

void test_conf_int_rejects_trailing_garbage(void) {
  unset_test_int_env();
  int   target = 42;
  char* argv[] = {"prog", "--test_int=8080abc"};
  c4_init_config(2, argv);
  TEST_ASSERT_EQUAL_INT(1, conf_int(&target, "C4_TEST_INT", "test_int", 0, "test int", 1, 65535));
  TEST_ASSERT_EQUAL_INT(42, target);
}

void test_conf_int_rejects_empty_and_out_of_range(void) {
  unset_test_int_env();
  int   target       = 42;
  char* argv_empty[] = {"prog", "--test_int="};
  c4_init_config(2, argv_empty);
  TEST_ASSERT_EQUAL_INT(1, conf_int(&target, "C4_TEST_INT", "test_int", 0, "test int", 1, 65535));
  TEST_ASSERT_EQUAL_INT(42, target);

  char* argv_hi[] = {"prog", "--test_int=65536"};
  c4_init_config(2, argv_hi);
  TEST_ASSERT_EQUAL_INT(1, conf_int(&target, "C4_TEST_INT", "test_int", 0, "test int", 1, 65535));
  TEST_ASSERT_EQUAL_INT(42, target);

  char* argv_lo[] = {"prog", "--test_int=0"};
  c4_init_config(2, argv_lo);
  TEST_ASSERT_EQUAL_INT(1, conf_int(&target, "C4_TEST_INT", "test_int", 0, "test int", 1, 65535));
  TEST_ASSERT_EQUAL_INT(42, target);
}

void test_conf_int_accepts_valid_bounds(void) {
  unset_test_int_env();
  int   target     = 42;
  char* argv_min[] = {"prog", "--test_int=1"};
  c4_init_config(2, argv_min);
  TEST_ASSERT_EQUAL_INT(0, conf_int(&target, "C4_TEST_INT", "test_int", 0, "test int", 1, 65535));
  TEST_ASSERT_EQUAL_INT(1, target);

  char* argv_max[] = {"prog", "--test_int=65535"};
  c4_init_config(2, argv_max);
  TEST_ASSERT_EQUAL_INT(0, conf_int(&target, "C4_TEST_INT", "test_int", 0, "test int", 1, 65535));
  TEST_ASSERT_EQUAL_INT(65535, target);
}

// atoi("-4294967295") wraps to 1, which is inside [1, 65535].
void test_conf_int_negative_overflow_does_not_wrap(void) {
  unset_test_int_env();
  int   target = 42;
  char* argv[] = {"prog", "--test_int=-4294967295"};
  c4_init_config(2, argv);
  TEST_ASSERT_EQUAL_INT(1, conf_int(&target, "C4_TEST_INT", "test_int", 0, "test int", 1, 65535));
  TEST_ASSERT_EQUAL_INT(42, target);
}

// Two-token form must hit the same parser; atoi("4295032830") wraps to 65534.
void test_conf_int_overflow_two_arg_form(void) {
  unset_test_int_env();
  int   target = 42;
  char* argv[] = {"prog", "--test_int", "4295032830"};
  c4_init_config(3, argv);
  TEST_ASSERT_EQUAL_INT(1, conf_int(&target, "C4_TEST_INT", "test_int", 0, "test int", 1, 65535));
  TEST_ASSERT_EQUAL_INT(42, target);
}

// INT_MAX+1 / INT_MIN-1 wrap to INT_MIN / INT_MAX with atoi; only visible if the
// configured range includes those endpoints.
void test_conf_int_rejects_int_max_plus_one(void) {
  unset_test_int_env();
  int   target    = 42;
  char* argv_hi[] = {"prog", "--test_int=2147483648"}; // INT_MAX + 1
  c4_init_config(2, argv_hi);
  TEST_ASSERT_EQUAL_INT(1, conf_int(&target, "C4_TEST_INT", "test_int", 0, "test int", INT_MIN, INT_MAX));
  TEST_ASSERT_EQUAL_INT(42, target);

  char* argv_lo[] = {"prog", "--test_int=-2147483649"}; // INT_MIN - 1
  c4_init_config(2, argv_lo);
  TEST_ASSERT_EQUAL_INT(1, conf_int(&target, "C4_TEST_INT", "test_int", 0, "test int", INT_MIN, INT_MAX));
  TEST_ASSERT_EQUAL_INT(42, target);
}

void test_conf_uint64_overflow_and_sign_rejected(void) {
  unset_test_int_env();
  uint64_t target = 7;
  char*    argv[] = {"prog", "--test_u64=18446744073709551616"}; // 2^64
  c4_init_config(2, argv);
  TEST_ASSERT_EQUAL_INT(1, conf_uint64(&target, "C4_TEST_U64", "test_u64", 0, "test u64", 1, UINT64_MAX));
  TEST_ASSERT_EQUAL_UINT64(7, target);

  char* argv_neg[] = {"prog", "--test_u64=-1"};
  c4_init_config(2, argv_neg);
  TEST_ASSERT_EQUAL_INT(1, conf_uint64(&target, "C4_TEST_U64", "test_u64", 0, "test u64", 1, UINT64_MAX));
  TEST_ASSERT_EQUAL_UINT64(7, target);

  // atoll("+1") succeeds; unsigned parser must reject the sign.
  char* argv_plus[] = {"prog", "--test_u64=+1"};
  c4_init_config(2, argv_plus);
  TEST_ASSERT_EQUAL_INT(1, conf_uint64(&target, "C4_TEST_U64", "test_u64", 0, "test u64", 1, UINT64_MAX));
  TEST_ASSERT_EQUAL_UINT64(7, target);
}

// Integration: --port overflow must not bind to a wrapped port.
void test_configure_port_overflow_keeps_default(void) {
  unsetenv("PORT");
  char* argv_ok[] = {"prog", "--port", "8090"};
  c4_configure(3, argv_ok);
  TEST_ASSERT_EQUAL_INT(8090, http_server.port);

  char* argv_wrap[] = {"prog", "--port=4295032830"};
  c4_configure(2, argv_wrap);
  TEST_ASSERT_EQUAL_INT(8090, http_server.port);

  char* argv_overflow[] = {"prog", "--port=9999999999"};
  c4_configure(2, argv_overflow);
  TEST_ASSERT_EQUAL_INT(8090, http_server.port);

  char* argv_space[] = {"prog", "--port", "4294967297"};
  c4_configure(3, argv_space);
  TEST_ASSERT_EQUAL_INT(8090, http_server.port);

  // Two-token large decimal (atoi wraps to 1410065407, still out of port range).
  char* argv_space_overflow[] = {"prog", "--port", "9999999999"};
  c4_configure(3, argv_space_overflow);
  TEST_ASSERT_EQUAL_INT(8090, http_server.port);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_configure_help_no_exit);
  RUN_TEST(test_configure_env_vs_arg_precedence);
  RUN_TEST(test_configure_load_config_file);
  RUN_TEST(test_configure_save_updates);
  RUN_TEST(test_conf_int_overflow_does_not_wrap);
  RUN_TEST(test_conf_int_overflow_large_decimal);
  RUN_TEST(test_conf_int_overflow_via_env);
  RUN_TEST(test_conf_int_rejects_trailing_garbage);
  RUN_TEST(test_conf_int_rejects_empty_and_out_of_range);
  RUN_TEST(test_conf_int_accepts_valid_bounds);
  RUN_TEST(test_conf_int_negative_overflow_does_not_wrap);
  RUN_TEST(test_conf_int_overflow_two_arg_form);
  RUN_TEST(test_conf_int_rejects_int_max_plus_one);
  RUN_TEST(test_conf_uint64_overflow_and_sign_rejected);
  RUN_TEST(test_configure_port_overflow_keeps_default);
  return UNITY_END();
}

#else // !HTTP_SERVER

// Stub main when HTTP_SERVER is not enabled
int main(void) {
  fprintf(stderr, "test_server_configure: Skipped (HTTP_SERVER not enabled)\n");
  return 0;
}

#endif // HTTP_SERVER