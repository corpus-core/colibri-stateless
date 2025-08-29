/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "beacon_types.h"
#include "chains/eth/server/eth_clients.h"
#include "logger.h"
#include "proofer.h"
#include "server.h"
#include "server_handlers.h"
#include "util/json.h"
#include "util/logger.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Constants for load balancing
#define MAX_CONSECUTIVE_FAILURES   3
#define HEALTH_CHECK_PENALTY       0.5    // Weight penalty for unhealthy servers
#define MIN_WEIGHT                 0.1    // Minimum weight to avoid division by zero
#define USER_ERROR_RESET_THRESHOLD 0.8    // If 80%+ servers are unhealthy, assume user error
#define RECOVERY_TIMEOUT_MS        300000 // 5 minutes before allowing recovery attempts
#define RECOVERY_SUCCESS_THRESHOLD 5      // Number of successful requests from other servers before allowing recovery

// Helper function for safe substring search in bytes_t
static bool bytes_contains_string(bytes_t data, const char* needle) {
  if (!data.data || !needle || data.len == 0) return false;

  size_t needle_len = strlen(needle);
  if (needle_len > data.len) return false;

// Use memmem if available, otherwise manual search
#ifdef __GLIBC__
  return memmem(data.data, data.len, needle, needle_len) != NULL;
#else
  // Manual search for portability
  for (size_t i = 0; i <= data.len - needle_len; i++) {
    if (memcmp(data.data + i, needle, needle_len) == 0) {
      return true;
    }
  }
  return false;
#endif
}

// Check if response indicates user error (4xx codes)
// For beacon API blocks, 404 might be sync lag rather than user error
bool c4_is_user_error_response(long http_code, const char* url, bytes_t response_body) {
  // All 5xx codes are server errors, not user errors
  if (http_code >= 500) return false;

  // Only 4xx codes can be user errors
  if (http_code < 400 || http_code >= 500) return false;

  // Server configuration/infrastructure errors (not user errors)
  if (http_code == 401) return false; // Unauthorized (API keys, auth issues)
  if (http_code == 403) return false; // Forbidden (often server misconfiguration)
  if (http_code == 429) return false; // Too Many Requests (rate limiting)

  // Special case: Beacon API requests with 404 might be sync lag or client incompatibility
  if (http_code == 404 && url &&
      (strstr(url, "/beacon/blocks/") || strstr(url, "/beacon/headers/") ||
       strstr(url, "/historical_summaries/") || strstr(url, "/nimbus/") || strstr(url, "/lodestar/"))) {
    // Check if response indicates the block/header simply isn't available yet (sync lag)
    if (response_body.data && response_body.len > 0 &&
        (bytes_contains_string(response_body, "Block header/data has not been found") ||
         bytes_contains_string(response_body, "Block not found") ||
         bytes_contains_string(response_body, "Header not found") ||
         bytes_contains_string(response_body, "block not found") ||
         bytes_contains_string(response_body, "header not found") ||
         bytes_contains_string(response_body, "unknown block") ||
         bytes_contains_string(response_body, "unknown header"))) {
      fprintf(stderr, "   [sync ] Detected potential sync lag for beacon API - treating as server error, not user error\n");
      return false; // Treat as server error (retryable) rather than user error
    }
  }

  // All other 4xx codes are user errors
  return true;
}

// Check if too many servers are unhealthy (indicating potential user error)
bool c4_should_reset_health_stats(server_list_t* servers) {
  if (!servers || !servers->health_stats || servers->count == 0) return false;

  size_t unhealthy_count = 0;
  for (size_t i = 0; i < servers->count; i++) {
    if (!servers->health_stats[i].is_healthy) {
      unhealthy_count++;
    }
  }

  double unhealthy_ratio = (double) unhealthy_count / servers->count;
  return unhealthy_ratio >= USER_ERROR_RESET_THRESHOLD;
}

