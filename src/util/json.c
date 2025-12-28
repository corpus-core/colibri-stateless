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

#include "json.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// Macros for JSON object creation and constants
#define json(jtype, data, length) \
  (json_t) { .type = jtype, .start = data, .len = length }

#define JSON_INVALID(ptr)   json(JSON_TYPE_INVALID, ptr, 0)
#define JSON_NOT_FOUND(ptr) json(JSON_TYPE_NOT_FOUND, ptr, 0)

// Constants for JSON literal lengths
#define JSON_TRUE_LEN  4
#define JSON_FALSE_LEN 5
#define JSON_NULL_LEN  4

/**
 * Skip whitespace characters and return pointer to next non-whitespace character.
 * @param data pointer to current position in string
 * @return pointer to next non-whitespace character, or NULL if end of string
 */
static const char* next_non_whitespace_token(const char* data) {
  while (*data && isspace((unsigned char)*data)) data++;
  return *data ? data : NULL;
}

/**
 * Find the matching end character for a JSON structure, handling nesting and string escapes.
 * Used to find closing brackets/braces while properly handling nested structures and quoted strings.
 * @param pos current position after opening character
 * @param start opening character to match (e.g., '{', '[', '"')
 * @param end closing character to find (e.g., '}', ']', '"')
 * @return pointer to matching end character, or NULL if not found
 */
static const char* find_end(const char* pos, char start, char end) {
  int  level     = 1;
  bool in_string = start == '"';
  for (; *pos; pos++) {
    if (in_string && *pos == '\\') {
      if (!(*(++pos))) return NULL; // Skip escaped character
      continue;
    }
    if (*pos == start && !in_string)
      level++;
    else if (*pos == end && !in_string) {
      level--;
      if (!level) return pos;
    }
    else if (*pos == '"') {
      in_string = !in_string;
      if (!in_string && *pos == end) return pos;
    }
  }
  return NULL;
}

/**
 * Parse a JSON number and determine its length.
 * Supports integers, floats, and scientific notation.
 * @param start pointer to start of number
 * @return json_t structure with type JSON_TYPE_NUMBER and correct length
 */
static json_t parse_number(const char* start) {
  json_t json = json(JSON_TYPE_NUMBER, start, 0);
  for (; *start; start++) {
    if (isdigit((unsigned char)*start) || *start == '.' || *start == '-' || *start == 'e' || *start == 'E')
      json.len++;
    else
      break;
  }
  return json;
}

json_t json_parse(const char* data) {
  if (!data) return JSON_INVALID(NULL);

  json_t      invalid = JSON_INVALID(data);
  const char* start   = next_non_whitespace_token(data);
  const char* end     = NULL;

  if (!start) return invalid;
  switch (*start) {
    case '{':
      end = find_end(start + 1, '{', '}');
      return end ? json(JSON_TYPE_OBJECT, start, end - start + 1) : invalid;
    case '[':
      end = find_end(start + 1, '[', ']');
      return end ? json(JSON_TYPE_ARRAY, start, end - start + 1) : invalid;
    case '"':
      end = find_end(start + 1, '"', '"');
      return end ? json(JSON_TYPE_STRING, start, end - start + 1) : invalid;
    case 't':
      return strncmp(start, "true", JSON_TRUE_LEN) ? invalid : json(JSON_TYPE_BOOLEAN, start, JSON_TRUE_LEN);
    case 'f':
      return strncmp(start, "false", JSON_FALSE_LEN) ? invalid : json(JSON_TYPE_BOOLEAN, start, JSON_FALSE_LEN);
    case 'n':
      return strncmp(start, "null", JSON_NULL_LEN) ? invalid : json(JSON_TYPE_NULL, start, JSON_NULL_LEN);
    case '-':
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      return parse_number(start);
    default:
      return invalid;
  }
  return invalid;
}

json_t json_next_value(json_t value, bytes_t* property_name, json_next_t type) {
  if (value.type == JSON_TYPE_INVALID || value.type == JSON_TYPE_NOT_FOUND) return value;
  const char* start = next_non_whitespace_token(value.start + (type == JSON_NEXT_FIRST ? 1 : value.len));
  if (!start) return JSON_INVALID(start);
  if (type != JSON_NEXT_FIRST) {
    if (*start == ',') {
      start++;
      start = next_non_whitespace_token(start);
      if (!start) return JSON_INVALID(value.start);
    }
  }
  else
    type = value.type == JSON_TYPE_OBJECT ? JSON_NEXT_PROPERTY : JSON_NEXT_VALUE;
  if (*start == '}' || *start == ']') return JSON_NOT_FOUND(start);

  if (type == JSON_NEXT_PROPERTY) {
    if (*start != '"') return JSON_INVALID(start);
    const char* end_name = find_end(start + 1, '"', '"');
    if (!end_name) return JSON_INVALID(start);
    if (property_name) {
      property_name->data = (uint8_t*) start + 1;
      property_name->len  = end_name - start - 1;
    }
    start = next_non_whitespace_token(end_name + 1);
    if (!start || *start != ':') return JSON_INVALID(start);
    start = next_non_whitespace_token(start + 1);
  }

  return json_parse(start);
}

