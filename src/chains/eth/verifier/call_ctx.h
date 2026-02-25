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
#include "eth_verify.h"
#include "json.h"
#include "plugin.h"
#include "ssz.h"
#include "state.h"
#include "state_overrides.h"
#include "verify.h"
#include <stdlib.h>
#include <string.h>

// :: Storage value source

typedef enum {
  STORAGE_SRC_NONE     = 0,
  STORAGE_SRC_PROOF    = 1,
  STORAGE_SRC_CACHE    = 2,
  STORAGE_SRC_OVERRIDE = 3,
  STORAGE_SRC_RPC      = 4,
} storage_source_t;

// :: Unified storage slot

typedef struct call_storage {
  bytes32_t            key;
  bytes32_t            src_value;
  bytes32_t            post_value;
  storage_source_t     source;
  uint64_t             verified_at;
  bool                 accessed;
  bool                 modified;
  struct call_storage* next;
} call_storage_t;

// :: Account flags

typedef enum {
  ACCOUNT_HAS_NONCE        = 1 << 0,
  ACCOUNT_HAS_BALANCE      = 1 << 1,
  ACCOUNT_HAS_CODE_HASH    = 1 << 2,
  ACCOUNT_HAS_STORAGE_ROOT = 1 << 3,
  ACCOUNT_HAS_CODE         = 1 << 4,
  ACCOUNT_FREE_CODE        = 1 << 5,
  ACCOUNT_FULL_STATE       = 1 << 6,
  ACCOUNT_DELETED          = 1 << 7,
} call_account_flags_t;

// :: Unified account

typedef struct call_account {
  address_t            address;
  uint64_t             nonce;
  bytes32_t            balance;
  bytes32_t            code_hash;
  bytes32_t            storage_root;
  bytes_t              code;
  uint32_t             flags;
  uint64_t             verified_at;
  call_storage_t*      storage;
  struct call_account* next;
} call_account_t;

#ifdef EVMONE
#include "evmone_c_wrapper.h"
#endif

typedef struct emitted_log {
  address_t           address;
  bytes_t             data;
  bytes32_t*          topics;
  size_t              topics_count;
  struct emitted_log* next;
} emitted_log_t;

/**
 * Shared context for EVM call verification (`eth_call`, `eth_estimateGas`, `colibri_simulateTransaction`).
 *
 * Holds all inputs, intermediate state, and outputs for the EVM execution.
 * In PAP mode this struct is heap-allocated and attached to `verify_ctx_t.user_data`
 * so it survives across multiple `C4_PENDING` rounds. For non-PAP paths (e.g. OP-Stack)
 * it may be stack-allocated with a single-pass lifetime.
 */
typedef struct evm_call_ctx {
  call_account_t* accounts;
  bytes_t         call_result;
  emitted_log_t*  logs;
  uint64_t        gas_used;
  bytes32_t       state_root;
  bool            pap_mode;
  bool            evm_done;
} evm_call_ctx_t;

void evm_call_ctx_free(evm_call_ctx_t* evm);

typedef struct evmone_context {
  void*                  executor;
  verify_ctx_t*          ctx;
  call_account_t*        accounts;
  uint64_t               block_number;
  bytes32_t              block_hash;
  uint64_t               timestamp;
  bytes32_t              tx_origin;
  uint64_t               gas_price;
  struct evmone_context* parent;
  void*                  results;
  emitted_log_t*         logs;
  bool                   capture_events;
  bool                   pap_mode;
  bool                   storage_miss;
} evmone_context_t;

// :: Account / storage helpers

static call_account_t* call_account_list_get_or_create(call_account_t** list, const address_t address) {
  for (call_account_t* acc = *list; acc; acc = acc->next)
    if (memcmp(acc->address, address, 20) == 0) return acc;
  call_account_t* acc = safe_calloc(1, sizeof(call_account_t));
  memcpy(acc->address, address, 20);
  acc->next = *list;
  *list     = acc;
  return acc;
}

static call_account_t* call_account_find(evmone_context_t* ctx, const address_t address) {
  for (call_account_t* acc = ctx->accounts; acc; acc = acc->next)
    if (memcmp(acc->address, address, 20) == 0) return acc;
  if (ctx->parent) return call_account_find(ctx->parent, address);
  return NULL;
}

static call_storage_t* call_storage_find(call_account_t* acc, const bytes32_t key) {
  if (!acc) return NULL;
  for (call_storage_t* s = acc->storage; s; s = s->next)
    if (memcmp(s->key, key, 32) == 0) return s;
  return NULL;
}

