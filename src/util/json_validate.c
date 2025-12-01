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

#include "bytes.h"
#include "json.h"
#include <ctype.h>
#include <stdint.h>
#include <string.h>

#define ERROR(fmt, ...) return bprintf(NULL, fmt, ##__VA_ARGS__)
static const char* find_end(const char* pos, char start, char end) {
  int level = 1;
  for (; *pos; pos++) {
    if (*pos == start)
      level++;
    else if (*pos == end) {
      level--;
      if (!level) return pos;
    }
  }
  return NULL;
}

static const char* next_name(const char* pos, const char** next, int* len) {
  while (*pos && isspace(*pos)) pos++;
  const char* start = pos;
  while (*pos && (isalnum(*pos) || *pos == '_')) pos++;
  *next = pos;
  *len  = pos - start;
  return start;
}

static const char* next_type(const char* pos, const char** next, int* len) {
  while (*pos && isspace(*pos)) pos++;
  const char* start = pos;
  if (*pos == '[') {
    const char* end = find_end(pos + 1, '[', ']');
    if (!end) return NULL;
    *next = end + 1;
    *len  = end - start;
    return start;
  }
  if (*pos == '{') {
    const char* end = find_end(pos + 1, '{', '}');
    if (!end) return NULL;
    *next = end + 1;
    *len  = end - start;
    return start;
  }
  return next_name(pos, next, len);
}

static bool starts_with_dot(const char* str) {
  return str && *(const volatile char*) str == '.';
}

static const char* check_array(json_t val, const char* def, const char* error_prefix) {
  if (val.type != JSON_TYPE_ARRAY) ERROR("%sExpected array", error_prefix);
  const char* next     = NULL;
  int         item_len = 0;
  int         idx      = 0;
  const char* item_def = next_type(def + 1, &next, &item_len);
  if (!next) ERROR("%sExpected array", error_prefix);
  while (*next && isspace(*next)) next++;
  json_for_each_value(val, item) {
    const char* err = json_validate(item, item_def, "");
    if (err) {
      char* new_err = bprintf(NULL, "%s at elemtent (idx: %d ) : %s", error_prefix, idx, err);
      safe_free((char*) err);
      return new_err;
    }
    if (*next == ',') {
      item_def = next_type(next + 1, &next, &item_len);
      while (*next && isspace(*next)) next++;
    }
    idx++;
  }
  return NULL;
}

static const char* check_object(json_t ob, const char* def, const char* error_prefix) {
  if (ob.type != JSON_TYPE_OBJECT) ERROR("%sExpected object", error_prefix);
  const char* next      = def;
  const char* name      = NULL;
  int         name_len  = 0;
  int         item_len  = 0;
  bytes_t     prop_name = NULL_BYTES;

  if (def[1] == '*' && def[2] == ':') {
    next += 3;
    while (*next && isspace(*next)) next++;
    const char* item_def = next_type(next, &next, &item_len);
    json_for_each_property(ob, val, prop_name) {
      const char* err = json_validate(val, item_def, "");
      if (err) {
        const char* sep     = starts_with_dot(err) ? "" : ":";
        const char* new_err = bprintf(NULL, "%s.%s%s", error_prefix, sep, err);
        safe_free((char*) err);
        return (char*) new_err;
      }
    }
    return NULL;
  }

  while ((name = next_name(def + 1, &next, &name_len))) {
    if (!next) ERROR("%sExpected object", error_prefix);
    bool optional = next && *next == '?';
    if (optional) next++;
    while (*next && isspace(*next)) next++;
    if (*next != ':') ERROR("%sExpected in def :", error_prefix);
    next++;
    while (*next && isspace(*next)) next++;
    const char* item_def = next_type(next, &next, &item_len);
    bool        found    = false;
    json_for_each_property(ob, val, prop_name) {
      if (prop_name.len == name_len && prop_name.data && memcmp(prop_name.data, name, name_len) == 0) {
        found = true;
        if (optional && val.type == JSON_TYPE_NULL) break;
        const char* err = json_validate(val, item_def, "");
        if (err) {
          const char* new_err = bprintf(NULL, "%s.%r%s%s", error_prefix, bytes(name, name_len), starts_with_dot(err) ? "" : ":", err);
          safe_free((char*) err);
          return new_err;
        }
        break;
      }
    }
    if (!found && !optional) ERROR("%smissing property %j", error_prefix, (json_t) {.type = JSON_TYPE_OBJECT, .start = name, .len = name_len});
    while (*next && isspace(*next)) next++;
    if (*next != ',') return NULL;
    def = next;
  }
  return NULL;
}

