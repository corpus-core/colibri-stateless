/*
 * Copyright (c) 2026 corpus.core
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

/**
 * Node.js N-API addon exposing the same functional surface as the
 * Emscripten wrapper (`bindings/emscripten/ems.c`), but with JS-friendly
 * types (strings, Uint8Array, BigInt) instead of WASM heap pointers.
 *
 * The addon links the full native C library statically, so BLS pairing
 * operations run with the platform-optimized blst assembly instead of the
 * generic WASM build.
 *
 * All status-returning functions serialize with `req_ptr_as_string=true`
 * because native pointers are 64-bit and must not be parsed as JS numbers.
 */

#define NAPI_VERSION 8
#include <node_api.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "colibri_common.h"
#include "plugin.h"
#include "version.h"

/* ── helpers ─────────────────────────────────────────────────────────── */

#define NAPI_CALL(env, call)                                        \
  do {                                                              \
    if ((call) != napi_ok) {                                        \
      bool pending = false;                                         \
      napi_is_exception_pending((env), &pending);                   \
      if (!pending) napi_throw_error((env), NULL, "N-API failure"); \
      return NULL;                                                  \
    }                                                               \
  } while (0)

static napi_value throw_error(napi_env env, const char* msg) {
  napi_throw_error(env, NULL, msg);
  return NULL;
}

// Returns a malloc'd UTF-8 copy of the string argument, or NULL if the value
// is null/undefined. Sets *ok=false and throws on type errors.
static char* get_opt_string(napi_env env, napi_value value, bool* ok) {
  *ok = true;
  napi_valuetype type;
  if (napi_typeof(env, value, &type) != napi_ok) goto fail;
  if (type == napi_null || type == napi_undefined) return NULL;
  if (type != napi_string) goto fail;

  size_t len = 0;
  if (napi_get_value_string_utf8(env, value, NULL, 0, &len) != napi_ok) goto fail;
  char* str = safe_malloc(len + 1);
  if (napi_get_value_string_utf8(env, value, str, len + 1, &len) != napi_ok) {
    safe_free(str);
    goto fail;
  }
  return str;

fail:
  *ok = false;
  napi_throw_type_error(env, NULL, "expected a string argument");
  return NULL;
}

// Like get_opt_string, but the argument is required.
static char* get_string(napi_env env, napi_value value, bool* ok) {
  char* str = get_opt_string(env, value, ok);
  if (*ok && !str) {
    *ok = false;
    napi_throw_type_error(env, NULL, "expected a string argument");
  }
  return str;
}

// Accepts BigInt or Number and returns the value as uint64.
static bool get_u64(napi_env env, napi_value value, uint64_t* result) {
  napi_valuetype type;
  if (napi_typeof(env, value, &type) != napi_ok) return false;
  if (type == napi_bigint) {
    bool lossless = false;
    return napi_get_value_bigint_uint64(env, value, result, &lossless) == napi_ok;
  }
  if (type == napi_number) {
    double d = 0;
    if (napi_get_value_double(env, value, &d) != napi_ok || d < 0) return false;
    *result = (uint64_t) d;
    return true;
  }
  napi_throw_type_error(env, NULL, "expected a bigint or number argument");
  return false;
}

static bool get_u32(napi_env env, napi_value value, uint32_t* result) {
  return napi_get_value_uint32(env, value, result) == napi_ok;
}

static bool get_i32(napi_env env, napi_value value, int32_t* result) {
  return napi_get_value_int32(env, value, result) == napi_ok;
}

// Returns a view into the bytes of a Uint8Array/Buffer argument.
// The view is only valid while the argument is alive (within the call).
static bool get_bytes_view(napi_env env, napi_value value, bytes_t* result) {
  bool is_typedarray = false;
  if (napi_is_typedarray(env, value, &is_typedarray) != napi_ok || !is_typedarray) {
    napi_throw_type_error(env, NULL, "expected a Uint8Array argument");
    return false;
  }
  napi_typedarray_type type;
  size_t               length = 0;
  void*                data   = NULL;
  if (napi_get_typedarray_info(env, value, &type, &length, &data, NULL, NULL) != napi_ok || type != napi_uint8_array) {
    napi_throw_type_error(env, NULL, "expected a Uint8Array argument");
    return false;
  }
  *result = bytes(data, (uint32_t) length);
  return true;
}