static call_account_t* call_account_get_or_create(evmone_context_t* ctx, const address_t address) {
  for (call_account_t* acc = ctx->accounts; acc; acc = acc->next)
    if (memcmp(acc->address, address, 20) == 0) return acc;

  call_account_t* parent_acc = ctx->parent ? call_account_find(ctx->parent, address) : NULL;
  call_account_t* acc        = safe_calloc(1, sizeof(call_account_t));
  memcpy(acc->address, address, 20);

  if (parent_acc) {
    acc->nonce = parent_acc->nonce;
    memcpy(acc->balance, parent_acc->balance, 32);
    memcpy(acc->code_hash, parent_acc->code_hash, 32);
    memcpy(acc->storage_root, parent_acc->storage_root, 32);
    acc->code        = parent_acc->code;
    acc->flags       = parent_acc->flags & ~ACCOUNT_FREE_CODE;
    acc->verified_at = parent_acc->verified_at;

    call_storage_t** sp = &acc->storage;
    for (call_storage_t* s = parent_acc->storage; s; s = s->next) {
      call_storage_t* ns = safe_calloc(1, sizeof(call_storage_t));
      *ns                = *s;
      ns->next           = NULL;
      *sp                = ns;
      sp                 = &ns->next;
    }
  }

  acc->next     = ctx->accounts;
  ctx->accounts = acc;
  return acc;
}

