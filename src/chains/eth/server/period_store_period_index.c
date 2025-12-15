/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */
#include "eth_conf.h"
#include "logger.h"
#include "period_store.h"
#include "uv_util.h"
#include <stdlib.h>
#include <string.h>
#include <uv.h>

static bool     g_initialized = false;
static bool     g_has_gaps    = false;
static bool     g_has_any     = false;
static uint64_t g_min_period  = 0;
static uint64_t g_max_period  = 0;

static int cmp_u64(const void* a, const void* b) {
  uint64_t aa = *(const uint64_t*) a;
  uint64_t bb = *(const uint64_t*) b;
  if (aa < bb) return -1;
  if (aa > bb) return 1;
  return 0;
}

static bool is_numeric_str(const char* s) {
  if (!s || !*s) return false;
  for (const char* p = s; *p; p++) {
    if (*p < '0' || *p > '9') return false;
  }
  return true;
}

void c4_ps_period_index_init_if_needed(void) {
  if (g_initialized) return;
  g_initialized = true;
  g_has_gaps    = false;
  g_has_any     = false;

  if (!eth_config.period_store) return;

  uv_fs_t req = {0};
  int     rc  = uv_fs_scandir(uv_default_loop(), &req, eth_config.period_store, 0, NULL);
  if (rc < 0) {
    C4_UV_LOG_ERR_NEG("period_store index: uv_fs_scandir", rc);
    uv_fs_req_cleanup(&req);
    return;
  }

  uint64_t* periods = (uint64_t*) safe_calloc((size_t) rc + 1, sizeof(uint64_t));
  size_t    n       = 0;

  uv_dirent_t ent;
  while (uv_fs_scandir_next(&req, &ent) != UV_EOF) {
    if (ent.type != UV_DIRENT_DIR) continue;
    if (!is_numeric_str(ent.name)) continue;
    periods[n++] = (uint64_t) strtoull(ent.name, NULL, 10);
  }
  uv_fs_req_cleanup(&req);

  if (n > 1) qsort(periods, n, sizeof(uint64_t), cmp_u64);
  // Deduplicate and detect gaps
  if (n > 1) {
    size_t w = 1;
    for (size_t r = 1; r < n; r++) {
      if (periods[r] == periods[w - 1]) continue;
      if (periods[r] - periods[w - 1] > 1) {
        log_error("period_store: period directory out of order (detected a gap): %l < %l", periods[r], periods[w - 1]);
        g_has_gaps = true;
      }
      periods[w++] = periods[r];
    }
    n = w;
  }

  if (n == 0) {
    safe_free(periods);
    return;
  }

  g_has_any    = true;
  g_min_period = periods[0];
  g_max_period = periods[n - 1];
  safe_free(periods);
}

void c4_ps_period_index_on_period_dir(uint64_t period) {
  c4_ps_period_index_init_if_needed();

  if (g_has_gaps) return;

  if (!g_has_any) {
    g_has_any    = true;
    g_min_period = period;
    g_max_period = period;
    return;
  }

  // We assume contiguous range. Allow extending at both ends by exactly 1.
  if (period == g_max_period + 1) {
    g_max_period = period;
    return;
  }
  if (period + 1 == g_min_period) {
    g_min_period = period;
    return;
  }
  if (period >= g_min_period && period <= g_max_period)
    // Already implied to exist in the contiguous range.
    return;

  // Any other insertion would introduce a gap. This is a critical integrity issue.
  log_error("period_store: period directory gap introduced at runtime (range=%l..%l, new=%l)", g_min_period, g_max_period, period);
  g_has_gaps = true;
}

bool c4_ps_period_index_has_gaps(void) {
  c4_ps_period_index_init_if_needed();
  return g_has_gaps;
}

bool c4_ps_period_index_get_contiguous_from(uint64_t start_period, uint64_t* first_period, uint64_t* last_period) {
  c4_ps_period_index_init_if_needed();
  if (first_period) *first_period = 0;
  if (last_period) *last_period = 0;
  if (!g_has_any) return false;
  if (g_has_gaps) return false;

  uint64_t first = start_period;
  if (first < g_min_period) first = g_min_period;
  if (first > g_max_period) return false;

  if (first_period) *first_period = first;
  if (last_period) *last_period = g_max_period;
  return true;
}
