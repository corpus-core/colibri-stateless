/*
 * Copyright 2025,2026 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "arb_preconf.h"
#include "beacon_types.h"
#include "crypto.h"
#include "feed_client.h"
#include "handler.h"
#include "logger.h"
#include "rlp.h"
#include "ssz.h"
#include <string.h>

#define ARB_HEADER_MAX (16u * 1024u)
#define ARB_FIELD_MAX  1024u

static void set_err(char** err, const char* msg) {
  if (err && !*err) *err = strdup(msg);
}

static int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

/** Decodes a JSON hex string. An odd nibble count is padded with one leading zero. */
static bool decode_hex_string(json_t value, buffer_t* out, uint32_t cap) {
  if (value.type != JSON_TYPE_STRING || value.len < 2) return false;
  const char* s = value.start + 1;
  size_t      n = value.len - 2;
  if (n >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    s += 2;
    n -= 2;
  }
  if (n == 0) return true;
  uint32_t out_len = (uint32_t) ((n + 1) / 2);
  if (out_len > cap || out->data.len > cap - out_len) return false;
  uint32_t start = out->data.len;
  buffer_grow(out, start + out_len);
  size_t   i        = 0;
  uint32_t produced = 0;
  if (n % 2) {
    int hi = hex_nibble(s[0]);
    if (hi < 0) return false;
    out->data.data[start] = (uint8_t) hi;
    produced              = 1;
    i                     = 1;
  }
  for (; i + 1 < n; i += 2) {
    int hi = hex_nibble(s[i]);
    int lo = hex_nibble(s[i + 1]);
    if (hi < 0 || lo < 0) return false;
    out->data.data[start + produced] = (uint8_t) ((hi << 4) | lo);
    produced++;
  }
  out->data.len = start + produced;
  return produced == out_len;
}

/**
 * Adds one execution-header field.
 *
 * @return 1 when the field was written, 0 when an optional field is absent, -1 on error
 */
static int rlp_header_field(buffer_t* rlp, json_t block, const char* name, uint32_t width, bool quantity, bool optional) {
  json_t value = json_get(block, name);
  if (value.type == JSON_TYPE_NOT_FOUND || value.type == JSON_TYPE_NULL) return optional ? 0 : -1;
  buffer_t raw = {0};
  if (!decode_hex_string(value, &raw, width ? width : ARB_FIELD_MAX)) {
    buffer_free(&raw);
    return -1;
  }
  if (quantity) {
    if (raw.data.len > 32) {
      buffer_free(&raw);
      return -1;
    }
    rlp_add_uint(rlp, raw.data);
  }
  else if (width) {
    if (raw.data.len != width) {
      buffer_free(&raw);
      return -1;
    }
    rlp_add_item(rlp, raw.data);
  }
  else {
    rlp_add_item(rlp, raw.data);
  }
  buffer_free(&raw);
  return rlp->data.len <= ARB_HEADER_MAX ? 1 : -1;
}

static bool rlp_from_block(json_t block, buffer_t* header) {
  static const struct {
    const char* name;
    uint32_t    width;
    bool        quantity;
    bool        optional;
  } fields[] = {
      {"parentHash", 32, false, false},
      {"sha3Uncles", 32, false, false},
      {"miner", 20, false, false},
      {"stateRoot", 32, false, false},
      {"transactionsRoot", 32, false, false},
      {"receiptsRoot", 32, false, false},
      {"logsBloom", 256, false, false},
      {"difficulty", 0, true, false},
      {"number", 0, true, false},
      {"gasLimit", 0, true, false},
      {"gasUsed", 0, true, false},
      {"timestamp", 0, true, false},
      {"extraData", 0, false, false},
      {"mixHash", 32, false, false},
      {"nonce", 8, false, false},
      {"baseFeePerGas", 0, true, true},
      {"withdrawalsRoot", 32, false, true},
      {"blobGasUsed", 0, true, true},
      {"excessBlobGas", 0, true, true},
      {"parentBeaconBlockRoot", 32, false, true},
      {"requestsHash", 32, false, true},
  };
  bool appending = true;
  for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
    if (fields[i].optional && !appending) continue;
    int rc = rlp_header_field(header, block, fields[i].name, fields[i].width, fields[i].quantity, fields[i].optional);
    if (rc < 0) return false;
    if (rc == 0) appending = false;
  }
  rlp_to_list(header);
  return header->data.len > 0 && header->data.len <= ARB_HEADER_MAX;
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
  buffer_t header = {0};
  bool     ok     = false;
  if (result.type == JSON_TYPE_STRING)
    ok = decode_hex_string(result, &header, ARB_HEADER_MAX);
  else if (result.type == JSON_TYPE_OBJECT)
    ok = rlp_from_block(result, &header);
  if (!ok || !header_matches(header.data, block_hash)) {
    buffer_free(&header);
    set_err(err, ok ? "raw header does not match the feed block hash" : "eth rpc did not return a block header");
    return NULL_BYTES;
  }

  ssz_builder_t proof = ssz_builder_for_type(ETH_SSZ_ARB_FEED_PROOF);
  if (!proof.def) {
    buffer_free(&header);
    set_err(err, "arbFeedProof type is not registered");
    return NULL_BYTES;
  }
  ssz_add_bytes(&proof, "elHeader", header.data);
  buffer_free(&header);
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

static bool result_is_raw_header(json_t root) {
  if (root.type != JSON_TYPE_OBJECT) return false;
  if (json_get(root, "error").type != JSON_TYPE_NOT_FOUND) return false;
  json_t result = json_get(root, "result");
  if (result.type != JSON_TYPE_STRING || result.len < 6 || !result.start) return false;
  return result.start[1] == '0' && (result.start[2] == 'x' || result.start[2] == 'X');
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

static json_t parse_rpc_body(bytes_t body, char** text) {
  *text = NULL;
  if (!body.data || body.len == 0) return (json_t) {0};
  // Curl responses are terminated past `len`. A memcached hit is a raw copy.
  *text = safe_malloc(body.len + 1);
  memcpy(*text, body.data, body.len);
  (*text)[body.len] = 0;
  return json_parse(*text);
}

static void arb_preconf_finish(arb_preconf_fetch_t* ctx, data_request_t* rpc, json_t root, bool parsed) {
  char*   err     = NULL;
  bytes_t encoded = parsed ? arb_preconf_encode(root, bytes(ctx->hash, 32), bytes(ctx->sig, 65), ctx->tail, &err) : NULL_BYTES;
  if (encoded.data)
    ctx->intern->req->response = encoded;
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
  arb_preconf_fetch_t* ctx    = data;
  char*                text   = NULL;
  json_t               root   = parse_rpc_body(rpc ? rpc->response : NULL_BYTES, &text);
  bool                 parsed = text != NULL;
  if (ctx->debug && (!parsed || !result_is_raw_header(root))) {
    if (parsed && debug_method_missing(root)) {
      server_list_t* list = c4_get_server_list(C4_DATA_TYPE_ETH_RPC, ctx->chain_id);
      c4_mark_method_unsupported(list, rpc->response_node_index, "debug_getRawHeader");
    }
    safe_free(text);
    c4_request_free(rpc);
    arb_preconf_fetch(ctx, false);
    return;
  }
  arb_preconf_finish(ctx, rpc, root, parsed);
  safe_free(text);
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
