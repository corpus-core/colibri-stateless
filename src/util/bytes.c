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

#include "./bytes.h"
#include "./compat.h" /* Include our compatibility header for PRI* macros */
#include "./json.h"
#include "ssz.h"
#include <assert.h>
#include <ctype.h>
#include <errno.h> // For errno
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// --- Macros and Constants ---

// Macro for iterating over bytes in a bytes_t structure
#define FOREACH_BYTE(bytes_data, index_var) \
  for (uint32_t index_var = 0; index_var < (bytes_data).len; index_var++)

// Macro for consistent memory allocation error messages
#define SAFE_ALLOC_ERROR_EXIT(func_name, size_info, error_msg)                      \
  do {                                                                              \
    fbprintf(stderr, "Error: Memory allocation failed (%s) for %s: %s. Exiting.\n", \
             func_name, size_info, error_msg);                                      \
    exit(EXIT_FAILURE);                                                             \
  } while (0)

// Constants for magic numbers
#define BUFFER_GROWTH_FACTOR_NUM 3
#define BUFFER_GROWTH_FACTOR_DEN 2
#define FILE_READ_CHUNK_SIZE     1024
#define HEX_CHAR_BUFFER_SIZE     4
#define SIZE_INFO_BUFFER_SIZE    64

static const char hex_digits[] = "0123456789abcdef";

// --- Safe Memory Allocation Wrappers ---

void* safe_malloc(size_t size) {
  void* ptr = malloc(size);
  if (size > 0 && ptr == NULL) {
    char size_info[SIZE_INFO_BUFFER_SIZE];
    sbprintf(size_info, "size %l bytes", (uint64_t) size);
    SAFE_ALLOC_ERROR_EXIT("malloc", size_info, strerror(errno));
  }
  return ptr;
}

void* safe_calloc(size_t num, size_t size) {
  void* ptr = calloc(num, size);
  if (num > 0 && size > 0 && ptr == NULL) {
    char size_info[SIZE_INFO_BUFFER_SIZE];
    sbprintf(size_info, "%l items of size %l bytes", (uint64_t) num, (uint64_t) size);
    SAFE_ALLOC_ERROR_EXIT("calloc", size_info, strerror(errno));
  }
  return ptr;
}

void* safe_realloc(void* ptr, size_t new_size) {
  void* new_ptr = realloc(ptr, new_size);
  // Note: safe_realloc(NULL, size) is equivalent to safe_malloc(size)
  // safe_realloc(ptr, 0) is equivalent to safe_free(ptr) and may return NULL
  if (new_size > 0 && new_ptr == NULL) {
    char size_info[SIZE_INFO_BUFFER_SIZE];
    sbprintf(size_info, "new size %l bytes", (uint64_t) new_size);
    // Important: The original block ptr is NOT freed by realloc if it fails
    SAFE_ALLOC_ERROR_EXIT("realloc", size_info, strerror(errno));
  }
  return new_ptr;
}

// safe_free is now implemented as a macro for better performance (see bytes.h)

// --- End Safe Memory Allocation Wrappers ---

uint32_t uint32_from_le(uint8_t* data) {
  // Alignment check optimization for 32-bit ARM architectures (e.g., ARMv7-a)
  // where unaligned loads are significantly more expensive than aligned ones.
  // On modern architectures (x86-64, ARM64), unaligned loads are fast enough
  // that the check overhead isn't worth it.
#if defined(__arm__) && !defined(__aarch64__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  if (((uintptr_t) data & 3) == 0) {
    return *(uint32_t*) data;
  }
#endif
  return (uint32_t) data[0] |
         ((uint32_t) data[1] << 8) |
         ((uint32_t) data[2] << 16) |
         ((uint32_t) data[3] << 24);
}

uint16_t uint16_from_le(uint8_t* data) {
  return (uint16_t) data[0] |
         ((uint16_t) data[1] << 8);
}