static napi_value make_string(napi_env env, const char* str) {
  napi_value result;
  NAPI_CALL(env, napi_create_string_utf8(env, str, NAPI_AUTO_LENGTH, &result));
  return result;
}

// Creates a string from a malloc'd C string and frees it.
static napi_value make_owned_string(napi_env env, char* str) {
  if (!str) {
    napi_value result;
    NAPI_CALL(env, napi_get_null(env, &result));
    return result;
  }
  napi_value result = make_string(env, str);
  free(str);
  return result;
}

/* ── context handles ─────────────────────────────────────────────────── */

typedef enum {
  HANDLE_PROVER,
  HANDLE_VERIFY,
  HANDLE_RPC,
} handle_kind_t;

typedef struct {
  void*         ptr;
  handle_kind_t kind;
} ctx_handle_t;

// Mirrors c4w_verify_ctx_t in ems.c: owns a copy of the proof bytes.
typedef struct {
  bytes_t      proof;
  verify_ctx_t verify;
} addon_verify_ctx_t;

static void free_ctx(ctx_handle_t* handle) {
  if (!handle->ptr) return;
  switch (handle->kind) {
    case HANDLE_PROVER:
      c4_prover_free((prover_ctx_t*) handle->ptr);
      break;
    case HANDLE_VERIFY: {
      addon_verify_ctx_t* ctx = (addon_verify_ctx_t*) handle->ptr;
      if (ctx->verify.method) free((char*) ctx->verify.method);
      if (ctx->verify.args.len) free((char*) ctx->verify.args.start);
      if (ctx->proof.data) free(ctx->proof.data);
      c4_verify_free_data(&ctx->verify);
      free(ctx);
      break;
    }
    case HANDLE_RPC:
      c4_rpc_ctx_free((c4_rpc_ctx_t*) handle->ptr);
      break;
  }
  handle->ptr = NULL;
}

// GC finalizer: frees the context if the JS side forgot to call free_*_ctx.
static void handle_finalize(napi_env env, void* data, void* hint) {
  (void) env;
  (void) hint;
  ctx_handle_t* handle = (ctx_handle_t*) data;
  free_ctx(handle);
  safe_free(handle);
}

static napi_value wrap_handle(napi_env env, void* ptr, handle_kind_t kind) {
  ctx_handle_t* handle = safe_malloc(sizeof(ctx_handle_t));
  handle->ptr          = ptr;
  handle->kind         = kind;
  napi_value external;
  if (napi_create_external(env, handle, handle_finalize, NULL, &external) != napi_ok) {
    free_ctx(handle);
    safe_free(handle);
    return throw_error(env, "failed to create context handle");
  }
  return external;
}

static ctx_handle_t* unwrap_handle(napi_env env, napi_value value, handle_kind_t kind) {
  void* data = NULL;
  if (napi_get_value_external(env, value, &data) != napi_ok || !data) {
    napi_throw_type_error(env, NULL, "expected a native context handle");
    return NULL;
  }
  ctx_handle_t* handle = (ctx_handle_t*) data;
  if (handle->kind != kind || !handle->ptr) {
    napi_throw_type_error(env, NULL, "invalid or already freed context handle");
    return NULL;
  }
  return handle;
}

#define GET_ARGS(env, info, count)                                                    \
  size_t     argc = (count);                                                          \
  napi_value argv[(count) > 0 ? (count) : 1];                                         \
  NAPI_CALL((env), napi_get_cb_info((env), (info), &argc, argv, NULL, NULL));         \
  if (argc < (count)) return throw_error((env), "missing arguments");

/* ── storage bridge (JS storage object ↔ storage_plugin_t) ───────────── */

// The env is only valid while we are inside a synchronous N-API call.
// All storage callbacks are triggered from within such calls (the C core
// only runs when JS invokes an addon function), so tracking the current
// env at every entry point is safe.
static napi_env g_env         = NULL;
static napi_ref g_storage_ref = NULL;

