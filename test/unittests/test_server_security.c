/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: MIT
 *
 * Security tests for C4 HTTP Server
 * Tests input validation, injection attacks, and resource exhaustion
 */

#include "test_server_helper.h"
#include "unity.h"
#include <arpa/inet.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

// Unity test framework callbacks
void setUp(void) {
  c4_test_server_setup(NULL);
}

void tearDown(void) {
  c4_test_server_teardown();
}

// ============================================================================
// P0 Critical Security Tests
// ============================================================================

// Test 1: Buffer Overflow - Very Large Content-Length
void test_oversized_content_length(void) {
  c4_test_server_seed_for_test("security_oversized");

  // Attempt to send request with huge Content-Length (but small actual body)
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  TEST_ASSERT_TRUE(sock >= 0);

  struct sockaddr_in server_addr;
  server_addr.sin_family      = AF_INET;
  server_addr.sin_port        = htons(TEST_PORT);
  server_addr.sin_addr.s_addr = inet_addr(TEST_HOST);

  int ret = connect(sock, (struct sockaddr*) &server_addr, sizeof(server_addr));
  TEST_ASSERT_EQUAL(0, ret);

  // Send request with 1GB Content-Length but only send small body
  const char* request = "POST /rpc HTTP/1.1\r\n"
                        "Host: localhost\r\n"
                        "Content-Type: application/json\r\n"
                        "Content-Length: 1073741824\r\n" // 1GB
                        "\r\n"
                        "{\"small\":\"body\"}";

  ssize_t sent = send(sock, request, strlen(request), 0);
  TEST_ASSERT_TRUE(sent > 0);

  // Set recv timeout (2 seconds) to avoid hanging
  struct timeval tv;
  tv.tv_sec  = 2;
  tv.tv_usec = 0;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  // Server should reject with 413 Payload Too Large
  char    response[2048] = {0};
  ssize_t received       = recv(sock, response, sizeof(response) - 1, 0);

  TEST_ASSERT_TRUE_MESSAGE(received > 0, "Server should send error response for oversized Content-Length");

  // Debug output
  if (received < 0) {
    printf("DEBUG: recv returned %zd (errno: %d)\n", received, errno);
  }
  else {
    printf("DEBUG: Response: %.200s\n", response);
  }

  // Should get 413 Payload Too Large
  bool has_413       = strstr(response, "413") != NULL;
  bool has_error_msg = strstr(response, "too large") != NULL || strstr(response, "Payload Too Large") != NULL;

  TEST_ASSERT_TRUE_MESSAGE(has_413, "Should return 413 status code");
  TEST_ASSERT_TRUE_MESSAGE(has_error_msg, "Error message should mention payload size");

  close(sock);
}

// Test 2: Path Traversal Attack
void test_path_traversal(void) {
  c4_test_server_seed_for_test("security_path_traversal");

  int   status_code = 0;
  char* response    = send_http_request("GET", "/../../../etc/passwd", NULL, &status_code);

  TEST_ASSERT_NOT_NULL(response);
  // Should reject with 400 or 404, not 200
  TEST_ASSERT_TRUE(status_code == 400 || status_code == 404);
  // Should NOT contain actual file content
  TEST_ASSERT_TRUE(strstr(response, "root:") == NULL);

  free(response);
}

// Test 3: Header Injection (CRLF)
void test_header_injection_crlf(void) {
  c4_test_server_seed_for_test("security_crlf");

  int sock = socket(AF_INET, SOCK_STREAM, 0);
  TEST_ASSERT_TRUE(sock >= 0);

  struct sockaddr_in server_addr;
  server_addr.sin_family      = AF_INET;
  server_addr.sin_port        = htons(TEST_PORT);
  server_addr.sin_addr.s_addr = inet_addr(TEST_HOST);

  connect(sock, (struct sockaddr*) &server_addr, sizeof(server_addr));

  // Try to inject extra headers via CRLF
  const char* request = "GET /health HTTP/1.1\r\n"
                        "Host: localhost\r\nX-Injected: malicious\r\n\r\nGET /rpc HTTP/1.1\r\n"
                        "\r\n";

  send(sock, request, strlen(request), 0);

  char response[2048] = {0};
  recv(sock, response, sizeof(response) - 1, 0);

  // Should only get ONE response, not two
  // Count "HTTP/" occurrences - should be exactly 1
  int   http_count = 0;
  char* pos        = response;
  while ((pos = strstr(pos, "HTTP/")) != NULL) {
    http_count++;
    pos += 5;
  }
  TEST_ASSERT_EQUAL_MESSAGE(1, http_count, "CRLF injection should not create multiple responses");

  close(sock);
}

