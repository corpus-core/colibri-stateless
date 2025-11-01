/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */

#ifndef C4_ETH_SERVER_METRICS_H
#define C4_ETH_SERVER_METRICS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Time helper and measurement macros live here to avoid duplication
#ifdef ETH_METRICS
// Prefer prover's current_ms() if available; otherwise fall back to gettimeofday()
#include "prover.h"
#ifdef PROVER_CACHE
static inline uint64_t c4_metrics_now_ms(void) { return current_ms(); }
#else
#ifdef _WIN32
#include <windows.h>
static inline uint64_t c4_metrics_now_ms(void) {
  FILETIME       ft;
  ULARGE_INTEGER li;
  GetSystemTimeAsFileTime(&ft);
  li.LowPart  = ft.dwLowDateTime;
  li.HighPart = ft.dwHighDateTime;
  return (li.QuadPart - 116444736000000000ULL) / 10000ULL; // ms
}
#else
#include <sys/time.h>
static inline uint64_t c4_metrics_now_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t) tv.tv_sec * 1000ULL + (uint64_t) tv.tv_usec / 1000ULL;
}
#endif
#endif // PROVER_CACHE
#define MEASURE_START(var)        (var = c4_metrics_now_ms())
#define MEASURE_LAP(dst, since)   (dst = c4_metrics_now_ms() - (since))
#define MEASURE_TOTAL(dst, since) MEASURE_LAP(dst, since)
#else
#define MEASURE_START(var) \
  do { (void) (var); } while (0)
#define MEASURE_LAP(dst, since) \
  do {                          \
    (void) (dst);               \
    (void) (since);             \
  } while (0)
#define MEASURE_TOTAL(dst, since) \
  do {                            \
    (void) (dst);                 \
    (void) (since);               \
  } while (0)
#endif

#ifdef ETH_METRICS
// Records timing metrics for a single eth_call proof build on the prover side
// All values are in milliseconds
#ifdef C4_ETH_SERVER_METRICS_IMPL
void eth_metrics_record_prover_eth_call(
    uint64_t beacon_ms,
    uint64_t debug_trace_ms,
    uint64_t check_blockroot_ms,
    uint64_t proofs_ms,
    uint64_t build_ms,
    uint32_t num_accounts,
    uint64_t total_ms);
#else
static inline void eth_metrics_record_prover_eth_call(
    uint64_t beacon_ms,
    uint64_t debug_trace_ms,
    uint64_t check_blockroot_ms,
    uint64_t proofs_ms,
    uint64_t build_ms,
    uint32_t num_accounts,
    uint64_t total_ms) {
  (void) beacon_ms;
  (void) debug_trace_ms;
  (void) check_blockroot_ms;
  (void) proofs_ms;
  (void) build_ms;
  (void) num_accounts;
  (void) total_ms;
}
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif // C4_ETH_SERVER_METRICS_H