static void call_account_lazy_fetch_storage(evmone_context_t* ctx, const address_t address, const bytes32_t key, bytes32_t result) {
  char      tmp[256];
  buffer_t  buf    = stack_buffer(tmp);
  bytes32_t req_id = {0};
  bprintf(&buf, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_getStorageAt\",\"params\":[\"0x%x\",\"0x%x\",\"latest\"]}", bytes((uint8_t*) address, 20), bytes((uint8_t*) key, 32));
  keccak(buf.data, req_id);

  data_request_t* req = c4_state_get_data_request_by_id(&ctx->ctx->state, req_id);
  if (req && req->response.data) {
    json_t    val_json = json_get(json_parse((char*) req->response.data), "result");
    bytes32_t val      = {0};
    if (val_json.type == JSON_TYPE_STRING) {
      buffer_t val_buf = stack_buffer(val);
      bytes_t  b       = json_as_bytes(val_json, &val_buf);
      if (b.len <= 32) memcpy(val + (32 - b.len), b.data, b.len);
    }
    memcpy(result, val, 32);

    call_account_t* acc = call_account_get_or_create(ctx, address);
    call_storage_t* s   = safe_calloc(1, sizeof(call_storage_t));
    memcpy(s->key, key, 32);
    memcpy(s->src_value, val, 32);
    memcpy(s->post_value, val, 32);
    s->source    = STORAGE_SRC_RPC;
    s->accessed  = true;
    s->next      = acc->storage;
    acc->storage = s;
    return;
  }
  else if (req && req->error) {
    c4_state_add_error(&ctx->ctx->state, req->error);
    return;
  }

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

static bytes_t call_account_get_code(evmone_context_t* ctx, const address_t address) {
  if (bytes_all_zero(bytes(address, 20)) || ctx->storage_miss) return NULL_BYTES;
  call_account_t* acc = call_account_find(ctx, address);
  if (!acc) {
    if (ctx->pap_mode) {
      acc               = call_account_get_or_create(ctx, address);
      ctx->storage_miss = true;
      return eth_fetch_account_code(ctx->ctx, acc) == C4_SUCCESS ? acc->code : NULL_BYTES;
    }

    if (!ctx->ctx->state.error) {
      char _tmp[64];
      sbprintf(_tmp, "Missing account proof for 0x%x", bytes(address, 20));
      c4_state_add_error(&ctx->ctx->state, _tmp);
    }
    return NULL_BYTES;
  }
  if (acc->flags & ACCOUNT_HAS_CODE) return acc->code;
  if (ctx->pap_mode && !(acc->flags & ACCOUNT_HAS_CODE_HASH))
    return eth_fetch_account_code(ctx->ctx, acc) == C4_SUCCESS ? acc->code : NULL_BYTES;
  return NULL_BYTES;
}

// :: State overrides

static c4_status_t call_apply_state_overrides(verify_ctx_t* ctx, call_account_t** accounts, json_t overrides_json) {
  if (overrides_json.type != JSON_TYPE_OBJECT) return C4_SUCCESS;
  eth_state_overrides_t overrides = {0};
  if (eth_parse_state_overrides(ctx, overrides_json, &overrides) != C4_SUCCESS) {
    eth_state_overrides_free(&overrides);
    return C4_ERROR;
  }
  for (const eth_account_override_t* a = overrides.accounts; a; a = a->next) {
    call_account_t* acc = call_account_list_get_or_create(accounts, a->address);
    if (a->has_balance) {
      memcpy(acc->balance, a->balance, 32);
      acc->flags |= ACCOUNT_HAS_BALANCE;
    }
    if (a->has_code) {
      acc->code = a->code;
      acc->flags |= ACCOUNT_HAS_CODE;
      acc->flags &= ~ACCOUNT_FREE_CODE;
    }
    if (a->storage) {
      if (a->full_state) acc->flags |= ACCOUNT_FULL_STATE;
      for (const eth_storage_override_t* s = a->storage; s; s = s->next) {
        call_storage_t* cs = call_storage_find(acc, s->key);
        if (cs) {
          memcpy(cs->src_value, s->value, 32);
          memcpy(cs->post_value, s->value, 32);
          cs->source = STORAGE_SRC_OVERRIDE;
        }
        else {
          cs = safe_calloc(1, sizeof(call_storage_t));
          memcpy(cs->key, s->key, 32);
          memcpy(cs->src_value, s->value, 32);
          memcpy(cs->post_value, s->value, 32);
          cs->source   = STORAGE_SRC_OVERRIDE;
          cs->next     = acc->storage;
          acc->storage = cs;
        }
      }
    }
  }
  eth_state_overrides_free(&overrides);
  return C4_SUCCESS;
}

// :: Memory management

static void call_storage_free_list(call_storage_t* s) {
  while (s) {
    call_storage_t* next = s->next;
    safe_free(s);
    s = next;
  }
}

static void call_account_free(call_account_t* acc) {
  call_storage_free_list(acc->storage);
  if (acc->flags & ACCOUNT_FREE_CODE) safe_free(acc->code.data);
  safe_free(acc);
}

static void call_account_free_list(call_account_t* list) {
  while (list) {
    call_account_t* next = list->next;
    call_account_free(list);
    list = next;
  }
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
  call_account_free_list(ctx->accounts);
  ctx->accounts = NULL;
  free_emitted_logs(ctx->logs);
  ctx->logs = NULL;
}

#ifdef EVMONE
static emitted_log_t* add_emitted_log(evmone_context_t* ctx, const evmc_address* addr, const uint8_t* data, size_t data_size, const evmc_bytes32 topics[], size_t topics_count) {
  if (!ctx->capture_events) return NULL;

  emitted_log_t* log = safe_calloc(1, sizeof(emitted_log_t));
  memcpy(log->address, addr->bytes, 20);

  if (data && data_size > 0) {
    log->data.data = safe_malloc(data_size);
    memcpy(log->data.data, data, data_size);
    log->data.len = data_size;
  }

  if (topics && topics_count > 0) {
    log->topics       = safe_calloc(topics_count, sizeof(bytes32_t));
    log->topics_count = topics_count;
    for (size_t i = 0; i < topics_count; i++)
      memcpy(log->topics[i], topics[i].bytes, 32);
  }

  log->next = ctx->logs;
  ctx->logs = log;

  return log;
}
#endif // EVMONE

static void context_apply(evmone_context_t* ctx) {
  if (!ctx->parent) return;

  for (call_account_t* acc = ctx->accounts; acc; acc = acc->next) {
    call_account_t* parent_acc = call_account_get_or_create(ctx->parent, acc->address);
    memcpy(parent_acc->balance, acc->balance, 32);
    parent_acc->nonce = acc->nonce;
    parent_acc->code  = acc->code;
    parent_acc->flags = (parent_acc->flags & ACCOUNT_FREE_CODE) | (acc->flags & ~ACCOUNT_FREE_CODE);

    for (call_storage_t* s = acc->storage; s; s = s->next) {
      call_storage_t* ps = call_storage_find(parent_acc, s->key);
      if (ps) {
        memcpy(ps->post_value, s->post_value, 32);
        ps->modified = memcmp(ps->src_value, ps->post_value, 32) != 0;
        ps->accessed |= s->accessed;
      }
      else {
        call_storage_t* ns  = safe_calloc(1, sizeof(call_storage_t));
        *ns                 = *s;
        ns->next            = parent_acc->storage;
        parent_acc->storage = ns;
      }
    }
  }

  if (ctx->parent->capture_events && ctx->logs) {
    emitted_log_t* log = ctx->logs;
    while (log) {
      emitted_log_t* next = log->next;
      log->next           = ctx->parent->logs;
      ctx->parent->logs   = log;
      log                 = next;
    }
    ctx->logs = NULL;
  }
}

// Shared simulation result builder for ETH and OP Stack
ssz_ob_t eth_build_simulation_result_ssz(bytes_t call_result, emitted_log_t* logs, bool success, uint64_t gas_used, ssz_ob_t* execution_payload);

/**
 * Runs an EVM call with optional event capture and gas metering.
 *
 * Reads `evm->accounts` as input (overrides must already be applied).
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