// Reset server health stats (used when detecting user errors affecting all servers)
void c4_reset_server_health_stats(server_list_t* servers) {
  if (!servers || !servers->health_stats) return;

  fprintf(stderr, ":: Resetting server health stats - detected user error pattern\n");

  for (size_t i = 0; i < servers->count; i++) {
    server_health_t* health      = &servers->health_stats[i];
    health->consecutive_failures = 0;
    health->is_healthy           = true;
    health->recovery_allowed     = true;
    health->weight               = 1.0;
    health->marked_unhealthy_at  = 0;
    // Keep historical stats (total_requests, successful_requests, total_response_time)
    // but reset the health flags
  }
}

// Calculate weights based on success rate and response time
void c4_calculate_server_weights(server_list_t* servers) {
  if (!servers || !servers->health_stats) return;

  uint64_t current_time = current_ms();

  for (size_t i = 0; i < servers->count; i++) {
    server_health_t* health = &servers->health_stats[i];

    // Base weight starts at 1.0
    health->weight = 1.0;

    // Calculate success rate (0.0 to 1.0)
    double success_rate = 1.0;
    if (health->total_requests > 0) {
      success_rate = (double) health->successful_requests / health->total_requests;
    }

    // Calculate average response time
    double avg_response_time = 100.0; // Default 100ms
    if (health->successful_requests > 0) {
      avg_response_time = (double) health->total_response_time / health->successful_requests;
    }

    // Weight based on success rate (higher success = higher weight)
    health->weight *= success_rate;

    // Weight based on response time (lower time = higher weight)
    // Invert response time: weight decreases as response time increases
    if (avg_response_time > 0) {
      health->weight *= (1000.0 / (avg_response_time + 100.0)); // Normalize to reasonable range
    }

    // Penalty for consecutive failures
    if (health->consecutive_failures > 0) {
      health->weight *= pow(HEALTH_CHECK_PENALTY, health->consecutive_failures);
    }

    // Mark as unhealthy if too many consecutive failures
    bool was_healthy   = health->is_healthy;
    health->is_healthy = health->consecutive_failures < MAX_CONSECUTIVE_FAILURES;

    // Record when server was marked unhealthy
    if (was_healthy && !health->is_healthy) {
      health->marked_unhealthy_at = current_time;
      health->recovery_allowed    = false;
      fprintf(stderr, "   [health] Server %zu marked as unhealthy\n", i);
    }

    if (!health->is_healthy) {
      health->weight *= 0.1; // Heavy penalty for unhealthy servers
    }

    // Ensure minimum weight
    if (health->weight < MIN_WEIGHT) {
      health->weight = MIN_WEIGHT;
    }

    // Slight preference for recently unused servers to balance load
    uint64_t time_since_last_use = current_time - health->last_used;
    if (time_since_last_use > 10000) { // 10 seconds
      health->weight *= 1.1;           // Small bonus for unused servers
    }
  }
}

// Check if servers are available after excluding failed ones
bool c4_has_available_servers(server_list_t* servers, uint32_t exclude_mask) {
  if (!servers || servers->count == 0) return false;

  for (size_t i = 0; i < servers->count; i++) {
    if (!(exclude_mask & (1 << i))) return true; // Found at least one available server
  }
  return false; // All servers are excluded
}

