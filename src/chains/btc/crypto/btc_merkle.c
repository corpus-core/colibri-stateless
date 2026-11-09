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

/**
 * Width of a merkle level. `height` 0 is the leaf level.
 *
 * @param tx_count number of transactions
 * @param height level above the leaves
 * @return number of nodes on that level
 */
static uint32_t merkleblock_width(uint32_t tx_count, int height) {
  return (uint32_t) (((uint64_t) tx_count + (1ull << height) - 1ull) >> height);
}

/**
 * Reads a little-endian uint32.
 *
 * @param p cursor, advanced on success
 * @param end first byte past the buffer
 * @param out receives the value
 * @return true when four bytes were available
 */
static bool merkleblock_u32(const uint8_t** p, const uint8_t* end, uint32_t* out) {
  if (*p + 4 > end) return false;
  *out = (uint32_t) (*p)[0] | ((uint32_t) (*p)[1] << 8) | ((uint32_t) (*p)[2] << 16) | ((uint32_t) (*p)[3] << 24);
  *p += 4;
  return true;
}

/**
 * Reads a minimally encoded Bitcoin compact size.
 *
 * @param p cursor, advanced on success
 * @param end first byte past the buffer
 * @param out receives the value
 * @return true when the encoding is canonical and in range
 */
static bool merkleblock_compact(const uint8_t** p, const uint8_t* end, uint64_t* out) {
  uint8_t tag;
  if (*p >= end) return false;
  tag = *(*p)++;
  if (tag < 253) {
    *out = tag;
    return true;
  }
  if (tag == 253) {
    uint64_t value;
    if (*p + 2 > end) return false;
    value = (uint64_t) (*p)[0] | ((uint64_t) (*p)[1] << 8);
    *p += 2;
    if (value < 253) return false;
    *out = value;
    return true;
  }
  if (tag == 254) {
    uint32_t value;
    if (!merkleblock_u32(p, end, &value) || value < 0x10000u) return false;
    *out = value;
    return true;
  }
  return false;
}

/**
 * Walks one partial-merkle node and records left-spine siblings.
 *
 * @param hashes first internal-order hash
 * @param hash_count number of hashes
 * @param hash_used hashes consumed so far
 * @param bits packed match flags
 * @param bit_count number of flags
 * @param bit_used flags consumed so far
 * @param siblings sibling buffer, 32 bytes each
 * @param sibling_count siblings recorded so far
 * @param height level above the leaves
 * @param pos node index on that level
 * @param tx_count transaction count
 * @param spine true along the left edge that reaches the coinbase
 * @param out receives this node's hash
 * @return true when the node is well formed and only the coinbase matches
 */
static bool merkleblock_walk(const uint8_t* hashes, uint32_t hash_count, uint32_t* hash_used, const uint8_t* bits, uint32_t bit_count, uint32_t* bit_used, uint8_t* siblings, uint32_t* sibling_count, int height, uint32_t pos, uint32_t tx_count, bool spine, uint8_t out[32]) {
  bool     match;
  uint8_t  left[32];
  uint8_t  right[32];
  uint32_t below;
  if (*bit_used >= bit_count) return false;
  match = (bits[*bit_used / 8u] >> (*bit_used % 8u)) & 1u;
  (*bit_used)++;
  if (height == 0 || !match) {
    if (*hash_used >= hash_count) return false;
    memcpy(out, hashes + (size_t) (*hash_used) * 32u, 32);
    (*hash_used)++;
    if (height == 0 && spine && pos == 0 && !match) return false;
    if (height == 0 && match && !(spine && pos == 0)) return false;
    return true;
  }
  if (!merkleblock_walk(hashes, hash_count, hash_used, bits, bit_count, bit_used, siblings, sibling_count, height - 1, pos * 2u, tx_count, spine && pos == 0, left)) return false;
  below = merkleblock_width(tx_count, height - 1);
  if (pos * 2u + 1u < below) {
    if (!merkleblock_walk(hashes, hash_count, hash_used, bits, bit_count, bit_used, siblings, sibling_count, height - 1, pos * 2u + 1u, tx_count, false, right)) return false;
  }
  else
    memcpy(right, left, 32);
  if (spine && pos == 0) {
    if (*sibling_count >= 32u) return false;
    memcpy(siblings + (size_t) (*sibling_count) * 32u, right, 32);
    (*sibling_count)++;
  }
  btc_hash256_pair(left, right, out);
  return true;
}

bool btc_merkleblock_coinbase(bytes_t raw, uint8_t header[80], uint32_t* tx_count, uint8_t* siblings, uint32_t* sibling_count) {
  const uint8_t* p;
  const uint8_t* end;
  const uint8_t* hashes;
  const uint8_t* bits;
  uint64_t       hash_count64;
  uint64_t       flag_bytes64;
  uint32_t       hash_count;
  uint32_t       flag_bytes;
  uint32_t       bit_count;
  uint32_t       hash_used;
  uint32_t       bit_used;
  uint32_t       count;
  uint32_t       nsib;
  int            height;
  uint8_t        root[32];

  if (!raw.data || !header || !tx_count || !siblings || !sibling_count) return false;
  if (raw.len < BTC_HEADER_SIZE + 4u || raw.len > 8192u) return false;
  p   = raw.data;
  end = raw.data + raw.len;
  memcpy(header, p, BTC_HEADER_SIZE);
  p += BTC_HEADER_SIZE;
  if (!merkleblock_u32(&p, end, &count) || count == 0) return false;
  if (!merkleblock_compact(&p, end, &hash_count64) || hash_count64 == 0 || hash_count64 > 33u) return false;
  hash_count = (uint32_t) hash_count64;
  if (p + (size_t) hash_count * 32u > end) return false;
  hashes = p;
  p += (size_t) hash_count * 32u;
  // Bitcoin Core stores the number of flag bytes, not the number of bits.
  if (!merkleblock_compact(&p, end, &flag_bytes64) || flag_bytes64 == 0 || flag_bytes64 > 32u) return false;
  flag_bytes = (uint32_t) flag_bytes64;
  if (p + flag_bytes != end) return false;
  bits      = p;
  bit_count = flag_bytes * 8u;

  height = 0;
  while (merkleblock_width(count, height) > 1) {
    height++;
    if (height > 32) return false;
  }
  hash_used = 0;
  bit_used  = 0;
  nsib      = 0;
  if (!merkleblock_walk(hashes, hash_count, &hash_used, bits, bit_count, &bit_used, siblings, &nsib, height, 0, count, true, root)) return false;
  if (hash_used != hash_count || (bit_used + 7u) / 8u != flag_bytes || nsib != (uint32_t) height) return false;
  if (memcmp(root, header + BTC_HEADER_MERKLE, 32) != 0) return false;
  *tx_count      = count;
  *sibling_count = nsib;
  return true;
}