uint64_t uint64_from_le(uint8_t* data) {
  // Alignment check optimization for 32-bit ARM architectures (e.g., ARMv7-a)
  // where aligned loads use ldrd (2 instructions) vs 8 individual ldrb
  // instructions for unaligned data - a massive performance difference.
  // On modern architectures (x86-64, ARM64), unaligned loads are fast enough
  // that the check overhead isn't worth it.
#if defined(__arm__) && !defined(__aarch64__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  if (((uintptr_t) data & 7) == 0) {
    return *(uint64_t*) data;
  }
#endif
  return (uint64_t) data[0] |
         ((uint64_t) data[1] << 8) |
         ((uint64_t) data[2] << 16) |
         ((uint64_t) data[3] << 24) |
         ((uint64_t) data[4] << 32) |
         ((uint64_t) data[5] << 40) |
         ((uint64_t) data[6] << 48) |
         ((uint64_t) data[7] << 56);
}

uint64_t uint64_from_be(uint8_t* data) {
  return (uint64_t) data[7] |
         ((uint64_t) data[6] << 8) |
         ((uint64_t) data[5] << 16) |
         ((uint64_t) data[4] << 24) |
         ((uint64_t) data[3] << 32) |
         ((uint64_t) data[2] << 40) |
         ((uint64_t) data[1] << 48) |
         ((uint64_t) data[0] << 56);
}
void uint64_to_be(uint8_t* data, uint64_t value) {
  data[0] = (value >> 56) & 0xFF;
  data[1] = (value >> 48) & 0xFF;
  data[2] = (value >> 40) & 0xFF;
  data[3] = (value >> 32) & 0xFF;
  data[4] = (value >> 24) & 0xFF;
  data[5] = (value >> 16) & 0xFF;
  data[6] = (value >> 8) & 0xFF;
  data[7] = value & 0xFF;
}

void uint64_to_le(uint8_t* data, uint64_t value) {
  // Alignment check optimization for 32-bit ARM architectures where
  // aligned stores (strd) are much faster than 8 individual byte stores.
#if defined(__arm__) && !defined(__aarch64__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  if (((uintptr_t) data & 7) == 0) {
    *(uint64_t*) data = value;
    return;
  }
#endif
  data[0] = value & 0xFF;
  data[1] = (value >> 8) & 0xFF;
  data[2] = (value >> 16) & 0xFF;
  data[3] = (value >> 24) & 0xFF;
  data[4] = (value >> 32) & 0xFF;
  data[5] = (value >> 40) & 0xFF;
  data[6] = (value >> 48) & 0xFF;
  data[7] = (value >> 56) & 0xFF;
}

void uint32_to_le(uint8_t* data, uint32_t value) {
  // Alignment check optimization for 32-bit ARM architectures where
  // aligned stores (str) are much faster than 4 individual byte stores.
#if defined(__arm__) && !defined(__aarch64__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  if (((uintptr_t) data & 3) == 0) {
    *(uint32_t*) data = value;
    return;
  }
#endif
  data[0] = value & 0xFF;
  data[1] = (value >> 8) & 0xFF;
  data[2] = (value >> 16) & 0xFF;
  data[3] = (value >> 24) & 0xFF;
}

size_t buffer_grow(buffer_t* buffer, size_t min_len) {
  // Initial allocation: use either min_len or pre-allocated size, whichever is larger
  if (buffer->data.data == NULL) {
    if (buffer->allocated > 0 && (size_t) buffer->allocated > min_len) min_len = (size_t) buffer->allocated;
    buffer->data.data = safe_malloc(min_len);
    buffer->allocated = (int32_t) min_len;
  }
  // Growth strategy: multiply by 3/2 until we reach required size (amortized O(1) append)
  else if (buffer->allocated >= 0 && (size_t) buffer->allocated < min_len) {
    size_t new_len = (size_t) buffer->allocated;
    while (new_len < min_len) new_len = (new_len + 1) * BUFFER_GROWTH_FACTOR_NUM / BUFFER_GROWTH_FACTOR_DEN;
    buffer->data.data = safe_realloc(buffer->data.data, new_len);
    buffer->allocated = (int32_t) new_len;
  }
  // Note: negative allocated means fixed-size buffer (no growth allowed)
  return buffer->allocated > 0 ? (size_t) buffer->allocated : (size_t) (0 - buffer->allocated);
}

