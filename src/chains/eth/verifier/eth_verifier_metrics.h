/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */

#ifndef C4_ETH_VERIFIER_METRICS_H
#define C4_ETH_VERIFIER_METRICS_H

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Time helper and measurement macros for verifier side
#ifdef ETH_METRICS
#ifdef _WIN32
#include <windows.h>
static inline uint64_t c4_metrics_now_ms(void) {
  FILETIME       ft;
  ULARGE_INTEGER li;
  GetSystemTimeAsFileTime(&ft);
  li.LowPart  = ft.dwLowDateTime;
  li.HighPart = ft.dwHighDateTime;
  return (li.QuadPart - 116444736000000000ULL) / 10000ULL;
}
#else
#include <sys/time.h>
static inline uint64_t c4_metrics_now_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t) tv.tv_sec * 1000ULL + (uint64_t) tv.tv_usec / 1000ULL;
}
#endif
#define MEASURE_START(var)        (var = c4_metrics_now_ms())
#define MEASURE_LAP(dst, since)   (dst = c4_metrics_now_ms() - (since))
#define MEASURE_TOTAL(dst, since) MEASURE_LAP(dst, since)
#define ELAPSED_MS(since)         (c4_metrics_now_ms() - (since))
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
#define ELAPSED_MS(since) (0)
#endif

#ifdef ETH_METRICS
void eth_verifier_metrics_reset(void);
void eth_verifier_metrics_set_read_from_prover(uint64_t ms);
void eth_verifier_metrics_add_evm_run(uint64_t ms);
void eth_verifier_metrics_add_accounts_proof(uint64_t ms);
void eth_verifier_metrics_add_header_verify(uint64_t ms);
void eth_verifier_metrics_set_verify_total(uint64_t ms);
void eth_verifier_metrics_fprint_line(FILE* f);
#endif

#ifdef __cplusplus
}
#endif

#endif // C4_ETH_VERIFIER_METRICS_H