// Attempt to recover unhealthy servers after timeout or success threshold
void c4_attempt_server_recovery(server_list_t* servers) {
  if (!servers || !servers->health_stats) return;

  uint64_t current_time    = current_ms();
  size_t   healthy_servers = 0;

  // Count healthy servers and calculate total successful requests
  uint64_t total_recent_successes = 0;
  for (size_t i = 0; i < servers->count; i++) {
    if (servers->health_stats[i].is_healthy) {
      healthy_servers++;
      // Count recent successes (rough estimate)
      total_recent_successes += servers->health_stats[i].successful_requests;
    }
  }

  // Try to recover unhealthy servers
  for (size_t i = 0; i < servers->count; i++) {
    server_health_t* health = &servers->health_stats[i];

    if (!health->is_healthy && !health->recovery_allowed) {
      // Check recovery conditions
      bool timeout_passed        = (current_time - health->marked_unhealthy_at) >= RECOVERY_TIMEOUT_MS;
      bool success_threshold_met = total_recent_successes >= RECOVERY_SUCCESS_THRESHOLD;

      if (timeout_passed || success_threshold_met) {
        health->recovery_allowed     = true;
        health->consecutive_failures = MAX_CONSECUTIVE_FAILURES - 1; // Allow one chance
        health->is_healthy           = true;
        health->weight               = MIN_WEIGHT; // Start with minimal weight
        fprintf(stderr, "   [recovery] Server %zu allowed recovery attempt (%s)\n",
                i, timeout_passed ? "timeout" : "success threshold");
      }
    }
  }
}

// Check if all servers are effectively unavailable (emergency reset condition)
static bool c4_all_servers_unavailable(server_list_t* servers, uint32_t exclude_mask) {
  if (!servers || !servers->health_stats) return true;

  for (size_t i = 0; i < servers->count; i++) {
    if (!(exclude_mask & (1 << i)) && servers->health_stats[i].is_healthy) {
      return false; // Found at least one healthy and non-excluded server
    }
  }
  return true; // No healthy servers available
}

// Emergency reset when all servers are unavailable
static void c4_emergency_reset_all_servers(server_list_t* servers) {
  if (!servers || !servers->health_stats) return;

  fprintf(stderr, ":: EMERGENCY RESET: All servers unavailable - resetting all health stats\n");

  for (size_t i = 0; i < servers->count; i++) {
    server_health_t* health      = &servers->health_stats[i];
    health->consecutive_failures = 0;
    health->is_healthy           = true;
    health->recovery_allowed     = true;
    health->weight               = 1.0;
    health->marked_unhealthy_at  = 0;

    // Keep historical stats but give servers a fresh chance
    fprintf(stderr, "   [reset] Server %zu: %s restored to healthy state\n",
            i, servers->urls[i]);
  }
}

