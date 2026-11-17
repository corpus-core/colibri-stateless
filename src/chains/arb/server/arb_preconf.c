/*
 * Copyright 2025,2026 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "arb_preconf.h"
#include "beacon_types.h"
#include "crypto.h"
#include "el_header.h"
#include "eth_req.h"
#include "feed_client.h"
#include "handler.h"
#include "logger.h"
#include "ssz.h"
#include <string.h>

#define ARB_HEADER_MAX (16u * 1024u)

static void set_err(char** err, const char* msg) {
  if (err && !*err) *err = strdup(msg);
}

static bool json_field_present(json_t block, const char* name) {
  json_t field = json_get(block, name);
  return field.type != JSON_TYPE_NOT_FOUND && field.type != JSON_TYPE_NULL;
}

/** Header layout of an `eth_getBlockByHash` object, from the newest field it carries. */
static fork_id_t arb_header_fork(json_t block) {
  if (json_field_present(block, EL_SLOT_NUMBER) || json_field_present(block, EL_BLOCK_ACCESS_LIST_HASH))
    return C4_FORK_GLOAS;
  if (json_field_present(block, EL_REQUESTS_HASH))
    return C4_FORK_ELECTRA;
  if (json_field_present(block, EL_PARENT_BEACON_BLOCK_ROOT) || json_field_present(block, EL_BLOB_GAS_USED) || json_field_present(block, EL_EXCESS_BLOB_GAS))
    return C4_FORK_DENEB;
  if (json_field_present(block, EL_WITHDRAWALS_ROOT))
    return C4_FORK_CAPELLA;
  return C4_FORK_BELLATRIX;
}

static bool header_matches(bytes_t header, bytes_t block_hash) {
  uint8_t got[32];
  keccak(header, got);
  return memcmp(got, block_hash.data, 32) == 0;
}

bytes_t arb_preconf_encode(json_t rpc_json, bytes_t block_hash, bytes_t signature, bytes_t signed_tail, char** err) {
  if (err) *err = NULL;
  if (block_hash.len != 32 || signature.len != 65 || !signed_tail.data || signed_tail.len < 45) {
    set_err(err, "feed head is incomplete");
    return NULL_BYTES;
  }
  if (rpc_json.type != JSON_TYPE_OBJECT) {
    set_err(err, "eth rpc response is not an object");
    return NULL_BYTES;
  }
  json_t error = json_get(rpc_json, "error");
  if (error.type == JSON_TYPE_OBJECT) {
    json_t message = json_get(error, "message");
    if (message.type == JSON_TYPE_STRING && message.len > 2) {
      size_t n = message.len - 2;
      if (n > 160) n = 160;
      char* text = safe_malloc(n + 1);
      for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char) message.start[1 + i];
        text[i]         = (c < 0x20 || c == 0x7f || c == '%') ? ' ' : (char) c;
      }
      text[n] = 0;
      set_err(err, text);
      safe_free(text);
    }
    if (err && !*err) set_err(err, "eth rpc error");
    return NULL_BYTES;
  }

  json_t   result = json_get(rpc_json, "result");
  buffer_t owned  = {0};
  bytes_t  header = NULL_BYTES;
  if (result.type == JSON_TYPE_STRING)
    header = json_as_bytes(result, &owned);
  else if (result.type == JSON_TYPE_OBJECT) {
    bytes_t built = NULL_BYTES;
    if (eth_el_header_build_from_json(NULL, &built, arb_header_fork(result), result) == C4_SUCCESS)
      header = built;
    owned.data      = header;
    owned.allocated = header.data ? 1 : 0;
  }
  bool produced = header.data && header.len > 0 && header.len <= ARB_HEADER_MAX;
  bool matches  = produced && header_matches(header, block_hash);
  if (!matches) {
    buffer_free(&owned);
    set_err(err, produced ? "raw header does not match the feed block hash" : "eth rpc did not return a block header");
    return NULL_BYTES;
  }

  ssz_builder_t proof = ssz_builder_for_type(ETH_SSZ_ARB_FEED_PROOF);
  if (!proof.def) {
    buffer_free(&owned);
    set_err(err, "arbFeedProof type is not registered");
    return NULL_BYTES;
  }
  ssz_add_bytes(&proof, "elHeader", header);
  buffer_free(&owned);
  const ssz_def_t* tail_union = ssz_get_def(proof.def, "signedTail");
  if (!tail_union || tail_union->type != SSZ_TYPE_UNION || tail_union->def.container.len < 2) {
    ssz_builder_free(&proof);
    set_err(err, "signedTail union is missing");
    return NULL_BYTES;
  }
  ssz_builder_t tail_builder = ssz_builder_for_def(tail_union->def.container.elements + 1);
  buffer_append(&tail_builder.fixed, signed_tail);
  ssz_add_builders(&proof, "signedTail", tail_builder);
  ssz_add_bytes(&proof, "signature", signature);
  ssz_ob_t ob = ssz_builder_to_bytes(&proof);
  if (!ob.bytes.data || ob.bytes.len == 0) {
    safe_free(ob.bytes.data);
    set_err(err, "failed to encode arbFeedProof");
    return NULL_BYTES;
  }
  return ob.bytes;
}

