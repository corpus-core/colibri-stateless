/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "eth_conf.h"
#include "eth_verify.h"
#include "handler.h"
#include "logger.h"
#include "period_store.h"
#include "server.h"
#include "sync_committee.h"
#include "uv_util.h"
#include <stdlib.h>
#include <string.h>

#define MAX_BACKFILL_PERIODS 10
#define THROW(message)                             \
  do {                                             \
    c4_write_error_response(client, 500, message); \
    return;                                        \
  } while (0)

static bool c4_get_query_address(char* path, char* param, address_t addr) {
  char* query = strstr(path, "?");
  if (!query) return false;
  query += 1;
  char* found = strstr(query, param);
  if (!found) return false;
  found += strlen(param);
  if (*found == '=')
    found++;
  else
    return false;

  char tmp[50] = {0};
  int  len     = 0;
  for (int i = 0; i < sizeof(tmp); i++, len++) {
    if (!found[i] || found[i] == '&') break;
    tmp[i] = found[i];
  }
  return hex_to_bytes(tmp, len, bytes(addr, ADDRESS_SIZE)) == ADDRESS_SIZE;
}

typedef struct {
  client_t* client;
  address_t addr;
  uint64_t  periods[MAX_BACKFILL_PERIODS];
  int       num_periods;
  json_t    payload;
} checkpoints_ctx_t;

static bool get_checkpoint_from_proof(bytes_t proof_data, bytes32_t checkpoint, uint64_t* slot) {
  if (proof_data.len < 25000) return false; //  make sure we have a min len
  ssz_ob_t proof  = {.bytes = proof_data, .def = C4_ETH_REQUEST_SYNCDATA_UNION + 2};
  ssz_ob_t cp     = ssz_get(&proof, "checkpoint");
  ssz_ob_t header = ssz_get(&cp, "header");
  *slot           = ssz_get_uint64(&header, "slot");
  ssz_hash_tree_root(header, checkpoint);
  return true;
}

static void missing_checkpoints_cb(void* user_data, file_data_t* files, int num_files) {
  checkpoints_ctx_t* ctx    = (checkpoints_ctx_t*) user_data;
  buffer_t           buffer = {0};
  buffer_add_chars(&buffer, "[");
  for (int i = 0; i < num_files; i++) {
    if (files[i].error) {
      log_error("Failed to read file when finding missing checkpoints: %s", files[i].error);
      continue;
    }
    bytes32_t checkpoint = {0};
    uint64_t  slot       = 0;
    if (!get_checkpoint_from_proof(files[i].data, checkpoint, &slot)) {
      log_error("Failed to get checkpoint from lcu: %l", ctx->periods[i]);
      continue;
    }
    if (i > 0) buffer_add_chars(&buffer, ",");
    bprintf(&buffer, "{\"period\":%l,\"slot\":%l,\"root\":\"0x%x\"}", ctx->periods[i], slot, bytes(checkpoint, 32));
  }
  buffer_add_chars(&buffer, "]");
  c4_http_respond(ctx->client, 200, "application/json", buffer.data);
  buffer_free(&buffer);
  c4_file_data_array_free(files, num_files, 1);
  safe_free(ctx);
}

static void find_missing_checkpoints(client_t* client) {
  char              sigfile[50]                 = {0};
  uint64_t          first_period                = 0;
  uint64_t          last_period                 = 0;
  file_data_t       files[MAX_BACKFILL_PERIODS] = {0};
  checkpoints_ctx_t ctx                         = {.client = client};

  if (!eth_config.period_store) THROW("Period store not configured");
  if (!c4_get_query_address(client->request.path, "signer", ctx.addr)) THROW("Invalid or missing signer address as query parameter");
  if (!c4_ps_period_index_get_contiguous_from(0, &first_period, &last_period)) THROW("Failed to get contiguous periods");
  sbprintf(sigfile, "sig_%x", bytes(ctx.addr, ADDRESS_SIZE));

  for (uint64_t period = last_period; period >= first_period && period > last_period - MAX_BACKFILL_PERIODS; period--) {
    if (c4_ps_file_exists(period, sigfile)) break;
    if (!c4_ps_file_exists(period, "zk_proof.ssz")) continue; // no bootstrap, nothing to sign
    ctx.periods[ctx.num_periods]  = period;
    files[ctx.num_periods++].path = bprintf(NULL, "%s/%l/zk_proof.ssz", eth_config.period_store, period);
  }

  if (ctx.num_periods == 0) {
    c4_http_respond(client, 200, "application/json", bytes("[]", 2));
    return;
  }

  c4_read_files_uv(bytes_dup(bytes(&ctx, sizeof(ctx))).data, missing_checkpoints_cb, files, ctx.num_periods);
}

static void add_missing_checkpoints_write_done_cb(void* user_data, file_data_t* files, int num_files) {
  checkpoints_ctx_t* ctx = (checkpoints_ctx_t*) user_data;
  for (int i = 0; i < num_files; i++) {
    if (files[i].error) {
      log_error("Failed to write file when adding missing checkpoints for %s : %s", files[i].path, files[i].error);
      continue;
    }
  }

  c4_file_data_array_free(files, num_files, 1);
  c4_http_respond(ctx->client, 200, "application/json", bytes("{\"success\":\"Checkpoints added\"}", 22));
  safe_free(ctx);
}

