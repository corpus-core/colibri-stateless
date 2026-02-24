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

#ifndef CALL_CTX_H
#define CALL_CTX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "bytes.h"
#include "crypto.h"
#include "eth_account.h"
#include "json.h"
#include "plugin.h"
#include "ssz.h"
#include "state.h"
#include "state_overrides.h"
#include "verify.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
  bytes32_t key;
  bytes32_t value;
  uint64_t  verified_at; // block number when this value was verified or zero if not at all.
  bool      accessed;    // true if this slot was read during the current EVM run (not persisted)
} cached_storage_t;

typedef struct cached_account {
  address_t              address;
  bytes32_t              storage_root;
  bytes32_t              balance;
  bytes32_t              code_hash;
  uint32_t               num_storage;
  uint64_t               verified_at; // block number when this value was verified or zero if not at all.
  cached_storage_t*      storage;
  struct cached_account* next;
} cached_account_t;

// Forward declarations for eth_call_cache helpers used in PAP-mode static functions below.
// Full definitions are in eth_call_cache.h which must be included before call_ctx.h in
// any translation unit that uses the PAP path (verify_call.c, call_evmone.c).
cached_account_t* eth_call_cache_find(cached_account_t* list, const address_t addr);
cached_account_t* eth_call_cache_get_or_create(cached_account_t** list, const address_t addr);
void              eth_call_cache_set_storage(cached_account_t* account, const bytes32_t key, const bytes32_t value, uint64_t verified_at);

#ifdef EVMONE
#include "evmone_c_wrapper.h" // For evmc_address and evmc_bytes32
#endif

typedef struct account_storage {
  bytes32_t               key;
  bytes32_t               value;
  bool                    original; // true = cached from proof, not yet modified by SSTORE
  struct account_storage* next;
} account_storage_t;

typedef struct account_state {
  address_t             address;
  bytes32_t             balance;
  bytes_t               code;
  struct account_state* next;
  account_storage_t*    storage;
  bool                  full_state_override;
  bool                  deleted;
  bool                  free_code;
} account_state_t;

// Context for EVM execution
// Structure to store emitted log events
typedef struct emitted_log {
  address_t           address;      // Contract address that emitted the log
  bytes_t             data;         // Log data
  bytes32_t*          topics;       // Array of topics
  size_t              topics_count; // Number of topics
  struct emitted_log* next;         // Linked list pointer
} emitted_log_t;

/**
 * Shared context for EVM call verification (`eth_call`, `eth_estimateGas`, `colibri_simulateTransaction`).
 *
 * Holds all inputs, intermediate state, and outputs for the EVM execution.
 * Cleanup via `evm_call_ctx_free()` frees all owned resources (overrides, call_codes,
 * call_result, logs). Fields whose ownership was transferred to `ctx->data`
 * must be zeroed before calling free.
 */
typedef struct evm_call_ctx {
  ssz_ob_t              accounts;
  call_code_t*          call_codes;
  eth_state_overrides_t overrides;
  bytes_t               call_result;
  emitted_log_t*        logs;
  uint64_t              gas_used;
  bytes32_t             state_root;
  cached_account_t**    pap_accounts; // pointer to list head (e.g. &state->accounts); NULL if not used
} evm_call_ctx_t;

void evm_call_ctx_free(evm_call_ctx_t* evm);

typedef struct evmone_context {
  void*            executor;
  verify_ctx_t*    ctx;
  ssz_ob_t         src_accounts;
  account_state_t* account_states;
  call_code_t*     call_codes;
  // Current block info
  uint64_t  block_number;
  bytes32_t block_hash;
  uint64_t  timestamp;
  // Transaction info
  bytes32_t tx_origin;
  uint64_t  gas_price;
  // For storing results
  struct evmone_context* parent;
  void*                  results;
  // Event logging
  emitted_log_t* logs;           // Linked list of emitted logs
  bool           capture_events; // Whether to capture events
  // Pointer to cache list head (e.g. &state->accounts); NULL if not used
  cached_account_t** pap_accounts;
  bool               storage_miss; // true once a storage request was created; suppresses further requests
} evmone_context_t;

static account_state_t* create_account_state(evmone_context_t* ctx, const address_t address, bool* created);

