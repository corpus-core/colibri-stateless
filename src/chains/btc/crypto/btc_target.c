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

#include "btc_target.h"
#include "btc_hash.h"
#include "plugin.h"
#include <inttypes.h>
#include <string.h>

/** Mainnet block 840000. Display hash 0000000000000000000320283a032748cef8227873ff4872689bf23f1cda83a5. */
static const uint8_t BTC_CHECKPOINT_HEADER[BTC_HEADER_SIZE] = {
    0x00, 0xe0, 0x5f, 0x2a, 0xab, 0x94, 0x84, 0x91, 0x07, 0x12, 0x65, 0xad, 0x55, 0x23, 0x51, 0xd0,
    0xad, 0x62, 0x57, 0x45, 0x66, 0x8d, 0xa5, 0x4b, 0x01, 0x72, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x4f, 0x89, 0xa5, 0xd7, 0x3b, 0xd4, 0xd4, 0x88, 0x7f, 0x25, 0x98, 0x1f,
    0xe8, 0x18, 0x92, 0xcc, 0xaf, 0xda, 0x10, 0xc2, 0x7f, 0x52, 0xd6, 0xf3, 0xdd, 0x28, 0x18, 0x3a,
    0x7c, 0x41, 0x1b, 0x03, 0xb7, 0x07, 0x23, 0x66, 0x19, 0x42, 0x03, 0x17, 0x7d, 0x98, 0x63, 0xea};

static const uint8_t BTC_CACHE_MAGIC[4] = {'B', 'T', 'C', '1'};

/**
 * Reads a little-endian uint32.
 *
 * @param p four bytes
 * @return the integer
 */
