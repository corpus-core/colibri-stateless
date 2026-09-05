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

#include "call_ctx.h"
#include "../precompiles/precompiles.h"
#include "el_header.h"
#include "eth_account.h"
#include "eth_verify.h"
#include "plugin.h"
#include "state.h"
#include "state_overrides.h"
#include <stdlib.h>
#include <string.h>

#define JSON_ETH_PROOF_FIELDS "{accountProof:[bytes],storageProof:[{key:hex32,value:hexuint,proof:[bytes]}],balance:hexuint,codeHash:bytes32,nonce:hexuint,storageHash:bytes32}"

// :: Account lookup helpers (traverse parent chain)

static bool eth_get_call_block_context_from_header_data(bytes_t el_header, eth_call_block_context_t* out) {
  if (!el_header.data) return false;

  out->block_number    = eth_el_header_get_uint64(el_header, EL_BLOCK_NUMBER);
  out->timestamp       = eth_el_header_get_uint64(el_header, EL_TIMESTAMP);
  out->gas_limit       = eth_el_header_get_uint64(el_header, EL_GAS_LIMIT);
  out->excess_blob_gas = eth_el_header_get_uint64(el_header, EL_EXCESS_BLOB_GAS);

  bytes_t coinbase = eth_el_header_get(el_header, EL_FEE_RECIPIENT);
  if (coinbase.data && coinbase.len >= 20) memcpy(out->coinbase, coinbase.data, 20);
  memcpy(out->prev_randao, eth_el_header_get(el_header, EL_PREV_RANDAO).data, 32); // not available in ETH_BLOCK_HEADER_DATA
  bytes_t base_fee = eth_el_header_get(el_header, EL_BASE_FEE_PER_GAS);
  if (base_fee.data && base_fee.len >= 32) memcpy(out->base_fee_per_gas, base_fee.data, 32);
  keccak(el_header, out->block_hash);

  return true;
}

bool eth_get_call_block_context_from_proof(verify_ctx_t* ctx, eth_call_block_context_t* out) {
  evm_call_ctx_t* call_ctx = (evm_call_ctx_t*) ctx->user_data;
  if (!call_ctx) return false;
  if (!call_ctx->el_header.data) {
    if (ctx->proof.def && ctx->proof.def->type != SSZ_TYPE_NONE) {
      if (c4_verify_block(ctx, ssz_get(&ctx->proof, "block"), &call_ctx->el_header, &call_ctx->el_block_hash) != C4_SUCCESS) return false;
    }
    else
      return false;
  }
  return eth_get_call_block_context_from_header_data(call_ctx->el_header, out);
}

call_account_t* call_account_find(evmone_context_t* ctx, const address_t address) {
  for (call_account_t* acc = ctx->accounts; acc; acc = acc->next)
    if (memcmp(acc->address, address, 20) == 0) return acc;
  if (ctx->parent) return call_account_find(ctx->parent, address);
  return NULL;
}