static ssz_ob_t get_src_account(evmone_context_t* ctx, const address_t address, bool allow_missing) {
  size_t len = ssz_len(ctx->src_accounts);
  for (int i = 0; i < len; i++) {
    ssz_ob_t account = ssz_at(ctx->src_accounts, i);
    bytes_t  addr    = ssz_get(&account, "address").bytes;
    if (memcmp(addr.data, address, 20) == 0)
      return account;
  }
  if (ctx->parent)
    return get_src_account(ctx->parent, address, allow_missing);
  if (!ctx->ctx->state.error && !allow_missing) {
    char _tmp[64];
    sbprintf(_tmp, "Missing account proof for 0x%x", bytes(address, 20));
    c4_state_add_error(&ctx->ctx->state, _tmp);
  }

  return (ssz_ob_t) {0};
}

static void get_src_storage(evmone_context_t* ctx, const address_t address, const bytes32_t key, bytes32_t result) {
  ssz_ob_t account = get_src_account(ctx, address, ctx->pap_accounts != NULL);
  if (account.def) {
    ssz_ob_t storage = ssz_get(&account, "storageProof");
    uint32_t len     = ssz_len(storage);
    for (uint32_t i = 0; i < len; i++) {
      ssz_ob_t entry = ssz_at(storage, i);
      if (memcmp(ssz_get(&entry, "key").bytes.data, key, 32) == 0) {
        if (!eth_get_storage_value(entry, key, result)) memset(result, 0, 32);
        // cache the verified value for subsequent reads
        bool               created;
        account_state_t*   acc = create_account_state(ctx, address, &created);
        account_storage_t* s   = safe_calloc(1, sizeof(account_storage_t));
        memcpy(s->key, key, 32);
        memcpy(s->value, result, 32);
        s->original  = true;
        s->next      = acc->storage;
        acc->storage = s;
        return;
      }
    }
  }

  // No value in proof: fetch via eth_getStorageAt (unified path; caller sets pap_accounts so we can track).
  if (!ctx->pap_accounts) return; // No list to track; request creation below would orphan the response.

  // Check for an existing data_request, or create one.
  char      tmp[256];
  buffer_t  buf    = stack_buffer(tmp);
  bytes32_t req_id = {0};
  bprintf(&buf, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_getStorageAt\",\"params\":[\"0x%x\",\"0x%x\",\"latest\"]}", bytes((uint8_t*) address, 20), bytes((uint8_t*) key, 32));
  keccak(buf.data, req_id);

  data_request_t* req = c4_state_get_data_request_by_id(&ctx->ctx->state, req_id);
  if (req && req->response.data) {
    // Response available: parse the value and populate cache.
    json_t    val_json = json_get(json_parse((char*) req->response.data), "result");
    bytes32_t val      = {0};
    if (val_json.type == JSON_TYPE_STRING) {
      buffer_t val_buf = stack_buffer(val);
      bytes_t  b       = json_as_bytes(val_json, &val_buf);
      if (b.len <= 32) memcpy(val + (32 - b.len), b.data, b.len);
    }
    memcpy(result, val, 32);

    // Store in the cache list (unverified) and mark as accessed.
    if (ctx->pap_accounts) {
      cached_account_t* cached = eth_call_cache_get_or_create(ctx->pap_accounts, address);
      eth_call_cache_set_storage(cached, key, val, 0);
      // Mark accessed so Phase C knows this slot needs proof verification.
      for (uint32_t _i = 0; _i < cached->num_storage; _i++) {
        if (memcmp(cached->storage[_i].key, key, 32) == 0) {
          cached->storage[_i].accessed = true;
          break;
        }
      }
    }

    // Also add to account_states for fast subsequent lookups within this EVM run.
    bool               created;
    account_state_t*   acc = create_account_state(ctx, address, &created);
    account_storage_t* s   = safe_calloc(1, sizeof(account_storage_t));
    memcpy(s->key, key, 32);
    memcpy(s->value, val, 32);
    s->original  = true;
    s->next      = acc->storage;
    acc->storage = s;
    return;
  }
  else if (req && req->error) {
    c4_state_add_error(&ctx->ctx->state, req->error);
    return;
  }

  // No existing request: create one (unless we already have a pending miss) and let the EVM
  // continue with 0. Once there is a miss, all subsequent results are unreliable so we skip
  // creating further requests to avoid fetching values only needed on the wrong execution path.
  if (!ctx->storage_miss) {
    data_request_t* new_req = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
    new_req->chain_id       = ctx->ctx->chain_id;
    new_req->encoding       = C4_DATA_ENCODING_JSON;
    new_req->type           = C4_DATA_TYPE_ETH_RPC;
    new_req->method         = C4_DATA_METHOD_POST;
    new_req->payload        = bytes_dup(buf.data);
    memcpy(new_req->id, req_id, 32);
    c4_state_add_request(&ctx->ctx->state, new_req);
    ctx->storage_miss = true;
  }
}