typedef struct {
  single_request_t* intern;
  chain_id_t        chain_id;
  bool              debug;
  uint8_t           hash[32];
  uint8_t           sig[65];
  bytes_t           tail;
} arb_preconf_fetch_t;

/** Not an HTTP connection. `c4_add_request` only uses it as the callback owner. */
static client_t arb_rpc_client;

static bool debug_method_missing(json_t root) {
  json_t code = json_get(json_get(root, "error"), "code");
  return code.type == JSON_TYPE_NUMBER && code.len == 6 && code.start && memcmp(code.start, "-32601", 6) == 0;
}

static bool any_server_supports_debug(chain_id_t chain_id) {
  server_list_t* list = c4_get_server_list(C4_DATA_TYPE_ETH_RPC, chain_id);
  if (!list) return false;
  for (size_t i = 0; i < list->count && i < 32; i++) {
    if (c4_is_method_supported(list, (int) i, "debug_getRawHeader")) return true;
  }
  return false;
}

/**
 * Builds one JSON-RPC POST. The HTTP client picks the server, retries, and
 * caches the body when `ttl` is non-zero.
 *
 * `ttl` stays 0: the feed runs ahead of the execution node, and a cached
 * `result: null` would keep this hash failing until the entry expired.
 * A header that matches the hash is immutable, but the miss is the race.
 */
static data_request_t* arb_rpc_request(chain_id_t chain_id, const char* method, bytes_t hash, bool with_object_flag) {
  data_request_t* req = safe_calloc(1, sizeof(*req));
  req->chain_id       = chain_id;
  req->method         = C4_DATA_METHOD_POST;
  req->type           = C4_DATA_TYPE_ETH_RPC;
  req->encoding       = C4_DATA_ENCODING_JSON;
  buffer_t payload    = {0};
  if (with_object_flag)
    bprintf(&payload, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"%s\",\"params\":[\"0x%x\",false]}", method, hash);
  else
    bprintf(&payload, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"%s\",\"params\":[\"0x%x\"]}", method, hash);
  req->payload = payload.data;
  return req;
}

static void arb_preconf_finish(arb_preconf_fetch_t* ctx, data_request_t* rpc, json_t root, char* schema_err) {
  char*   err     = NULL;
  bytes_t encoded = NULL_BYTES;
  if (!schema_err && root.type == JSON_TYPE_OBJECT)
    encoded = arb_preconf_encode(root, bytes(ctx->hash, 32), bytes(ctx->sig, 65), ctx->tail, &err);
  if (encoded.data)
    ctx->intern->req->response = encoded;
  else if (schema_err)
    ctx->intern->req->error = schema_err;
  else if (err)
    ctx->intern->req->error = err;
  else if (rpc && rpc->error)
    ctx->intern->req->error = strdup(rpc->error);
  else
    ctx->intern->req->error = strdup("eth rpc did not return a block header");
  c4_internal_call_finish(ctx->intern);
  c4_request_free(rpc);
  safe_free(ctx->tail.data);
  safe_free(ctx);
}