// Test 4: Invalid JSON - Malformed Payloads
void test_invalid_json(void) {
  c4_test_server_seed_for_test("security_invalid_json");

  const char* invalid_jsons[] = {
      "{invalid}",                // Missing quotes
      "{\"key\": }",              // Missing value
      "{\"key\": \"value\"",      // Missing closing brace
      "[[[[[",                    // Unclosed arrays
      "{\"a\":1, \"a\":2}",       // Duplicate keys (valid JSON but suspicious)
      "",                         // Empty body
      "not json at all",          // Plain text
      "{\"method\": null}",       // Null method
      "{\"params\": \"string\"}", // Params should be array/object
  };

  for (size_t i = 0; i < sizeof(invalid_jsons) / sizeof(invalid_jsons[0]); i++) {
    int   status_code = 0;
    char* response    = send_http_request("POST", "/rpc", invalid_jsons[i], &status_code);

    TEST_ASSERT_NOT_NULL_MESSAGE(response, "Should return error response, not crash");
    TEST_ASSERT_TRUE_MESSAGE(status_code == 400 || status_code == 500,
                             "Should return 400/500 for invalid JSON");

    free(response);
  }
}

// Test 5: Deeply Nested JSON (Stack Overflow)
void test_deeply_nested_json(void) {
  c4_test_server_seed_for_test("security_nested_json");

  // Create JSON with 10000 nested objects
  char* deep_json = malloc(30000);
  TEST_ASSERT_NOT_NULL(deep_json);

  strcpy(deep_json, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_blockNumber\",\"params\":");
  size_t offset = strlen(deep_json);

  // Add deep nesting
  for (int i = 0; i < 1000; i++) {
    strcpy(deep_json + offset, "{\"a\":");
    offset += 5;
  }
  strcpy(deep_json + offset, "null");
  offset += 4;
  for (int i = 0; i < 1000; i++) {
    deep_json[offset++] = '}';
  }
  deep_json[offset++] = '}';
  deep_json[offset]   = '\0';

  int   status_code = 0;
  char* response    = send_http_request("POST", "/rpc", deep_json, &status_code);

  TEST_ASSERT_NOT_NULL_MESSAGE(response, "Server should handle deep nesting gracefully");
  // Should reject or handle without crash
  TEST_ASSERT_TRUE(status_code >= 400 || status_code == 200);

  free(deep_json);
  free(response);
}

// ============================================================================
// P1 High Priority Security Tests
// ============================================================================

// Test 6: Invalid HTTP Method
void test_invalid_http_method(void) {
  c4_test_server_seed_for_test("security_invalid_method");

  int sock = socket(AF_INET, SOCK_STREAM, 0);
  TEST_ASSERT_TRUE(sock >= 0);

  struct sockaddr_in server_addr;
  server_addr.sin_family      = AF_INET;
  server_addr.sin_port        = htons(TEST_PORT);
  server_addr.sin_addr.s_addr = inet_addr(TEST_HOST);

  connect(sock, (struct sockaddr*) &server_addr, sizeof(server_addr));

  const char* request = "INVALID /rpc HTTP/1.1\r\n"
                        "Host: localhost\r\n"
                        "\r\n";

  send(sock, request, strlen(request), 0);

  char response[1024] = {0};
  recv(sock, response, sizeof(response) - 1, 0);

  // Should get 405 Method Not Allowed or 400 Bad Request
  TEST_ASSERT_TRUE(strstr(response, "400") || strstr(response, "405"));

  close(sock);
}

