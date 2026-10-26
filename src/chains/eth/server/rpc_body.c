/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "rpc_body.h"
#include "eth_clients.h"
#include "util/json.h"
#include <string.h>

/**
 * Rewrites an `eth_createAccessList` body so Nethermind keeps warm storage keys.
 *
 * @param payload JSON-RPC request body
 * @param out destination buffer for a rewritten body; must be zeroed
 * @return the body to send
 */
static bytes_t nethermind_access_list_body(bytes_t payload, buffer_t* out) {
  if (!out || !payload.data || payload.len == 0) return payload;
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

bytes_t eth_rewrite_rpc_body(bytes_t payload, uint32_t client_type, buffer_t* out) {
  if (!out || !payload.data || payload.len == 0) return payload;
  // Further Ethereum client patches go here.
  if ((client_type & RPC_CLIENT_NETHERMIND) == 0) return payload;

  // A previous chain may already have stored `payload` in `out`.
  if (payload.data == out->data.data) {
    buffer_t scratch   = {0};
    bytes_t  rewritten = nethermind_access_list_body(payload, &scratch);
    if (rewritten.data != scratch.data.data) {
      buffer_free(&scratch);
      return payload;
    }
    buffer_free(out);
    *out = scratch;
    return out->data;
  }
  return nethermind_access_list_body(payload, out);
}