static account_state_t* get_account_state(evmone_context_t* ctx, const address_t address) {
  for (account_state_t* acc = ctx->account_states; acc != NULL; acc = acc->next) {
    if (memcmp(acc->address, address, 20) == 0)
      return acc;
  }
  if (ctx->parent)
    return get_account_state(ctx->parent, address);
  return NULL;
}

static account_storage_t* get_storage(evmone_context_t* ctx, const address_t addr, const bytes32_t key) {
  account_state_t* account = get_account_state(ctx, addr);
  if (!account) return NULL;
  for (account_storage_t* s = account->storage; s != NULL; s = s->next) {
    if (memcmp(s->key, key, 32) == 0)
      return s;
  }
  return NULL;
}

static account_state_t* create_account_state(evmone_context_t* ctx, const address_t address, bool* created) {
  *created = false;
  for (account_state_t* acc = ctx->account_states; acc != NULL; acc = acc->next) {
    if (memcmp(acc->address, address, 20) == 0)
      return acc;
  }
  account_state_t* parent_acc  = ctx->parent ? get_account_state(ctx->parent, address) : NULL;
  *created                     = parent_acc == NULL;
  ssz_ob_t         old_account = get_src_account(ctx, address, true);
  account_state_t* acc         = safe_calloc(1, sizeof(account_state_t));
  memcpy(acc->address, address, 20);
  acc->next           = ctx->account_states;
  ctx->account_states = acc;

  if (parent_acc) {
    memcpy(acc->balance, parent_acc->balance, 32);
    acc->code                       = parent_acc->code;
    acc->full_state_override        = parent_acc->full_state_override;
    account_storage_t** storage_ptr = &acc->storage;
    for (account_storage_t* s = parent_acc->storage; s != NULL; s = s->next) {
      *storage_ptr = safe_calloc(1, sizeof(account_storage_t));
      memcpy((*storage_ptr)->key, s->key, 32);
      memcpy((*storage_ptr)->value, s->value, 32);
      (*storage_ptr)->original = s->original;
      (*storage_ptr)->next     = NULL;
      storage_ptr              = &(*storage_ptr)->next;
    }
  }
  else if (old_account.def) {
    ssz_ob_t code = ssz_get(&old_account, "code");
    if (code.def && code.def->type == SSZ_TYPE_LIST && code.bytes.len > 0) acc->code = code.bytes;
    eth_get_account_value(old_account, ETH_ACCOUNT_BALANCE, acc->balance);
  }
  else if (ctx->pap_accounts && *ctx->pap_accounts) {
    // No proof account – fall back to cached account data when list exists.
    cached_account_t* cached = eth_call_cache_find(*ctx->pap_accounts, address);
    if (cached) memcpy(acc->balance, cached->balance, 32);
  }
  return acc;
}

static void set_storage(evmone_context_t* ctx, const address_t addr, const bytes32_t key, const bytes32_t value, bool* account_created, bool* storage_created) {
  account_storage_t* storage = get_storage(ctx, addr, key);
  if (storage) {
    *storage_created  = storage->original; // first real write counts as created
    storage->original = false;
    memcpy(storage->value, value, 32);
    *account_created = false;
  }
  else {
    account_state_t* account   = create_account_state(ctx, addr, account_created);
    *storage_created           = true;
    account_storage_t* storage = safe_calloc(1, sizeof(account_storage_t));
    memcpy(storage->key, key, 32);
    memcpy(storage->value, value, 32);
    storage->next    = account->storage;
    account->storage = storage;
  }
}
static bytes_t get_code(evmone_context_t* ctx, const address_t address) {
  if (bytes_all_zero(bytes(address, 20))) return NULL_BYTES;
  account_state_t* acc = get_account_state(ctx, address);
  if (acc) return acc->code;
  ssz_ob_t account = get_src_account(ctx, address, false);
  if (!account.def) return NULL_BYTES;
  bytes32_t code_hash = {0};
  eth_get_account_value(account, ETH_ACCOUNT_CODE_HASH, code_hash);
  for (call_code_t* call_code = ctx->call_codes; call_code; call_code = call_code->next) {
    if (memcmp(call_code->hash, code_hash, 32) == 0)
      return call_code->code;
  }

  ssz_ob_t code = ssz_get(&account, "code");
  if (code.def && code.def->type == SSZ_TYPE_LIST) return code.bytes;
  return NULL_BYTES;
  //  return account.def ? ssz_get(&account, "code").bytes : NULL_BYTES;
}