// Test 7: Null Byte Injection in Path
void test_null_byte_in_path(void) {
  c4_test_server_seed_for_test("security_null_byte");

  int sock = socket(AF_INET, SOCK_STREAM, 0);
  TEST_ASSERT_TRUE(sock >= 0);

  struct sockaddr_in server_addr;
  server_addr.sin_family      = AF_INET;
  server_addr.sin_port        = htons(TEST_PORT);
  server_addr.sin_addr.s_addr = inet_addr(TEST_HOST);

  connect(sock, (struct sockaddr*) &server_addr, sizeof(server_addr));

  // Path with null byte (URL encoded as %00)
  const char* request = "GET /rpc%00.txt HTTP/1.1\r\n"
                        "Host: localhost\r\n"
                        "\r\n";

  send(sock, request, strlen(request), 0);

  char response[1024] = {0};
  recv(sock, response, sizeof(response) - 1, 0);

  // Should reject
  TEST_ASSERT_TRUE(strstr(response, "400") || strstr(response, "404"));

  close(sock);
}

// Test 8: Content-Length Mismatch
void test_content_length_mismatch(void) {
  c4_test_server_seed_for_test("security_content_length");

  int sock = socket(AF_INET, SOCK_STREAM, 0);
  TEST_ASSERT_TRUE(sock >= 0);

  struct sockaddr_in server_addr;
  server_addr.sin_family      = AF_INET;
  server_addr.sin_port        = htons(TEST_PORT);
  server_addr.sin_addr.s_addr = inet_addr(TEST_HOST);

  connect(sock, (struct sockaddr*) &server_addr, sizeof(server_addr));

  // Say Content-Length is 100, but only send 10 bytes
  const char* request = "POST /rpc HTTP/1.1\r\n"
                        "Host: localhost\r\n"
                        "Content-Type: application/json\r\n"
                        "Content-Length: 100\r\n"
                        "\r\n"
                        "short body";

  send(sock, request, strlen(request), 0);

  // Server should timeout or reject
  struct timeval tv;
  tv.tv_sec  = 2;
  tv.tv_usec = 0;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  char    response[1024] = {0};
  ssize_t received       = recv(sock, response, sizeof(response) - 1, 0);

  // Either timeout (0/-1) or get error response
  if (received > 0) {
    TEST_ASSERT_TRUE(strstr(response, "400") || strstr(response, "500"));
  }

  close(sock);
}

// Test 9: Invalid Chain ID
void test_invalid_chain_id(void) {
  c4_test_server_seed_for_test("security_chain_id");

  // Test various invalid chain IDs
  const char* invalid_requests[] = {
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_blockNumber\",\"params\":[],\"chainId\":0}",
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_blockNumber\",\"params\":[],\"chainId\":-1}",
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_blockNumber\",\"params\":[],\"chainId\":999999}",
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_blockNumber\",\"params\":[],\"chainId\":\"invalid\"}",
  };

  for (size_t i = 0; i < sizeof(invalid_requests) / sizeof(invalid_requests[0]); i++) {
    int   status_code = 0;
    char* response    = send_http_request("POST", "/rpc", invalid_requests[i], &status_code);

    TEST_ASSERT_NOT_NULL(response);
    // Should return error (400 or JSON-RPC error)
    if (status_code == 200) {
      // Check for JSON-RPC error
      TEST_ASSERT_TRUE(strstr(response, "error"));
    }
    else {
      TEST_ASSERT_EQUAL(400, status_code);
    }

    free(response);
  }
}

// Test 10: Invalid RPC Method
void test_invalid_rpc_method(void) {
  c4_test_server_seed_for_test("security_rpc_method");

  const char* dangerous_methods[] = {
      "eth_sendRawTransaction",       // Should not be supported
      "debug_traceTransaction",       // Admin method
      "personal_unlockAccount",       // Dangerous
      "admin_startRPC",               // Admin method
      "../../../../../../etc/passwd", // Path traversal in method
      "<script>alert(1)</script>",    // XSS attempt
      "; rm -rf /",                   // Command injection
      "%s%s%s%n",                     // Format string
      "eth_blockNumber\r\n\r\nGET /", // CRLF injection
  };

  for (size_t i = 0; i < sizeof(dangerous_methods) / sizeof(dangerous_methods[0]); i++) {
    char request[512];
    snprintf(request, sizeof(request),
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"%s\",\"params\":[]}",
             dangerous_methods[i]);

    int   status_code = 0;
    char* response    = send_http_request("POST", "/rpc", request, &status_code);

    TEST_ASSERT_NOT_NULL_MESSAGE(response, "Server should not crash on malicious method");

    // Should return error
    if (status_code == 200) {
      // Check for JSON-RPC error
      TEST_ASSERT_TRUE_MESSAGE(strstr(response, "error"), "Should return JSON-RPC error");
    }
    else {
      TEST_ASSERT_TRUE(status_code >= 400);
    }

    free(response);
  }
}