static bool storage_call(const char* fn_name, size_t argc, napi_value* argv, napi_value* result) {
  if (!g_env || !g_storage_ref) return false;
  napi_value storage, fn;
  if (napi_get_reference_value(g_env, g_storage_ref, &storage) != napi_ok) return false;
  if (napi_get_named_property(g_env, storage, fn_name, &fn) != napi_ok) return false;
  napi_valuetype type;
  if (napi_typeof(g_env, fn, &type) != napi_ok || type != napi_function) return false;
  return napi_call_function(g_env, storage, fn, argc, argv, result) == napi_ok;
}

static bool js_storage_get(char* key, buffer_t* buffer) {
  napi_value key_value, result;
  if (napi_create_string_utf8(g_env, key, NAPI_AUTO_LENGTH, &key_value) != napi_ok) return false;
  if (!storage_call("get", 1, &key_value, &result)) return false;

  bool is_typedarray = false;
  if (napi_is_typedarray(g_env, result, &is_typedarray) != napi_ok || !is_typedarray) return false;

  napi_typedarray_type type;
  size_t               length = 0;
  void*                data   = NULL;
  if (napi_get_typedarray_info(g_env, result, &type, &length, &data, NULL, NULL) != napi_ok || type != napi_uint8_array) return false;

  buffer_grow(buffer, length + 1);
  buffer->data.len = (uint32_t) length;
  if (length) memcpy(buffer->data.data, data, length);
  return true;
}

static void js_storage_set(char* key, bytes_t value) {
  napi_value argv[2];
  void*      data = NULL;
  napi_value arraybuffer;
  if (napi_create_string_utf8(g_env, key, NAPI_AUTO_LENGTH, &argv[0]) != napi_ok) return;
  if (napi_create_arraybuffer(g_env, value.len, &data, &arraybuffer) != napi_ok) return;
  if (value.len) memcpy(data, value.data, value.len);
  if (napi_create_typedarray(g_env, napi_uint8_array, value.len, arraybuffer, 0, &argv[1]) != napi_ok) return;
  napi_value result;
  storage_call("set", 2, argv, &result);
}

static void js_storage_del(char* key) {
  napi_value key_value, result;
  if (napi_create_string_utf8(g_env, key, NAPI_AUTO_LENGTH, &key_value) != napi_ok) return;
  storage_call("del", 1, &key_value, &result);
}

// registerStorage(storage: {get,set,del}) -> void
static napi_value register_storage(napi_env env, napi_callback_info info) {
  GET_ARGS(env, info, 1);
  g_env = env;

  if (g_storage_ref) {
    napi_delete_reference(env, g_storage_ref);
    g_storage_ref = NULL;
  }
  NAPI_CALL(env, napi_create_reference(env, argv[0], 1, &g_storage_ref));

  storage_plugin_t plugin = {
      .get             = js_storage_get,
      .set             = js_storage_set,
      .del             = js_storage_del,
      .max_sync_states = 3};
  c4_set_storage_config(&plugin);
  return NULL;
}

/* ── method type ─────────────────────────────────────────────────────── */

// getMethodType(chainId, method, paramsJson | null, flags) -> number
static napi_value get_method_type(napi_env env, napi_callback_info info) {
  GET_ARGS(env, info, 4);
  g_env = env;

  uint64_t chain_id = 0;
  uint32_t flags    = 0;
  bool     ok       = true;
  if (!get_u64(env, argv[0], &chain_id)) return NULL;
  char* method = get_string(env, argv[1], &ok);
  if (!ok) return NULL;
  char* params = get_opt_string(env, argv[2], &ok);
  if (!ok) {
    safe_free(method);
    return NULL;
  }
  if (!get_u32(env, argv[3], &flags)) {
    safe_free(method);
    safe_free(params);
    return NULL;
  }

  method_type_t type = c4_get_method_type((chain_id_t) chain_id, method,
                                          params ? json_parse(params) : (json_t) {0}, (verify_flags_t) flags);
  safe_free(method);
  safe_free(params);

  napi_value result;
  NAPI_CALL(env, napi_create_int32(env, (int32_t) type, &result));
  return result;
}

