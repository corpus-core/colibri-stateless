#include "logger.h"
#include "proofer.h"
#include "server.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Constants for load balancing
#define MAX_CONSECUTIVE_FAILURES   3
#define HEALTH_CHECK_PENALTY       0.5    // Weight penalty for unhealthy servers
#define MIN_WEIGHT                 0.1    // Minimum weight to avoid division by zero
#define USER_ERROR_RESET_THRESHOLD 0.8    // If 80%+ servers are unhealthy, assume user error
#define RECOVERY_TIMEOUT_MS        300000 // 5 minutes before allowing recovery attempts
#define RECOVERY_SUCCESS_THRESHOLD 5      // Number of successful requests from other servers before allowing recovery

// Check if response indicates user error (4xx codes)
bool c4_is_user_error_response(long http_code) {
  return http_code >= 400 && http_code < 500; // 4xx codes indicate client/user errors
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
    if (!(exclude_mask & (1 << i))) {
      return true; // Found at least one available server
    }
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

// Select best server using weighted random selection
int c4_select_best_server(server_list_t* servers, uint32_t exclude_mask) {
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

  // First pass: try to find healthy servers not in exclude mask
  double total_weight = 0.0;
  for (size_t i = 0; i < servers->count; i++) {
    if (!(exclude_mask & (1 << i)) && servers->health_stats[i].is_healthy) {
      total_weight += servers->health_stats[i].weight;
    }
  }

  // If no healthy servers available, include unhealthy ones
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

  // Weighted random selection
  double random_value   = ((double) rand() / RAND_MAX) * total_weight;
  double current_weight = 0.0;

  for (size_t i = 0; i < servers->count; i++) {
    if (exclude_mask & (1 << i)) continue;

    // Use healthy servers first, then unhealthy if needed
    bool use_server = servers->health_stats[i].is_healthy ||
                      (total_weight == 0.0); // Include unhealthy if no healthy ones

    if (use_server) {
      current_weight += servers->health_stats[i].weight;
      if (current_weight >= random_value) {
        return (int) i;
      }
    }
  }

  // Fallback to first available server
  for (size_t i = 0; i < servers->count; i++) {
    if (!(exclude_mask & (1 << i))) {
      return (int) i;
    }
  }

  return -1;
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
