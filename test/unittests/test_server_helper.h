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
#include "../../src/util/bytes.h"
#include "file_mock_helper.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include "../../src/util/win_compat.h"
#include <process.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#ifdef PROVER_CACHE
#include "../../src/prover/prover.h"
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
#ifdef _WIN32
typedef HANDLE pthread_t;
#endif
static pthread_t     server_thread;
static volatile bool server_should_stop = false;
static const char*   current_test_name  = NULL;

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
#ifdef PROVER_CACHE
  // Clear prover cache using max timestamp to remove all entries
  c4_prover_cache_cleanup(0xffffffffffffffffULL, 0);
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

#ifdef _WIN32
// Minimal pthread shim for Windows/MSVC
#ifndef C4_HAVE_USLEEP
#define C4_HAVE_USLEEP 1
static void usleep(unsigned int usec) { Sleep((usec + 999) / 1000); }
#endif

static unsigned __stdcall c4_thread_start(void* arg) {
  server_thread_func(arg);
  _endthreadex(0);
  return 0;
}

static int pthread_create(pthread_t* thread, void* attr, void* (*start_routine)(void*), void* arg) {
  (void) attr;
  uintptr_t h = _beginthreadex(NULL, 0, (unsigned(__stdcall*)(void*)) c4_thread_start, arg, 0, NULL);
  if (!h) return -1;
  *thread = (HANDLE) h;
  return 0;
}

static int pthread_join(pthread_t thread, void* retval) {
  (void) retval;
  WaitForSingleObject(thread, INFINITE);
  CloseHandle(thread);
  return 0;
}
#endif

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
static size_t curl_write_cb(void* contents, size_t size, size_t nmemb, void* userp) {
  size_t    realsize = size * nmemb;
  buffer_t* buf      = (buffer_t*) userp;
  buffer_append(buf, bytes(contents, realsize));
  return realsize;
}

static char* send_http_request(const char* method, const char* path,
                               const char* body, int* status_code) {
  CURL* curl = curl_easy_init();
  if (!curl) return NULL;

  char url[512];
  snprintf(url, sizeof(url), "http://%s:%d%s", TEST_HOST, TEST_PORT, path ? path : "/");
  buffer_t           response = {0};
  struct curl_slist* headers  = NULL;

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  // Include HTTP headers in the response buffer to keep legacy behavior
  curl_easy_setopt(curl, CURLOPT_HEADER, 1L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

  if (strcasecmp(method, "POST") == 0 || strcasecmp(method, "PUT") == 0 || strcasecmp(method, "DELETE") == 0) {
    if (strcasecmp(method, "POST") == 0)
      curl_easy_setopt(curl, CURLOPT_POST, 1L);
    else if (strcasecmp(method, "PUT") == 0)
      curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    else if (strcasecmp(method, "DELETE") == 0)
      curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");

    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if (body) {
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long) strlen(body));
    }
  }

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (response.data.data) free(response.data.data);
    return NULL;
  }

  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  if (status_code) *status_code = (int) http_code;

  if (headers) curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  // Null-terminate
  buffer_add_chars(&response, "");
  char* out = strdup((char*) response.data.data);
  free(response.data.data);
  return out;
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