/* ── prover API ──────────────────────────────────────────────────────── */

// createProverCtx(method, argsJson, chainId, flags) -> handle
static napi_value create_prover_ctx(napi_env env, napi_callback_info info) {
  GET_ARGS(env, info, 4);
  g_env = env;

  uint64_t chain_id = 0;
  uint32_t flags    = 0;
  bool     ok       = true;
  char*    method   = get_string(env, argv[0], &ok);
  if (!ok) return NULL;
  char* args = get_string(env, argv[1], &ok);
  if (!ok) {
    safe_free(method);
    return NULL;
  }
  if (!get_u64(env, argv[2], &chain_id) || !get_u32(env, argv[3], &flags)) {
    safe_free(method);
    safe_free(args);
    return NULL;
  }

  prover_ctx_t* ctx = c4_prover_create(method, args, (chain_id_t) chain_id, flags);
  safe_free(method);
  safe_free(args);
  if (!ctx) return throw_error(env, "failed to create prover context");
  return wrap_handle(env, ctx, HANDLE_PROVER);
}

// executeProverCtx(handle) -> JSON status string
static napi_value execute_prover_ctx(napi_env env, napi_callback_info info) {
  GET_ARGS(env, info, 1);
  g_env = env;

  ctx_handle_t* handle = unwrap_handle(env, argv[0], HANDLE_PROVER);
  if (!handle) return NULL;
  prover_ctx_t* ctx    = (prover_ctx_t*) handle->ptr;
  c4_status_t   status = c4_prover_execute(ctx);
  return make_owned_string(env, c4i_build_prover_json_status(status, &ctx->state, ctx->proof.data, ctx->proof.len, true));
}

// getProof(handle) -> Uint8Array (copy of the generated proof)
static napi_value get_proof(napi_env env, napi_callback_info info) {
  GET_ARGS(env, info, 1);
  g_env = env;

  ctx_handle_t* handle = unwrap_handle(env, argv[0], HANDLE_PROVER);
  if (!handle) return NULL;
  prover_ctx_t* ctx = (prover_ctx_t*) handle->ptr;

  void*      data = NULL;
  napi_value arraybuffer, result;
  NAPI_CALL(env, napi_create_arraybuffer(env, ctx->proof.len, &data, &arraybuffer));
  if (ctx->proof.len) memcpy(data, ctx->proof.data, ctx->proof.len);
  NAPI_CALL(env, napi_create_typedarray(env, napi_uint8_array, ctx->proof.len, arraybuffer, 0, &result));
  return result;
}

// freeProverCtx(handle) -> void
static napi_value free_prover_ctx(napi_env env, napi_callback_info info) {
  GET_ARGS(env, info, 1);
  g_env                = env;
  ctx_handle_t* handle = unwrap_handle(env, argv[0], HANDLE_PROVER);
  if (handle) free_ctx(handle);
  return NULL;
}

/* ── verify API ──────────────────────────────────────────────────────── */