// Select best server using weighted random selection with optional client type preference
int c4_select_best_server(server_list_t* servers, uint32_t exclude_mask, uint32_t preferred_client_type) {
  if (!servers || servers->count == 0) return -1;

  // Emergency check: if all servers are unavailable, reset everything
  if (c4_all_servers_unavailable(servers, exclude_mask)) {
    // Try recovery first
    c4_attempt_server_recovery(servers);

    // If still all unavailable, do emergency reset
    if (c4_all_servers_unavailable(servers, exclude_mask)) {
      c4_emergency_reset_all_servers(servers);
      // Recalculate weights after emergency reset
      c4_calculate_server_weights(servers);
    }
  }

  // Helper macro to check if server matches client type preference (bitmask)
#define matches_client_type(i)                           \
  ((preferred_client_type == 0) ||                       \
   (!servers->client_types) ||                           \
   (servers->client_types[i] & preferred_client_type) || \
   (servers->client_types[i] == 0))

  // First pass: try to find healthy servers with preferred client type, not in exclude mask
  double total_weight = 0.0;
  for (size_t i = 0; i < servers->count; i++) {
    if (!(exclude_mask & (1 << i)) && servers->health_stats[i].is_healthy && matches_client_type(i))
      total_weight += servers->health_stats[i].weight;
  }

  // Second pass: if no preferred client type found, try any healthy servers
  if (total_weight <= 0.0 && preferred_client_type != 0) {
    for (size_t i = 0; i < servers->count; i++) {
      if (!(exclude_mask & (1 << i)) && servers->health_stats[i].is_healthy)
        total_weight += servers->health_stats[i].weight;
    }
  }

  // Third pass: if no healthy servers available, include unhealthy ones with preferred type
  if (total_weight <= 0.0) {
    for (size_t i = 0; i < servers->count; i++) {
      if (!(exclude_mask & (1 << i)) && (preferred_client_type == 0 || matches_client_type(i)))
        total_weight += servers->health_stats[i].weight;
    }
  }

  // Final pass: if still nothing, include any server not excluded
  if (total_weight <= 0.0) {
    for (size_t i = 0; i < servers->count; i++) {
      if (!(exclude_mask & (1 << i))) {
        total_weight += servers->health_stats[i].weight;
      }
    }
  }

  if (total_weight <= 0.0) {
    // Final fallback to round-robin if all weights are zero
    for (size_t i = 0; i < servers->count; i++) {
      int idx = (servers->next_index + i) % servers->count;
      if (!(exclude_mask & (1 << idx))) {
        servers->next_index = (idx + 1) % servers->count;
        return idx;
      }
    }
    return -1; // All servers excluded
  }

  // Weighted random selection using same criteria as weight calculation
  double random_value   = ((double) rand() / RAND_MAX) * total_weight;
  double current_weight = 0.0;

  // Try each selection pass in the same order as weight calculation
  // Pass 1: Healthy servers with preferred client type
  for (size_t i = 0; i < servers->count; i++) {
    if (!(exclude_mask & (1 << i)) && servers->health_stats[i].is_healthy && matches_client_type(i)) {
      current_weight += servers->health_stats[i].weight;
      if (current_weight >= random_value) return (int) i;
    }
  }

  // Pass 2: Any healthy servers (if preferred type failed)
  if (preferred_client_type != 0) {
    for (size_t i = 0; i < servers->count; i++) {
      if (!(exclude_mask & (1 << i)) && servers->health_stats[i].is_healthy && !matches_client_type(i)) {
        current_weight += servers->health_stats[i].weight;
        if (current_weight >= random_value) return (int) i;
      }
    }
  }

  // Pass 3: Unhealthy servers with preferred type
  for (size_t i = 0; i < servers->count; i++) {
    if (!(exclude_mask & (1 << i)) && !servers->health_stats[i].is_healthy &&
        (preferred_client_type == 0 || matches_client_type(i))) {
      current_weight += servers->health_stats[i].weight;
      if (current_weight >= random_value) return (int) i;
    }
  }

  // Pass 4: Any remaining servers
  for (size_t i = 0; i < servers->count; i++) {
    if (!(exclude_mask & (1 << i))) {
      current_weight += servers->health_stats[i].weight;
      if (current_weight >= random_value) {
        return (int) i;
      }
    }
  }

  // Fallback to first available server
  for (size_t i = 0; i < servers->count; i++) {
    if (!(exclude_mask & (1 << i))) {
#undef matches_client_type
      return (int) i;
    }
  }

#undef matches_client_type
  return -1;
}

// Case-insensitive string search helper
static bool contains_client_name(const char* response, const char* client_name) {
  if (!response || !client_name) return false;

  const char* pos        = response;
  size_t      client_len = strlen(client_name);

  while (*pos) {
    if (strncasecmp(pos, client_name, client_len) == 0) return true;
    pos++;
  }
  return false;
}

// Server Selection
// ================

// Convert client type bitmask to human-readable name
const char* c4_client_type_to_name(beacon_client_type_t client_type, http_server_t* http_server) {
  const client_type_mapping_t* mappings = c4_server_handlers_get_client_mappings(http_server);
  if (!mappings) return "Unknown";

  for (int i = 0; mappings[i].config_name != NULL; i++) {
    if (mappings[i].value == client_type) {
      return mappings[i].display_name;
    }
  }
  return "Unknown";
}

