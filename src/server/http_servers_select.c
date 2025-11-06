/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "beacon_types.h"
#include "chains/eth/server/eth_clients.h"
#include "logger.h"
#include "prover.h"
#include "server.h"
#include "server_handlers.h"
#include "util/chain_props.h"
#include "util/json.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Constants for load balancing
#define MAX_CONSECUTIVE_FAILURES   2
#define HEALTH_CHECK_PENALTY       0.5   // Weight penalty for unhealthy servers
#define MIN_WEIGHT                 0.1   // Minimum weight to avoid division by zero
#define USER_ERROR_RESET_THRESHOLD 0.8   // If 80%+ servers are unhealthy, assume user error
#define RECOVERY_TIMEOUT_MS        60000 // 60 seconds before allowing recovery attempts
#define RECOVERY_SUCCESS_THRESHOLD 5     // Number of successful requests from other servers before allowing recovery

// Recovery polling cadence (ms) from request-end path
#define RECOVERY_POLL_MS 5000

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

  log_info(":: Resetting server health stats - detected user error pattern");

  for (size_t i = 0; i < servers->count; i++) {
    server_health_t* health      = &servers->health_stats[i];
    health->consecutive_failures = 0;
    health->is_healthy           = true;
    health->recovery_allowed     = true;
    health->weight               = 1.0;
    health->marked_unhealthy_at  = 0;
    // Keep historical stats (total_requests, successful_requests, total_response_time)
    // but reset the health flags
    // Note: We keep the unsupported_methods list as these are method-specific limitations
  }
}

// Method support tracking functions
void c4_mark_method_unsupported(server_list_t* servers, int server_index, const char* method) {
  if (!servers || !servers->health_stats || server_index < 0 || server_index >= servers->count || !method) return;

  server_health_t* health = &servers->health_stats[server_index];

  // Check if method is already marked as unsupported
  method_support_t* current = health->unsupported_methods;
  while (current) {
    if (strcmp(current->method_name, method) == 0) {
      current->is_supported = false; // Ensure it's marked as unsupported
      return;
    }
    current = current->next;
  }

  // Add new unsupported method to the list
  method_support_t* new_entry = (method_support_t*) safe_calloc(1, sizeof(method_support_t));
  new_entry->method_name      = strdup(method);
  new_entry->is_supported     = false;
  new_entry->next             = health->unsupported_methods;
  health->unsupported_methods = new_entry;

  log_warn("   [method] Server " BRIGHT_BLUE("%s") ": Marked method '" BOLD("%s") "' as unsupported", c4_extract_server_name(servers->urls[server_index]), method);
}

bool c4_is_method_supported(server_list_t* servers, int server_index, const char* method) {
  if (!servers || !servers->health_stats || server_index < 0 || server_index >= servers->count || !method) return true;

  server_health_t*  health  = &servers->health_stats[server_index];
  method_support_t* current = health->unsupported_methods;

  while (current) {
    if (strcmp(current->method_name, method) == 0) {
      return current->is_supported;
    }
    current = current->next;
  }

  // Method not found in unsupported list, assume it's supported
  return true;
}

void c4_cleanup_method_support(server_health_t* health) {
  if (!health) return;

  method_support_t* current = health->unsupported_methods;
  while (current) {
    method_support_t* next = current->next;
    safe_free(current->method_name);
    safe_free(current);
    current = next;
  }
  health->unsupported_methods = NULL;
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
      log_warn("   [health] Server %l marked as unhealthy", (uint64_t) i);
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

    // Incorporate capacity factor based on current inflight vs. max_concurrency
    // capacity_factor = (available + 1) / (max + 1) to avoid zeroing
    uint32_t max_c           = health->max_concurrency > 0 ? health->max_concurrency : 1;
    uint32_t infl            = health->inflight;
    double   capacity_factor = ((double) ((max_c > infl ? (max_c - infl) : 0) + 1)) / ((double) (max_c + 1));
    health->weight *= capacity_factor;

    // Optional small staleness penalty if head polling is enabled and data is stale
    if (http_server.rpc_head_poll_enabled && health->latest_block > 0 && health->head_last_seen_ms > 0) {
      uint64_t stale_ms = current_time - health->head_last_seen_ms;
      if (stale_ms > 15000) { // 15s stale
        health->weight *= 0.9;
      }
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
        log_info("   [recovery] Server " BRIGHT_BLUE("%s") " allowed recovery attempt (%s)", c4_extract_server_name(servers->urls[i]), timeout_passed ? "timeout" : "success threshold");
      }
    }
  }
}