// ============================================================================
// P2 Medium Priority Security Tests
// ============================================================================

// Test 11: XSS in Parameters
void test_xss_in_params(void) {
  c4_test_server_seed_for_test("security_xss");

  const char* xss_payloads[] = {
      "<script>alert('XSS')</script>",
      "<img src=x onerror=alert(1)>",
      "javascript:alert(1)",
      "<iframe src=javascript:alert(1)>",
  };

  for (size_t i = 0; i < sizeof(xss_payloads) / sizeof(xss_payloads[0]); i++) {
    char request[512];
    snprintf(request, sizeof(request),
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_getBlockByNumber\",\"params\":[\"%s\",false]}",
             xss_payloads[i]);

    int   status_code = 0;
    char* response    = send_http_request("POST", "/rpc", request, &status_code);

    TEST_ASSERT_NOT_NULL(response);
    // Response should NOT contain unescaped script tags
    TEST_ASSERT_TRUE(strstr(response, "<script>") == NULL);

    free(response);
  }
}

// Test 12: Command Injection in Parameters
void test_command_injection(void) {
  c4_test_server_seed_for_test("security_cmd_inject");

  const char* cmd_payloads[] = {
      "; ls -la",
      "| cat /etc/passwd",
      "` whoami `",
      "$( id )",
      "&& echo vulnerable",
  };

  for (size_t i = 0; i < sizeof(cmd_payloads) / sizeof(cmd_payloads[0]); i++) {
    char request[512];
    snprintf(request, sizeof(request),
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_getBlockByHash\",\"params\":[\"%s\",false]}",
             cmd_payloads[i]);

    int   status_code = 0;
    char* response    = send_http_request("POST", "/rpc", request, &status_code);

    TEST_ASSERT_NOT_NULL_MESSAGE(response, "Server should handle command injection attempts");

    // Should return error for invalid block hash format
    if (status_code == 200) {
      TEST_ASSERT_TRUE(strstr(response, "error"));
    }

    free(response);
  }
}

// Test 13: Invalid Hex Encoding
void test_invalid_hex_encoding(void) {
  c4_test_server_seed_for_test("security_hex");

  const char* invalid_hex[] = {
      "0xGGGG",         // Non-hex characters
      "0x123",          // Odd length
      "0x",             // Empty
      "not_hex_at_all", // No 0x prefix
      "0x"
      "ZZZZ", // Invalid chars
  };

  for (size_t i = 0; i < sizeof(invalid_hex) / sizeof(invalid_hex[0]); i++) {
    char request[512];
    snprintf(request, sizeof(request),
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_getBlockByHash\",\"params\":[\"%s\",false]}",
             invalid_hex[i]);

    int   status_code = 0;
    char* response    = send_http_request("POST", "/rpc", request, &status_code);

    TEST_ASSERT_NOT_NULL(response);
    // Should return error for invalid hex
    if (status_code == 200) {
      TEST_ASSERT_TRUE(strstr(response, "error"));
    }

    free(response);
  }
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main(void) {
  UNITY_BEGIN();

  // P0 Critical Tests
  RUN_TEST(test_oversized_content_length);
  RUN_TEST(test_path_traversal);
  RUN_TEST(test_header_injection_crlf);
  RUN_TEST(test_invalid_json);
  RUN_TEST(test_deeply_nested_json);

  // P1 High Priority Tests
  RUN_TEST(test_invalid_http_method);
  RUN_TEST(test_null_byte_in_path);
  RUN_TEST(test_content_length_mismatch);
  RUN_TEST(test_invalid_chain_id);
  RUN_TEST(test_invalid_rpc_method);

  // P2 Medium Priority Tests
  RUN_TEST(test_xss_in_params);
  RUN_TEST(test_command_injection);
  RUN_TEST(test_invalid_hex_encoding);

  return UNITY_END();
}
