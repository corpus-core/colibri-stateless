#ifndef C4_ETH_PERIOD_PROVER_H
#define C4_ETH_PERIOD_PROVER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint64_t last_run_timestamp;
  uint64_t last_check_timestamp;
  uint64_t last_run_duration_ms;
  uint64_t last_run_status; // 0=success, 1=failure
  uint64_t current_period;
  uint64_t total_success;
  uint64_t total_failure;
} prover_stats_t;

extern prover_stats_t prover_stats;

/**
 * Initializes prover stats from existing period_store artifacts.
 *
 * Intended to be called on server startup (master only) to avoid a zero
 * last_run timestamp after restarts.
 */
void c4_period_prover_init_from_store(void);

/**
 * Triggered on checkpoint to potentially generate a proof.
 *
 * @param period The finalized period.
 */
void c4_period_prover_on_checkpoint(uint64_t period);

#endif // C4_ETH_PERIOD_PROVER_H
