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

#include "rpc_body.h"
#include "json.h"
#include <string.h>

static bool bytes_contains(bytes_t hay, const char* needle) {
  size_t n = strlen(needle);
  if (!hay.data || n == 0 || hay.len < n) return false;
  for (uint32_t i = 0; i + n <= hay.len; i++) {
    if (memcmp(hay.data + i, needle, n) == 0) return true;
  }
  return false;
}

bytes_t c4_nethermind_access_list_body(bytes_t payload, bool nethermind, buffer_t* out) {
  if (!nethermind || !out || !payload.data || payload.len == 0) return payload;
  if (!bytes_contains(payload, "\"eth_createAccessList\"")) return payload;

  // json_parse needs a trailing NUL. bytes_t does not promise one past len.
  buffer_t tmp = {0};
  buffer_grow(&tmp, (size_t) payload.len + 1);
  buffer_append(&tmp, payload);
  tmp.data.data[payload.len] = 0;

  json_t root = json_parse((char*) tmp.data.data);
  if (root.type != JSON_TYPE_OBJECT) {
    buffer_free(&tmp);
    return payload;
  }

  json_t method = json_get(root, "method");
  if (method.type != JSON_TYPE_STRING || method.len < 2 || method.start[0] != '"') {
    buffer_free(&tmp);
    return payload;
  }
  if (method.len - 2 != sizeof("eth_createAccessList") - 1 || memcmp(method.start + 1, "eth_createAccessList", method.len - 2) != 0) {
    buffer_free(&tmp);
    return payload;
  }

  json_t params = json_get(root, "params");
  if (params.type != JSON_TYPE_ARRAY || params.len < 2 || params.start < (const char*) tmp.data.data || params.start + params.len > (const char*) tmp.data.data + payload.len) {
    buffer_free(&tmp);
    return payload;
  }

  size_t n = json_len(params);
  if (n < 2 || n >= 4) {
    buffer_free(&tmp);
    return payload;
  }

  size_t bracket = (size_t) (params.start - (const char*) tmp.data.data) + params.len - 1;
  if (payload.data[bracket] != ']') {
    buffer_free(&tmp);
    return payload;
  }

  buffer_append(out, bytes(payload.data, (uint32_t) bracket));
  buffer_add_chars(out, n == 2 ? ",null,false" : ",false");
  buffer_append(out, bytes(payload.data + bracket, payload.len - (uint32_t) bracket));
  buffer_grow(out, (size_t) out->data.len + 1);
  out->data.data[out->data.len] = 0;
  buffer_free(&tmp);
  return out->data;
}