// createVerifyCtx(proof, method, argsJson, chainId, checkpoint|null, witnessKeys|null, flags, minLatestBlockTs) -> handle
static napi_value create_verify_ctx(napi_env env, napi_callback_info info) {
  GET_ARGS(env, info, 8);
  g_env = env;

  bytes_t  proof               = {0};
  uint64_t chain_id            = 0;
  uint32_t flags               = 0;
  uint64_t min_latest_block_ts = 0;
  bool     ok                  = true;

  if (!get_bytes_view(env, argv[0], &proof)) return NULL;
  char* method = get_string(env, argv[1], &ok);
  if (!ok) return NULL;
  char* args = get_opt_string(env, argv[2], &ok);
  if (!ok) {
    safe_free(method);
    return NULL;
  }
  char* checkpoint = NULL;
  char* witness    = NULL;
  if (!get_u64(env, argv[3], &chain_id)) goto cleanup_fail;
  checkpoint = get_opt_string(env, argv[4], &ok);
  if (!ok) goto cleanup_fail;
  witness = get_opt_string(env, argv[5], &ok);
  if (!ok) goto cleanup_fail;
  if (!get_u32(env, argv[6], &flags) || !get_u64(env, argv[7], &min_latest_block_ts)) goto cleanup_fail;

  c4_set_checkpoint((chain_id_t) chain_id, checkpoint);

  addon_verify_ctx_t* ctx = safe_calloc(1, sizeof(addon_verify_ctx_t));
  ctx->proof              = bytes_dup(proof);
  c4_verify_init(&ctx->verify, ctx->proof, method,
                 args ? json_parse(args) : ((json_t) {.len = 0, .start = "[]", .type = JSON_TYPE_ARRAY}),
                 (chain_id_t) chain_id, (verify_flags_t) flags);
  // method and args are now owned by the verify ctx (freed in free_ctx)

  if (witness && strlen(witness) > 40 && witness[0] == '0' && witness[1] == 'x') {
    bytes_t witness_key_bytes = bytes(safe_malloc(strlen(witness) / 2), (strlen(witness) - 2) / 2);
    hex_to_bytes(witness + 2, -1, witness_key_bytes);
    ctx->verify.witness_keys = witness_key_bytes;
  }
  ctx->verify.min_latest_block_ts = min_latest_block_ts;

  safe_free(checkpoint);
  safe_free(witness);
  return wrap_handle(env, ctx, HANDLE_VERIFY);

cleanup_fail:
  safe_free(method);
  safe_free(args);
  safe_free(checkpoint);
  safe_free(witness);
  return NULL;
}

// verifyProof(handle) -> JSON status string
static napi_value verify_proof(napi_env env, napi_callback_info info) {
  GET_ARGS(env, info, 1);
  g_env = env;

  ctx_handle_t* handle = unwrap_handle(env, argv[0], HANDLE_VERIFY);
  if (!handle) return NULL;
  verify_ctx_t* ctx      = &((addon_verify_ctx_t*) handle->ptr)->verify;
  c4_status_t   status   = c4_verify(ctx);
  bool          reverted = (ctx->flags & VERIFY_FLAG_REVERTED) != 0;
  return make_owned_string(env, c4i_build_verifier_json_status(status, &ctx->state, ctx->data, reverted, true));
}

// freeVerifyCtx(handle) -> void
static napi_value free_verify_ctx(napi_env env, napi_callback_info info) {
  GET_ARGS(env, info, 1);
  g_env                = env;
  ctx_handle_t* handle = unwrap_handle(env, argv[0], HANDLE_VERIFY);
  if (handle) free_ctx(handle);
  return NULL;
}

/* ── unified RPC API ─────────────────────────────────────────────────── */

// createRpcCtx(method, paramsJson, chainId, proverFlags, verifyFlags, proverMode) -> handle
static napi_value create_rpc_ctx(napi_env env, napi_callback_info info) {
  GET_ARGS(env, info, 6);
  g_env = env;

  uint64_t chain_id     = 0;
  uint32_t prover_flags = 0;
  uint32_t verify_flags = 0;
  int32_t  prover_mode  = 0;
  bool     ok           = true;

  char* method = get_string(env, argv[0], &ok);
  if (!ok) return NULL;
  char* params = get_string(env, argv[1], &ok);
  if (!ok) {
    safe_free(method);
    return NULL;
  }
  if (!get_u64(env, argv[2], &chain_id) || !get_u32(env, argv[3], &prover_flags) ||
      !get_u32(env, argv[4], &verify_flags) || !get_i32(env, argv[5], &prover_mode)) {
    safe_free(method);
    safe_free(params);
    return NULL;
  }

  c4_rpc_ctx_t* ctx = c4_rpc_ctx_create(method, params, (chain_id_t) chain_id,
                                        (prover_flags_t) prover_flags, (verify_flags_t) verify_flags,
                                        (c4_prover_mode_t) prover_mode);
  safe_free(method);
  safe_free(params);
  if (!ctx) return throw_error(env, "failed to create rpc context");
  return wrap_handle(env, ctx, HANDLE_RPC);
}

