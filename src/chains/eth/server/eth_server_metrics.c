/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */

#define C4_ETH_SERVER_METRICS_IMPL 1
#include "eth_server_metrics.h"
#include "server/server.h" // http_server_t, buffer_t, bprintf

#ifdef ETH_METRICS

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint64_t sum;
  uint64_t count;
  uint64_t last;
} metric_accum_t;

static struct {
  metric_accum_t beacon_ms;
  metric_accum_t debug_trace_ms;
  metric_accum_t check_blockroot_ms;
  metric_accum_t get_proofs_ms;
  metric_accum_t build_proof_ms;
  metric_accum_t total_ms;
  metric_accum_t accounts; // num accounts per call (treated as gauge/summary)
} g_eth_metrics = {0};

static inline void accum_add(metric_accum_t* m, uint64_t v) {
  m->sum += v;
  m->count += 1;
  m->last = v;
}

void eth_metrics_record_prover_eth_call(
    uint64_t beacon_ms,
    uint64_t debug_trace_ms,
    uint64_t check_blockroot_ms,
    uint64_t proofs_ms,
    uint64_t build_ms,
    uint32_t num_accounts,
    uint64_t total_ms) {
  accum_add(&g_eth_metrics.beacon_ms, beacon_ms);
  accum_add(&g_eth_metrics.debug_trace_ms, debug_trace_ms);
  accum_add(&g_eth_metrics.check_blockroot_ms, check_blockroot_ms);
  accum_add(&g_eth_metrics.get_proofs_ms, proofs_ms);
  accum_add(&g_eth_metrics.build_proof_ms, build_ms);
  accum_add(&g_eth_metrics.total_ms, total_ms);
  accum_add(&g_eth_metrics.accounts, (uint64_t) num_accounts);
}

#endif // ETH_METRICS

// Prometheus exposition (always define symbol; prints only when enabled)
void eth_server_metrics(http_server_t* server, buffer_t* data) {
#ifdef ETH_METRICS
  // HELP/TYPE for each counter/gauge
  bprintf(data, "# HELP colibri_eth_call_prover_beacon_ms_sum Total time spent fetching beacon blocks (ms).\n");
  bprintf(data, "# TYPE colibri_eth_call_prover_beacon_ms_sum counter\n");
  bprintf(data, "colibri_eth_call_prover_beacon_ms_sum{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, g_eth_metrics.beacon_ms.sum);
  bprintf(data, "colibri_eth_call_prover_beacon_ms_count{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, g_eth_metrics.beacon_ms.count);
  bprintf(data, "colibri_eth_call_prover_beacon_ms_last{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, g_eth_metrics.beacon_ms.last);

  bprintf(data, "# HELP colibri_eth_call_prover_debug_trace_ms_sum Total time spent in debug_traceCall (ms).\n");
  bprintf(data, "# TYPE colibri_eth_call_prover_debug_trace_ms_sum counter\n");
  bprintf(data, "colibri_eth_call_prover_debug_trace_ms_sum{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, g_eth_metrics.debug_trace_ms.sum);
  bprintf(data, "colibri_eth_call_prover_debug_trace_ms_count{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, g_eth_metrics.debug_trace_ms.count);
  bprintf(data, "colibri_eth_call_prover_debug_trace_ms_last{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, g_eth_metrics.debug_trace_ms.last);

  bprintf(data, "# HELP colibri_eth_call_prover_check_blockroot_ms_sum Total time spent in check_blockroot (ms).\n");
  bprintf(data, "# TYPE colibri_eth_call_prover_check_blockroot_ms_sum counter\n");
  bprintf(data, "colibri_eth_call_prover_check_blockroot_ms_sum{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, g_eth_metrics.check_blockroot_ms.sum);
  bprintf(data, "colibri_eth_call_prover_check_blockroot_ms_count{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, g_eth_metrics.check_blockroot_ms.count);
  bprintf(data, "colibri_eth_call_prover_check_blockroot_ms_last{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, g_eth_metrics.check_blockroot_ms.last);

  bprintf(data, "# HELP colibri_eth_call_prover_get_proofs_ms_sum Total time spent in eth_getProof aggregation (ms).\n");
  bprintf(data, "# TYPE colibri_eth_call_prover_get_proofs_ms_sum counter\n");
  bprintf(data, "colibri_eth_call_prover_get_proofs_ms_sum{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, g_eth_metrics.get_proofs_ms.sum);
  bprintf(data, "colibri_eth_call_prover_get_proofs_ms_count{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, g_eth_metrics.get_proofs_ms.count);
  bprintf(data, "colibri_eth_call_prover_get_proofs_ms_last{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, g_eth_metrics.get_proofs_ms.last);

  bprintf(data, "# HELP colibri_eth_call_prover_build_proof_ms_sum Total time spent constructing the proof (ms).\n");
  bprintf(data, "# TYPE colibri_eth_call_prover_build_proof_ms_sum counter\n");
  bprintf(data, "colibri_eth_call_prover_build_proof_ms_sum{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, g_eth_metrics.build_proof_ms.sum);
  bprintf(data, "colibri_eth_call_prover_build_proof_ms_count{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, g_eth_metrics.build_proof_ms.count);
  bprintf(data, "colibri_eth_call_prover_build_proof_ms_last{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, g_eth_metrics.build_proof_ms.last);

  bprintf(data, "# HELP colibri_eth_call_prover_total_ms_sum Total time per request end-to-end (ms).\n");
  bprintf(data, "# TYPE colibri_eth_call_prover_total_ms_sum counter\n");
  bprintf(data, "colibri_eth_call_prover_total_ms_sum{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, g_eth_metrics.total_ms.sum);
  bprintf(data, "colibri_eth_call_prover_total_ms_count{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, g_eth_metrics.total_ms.count);
  bprintf(data, "colibri_eth_call_prover_total_ms_last{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, g_eth_metrics.total_ms.last);

  bprintf(data, "# HELP colibri_eth_call_prover_accounts_sum Number of accounts used (sum).\n");
  bprintf(data, "# TYPE colibri_eth_call_prover_accounts_sum counter\n");
  bprintf(data, "colibri_eth_call_prover_accounts_sum{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, g_eth_metrics.accounts.sum);
  bprintf(data, "colibri_eth_call_prover_accounts_count{chain_id=\"%d\"} %l\n", (uint32_t) server->chain_id, g_eth_metrics.accounts.count);
  bprintf(data, "colibri_eth_call_prover_accounts_last{chain_id=\"%d\"} %l\n\n", (uint32_t) server->chain_id, g_eth_metrics.accounts.last);
  (void) server;
  (void) data; // keep analyzers happy in both branches
#else
  (void) server;
  (void) data;
#endif
}
