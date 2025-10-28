/*
 * Server config API tests: GET /config, POST /config, GET /config.html
 */

#include "unity.h"

#ifdef HTTP_SERVER

#include "../../src/server/server.h"
#include "test_server_helper.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// from configure.c
void        c4_configure(int argc, char* argv[]);
const char* c4_get_config_file_path();

static char tmp_cfg[512];

static char* write_file(const char* path, const char* content) {
  FILE* f = fopen(path, "w");
  if (!f) return NULL;
  fputs(content, f);
  fclose(f);
  return (char*) path;
}

void setUp(void) {
  // Prepare a temp config file
  snprintf(tmp_cfg, sizeof(tmp_cfg), "/tmp/c4_cfg_api_%d.conf", getpid());
  // Enable Web UI for tests; set a specific port value
  write_file(tmp_cfg, "WEB_UI_ENABLED=1\nPORT=28545\n");

  // Call configure to register params and set current_config_file_path
  char* argv[] = {"prog", "--config", tmp_cfg};
  c4_configure(3, argv);

  // Start server with web_ui enabled
  http_server_t config  = (http_server_t) {0};
  config.port           = TEST_PORT;
  config.host           = TEST_HOST;
  config.chain_id       = 1;
  config.web_ui_enabled = 1;
  c4_test_server_setup(&config);
}

void tearDown(void) {
  c4_test_server_teardown();
  remove(tmp_cfg);
}

void test_get_config_returns_parameters(void) {
  int   status_code = 0;
  char* response    = send_http_request("GET", "/config", NULL, &status_code);
  TEST_ASSERT_NOT_NULL(response);
  TEST_ASSERT_EQUAL(200, status_code);
  char* body = extract_json_body(response);
  TEST_ASSERT_NOT_NULL(body);
  TEST_ASSERT_NOT_NULL(strstr(body, "\"parameters\""));
  // Should include "port" entry
  TEST_ASSERT_NOT_NULL(strstr(body, "\"name\": \"port\""));
  free(body);
  free(response);
}

void test_post_config_updates_file(void) {
  // Build payload to update PORT
  const char* payload =
      "{\"parameters\":[{\"env\":\"PORT\",\"value\":\"29999\"}]}";

  int   status_code = 0;
  char* response    = send_http_request("POST", "/config", payload, &status_code);
  TEST_ASSERT_NOT_NULL(response);
  TEST_ASSERT_EQUAL(200, status_code);
  char* body = extract_json_body(response);
  TEST_ASSERT_NOT_NULL(body);
  TEST_ASSERT_NOT_NULL(strstr(body, "\"success\": true"));
  TEST_ASSERT_NOT_NULL(strstr(body, "\"restart_required\": true"));
  free(body);
  free(response);

  // Verify config file got updated
  const char* cfg_path = c4_get_config_file_path();
  TEST_ASSERT_NOT_NULL(cfg_path);
  FILE* f = fopen(cfg_path, "r");
  TEST_ASSERT_NOT_NULL(f);
  char   buf[2048];
  size_t n = fread(buf, 1, sizeof(buf) - 1, f);
  buf[n]   = 0;
  fclose(f);
  TEST_ASSERT_NOT_NULL(strstr(buf, "PORT=29999"));
}

void test_get_config_html_served(void) {
  int   status_code = 0;
  char* response    = send_http_request("GET", "/config.html", NULL, &status_code);
  TEST_ASSERT_NOT_NULL(response);
  TEST_ASSERT_EQUAL(200, status_code);
  // Content-Length header present and >0
  TEST_ASSERT_NOT_NULL(strstr(response, "Content-Length: "));
  free(response);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_get_config_returns_parameters);
  RUN_TEST(test_post_config_updates_file);
  RUN_TEST(test_get_config_html_served);
  return UNITY_END();
}

#else
int main(void) {
  fprintf(stderr, "test_server_config_api: Skipped (HTTP_SERVER not enabled)\n");
  return 0;
}
#endif
