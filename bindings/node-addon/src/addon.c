/**
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

#include <node_api.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bindings/colibri.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#include <limits.h>
#endif

typedef prover_t* (*fn_c4_create_prover_ctx)(char*, char*, uint64_t, uint32_t);
typedef char* (*fn_c4_prover_execute_json_status)(prover_t*);
typedef bytes_t (*fn_c4_prover_get_proof)(prover_t*);
typedef void (*fn_c4_free_prover_ctx)(prover_t*);
typedef void (*fn_c4_req_set_response)(void*, bytes_t, uint16_t);
typedef void (*fn_c4_req_set_error)(void*, char*, uint16_t);
typedef void* (*fn_c4_verify_create_ctx)(bytes_t, char*, char*, uint64_t, char*);
typedef void* (*fn_c4_verify_create_ctx_ext)(bytes_t, char*, char*, uint64_t, char*, char*);
typedef char* (*fn_c4_verify_execute_json_status)(void*);
typedef void (*fn_c4_verify_free_ctx)(void*);
typedef int (*fn_c4_get_method_support)(uint64_t, char*);

typedef struct {
  fn_c4_create_prover_ctx          c4_create_prover_ctx;
  fn_c4_prover_execute_json_status c4_prover_execute_json_status;
  fn_c4_prover_get_proof           c4_prover_get_proof;
  fn_c4_free_prover_ctx            c4_free_prover_ctx;
  fn_c4_req_set_response           c4_req_set_response;
  fn_c4_req_set_error              c4_req_set_error;
  fn_c4_verify_create_ctx          c4_verify_create_ctx;
  fn_c4_verify_create_ctx_ext      c4_verify_create_ctx_ext;
  fn_c4_verify_execute_json_status c4_verify_execute_json_status;
  fn_c4_verify_free_ctx            c4_verify_free_ctx;
  fn_c4_get_method_support         c4_get_method_support;
#if defined(_WIN32)
  HMODULE lib;
#else
  void* lib;
#endif
} c4_symbols_t;

static c4_symbols_t g_c4 = {0};

static void* load_symbol(const char* name) {
#if defined(_WIN32)
  return (void*) GetProcAddress(g_c4.lib, name);
#else
  return dlsym(g_c4.lib, name);
#endif
}

static bool load_c4_library(void) {
  if (g_c4.lib) return true;

#if defined(_WIN32)
  char    self_path[MAX_PATH] = {0};
  char    self_dir[MAX_PATH]  = {0};
  HMODULE self_mod            = NULL;
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         (LPCSTR) &load_c4_library, &self_mod)) {
    DWORD n = GetModuleFileNameA(self_mod, self_path, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
      strncpy(self_dir, self_path, MAX_PATH - 1);
      char* last = strrchr(self_dir, '\\');
      if (!last) last = strrchr(self_dir, '/');
      if (last) *last = 0;
    }
  }

  // Prefer loading from the addon directory.
  if (self_dir[0]) {
    char lib_path[MAX_PATH] = {0};
    snprintf(lib_path, MAX_PATH - 1, "%s\\c4.dll", self_dir);
    g_c4.lib = LoadLibraryA(lib_path);
    if (!g_c4.lib) {
      snprintf(lib_path, MAX_PATH - 1, "%s\\libc4.dll", self_dir);
      g_c4.lib = LoadLibraryA(lib_path);
    }
  }

  // Fallback to standard DLL search rules / PATH.
  if (!g_c4.lib) g_c4.lib = LoadLibraryA("c4.dll");
  if (!g_c4.lib) g_c4.lib = LoadLibraryA("libc4.dll");
#else
  // macOS/Linux: prefer loading from the addon directory (packaged alongside the .node).
  char    self_dir[PATH_MAX] = {0};
  Dl_info info               = {0};
  if (dladdr((void*) &load_c4_library, &info) && info.dli_fname) {
    strncpy(self_dir, info.dli_fname, sizeof(self_dir) - 1);
    char* last = strrchr(self_dir, '/');
    if (last) *last = 0;
  }

  if (self_dir[0]) {
    char lib_path[PATH_MAX] = {0};
#if defined(__APPLE__)
    snprintf(lib_path, sizeof(lib_path) - 1, "%s/libc4.dylib", self_dir);
    g_c4.lib = dlopen(lib_path, RTLD_LAZY);
    if (!g_c4.lib) {
      snprintf(lib_path, sizeof(lib_path) - 1, "%s/libc4.1.0.dylib", self_dir);
      g_c4.lib = dlopen(lib_path, RTLD_LAZY);
    }
    if (!g_c4.lib) {
      snprintf(lib_path, sizeof(lib_path) - 1, "%s/libc4.1.dylib", self_dir);
      g_c4.lib = dlopen(lib_path, RTLD_LAZY);
    }
#endif
    if (!g_c4.lib) {
      snprintf(lib_path, sizeof(lib_path) - 1, "%s/libc4.so", self_dir);
      g_c4.lib = dlopen(lib_path, RTLD_LAZY);
    }
    if (!g_c4.lib) {
      snprintf(lib_path, sizeof(lib_path) - 1, "%s/libc4.so.1", self_dir);
      g_c4.lib = dlopen(lib_path, RTLD_LAZY);
    }
    if (!g_c4.lib) {
      snprintf(lib_path, sizeof(lib_path) - 1, "%s/libc4.so.1.0", self_dir);
      g_c4.lib = dlopen(lib_path, RTLD_LAZY);
    }
  }

  // Fallback to loader path / system search.
  if (!g_c4.lib) g_c4.lib = dlopen("libc4.dylib", RTLD_LAZY);
#if defined(__APPLE__)
  if (!g_c4.lib) g_c4.lib = dlopen("libc4.1.0.dylib", RTLD_LAZY);
  if (!g_c4.lib) g_c4.lib = dlopen("libc4.1.dylib", RTLD_LAZY);
#endif
  if (!g_c4.lib) g_c4.lib = dlopen("libc4.so", RTLD_LAZY);
  if (!g_c4.lib) g_c4.lib = dlopen("libc4.so.1", RTLD_LAZY);
  if (!g_c4.lib) g_c4.lib = dlopen("libc4.so.1.0", RTLD_LAZY);
#endif

  if (!g_c4.lib) return false;

  g_c4.c4_create_prover_ctx          = (fn_c4_create_prover_ctx) load_symbol("c4_create_prover_ctx");
  g_c4.c4_prover_execute_json_status = (fn_c4_prover_execute_json_status) load_symbol("c4_prover_execute_json_status");
  g_c4.c4_prover_get_proof           = (fn_c4_prover_get_proof) load_symbol("c4_prover_get_proof");
  g_c4.c4_free_prover_ctx            = (fn_c4_free_prover_ctx) load_symbol("c4_free_prover_ctx");
  g_c4.c4_req_set_response           = (fn_c4_req_set_response) load_symbol("c4_req_set_response");
  g_c4.c4_req_set_error              = (fn_c4_req_set_error) load_symbol("c4_req_set_error");
  g_c4.c4_verify_create_ctx          = (fn_c4_verify_create_ctx) load_symbol("c4_verify_create_ctx");
  g_c4.c4_verify_create_ctx_ext      = (fn_c4_verify_create_ctx_ext) load_symbol("c4_verify_create_ctx_ext");
  g_c4.c4_verify_execute_json_status = (fn_c4_verify_execute_json_status) load_symbol("c4_verify_execute_json_status");
  g_c4.c4_verify_free_ctx            = (fn_c4_verify_free_ctx) load_symbol("c4_verify_free_ctx");
  g_c4.c4_get_method_support         = (fn_c4_get_method_support) load_symbol("c4_get_method_support");

  return g_c4.c4_create_prover_ctx && g_c4.c4_prover_execute_json_status && g_c4.c4_prover_get_proof &&
         g_c4.c4_free_prover_ctx && g_c4.c4_req_set_response && g_c4.c4_req_set_error &&
         g_c4.c4_verify_create_ctx && g_c4.c4_verify_execute_json_status && g_c4.c4_verify_free_ctx &&
         g_c4.c4_get_method_support;
}

static napi_value throw_error(napi_env env, const char* msg) {
  napi_throw_error(env, NULL, msg);
  return NULL;
}

static bool require_c4(napi_env env) {
  if (!load_c4_library()) {
    throw_error(env, "Failed to load libc4 shared library (expected c4.dll/libc4.dll next to the addon or on the loader path).");
    return false;
  }
  return true;
}

typedef struct {
  prover_t* ctx;
} prover_wrap_t;

typedef struct {
  void* ctx;
} verify_wrap_t;

static void prover_finalize(napi_env env, void* data, void* hint) {
  (void) env;
  (void) hint;
  prover_wrap_t* w = (prover_wrap_t*) data;
  if (!w) return;
  if (w->ctx && g_c4.c4_free_prover_ctx) {
    g_c4.c4_free_prover_ctx(w->ctx);
    w->ctx = NULL;
  }
  free(w);
}

static void verify_finalize(napi_env env, void* data, void* hint) {
  (void) env;
  (void) hint;
  verify_wrap_t* w = (verify_wrap_t*) data;
  if (!w) return;
  if (w->ctx && g_c4.c4_verify_free_ctx) {
    g_c4.c4_verify_free_ctx(w->ctx);
    w->ctx = NULL;
  }
  free(w);
}

static bool get_string(napi_env env, napi_value v, char** out) {
  size_t len = 0;
  if (napi_get_value_string_utf8(env, v, NULL, 0, &len) != napi_ok) return false;
  char* buf = (char*) malloc(len + 1);
  if (!buf) return false;
  if (napi_get_value_string_utf8(env, v, buf, len + 1, &len) != napi_ok) {
    free(buf);
    return false;
  }
  buf[len] = 0;
  *out     = buf;
  return true;
}

static bool get_u64(napi_env env, napi_value v, uint64_t* out) {
  napi_valuetype t;
  if (napi_typeof(env, v, &t) != napi_ok) return false;
  if (t == napi_bigint) {
    bool lossless = false;
    return napi_get_value_bigint_uint64(env, v, out, &lossless) == napi_ok;
  }
  if (t == napi_number) {
    double d = 0;
    if (napi_get_value_double(env, v, &d) != napi_ok) return false;
    if (d < 0) return false;
    *out = (uint64_t) d;
    return true;
  }
  return false;
}

static napi_value js_get_method_support(napi_env env, napi_callback_info info) {
  napi_value argv[2];
  size_t     argc = 2;
  napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
  if (argc < 2) return throw_error(env, "getMethodSupport(chainId, method) requires 2 arguments");
  if (!require_c4(env)) return NULL;

  uint64_t chain_id = 0;
  if (!get_u64(env, argv[0], &chain_id)) return throw_error(env, "Invalid chainId");
  char* method = NULL;
  if (!get_string(env, argv[1], &method)) return throw_error(env, "Invalid method");

  int res = g_c4.c4_get_method_support(chain_id, method);
  free(method);

  napi_value out;
  napi_create_int32(env, res, &out);
  return out;
}

static napi_value js_create_prover_ctx(napi_env env, napi_callback_info info) {
  napi_value argv[4];
  size_t     argc = 4;
  napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
  if (argc < 4) return throw_error(env, "createProverCtx(method, paramsJson, chainId, flags) requires 4 arguments");
  if (!require_c4(env)) return NULL;

  char* method = NULL;
  char* params = NULL;
  if (!get_string(env, argv[0], &method)) return throw_error(env, "Invalid method");
  if (!get_string(env, argv[1], &params)) {
    free(method);
    return throw_error(env, "Invalid paramsJson");
  }
  uint64_t chain_id = 0;
  if (!get_u64(env, argv[2], &chain_id)) {
    free(method);
    free(params);
    return throw_error(env, "Invalid chainId");
  }
  uint32_t flags = 0;
  napi_get_value_uint32(env, argv[3], &flags);

  prover_t* ctx = g_c4.c4_create_prover_ctx(method, params, chain_id, flags);
  free(method);
  free(params);

  prover_wrap_t* wrap = (prover_wrap_t*) calloc(1, sizeof(prover_wrap_t));
  wrap->ctx           = ctx;

  napi_value ext;
  napi_create_external(env, wrap, prover_finalize, NULL, &ext);
  return ext;
}

static bool unwrap_prover(napi_env env, napi_value v, prover_wrap_t** out) {
  void* ptr = NULL;
  if (napi_get_value_external(env, v, &ptr) != napi_ok) return false;
  *out = (prover_wrap_t*) ptr;
  return true;
}

static bool unwrap_verify(napi_env env, napi_value v, verify_wrap_t** out) {
  void* ptr = NULL;
  if (napi_get_value_external(env, v, &ptr) != napi_ok) return false;
  *out = (verify_wrap_t*) ptr;
  return true;
}

static napi_value js_prover_execute_json_status(napi_env env, napi_callback_info info) {
  napi_value argv[1];
  size_t     argc = 1;
  napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
  if (argc < 1) return throw_error(env, "proverExecuteJsonStatus(ctx) requires 1 argument");
  if (!require_c4(env)) return NULL;

  prover_wrap_t* wrap = NULL;
  if (!unwrap_prover(env, argv[0], &wrap) || !wrap || !wrap->ctx) return throw_error(env, "Invalid prover context");

  char* status = g_c4.c4_prover_execute_json_status(wrap->ctx);
  if (!status) return throw_error(env, "c4_prover_execute_json_status returned NULL");

  napi_value out;
  napi_create_string_utf8(env, status, NAPI_AUTO_LENGTH, &out);
  free(status);
  return out;
}

static napi_value js_prover_get_proof(napi_env env, napi_callback_info info) {
  napi_value argv[1];
  size_t     argc = 1;
  napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
  if (argc < 1) return throw_error(env, "proverGetProof(ctx) requires 1 argument");
  if (!require_c4(env)) return NULL;

  prover_wrap_t* wrap = NULL;
  if (!unwrap_prover(env, argv[0], &wrap) || !wrap || !wrap->ctx) return throw_error(env, "Invalid prover context");

  bytes_t    proof = g_c4.c4_prover_get_proof(wrap->ctx);
  napi_value arraybuffer;
  void*      ab_data = NULL;
  napi_create_arraybuffer(env, proof.len, &ab_data, &arraybuffer);
  if (proof.len && proof.data && ab_data) memcpy(ab_data, proof.data, proof.len);

  napi_value out;
  napi_create_typedarray(env, napi_uint8_array, proof.len, arraybuffer, 0, &out);
  return out;
}

static napi_value js_free_prover_ctx(napi_env env, napi_callback_info info) {
  napi_value argv[1];
  size_t     argc = 1;
  napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
  if (argc < 1) return throw_error(env, "freeProverCtx(ctx) requires 1 argument");
  if (!require_c4(env)) return NULL;

  prover_wrap_t* wrap = NULL;
  if (!unwrap_prover(env, argv[0], &wrap) || !wrap) return throw_error(env, "Invalid prover context");
  if (wrap->ctx) {
    g_c4.c4_free_prover_ctx(wrap->ctx);
    wrap->ctx = NULL;
  }
  return NULL;
}

static napi_value js_create_verify_ctx(napi_env env, napi_callback_info info) {
  napi_value argv[6];
  size_t     argc = 6;
  napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
  if (argc < 5) return throw_error(env, "createVerifyCtx(proof, method, argsJson, chainId, trustedCheckpoint, witnessKeys) requires at least 5 arguments");
  if (!require_c4(env)) return NULL;

  // proof: Uint8Array
  bool is_typed = false;
  napi_is_typedarray(env, argv[0], &is_typed);
  if (!is_typed) return throw_error(env, "Invalid proof (expected Uint8Array)");
  napi_typedarray_type ta_type;
  size_t               ta_len;
  void*                ta_data;
  napi_value           ta_ab;
  size_t               ta_off;
  napi_get_typedarray_info(env, argv[0], &ta_type, &ta_len, &ta_data, &ta_ab, &ta_off);
  if (ta_type != napi_uint8_array) return throw_error(env, "Invalid proof (expected Uint8Array)");

  bytes_t proof = {.len = (uint32_t) ta_len, .data = (uint8_t*) ta_data};

  char* method = NULL;
  char* args   = NULL;
  if (!get_string(env, argv[1], &method)) return throw_error(env, "Invalid method");
  if (!get_string(env, argv[2], &args)) {
    free(method);
    return throw_error(env, "Invalid argsJson");
  }

  uint64_t chain_id = 0;
  if (!get_u64(env, argv[3], &chain_id)) {
    free(method);
    free(args);
    return throw_error(env, "Invalid chainId");
  }

  char* checkpoint = NULL;
  if (argc >= 5) {
    napi_valuetype t;
    napi_typeof(env, argv[4], &t);
    if (t == napi_string) {
      if (!get_string(env, argv[4], &checkpoint)) checkpoint = NULL;
    }
  }

  char* witness = NULL;
  if (argc >= 6) {
    napi_valuetype t;
    napi_typeof(env, argv[5], &t);
    if (t == napi_string) {
      if (!get_string(env, argv[5], &witness)) witness = NULL;
    }
  }

  void* vctx = NULL;
  if (witness && g_c4.c4_verify_create_ctx_ext) {
    vctx = g_c4.c4_verify_create_ctx_ext(proof, method, args, chain_id, checkpoint, witness);
  }
  else {
    vctx = g_c4.c4_verify_create_ctx(proof, method, args, chain_id, checkpoint);
  }

  free(method);
  free(args);
  if (checkpoint) free(checkpoint);
  if (witness) free(witness);

  verify_wrap_t* wrap = (verify_wrap_t*) calloc(1, sizeof(verify_wrap_t));
  wrap->ctx           = vctx;

  napi_value ext;
  napi_create_external(env, wrap, verify_finalize, NULL, &ext);
  return ext;
}

static napi_value js_verify_execute_json_status(napi_env env, napi_callback_info info) {
  napi_value argv[1];
  size_t     argc = 1;
  napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
  if (argc < 1) return throw_error(env, "verifyExecuteJsonStatus(ctx) requires 1 argument");
  if (!require_c4(env)) return NULL;

  verify_wrap_t* wrap = NULL;
  if (!unwrap_verify(env, argv[0], &wrap) || !wrap || !wrap->ctx) return throw_error(env, "Invalid verify context");

  char* status = g_c4.c4_verify_execute_json_status(wrap->ctx);
  if (!status) return throw_error(env, "c4_verify_execute_json_status returned NULL");

  napi_value out;
  napi_create_string_utf8(env, status, NAPI_AUTO_LENGTH, &out);
  free(status);
  return out;
}

static napi_value js_free_verify_ctx(napi_env env, napi_callback_info info) {
  napi_value argv[1];
  size_t     argc = 1;
  napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
  if (argc < 1) return throw_error(env, "freeVerifyCtx(ctx) requires 1 argument");
  if (!require_c4(env)) return NULL;

  verify_wrap_t* wrap = NULL;
  if (!unwrap_verify(env, argv[0], &wrap) || !wrap) return throw_error(env, "Invalid verify context");
  if (wrap->ctx) {
    g_c4.c4_verify_free_ctx(wrap->ctx);
    wrap->ctx = NULL;
  }
  return NULL;
}

static napi_value js_req_set_response(napi_env env, napi_callback_info info) {
  napi_value argv[3];
  size_t     argc = 3;
  napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
  if (argc < 3) return throw_error(env, "reqSetResponse(reqPtr, data, nodeIndex) requires 3 arguments");
  if (!require_c4(env)) return NULL;

  uint64_t req_ptr_u64 = 0;
  if (!get_u64(env, argv[0], &req_ptr_u64)) return throw_error(env, "Invalid reqPtr");
  void* req_ptr = (void*) (uintptr_t) req_ptr_u64;

  bool is_typed = false;
  napi_is_typedarray(env, argv[1], &is_typed);
  if (!is_typed) return throw_error(env, "Invalid data (expected Uint8Array)");
  napi_typedarray_type ta_type;
  size_t               ta_len;
  void*                ta_data;
  napi_value           ta_ab;
  size_t               ta_off;
  napi_get_typedarray_info(env, argv[1], &ta_type, &ta_len, &ta_data, &ta_ab, &ta_off);
  if (ta_type != napi_uint8_array) return throw_error(env, "Invalid data (expected Uint8Array)");

  uint32_t node_index_u32 = 0;
  napi_get_value_uint32(env, argv[2], &node_index_u32);

  bytes_t data = {.len = (uint32_t) ta_len, .data = (uint8_t*) ta_data};
  g_c4.c4_req_set_response(req_ptr, data, (uint16_t) node_index_u32);
  return NULL;
}

static napi_value js_req_set_error(napi_env env, napi_callback_info info) {
  napi_value argv[3];
  size_t     argc = 3;
  napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
  if (argc < 3) return throw_error(env, "reqSetError(reqPtr, error, nodeIndex) requires 3 arguments");
  if (!require_c4(env)) return NULL;

  uint64_t req_ptr_u64 = 0;
  if (!get_u64(env, argv[0], &req_ptr_u64)) return throw_error(env, "Invalid reqPtr");
  void* req_ptr = (void*) (uintptr_t) req_ptr_u64;

  char* err = NULL;
  if (!get_string(env, argv[1], &err)) return throw_error(env, "Invalid error string");

  uint32_t node_index_u32 = 0;
  napi_get_value_uint32(env, argv[2], &node_index_u32);
  g_c4.c4_req_set_error(req_ptr, err, (uint16_t) node_index_u32);
  free(err);
  return NULL;
}

static napi_value init(napi_env env, napi_value exports) {
  napi_property_descriptor props[] = {
      {"getMethodSupport", NULL, js_get_method_support, NULL, NULL, NULL, napi_default, NULL},
      {"createProverCtx", NULL, js_create_prover_ctx, NULL, NULL, NULL, napi_default, NULL},
      {"proverExecuteJsonStatus", NULL, js_prover_execute_json_status, NULL, NULL, NULL, napi_default, NULL},
      {"proverGetProof", NULL, js_prover_get_proof, NULL, NULL, NULL, napi_default, NULL},
      {"freeProverCtx", NULL, js_free_prover_ctx, NULL, NULL, NULL, napi_default, NULL},
      {"createVerifyCtx", NULL, js_create_verify_ctx, NULL, NULL, NULL, napi_default, NULL},
      {"verifyExecuteJsonStatus", NULL, js_verify_execute_json_status, NULL, NULL, NULL, napi_default, NULL},
      {"freeVerifyCtx", NULL, js_free_verify_ctx, NULL, NULL, NULL, napi_default, NULL},
      {"reqSetResponse", NULL, js_req_set_response, NULL, NULL, NULL, napi_default, NULL},
      {"reqSetError", NULL, js_req_set_error, NULL, NULL, NULL, napi_default, NULL},
  };

  napi_define_properties(env, exports, sizeof(props) / sizeof(props[0]), props);
  return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, init)