// executeRpcCtx(handle) -> JSON status string
static napi_value execute_rpc_ctx(napi_env env, napi_callback_info info) {
  GET_ARGS(env, info, 1);
  g_env = env;

  ctx_handle_t* handle = unwrap_handle(env, argv[0], HANDLE_RPC);
  if (!handle) return NULL;
  return make_owned_string(env, c4_rpc_build_json_status((c4_rpc_ctx_t*) handle->ptr, true));
}

// freeRpcCtx(handle) -> void
static napi_value free_rpc_ctx(napi_env env, napi_callback_info info) {
  GET_ARGS(env, info, 1);
  g_env                = env;
  ctx_handle_t* handle = unwrap_handle(env, argv[0], HANDLE_RPC);
  if (handle) free_ctx(handle);
  return NULL;
}

// rpcCtxSetWitnessKeys(handle, keys) -> void
static napi_value rpc_ctx_set_witness_keys(napi_env env, napi_callback_info info) {
  GET_ARGS(env, info, 2);
  g_env = env;

  ctx_handle_t* handle = unwrap_handle(env, argv[0], HANDLE_RPC);
  if (!handle) return NULL;
  bool  ok   = true;
  char* keys = get_opt_string(env, argv[1], &ok);
  if (!ok) return NULL;
  c4_rpc_ctx_set_witness_keys((c4_rpc_ctx_t*) handle->ptr, keys);
  safe_free(keys);
  return NULL;
}

// rpcCtxSetProxyUrls(handle, rpcUrls, beaconUrls) -> void
static napi_value rpc_ctx_set_proxy_urls(napi_env env, napi_callback_info info) {
  GET_ARGS(env, info, 3);
  g_env = env;

  ctx_handle_t* handle = unwrap_handle(env, argv[0], HANDLE_RPC);
  if (!handle) return NULL;
  bool  ok       = true;
  char* rpc_urls = get_opt_string(env, argv[1], &ok);
  if (!ok) return NULL;
  char* beacon_urls = get_opt_string(env, argv[2], &ok);
  if (!ok) {
    safe_free(rpc_urls);
    return NULL;
  }
  c4_rpc_ctx_set_proxy_urls((c4_rpc_ctx_t*) handle->ptr, rpc_urls, beacon_urls);
  safe_free(rpc_urls);
  safe_free(beacon_urls);
  return NULL;
}

// rpcCtxSetMinLatestBlockTs(handle, ts) -> void
static napi_value rpc_ctx_set_min_latest_block_ts(napi_env env, napi_callback_info info) {
  GET_ARGS(env, info, 2);
  g_env = env;

  ctx_handle_t* handle = unwrap_handle(env, argv[0], HANDLE_RPC);
  if (!handle) return NULL;
  uint64_t ts = 0;
  if (!get_u64(env, argv[1], &ts)) return NULL;
  c4_rpc_ctx_set_min_latest_block_ts((c4_rpc_ctx_t*) handle->ptr, ts);
  return NULL;
}

/* ── requests ────────────────────────────────────────────────────────── */

// The req_ptr is transferred to JS as decimal string (req_ptr_as_string=true)
// and parsed back here.
static data_request_t* parse_req_ptr(napi_env env, napi_value value) {
  bool  ok  = true;
  char* str = get_string(env, value, &ok);
  if (!ok) return NULL;
  data_request_t* req = (data_request_t*) (uintptr_t) strtoull(str, NULL, 10);
  safe_free(str);
  if (!req) napi_throw_type_error(env, NULL, "invalid req_ptr");
  return req;
}

// reqSetResponse(reqPtr: string, data: Uint8Array, nodeIndex: number) -> void
static napi_value req_set_response(napi_env env, napi_callback_info info) {
  GET_ARGS(env, info, 3);
  g_env = env;

  data_request_t* req = parse_req_ptr(env, argv[0]);
  if (!req) return NULL;
  bytes_t data = {0};
  if (!get_bytes_view(env, argv[1], &data)) return NULL;
  uint32_t node_index = 0;
  if (!get_u32(env, argv[2], &node_index)) return NULL;

  // Allocate len+1 and zero-terminate so JSON responses can also be read as C strings
  // (mirrors `copy_to_c` in the WASM binding). The request owns the copy.
  uint8_t* copy = safe_malloc(data.len + 1);
  if (data.len) memcpy(copy, data.data, data.len);
  copy[data.len]           = 0;
  req->response            = bytes(copy, data.len);
  req->response_node_index = (uint16_t) node_index;
  return NULL;
}