uint32_t buffer_append(buffer_t* buffer, bytes_t data) {
  if (buffer->allocated < 0 && buffer->data.len + data.len > (uint32_t) (0 - buffer->allocated))
    data.len = ((uint32_t) (0 - buffer->allocated)) - buffer->data.len;
  if (!data.len) return 0;
  buffer_grow(buffer, buffer->data.len + data.len);
  if (data.data)
    memcpy(buffer->data.data + buffer->data.len, data.data, data.len);
  else
    memset(buffer->data.data + buffer->data.len, 0, data.len);
  buffer->data.len += data.len;
  return data.len;
}

void buffer_splice(buffer_t* buffer, size_t offset, uint32_t len, bytes_t data) {
  if (data.len > len) { // if the buffer grows, check the limits
    size_t max_len = buffer_grow(buffer, buffer->data.len + data.len - len);
    if (buffer->data.len + data.len - len > max_len) return;
  }
  uint32_t old_end_offset = offset + len;
  uint32_t new_end_offset = offset + data.len;

  // Move existing data if splice position changes
  if (new_end_offset != old_end_offset && ((uint32_t) buffer->data.len) - old_end_offset > 0)
    memmove(buffer->data.data + new_end_offset, buffer->data.data + old_end_offset, ((uint32_t) buffer->data.len) - old_end_offset);

  if (data.len) {
    if (data.data)
      memcpy(buffer->data.data + offset, data.data, data.len);
    else
      memset(buffer->data.data + offset, 0, data.len);
  }

  buffer->data.len = buffer->data.len + data.len - len;
}

void buffer_free(buffer_t* buffer) {
  if (buffer->data.data && buffer->allocated > 0)
    safe_free(buffer->data.data);
  buffer->data.data = NULL;
  buffer->allocated = 0;
  buffer->data.len  = 0;
}

void print_hex(FILE* f, bytes_t data, char* prefix, char* suffix) {
  if (prefix) fprintf(f, "%s", prefix);
  FOREACH_BYTE(data, i) {
    fprintf(f, "%02x", data.data[i]);
  }
  if (suffix) fprintf(f, "%s", suffix);
}

bool bytes_all_equal(bytes_t a, uint8_t value) {
  FOREACH_BYTE(a, i) {
    if (a.data[i] != value) return false;
  }
  return true;
}

static inline int hexchar_to_int(char c) {
  if (isdigit(c)) return c - '0';
  if (isxdigit(c)) return tolower(c) - 'a' + 10;
  return -1; // Invalid character
}

int hex_to_bytes(const char* hexstring, int len, bytes_t buffer) {
  size_t hex_len = len == -1 ? strlen(hexstring) : (size_t) len;
  if (!hexstring || !buffer.data) return -1;

  // Handle odd-length hex strings by padding with leading zero
  int dst_offset = hex_len % 2;
  // Skip optional "0x" prefix
  int src_offset = (hexstring[0] == '0' && hexstring[1] == 'x') ? 2 : 0;

  // Handle leading nibble for odd-length strings
  if (dst_offset) buffer.data[0] = hexchar_to_int(hexstring[src_offset++]);

  // Validate buffer has enough space for the conversion
  if ((hex_len - src_offset) % 2 || (buffer.len - dst_offset) < (hex_len - src_offset) / 2)
    return -1;

  // Convert pairs of hex characters to bytes
  for (size_t i = src_offset; i < hex_len; i += 2) {
    int high_nibble = hexchar_to_int(hexstring[i]);
    int low_nibble  = hexchar_to_int(hexstring[i + 1]);
    if (high_nibble == -1 || low_nibble == -1) return -1;
    buffer.data[dst_offset++] = (high_nibble << 4) | low_nibble;
  }

  return dst_offset;
}