static uint32_t target_read_le32(const uint8_t p[4]) {
  return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

/**
 * Writes a little-endian uint32.
 *
 * @param p four-byte destination
 * @param value integer to write
 */
static void target_write_le32(uint8_t p[4], uint32_t value) {
  p[0] = (uint8_t) value;
  p[1] = (uint8_t) (value >> 8);
  p[2] = (uint8_t) (value >> 16);
  p[3] = (uint8_t) (value >> 24);
}

/**
 * Compact target of the compiled checkpoint header.
 *
 * @return the checkpoint `nBits`
 */
static uint32_t target_checkpoint_bits(void) {
  return target_read_le32(BTC_CHECKPOINT_HEADER + BTC_HEADER_BITS);
}

/**
 * Multiplies a 32-byte little-endian integer by `factor` into 36 bytes.
 *
 * @param src 32-byte little-endian value
 * @param factor multiplier
 * @param dst 36-byte little-endian product
 */
static void target_mul(const uint8_t src[32], uint32_t factor, uint8_t dst[36]) {
  uint64_t carry = 0;
  int      i;
  memset(dst, 0, 36);
  for (i = 0; i < 32; i++) {
    carry += (uint64_t) src[i] * factor;
    dst[i] = (uint8_t) carry;
    carry >>= 8;
  }
  for (i = 32; i < 36 && carry; i++) {
    dst[i] = (uint8_t) carry;
    carry >>= 8;
  }
}

/**
 * Compares two 36-byte little-endian integers.
 *
 * @param a first value
 * @param b second value
 * @return negative if `a < b`, zero if equal, positive if `a > b`
 */
static int target_cmp36(const uint8_t a[36], const uint8_t b[36]) {
  int i;
  for (i = 35; i >= 0; i--) {
    if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
  }
  return 0;
}

/**
 * Returns whether `left * left_factor` is less than or equal to `right * right_factor`.
 *
 * @param left 32-byte little-endian value
 * @param left_factor multiplier for `left`
 * @param right 32-byte little-endian value
 * @param right_factor multiplier for `right`
 * @return true when the scaled left side does not exceed the scaled right side
 */
static bool target_mul_lte(const uint8_t left[32], uint32_t left_factor, const uint8_t right[32], uint32_t right_factor) {
  uint8_t a[36];
  uint8_t b[36];
  target_mul(left, left_factor, a);
  target_mul(right, right_factor, b);
  return target_cmp36(a, b) <= 0;
}

/**
 * Divides a 40-byte little-endian integer by `divisor`.
 *
 * @param value 40-byte value, replaced by the quotient
 * @param divisor divisor, must be non-zero
 * @return false when the quotient does not fit in 32 bytes
 */
static bool target_div40(uint8_t value[40], uint32_t divisor) {
  uint32_t rem = 0;
  int      i;
  if (divisor == 0) return false;
  for (i = 39; i >= 0; i--) {
    uint64_t cur = ((uint64_t) rem << 8) | value[i];
    value[i]     = (uint8_t) (cur / divisor);
    rem          = (uint32_t) (cur % divisor);
  }
  for (i = 32; i < 40; i++) {
    if (value[i] != 0) return false;
  }
  return true;
}

/**
 * Encodes a 32-byte little-endian target as compact `nBits`.
 *
 * @param target little-endian target
 * @return compact form, or 0 when `target` is zero
 */
static uint32_t target_to_bits(const uint8_t target[32]) {
  int      size = 0;
  int      i;
  uint32_t compact;
  for (i = 31; i >= 0; i--) {
    if (target[i] != 0) {
      size = i + 1;
      break;
    }
  }
  if (size == 0) return 0;
  if (size <= 3) {
    compact = target_read_le32(target) << (8u * (uint32_t) (3 - size));
  }
  else {
    compact = (uint32_t) target[size - 3] | ((uint32_t) target[size - 2] << 8) | ((uint32_t) target[size - 1] << 16);
  }
  if (compact & 0x00800000u) {
    compact >>= 8;
    size++;
  }
  return compact | ((uint32_t) size << 24);
}

/**
 * Returns the absolute distance between two epoch numbers.
 *
 * @param a first epoch
 * @param b second epoch
 * @return the distance
 */
static uint32_t target_distance(uint32_t a, uint32_t b) {
  return a > b ? a - b : b - a;
}

/**
 * Resolves NULL limits to the defaults.
 *
 * @param limits caller limits, or NULL
 * @param resolved receives the limits that apply
 * @return false when `max_percent` is above 1000000
 */
static bool target_limits(const btc_target_limits_t* limits, btc_target_limits_t* resolved) {
  if (!limits) {
    btc_target_limits_default(resolved);
    return true;
  }
  if (limits->max_percent > 1000000u) return false;
  *resolved = *limits;
  return true;
}

uint32_t btc_dap(uint32_t height) {
  return height / BTC_EPOCH_LENGTH;
}

bytes_t btc_mainnet_checkpoint_header(void) {
  return bytes((uint8_t*) BTC_CHECKPOINT_HEADER, BTC_HEADER_SIZE);
}

void btc_target_limits_default(btc_target_limits_t* limits) {
  if (!limits) return;
  limits->max_daps    = BTC_TARGET_MAX_DAPS;
  limits->max_percent = BTC_TARGET_MAX_PERCENT;
}

void btc_target_cache_init(btc_target_cache_t* cache) {
  if (!cache) return;
  memset(cache, 0, sizeof(*cache));
  cache->entries[0].dap  = btc_dap(BTC_MAINNET_CHECKPOINT_HEIGHT);
  cache->entries[0].bits = target_checkpoint_bits();
  cache->count           = 1;
}

/**
 * Prefers the harder of two equally distant cache entries.
 *
 * A compact value that decodes beats one that does not. Equal targets fall back to the lower epoch.
 *
 * @param bits_a compact target of the candidate
 * @param dap_a epoch of the candidate
 * @param bits_b compact target of the current selection
 * @param dap_b epoch of the current selection
 * @return negative when the candidate should replace the current selection
 */
static int target_prefer(uint32_t bits_a, uint32_t dap_a, uint32_t bits_b, uint32_t dap_b) {
  uint8_t a[32];
  uint8_t b[32];
  bool    a_ok = btc_target_from_bits(bits_a, a);
  bool    b_ok = btc_target_from_bits(bits_b, b);
  int     cmp;
  if (a_ok != b_ok) return a_ok ? -1 : 1;
  if (a_ok) {
    cmp = btc_hash_cmp(a, b);
    if (cmp != 0) return cmp;
  }
  if (dap_a == dap_b) return 0;
  return dap_a < dap_b ? -1 : 1;
}

bool btc_target_cache_find(const btc_target_cache_t* cache, uint32_t dap, uint32_t* bits, uint32_t* found_dap) {
  uint32_t best_dist = UINT32_MAX;
  uint32_t best      = 0;
  uint32_t i;
  bool     found = false;
  if (!cache || cache->count == 0) return false;
  for (i = 0; i < cache->count; i++) {
    uint32_t dist = target_distance(cache->entries[i].dap, dap);
    if (!found || dist < best_dist ||
        (dist == best_dist && target_prefer(cache->entries[i].bits, cache->entries[i].dap, cache->entries[best].bits, cache->entries[best].dap) < 0)) {
      best      = i;
      best_dist = dist;
      found     = true;
    }
  }
  if (!found) return false;
  if (bits) *bits = cache->entries[best].bits;
  if (found_dap) *found_dap = cache->entries[best].dap;
  return true;
}

bool btc_target_cache_put(btc_target_cache_t* cache, uint32_t dap, uint32_t bits) {
  uint32_t i;
  if (!cache) return false;
  if (dap == btc_dap(BTC_MAINNET_CHECKPOINT_HEIGHT) && bits != target_checkpoint_bits()) return false;
  for (i = 0; i < cache->count; i++) {
    if (cache->entries[i].dap == dap) {
      cache->entries[i].bits = bits;
      return true;
    }
  }
  if (cache->count >= BTC_TARGET_CACHE_MAX) return false;
  i = cache->count;
  while (i > 0 && cache->entries[i - 1].dap > dap) {
    cache->entries[i] = cache->entries[i - 1];
    i--;
  }
  cache->entries[i].dap  = dap;
  cache->entries[i].bits = bits;
  cache->count++;
  return true;
}

bool btc_target_retarget(uint32_t old_bits, uint32_t first_time, uint32_t last_time, const uint8_t pow_limit[32], uint32_t* new_bits) {
  uint8_t  old_target[32];
  uint8_t  product[40];
  int64_t  span;
  uint32_t clamped;
  int      i;
  if (!pow_limit || !new_bits) return false;
  if (!btc_target_from_bits(old_bits, old_target)) return false;

  span = (int64_t) last_time - (int64_t) first_time;
  if (span < (int64_t) (BTC_TARGET_TIMESPAN / 4u)) span = (int64_t) (BTC_TARGET_TIMESPAN / 4u);
  if (span > (int64_t) BTC_TARGET_TIMESPAN * 4) span = (int64_t) BTC_TARGET_TIMESPAN * 4;
  clamped = (uint32_t) span;

  memset(product, 0, sizeof(product));
  {
    uint64_t carry = 0;
    for (i = 0; i < 32; i++) {
      carry += (uint64_t) old_target[i] * clamped;
      product[i] = (uint8_t) carry;
      carry >>= 8;
    }
    for (i = 32; carry && i < 40; i++) {
      product[i] = (uint8_t) carry;
      carry >>= 8;
    }
  }
  if (!target_div40(product, BTC_TARGET_TIMESPAN) || btc_hash_cmp(product, pow_limit) > 0)
    memcpy(product, pow_limit, 32);
  *new_bits = target_to_bits(product);
  return *new_bits != 0;
}

bool btc_target_cache_encode(const btc_target_cache_t* cache, buffer_t* out) {
  uint8_t  header[8];
  uint32_t i;
  if (!cache || !out || cache->count == 0 || cache->count > BTC_TARGET_CACHE_MAX) return false;
  buffer_reset(out);
  memcpy(header, BTC_CACHE_MAGIC, 4);
  target_write_le32(header + 4, cache->count);
  if (buffer_append(out, bytes(header, 8)) != 8) return false;
  for (i = 0; i < cache->count; i++) {
    uint8_t pair[8];
    target_write_le32(pair, cache->entries[i].dap);
    target_write_le32(pair + 4, cache->entries[i].bits);
    if (buffer_append(out, bytes(pair, 8)) != 8) return false;
  }
  return true;
}

bool btc_target_cache_decode(bytes_t blob, btc_target_cache_t* cache) {
  btc_target_cache_t parsed;
  uint32_t           count;
  uint32_t           i;
  uint32_t           checkpoint_bits;
  bool               saw_checkpoint = false;
  if (!cache || !blob.data || blob.len < 8) return false;
  if (memcmp(blob.data, BTC_CACHE_MAGIC, 4) != 0) return false;
  count = target_read_le32(blob.data + 4);
  if (count == 0 || count > BTC_TARGET_CACHE_MAX) return false;
  if (blob.len != 8u + count * 8u) return false;

  memset(&parsed, 0, sizeof(parsed));
  checkpoint_bits = target_checkpoint_bits();
  for (i = 0; i < count; i++) {
    const uint8_t* pair = blob.data + 8u + i * 8u;
    uint32_t       dap  = target_read_le32(pair);
    uint32_t       bits = target_read_le32(pair + 4);
    if (i > 0 && dap <= parsed.entries[i - 1].dap) return false;
    if (dap == btc_dap(BTC_MAINNET_CHECKPOINT_HEIGHT)) {
      if (bits != checkpoint_bits) return false;
      saw_checkpoint = true;
    }
    parsed.entries[i].dap  = dap;
    parsed.entries[i].bits = bits;
  }
  if (!saw_checkpoint) return false;
  parsed.count = count;
  *cache       = parsed;
  return true;
}

/**
 * Builds the storage key for a chain's target cache.
 *
 * @param chain_id chain identifier
 * @param key receives the key
 * @param key_len size of `key`
 * @return false when the key does not fit
 */
static bool target_cache_key(chain_id_t chain_id, char* key, size_t key_len) {
  int wrote = snprintf(key, key_len, "btc_target_%" PRIu64, (uint64_t) chain_id);
  return wrote > 0 && (size_t) wrote < key_len && c4_storage_name_is_safe(key);
}

bool btc_target_cache_load(chain_id_t chain_id, btc_target_cache_t* cache) {
  storage_plugin_t storage = {0};
  buffer_t         blob    = {0};
  char             key[80];
  bool             ok = false;
  if (!cache) return false;
  btc_target_cache_init(cache);
  if (!target_cache_key(chain_id, key, sizeof(key))) return false;
  c4_get_storage_config(&storage);
  if (!storage.get || !storage.get(key, &blob) || !blob.data.data || blob.data.len == 0) {
    buffer_free(&blob);
    return false;
  }
  ok = btc_target_cache_decode(blob.data, cache);
  buffer_free(&blob);
  if (!ok) btc_target_cache_init(cache);
  return ok;
}

bool btc_target_cache_store(chain_id_t chain_id, const btc_target_cache_t* cache) {
  storage_plugin_t storage = {0};
  buffer_t         blob    = {0};
  char             key[80];
  if (!cache || !target_cache_key(chain_id, key, sizeof(key))) return false;
  c4_get_storage_config(&storage);
  if (!storage.set || !btc_target_cache_encode(cache, &blob)) {
    buffer_free(&blob);
    return false;
  }
  storage.set(key, blob.data);
  buffer_free(&blob);
  return true;
}

bool btc_target_authorize(const btc_target_cache_t* cache, const btc_target_limits_t* limits, uint32_t height, uint32_t bits) {
  btc_target_limits_t resolved;
  uint8_t             claimed[32];
  uint8_t             known[32];
  uint32_t            known_bits = 0;
  uint32_t            known_dap  = 0;
  uint32_t            dap;
  uint32_t            dist;
  if (!cache || !target_limits(limits, &resolved)) return false;
  if (!btc_target_from_bits(bits, claimed)) return false;
  if (!btc_target_cache_find(cache, btc_dap(height), &known_bits, &known_dap)) return false;
  if (!btc_target_from_bits(known_bits, known)) return false;

  dap  = btc_dap(height);
  dist = target_distance(dap, known_dap);
  if (dist == 0) return memcmp(claimed, known, 32) == 0;
  if (dist > resolved.max_daps) return false;
  if (dist == 1) {
    return target_mul_lte(claimed, 1, known, 4) && target_mul_lte(known, 1, claimed, 4);
  }
  return target_mul_lte(claimed, 100u, known, 100u + resolved.max_percent);
}

bool btc_target_accept_header(btc_target_cache_t* cache, const btc_target_limits_t* limits, uint32_t height, bytes_t header) {
  uint8_t  hash[32];
  uint8_t  checkpoint_hash[32];
  uint32_t bits;
  if (!cache || !btc_header_check(header, hash)) return false;
  bits = target_read_le32(header.data + BTC_HEADER_BITS);
  if (!btc_header_check(btc_mainnet_checkpoint_header(), checkpoint_hash)) return false;
  if (memcmp(hash, checkpoint_hash, 32) == 0)
    return height == BTC_MAINNET_CHECKPOINT_HEIGHT && bits == target_checkpoint_bits() && btc_target_cache_put(cache, btc_dap(height), bits);
  if (!btc_target_authorize(cache, limits, height, bits)) return false;
  return btc_target_cache_put(cache, btc_dap(height), bits);
}

bool btc_target_accept_path(btc_target_cache_t* cache, const btc_target_limits_t* limits, const uint32_t* heights, bytes_t headers, uint32_t count) {
  btc_target_cache_t working;
  uint32_t           i;
  int                direction = 0;
  if (!cache || !heights || count == 0 || count > BTC_TARGET_PATH_LIMIT) return false;
  if (!headers.data || headers.len != count * BTC_HEADER_SIZE) return false;
  working = *cache;
  for (i = 0; i < count; i++) {
    uint32_t dap = btc_dap(heights[i]);
    if (i > 0) {
      uint32_t prev = btc_dap(heights[i - 1]);
      int      step = dap > prev ? 1 : dap < prev ? -1
                                                  : 0;
      if (step == 0) return false;
      if (direction == 0) direction = step;
      if (step != direction) return false;
    }
    if (!btc_target_accept_header(&working, limits, heights[i], bytes(headers.data + (size_t) i * BTC_HEADER_SIZE, BTC_HEADER_SIZE)))
      return false;
  }
  *cache = working;
  return true;
}
