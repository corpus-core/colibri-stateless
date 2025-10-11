/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: MIT
 *
 * Server test helpers - only compiled when HTTP_SERVER is enabled
 */

#ifndef TEST_SERVER_HELPER_H
#define TEST_SERVER_HELPER_H

#ifdef HTTP_SERVER

#include "../../src/server/server.h"
#include "file_mock_helper.h"
#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef PROOFER_CACHE
#include "../../src/proofer/proofer.h"
#endif

// Test configuration
#define TEST_PORT 28545
#define TEST_HOST "127.0.0.1"

// TESTDATA_DIR is defined by CMake via -DTESTDATA_DIR="..."
#ifndef TESTDATA_DIR
#error "TESTDATA_DIR must be defined (build with -DTEST=1)"
#endif

// TESTDATA_DIR is already defined as a string by CMake via -DTESTDATA_DIR="..."
// No need for STRINGIFY/TOSTRING macros

// Global server instance and thread
static server_instance_t server_instance;
static pthread_t         server_thread;
static volatile bool     server_should_stop = false;
static const char*       current_test_name  = NULL;

// c4_test_url_rewriter is declared in server.h (TEST builds only)

// URL rewriter function for file:// mocking
static char* test_url_rewriter(const char* url, const char* payload) {
  return c4_file_mock_replace_url(url, payload, current_test_name);
}

// Set seed based on test name for deterministic behavior
// Call this at the start of each test function to ensure consistent results
// even when tests are commented out or reordered
static void c4_test_server_seed_for_test(const char* test_name) {
  // Generate deterministic seed from test name
  uint32_t seed = 42; // base seed
  if (test_name) {
    for (const char* p = test_name; *p; p++) {
      seed = seed * 31 + (uint32_t) (*p);
    }
  }
  c4_file_mock_seed_random(seed);
  current_test_name = test_name;

  // Clear caches for test isolation
  c4_clear_storage_cache();
#ifdef PROOFER_CACHE
  // Clear proofer cache using max timestamp to remove all entries
  c4_proofer_cache_cleanup(0xffffffffffffffffULL, 0);
#endif

  // Set C4_STATES_DIR to test-specific directory
  if (test_name) {
    char states_dir[512];
    snprintf(states_dir, sizeof(states_dir), "%s/server/%s", TESTDATA_DIR, test_name);
    setenv("C4_STATES_DIR", states_dir, 1); // Overwrite existing value
    fprintf(stderr, "[TEST] %s: seed=%u, states_dir=%s\n", test_name, seed, states_dir);
  }
  else {
    fprintf(stderr, "[TEST] %s: seed=%u\n", test_name ? test_name : "unknown", seed);
  }
}

// Server thread function
static void* server_thread_func(void* arg) {
  server_instance_t* instance = (server_instance_t*) arg;

  while (!server_should_stop && instance->is_running) {
    c4_server_run_once(instance);
    usleep(1000); // 1ms sleep to prevent busy-waiting
  }

  return NULL;
}

// Setup function - call from Unity setUp()
// config: Optional custom configuration (NULL = use defaults)
static void c4_test_server_setup(http_server_t* config) {
  // Initialize file mock system
  c4_file_mock_init(TESTDATA_DIR, false); // set to true to record responses
  c4_test_url_rewriter = test_url_rewriter;

  // Note: Seed is set per-test via c4_test_server_seed_for_test()
  // to ensure deterministic behavior even when tests are reordered

  // Configure http_server global with test settings
  if (config) {
    // Use provided configuration
    memcpy(&http_server, config, sizeof(http_server_t));
  }
  else {
    // Use default test configuration
    memset(&http_server, 0, sizeof(http_server));
    http_server.port           = TEST_PORT;
    http_server.memcached_host = "localhost";
    http_server.memcached_port = 11211;
    http_server.memcached_pool = 0; // Disable memcache for tests
    http_server.chain_id       = 1; // Ethereum mainnet

    // Configure multiple servers to test load balancing and retries
    http_server.rpc_nodes    = "http://eth-rpc-1:8545,http://eth-rpc-2:8545,http://eth-rpc-3:8545";
    http_server.beacon_nodes = "http://beacon-1:5051,http://beacon-2:5051";
  }

  // Start server
  int result = c4_server_start(&server_instance, http_server.port);
  if (result != 0) {
    fprintf(stderr, "Failed to start server: %d\n", result);
    exit(1);
    return;
  }

  // Start server thread
  server_should_stop = false;
  pthread_create(&server_thread, NULL, server_thread_func, &server_instance);

  // Give server time to start accepting connections
  usleep(100000); // 100ms
}

