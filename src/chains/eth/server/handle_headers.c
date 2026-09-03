/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "chains/eth/prover/bootstrap_gloas.h"
#include "handler.h"
#include "logger.h"
#include "prover/prover.h"
#include "util/bytes.h"
#include <stdlib.h>
#include <string.h>

// Callback for proxied requests
static void c4_proxy_callback(client_t* client, void* data, data_request_t* req) {
  // Check if client is still valid before responding
  if (!client || client->being_closed) {
    log_warn("Client is no longer valid or is being closed - discarding proxy response\n");
    // Clean up resources
    if (req) {
      safe_free(req->url);
      safe_free(req->response.data);
      safe_free(req->error);
      safe_free(req);
    }
    return;
  }

  if (req->response.data)
    c4_http_respond(client, 200, req->encoding == C4_DATA_ENCODING_SSZ ? "application/octet-stream" : "application/json", req->response);
  else
    c4_write_error_response(client, 500, req->error);
  safe_free(req->url);
  safe_free(req->response.data);
  safe_free(req->error);
  safe_free(req);
}

#ifdef PROVER_CACHE
// Short-circuit `GET /eth/v1/beacon/light_client/bootstrap/0x<block_root>`
// when the finalization-driven precompute has already cached that root.
//
// Returns `true` when the request was served from the cache (caller must
// stop, no upstream fetch), `false` to fall through to the standard proxy.
//
// Only serves the SSZ variant on purpose: the precompute stores raw SSZ
// bytes, so a client asking for JSON goes through the beacon proxy path.
static bool try_serve_cached_lc_bootstrap(client_t* client) {
  static const char bootstrap_prefix[] = "/eth/v1/beacon/light_client/bootstrap/";
  const size_t      prefix_len         = sizeof(bootstrap_prefix) - 1;

  if (strncmp(client->request.path, bootstrap_prefix, prefix_len) != 0) return false;

  // Client must have negotiated SSZ; JSON re-encodes the container, which
  // we do not do here. Exact-match (not the prefix `strncmp` used in
  // `c4_proxy`) so an ambiguous `Accept` (multiple media ranges, q-values,
  // trailing garbage like `application/octet-streamXYZ`) falls through to
  // the proxy path, which does its own content-type negotiation with
  // upstream. Losing a cache hit on an unusual `Accept` header is
  // strictly better than silently mis-serving.
  if (!client->request.accept ||
      strcmp(client->request.accept, "application/octet-stream") != 0)
    return false;

  // Path tail must be exactly "0x" + 64 hex chars. Reject anything else
  // (trailing slashes, query strings, alternate encodings) so we do not
  // silently mis-serve a request the upstream would have handled.
  const char* tail = client->request.path + prefix_len;
  if (strlen(tail) != 66 || tail[0] != '0' || tail[1] != 'x') return false;

  bytes32_t block_root = {0};
  if (hex_to_bytes(tail + 2, 64, bytes(block_root, 32)) != 32) return false;

  bytes32_t cache_key = {0};
  c4_gloas_bootstrap_cache_key(block_root, cache_key);

  // We need a heap-allocated scratch ctx: `c4_prover_cache_get` promotes
  // a global-cache hit into a local linked-list entry and bumps the
  // global entry's `use_counter` -- both must be reversed by
  // `c4_prover_free` (which frees the ctx itself) to avoid a per-request
  // leak and to release the eviction-pin on the global entry.
  prover_ctx_t* scratch = (prover_ctx_t*) safe_calloc(1, sizeof(prover_ctx_t));
  scratch->chain_id     = http_server.chain_id;
  const void* cached    = c4_prover_cache_get(scratch, cache_key);
  if (!cached) {
    c4_prover_free(scratch);
    return false;
  }

  // The cached value is a fixed-size Gloas LightClientBootstrap SSZ blob.
  // Copy it into a locally-owned buffer *before* releasing the scratch
  // ctx: `c4_prover_free` drops the `use_counter` back to zero and lets
  // the next cache cleanup evict (and free) the value, which would
  // otherwise UAF the buffer we hand to `uv_write`. The copy mirrors what
  // the proxy path does with `req->response.data`.
  uint8_t* body_copy = (uint8_t*) safe_malloc(C4_GLOAS_BOOTSTRAP_SIZE);
  memcpy(body_copy, cached, C4_GLOAS_BOOTSTRAP_SIZE);
  c4_prover_free(scratch);

  c4_http_respond(client, 200, "application/octet-stream",
                  bytes(body_copy, C4_GLOAS_BOOTSTRAP_SIZE));
  safe_free(body_copy);
  return true;
}
#endif

bool c4_proxy(client_t* client) {
  const char* path_headers              = "/eth/v1/beacon/headers/";
  const char* path_lightclient          = "/eth/v1/beacon/light_client";
  const char* path_finality_checkpoints = "/eth/v1/beacon/states/head/finality_checkpoints";

  if (strncmp(client->request.path, path_headers, strlen(path_headers)) != 0 && strncmp(client->request.path, path_lightclient, strlen(path_lightclient)) != 0 && strncmp(client->request.path, path_finality_checkpoints, strlen(path_finality_checkpoints)) != 0) return false;

#ifdef PROVER_CACHE
  // Fast path: precomputed Gloas bootstrap for the current finalized
  // checkpoint (see `c4_precompute_finalized_gloas_bootstrap` in
  // `head_update.c`). Falls through on miss.
  if (try_serve_cached_lc_bootstrap(client)) return true;
#endif

  data_request_t* req = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
  req->url            = strdup(client->request.path + 1);
  req->method         = C4_DATA_METHOD_GET;
  req->chain_id       = http_server.chain_id;
  req->type           = C4_DATA_TYPE_BEACON_API;
  req->encoding       = C4_DATA_ENCODING_JSON;

  if (client->request.accept && strncmp(client->request.accept, "application/octet-stream", strlen("application/octet-stream")) == 0)
    req->encoding = C4_DATA_ENCODING_SSZ;
  c4_add_request(client, req, NULL, c4_proxy_callback);
  return true;
}
