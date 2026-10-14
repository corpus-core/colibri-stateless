/*
 * Copyright (c) 2025 corpus.core
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

#include "btc_proof.h"
#include "btc_hash.h"
#include <string.h>

/**
 * Checks that `finals` continues `header` by parent hash and proof-of-work.
 *
 * @param header 80-byte step header
 * @param finals list of 80-byte headers, possibly empty
 * @return true when every finality header links and meets its target
 */
static bool proof_final_headers(bytes_t header, ssz_ob_t finals) {
  uint8_t  prev[32];
  uint32_t n;
  uint32_t i;
  if (!btc_header_check(header, prev)) return false;
  if (!finals.def) return false;
  n = ssz_len(finals);
  for (i = 0; i < n; i++) {
    ssz_ob_t item = ssz_at(finals, i);
    if (!item.bytes.data || item.bytes.len != BTC_HEADER_SIZE) return false;
    if (memcmp(item.bytes.data + BTC_HEADER_PARENT, prev, 32) != 0) return false;
    if (!btc_header_check(item.bytes, prev)) return false;
  }
  return true;
}

bool btc_sync_accept_epoch_path(ssz_ob_t sync_data, btc_target_cache_t* cache, const btc_target_limits_t* limits) {
  ssz_ob_t steps;
  uint32_t heights[BTC_TARGET_PATH_LIMIT];
  uint8_t  headers[BTC_TARGET_PATH_LIMIT * BTC_HEADER_SIZE];
  uint32_t count;
  uint32_t i;
  if (!cache) return false;
  if (!sync_data.def || sync_data.def->type == SSZ_TYPE_NONE) return true;
  if (!sync_data.def->name || strcmp(sync_data.def->name, "BtcEpochPath") != 0) return false;
  if (!sync_data.bytes.data || !ssz_is_valid(sync_data, true, NULL)) return false;

  steps = ssz_get(&sync_data, "steps");
  count = ssz_len(steps);
  if (count == 0 || count > BTC_TARGET_PATH_LIMIT) return false;
  for (i = 0; i < count; i++) {
    ssz_ob_t step   = ssz_at(steps, i);
    ssz_ob_t header = ssz_get(&step, "header");
    ssz_ob_t finals = ssz_get(&step, "final_headers");
    if (!header.bytes.data || header.bytes.len != BTC_HEADER_SIZE) return false;
    if (!proof_final_headers(header.bytes, finals)) return false;
    heights[i] = ssz_get_uint32(&step, "height");
    memcpy(headers + (size_t) i * BTC_HEADER_SIZE, header.bytes.data, BTC_HEADER_SIZE);
  }
  return btc_target_accept_path(cache, limits, heights, bytes(headers, count * BTC_HEADER_SIZE), count);
}