json_t json_get(json_t parent, const char* property) {
  if (!property) return JSON_INVALID(parent.start);
  if (parent.type != JSON_TYPE_OBJECT) return JSON_INVALID(parent.start);
  bytes_t property_name = NULL_BYTES;
  size_t  len           = strlen(property);
  json_for_each_property(parent, value, property_name) {
    if (property_name.len == len && property_name.data && memcmp(property_name.data, property, len) == 0) return value;
  }
  return JSON_NOT_FOUND(parent.start);
}

json_t json_at(json_t parent, size_t index) {
  if (parent.type != JSON_TYPE_ARRAY) return JSON_INVALID(parent.start);
  size_t i = 0;
  json_for_each_value(parent, value) {
    if (index == i++) return value;
  }
  return JSON_NOT_FOUND(parent.start);
}

size_t json_len(json_t parent) {
  if (parent.type != JSON_TYPE_ARRAY) return 0;
  size_t i = 0;
  json_for_each_value(parent, value) i++;
  return i;
}

json_t json_get_path(json_t parent, const char* path) {
  char tmp[256];
  if (!path) return JSON_INVALID(parent.start);
  const char* next_prop = strchr(path, '.');
  const char* next_idx  = strchr(path, '[');
  const char* next      = (next_prop && next_idx) ? (next_prop < next_idx ? next_prop : next_idx) : (next_prop ? next_prop : next_idx);
  if (!next) return json_get(parent, path);
  strncpy(tmp, path, next - path);
  tmp[next - path] = '\0';
  json_t value     = json_get(parent, tmp);
  if (value.type == JSON_TYPE_INVALID) return value;
  if (next && strlen(next + 1) > 0) {
    if (*next == '[') {
      const char* end_idx = strchr(next + 1, ']');
      if (!end_idx) return JSON_INVALID(parent.start);
      strncpy(tmp, next + 1, end_idx - next - 1);
      tmp[end_idx - next - 1] = '\0';
      json_t item             = json_at(value, atoi(tmp));
      if (item.type == JSON_TYPE_INVALID) return item;
      if (end_idx[1] == '.')
        return json_get_path(item, end_idx + 2);
      else
        return item;
    }
    else
      return json_get_path(value, next + 1);
  }
  return value;
}

static void json_deescape_string(buffer_t* buffer) {
  if (!buffer) return;
  for (size_t i = 0; i < buffer->data.len; i++) {
    if (buffer->data.data[i] == '\\' && i + 1 < buffer->data.len) {
      uint8_t replacement[4];
      size_t  replacement_len = 0;
      uint8_t next_char       = buffer->data.data[i + 1];

      switch (next_char) {
        case '"':
          replacement[0]  = '"';
          replacement_len = 1;
          break;
        case '\\':
          replacement[0]  = '\\';
          replacement_len = 1;
          break;
        case '/':
          replacement[0]  = '/';
          replacement_len = 1;
          break;
        case 'b':
          replacement[0]  = '\b';
          replacement_len = 1;
          break;
        case 'f':
          replacement[0]  = '\f';
          replacement_len = 1;
          break;
        case 'n':
          replacement[0]  = '\n';
          replacement_len = 1;
          break;
        case 'r':
          replacement[0]  = '\r';
          replacement_len = 1;
          break;
        case 't':
          replacement[0]  = '\t';
          replacement_len = 1;
          break;
        case 'u':
          // Unicode escape sequence: \uXXXX
          if (i + 5 < buffer->data.len) {
            // Parse the 4 hex digits
            uint16_t codepoint = 0;
            bool     valid     = true;
            for (int j = 0; j < 4; j++) {
              uint8_t hex_char = buffer->data.data[i + 2 + j];
              uint8_t hex_val  = 0;
              if (hex_char >= '0' && hex_char <= '9')
                hex_val = hex_char - '0';
              else if (hex_char >= 'a' && hex_char <= 'f')
                hex_val = hex_char - 'a' + 10;
              else if (hex_char >= 'A' && hex_char <= 'F')
                hex_val = hex_char - 'A' + 10;
              else {
                valid = false;
                break;
              }
              codepoint = (codepoint << 4) | hex_val;
            }

            if (valid) {
              // Convert Unicode codepoint to UTF-8
              if (codepoint <= 0x7F) {
                // 1-byte UTF-8
                replacement[0]  = (uint8_t) codepoint;
                replacement_len = 1;
              }
              else if (codepoint <= 0x7FF) {
                // 2-byte UTF-8
                replacement[0]  = 0xC0 | ((codepoint >> 6) & 0x1F);
                replacement[1]  = 0x80 | (codepoint & 0x3F);
                replacement_len = 2;
              }
              else {
                // 3-byte UTF-8 (covers BMP)
                replacement[0]  = 0xE0 | ((codepoint >> 12) & 0x0F);
                replacement[1]  = 0x80 | ((codepoint >> 6) & 0x3F);
                replacement[2]  = 0x80 | (codepoint & 0x3F);
                replacement_len = 3;
              }
              buffer_splice(buffer, i, 6, bytes(replacement, replacement_len));
              i += replacement_len - 1;
              continue;
            }
          }
          // Invalid unicode escape, skip
          continue;
        default:
          // Unknown escape sequence, skip
          continue;
      }

      // Replace the escape sequence with the unescaped character
      buffer_splice(buffer, i, 2, bytes(replacement, replacement_len));
    }
  }
}
json_t json_dup(json_t json) {
  char* val       = (char*) bytes_dup(bytes(json.start, json.len + 1)).data;
  val[json.len]   = 0;
  json_t new_json = json;
  new_json.start  = val;
  return new_json;
}