// Get known config names for URL parsing
static const char** c4_get_known_config_names() {
  static const char* known_names[32]; // Static array to hold pointers
  static bool        initialized = false;

  if (!initialized) {
    const client_type_mapping_t* mappings = c4_server_handlers_get_client_mappings(&http_server);
    if (mappings) {
      int i = 0;
      while (mappings[i].config_name != NULL && i < 31) {
        known_names[i] = mappings[i].config_name;
        i++;
      }
      known_names[i] = NULL; // Null terminator
    }
    else {
      known_names[0] = NULL; // Ensure it's null if no handler is present
    }
    initialized = true;
  }

  return known_names;
}

// Parse config name to client type
static beacon_client_type_t c4_parse_config_name(const char* config_name, http_server_t* http_server) {
  const client_type_mapping_t* mappings = c4_server_handlers_get_client_mappings(http_server);
  if (!mappings) return BEACON_CLIENT_UNKNOWN;

  for (int i = 0; mappings[i].config_name != NULL; i++) {
    if (strcmp(config_name, mappings[i].config_name) == 0) {
      return mappings[i].value;
    }
  }
  return BEACON_CLIENT_UNKNOWN;
}

// Server configuration parsing with client type detection
void c4_parse_server_config(server_list_t* list, char* servers) {
  if (!servers || !list) return;

  // Count servers first
  char* servers_copy = strdup(servers);
  int   count        = 0;
  char* token        = strtok(servers_copy, ",");
  while (token) {
    count++;
    token = strtok(NULL, ",");
  }

  // Reset for actual parsing
  memcpy(servers_copy, servers, strlen(servers) + 1);

  // Allocate arrays
  list->urls         = (char**) safe_calloc(count, sizeof(char*));
  list->health_stats = (server_health_t*) safe_calloc(count, sizeof(server_health_t));
  list->client_types = (beacon_client_type_t*) safe_calloc(count, sizeof(beacon_client_type_t));
  list->count        = count;
  list->next_index   = 0;

  // Get known client type suffixes for manual configuration
  const char** known_types = c4_get_known_config_names();

  count = 0;
  token = strtok(servers_copy, ",");

  while (token) {
    // Parse optional client type suffix: "https://server.com:NIMBUS"
    char*                url_part    = token;
    beacon_client_type_t client_type = BEACON_CLIENT_UNKNOWN;

    // Find client type separator (look for :TYPE pattern after URL)
    char* type_separator = NULL;
    char* type_str       = NULL;

    // Strategy: Look for known client type names after a colon
    for (int i = 0; known_types[i] != NULL && !type_separator; i++) {
      char search_pattern[64]; // Stack allocation instead of malloc
      snprintf(search_pattern, sizeof(search_pattern), ":%s", known_types[i]);
      char* found = strstr(token, search_pattern);
      if (found && (found + strlen(search_pattern) == token + strlen(token))) {
        // Found known type at end of string
        type_separator = found;
        type_str       = found + 1;
      }
    }

    if (type_separator && type_str) {
      // Split URL and type
      *type_separator = '\0';

      // Parse client type string using mapping
      client_type = c4_parse_config_name(type_str, &http_server);
      if (client_type == BEACON_CLIENT_UNKNOWN) {
        fprintf(stderr, "   [config] Unknown client type '%s' for server %s\n", type_str, url_part);
      }
    }
    if (client_type != BEACON_CLIENT_UNKNOWN)
      fprintf(stderr, "   [config] Server %d: %s (Type: %s)\n", count, url_part, c4_client_type_to_name(client_type, &http_server));

    list->urls[count]                             = strdup(url_part);
    list->health_stats[count].is_healthy          = true;
    list->health_stats[count].recovery_allowed    = true;
    list->health_stats[count].weight              = 1.0;
    list->health_stats[count].last_used           = current_ms();
    list->health_stats[count].marked_unhealthy_at = 0;
    list->client_types[count]                     = client_type;

    count++;
    token = strtok(NULL, ",");
  }

  safe_free(servers_copy);
}

// Parse client version response to determine client type
beacon_client_type_t c4_parse_client_version_response(const char* response, data_request_type_t type) {
  // Dispatch to chain-specific handlers
  return c4_server_handlers_parse_version_response(&http_server, response, type);
}