void buffer_add_chars(buffer_t* buffer, const char* data) {
  if (!data) return;
  size_t len = strlen(data);
  buffer_append(buffer, bytes((uint8_t*) data, len + 1));
  buffer->data.len -= 1;
}

static inline void buffer_add_chars_escaped(buffer_t* buffer, const char* data) {
  if (!data) return;
  const uint8_t* str = (const uint8_t*) data;
  int            len = strlen(data);

  // Calculate additional bytes needed for escape sequences
  int escaped_len = 0;
  for (int i = 0; i < len; i++) {
    uint8_t c = str[i];

    switch (c) {
      // Special JSON characters that need escaping (add 1 byte for backslash)
      case '"':
      case '\\':
      case '\b':
      case '\f':
      case '\n':
      case '\r':
      case '\t':
        escaped_len += 1; // Add 1 byte for the backslash
        break;

      default:
        if (c < 0x20) {
          // Other control characters: escape as \uXXXX (add 5 bytes: \uXXXX)
          escaped_len += 5;
        }
        // else: no additional bytes needed
        break;
    }
  }

  buffer_grow(buffer, buffer->data.len + len + escaped_len + 1);

  // Fast path: if no escaping needed, use buffer_append
  if (escaped_len == 0) {
    buffer_append(buffer, bytes(str, len));
    buffer->data.data[buffer->data.len] = 0;
    return;
  }

  // Perform the actual escaping
  for (int i = 0; i < len; i++) {
    uint8_t c = str[i];

    switch (c) {
      case '"':
        buffer->data.data[buffer->data.len++] = '\\';
        buffer->data.data[buffer->data.len++] = '"';
        break;
      case '\\':
        buffer->data.data[buffer->data.len++] = '\\';
        buffer->data.data[buffer->data.len++] = '\\';
        break;
      case '\b':
        buffer->data.data[buffer->data.len++] = '\\';
        buffer->data.data[buffer->data.len++] = 'b';
        break;
      case '\f':
        buffer->data.data[buffer->data.len++] = '\\';
        buffer->data.data[buffer->data.len++] = 'f';
        break;
      case '\n':
        buffer->data.data[buffer->data.len++] = '\\';
        buffer->data.data[buffer->data.len++] = 'n';
        break;
      case '\r':
        buffer->data.data[buffer->data.len++] = '\\';
        buffer->data.data[buffer->data.len++] = 'r';
        break;
      case '\t':
        buffer->data.data[buffer->data.len++] = '\\';
        buffer->data.data[buffer->data.len++] = 't';
        break;

      default:
        if (c < 0x20) {
          // Other control characters: escape as \uXXXX
          buffer->data.data[buffer->data.len++] = '\\';
          buffer->data.data[buffer->data.len++] = 'u';
          buffer->data.data[buffer->data.len++] = '0';
          buffer->data.data[buffer->data.len++] = '0';
          buffer->data.data[buffer->data.len++] = hex_digits[(c >> 4) & 0x0F];
          buffer->data.data[buffer->data.len++] = hex_digits[c & 0x0F];
        }
        else {
          // Regular character (including UTF-8 multi-byte sequences)
          buffer->data.data[buffer->data.len++] = c;
        }
        break;
    }
  }

  buffer->data.data[buffer->data.len] = 0;
}

static inline void buffer_add_hex_chars(buffer_t* buffer, bytes_t data) {
  uint32_t total_len = data.len;
  size_t   max_len   = buffer_grow(buffer, buffer->data.len + total_len * 2 + 1) - buffer->data.len;
  if (total_len * 2 > max_len) total_len = max_len / 2;
  if (!total_len) return;
  char* dst = (char*) (buffer->data.data + buffer->data.len);

  for (size_t i = 0, j = 0; i < total_len; i++) {
    dst[j++] = hex_digits[(data.data[i] >> 4) & 0x0F];
    dst[j++] = hex_digits[data.data[i] & 0x0F];
  }
  buffer->data.len += total_len * 2;
}