static const char* check_hex(json_t val, int len, bool isuint, const char* error_prefix) {
  if (val.type != JSON_TYPE_STRING) ERROR("%sExpected hex string", error_prefix);
  if (val.start[1] != '0' && val.start[2] != 'x') ERROR("%sExpected hex prefixed (0x) string", error_prefix);
  int l = 0;
  for (int i = 3; i < val.len - 1; i++, l++) {
    if (!isxdigit(val.start[i])) ERROR("%sExpected hex string", error_prefix);
  }

  if (len > 0 && (l % 2 || l / 2 != len)) ERROR("%sExpected hex string with fixed size (%d) but got %d bytes", error_prefix, len, l / 2);
  if (isuint && (l == 0 || (val.start[3] == '0' && l > 1)))
    ERROR("%sno leading zeros allowed for uint", error_prefix);
  if (isuint && l / 2 > 32) ERROR("%sexpected uint with max 32 bytes length, but got %d bytes ", error_prefix, l / 2);
  if (len < 0 && l / 2 > (-len)) ERROR("%sexpected uint with max %d bytes length, but got %d bytes ", error_prefix, -len, l / 2);
  return NULL;
}

static const char* check_block(json_t val, const char* error_prefix) {
  if (val.type != JSON_TYPE_STRING) ERROR("%sExpected block number", error_prefix);
  if (strncmp("\"latest\"", val.start, 8) == 0 || strncmp("\"safe\"", val.start, 6) == 0 || strncmp("\"finalized\"", val.start, 11) == 0) return NULL;
  return check_hex(val, 0, true, error_prefix);
}

static const char* check_suint(json_t val, const char* error_prefix) {
  // this is a uint number in quotes as string (no hex, only 0-9+)
  if (val.type != JSON_TYPE_STRING) ERROR("%sExpected suint", error_prefix);
  for (int i = 1; i < val.len - 1; i++) {
    if (!isdigit(val.start[i])) ERROR("%sExpected suint", error_prefix);
  }
  return NULL;
}

const char* json_validate(json_t val, const char* def, const char* error_prefix) {
  if (val.type == JSON_TYPE_INVALID) return strdup("Invalid JSON");
  if (*def == '[') return check_array(val, def, error_prefix ? error_prefix : "");
  if (*def == '{') return check_object(val, def, error_prefix ? error_prefix : "");
  if (strncmp(def, "bytes32", 7) == 0) return check_hex(val, 32, false, error_prefix ? error_prefix : "");
  if (strncmp(def, "address", 7) == 0) return check_hex(val, 20, false, error_prefix);
  if (strncmp(def, "hexuint", 7) == 0) return check_hex(val, 0, true, error_prefix);
  if (strncmp(def, "hex32", 5) == 0) return check_hex(val, -32, false, error_prefix);
  if (strncmp(def, "bytes", 5) == 0) return check_hex(val, 0, false, error_prefix);
  if (strncmp(def, "uint", 4) == 0) return val.type == JSON_TYPE_NUMBER ? NULL : strdup("Expected uint");
  if (strncmp(def, "suint", 5) == 0) return val.type == JSON_TYPE_STRING ? NULL : strdup("Expected suint");
  if (strncmp(def, "bool", 4) == 0) return val.type == JSON_TYPE_BOOLEAN ? NULL : strdup("Expected boolean");
  if (strncmp(def, "block", 5) == 0) return check_block(val, error_prefix);
  ERROR("%sUnknown type %s", error_prefix ? error_prefix : "", def);
}

// Lightweight cache for json validation results (non-security critical).
// We use a small ring buffer of the last N hashes, computed over (def || 0x00 || val.bytes).
#define JSON_VALIDATE_CACHE_COUNT 10
static uint64_t json_validate_cache[JSON_VALIDATE_CACHE_COUNT] = {0};
static uint32_t json_validate_cache_idx                        = 0;

static inline uint64_t fnv1a64_update(uint64_t h, const uint8_t* d, size_t n) {
  for (size_t i = 0; i < n; i++) {
    h ^= d[i];
    h *= 1099511628211ull; // FNV prime
  }
  return h;
}

const char* json_validate_cached(json_t val, const char* def, const char* error_prefix) {
  // Compute fast non-crypto hash of schema + separator + raw json bytes
  uint64_t h = 1469598103934665603ull; // FNV offset basis
  if (def && *def) h = fnv1a64_update(h, (const uint8_t*) def, strlen(def));
  uint8_t sep = 0;
  h           = fnv1a64_update(h, &sep, 1);
  if (val.start && val.len) h = fnv1a64_update(h, (const uint8_t*) val.start, val.len);

  for (uint32_t i = 0; i < JSON_VALIDATE_CACHE_COUNT; i++) {
    if (json_validate_cache[i] == h) return NULL; // cache hit → already validated
  }

  const char* err = json_validate(val, def, error_prefix);
  if (!err) {
    json_validate_cache[json_validate_cache_idx] = h;
    json_validate_cache_idx                      = (json_validate_cache_idx + 1) % JSON_VALIDATE_CACHE_COUNT;
  }
  return err;
}