static void account_state_free(account_state_t* acc) {
  while (acc->storage) {
    account_storage_t* storage = acc->storage;
    acc->storage               = storage->next;
    safe_free(storage);
  }
  if (acc->code.data && acc->free_code)
    safe_free(acc->code.data);
  safe_free(acc);
}

static void free_emitted_logs(emitted_log_t* logs) {
  while (logs) {
    emitted_log_t* next = logs->next;
    if (logs->data.data) safe_free(logs->data.data);
    if (logs->topics) safe_free(logs->topics);
    safe_free(logs);
    logs = next;
  }
}

static void context_free(evmone_context_t* ctx) {
  while (ctx->account_states) {
    account_state_t* next = ctx->account_states->next;
    account_state_free(ctx->account_states);
    ctx->account_states = next;
  }
  free_emitted_logs(ctx->logs);
  ctx->logs = NULL;
}

#ifdef EVMONE
static emitted_log_t* add_emitted_log(evmone_context_t* ctx, const evmc_address* addr, const uint8_t* data, size_t data_size, const evmc_bytes32 topics[], size_t topics_count) {
  if (!ctx->capture_events) return NULL;

  emitted_log_t* log = safe_calloc(1, sizeof(emitted_log_t));
  memcpy(log->address, addr->bytes, 20);

  // Copy log data
  if (data && data_size > 0) {
    log->data.data = safe_malloc(data_size);
    memcpy(log->data.data, data, data_size);
    log->data.len = data_size;
  }

  // Copy topics
  if (topics && topics_count > 0) {
    log->topics       = safe_calloc(topics_count, sizeof(bytes32_t));
    log->topics_count = topics_count;
    for (size_t i = 0; i < topics_count; i++) {
      memcpy(log->topics[i], topics[i].bytes, 32);
    }
  }

  // Add to linked list
  log->next = ctx->logs;
  ctx->logs = log;

  return log;
}
#endif // EVMONE

static void context_apply(evmone_context_t* ctx) {
  if (!ctx->parent) return;
  bool created;
  for (account_state_t* acc = ctx->account_states; acc; acc = acc->next) {
    account_state_t* parent_acc = create_account_state(ctx->parent, acc->address, &created);
    memcpy(parent_acc->balance, acc->balance, 32);
    parent_acc->code                = acc->code;
    parent_acc->free_code           = acc->free_code;
    parent_acc->full_state_override = acc->full_state_override;

    for (account_storage_t* s = acc->storage; s; s = s->next)
      set_storage(ctx->parent, acc->address, s->key, s->value, &created, &created);
  }

  // Transfer logs to parent if parent is capturing events
  if (ctx->parent->capture_events && ctx->logs) {
    emitted_log_t* log = ctx->logs;
    while (log) {
      emitted_log_t* next = log->next;
      log->next           = ctx->parent->logs;
      ctx->parent->logs   = log;
      log                 = next;
    }
    ctx->logs = NULL; // Prevent double-free
  }
}

// Shared simulation result builder for ETH and OP Stack
ssz_ob_t eth_build_simulation_result_ssz(bytes_t call_result, emitted_log_t* logs, bool success, uint64_t gas_used, ssz_ob_t* execution_payload);

/**
 * Runs an EVM call with optional event capture and gas metering.
 *
 * Reads `evm->accounts`, `evm->call_codes`, and `evm->overrides` as inputs.
 * Writes results to `evm->call_result`, `evm->logs` (when `capture_events`),
 * and `evm->gas_used`. The transaction is read from `ctx->args[0]`.
 *
 * @param ctx the verification context
 * @param evm call context with inputs populated, outputs written on return
 * @param capture_events whether to capture emitted events
 * @return C4_SUCCESS, C4_ERROR, or C4_PENDING
 */
c4_status_t eth_run_call_evmone_with_events(verify_ctx_t* ctx, evm_call_ctx_t* evm, bool capture_events);

#ifdef __cplusplus
}
#endif

#endif /* CALL_CTX_H */