bytes_t bytes_dup(bytes_t data) {
  bytes_t result = {.data = safe_malloc(data.len), .len = data.len};
  if (!result.data && data.len > 0) {
    // This case should not be reached since safe_malloc exits on failure,
    // but kept for completeness in case safe_malloc behavior changes.
    return NULL_BYTES; // Indicate failure if malloc fails
  }
  memcpy(result.data, data.data, data.len);
  return result;
}

void bytes_write(bytes_t data, FILE* f, bool close) {
  if (!f) return;
  fwrite(data.data, 1, data.len, f);
  if (close && f != stdout && f != stderr) fclose(f);
}

bytes_t bytes_read(char* filename) {
  unsigned char read_buffer[FILE_READ_CHUNK_SIZE];
  size_t        bytes_read_count;
  buffer_t      data = {0};

  FILE* file = strcmp(filename, "-") ? fopen(filename, "rb") : stdin;
  if (file == NULL) return NULL_BYTES;

  while ((bytes_read_count = fread(read_buffer, 1, FILE_READ_CHUNK_SIZE, file)) == sizeof(read_buffer))
    buffer_append(&data, bytes(read_buffer, bytes_read_count));
  if (bytes_read_count > 0) buffer_append(&data, bytes(read_buffer, bytes_read_count));
  buffer_append(&data, bytes(NULL, 1));
  data.data.len--;

  if (ferror(file)) {
    fprintf(stderr, "Error reading file: %s\n", filename);
    buffer_free(&data);
    data.data = NULL_BYTES;
  }

#ifndef __clang_analyzer__
  if (file != stdin)
#endif
    fclose(file);
  return data.data;
}

void buffer_add_bytes(buffer_t* buf, uint32_t len, ...) {
  buffer_grow(buf, buf->data.len + len);
  va_list args;
  va_start(args, len);
  for (uint32_t i = 0; i < len; i++) {
    buf->data.data[buf->data.len] = (uint8_t) va_arg(args, int);
    buf->data.len++;
  }
  va_end(args);
}

void buffer_add_be(buffer_t* buffer, uint64_t value, uint32_t len) {
  buffer_grow(buffer, buffer->data.len + len);
  uint32_t s = buffer->data.len;
  for (uint32_t i = 0; i < len; i++)
    buffer->data.data[s + len - i - 1] = (value >> (i << 3)) & 0xFF;
  buffer->data.len += len;
}
void buffer_add_le(buffer_t* buffer, uint64_t value, uint32_t len) {
  buffer_grow(buffer, buffer->data.len + len);
  uint32_t s = buffer->data.len;
  for (uint32_t i = 0; i < len; i++)
    buffer->data.data[s + i] = (value >> (i << 3)) & 0xFF;
  buffer->data.len += len;
}

static inline void append_u64_to_dec(buffer_t* out, uint64_t v) {
  if (!out) return;
  if (v == 0) {
    buffer_add_chars(out, "0");
    return;
  }
  char   tmp[20];
  size_t i = 0;
  while (v) {
    tmp[i++] = (char) ('0' + (v % 10));
    v /= 10;
  }

  size_t max_len = buffer_grow(out, out->data.len + i + 1) - out->data.len;
  if (i > max_len) i = max_len;
  if (!i) return;
  char* dst = (char*) (out->data.data + out->data.len);
  /* reverse */
  for (size_t j = 0; j < i; ++j) dst[j] = tmp[i - 1 - j];
  out->data.len += i;
}