// reqSetError(reqPtr: string, error: string, nodeIndex: number) -> void
static napi_value req_set_error(napi_env env, napi_callback_info info) {
  GET_ARGS(env, info, 3);
  g_env = env;

  data_request_t* req = parse_req_ptr(env, argv[0]);
  if (!req) return NULL;
  bool  ok    = true;
  char* error = get_string(env, argv[1], &ok);
  if (!ok) return NULL;
  uint32_t node_index = 0;
  if (!get_u32(env, argv[2], &node_index)) {
    safe_free(error);
    return NULL;
  }

  req->error               = error; // ownership transferred to the request
  req->response_node_index = (uint16_t) node_index;
  return NULL;
}

/* ── utilities ───────────────────────────────────────────────────────── */

// setCheckpoint(chainId, checkpoint) -> void
static napi_value set_checkpoint(napi_env env, napi_callback_info info) {
  GET_ARGS(env, info, 2);
  g_env = env;

  uint64_t chain_id = 0;
  if (!get_u64(env, argv[0], &chain_id)) return NULL;
  bool  ok         = true;
  char* checkpoint = get_opt_string(env, argv[1], &ok);
  if (!ok) return NULL;
  c4_set_checkpoint((chain_id_t) chain_id, checkpoint);
  safe_free(checkpoint);
  return NULL;
}

// decodeProof(data: Uint8Array) -> JSON string | null
static napi_value decode_proof(napi_env env, napi_callback_info info) {
  GET_ARGS(env, info, 1);
  g_env = env;

  bytes_t data = {0};
  if (!get_bytes_view(env, argv[0], &data)) return NULL;
  const ssz_def_t* def = c4_get_req_type_from_req(data);
  if (!def) {
    napi_value result;
    NAPI_CALL(env, napi_get_null(env, &result));
    return result;
  }
  return make_owned_string(env, bprintf(NULL, "%Z", (ssz_ob_t) {.def = def, .bytes = data}));
}

// version() -> number
static napi_value version(napi_env env, napi_callback_info info) {
  (void) info;
  napi_value result;
  NAPI_CALL(env, napi_create_uint32(env, c4_current_version_number(), &result));
  return result;
}

/* ── module init ─────────────────────────────────────────────────────── */

static napi_value init(napi_env env, napi_value exports) {
  const struct {
    const char*   name;
    napi_callback fn;
  } functions[] = {
      {"getMethodType", get_method_type},
      {"createProverCtx", create_prover_ctx},
      {"executeProverCtx", execute_prover_ctx},
      {"getProof", get_proof},
      {"freeProverCtx", free_prover_ctx},
      {"createVerifyCtx", create_verify_ctx},
      {"verifyProof", verify_proof},
      {"freeVerifyCtx", free_verify_ctx},
      {"createRpcCtx", create_rpc_ctx},
      {"executeRpcCtx", execute_rpc_ctx},
      {"freeRpcCtx", free_rpc_ctx},
      {"rpcCtxSetWitnessKeys", rpc_ctx_set_witness_keys},
      {"rpcCtxSetProxyUrls", rpc_ctx_set_proxy_urls},
      {"rpcCtxSetMinLatestBlockTs", rpc_ctx_set_min_latest_block_ts},
      {"reqSetResponse", req_set_response},
      {"reqSetError", req_set_error},
      {"setCheckpoint", set_checkpoint},
      {"decodeProof", decode_proof},
      {"registerStorage", register_storage},
      {"version", version},
  };

  for (size_t i = 0; i < sizeof(functions) / sizeof(functions[0]); i++) {
    napi_value fn;
    NAPI_CALL(env, napi_create_function(env, functions[i].name, NAPI_AUTO_LENGTH, functions[i].fn, NULL, &fn));
    NAPI_CALL(env, napi_set_named_property(env, exports, functions[i].name, fn));
  }
  return exports;
}

NAPI_MODULE(colibri_native, init)
