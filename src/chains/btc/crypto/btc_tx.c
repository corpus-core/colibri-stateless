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

#include "btc_tx.h"
#include "btc_hash.h"
#include <string.h>

/** Read cursor over an untrusted byte string. `p` stays inside `[start, end]`. */
typedef struct {
  const uint8_t* p;
  const uint8_t* end;
} btc_cursor_t;

/**
 * Bytes still available to the cursor.
 *
 * @param cursor read cursor
 * @return remaining byte count, or 0 when the cursor is empty or inverted
 */
static size_t btc_left(const btc_cursor_t* cursor) {
  if (!cursor->p || !cursor->end || cursor->p > cursor->end) return 0;
  return (size_t) (cursor->end - cursor->p);
}

/**
 * Reads one byte.
 *
 * @param cursor read cursor
 * @param out receives the byte
 * @return false when no byte remains
 */
static bool btc_read_u8(btc_cursor_t* cursor, uint8_t* out) {
  if (btc_left(cursor) < 1) return false;
  *out = *cursor->p++;
  return true;
}

/**
 * Reads a little-endian uint32. The caller has already checked that four bytes remain.
 *
 * @param p four bytes
 * @return the integer
 */
static uint32_t btc_load_le32(const uint8_t p[4]) {
  return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

/**
 * Reads a little-endian uint64. The caller has already checked that eight bytes remain.
 *
 * @param p eight bytes
 * @return the integer
 */
static uint64_t btc_load_le64(const uint8_t p[8]) {
  return (uint64_t) btc_load_le32(p) | ((uint64_t) btc_load_le32(p + 4) << 32);
}

/**
 * Reads a Bitcoin compact size and rejects a non-canonical encoding.
 *
 * @param cursor read cursor
 * @param out receives the value
 * @return false on truncation or a value that would have fit in a shorter form
 */
static bool btc_read_compact(btc_cursor_t* cursor, uint64_t* out) {
  uint8_t  tag;
  uint64_t value;

  if (!btc_read_u8(cursor, &tag)) return false;
  if (tag < 253) {
    *out = tag;
    return true;
  }
  if (tag == 253) {
    if (btc_left(cursor) < 2) return false;
    value = (uint64_t) cursor->p[0] | ((uint64_t) cursor->p[1] << 8);
    cursor->p += 2;
    if (value < 253) return false;
  }
  else if (tag == 254) {
    if (btc_left(cursor) < 4) return false;
    value = btc_load_le32(cursor->p);
    cursor->p += 4;
    if (value < 0x10000ull) return false;
  }
  else {
    if (btc_left(cursor) < 8) return false;
    value = btc_load_le64(cursor->p);
    cursor->p += 8;
    if (value < 0x100000000ull) return false;
  }
  *out = value;
  return true;
}

/**
 * Reads a compact-size byte string.
 *
 * @param cursor read cursor
 * @param out receives the slice; may be NULL when the caller only wants to skip
 * @return false when the length is truncated or extends past the cursor
 */
static bool btc_read_bytes(btc_cursor_t* cursor, bytes_t* out) {
  uint64_t len;
  if (!btc_read_compact(cursor, &len)) return false;
  if (len > btc_left(cursor)) return false;
  if (out) *out = bytes((uint8_t*) cursor->p, (uint32_t) len);
  cursor->p += (size_t) len;
  return true;
}

/**
 * Skips one transaction input.
 *
 * An input is 32 bytes of txid, a 4-byte index, a script, and a 4-byte sequence.
 *
 * @param cursor read cursor
 * @return false when the input is truncated
 */
static bool btc_skip_input(btc_cursor_t* cursor) {
  if (btc_left(cursor) < 36) return false;
  cursor->p += 36;
  if (!btc_read_bytes(cursor, NULL)) return false;
  if (btc_left(cursor) < 4) return false;
  cursor->p += 4;
  return true;
}

/**
 * Skips one transaction output.
 *
 * An output is an 8-byte value followed by a script.
 *
 * @param cursor read cursor
 * @return false when the output is truncated
 */
static bool btc_skip_output(btc_cursor_t* cursor) {
  if (btc_left(cursor) < 8) return false;
  cursor->p += 8;
  return btc_read_bytes(cursor, NULL);
}

/**
 * Returns whether a script-number payload is minimally encoded.
 *
 * @param data script-number bytes, most significant byte last
 * @param len byte count, already checked to be in range
 * @return true when Bitcoin would accept the encoding
 */
static bool btc_script_num_minimal(const uint8_t* data, uint32_t len) {
  if (len == 0 || len > 5) return false;
  if ((data[len - 1] & 0x7f) == 0) {
    if (len == 1 || (data[len - 2] & 0x80) == 0) return false;
  }
  return true;
}

/**
 * Opens a cursor on a slice.
 *
 * @param cursor receives the cursor
 * @param data slice to read
 * @return false when `data` claims bytes it does not point at
 */
static bool btc_cursor_open(btc_cursor_t* cursor, bytes_t data) {
  if (data.len && !data.data) return false;
  cursor->p   = data.data;
  cursor->end = data.data + data.len;
  return true;
}

bool btc_tx_parse(bytes_t raw, btc_tx_t* tx) {
  btc_cursor_t   cursor;
  btc_tx_t       parsed;
  const uint8_t* vin_at;
  const uint8_t* vout_at;
  uint64_t       count;
  uint32_t       i;
  bool           any_witness = false;

  if (!tx) return false;
  memset(tx, 0, sizeof(*tx));
  if (!raw.data || raw.len < 4) return false;

  memset(&parsed, 0, sizeof(parsed));
  parsed.raw = raw;
  cursor.p   = raw.data;
  cursor.end = raw.data + raw.len;

  parsed.version = btc_load_le32(cursor.p);
  cursor.p += 4;

  vin_at = cursor.p;
  if (!btc_read_compact(&cursor, &count)) return false;
  // A leading zero input count is the segwit marker when another byte follows. Bitcoin then reads
  // the flag and, only if it is non-zero, the real inputs. Flag bits other than the witness bit are
  // not consensus-valid, so the flag must be exactly 1.
  if (count == 0 && btc_left(&cursor) > 0) {
    uint8_t flags = 0;
    if (!btc_read_u8(&cursor, &flags) || flags != 1) return false;
    parsed.segwit = true;
    vin_at        = cursor.p;
    if (!btc_read_compact(&cursor, &count)) return false;
  }
  if (count == 0 || count > btc_left(&cursor) / 41u) return false;
  parsed.input_count = (uint32_t) count;
  for (i = 0; i < parsed.input_count; i++) {
    if (!btc_skip_input(&cursor)) return false;
  }
  parsed.vin = bytes((uint8_t*) vin_at, (uint32_t) (cursor.p - vin_at));

  vout_at = cursor.p;
  if (!btc_read_compact(&cursor, &count)) return false;
  if (count == 0 || count > btc_left(&cursor) / 9u) return false;
  parsed.output_count = (uint32_t) count;
  for (i = 0; i < parsed.output_count; i++) {
    if (!btc_skip_output(&cursor)) return false;
  }
  parsed.vout = bytes((uint8_t*) vout_at, (uint32_t) (cursor.p - vout_at));

  if (parsed.segwit) {
    const uint8_t* witness_at;
    if (btc_left(&cursor) < 4) return false;
    cursor.end -= 4;
    witness_at = cursor.p;
    for (i = 0; i < parsed.input_count; i++) {
      uint64_t items;
      uint64_t item;
      if (!btc_read_compact(&cursor, &items)) return false;
      if (items > btc_left(&cursor)) return false;
      if (items > 0) any_witness = true;
      for (item = 0; item < items; item++) {
        if (!btc_read_bytes(&cursor, NULL)) return false;
      }
    }
    if (!any_witness || cursor.p != cursor.end) return false;
    parsed.witness = bytes((uint8_t*) witness_at, (uint32_t) (cursor.p - witness_at));
    cursor.end += 4;
  }

  if (btc_left(&cursor) != 4) return false;
  parsed.locktime = btc_load_le32(cursor.p);
  *tx             = parsed;
  return true;
}

bool btc_tx_id(const btc_tx_t* tx, uint8_t out[32]) {
  bytes_t parts[4];
  if (!tx || !out || !tx->raw.data || tx->raw.len < 8) return false;
  parts[0] = bytes(tx->raw.data, 4);
  parts[1] = tx->vin;
  parts[2] = tx->vout;
  parts[3] = bytes(tx->raw.data + tx->raw.len - 4, 4);
  return btc_hash256_parts(parts, 4, out);
}

bool btc_tx_wtxid(const btc_tx_t* tx, uint8_t out[32]) {
  if (!tx || !out) return false;
  if (!tx->segwit) return btc_tx_id(tx, out);
  if (!tx->raw.data || tx->raw.len < 8) return false;
  btc_hash256(tx->raw, out);
  return true;
}

/**
 * Positions a cursor on input `index` and reads it.
 *
 * @param tx parsed transaction
 * @param index input index
 * @param prev_txid receives the previous txid; may be NULL
 * @param prev_index receives the previous output index; may be NULL
 * @param script receives the scriptSig; may be NULL
 * @param sequence receives the sequence; may be NULL
 * @return false when the input cannot be decoded
 */
static bool btc_input_at(const btc_tx_t* tx, uint32_t index, bytes_t* prev_txid, uint32_t* prev_index, bytes_t* script, uint32_t* sequence) {
  btc_cursor_t cursor;
  uint64_t     count;
  uint32_t     i;

  if (!tx || index >= tx->input_count) return false;
  if (!btc_cursor_open(&cursor, tx->vin)) return false;
  if (!btc_read_compact(&cursor, &count) || count != tx->input_count) return false;
  for (i = 0; i <= index; i++) {
    const uint8_t* prev;
    uint32_t       prev_out;
    bytes_t        script_sig;
    uint32_t       seq;
    if (btc_left(&cursor) < 36) return false;
    prev = cursor.p;
    cursor.p += 32;
    prev_out = btc_load_le32(cursor.p);
    cursor.p += 4;
    if (!btc_read_bytes(&cursor, &script_sig) || btc_left(&cursor) < 4) return false;
    seq = btc_load_le32(cursor.p);
    cursor.p += 4;
    if (i == index) {
      if (prev_txid) *prev_txid = bytes((uint8_t*) prev, 32);
      if (prev_index) *prev_index = prev_out;
      if (script) *script = script_sig;
      if (sequence) *sequence = seq;
      return true;
    }
  }
  return false;
}

bool btc_tx_input(const btc_tx_t* tx, uint32_t index, bytes_t* prev_txid, uint32_t* prev_index, bytes_t* script, uint32_t* sequence) {
  return btc_input_at(tx, index, prev_txid, prev_index, script, sequence);
}

bool btc_tx_output(const btc_tx_t* tx, uint32_t index, uint64_t* value, bytes_t* script) {
  btc_cursor_t cursor;
  uint64_t     count;
  uint32_t     i;

  if (!tx || index >= tx->output_count) return false;
  if (!btc_cursor_open(&cursor, tx->vout)) return false;
  if (!btc_read_compact(&cursor, &count) || count != tx->output_count) return false;
  for (i = 0; i <= index; i++) {
    uint64_t amount;
    bytes_t  script_pub;
    if (btc_left(&cursor) < 8) return false;
    amount = btc_load_le64(cursor.p);
    cursor.p += 8;
    if (!btc_read_bytes(&cursor, &script_pub)) return false;
    if (i == index) {
      if (value) *value = amount;
      if (script) *script = script_pub;
      return true;
    }
  }
  return false;
}

bool btc_tx_witness_item(const btc_tx_t* tx, uint32_t input, uint32_t item_index, bytes_t* out) {
  btc_cursor_t cursor;
  uint32_t     i;

  if (!tx || !out || !tx->segwit || input >= tx->input_count) return false;
  if (!btc_cursor_open(&cursor, tx->witness)) return false;
  for (i = 0; i <= input; i++) {
    uint64_t items;
    uint64_t item;
    if (!btc_read_compact(&cursor, &items) || items > btc_left(&cursor)) return false;
    if (i == input) {
      if ((uint64_t) item_index >= items) return false;
      for (item = 0; item <= item_index; item++) {
        bytes_t got;
        if (!btc_read_bytes(&cursor, &got)) return false;
        if (item == item_index) {
          *out = got;
          return true;
        }
      }
    }
    else {
      for (item = 0; item < items; item++) {
        if (!btc_read_bytes(&cursor, NULL)) return false;
      }
    }
  }
  return false;
}

bool btc_tx_coinbase_nonce(const btc_tx_t* tx, bytes_t* nonce) {
  btc_cursor_t cursor;
  uint64_t     items;
  bytes_t      item;

  if (!tx || !nonce || !tx->segwit || tx->input_count == 0) return false;
  if (!btc_cursor_open(&cursor, tx->witness)) return false;
  if (!btc_read_compact(&cursor, &items) || items != 1) return false;
  if (!btc_read_bytes(&cursor, &item) || item.len != 32) return false;
  *nonce = item;
  return true;
}

bool btc_bip34_height(bytes_t script, uint32_t* height) {
  btc_cursor_t   cursor;
  uint8_t        opcode;
  const uint8_t* number     = NULL;
  uint32_t       number_len = 0;
  uint64_t       value      = 0;
  uint32_t       i;

  if (!height || !btc_cursor_open(&cursor, script) || !btc_read_u8(&cursor, &opcode)) return false;
  if (opcode == 0x00) {
    *height = 0;
    return true;
  }
  if (opcode >= 0x51 && opcode <= 0x60) {
    *height = (uint32_t) opcode - 0x50u;
    return true;
  }
  if (opcode >= 1 && opcode <= 75) {
    number_len = opcode;
    if (btc_left(&cursor) < number_len) return false;
    number = cursor.p;
  }
  else if (opcode == 0x4c || opcode == 0x4d || opcode == 0x4e) {
    uint64_t len;
    uint32_t prefix = opcode == 0x4c ? 1u : opcode == 0x4d ? 2u
                                                           : 4u;
    if (btc_left(&cursor) < prefix) return false;
    len = prefix == 1 ? cursor.p[0] : prefix == 2 ? ((uint64_t) cursor.p[0] | ((uint64_t) cursor.p[1] << 8))
                                                  : btc_load_le32(cursor.p);
    cursor.p += prefix;
    // A direct push covers 1..75, PUSHDATA1 covers 76..255, PUSHDATA2 covers 256..65535.
    if (opcode == 0x4c && len < 76) return false;
    if (opcode == 0x4d && len < 256) return false;
    if (opcode == 0x4e && len < 65536) return false;
    if (len > btc_left(&cursor) || len > 5) return false;
    number_len = (uint32_t) len;
    number     = cursor.p;
  }
  else {
    return false;
  }

  if (!btc_script_num_minimal(number, number_len)) return false;
  if ((number[number_len - 1] & 0x80) != 0) return false;
  for (i = 0; i < number_len && i < 4; i++) value |= (uint64_t) number[i] << (8u * i);
  if (number_len == 5 && number[4] != 0) return false;
  if (value > 0xffffffffull) return false;
  *height = (uint32_t) value;
  return true;
}

bool btc_tx_witness_commitment(const btc_tx_t* tx, bytes_t* commitment) {
  static const uint8_t magic[4] = {0xaa, 0x21, 0xa9, 0xed};
  btc_cursor_t         cursor;
  uint64_t             count;
  uint32_t             i;
  bool                 found = false;

  if (!tx || !commitment) return false;
  if (!btc_cursor_open(&cursor, tx->vout)) return false;
  if (!btc_read_compact(&cursor, &count) || count != tx->output_count) return false;
  for (i = 0; i < tx->output_count; i++) {
    bytes_t script;
    if (btc_left(&cursor) < 8) return false;
    cursor.p += 8;
    if (!btc_read_bytes(&cursor, &script)) return false;
    if (script.len >= 38 && script.data && script.data[0] == 0x6a && script.data[1] == 0x24 && memcmp(script.data + 2, magic, 4) == 0) {
      *commitment = bytes(script.data + 6, 32);
      found       = true;
    }
  }
  return found;
}

bool btc_witness_commitment_matches(bytes_t commitment, const uint8_t witness_root[32], const uint8_t nonce[32]) {
  uint8_t expected[32];
  if (!witness_root || !nonce || commitment.len != 32 || !commitment.data) return false;
  btc_hash256_pair(witness_root, nonce, expected);
  return memcmp(expected, commitment.data, 32) == 0;
}