static inline void append_u64_to_hex(buffer_t* out, uint64_t v) {
  if (!out) return;
  if (v == 0) {
    buffer_add_chars(out, "0");
    return;
  }
  char   tmp[16];
  size_t i = 0;
  while (v) {
    uint8_t digit = v & 0xF;
    tmp[i++]      = (char) (digit < 10 ? ('0' + digit) : ('a' + digit - 10));
    v >>= 4;
  }

  size_t max_len = buffer_grow(out, out->data.len + i + 1) - out->data.len;
  if (i > max_len) i = max_len;
  if (!i) return;
  char* dst = (char*) (out->data.data + out->data.len);
  /* reverse */
  for (size_t j = 0; j < i; ++j) dst[j] = tmp[i - 1 - j];
  out->data.len += i;
}

/**
 * Appends a double value as decimal string to a buffer.
 *
 * @param out The output buffer
 * @param val The double value to append
 * @param precision Maximum number of decimal places (0-18 recommended)
 * @param fixed_precision If true, always show exactly 'precision' decimal places (like printf "%.Nf")
 *                        If false, show up to 'precision' decimal places, removing trailing zeros
 */
static inline void append_double_to_dec(buffer_t* out, double val, size_t precision, bool fixed_precision) {
  if (!out) return;

  /* handle special cases */
  if (isnan(val)) {
    buffer_add_chars(out, "NaN");
    return;
  }
  if (isinf(val)) {
    buffer_add_chars(out, val < 0 ? "-Infinity" : "Infinity");
    return;
  }

  /* handle negative values */
  if (val < 0) {
    buffer_add_chars(out, "-");
    val = -val;
  }

  /* round to precision */
  double rounding = 0.5;
  for (size_t i = 0; i < precision; i++) rounding /= 10.0;
  val += rounding;

  /* extract integer and fractional parts */
  uint64_t int_part  = (uint64_t) val;
  double   frac_part = val - (double) int_part;

  /* append integer part */
  append_u64_to_dec(out, int_part);

  /* append fractional part if precision > 0 */
  if (precision > 0) {
    /* reserve space for all fractional digits and check available space */
    size_t max_len      = buffer_grow(out, out->data.len + precision + 2) - out->data.len;
    size_t digits_write = precision;
    if (digits_write + 1 > max_len) digits_write = max_len > 0 ? max_len - 1 : 0;
    if (digits_write == 0) return;

    /* store starting position for potential trimming */
    size_t decimal_pos = out->data.len;

    /* add decimal point */
    out->data.data[out->data.len++] = '.';

    /* convert fractional part to digits */
    for (size_t i = 0; i < digits_write; i++) {
      frac_part *= 10.0;
      uint8_t digit                   = (uint8_t) frac_part;
      out->data.data[out->data.len++] = (uint8_t) ('0' + digit);
      frac_part -= (double) digit;
    }

    /* trim trailing zeros if not fixed precision */
    if (!fixed_precision) {
      while (out->data.len > decimal_pos + 1 && out->data.data[out->data.len - 1] == '0')
        out->data.len--;
      /* remove decimal point if no fractional part remains */
      if (out->data.len == decimal_pos + 1)
        out->data.len = decimal_pos;
    }
  }
}