call_account_t* call_account_get_or_create(evmone_context_t* ctx, const address_t address) {
  for (call_account_t* acc = ctx->accounts; acc; acc = acc->next)
    if (memcmp(acc->address, address, 20) == 0) return acc;

  call_account_t* parent_acc = ctx->parent ? call_account_find(ctx->parent, address) : NULL;
  call_account_t* acc        = safe_calloc(1, sizeof(call_account_t));
  memcpy(acc->address, address, 20);

  if (parent_acc) {
    acc->nonce = parent_acc->nonce;
    memcpy(acc->balance, parent_acc->balance, 32);
    // Preserve the "started at src_*, currently at balance/nonce" invariant
    // established by call_account_reset_accessed on the root context. Without
    // this, an account first materialised inside a nested frame would show up
    // in the SSZ stateChanges with previousValue = 0, which lies about the
    // pre-simulation balance whenever the ancestor account had funds.
    memcpy(acc->src_balance, parent_acc->src_balance, 32);
    acc->src_nonce = parent_acc->src_nonce;
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

// :: PAP-mode lazy fetchers

void call_account_lazy_fetch_storage(evmone_context_t* ctx, const address_t address, const bytes32_t key, bytes32_t result) {
#ifdef ETH_OBLIVIOUS
  bool is_oblivious = (ctx->ctx->flags & VERIFY_FLAG_OBLIVIOUS) != 0;
#else
  // Without oblivious support the flag is meaningless; pinning to false lets the
  // compiler dead-strip the oblivious branches below, so the linker can drop
  // the adaptive retry-delay learner together with `eth_oblivious_*` helpers.
  const bool is_oblivious = false;
#endif
  char      tmp[256];
  buffer_t  buf    = stack_buffer(tmp);
  bytes32_t req_id = {0};
  if (is_oblivious)
    bprintf(&buf, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_getProof\",\"params\":[\"0x%x\",[\"0x%x\"],\"latest\"]}", bytes((uint8_t*) address, 20), bytes((uint8_t*) key, 32));
  else
    bprintf(&buf, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_getStorageAt\",\"params\":[\"0x%x\",\"0x%x\",\"latest\"]}", bytes((uint8_t*) address, 20), bytes((uint8_t*) key, 32));
  keccak(buf.data, req_id);

  data_request_t* req = c4_state_get_data_request_by_id(&ctx->ctx->state, req_id);
  if (req && req->response.data) {
    json_t    response = json_parse((char*) req->response.data);
    bytes32_t val      = {0};
    if (is_oblivious) {
#ifdef ETH_OBLIVIOUS
      // The oblivious node may report the requested state as not yet available
      // (TEE/ORAM warm-up). Retry the SAME node after a short delay, bounded by
      // the retry budget, instead of treating the missing `result` as zero.
      if (eth_is_oblivious_unavailable(response)) {
        if (c4_state_retry_after(req, eth_oblivious_retry_delay(req->chain_id, req->retry_count), ETH_OBLIVIOUS_MAX_RETRIES))
          ctx->storage_miss = true;
        else
          c4_state_add_error(&ctx->ctx->state, "oblivious node did not provide the proof within the retry budget");
        return;
      }
      // Any other JSON-RPC error must surface as a failure rather than being
      // silently interpreted as an empty storage slot.
      json_t err = json_get(response, "error");
      if (err.type == JSON_TYPE_OBJECT || err.type == JSON_TYPE_STRING) {
        c4_state_add_error(&ctx->ctx->state, "oblivious eth_getProof returned an error");
        return;
      }
      json_t proof_result = json_get(response, "result");
      if (!req->validated) {
        if (c4_check_json_inline(&ctx->ctx->state, proof_result, JSON_ETH_PROOF_FIELDS, "Invalid results for eth_getProof: ") != C4_SUCCESS)
          return;
        req->validated = true;
      }
      json_t proof_item = json_get(json_at(json_get(proof_result, "storageProof"), 0), "value");
      if (proof_item.type == JSON_TYPE_STRING) {
        buffer_t val_buf = stack_buffer(val);
        bytes_t  b       = json_as_bytes(proof_item, &val_buf);
        if (b.len <= 32) memcpy(val + (32 - b.len), b.data, b.len);
      }
      // Feed the adaptive learner. The oblivious flag confirms the request
      // went through the oblivious code path, so even retry_count == 0 is
      // meaningful (enables the slow downward probe).
      eth_oblivious_retry_observe(req->chain_id, req->retry_count);
#endif // ETH_OBLIVIOUS
    }
    else {
      json_t val_json = json_get(response, "result");
      if (!req->validated) {
        if (c4_check_json_inline(&ctx->ctx->state, val_json, "hex32", "Invalid results for eth_getStorageAt: ") != C4_SUCCESS)
          return;
        req->validated = true;
      }
      if (val_json.type == JSON_TYPE_STRING) {
        buffer_t val_buf = stack_buffer(val);
        bytes_t  b       = json_as_bytes(val_json, &val_buf);
        if (b.len <= 32) memcpy(val + (32 - b.len), b.data, b.len);
      }
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
#ifdef ETH_OBLIVIOUS
    // A transport-level error against the oblivious node may also be transient
    // (node still warming up). Retry the same node with delay before failing.
    if (is_oblivious && c4_state_retry_after(req, eth_oblivious_retry_delay(req->chain_id, req->retry_count), ETH_OBLIVIOUS_MAX_RETRIES)) {
      ctx->storage_miss = true;
      return;
    }
#endif
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

bytes_t call_account_get_code(evmone_context_t* ctx, const address_t address) {
  if (bytes_all_zero(bytes(address, 20)) || ctx->storage_miss || eth_is_precompile_address(address)) return NULL_BYTES;
  call_account_t* acc = call_account_find(ctx, address);
  if (!acc && ctx->pap_mode) {
    acc = eth_call_account_cache_load(ctx->ctx, address);
    if (acc) {
      acc->next     = ctx->accounts;
      ctx->accounts = acc;
    }
  }

  if (!acc) {
    if (ctx->pap_mode) {
      acc               = call_account_get_or_create(ctx, address);
      ctx->storage_miss = true;
      return eth_fetch_account_code(ctx->ctx, acc) == C4_SUCCESS ? acc->code : NULL_BYTES;
    }

    if (!ctx->ctx->state.error) {
      char _tmp[100];
      sbprintf(_tmp, "Missing account proof for 0x%x", bytes(address, 20));
      c4_state_add_error(&ctx->ctx->state, _tmp);
    }
    return NULL_BYTES;
  }
  if (acc->flags & ACCOUNT_HAS_CODE) return acc->code;
  if (acc->flags & ACCOUNT_HAS_CODE_HASH) {
    if (memcmp(acc->code_hash, EMPTY_HASH, 32) == 0) {
      acc->code = NULL_BYTES;
      acc->flags |= ACCOUNT_HAS_CODE;
      return acc->code;
    }
    else {
      storage_plugin_t cache = {0};
      c4_get_storage_config(&cache);
      char tmp[80];
      sbprintf(tmp, "code_%x", bytes(acc->code_hash, 32));
      buffer_t data = {0};
      if (cache.get && cache.get(tmp, &data)) {
        acc->code = data.data;
        acc->flags |= ACCOUNT_HAS_CODE | ACCOUNT_FREE_CODE;
        return acc->code;
      }
    }
  }
  if (ctx->pap_mode && !(acc->flags & ACCOUNT_HAS_CODE_HASH))
    return eth_fetch_account_code(ctx->ctx, acc) == C4_SUCCESS ? acc->code : NULL_BYTES;
  return NULL_BYTES;
}

// :: Block hash lookup

c4_status_t call_fetch_block_hash(evmone_context_t* ctx, int64_t number, bytes32_t result) {
  // TODO: implement block hash cache lookup / lazy RPC fetch
  // For now, return zero hash (BLOCKHASH opcode returns 0 for unknown blocks)
  (void) ctx;
  (void) number;
  memset(result, 0, 32);
  return C4_SUCCESS;
}

// :: State overrides

c4_status_t call_apply_state_overrides(verify_ctx_t* ctx, call_account_t** accounts, json_t overrides_json) {
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
      if (acc->code.data && (acc->flags & ACCOUNT_FREE_CODE))
        safe_free(acc->code.data);
      acc->code = bytes_dup(a->code);
      acc->flags |= ACCOUNT_HAS_CODE | ACCOUNT_FREE_CODE;
    }
    if (a->storage) {
      if (a->full_state) acc->flags |= ACCOUNT_FULL_STATE;
      for (const eth_storage_override_t* s = a->storage; s; s = s->next) {
        call_storage_t* cs = call_storage_find(acc, s->key);
        if (cs) {
          memcpy(cs->src_value, s->value, 32);
          memcpy(cs->post_value, s->value, 32);
          cs->verified_at = 1;
          cs->source      = STORAGE_SRC_OVERRIDE;
        }
        else {
          cs = safe_calloc(1, sizeof(call_storage_t));
          memcpy(cs->key, s->key, 32);
          memcpy(cs->src_value, s->value, 32);
          memcpy(cs->post_value, s->value, 32);
          cs->source      = STORAGE_SRC_OVERRIDE;
          cs->verified_at = 1;
          cs->next        = acc->storage;
          acc->storage    = cs;
        }
      }
    }
  }
  eth_state_overrides_free(&overrides);
  return C4_SUCCESS;
}

// :: Emitted log helpers

void free_keccak_entries(keccak_entry_t* entries) {
  while (entries) {
    keccak_entry_t* next = entries->next;
    safe_free(entries->input.data);
    safe_free(entries);
    entries = next;
  }
}

void free_trace_entries(trace_entry_t* entries) {
  while (entries) {
    trace_entry_t* next = entries->next;
    safe_free(entries->input.data);
    safe_free(entries->output.data);
    safe_free(entries->trace_address);
    safe_free(entries);
    entries = next;
  }
}

void free_emitted_logs(emitted_log_t* logs) {
  while (logs) {
    emitted_log_t* next = logs->next;
    if (logs->data.data) safe_free(logs->data.data);
    if (logs->topics) safe_free(logs->topics);
    safe_free(logs);
    logs = next;
  }
}

emitted_log_t* add_emitted_log(emitted_log_t** logs, const address_t addr, const uint8_t* data, size_t data_size, const bytes32_t* topics, size_t topics_count) {
  emitted_log_t* log = safe_calloc(1, sizeof(emitted_log_t));
  memcpy(log->address, addr, 20);

  if (data && data_size > 0) {
    log->data.data = safe_malloc(data_size);
    memcpy(log->data.data, data, data_size);
    log->data.len = data_size;
  }

  if (topics && topics_count > 0) {
    log->topics       = safe_calloc(topics_count, sizeof(bytes32_t));
    log->topics_count = topics_count;
    for (size_t i = 0; i < topics_count; i++)
      memcpy(log->topics[i], topics[i], 32);
  }

  log->next = *logs;
  *logs     = log;

  return log;
}

// EIP-7708: keccak256("Transfer(address,address,uint256)")
static const uint8_t EIP7708_TRANSFER_TOPIC[32] = {
    0xdd, 0xf2, 0x52, 0xad, 0x1b, 0xe2, 0xc8, 0x9b,
    0x69, 0xc2, 0xb0, 0x68, 0xfc, 0x37, 0x8d, 0xaa,
    0x95, 0x2b, 0xa7, 0xf1, 0x63, 0xc4, 0xa1, 0x16,
    0x28, 0xf5, 0x5a, 0x4d, 0xf5, 0x23, 0xb3, 0xef};

// EIP-7708 / EIP-4788 SYSTEM_ADDRESS: 0xfffffffffffffffffffffffffffffffffffffffe
static const address_t EIP7708_SYSTEM_ADDRESS = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe};

void emit_eth_transfer_log(emitted_log_t** logs, const address_t from, const address_t to, const uint8_t value[32]) {
  // EIP-7708: no log for zero-value transfers or self-transfers.
  if (!value || bytes_all_zero(bytes((uint8_t*) value, 32))) return;
  if (memcmp(from, to, 20) == 0) return;

  bytes32_t topics[3] = {0};
  memcpy(topics[0], EIP7708_TRANSFER_TOPIC, 32);
  memcpy(topics[1] + 12, from, 20);
  memcpy(topics[2] + 12, to, 20);
  add_emitted_log(logs, EIP7708_SYSTEM_ADDRESS, value, 32, (const bytes32_t*) topics, 3);
}

// :: Child-context management

static void free_transient_storage(transient_slot_t* slots) {
  while (slots) {
    transient_slot_t* next = slots->next;
    safe_free(slots);
    slots = next;
  }
}

void context_free(evmone_context_t* ctx) {
  call_account_free_list(ctx->accounts);
  ctx->accounts = NULL;
  free_emitted_logs(ctx->logs);
  ctx->logs = NULL;
  free_trace_entries(ctx->traces);
  ctx->traces = NULL;
  if (!ctx->parent) {
    free_transient_storage(ctx->transient_storage);
    ctx->transient_storage = NULL;
  }
}

void context_apply(evmone_context_t* ctx) {
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
    // Splice child logs at the head of the parent list while preserving the
    // child's LIFO order (newest at head). Combined with the final reversal in
    // match_simulate_result, this yields chronological output across nesting.
    emitted_log_t* tail = ctx->logs;
    while (tail->next) tail = tail->next;
    tail->next        = ctx->parent->logs;
    ctx->parent->logs = ctx->logs;
    ctx->logs         = NULL;
  }
}

// :: Context initialization

void init_evmone_context(evmone_context_t* out, verify_ctx_t* ctx, evm_call_ctx_t* evm, void* executor, bool capture_events) {
  memset(out, 0, sizeof(*out));
  out->executor        = executor;
  out->ctx             = ctx;
  out->accounts        = evm->accounts;
  out->chain_id        = ctx->chain_id;
  out->block_gas_limit = 300000000; // max limit
  out->capture_events  = capture_events;
  out->pap_mode        = evm->pap_mode;

  // extract EVM block context from the verified RLP EL header (via ETH_BLOCK_PROOF_UNION)
  eth_call_block_context_t bctx = {0};
  if (eth_get_call_block_context_from_proof(ctx, &bctx)) {
    out->block_number    = bctx.block_number;
    out->timestamp       = bctx.timestamp;
    out->block_gas_limit = bctx.gas_limit;
    memcpy(out->block_coinbase, bctx.coinbase, 20);
    memcpy(out->block_prev_randao, bctx.prev_randao, 32);
    memcpy(out->block_base_fee, bctx.base_fee_per_gas, 32);
    memcpy(out->block_hash, bctx.block_hash, 32);
    // blob_base_fee can be derived from excess_blob_gas (EIP-4844); for now leave zero
    (void) bctx.excess_blob_gas;
  }
}

// :: EVM call context lifecycle

void evm_call_ctx_free(evm_call_ctx_t* evm) {
  safe_free(evm->call_result.data);
  evm->call_result = NULL_BYTES;
  free_emitted_logs(evm->logs);
  evm->logs = NULL;
  free_keccak_entries(evm->keccak_entries);
  evm->keccak_entries = NULL;
  free_trace_entries(evm->traces);
  evm->traces = NULL;
  call_account_free_list(evm->accounts);
  evm->accounts = NULL;
}
