/*
 * Copyright (c) 2025,2026 corpus.core
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include "retry_delay.h"
#include "bytes.h"
#include "plugin.h"
#include <stdio.h>
#include <string.h>

// Asymmetric learning gains expressed as right-shifts (powers of two) so the
// arithmetic is exact on integers and obvious from the code. Up: react fast
// (1/2 of the gap); Down: probe slowly (1/8 of the gap). The factors were
// chosen so that a single under-estimate quickly catches up (one extra retry
// roughly doubles the base) while a slightly over-estimated base takes many
// successful R=0 outcomes to drift down (avoiding an extra retry just to
// "save" a few hundred ms).
#define RETRY_DELAY_UP_SHIFT   1
#define RETRY_DELAY_DOWN_SHIFT 3

// On R == 0 (first request already succeeded) we have no measurement of how
// much faster the node could be; probe downward by 25% as a conservative
// guess. Combined with DOWN_SHIFT the effective base reduction is
// (1 - 3/4) / 8 = 1/32 of the current base per R=0 success.
#define RETRY_DELAY_R0_TARGET_NUM 3
#define RETRY_DELAY_R0_TARGET_DEN 4

// Persisted blob layout: 1 byte version || 4 bytes uint32 LE base_ms.
// The version byte lets us evolve the format without breaking older state
// files (rejected on mismatch -> falls back to the default).
#define RETRY_DELAY_BLOB_VERSION 1u
#define RETRY_DELAY_BLOB_SIZE    5u

// Fixed-size in-memory cache. Sized to comfortably hold a handful of
// (category, chain) pairs (today: 1 category x ~2-3 active chains). Overflow
// reuses slot 0; the resulting churn just costs an extra storage lookup.
#define RETRY_DELAY_CACHE_SIZE 8

typedef struct {
  const char* category;
  chain_id_t  chain;
  uint32_t    base_ms;
  bool        in_use;
} retry_delay_entry_t;

static retry_delay_entry_t g_cache[RETRY_DELAY_CACHE_SIZE];

static void build_key(const char* category, chain_id_t chain, char* buf, size_t len) {
  snprintf(buf, len, "rdelay_%s_%llu", category, (unsigned long long) chain);
}

static uint32_t clamp_base(uint32_t base) {
  if (base < C4_RETRY_DELAY_MIN_MS) return C4_RETRY_DELAY_MIN_MS;
  if (base > C4_RETRY_DELAY_MAX_MS) return C4_RETRY_DELAY_MAX_MS;
  return base;
}

static bool decode_base(bytes_t blob, uint32_t* out) {
  if (blob.len != RETRY_DELAY_BLOB_SIZE || blob.data == NULL) return false;
  if (blob.data[0] != RETRY_DELAY_BLOB_VERSION) return false;
  uint32_t v = (uint32_t) blob.data[1] |
               ((uint32_t) blob.data[2] << 8) |
               ((uint32_t) blob.data[3] << 16) |
               ((uint32_t) blob.data[4] << 24);
  *out = clamp_base(v);
  return true;
}

static void encode_base(uint32_t base, uint8_t buf[RETRY_DELAY_BLOB_SIZE]) {
  buf[0] = (uint8_t) RETRY_DELAY_BLOB_VERSION;
  buf[1] = (uint8_t) (base & 0xff);
  buf[2] = (uint8_t) ((base >> 8) & 0xff);
  buf[3] = (uint8_t) ((base >> 16) & 0xff);
  buf[4] = (uint8_t) ((base >> 24) & 0xff);
}

static retry_delay_entry_t* lookup_entry(const char* category, chain_id_t chain) {
  retry_delay_entry_t* free_slot = NULL;
  for (size_t i = 0; i < RETRY_DELAY_CACHE_SIZE; i++) {
    retry_delay_entry_t* e = &g_cache[i];
    if (!e->in_use) {
      if (!free_slot) free_slot = e;
      continue;
    }
    if (e->chain == chain && e->category && strcmp(e->category, category) == 0) return e;
  }
  // Cache full: reuse slot 0. With only a few categories x chains this is a
  // pathological case; the worst outcome is one extra storage lookup.
  if (!free_slot) free_slot = &g_cache[0];

  uint32_t         base   = C4_RETRY_DELAY_DEFAULT_MS;
  storage_plugin_t plugin = {0};
  c4_get_storage_config(&plugin);
  if (plugin.get) {
    char     key[64];
    buffer_t buf = {0};
    build_key(category, chain, key, sizeof(key));
    if (plugin.get(key, &buf)) {
      uint32_t parsed = 0;
      if (decode_base(buf.data, &parsed)) base = parsed;
    }
    buffer_free(&buf);
  }

  free_slot->category = category;
  free_slot->chain    = chain;
  free_slot->base_ms  = base;
  free_slot->in_use   = true;
  return free_slot;
}

static void persist_entry(const retry_delay_entry_t* e) {
  storage_plugin_t plugin = {0};
  c4_get_storage_config(&plugin);
  if (!plugin.set) return;

  char    key[64];
  uint8_t blob[RETRY_DELAY_BLOB_SIZE];
  build_key(e->category, e->chain, key, sizeof(key));
  encode_base(e->base_ms, blob);
  plugin.set(key, bytes(blob, RETRY_DELAY_BLOB_SIZE));
}

uint32_t c4_retry_delay_for(const char* category, chain_id_t chain, uint16_t retry_count) {
  if (!category) return C4_RETRY_DELAY_DEFAULT_MS;
  retry_delay_entry_t* e     = lookup_entry(category, chain);
  uint32_t             delay = e->base_ms;
  for (uint16_t i = 0; i < retry_count && delay < C4_RETRY_DELAY_MAX_MS; i++)
    delay <<= 1;
  return delay > C4_RETRY_DELAY_MAX_MS ? C4_RETRY_DELAY_MAX_MS : delay;
}

void c4_retry_delay_observe(const char* category, chain_id_t chain, uint16_t retry_count) {
  if (!category) return;
  retry_delay_entry_t* e    = lookup_entry(category, chain);
  uint32_t             base = e->base_ms;

  // Translate the (retry_count, base) outcome into a target base for next time:
  //   R == 0: server was already warm -> probe down to 0.75 * base.
  //   R >= 1: warm-up time was approximately base * (2^R - 1) (the cumulative
  //           scheduled wait at the successful retry). R == 1 hits target=base
  //           exactly, which means "no change" -- the sweet spot.
  uint32_t target;
  if (retry_count == 0) {
    target = (uint32_t) ((uint64_t) base * RETRY_DELAY_R0_TARGET_NUM / RETRY_DELAY_R0_TARGET_DEN);
  }
  else {
    // Compute (2^R - 1), saturating in uint64_t to keep the multiplication
    // safe. The final value is clamped to MAX anyway.
    uint64_t mult = 0;
    uint64_t cur  = 1;
    for (uint16_t i = 0; i < retry_count; i++) {
      mult += cur;
      // Saturate the running shift; once mult exceeds MAX/base, further bits
      // would only push us higher than the cap.
      if (cur > (uint64_t) C4_RETRY_DELAY_MAX_MS) break;
      cur <<= 1;
    }
    uint64_t t = mult * (uint64_t) base;
    target     = t > (uint64_t) C4_RETRY_DELAY_MAX_MS ? C4_RETRY_DELAY_MAX_MS : (uint32_t) t;
  }

  // Round the (gap >> shift) step up so a 1-unit gap still moves us by 1.
  // Without this, integer truncation would stall the base just shy of the
  // target (e.g. forever at 7999 instead of reaching MAX=8000).
  uint32_t new_base;
  if (target > base) {
    uint32_t gap  = target - base;
    uint32_t step = (gap + (1u << RETRY_DELAY_UP_SHIFT) - 1u) >> RETRY_DELAY_UP_SHIFT;
    new_base      = base + step;
  }
  else if (target < base) {
    uint32_t gap  = base - target;
    uint32_t step = (gap + (1u << RETRY_DELAY_DOWN_SHIFT) - 1u) >> RETRY_DELAY_DOWN_SHIFT;
    new_base      = base - step;
  }
  else
    new_base = base;

  new_base = clamp_base(new_base);
  if (new_base == e->base_ms) return;
  e->base_ms = new_base;
  persist_entry(e);
}

void c4_retry_delay_reset(void) {
  memset(g_cache, 0, sizeof(g_cache));
}