char* bprintf(buffer_t* buf, const char* fmt, ...) {
  buffer_t tmp_buf = {0};
  if (buf == NULL) buf = &tmp_buf;
  va_list args;
  va_start(args, fmt);
  const char* last_pos = fmt;
  const char* p        = fmt;
  for (; *p; p++) {
    if (*p == '%') {
      if (p != last_pos) buffer_append(buf, bytes((uint8_t*) last_pos, p - last_pos));
      switch (*(p + 1)) {
        case 's': // normal string
          buffer_add_chars(buf, va_arg(args, const char*));
          break;
        case 'S': // escaped string
          buffer_add_chars_escaped(buf, va_arg(args, const char*));
          break;
        case 'x': // bytes as hex
        case 'b':
          buffer_add_hex_chars(buf, va_arg(args, bytes_t));
          break;
        case 'u': { // write bytes as hex without leading zeros
          uint32_t len = buf->data.len;
          buffer_add_hex_chars(buf, bytes_remove_leading_zeros(va_arg(args, bytes_t)));
          if (buf->data.data[len] == '0')
            buffer_splice(buf, len, 1, bytes(NULL, 0));
          break;
        }
        case 'J': { // write json string
          json_t val = va_arg(args, json_t);
          buffer_append(buf, bytes((uint8_t*) val.start, val.len));
          break;
        }
        case 'j': { // write json string, but on case of a string, the quotes are removed
          json_t val = va_arg(args, json_t);
          if (val.type == JSON_TYPE_STRING)
            buffer_append(buf, bytes((uint8_t*) val.start + 1, val.len - 2));
          else
            buffer_append(buf, bytes((uint8_t*) val.start, val.len));
          break;
        }
        case 'l': {
          uint64_t value = va_arg(args, uint64_t);
          if (*(p + 2) == 'x') {
            p++;
            if (!*(p + 1)) break;
            append_u64_to_hex(buf, value);
          }
          else
            append_u64_to_dec(buf, value);
          break;
        }
        case 'd': {
          uint32_t value = va_arg(args, uint32_t);
          if (*(p + 2) == 'x') {
            p++;
            if (!*(p + 1)) break;
            append_u64_to_hex(buf, (uint64_t) value);
          }
          else
            append_u64_to_dec(buf, (uint64_t) value);
          break;
        }
        case 'f': {
          append_double_to_dec(buf, va_arg(args, double), 6, false);
          break;
        }
        case 'c': {
          char c = va_arg(args, int);
          buffer_append(buf, bytes((uint8_t*) &c, 1));
          break;
        }
        case 'z': {
          char* s = ssz_dump_to_str(va_arg(args, ssz_ob_t), false, false);
          buffer_add_chars(buf, s);
          safe_free(s);
          break;
        }
        case 'Z': {
          char* s = ssz_dump_to_str(va_arg(args, ssz_ob_t), false, true);
          buffer_add_chars(buf, s);
          safe_free(s);
          break;
        }
        case 'r': { // raw bytes as string
          buffer_append(buf, va_arg(args, bytes_t));
          break;
        }
        case '%': { // append the next character
          buffer_append(buf, bytes((uint8_t*) p + 1, 1));
          break;
        }
      }
      p++;
      last_pos = p + 1;
      if (!*(p + 1)) break;
    }
  }
  va_end(args);
  if (last_pos != p)
    buffer_add_chars(buf, last_pos);                                // automaticly appen NULL-Terminator
  else if (buffer_grow(buf, buf->data.len + 1) - buf->data.len > 0) // can we add the NULL-Terminator?
    buf->data.data[buf->data.len] = 0;                              // then add it
  else {                                                            // so we reached a limit without space for the NULL-Terminator
    buf->data.len--;                                                // remove the last character
    buf->data.data[buf->data.len] = 0;                              // then add it
  }
  return (char*) buf->data.data;
}

bytes_t bytes_remove_leading_zeros(bytes_t data) {
  while (data.len > 1 && data.data[0] == 0) {
    data.len--;
    data.data++;
  }
  return data;
}

bool bytes_eq(bytes_t a, bytes_t b) {
  if (a.len != b.len) return false;
  FOREACH_BYTE(a, i) {
    if (a.data[i] != b.data[i]) return false;
  }
  return true;
}
uint64_t bytes_as_le(bytes_t data) {
  uint64_t result = 0;
  FOREACH_BYTE(data, i) {
    result = (result << 8) | data.data[data.len - i - 1];
  }
  return result;
}
uint64_t bytes_as_be(bytes_t data) {
  uint64_t result = 0;
  FOREACH_BYTE(data, i) {
    result = (result << 8) | data.data[i];
  }
  return result;
}