char* json_as_string(json_t value, buffer_t* buffer) {
  buffer_t tmp = {0};
  if (!buffer) buffer = &tmp;
  buffer->data.len = 0;
  buffer_grow(buffer, value.len + 1);
  if (value.type == JSON_TYPE_STRING) {
    buffer_append(buffer, bytes((uint8_t*) value.start + 1, value.len - 2));
    buffer->data.data[buffer->data.len] = '\0';
    buffer->data.len++;
    json_deescape_string(buffer);
  }
  else {
    buffer_append(buffer, bytes((uint8_t*) value.start, value.len));
    buffer->data.data[buffer->data.len] = '\0';
  }
  return (char*) buffer->data.data;
}

uint64_t json_as_uint64(json_t value) {
  uint8_t  tmp[20] = {0};
  buffer_t buffer  = stack_buffer(tmp);
  if (value.len > 4 && value.start && value.start[1] == '0' && value.start[2] == 'x') {
    int len = hex_to_bytes(value.start + 1, value.len - 2, bytes(tmp, 20));
    if (len == -1) return 0;
    memmove(tmp + 8 - len, tmp, len);
    memset(tmp, 0, 8 - len);
    return uint64_from_be(tmp);
  }
  return (uint64_t) strtoull(json_as_string(value, &buffer), NULL, 10);
}
uint32_t json_to_bytes(json_t value, bytes_t target) {
  buffer_t buffer = (buffer_t) {.data = bytes(target.data, 0), .allocated = -((int32_t) target.len)};
  return json_as_bytes(value, &buffer).len;
}

bytes_t json_as_bytes(json_t value, buffer_t* buffer) {
  buffer_t tmp = {0};
  if (!buffer) buffer = &tmp;
  if (value.type == JSON_TYPE_NUMBER) {
    buffer->data.len = 8;
    buffer_grow(buffer, 8);
    uint64_to_be(buffer->data.data, json_as_uint64(value));
    return buffer->data;
  }
  if (value.type != JSON_TYPE_STRING) return NULL_BYTES;

  buffer_grow(buffer, value.len / 2);
  buffer->data.len = value.len;
  int len          = hex_to_bytes(value.start + 1, value.len - 2, buffer->data);
  if (len == -1) return NULL_BYTES;
  buffer->data.len = (uint32_t) len;
  return buffer->data;
}

bool json_as_bool(json_t value) {
  return value.type == JSON_TYPE_BOOLEAN && value.start && value.start[0] == 't';
}

bool json_as_null(json_t value) {
  return value.type == JSON_TYPE_NULL && value.start && value.start[0] == 'n';
}

void buffer_add_json(buffer_t* buffer, json_t data) {
  buffer_grow(buffer, buffer->data.len + data.len + 1);
  buffer_append(buffer, bytes((uint8_t*) data.start, data.len));
  buffer->data.data[buffer->data.len] = 0;
}

char* json_new_string(json_t parent) {
  return json_as_string(parent, NULL);
}

bool json_equal_string(json_t value, const char* str) {
  if (!str || !value.start) return false;
  int len = strlen(str);
  if (len < 2) return false; // Prevent underflow
  return value.type == JSON_TYPE_STRING && value.len == len - 2 && memcmp(value.start + 1, str, value.len - 2) == 0;
}