static void arb_preconf_fetch(arb_preconf_fetch_t* ctx, bool debug);

static void arb_preconf_rpc_cb(client_t* client, void* data, data_request_t* rpc) {
  (void) client;
  arb_preconf_fetch_t* ctx  = data;
  json_t               root = rpc && rpc->response.data ? json_parse((const char*) rpc->response.data) : (json_t) {0};
  // `debug_getRawHeader` yields a hex string. `eth_getBlockByHash` yields the header object.
  const char* schema = ctx->debug ? "bytes" : JSON_BLOCK_HEADER_FIELDS;
  const char* prefix = ctx->debug ? "debug_getRawHeader: " : "Invalid results for Block: ";
  char*       err    = (char*) json_validate(json_get(root, "result"), schema, prefix);
  if (err && ctx->debug) {
    safe_free(err);
    if (debug_method_missing(root) && rpc) {
      server_list_t* list = c4_get_server_list(C4_DATA_TYPE_ETH_RPC, ctx->chain_id);
      c4_mark_method_unsupported(list, rpc->response_node_index, "debug_getRawHeader");
    }
    c4_request_free(rpc);
    arb_preconf_fetch(ctx, false);
    return;
  }
  // A JSON-RPC error object, or a transport error, is more specific than the schema mismatch.
  if (err && (json_get(root, "error").type == JSON_TYPE_OBJECT || (rpc && rpc->error))) {
    safe_free(err);
    err = NULL;
  }
  arb_preconf_finish(ctx, rpc, root, err);
}

static void arb_preconf_fetch(arb_preconf_fetch_t* ctx, bool debug) {
  ctx->debug          = debug;
  data_request_t* req = arb_rpc_request(ctx->chain_id, debug ? "debug_getRawHeader" : "eth_getBlockByHash", bytes(ctx->hash, 32), !debug);
  c4_add_request(&arb_rpc_client, req, ctx, arb_preconf_rpc_cb);
}

static void finish_error(single_request_t* r, const char* msg) {
  r->req->error = strdup(msg);
  c4_internal_call_finish(r);
}

bool c4_handle_arb_preconf(single_request_t* r) {
  if (!r || !r->req || !r->req->url) return false;
  if (strncmp(r->req->url, "arb_preconf/", 12) != 0) return false;
  if (strcmp(r->req->url + 12, "latest") != 0) {
    finish_error(r, "only latest is supported");
    return true;
  }

  uint8_t  hash[32];
  uint8_t  sig[65];
  uint8_t* tail     = NULL;
  uint32_t tail_len = 0;
  if (!arb_feed_copy_head(hash, sig, &tail, &tail_len, NULL)) {
    finish_error(r, "no sequencer block with a signature yet");
    return true;
  }

  server_list_t* list = c4_get_server_list(C4_DATA_TYPE_ETH_RPC, r->req->chain_id);
  if (!list || list->count == 0 || !list->urls) {
    safe_free(tail);
    finish_error(r, "no eth rpc configured");
    return true;
  }

  arb_preconf_fetch_t* ctx = safe_calloc(1, sizeof(*ctx));
  ctx->intern              = r;
  ctx->chain_id            = r->req->chain_id;
  memcpy(ctx->hash, hash, 32);
  memcpy(ctx->sig, sig, 65);
  ctx->tail = bytes(tail, tail_len);
  arb_preconf_fetch(ctx, any_server_supports_debug(r->req->chain_id));
  return true;
}
