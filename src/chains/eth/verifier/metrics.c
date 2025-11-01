/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */

#include "eth_verifier_metrics.h"

#ifdef ETH_METRICS

#include <stdint.h>
#include <stdio.h>

typedef struct {
  uint64_t read_from_prover_ms;
  uint64_t verify_total_ms;
  uint64_t evm_run_ms;
  uint64_t accounts_proof_ms;
  uint64_t header_verify_ms;
} eth_verifier_metrics_t;

static eth_verifier_metrics_t g_vmetrics = {0};

void eth_verifier_metrics_reset(void) {
  g_vmetrics = (eth_verifier_metrics_t) {0};
}

void eth_verifier_metrics_set_read_from_prover(uint64_t ms) {
  g_vmetrics.read_from_prover_ms = ms;
}

void eth_verifier_metrics_add_evm_run(uint64_t ms) {
  g_vmetrics.evm_run_ms += ms;
}

void eth_verifier_metrics_add_accounts_proof(uint64_t ms) {
  g_vmetrics.accounts_proof_ms += ms;
}

void eth_verifier_metrics_add_header_verify(uint64_t ms) {
  g_vmetrics.header_verify_ms += ms;
}

void eth_verifier_metrics_set_verify_total(uint64_t ms) {
  g_vmetrics.verify_total_ms = ms;
}

void eth_verifier_metrics_fprint_line(FILE* f) {
  if (!f) return;
  fprintf(f,
          "read_from_prover_ms=%llu verify_total_ms=%llu evm_run_ms=%llu accounts_proof_ms=%llu header_verify_ms=%llu\n",
          (unsigned long long) g_vmetrics.read_from_prover_ms,
          (unsigned long long) g_vmetrics.verify_total_ms,
          (unsigned long long) g_vmetrics.evm_run_ms,
          (unsigned long long) g_vmetrics.accounts_proof_ms,
          (unsigned long long) g_vmetrics.header_verify_ms);
}

#endif // ETH_METRICS