static void add_missing_checkpoints_cb(void* user_data, file_data_t* files, int num_files) {
  checkpoints_ctx_t* ctx                               = (checkpoints_ctx_t*) user_data;
  file_data_t        write_files[MAX_BACKFILL_PERIODS] = {0};
  int                num_write_files                   = 0;

  for (int i = 0; i < num_files; i++) {
    if (files[i].error) {
      log_error("Failed to read file when adding missing checkpoints: %s", files[i].error);
      continue;
    }
    uint8_t   signature[65] = {0};
    bytes32_t checkpoint    = {0};
    uint64_t  slot          = 0;
    bytes32_t hash          = {0};
    bytes32_t digest        = {0};
    uint8_t   pub_key[64]   = {0};
    bool      found         = false;
    json_for_each_value(ctx->payload, item) {
      uint64_t period = json_get_uint64(item, "period");
      if (period == ctx->periods[i]) {
        found = true;
        json_to_bytes(json_get(item, "signature"), bytes(signature, 65));
        break;
      }
    }
    if (!found) continue;
    if (!get_checkpoint_from_proof(files[i].data, checkpoint, &slot)) {
      log_error("Failed to get checkpoint from proof: %l", ctx->periods[i]);
      continue;
    }

    c4_eth_eip191_digest_32(checkpoint, digest);
    if (!secp256k1_recover(digest, bytes(signature, 65), pub_key)) {
      log_error("Failed to recover public key from signature: 0x%x for checkpoint: 0x%x in period:%l", bytes(signature, 65), bytes(checkpoint, 32), ctx->periods[i]);
      continue;
    }
    keccak(bytes(pub_key, 64), hash);
    write_files[num_write_files].data   = bytes_dup(bytes(signature, 65));
    write_files[num_write_files++].path = bprintf(NULL, "%s/%l/sig_%x", eth_config.period_store, ctx->periods[i], bytes(hash + 12, 20));
  }

  safe_free((void*) ctx->payload.start);
  ctx->payload = (json_t) {0};

  if (num_write_files == 0) {
    c4_write_error_response(ctx->client, 400, "No signatures to add");
    c4_file_data_array_free(write_files, num_write_files, 1);
    safe_free(ctx);
    return;
  }

  c4_write_files_uv(ctx, add_missing_checkpoints_write_done_cb, write_files, num_write_files, O_WRONLY | O_CREAT, 0666);
}

static void add_missing_checkpoints(client_t* client) {
  uint64_t    first_period                = 0;
  uint64_t    last_period                 = 0;
  file_data_t files[MAX_BACKFILL_PERIODS] = {0};

  if (!eth_config.period_store) THROW("Period store not configured");
  if (!c4_ps_period_index_get_contiguous_from(0, &first_period, &last_period)) THROW("Failed to get contiguous periods");

  char* payload_json = safe_malloc(client->request.payload_len + 1);
  memcpy(payload_json, client->request.payload, client->request.payload_len);
  payload_json[client->request.payload_len] = 0;
  checkpoints_ctx_t* ctx                    = safe_calloc(1, sizeof(checkpoints_ctx_t));
  ctx->client                               = client;
  ctx->payload                              = json_parse((const char*) payload_json);
  ctx->num_periods                          = 0;
  // Ownership of `payload_json` is transferred to `ctx->payload.start` and will be freed
  // once we no longer need the JSON (see add_missing_checkpoints_cb / error paths below).
  payload_json = NULL;

  if (ctx->payload.type != JSON_TYPE_ARRAY || json_len(ctx->payload) > MAX_BACKFILL_PERIODS) {
    c4_http_respond(client, 400, "application/json", bytes("{\"error\":\"Invalid payload\"}", 22));
    safe_free((void*) ctx->payload.start);
    ctx->payload = (json_t) {0};
    safe_free(ctx);
    return;
  }

  json_for_each_value(ctx->payload, item) {
    uint64_t period = json_get_uint64(item, "period");
    if (period < first_period || period > last_period || !c4_ps_file_exists(period, "zk_proof.ssz")) continue;
    ctx->periods[ctx->num_periods] = period;
    files[ctx->num_periods++].path = bprintf(NULL, "%s/%l/zk_proof.ssz", eth_config.period_store, period);
  }

  if (ctx->num_periods == 0) {
    c4_write_error_response(client, 400, "No signatures to add");
    safe_free((void*) ctx->payload.start);
    ctx->payload = (json_t) {0};
    safe_free(ctx);
    return;
  }

  c4_read_files_uv(ctx, add_missing_checkpoints_cb, files, ctx->num_periods);
}

bool c4_handle_checkpoints(client_t* client) {
  const char* path_headers = "/signed_checkpoints";
  if (strncmp(client->request.path, path_headers, strlen(path_headers)) != 0) return false;

  if (client->request.method == C4_DATA_METHOD_GET) {
    find_missing_checkpoints(client);
    return true;
  }

  if (client->request.method == C4_DATA_METHOD_POST) {
    add_missing_checkpoints(client);
    return true;
  }

  return false;
}