// Teardown function - call from Unity tearDown()
static void c4_test_server_teardown(void) {
  // Signal server to stop
  server_should_stop = true;

  // Give server thread a moment to see the stop signal
  // Server thread checks every 1ms, so 10ms should be plenty
  usleep(10000); // 10ms

  // Join thread (should be quick now that it's stopping)
  pthread_join(server_thread, NULL);

  // Stop server and cleanup resources
  c4_server_stop(&server_instance);

  // Cleanup file mock system
  c4_file_mock_cleanup();
  c4_test_url_rewriter = NULL;
  current_test_name    = NULL;
}

// Helper function to send HTTP request and receive response
static char* send_http_request(const char* method, const char* path,
                               const char* body, int* status_code) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    fprintf(stderr, "Failed to create socket\n");
    return NULL;
  }

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port   = htons(TEST_PORT);
  inet_pton(AF_INET, TEST_HOST, &server_addr.sin_addr);

  if (connect(sock, (struct sockaddr*) &server_addr, sizeof(server_addr)) < 0) {
    fprintf(stderr, "Failed to connect to server\n");
    close(sock);
    return NULL;
  }

  // Build HTTP request
  char request[4096];
  if (body) {
    snprintf(request, sizeof(request),
             "%s %s HTTP/1.1\r\n"
             "Host: %s:%d\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "\r\n"
             "%s",
             method, path, TEST_HOST, TEST_PORT, strlen(body), body);
  }
  else {
    snprintf(request, sizeof(request),
             "%s %s HTTP/1.1\r\n"
             "Host: %s:%d\r\n"
             "Connection: close\r\n"
             "\r\n",
             method, path, TEST_HOST, TEST_PORT);
  }

  // Send request
  send(sock, request, strlen(request), 0);

  // Set receive timeout to avoid hanging on keep-alive connections
  struct timeval timeout;
  timeout.tv_sec  = 2; // 2 seconds timeout
  timeout.tv_usec = 0;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  // Receive response
  char*   response = (char*) malloc(65536);
  ssize_t total    = 0;
  ssize_t n;

  // Read headers first to get Content-Length
  size_t header_end       = 0;
  bool   headers_complete = false;

  while (!headers_complete && total < 65535) {
    n = recv(sock, response + total, 65536 - total - 1, 0);
    if (n <= 0) break;

    total += n;
    response[total] = '\0';

    // Check for end of headers
    char* header_end_marker = strstr(response, "\r\n\r\n");
    if (header_end_marker) {
      headers_complete = true;
      header_end       = header_end_marker + 4 - response;
    }
  }

  // Parse Content-Length from headers
  size_t content_length = 0;
  if (headers_complete) {
    char* cl_header = strstr(response, "Content-Length: ");
    if (cl_header) {
      content_length = atoi(cl_header + 16);
    }

    // Calculate how much body we already have
    size_t body_received = total - header_end;

    // Read remaining body based on Content-Length
    while (content_length > 0 && body_received < content_length && total < 65535) {
      n = recv(sock, response + total, 65536 - total - 1, 0);
      if (n <= 0) break;
      total += n;
      body_received += n;
    }
  }

  response[total] = '\0';
  close(sock);

  // Parse status code
  if (status_code) {
    char* status_line = strstr(response, "HTTP/1.1 ");
    if (status_line) {
      *status_code = atoi(status_line + 9);
    }
  }

  return response;
}

// Helper to extract JSON body from HTTP response
static char* extract_json_body(const char* response) {
  const char* body = strstr(response, "\r\n\r\n");
  if (body) {
    return strdup(body + 4);
  }
  return NULL;
}

#endif // HTTP_SERVER

#endif // TEST_SERVER_HELPER_H
