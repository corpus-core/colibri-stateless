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

#include "btc_hash.h"
#include "crypto.h"
#include "sha2.h"
#include <string.h>

/**
 * Reads a little-endian uint32.
 *
 * @param p four bytes
 * @return the integer
 */
static uint32_t btc_read_le32(const uint8_t p[4]) {
  return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

void btc_hash256(bytes_t data, uint8_t out[32]) {
  uint8_t mid[32];
  if (data.len && !data.data) {
    memset(out, 0, 32);
    return;
  }
  sha256(data, mid);
  sha256(bytes(mid, 32), out);
}

bool btc_hash256_parts(const bytes_t* parts, size_t count, uint8_t out[32]) {
  SHA256_CTX ctx;
  uint8_t    mid[32];
  size_t     i;

  if (!out || (count && !parts)) return false;
  for (i = 0; i < count; i++) {
    if (parts[i].len && !parts[i].data) return false;
  }

  sha256_Init(&ctx);
  for (i = 0; i < count; i++) {
    if (parts[i].len) sha256_Update(&ctx, parts[i].data, parts[i].len);
  }
  sha256_Final(&ctx, mid);
  sha256(bytes(mid, 32), out);
  return true;
}

void btc_hash256_pair(const uint8_t left[32], const uint8_t right[32], uint8_t out[32]) {
  uint8_t buf[64];
  memcpy(buf, left, 32);
  memcpy(buf + 32, right, 32);
  btc_hash256(bytes(buf, 64), out);
}

void btc_hash_reverse(const uint8_t in[32], uint8_t out[32]) {
  uint8_t tmp[32];
  int     i;
  for (i = 0; i < 32; i++) tmp[i] = in[31 - i];
  memcpy(out, tmp, 32);
}

int btc_hash_cmp(const uint8_t a[32], const uint8_t b[32]) {
  int i;
  for (i = 31; i >= 0; i--) {
    if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
  }
  return 0;
}

bool btc_target_from_bits(uint32_t bits, uint8_t out[32]) {
  uint32_t size = bits >> 24;
  uint32_t word = bits & 0x007fffffu;
  uint32_t offset;

  memset(out, 0, 32);
  if (word != 0 && (bits & 0x00800000u) != 0) return false;
  if (word != 0 && (size > 34 || (word > 0xffu && size > 33) || (word > 0xffffu && size > 32))) return false;

  if (size <= 3) {
    word >>= 8u * (3u - size);
    out[0] = (uint8_t) word;
    out[1] = (uint8_t) (word >> 8);
    out[2] = (uint8_t) (word >> 16);
  }
  else {
    offset = size - 3;
    if (offset < 32) out[offset] = (uint8_t) word;
    if (offset + 1 < 32) out[offset + 1] = (uint8_t) (word >> 8);
    if (offset + 2 < 32) out[offset + 2] = (uint8_t) (word >> 16);
  }

  for (offset = 0; offset < 32; offset++) {
    if (out[offset] != 0) return true;
  }
  return false;
}

bool btc_header_check(bytes_t header, uint8_t hash_out[32]) {
  uint8_t  target[32];
  uint32_t bits;

  if (!hash_out || !header.data || header.len != BTC_HEADER_SIZE) return false;
  bits = btc_read_le32(header.data + BTC_HEADER_BITS);
  if (!btc_target_from_bits(bits, target)) return false;
  btc_hash256(header, hash_out);
  return btc_hash_cmp(hash_out, target) <= 0;
}

bool btc_check_header_chain(bytes_t headers, uint8_t* first_hash, uint8_t* last_hash) {
  uint8_t  first[32];
  uint8_t  prev[32];
  uint32_t count;
  uint32_t i;

  if (!headers.data || headers.len == 0 || headers.len % BTC_HEADER_SIZE != 0) return false;
  count = headers.len / BTC_HEADER_SIZE;
  for (i = 0; i < count; i++) {
    bytes_t header = bytes(headers.data + (size_t) i * BTC_HEADER_SIZE, BTC_HEADER_SIZE);
    uint8_t hash[32];
    if (!btc_header_check(header, hash)) return false;
    if (i > 0 && memcmp(header.data + BTC_HEADER_PARENT, prev, 32) != 0) return false;
    if (i == 0) memcpy(first, hash, 32);
    memcpy(prev, hash, 32);
  }
  if (last_hash) memcpy(last_hash, prev, 32);
  if (first_hash && first_hash != last_hash) memcpy(first_hash, first, 32);
  return true;
}
