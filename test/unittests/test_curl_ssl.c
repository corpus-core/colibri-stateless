/*
 * Regression tests for outbound TLS settings (audit F-EFF82F).
 */

#include "unity.h"

#ifdef HTTP_SERVER

#include "../../src/server/server.h"
#include <curl/curl.h>

static int   saved_tls_insecure;
static char* saved_ca_file;
static char* saved_ca_path;

void setUp(void) {
  saved_tls_insecure = http_server.curl.tls_insecure;
  saved_ca_file      = http_server.curl.ca_file;
  saved_ca_path      = http_server.curl.ca_path;
}

void tearDown(void) {
  http_server.curl.tls_insecure = saved_tls_insecure;
  http_server.curl.ca_file      = saved_ca_file;
  http_server.curl.ca_path      = saved_ca_path;
}

void test_ssl_options_secure_by_default(void) {
  http_server.curl.tls_insecure = 0;
  http_server.curl.ca_file      = "";
  http_server.curl.ca_path      = "";

  c4_curl_ssl_options_t opt;
  c4_curl_ssl_options(&opt);
  TEST_ASSERT_EQUAL(1L, opt.verify_peer);
  TEST_ASSERT_EQUAL(2L, opt.verify_host);
  TEST_ASSERT_EQUAL(CURL_SSLVERSION_TLSv1_2, opt.ssl_version);
  TEST_ASSERT_NULL(opt.ca_file);
  TEST_ASSERT_NULL(opt.ca_path);
}

void test_ssl_options_insecure_disables_verification(void) {
  http_server.curl.tls_insecure = 1;
  http_server.curl.ca_file      = "";
  http_server.curl.ca_path      = "";

  c4_curl_ssl_options_t opt;
  c4_curl_ssl_options(&opt);
  TEST_ASSERT_EQUAL(0L, opt.verify_peer);
  TEST_ASSERT_EQUAL(0L, opt.verify_host);
  TEST_ASSERT_EQUAL(CURL_SSLVERSION_TLSv1_2, opt.ssl_version);
}

void test_ssl_options_custom_ca(void) {
  http_server.curl.tls_insecure = 0;
  http_server.curl.ca_file      = (char*) "/etc/ssl/cert.pem";
  http_server.curl.ca_path      = (char*) "/etc/ssl/certs";

  c4_curl_ssl_options_t opt;
  c4_curl_ssl_options(&opt);
  TEST_ASSERT_EQUAL(1L, opt.verify_peer);
  TEST_ASSERT_EQUAL(2L, opt.verify_host);
  TEST_ASSERT_EQUAL_STRING("/etc/ssl/cert.pem", opt.ca_file);
  TEST_ASSERT_EQUAL_STRING("/etc/ssl/certs", opt.ca_path);
}

void test_ssl_options_null_out_is_safe(void) {
  c4_curl_ssl_options(NULL);
}

void test_ssl_options_null_ca_uses_system_default(void) {
  http_server.curl.tls_insecure = 0;
  http_server.curl.ca_file      = NULL;
  http_server.curl.ca_path      = NULL;

  c4_curl_ssl_options_t opt;
  c4_curl_ssl_options(&opt);
  TEST_ASSERT_EQUAL(1L, opt.verify_peer);
  TEST_ASSERT_EQUAL(2L, opt.verify_host);
  TEST_ASSERT_NULL(opt.ca_file);
  TEST_ASSERT_NULL(opt.ca_path);
}

void test_configure_ssl_accepts_easy_handle(void) {
  http_server.curl.tls_insecure = 0;
  http_server.curl.ca_file      = "";
  http_server.curl.ca_path      = "";

  CURL* easy = curl_easy_init();
  TEST_ASSERT_NOT_NULL(easy);
  c4_curl_configure_ssl(easy);
  curl_easy_cleanup(easy);
}

void test_configure_ssl_null_easy_is_safe(void) {
  c4_curl_configure_ssl(NULL);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_ssl_options_secure_by_default);
  RUN_TEST(test_ssl_options_insecure_disables_verification);
  RUN_TEST(test_ssl_options_custom_ca);
  RUN_TEST(test_ssl_options_null_out_is_safe);
  RUN_TEST(test_ssl_options_null_ca_uses_system_default);
  RUN_TEST(test_configure_ssl_accepts_easy_handle);
  RUN_TEST(test_configure_ssl_null_easy_is_safe);
  return UNITY_END();
}

#else // !HTTP_SERVER

int main(void) {
  UNITY_BEGIN();
  return UNITY_END();
}

#endif
