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

#include "state_overrides.h"
#include "../../../util/json.h"
#include "../../../util/state.h"

static bool prop_eq(bytes_t prop, const char* name) {
  size_t n = strlen(name);
  return prop.data && prop.len == n && memcmp(prop.data, name, n) == 0;
}

static c4_status_t parse_address_key(c4_state_t* st, bytes_t prop_name, address_t* out_address) {
  // Recreate a JSON string value for validation/parsing (key is not quoted in prop_name).
  json_t addr_json = (json_t) {
      .type  = JSON_TYPE_STRING,
      .start = (const char*) prop_name.data - 1,
      .len   = prop_name.len + 2,
  };

  char* err = (char*) json_validate(addr_json, "address", "stateOverrides");
  if (err) return c4_state_set_error_msg(st, err);

  buffer_t buf = stack_buffer(*out_address);
  if (json_as_bytes(addr_json, &buf).len != 20)
    return c4_state_set_error_msg(st, bprintf(NULL, "stateOverrides: invalid address key"));

  return C4_SUCCESS;
}

static c4_status_t parse_uint256_be(c4_state_t* st, json_t value, bytes32_t out) {
  buffer_t tmp = {0};
  bytes_t  b   = json_as_bytes(value, &tmp);
  if (!b.data) {
    buffer_free(&tmp);
    return c4_state_set_error_msg(st, bprintf(NULL, "stateOverrides: invalid uint256 value"));
  }
  if (b.len > 32) {
    buffer_free(&tmp);
    return c4_state_set_error_msg(st, bprintf(NULL, "stateOverrides: uint256 too large"));
  }
  memset(out, 0, 32);
  memcpy(out + (32 - b.len), b.data, b.len);
  buffer_free(&tmp);
  return C4_SUCCESS;
}

static c4_status_t parse_storage_map(c4_state_t* st, json_t storage_obj, eth_storage_override_t** out_list) {
  bytes_t key_name = NULL_BYTES;
  json_for_each_property(storage_obj, slot_val, key_name) {
    bytes32_t key   = {0};
    bytes32_t value = {0};

    json_t key_json = (json_t) {
        .type  = JSON_TYPE_STRING,
        .start = (const char*) key_name.data - 1,
        .len   = key_name.len + 2,
    };

    char* err = (char*) json_validate(key_json, "bytes32", "stateOverrides.storage");
    if (err) return c4_state_set_error_msg(st, err);

    buffer_t key_buf = stack_buffer(key);
    if (json_as_bytes(key_json, &key_buf).len != 32)
      return c4_state_set_error_msg(st, bprintf(NULL, "stateOverrides: invalid storage slot key length"));

    buffer_t val_buf = stack_buffer(value);
    if (json_as_bytes(slot_val, &val_buf).len != 32)
      return c4_state_set_error_msg(st, bprintf(NULL, "stateOverrides: invalid storage slot value length"));

    eth_storage_override_t* entry = (eth_storage_override_t*) safe_calloc(1, sizeof(eth_storage_override_t));
    memcpy(entry->key, key, 32);
    memcpy(entry->value, value, 32);
    entry->next = *out_list;
    *out_list   = entry;
  }
  return C4_SUCCESS;
}

const eth_account_override_t* eth_state_overrides_find(const eth_state_overrides_t* overrides, const address_t address) {
  if (!overrides) return NULL;
  for (const eth_account_override_t* a = overrides->accounts; a; a = a->next) {
    if (memcmp(a->address, address, 20) == 0) return a;
  }
  return NULL;
}

void eth_state_overrides_free(eth_state_overrides_t* overrides) {
  if (!overrides) return;
  while (overrides->accounts) {
    eth_account_override_t* next = overrides->accounts->next;
    eth_storage_override_t* s    = overrides->accounts->storage;
    while (s) {
      eth_storage_override_t* sn = s->next;
      safe_free(s);
      s = sn;
    }
    if (overrides->accounts->has_code && overrides->accounts->code.data)
      safe_free(overrides->accounts->code.data);
    safe_free(overrides->accounts);
    overrides->accounts = next;
  }
}

static c4_status_t validate_override_keys(c4_state_t* st, json_t override_obj) {
  bytes_t prop = NULL_BYTES;
  json_for_each_property(override_obj, val, prop) {
    if (prop_eq(prop, "balance") || prop_eq(prop, "code") || prop_eq(prop, "state") || prop_eq(prop, "stateDiff"))
      continue;
    if (prop_eq(prop, "nonce"))
      return c4_state_set_error_msg(st, bprintf(NULL, "stateOverrides: property 'nonce' is not supported"));
    if (prop_eq(prop, "movePrecompileToAddress"))
      return c4_state_set_error_msg(st, bprintf(NULL, "stateOverrides: property 'movePrecompileToAddress' is not supported"));
    if (prop_eq(prop, "blockOverrides"))
      return c4_state_set_error_msg(st, bprintf(NULL, "stateOverrides: property 'blockOverrides' is not supported"));

    return c4_state_set_error_msg(st, bprintf(NULL, "stateOverrides: unsupported property '%r'", bytes(prop.data, prop.len)));
  }
  return C4_SUCCESS;
}

