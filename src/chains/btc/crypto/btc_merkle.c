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

#include "btc_merkle.h"
#include "btc_hash.h"
#include <string.h>

bool btc_merkle_verify(const uint8_t leaf[32], uint32_t index, uint32_t tx_count, bytes_t proof, const uint8_t root[32]) {
  uint8_t        hash[32];
  const uint8_t* siblings;
  uint32_t       sibling_count;
  uint32_t       step;
  uint32_t       idx;
  uint64_t       width;

  if (!leaf || !root || tx_count == 0 || index >= tx_count) return false;
  if (proof.len % 32u != 0) return false;
  if (proof.len && !proof.data) return false;

  siblings      = proof.data;
  sibling_count = proof.len / 32u;
  memcpy(hash, leaf, 32);
  idx   = index;
  width = tx_count;

  for (step = 0; step < sibling_count; step++) {
    const uint8_t* sibling = siblings + (size_t) step * 32u;
    bool           padding;

    if (width <= 1) return false;
    padding = (width & 1ull) != 0 && (uint64_t) idx == width - 1ull;
    if (padding) {
      if (memcmp(sibling, hash, 32) != 0) return false;
      btc_hash256_pair(hash, hash, hash);
    }
    else if (memcmp(sibling, hash, 32) == 0) {
      return false;
    }
    else if ((idx & 1u) == 0) {
      btc_hash256_pair(hash, sibling, hash);
    }
    else {
      btc_hash256_pair(sibling, hash, hash);
    }
    idx >>= 1;
    width = (width + 1ull) >> 1;
  }

  return width == 1 && idx == 0 && memcmp(hash, root, 32) == 0;
}

bool btc_merkle_root(const uint8_t* leaves, uint32_t count, uint8_t root[32], bool* mutated) {
  uint8_t* level;
  uint32_t width;

  if (!leaves || !root || count == 0 || count > BTC_MERKLE_MAX_LEAVES) return false;
  if ((size_t) count + 1u > SIZE_MAX / 32u) return false;
  if (mutated) *mutated = false;

  level = safe_malloc(((size_t) count + 1u) * 32u);
  memcpy(level, leaves, (size_t) count * 32u);
  width = count;

  while (width > 1) {
    uint32_t pairs;
    uint32_t i;

    if (mutated && !*mutated) {
      for (i = 0; i + 1u < width; i += 2u) {
        if (memcmp(level + (size_t) i * 32u, level + (size_t) (i + 1u) * 32u, 32) == 0) {
          *mutated = true;
          break;
        }
      }
    }
    if (width & 1u) {
      memcpy(level + (size_t) width * 32u, level + (size_t) (width - 1u) * 32u, 32);
      width++;
    }
    pairs = width / 2u;
    for (i = 0; i < pairs; i++) {
      btc_hash256_pair(level + (size_t) (2u * i) * 32u, level + (size_t) (2u * i + 1u) * 32u, level + (size_t) i * 32u);
    }
    width = pairs;
  }

  memcpy(root, level, 32);
  safe_free(level);
  return true;
}