// Helper structure for parallel client detection
typedef struct {
  size_t             server_index;
  char*              detection_url;
  CURL*              easy_handle;
  buffer_t           response_buffer;
  struct curl_slist* headers; // Store headers for cleanup
} detection_request_t;

// CURL write callback for detection requests
static size_t detection_write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
  buffer_t* buffer   = (buffer_t*) userp;
  size_t    realsize = size * nmemb;
  buffer_grow(buffer, buffer->data.len + realsize + 1);
  memcpy(buffer->data.data + buffer->data.len, contents, realsize);
  buffer->data.len += realsize;
  buffer->data.data[buffer->data.len] = '\0'; // NULL terminate
  return realsize;
}

// Auto-detect client types for all servers in a list using parallel requests
void c4_detect_server_client_types(server_list_t* servers, data_request_type_t type) {
  if (!servers || !servers->client_types || servers->count == 0) return;

  const char* detection_endpoint = NULL;
  const char* rpc_payload        = NULL;

  // Get detection parameters from chain-specific handler
  if (!c4_server_handlers_get_detection_request(&http_server, type, &detection_endpoint, &rpc_payload)) {
    fprintf(stderr, ":: Client type detection not implemented for this server type yet\n");
    return;
  }

  fprintf(stderr, ":: Auto-detecting client types for %s servers using %s...\n",
          type == C4_DATA_TYPE_BEACON_API ? "beacon" : "rpc",
          type == C4_DATA_TYPE_BEACON_API ? detection_endpoint : "web3_clientVersion");

  // Count servers that need detection
  size_t detection_count = 0;
  for (size_t i = 0; i < servers->count; i++) {
    if (servers->client_types[i] == BEACON_CLIENT_UNKNOWN)
      detection_count++;
  }

  if (detection_count == 0) {
    fprintf(stderr, "   [detect] All servers already have known client types\n");
    return;
  }

  // Prepare parallel detection requests
  detection_request_t* requests     = (detection_request_t*) safe_calloc(detection_count, sizeof(detection_request_t));
  CURLM*               multi_handle = curl_multi_init();
  size_t               request_idx  = 0;
  for (size_t i = 0; i < servers->count; i++) {
    if (servers->client_types[i] != BEACON_CLIENT_UNKNOWN) continue;

    detection_request_t* req = &requests[request_idx];
    req->server_index        = i;
    req->response_buffer     = (buffer_t) {0};
    req->headers             = NULL;
    char* base_url           = servers->urls[i];

    if (base_url && strlen(base_url) > 0)
      req->detection_url = bprintf(NULL, "%s%s%s", base_url,
                                   (base_url[strlen(base_url) - 1] == '/') ? "" : "/",
                                   detection_endpoint);
    else {
      fprintf(stderr, "   [detect] Server %zu (%s): has invalid URL\n", i, base_url ? base_url : "<empty>");
      continue;
    }

    // Setup CURL easy handle
    req->easy_handle = curl_easy_init();
    if (!req->easy_handle) {
      fprintf(stderr, "   [detect] Failed to create CURL handle for server %zu\n", i);
      safe_free(req->detection_url);
      continue;
    }

    curl_easy_setopt(req->easy_handle, CURLOPT_URL, req->detection_url);
    curl_easy_setopt(req->easy_handle, CURLOPT_WRITEFUNCTION, detection_write_callback);
    curl_easy_setopt(req->easy_handle, CURLOPT_WRITEDATA, &req->response_buffer);
    curl_easy_setopt(req->easy_handle, CURLOPT_TIMEOUT, 10L); // 10 second timeout
    curl_easy_setopt(req->easy_handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(req->easy_handle, CURLOPT_SSL_VERIFYPEER, 0L); // Same as main client
    curl_easy_setopt(req->easy_handle, CURLOPT_SSL_VERIFYHOST, 0L);

    // Setup RPC-specific headers and payload if needed
    req->headers = NULL;
    if (rpc_payload) {
      req->headers = curl_slist_append(req->headers, "Content-Type: application/json");
      curl_easy_setopt(req->easy_handle, CURLOPT_HTTPHEADER, req->headers);
      curl_easy_setopt(req->easy_handle, CURLOPT_POSTFIELDS, rpc_payload);
      curl_easy_setopt(req->easy_handle, CURLOPT_POSTFIELDSIZE, strlen(rpc_payload));
    }

    // Add to multi handle
    curl_multi_add_handle(multi_handle, req->easy_handle);

    //    fprintf(stderr, "   [detect] Starting detection for server %zu: %s\n", i, req->detection_url);
    request_idx++;
  }

  // Execute all requests in parallel
  int       running_handles;
  CURLMcode mc = curl_multi_perform(multi_handle, &running_handles);

  if (mc != CURLM_OK)
    fprintf(stderr, "   [detect] curl_multi_perform failed: %s\n", curl_multi_strerror(mc));
  else {
    // Wait for all requests to complete
    while (running_handles > 0) {
      int numfds = 0;
      mc         = curl_multi_wait(multi_handle, NULL, 0, 1000, &numfds);
      if (mc != CURLM_OK) break;

      mc = curl_multi_perform(multi_handle, &running_handles);
      if (mc != CURLM_OK) break;
    }
  }

  // Process results
  CURLMsg* msg;
  int      msgs_left;
  while ((msg = curl_multi_info_read(multi_handle, &msgs_left))) {
    if (msg->msg == CURLMSG_DONE) {
      CURL* easy_handle = msg->easy_handle;

      // Find corresponding request
      detection_request_t* req = NULL;
      for (size_t j = 0; j < request_idx; j++) {
        if (requests[j].easy_handle == easy_handle) {
          req = &requests[j];
          break;
        }
      }

      if (!req) continue;

      long response_code;
      curl_easy_getinfo(easy_handle, CURLINFO_RESPONSE_CODE, &response_code);

      if (msg->data.result == CURLE_OK && response_code == 200 && req->response_buffer.data.len > 0) {
        // Parse response to determine client type
        beacon_client_type_t detected_type = c4_parse_client_version_response((char*) req->response_buffer.data.data, type);

        if (detected_type != BEACON_CLIENT_UNKNOWN) {
          servers->client_types[req->server_index] = detected_type;
          fprintf(stderr, "   [detect] Server %zu (%s): Detected type %s\n",
                  req->server_index, servers->urls[req->server_index], c4_client_type_to_name(detected_type, &http_server));
        }
        else {
          fprintf(stderr, "   [detect] Server %zu (%s): Could not determine client type from response\n",
                  req->server_index, servers->urls[req->server_index]);
        }
      }
      else {
        const char* curl_error_msg = curl_easy_strerror(msg->data.result);
        if (response_code > 0)
          log_warn("Server %d (%s): Detection failed - HTTP %d, %s", req->server_index, servers->urls[req->server_index], (uint32_t) response_code, curl_error_msg);
        else
          log_warn("Server %d (%s): Detection failed - %s", req->server_index, servers->urls[req->server_index], curl_error_msg);
      }
    }
  }

  // Cleanup
  for (size_t j = 0; j < request_idx; j++) {
    if (requests[j].easy_handle) {
      curl_multi_remove_handle(multi_handle, requests[j].easy_handle);
      curl_easy_cleanup(requests[j].easy_handle);
    }
    if (requests[j].headers) curl_slist_free_all(requests[j].headers);
    if (requests[j].detection_url) safe_free(requests[j].detection_url);
    buffer_free(&requests[j].response_buffer);
  }

  safe_free(requests);
  curl_multi_cleanup(multi_handle);

  fprintf(stderr, ":: Client type detection completed\n");
}

// Update server health statistics
void c4_update_server_health(server_list_t* servers, int server_index, uint64_t response_time, bool success) {
  if (!servers || !servers->health_stats || server_index < 0 || server_index >= servers->count) return;

  server_health_t* health       = &servers->health_stats[server_index];
  uint64_t         current_time = current_ms();

  health->total_requests++;
  health->last_used = current_time;

  if (success) {
    health->successful_requests++;
    health->total_response_time += response_time;
    health->consecutive_failures = 0; // Reset failure counter
  }
  else {
    health->consecutive_failures++;
  }

  // Recalculate weights periodically
  if (health->total_requests % 10 == 0) {
    c4_calculate_server_weights(servers);
  }

  // Attempt server recovery periodically
  if (health->total_requests % 20 == 0) {
    c4_attempt_server_recovery(servers);
  }
}

static bytes_t convert_lighthouse_to_ssz(data_request_t* req, json_t result, uint64_t start, uint64_t count) {
  const chain_spec_t* chain           = c4_eth_get_chain_spec((chain_id_t) http_server.chain_id);
  uint64_t            slot_start      = slot_for_period(start, chain);
  uint64_t            slots_per_epoch = slot_for_period(1, chain);
  c4_state_t          state           = {0};
  buffer_t            response        = {0};

  int found = 0;
  json_for_each_value(result, entry) {
    json_t   data = json_get(entry, "data");
    uint64_t slot = json_get_uint64(json_get(json_get(data, "attested_header"), "beacon"), "slot");
    if (slot >= slot_start + found * slots_per_epoch && slot < slot_start + (found + 1) * slots_per_epoch && found < count) {
      const ssz_def_t* client_update_list = eth_get_light_client_update_list(c4_chain_fork_id(chain->chain_id, slot));
      if (!client_update_list) continue;
      ssz_ob_t ob = ssz_from_json(data, client_update_list->def.vector.type, &state);
      if (state.error) {
        if (ob.bytes.data) safe_free(ob.bytes.data);
        req->error = bprintf(NULL, "Failed to convert lighthouse light client update to ssz: %s", state.error);
        safe_free(state.error);
        return NULL_BYTES;
      }
      buffer_add_le(&response, ob.bytes.len, 8);
      buffer_append(&response, bytes(NULL, 4));
      buffer_append(&response, ob.bytes);
      safe_free(ob.bytes.data);
      found++;
    }
  }
  return response.data;
}

char* c4_request_fix_url(char* url, single_request_t* r, beacon_client_type_t client_type) {
  static char buffer[1024];
  buffer_t    buf = stack_buffer(buffer);
  if ((client_type & BEACON_CLIENT_NIMBUS) && strncmp(url, "eth/v1/lodestar/historical_summaries/", 39) == 0) {
    buffer_reset(&buf);
    return bprintf(&buf, "nimbus/v1/debug/beacon/states/%s/historical_summaries", url + 39);
  }

  return url;
}

data_request_encoding_t c4_request_fix_encoding(data_request_encoding_t encoding, single_request_t* r, beacon_client_type_t client_type) {
  if ((client_type & BEACON_CLIENT_LIGHTHOUSE) && strncmp(r->req->url, "eth/v1/beacon/light_client/updates", 34) == 0) {
    return C4_DATA_ENCODING_JSON;
  }
  return encoding;
}

bytes_t c4_request_fix_response(bytes_t response, single_request_t* r, beacon_client_type_t client_type) {
  if ((client_type & BEACON_CLIENT_LIGHTHOUSE) && strncmp(r->req->url, "eth/v1/beacon/light_client/updates", 34) == 0) {
    bytes_t ssz_data = convert_lighthouse_to_ssz(r->req, json_parse((char*) response.data), c4_get_query(r->req->url, "start_period"), c4_get_query(r->req->url, "count"));
    if (ssz_data.data) {
      safe_free(response.data);
      return ssz_data;
    }
    else
      return NULL_BYTES;
  }
  return response;
}