// Method factor helper at file scope (used by method-aware selection)
static double c4_method_factor_for(server_list_t* s, int i, const char* m) {
  if (!s || i < 0 || i >= (int) s->count) return 1.0;
  server_health_t* h      = &s->health_stats[i];
  double           factor = 1.0;
  if (h->rate_limited_recent && h->rate_limited_at_ms > 0 && current_ms() - h->rate_limited_at_ms < 60000) {
    factor *= 0.8;
  }
  if (http_server.rpc_head_poll_enabled && h->head_last_seen_ms > 0) {
    uint64_t stale_ms = current_ms() - h->head_last_seen_ms;
    if (stale_ms > 15000) factor *= 0.9;
  }
  if (m && h->method_stats) {
    method_stats_t* ms = h->method_stats;
    while (ms) {
      if (strcmp(ms->name, m) == 0) {
        double nf = ms->not_found_ewma;
        if (nf > 0.0) {
          double pen = 1.0 - fmin(0.9, nf * 0.7);
          factor *= pen;
        }
        if (ms->rate_limited_recent) factor *= 0.85;
        break;
      }
      ms = ms->next;
    }
  }
  return factor;
}

static bool c4_matches_client_type(server_list_t* servers, uint32_t preferred_client_type, size_t i) {
  return ((preferred_client_type == 0) ||
          (!servers->client_types) ||
          (servers->client_types[i] & preferred_client_type) ||
          (servers->client_types[i] == 0));
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

  log_error(":: EMERGENCY RESET: All servers unavailable - resetting all health stats");

  for (size_t i = 0; i < servers->count; i++) {
    server_health_t* health      = &servers->health_stats[i];
    health->consecutive_failures = 0;
    health->is_healthy           = true;
    health->recovery_allowed     = true;
    health->weight               = 1.0;
    health->marked_unhealthy_at  = 0;

    // Keep historical stats but give servers a fresh chance
    log_info("   [reset] Server %l: %s restored to healthy state", (uint64_t) i, servers->urls[i]);
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
  // Note: rand() can be seeded with srand() for deterministic testing
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

// Select best server for a specific RPC method, excluding servers that don't support it
// Estimate whether a server likely has the requested block; returns factor [0..1]
static double c4_block_factor_for(server_list_t* servers, int idx, uint64_t requested_block, bool has_block, const char* method) {
  if (!has_block || !servers || idx < 0 || idx >= (int) servers->count) return 1.0;
  server_health_t* h = &servers->health_stats[idx];
  if (h->latest_block == 0 || h->head_last_seen_ms == 0) return 1.0;
  // Predict head using chain-specific block time
  uint64_t           now_ms        = current_ms();
  uint64_t           elapsed_ms    = (now_ms > h->head_last_seen_ms) ? (now_ms - h->head_last_seen_ms) : 0;
  chain_properties_t props         = {0};
  uint64_t           block_time_ms = 0;
  if (c4_chains_get_props(http_server.chain_id, &props)) block_time_ms = props.block_time;
  if (block_time_ms == 0) block_time_ms = 12000;
  uint64_t predicted_head = h->latest_block + (elapsed_ms / block_time_ms);
  if (requested_block <= predicted_head) return 1.0; // older blocks very likely available
  uint64_t delta = requested_block - predicted_head;
  // Methods that require the exact requested block: be strict
  if (method && (strcmp(method, "eth_getProof") == 0 ||
                 strcmp(method, "debug_traceCall") == 0 ||
                 strcmp(method, "eth_call") == 0 ||
                 strcmp(method, "eth_getBlockReceipts") == 0)) {
    return 0.0; // exclude nodes that are behind to avoid immediate failure
  }
  if (delta == 1) return 0.5; // soft penalty when just one ahead
  if (delta == 2) return 0.2; // stronger penalty
  return 0.0;                 // treat as effectively unavailable
}

int c4_select_best_server_for_method(server_list_t* servers, uint32_t exclude_mask, uint32_t preferred_client_type, const char* method, uint64_t requested_block, bool has_block) {
  if (!servers || servers->count == 0) return -1;

  // If no method specified, fall back to regular selection
  if (!method) return c4_select_best_server(servers, exclude_mask, preferred_client_type);

  // Create extended exclude mask that includes servers not supporting the method
  uint32_t method_exclude_mask = exclude_mask;
  for (size_t i = 0; i < servers->count && i < 32; i++) {
    if (!c4_is_method_supported(servers, i, method)) {
      method_exclude_mask |= (1 << i);
    }
  }

  // If all servers are excluded due to method support, fall back to regular selection
  // (this handles cases where method support info might be incomplete/wrong)
  if (method_exclude_mask == ((1 << servers->count) - 1)) {
    log_warn("   [method] No servers support method '%s', falling back to regular selection", method);
    return c4_select_best_server(servers, exclude_mask, preferred_client_type);
  }

  // Method-aware selection: weight each candidate by a method-specific factor

  // Selection mirrors c4_select_best_server, but with method factors folded in
  double total_weight = 0.0;
  for (size_t i = 0; i < servers->count; i++) {
    if (!(method_exclude_mask & (1 << i)) && servers->health_stats[i].is_healthy && c4_matches_client_type(servers, preferred_client_type, i))
      total_weight += servers->health_stats[i].weight * c4_method_factor_for(servers, (int) i, method) * c4_block_factor_for(servers, (int) i, requested_block, has_block, method);
  }

  if (total_weight <= 0.0 && preferred_client_type != 0) {
    for (size_t i = 0; i < servers->count; i++) {
      if (!(method_exclude_mask & (1 << i)) && servers->health_stats[i].is_healthy)
        total_weight += servers->health_stats[i].weight * c4_method_factor_for(servers, (int) i, method) * c4_block_factor_for(servers, (int) i, requested_block, has_block, method);
    }
  }

  if (total_weight <= 0.0) {
    for (size_t i = 0; i < servers->count; i++) {
      if (!(method_exclude_mask & (1 << i)) && (preferred_client_type == 0 || c4_matches_client_type(servers, preferred_client_type, i)))
        total_weight += servers->health_stats[i].weight * c4_method_factor_for(servers, (int) i, method) * c4_block_factor_for(servers, (int) i, requested_block, has_block, method);
    }
  }

  if (total_weight <= 0.0) {
    for (size_t i = 0; i < servers->count; i++) {
      if (!(method_exclude_mask & (1 << i))) {
        total_weight += servers->health_stats[i].weight * c4_method_factor_for(servers, (int) i, method) * c4_block_factor_for(servers, (int) i, requested_block, has_block, method);
      }
    }
  }

  if (total_weight <= 0.0) {
    for (size_t i = 0; i < servers->count; i++) {
      int idx = (servers->next_index + i) % servers->count;
      if (!(method_exclude_mask & (1 << idx))) {
        servers->next_index = (idx + 1) % servers->count;
        return idx;
      }
    }
    return -1;
  }

  double random_value   = ((double) rand() / RAND_MAX) * total_weight;
  double current_weight = 0.0;

  for (size_t i = 0; i < servers->count; i++) {
    if (!(method_exclude_mask & (1 << i)) && servers->health_stats[i].is_healthy && c4_matches_client_type(servers, preferred_client_type, i)) {
      double w = servers->health_stats[i].weight * c4_method_factor_for(servers, (int) i, method) * c4_block_factor_for(servers, (int) i, requested_block, has_block, method);
      current_weight += w;
      if (current_weight >= random_value) return (int) i;
    }
  }

  if (preferred_client_type != 0) {
    for (size_t i = 0; i < servers->count; i++) {
      if (!(method_exclude_mask & (1 << i)) && servers->health_stats[i].is_healthy && !c4_matches_client_type(servers, preferred_client_type, i)) {
        double w = servers->health_stats[i].weight * c4_method_factor_for(servers, (int) i, method) * c4_block_factor_for(servers, (int) i, requested_block, has_block, method);
        current_weight += w;
        if (current_weight >= random_value) return (int) i;
      }
    }
  }

  for (size_t i = 0; i < servers->count; i++) {
    if (!(method_exclude_mask & (1 << i)) && !servers->health_stats[i].is_healthy &&
        (preferred_client_type == 0 || c4_matches_client_type(servers, preferred_client_type, i))) {
      double w = servers->health_stats[i].weight * c4_method_factor_for(servers, (int) i, method) * c4_block_factor_for(servers, (int) i, requested_block, has_block, method);
      current_weight += w;
      if (current_weight >= random_value) return (int) i;
    }
  }

  for (size_t i = 0; i < servers->count; i++) {
    if (!(method_exclude_mask & (1 << i))) {
      double w = servers->health_stats[i].weight * c4_method_factor_for(servers, (int) i, method) * c4_block_factor_for(servers, (int) i, requested_block, has_block, method);
      current_weight += w;
      if (current_weight >= random_value) {
        return (int) i;
      }
    }
  }

  for (size_t i = 0; i < servers->count; i++) {
    if (!(method_exclude_mask & (1 << i))) {
      return (int) i;
    }
  }

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
        log_warn("   [config] Unknown client type '%s' for server %s", type_str, url_part);
      }
    }
    if (client_type != BEACON_CLIENT_UNKNOWN)
      log_info("   [config] Server %d: %s (Type: %s)", count, url_part, c4_client_type_to_name(client_type, &http_server));

    list->urls[count]                             = strdup(url_part);
    list->health_stats[count].is_healthy          = true;
    list->health_stats[count].recovery_allowed    = true;
    list->health_stats[count].weight              = 1.0;
    list->health_stats[count].last_used           = current_ms();
    list->health_stats[count].marked_unhealthy_at = 0;
    // Initialize dynamic capacity / latency / head fields
    list->health_stats[count].inflight            = 0;
    list->health_stats[count].max_concurrency     = (uint32_t) (http_server.max_concurrency_default > 0 ? http_server.max_concurrency_default : 1);
    list->health_stats[count].min_concurrency     = 1;
    list->health_stats[count].ewma_latency_ms     = 100.0;
    list->health_stats[count].last_adjust_ms      = 0;
    list->health_stats[count].rate_limited_recent = false;
    list->health_stats[count].rate_limited_at_ms  = 0;
    list->health_stats[count].latest_block        = 0;
    list->health_stats[count].head_last_seen_ms   = 0;
    list->health_stats[count].method_stats        = NULL;
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

#ifdef TEST
  // Skip client type detection in TEST mode when URL rewriter is active
  // Tests can explicitly set client types if needed
  // c4_test_url_rewriter is declared in server.h
  if (c4_test_url_rewriter) {
    log_info(":: Skipping client type detection in TEST mode");
    return;
  }
#endif

  const char* detection_endpoint = NULL;
  const char* rpc_payload        = NULL;

  // Get detection parameters from chain-specific handler
  if (!c4_server_handlers_get_detection_request(&http_server, type, &detection_endpoint, &rpc_payload)) {
    log_info(":: Client type detection not implemented for this server type yet");
    return;
  }

  log_info(":: Auto-detecting client types for %s servers using %s...",
           type == C4_DATA_TYPE_BEACON_API ? "beacon" : "rpc",
           type == C4_DATA_TYPE_BEACON_API ? detection_endpoint : "web3_clientVersion");

  // Count servers that need detection
  size_t detection_count = 0;
  for (size_t i = 0; i < servers->count; i++) {
    if (servers->client_types[i] == BEACON_CLIENT_UNKNOWN)
      detection_count++;
  }

  if (detection_count == 0) {
    log_info("   [detect] All servers already have known client types");
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
      log_warn("   [detect] Server %l (%s): has invalid URL", (uint64_t) i, base_url ? base_url : "<empty>");
      continue;
    }

    // Setup CURL easy handle
    req->easy_handle = curl_easy_init();
    if (!req->easy_handle) {
      log_error("   [detect] Failed to create CURL handle for server %l", (uint64_t) i);
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
    log_error("   [detect] curl_multi_perform failed: %s", curl_multi_strerror(mc));
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
          log_info("   [detect] Server %l (%s): Detected type %s",
                   (uint64_t) req->server_index, servers->urls[req->server_index], c4_client_type_to_name(detected_type, &http_server));
        }
        else {
          log_warn("   [detect] Server %l (%s): Could not determine client type from response",
                   (uint64_t) req->server_index, servers->urls[req->server_index]);
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

  log_info(":: Client type detection completed");
}

// (Head-Poller ausgelagert nach src/server/http_head_poller.c)

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

// ---- Concurrency lifecycle hooks and AIMD adjustment ----
static method_stats_t* c4_get_or_create_method_stats(server_health_t* health, const char* method) {
  if (!health || !method) return NULL;
  method_stats_t* cur = health->method_stats;
  while (cur) {
    if (strcmp(cur->name, method) == 0) return cur;
    cur = cur->next;
  }
  method_stats_t* m      = (method_stats_t*) safe_calloc(1, sizeof(method_stats_t));
  m->name                = strdup(method);
  m->ewma_latency_ms     = 0.0;
  m->success_ewma        = 0.0;
  m->not_found_ewma      = 0.0;
  m->rate_limited_recent = false;
  m->last_update_ms      = current_ms();
  m->next                = health->method_stats;
  health->method_stats   = m;
  return m;
}

bool c4_on_request_start(server_list_t* servers, int idx, bool allow_overflow) {
  if (!servers || idx < 0 || idx >= (int) servers->count) return false;
  server_health_t* h     = &servers->health_stats[idx];
  uint32_t         max_c = h->max_concurrency > 0 ? h->max_concurrency : 1;
  if (h->inflight >= max_c) {
    if (allow_overflow && http_server.overflow_slots > 0 && h->inflight < max_c + (uint32_t) http_server.overflow_slots) {
      h->inflight++;
      return true;
    }
    return false;
  }
  h->inflight++;
  return true;
}

void c4_on_request_end(server_list_t* servers, int idx, uint64_t resp_time_ms,
                       bool success, c4_response_type_t cls, long http_code,
                       const char* method, const char* method_context) {
  if (!servers || idx < 0 || idx >= (int) servers->count) return;
  server_health_t* h = &servers->health_stats[idx];
  if (h->inflight > 0) h->inflight--;

  // Update global per-server health
  c4_update_server_health(servers, idx, resp_time_ms, success);

  // Update EWMA latency
  double alpha = 0.1;
  if (resp_time_ms > 0) {
    if (h->ewma_latency_ms <= 0.0)
      h->ewma_latency_ms = (double) resp_time_ms;
    else
      h->ewma_latency_ms = alpha * (double) resp_time_ms + (1.0 - alpha) * h->ewma_latency_ms;
  }

  // Update per-method stats if available
  if (method) {
    method_stats_t* ms = c4_get_or_create_method_stats(h, method);
    if (ms) {
      if (resp_time_ms > 0) {
        if (ms->ewma_latency_ms <= 0.0)
          ms->ewma_latency_ms = (double) resp_time_ms;
        else
          ms->ewma_latency_ms = alpha * (double) resp_time_ms + (1.0 - alpha) * ms->ewma_latency_ms;
      }
      double success_val   = success ? 1.0 : 0.0;
      ms->success_ewma     = (ms->success_ewma == 0.0 ? success_val : (alpha * success_val + (1.0 - alpha) * ms->success_ewma));
      double not_found_val = (cls == C4_RESPONSE_ERROR_RETRY || cls == C4_RESPONSE_ERROR_USER) && http_code == 404 ? 1.0 : 0.0;
      ms->not_found_ewma   = (ms->not_found_ewma == 0.0 ? not_found_val : (alpha * not_found_val + (1.0 - alpha) * ms->not_found_ewma));
      ms->last_update_ms   = current_ms();
    }
  }

  // AIMD concurrency adjustment
  uint64_t now = current_ms();
  // Fast-mark unhealthy for hard server errors (curl/network or 5xx)
  if (!success) {
    bool hard_error = (http_code == 0) || (http_code >= 500);
    if (hard_error || (cls == C4_RESPONSE_ERROR_RETRY && h->consecutive_failures >= 2)) {
      h->is_healthy          = false;
      h->recovery_allowed    = false;
      h->marked_unhealthy_at = now;
      h->weight *= 0.1; // heavy penalty immediately
    }
  }
  if (h->last_adjust_ms == 0 || (int64_t) (now - h->last_adjust_ms) >= http_server.conc_cooldown_ms) {
    bool saturated = h->inflight >= h->max_concurrency;
    if (success && h->ewma_latency_ms > 0.0 && h->ewma_latency_ms <= (double) http_server.latency_target_ms && !saturated) {
      if (h->max_concurrency < (uint32_t) http_server.max_concurrency_cap) h->max_concurrency++;
      h->last_adjust_ms = now;
    }
    else if (!success || cls == C4_RESPONSE_ERROR_RETRY || http_code == 429 || (h->ewma_latency_ms > (double) http_server.latency_target_ms && saturated)) {
      uint32_t new_max = (uint32_t) ((double) h->max_concurrency * 0.7);
      if (new_max < h->min_concurrency) new_max = h->min_concurrency;
      h->max_concurrency = new_max;
      h->last_adjust_ms  = now;
    }
  }

  // Always try recovery checks after each request; cheap and ensures timely healing
  c4_attempt_server_recovery(servers);
}

void c4_signal_rate_limited(server_list_t* servers, int idx, const char* method) {
  if (!servers || idx < 0 || idx >= (int) servers->count) return;
  server_health_t* h     = &servers->health_stats[idx];
  h->rate_limited_recent = true;
  h->rate_limited_at_ms  = current_ms();
  if (method) {
    method_stats_t* ms = c4_get_or_create_method_stats(h, method);
    if (ms) ms->rate_limited_recent = true;
  }

  // Time-driven recovery check independent of traffic distribution
  {
    static uint64_t last_recovery_check_ms = 0;
    uint64_t        t                      = current_ms();
    if (last_recovery_check_ms == 0 || (t - last_recovery_check_ms) >= RECOVERY_POLL_MS) {
      c4_attempt_server_recovery(servers);
      last_recovery_check_ms = t;
    }
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
      const ssz_def_t* client_update_def = eth_get_light_client_update(c4_chain_fork_id(chain->chain_id, slot));
      if (!client_update_def) continue;
      ssz_ob_t ob = ssz_from_json(data, client_update_def, &state);
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
