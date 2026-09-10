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

#include "rlp.h"

// True if header (1 prefix byte + header_extra length bytes) and payload_len fit in src.
static bool payload_fits(uint32_t src_len, size_t src_idx, size_t header_extra, size_t payload_len) {
  if (src_idx >= src_len) return false;
  size_t remaining = (size_t) src_len - src_idx;
  size_t header    = 1 + header_extra;
  return header <= remaining && payload_len <= remaining - header;
}

// Read an RLP long-length prefix (1..8 big-endian bytes after src_idx). Rejects truncated prefixes and payloads.
static bool read_long_len(const bytes_t* src, size_t src_idx, size_t n_len, size_t* payload_len) {
  if (!n_len || n_len > 8 || src_idx >= src->len) return false;
  size_t remaining = (size_t) src->len - src_idx;
  if (remaining < 1 + n_len) return false;

  uint64_t len = 0;
  for (size_t i = 0; i < n_len; i++)
    len = (len << 8) | (uint64_t) src->data[src_idx + 1 + i];

  size_t max_payload = remaining - 1 - n_len;
  if (len > (uint64_t) max_payload) return false;
  *payload_len = (size_t) len;
  return true;
}

static int check_range(bytes_t* target, bytes_t* src, size_t new_len, uint8_t* new_start, rlp_type_t result_type) {
  if (!target || !src->data || !new_start || new_start < src->data) return RLP_OUT_OF_RANGE;
  size_t offset = (size_t) (new_start - src->data);
  if (offset > src->len || new_len > (size_t) src->len - offset) return RLP_OUT_OF_RANGE;
  *target = bytes(new_start, (uint32_t) new_len);
  return (int) result_type;
}

INTERNAL rlp_type_t rlp_decode(bytes_t* src, int index, bytes_t* target) {
  size_t pos = 0, src_idx = 0;
  for (; src_idx < src->len; src_idx++, pos++) {
    uint8_t c           = src->data[src_idx];
    bool    match_index = index >= 0 && (size_t) index == pos;
    if (c < 0x80) {
      if (match_index)
        return check_range(target, src, 1, src->data + src_idx, RLP_ITEM);
    }
    else if (c < 0xb8) {
      size_t len = (size_t) (c - 0x80);
      if (!payload_fits(src->len, src_idx, 0, len)) return RLP_OUT_OF_RANGE;
      if (match_index)
        return check_range(target, src, len, src->data + src_idx + 1, RLP_ITEM);
      src_idx += len;
    }
    else if (c < 0xc0) {
      size_t n_len = (size_t) (c - 0xb7);
      size_t len   = 0;
      if (!read_long_len(src, src_idx, n_len, &len)) return RLP_OUT_OF_RANGE;
      if (match_index)
        return check_range(target, src, len, src->data + src_idx + n_len + 1, RLP_ITEM);
      src_idx += len + n_len;
    }
    else if (c < 0xf8) {
      size_t len = (size_t) (c - 0xc0);
      if (!payload_fits(src->len, src_idx, 0, len)) return RLP_OUT_OF_RANGE;
      if (match_index)
        return check_range(target, src, len, src->data + src_idx + 1, RLP_LIST);
      src_idx += len;
    }
    else {
      size_t n_len = (size_t) (c - 0xf7);
      size_t len   = 0;
      if (!read_long_len(src, src_idx, n_len, &len)) return RLP_OUT_OF_RANGE;
      if (match_index)
        return check_range(target, src, len, src->data + src_idx + n_len + 1, RLP_LIST);
      src_idx += len + n_len;
    }
  }

  if (index < 0) return src_idx == src->len ? (rlp_type_t) pos : RLP_OUT_OF_RANGE;

  return (src_idx > src->len) ? RLP_OUT_OF_RANGE : RLP_NOT_FOUND;
}

static void encode_length(buffer_t* buf, uint32_t len, uint8_t offset) {
  uint8_t val = offset;
  if (len < 56)
    buffer_add_bytes(buf, 1, offset + len);
  else if (len < 0x100)
    buffer_add_bytes(buf, 2, offset + 55 + 1, len);
  else if (len < 0x10000) {
    buffer_add_bytes(buf, 1, offset + 55 + 2);
    buffer_add_be(buf, len, 2);
  }
  else if (len < 0x1000000) {
    buffer_add_bytes(buf, 1, offset + 55 + 3);
    buffer_add_be(buf, len, 3);
  }
  else {
    buffer_add_bytes(buf, 1, offset + 55 + 4);
    buffer_add_be(buf, len, 4);
  }
}

INTERNAL void rlp_add_item(buffer_t* buf, bytes_t data) {
  if (data.len == 1 && data.data[0] < 0x80) {
  }
  else if (data.len < 56)
    buffer_add_bytes(buf, 1, data.len + 0x80);
  else
    encode_length(buf, data.len, 0x80);
  buffer_append(buf, data);
}

INTERNAL void rlp_add_list(buffer_t* buf, bytes_t data) {
  encode_length(buf, data.len, 0xc0);
  buffer_append(buf, data);
}

INTERNAL void rlp_add_uint(buffer_t* buf, bytes_t data) {
  while (data.len && data.data[0] == 0) {
    data.data++;
    data.len--;
  }
  rlp_add_item(buf, data);
}

INTERNAL void rlp_add_uint64(buffer_t* buf, uint64_t value) {
  uint8_t data[8] = {0};
  uint64_to_be(data, value);
  rlp_add_uint(buf, bytes(data, 8));
}

INTERNAL void rlp_to_list(buffer_t* buf) {
  uint8_t  tmp[4] = {0};
  buffer_t tbuf   = stack_buffer(tmp);
  encode_length(&tbuf, buf->data.len, 0xc0);
  buffer_splice(buf, 0, 0, tbuf.data);
}

INTERNAL uint64_t rlp_get_uint64(bytes_t data, int index) {
  uint64_t value = 0;
  if (rlp_decode(&data, index, &data) != RLP_ITEM) return 0;
  if (data.len > 8 || !data.len) return 0;
  for (int i = 0; i < (int) data.len; i++)
    value |= ((uint64_t) data.data[i]) << ((data.len - i - 1) << 3);
  return value;
}