c4_status_t eth_parse_state_overrides_state(c4_state_t* state, json_t overrides, eth_state_overrides_t* out) {
  c4_state_t* st = state;
  if (!out) return c4_state_add_error(st, "stateOverrides: invalid output pointer");
  memset(out, 0, sizeof(*out));

  if (overrides.type == JSON_TYPE_NOT_FOUND || overrides.type == JSON_TYPE_NULL) return C4_SUCCESS;
  if (overrides.type == JSON_TYPE_INVALID) return c4_state_add_error(st, "stateOverrides: invalid JSON");
  if (overrides.type != JSON_TYPE_OBJECT) return c4_state_set_error_msg(st, bprintf(NULL, "stateOverrides: expected object"));

  bytes_t acc_key = NULL_BYTES;
  json_for_each_property(overrides, override_obj, acc_key) {
    address_t address = {0};
    TRY_ASYNC(parse_address_key(st, acc_key, &address));

    if (override_obj.type != JSON_TYPE_OBJECT)
      return c4_state_set_error_msg(st, bprintf(NULL, "stateOverrides: override for 0x%x must be an object", bytes(address, 20)));

    TRY_ASYNC(validate_override_keys(st, override_obj));

    json_t state_obj      = json_get(override_obj, "state");
    json_t state_diff_obj = json_get(override_obj, "stateDiff");
    bool   has_state      = state_obj.type == JSON_TYPE_OBJECT;
    bool   has_stateDiff  = state_diff_obj.type == JSON_TYPE_OBJECT;
    if (has_state && has_stateDiff)
      return c4_state_set_error_msg(st, bprintf(NULL, "stateOverrides: 'state' and 'stateDiff' are mutually exclusive"));

    eth_account_override_t* acc = (eth_account_override_t*) safe_calloc(1, sizeof(eth_account_override_t));
    memcpy(acc->address, address, 20);

    json_t balance = json_get(override_obj, "balance");
    if (balance.type != JSON_TYPE_NOT_FOUND && balance.type != JSON_TYPE_NULL) {
      char* err = (char*) json_validate(balance, "hexuint", "stateOverrides.balance");
      if (err) {
        safe_free(acc);
        return c4_state_set_error_msg(st, err);
      }
      c4_status_t s = parse_uint256_be(st, balance, acc->balance);
      if (s != C4_SUCCESS) {
        safe_free(acc);
        return s;
      }
      acc->has_balance = true;
    }

    json_t code = json_get(override_obj, "code");
    if (code.type != JSON_TYPE_NOT_FOUND && code.type != JSON_TYPE_NULL) {
      char* err = (char*) json_validate(code, "bytes", "stateOverrides.code");
      if (err) {
        safe_free(acc);
        return c4_state_set_error_msg(st, err);
      }
      buffer_t tmp = {0};
      bytes_t  c   = json_as_bytes(code, &tmp);
      if (!c.data) {
        buffer_free(&tmp);
        safe_free(acc);
        return c4_state_set_error_msg(st, bprintf(NULL, "stateOverrides: invalid 'code' value"));
      }
      acc->code     = bytes_dup(c);
      acc->has_code = true;
      buffer_free(&tmp);
    }

    if (has_state) {
      char* err = (char*) json_validate(state_obj, "{*:bytes32}", "stateOverrides.state");
      if (err) {
        safe_free(acc);
        return c4_state_set_error_msg(st, err);
      }
      acc->full_state = true;
      c4_status_t s   = parse_storage_map(st, state_obj, &acc->storage);
      if (s != C4_SUCCESS) {
        if (acc->storage) {
          eth_storage_override_t* cur = acc->storage;
          while (cur) {
            eth_storage_override_t* next = cur->next;
            safe_free(cur);
            cur = next;
          }
        }
        safe_free(acc);
        return s;
      }
    }
    else if (has_stateDiff) {
      char* err = (char*) json_validate(state_diff_obj, "{*:bytes32}", "stateOverrides.stateDiff");
      if (err) {
        safe_free(acc);
        return c4_state_set_error_msg(st, err);
      }
      acc->full_state = false;
      c4_status_t s   = parse_storage_map(st, state_diff_obj, &acc->storage);
      if (s != C4_SUCCESS) {
        if (acc->storage) {
          eth_storage_override_t* cur = acc->storage;
          while (cur) {
            eth_storage_override_t* next = cur->next;
            safe_free(cur);
            cur = next;
          }
        }
        safe_free(acc);
        return s;
      }
    }

    acc->next     = out->accounts;
    out->accounts = acc;
  }

  return C4_SUCCESS;
}

c4_status_t eth_parse_state_overrides(verify_ctx_t* ctx, json_t overrides, eth_state_overrides_t* out) {
  if (!ctx) return C4_ERROR;
  return eth_parse_state_overrides_state(&ctx->state, overrides, out);
}
