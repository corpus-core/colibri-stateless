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
#include "eth_account.h"
#include "ssz.h"
#include "verify.h"
#include <stdlib.h>
#include <string.h>

#ifdef EVMONE
#include "evmone_c_wrapper.h" // For evmc_address and evmc_bytes32
#endif

typedef struct changed_storage {
  bytes32_t               key;
  bytes32_t               value;
  struct changed_storage* next;

} changed_storage_t;

typedef struct changed_account {
  address_t               address;
  bytes32_t               balance;
  bytes_t                 code;
  struct changed_account* next;
  changed_storage_t*      storage;
  bool                    deleted;
  bool                    free_code;
} changed_account_t;

// Context for EVM execution
// Structure to store emitted log events
typedef struct emitted_log {
  address_t           address;      // Contract address that emitted the log
  bytes_t             data;         // Log data
  bytes32_t*          topics;       // Array of topics
  size_t              topics_count; // Number of topics
  struct emitted_log* next;         // Linked list pointer
} emitted_log_t;

typedef struct evmone_context {
  void*              executor;
  verify_ctx_t*      ctx;
  ssz_ob_t           src_accounts;
  changed_account_t* changed_accounts;
  call_code_t*       call_codes;
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
} evmone_context_t;

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
  if (!ctx->ctx->state.error && !allow_missing) ctx->ctx->state.error = bprintf(NULL, "Missing account proof for 0x%x", bytes(address, 20));

  return (ssz_ob_t) {0};
}

static void get_src_storage(evmone_context_t* ctx, const address_t address, const bytes32_t key, bytes32_t result) {
  ssz_ob_t account = get_src_account(ctx, address, false);
  if (!account.def) return;
  ssz_ob_t storage = ssz_get(&account, "storageProof");
  uint32_t len     = ssz_len(storage);
  for (int i = 0; i < len; i++) {
    ssz_ob_t entry = ssz_at(storage, i);
    if (memcmp(ssz_get(&entry, "key").bytes.data, key, 32) == 0) {
      if (!eth_get_storage_value(entry, result)) memset(result, 0, 32);
      return;
    }
  }
  if (!ctx->ctx->state.error) ctx->ctx->state.error = bprintf(NULL, "Missing account proof for account 0x%x and storage key 0x%x", bytes(address, 20), bytes(key, 32));
}

static changed_account_t* get_changed_account(evmone_context_t* ctx, const address_t address) {
  for (changed_account_t* acc = ctx->changed_accounts; acc != NULL; acc = acc->next) {
    if (memcmp(acc->address, address, 20) == 0)
      return acc;
  }
  if (ctx->parent)
    return get_changed_account(ctx->parent, address);
  return NULL;
}

static changed_storage_t* get_changed_storage(evmone_context_t* ctx, const address_t addr, const bytes32_t key) {
  changed_account_t* account = get_changed_account(ctx, addr);
  if (!account) return NULL;
  for (changed_storage_t* s = account->storage; s != NULL; s = s->next) {
    if (memcmp(s->key, key, 32) == 0)
      return s;
  }
  return NULL;
}

static changed_account_t* create_changed_account(evmone_context_t* ctx, const address_t address, bool* created) {
  *created = false;
  for (changed_account_t* acc = ctx->changed_accounts; acc != NULL; acc = acc->next) {
    if (memcmp(acc->address, address, 20) == 0)
      return acc;
  }
  changed_account_t* parent_acc  = ctx->parent ? get_changed_account(ctx->parent, address) : NULL;
  *created                       = parent_acc == NULL;
  ssz_ob_t           old_account = get_src_account(ctx, address, true);
  changed_account_t* acc         = safe_calloc(1, sizeof(changed_account_t));
  memcpy(acc->address, address, 20);
  acc->next             = ctx->changed_accounts;
  ctx->changed_accounts = acc;

  if (parent_acc) {
    memcpy(acc->balance, parent_acc->balance, 32);
    acc->code                       = parent_acc->code;
    changed_storage_t** storage_ptr = &acc->storage;
    for (changed_storage_t* s = parent_acc->storage; s != NULL; s = s->next) {
      *storage_ptr = safe_calloc(1, sizeof(changed_storage_t));
      memcpy((*storage_ptr)->key, s->key, 32);
      memcpy((*storage_ptr)->value, s->value, 32);
      (*storage_ptr)->next = NULL;
      storage_ptr          = &(*storage_ptr)->next;
    }
  }
  else if (old_account.def) {
    ssz_ob_t code = ssz_get(&old_account, "code");
    if (code.def && code.def->type == SSZ_TYPE_LIST && code.bytes.len > 0) acc->code = code.bytes;
    eth_get_account_value(old_account, ETH_ACCOUNT_BALANCE, acc->balance);
  }
  return acc;
}

static void set_changed_storage(evmone_context_t* ctx, const address_t addr, const bytes32_t key, const bytes32_t value, bool* account_created, bool* storage_created) {
  changed_storage_t* storage = get_changed_storage(ctx, addr, key);
  if (storage) {
    memcpy(storage->value, value, 32);
    *account_created = false;
    *storage_created = false;
  }
  else {
    changed_account_t* account = create_changed_account(ctx, addr, account_created);
    *storage_created           = true;
    changed_storage_t* storage = safe_calloc(1, sizeof(changed_storage_t));
    memcpy(storage->key, key, 32);
    memcpy(storage->value, value, 32);
    storage->next    = account->storage;
    account->storage = storage;
  }
}
static bytes_t get_code(evmone_context_t* ctx, const address_t address) {
  changed_account_t* changed_account = get_changed_account(ctx, address);
  if (changed_account) return changed_account->code;
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

static void changed_account_free(changed_account_t* acc) {
  while (acc->storage) {
    changed_storage_t* storage = acc->storage;
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
  while (ctx->changed_accounts) {
    changed_account_t* next = ctx->changed_accounts->next;
    changed_account_free(ctx->changed_accounts);
    ctx->changed_accounts = next;
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
  for (changed_account_t* acc = ctx->changed_accounts; acc; acc = acc->next) {
    changed_account_t* parent_acc = create_changed_account(ctx->parent, acc->address, &created);
    memcpy(parent_acc->balance, acc->balance, 32);
    parent_acc->code      = acc->code;
    parent_acc->free_code = acc->free_code;

    for (changed_storage_t* s = acc->storage; s; s = s->next)
      set_changed_storage(ctx->parent, acc->address, s->key, s->value, &created, &created);
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

#ifdef __cplusplus
}
#endif

#endif /* CALL_CTX_H */