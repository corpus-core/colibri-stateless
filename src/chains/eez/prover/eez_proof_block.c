/*
 * Copyright (c) 2025,2026 corpus.core GmbH
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

#include "beacon.h"
#include "beacon_types.h"
#include "bytes.h"
#include "crypto.h"
#include "eez_chains_conf.h"
#include "eez_prover.h"
#include "eez_verify.h"
#include "el_header.h"
#include "eth_req.h"
#include "eth_tools.h"
#include "json.h"
#include "prover.h"
#include "ssz.h"
#include "version.h"
#include <string.h>

// Cache freshness bound of the L1 prover request in seconds (one L1 slot).
#define EEZ_L1_PROOF_TTL 12

// Length of a quoted 32-byte hex string: `"0x` + 64 nibbles + `"`.
#define JSON_HASH_LEN 68

// Longest quoted hex block number: `"0x` + 16 nibbles (uint64) + `"`.
#define JSON_MAX_NUMBER_LEN 20

// Per-request snapshot of a resolved anchor block. `el_header` and `el_body.bytes` live in
// the tail of the same allocation; `account` and `sync_data` point into the response of
// the L1 prover request, which the prover state owns for the lifetime of the context.
typedef struct {
  bytes_t   el_header;     // RLP header of the anchor block
  ssz_ob_t  el_body;       // transactions + withdrawals, `def == NULL` when not requested
  bytes32_t el_block_hash; // hash of the anchor block as stored on L1
  ssz_ob_t  account;       // `EthAccountProof` of the L1 prover response
  ssz_ob_t  sync_data;     // `sync_data` of the L1 prover response
} eez_el_snap_t;

/**
 * Compares a block identifier (JSON string token including its quotes) with a literal.
 *
 * @param block JSON block identifier
 * @param literal expected token including the quotes, e.g. `"\"safe\""`
 * @return `true` if the token is a string equal to `literal`
 */
static bool block_is_tag(json_t block, const char* literal) {
  size_t literal_len = strlen(literal);
  return block.type == JSON_TYPE_STRING && block.len == literal_len && strncmp(block.start, literal, literal_len) == 0;
}

/**
 * Checks whether a block identifier is a block hash or a block number.
 *
 * @param block JSON block identifier
 * @return `true` for a quoted hex string (`"0x..."`) that is either a 32-byte hash or a number of 1 to 16 nibbles
 */
static bool block_is_hex(json_t block) {
  if (block.type != JSON_TYPE_STRING || block.len <= 4 || block.start[1] != '0' || block.start[2] != 'x') return false;
  if (block.len != JSON_HASH_LEN && block.len > JSON_MAX_NUMBER_LEN) return false;
  for (size_t i = 3; i + 1 < block.len; i++) {
    char c = block.start[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
  }
  return true;
}

/**
 * Checks whether the block identifier asks for the newest provable block.
 *
 * @param block JSON block identifier
 * @return `true` for `latest` and for a missing identifier
 */
static bool block_is_latest(json_t block) {
  return block.type == JSON_TYPE_NOT_FOUND || block.type == JSON_TYPE_NULL || !block.start || block_is_tag(block, "\"latest\"");
}

/**
 * Checks whether the block identifier is a tag that resolves to the anchor.
 *
 * @param block JSON block identifier
 * @return `true` for `latest`, a missing identifier, `safe` and `finalized`
 */
static bool block_is_anchor_tag(json_t block) {
  return block_is_latest(block) || block_is_tag(block, "\"safe\"") || block_is_tag(block, "\"finalized\"");
}

/**
 * Maps the L2 block identifier to the block tag of the L1 request.
 *
 * `safe` and `finalized` are finality statements of L1 and are passed on. Everything
 * else (including a concrete block number or hash) reads the newest anchor.
 *
 * @param block JSON block identifier of the L2 request
 * @return block tag for the L1 `eth_getStorageAt` request
 */
static const char* l1_block_tag(json_t block) {
  if (block_is_tag(block, "\"safe\"")) return "safe";
  if (block_is_tag(block, "\"finalized\"")) return "finalized";
  return "latest";
}

/**
 * Derives the snapshot key for a resolved block.
 *
 * @param block JSON block identifier
 * @param with_body whether the snapshot holds the block body
 * @param id receives the 32-byte key
 */
static void snap_id(json_t block, bool with_body, bytes32_t id) {
  buffer_t buf = {0};
  bprintf(&buf, "eez_block:%d:", (uint32_t) (with_body ? 1 : 0));
  if (block.start && block.len) buffer_append(&buf, bytes((uint8_t*) block.start, (uint32_t) block.len));
  keccak(buf.data, id);
  buffer_free(&buf);
}

/**
 * Fills the output block from a snapshot.
 *
 * @param out output block
 * @param snap snapshot owned by the prover state
 */
static void fill_from_snap(eth_block_t* out, const eez_el_snap_t* snap) {
  memset(out, 0, sizeof(*out));
  out->proof_type    = C4_BLOCK_PROOF_TYPE_EEZ;
  out->el_header     = snap->el_header;
  out->el_body       = snap->el_body;
  out->slot          = eth_el_header_get_uint64(snap->el_header, EL_BLOCK_NUMBER);
  out->eez.account   = snap->account;
  out->eez.sync_data = snap->sync_data;
  memcpy(out->el_block_hash, snap->el_block_hash, 32);
}

/**
 * Stores a resolved block as a snapshot in the prover state and fills the output block.
 * Takes ownership of `src.el_header.data` and `src.el_body.bytes.data`.
 *
 * @param ctx prover context
 * @param id snapshot key
 * @param src resolved block with heap-owned header and body
 * @param out output block
 */
static void attach_snap(prover_ctx_t* ctx, bytes32_t id, eez_el_snap_t src, eth_block_t* out) {
  uint32_t       body_len = src.el_body.bytes.data ? src.el_body.bytes.len : 0;
  uint32_t       size     = (uint32_t) sizeof(eez_el_snap_t) + src.el_header.len + body_len;
  eez_el_snap_t* snap     = safe_calloc(1, size);
  uint8_t*       tail     = (uint8_t*) (snap + 1);

  *snap = src;
  memcpy(tail, src.el_header.data, src.el_header.len);
  snap->el_header = bytes(tail, src.el_header.len);
  if (body_len) {
    memcpy(tail + src.el_header.len, src.el_body.bytes.data, body_len);
    snap->el_body.bytes = bytes(tail + src.el_header.len, body_len);
  }
  safe_free(src.el_header.data);
  safe_free(src.el_body.bytes.data);

  c4_state_cache_set(&ctx->state, id, bytes((uint8_t*) snap, size));
  fill_from_snap(out, snap);
}

/**
 * Requests the proof of the root slot from the prover of the L1 chain and returns the
 * validated response.
 *
 * The request is a `C4_DATA_TYPE_PROVER` request with the L1 chain id, so the host routes
 * it to an L1 prover. It carries the client state of the L2 request: the sync committee
 * state of an EEZ client is the one of its L1, and the L1 prover answers with the sync
 * data that client needs. `last_block_hash` is not sent, so the L1 block is always proven
 * by a full `clProof`.
 *
 * @param ctx prover context
 * @param conf configuration of the rollup
 * @param l1_tag block tag of the L1 request (`latest`, `safe` or `finalized`)
 * @param response receives the `C4Request` of the L1 prover (borrowed from the prover state)
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`
 */
static c4_status_t fetch_l1_anchor_proof(prover_ctx_t* ctx, const eez_chain_config_t* conf, const char* l1_tag, ssz_ob_t* response) {
  bytes32_t slot    = {0};
  bytes32_t id      = {0};
  buffer_t  payload = {0};

  eez_root_slot(conf->rollup_id, slot);
  bprintf(&payload, "{\"method\":\"eth_getStorageAt\",\"params\":[\"0x%x\",\"0x%x\",\"%s\"]", bytes((uint8_t*) conf->proxy, ADDRESS_SIZE), bytes(slot, 32), l1_tag);
  // The id derives from method + params only, so it is stable across re-entries.
  c4_chain_request_id(id, conf->l1_chain_id, payload.data);

  data_request_t* req = c4_state_get_data_request_by_id(&ctx->state, id);
  if (!req) {
    // Common remote-prover fields, written here instead of using
    // `c4_append_prover_request_props`: that helper falls back to the client state in the
    // local storage, which on a prover server belongs to somebody else.
    //
    // `version` is the version of this build, not `ctx->version`: the response is embedded
    // into a proof of this build, so it must use this wire format. A prover answers older
    // versions in their legacy format (and a prover context without a client version
    // defaults to an old one).
    bprintf(&payload, ",\"version\":%d", c4_current_version_number());
    if (ctx->client_state.data && ctx->client_state.len)
      bprintf(&payload, ",\"c4\":\"0x%x\"", ctx->client_state);
    if (ctx->flags & C4_PROVER_FLAG_ZK_PROOF)
      bprintf(&payload, ",\"zk_proof\":true");
    // `witness_key` holds the signer addresses of the client (n * 20 bytes). On a server
    // it may instead hold the 32-byte signing key of that server, which must never be sent.
    if (ctx->witness_key.data && ctx->witness_key.len && ctx->witness_key.len % ADDRESS_SIZE == 0)
      bprintf(&payload, ",\"signers\":\"0x%x\"", ctx->witness_key);
    bprintf(&payload, "}");

    req           = safe_calloc(1, sizeof(data_request_t));
    req->type     = C4_DATA_TYPE_PROVER;
    req->chain_id = conf->l1_chain_id;
    req->method   = C4_DATA_METHOD_POST;
    req->encoding = C4_DATA_ENCODING_SSZ;
    req->payload  = payload.data;
    req->ttl      = EEZ_L1_PROOF_TTL;
    memcpy(req->id, id, 32);
    c4_state_add_request(&ctx->state, req);
    return C4_PENDING;
  }

  buffer_free(&payload);
  if (c4_state_is_pending(req)) return C4_PENDING;
  if (req->error) THROW_ERROR_WITH("L1 prover request for the EEZ anchor failed: %s", req->error);
  // a prover answers errors with a JSON object instead of an SSZ request
  if (req->response.len && req->response.data[0] == '{') THROW_ERROR_WITH("L1 prover rejected the EEZ anchor request: %r", req->response);

  *response = (ssz_ob_t) {.def = eth_ssz_verification_type(ETH_SSZ_VERIFY_REQUEST), .bytes = req->response};
  if (!req->validated) {
    if (!ssz_is_valid(*response, true, &ctx->state)) THROW_ERROR_WITH(" (invalid response of the L1 prover)");
    req->validated = true;
  }
  return C4_SUCCESS;
}

/**
 * Reads the anchor (the L2 block hash stored on L1) from the response of the L1 prover.
 *
 * The prover does not verify the L1 consensus proof; it only checks that the storage
 * proof is consistent with the state root of the L1 header in the response. A lying L1
 * prover therefore yields a proof the verifier rejects, never an accepted wrong block.
 *
 * @param ctx prover context
 * @param conf configuration of the rollup
 * @param response validated `C4Request` of the L1 prover
 * @param account receives the `EthAccountProof` of the response
 * @param anchor receives the 32-byte hash of the anchor block
 * @return `C4_SUCCESS` or `C4_ERROR`
 */
static c4_status_t read_anchor(prover_ctx_t* ctx, const eez_chain_config_t* conf, ssz_ob_t response, ssz_ob_t* account, bytes32_t anchor) {
  *account = ssz_get(&response, "proof");
  if (!ssz_is_type(account, eth_ssz_verification_type(ETH_SSZ_VERIFY_ACCOUNT_PROOF)))
    THROW_ERROR("L1 prover did not return an account proof for the EEZ anchor");

  ssz_ob_t l1_block = ssz_get(account, "elProof");
  if (!l1_block.def || !l1_block.def->name || strcmp(l1_block.def->name, "clProof") != 0)
    THROW_ERROR("L1 prover did not prove the L1 block of the EEZ anchor with a clProof");

  bytes_t l1_header = ssz_get(&l1_block, "elHeader").bytes;
  TRY_ASYNC(eez_get_l1_storage_value(&ctx->state, conf, *account, eth_el_header_get(l1_header, EL_STATE_ROOT), anchor));
  if (bytes_all_zero(bytes(anchor, 32))) THROW_ERROR_WITH("no L2 block hash is stored on L1 for %s", conf->name);
  return C4_SUCCESS;
}

/**
 * Picks the header layout of an `eth_getBlockByHash` result from the fields it carries.
 *
 * @param block JSON block object
 * @return fork whose header layout matches the object
 */
static fork_id_t header_fork(json_t block) {
  if (json_get(block, EL_SLOT_NUMBER).type != JSON_TYPE_NOT_FOUND || json_get(block, EL_BLOCK_ACCESS_LIST_HASH).type != JSON_TYPE_NOT_FOUND) return C4_FORK_GLOAS;
  if (json_get(block, EL_REQUESTS_HASH).type != JSON_TYPE_NOT_FOUND) return C4_FORK_ELECTRA;
  return C4_FORK_DENEB;
}

/**
 * Fetches the header (and body) of the anchor block from the L2 execution node and
 * checks that the header hashes to the anchor.
 *
 * With a body the raw block is used (`debug_getRawBlock`), which yields the exact header
 * bytes. Without a body the header is rebuilt from `eth_getBlockByHash`.
 *
 * @param ctx prover context
 * @param anchor 32-byte hash of the anchor block
 * @param with_body whether transactions and withdrawals are needed
 * @param el_header receives the heap-owned RLP header
 * @param el_body receives the heap-owned body when `with_body` is set
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`
 */
static c4_status_t fetch_l2_block(prover_ctx_t* ctx, bytes32_t anchor, bool with_body, bytes_t* el_header, ssz_ob_t* el_body) {
  bytes32_t hash = {0};
  *el_header     = NULL_BYTES;
  *el_body       = (ssz_ob_t) {0};

  if (with_body) {
    bytes_t raw_block = NULL_BYTES;
    TRY_ASYNC(eth_debug_get_raw_block(ctx, anchor, &raw_block));
    ssz_builder_t body_builder = ssz_builder_for_type(ETH_SSZ_EL_BLOCK_CONTENT);
    // on failure `el_header` may point into `raw_block` or to freed memory: never free it here
    if (eth_el_header_get_from_raw_block(&ctx->state, raw_block, el_header, &body_builder) != C4_SUCCESS) {
      ssz_builder_free(&body_builder);
      *el_header = NULL_BYTES;
      return C4_ERROR;
    }
    *el_body = ssz_builder_to_bytes(&body_builder);
  }
  else {
    char   hash_json[JSON_HASH_LEN + 2] = {0};
    json_t block                        = {0};
    sbprintf(hash_json, "\"0x%x\"", bytes(anchor, 32));
    TRY_ASYNC(eth_get_block(ctx, (json_t) {.start = hash_json, .len = JSON_HASH_LEN, .type = JSON_TYPE_STRING}, false, &block));
    if (block.type != JSON_TYPE_OBJECT) THROW_ERROR_WITH("the L2 node does not know the block 0x%x anchored on L1", bytes(anchor, 32));
    TRY_ASYNC(eth_el_header_build_from_json(&ctx->state, el_header, header_fork(block), block));
  }

  keccak(*el_header, hash);
  if (memcmp(hash, anchor, 32) != 0) {
    safe_free(el_header->data);
    safe_free(el_body->bytes.data);
    *el_header = NULL_BYTES;
    *el_body   = (ssz_ob_t) {0};
    THROW_ERROR_WITH("the L2 header does not hash to the block 0x%x anchored on L1", bytes(anchor, 32));
  }
  return C4_SUCCESS;
}

/**
 * Checks that the requested block is the anchor.
 *
 * Tags always resolve to the anchor. A block hash or number must be the anchor itself;
 * for any other block the error states why it cannot be proven yet.
 *
 * @param ctx prover context
 * @param conf configuration of the rollup
 * @param block JSON block identifier of the request
 * @param anchor 32-byte hash of the anchor block
 * @param anchor_number block number of the anchor block
 * @return `C4_SUCCESS`, `C4_PENDING` (number of a requested hash is being fetched), or `C4_ERROR`
 */
static c4_status_t check_requested_block(prover_ctx_t* ctx, const eez_chain_config_t* conf, json_t block, bytes32_t anchor, uint64_t anchor_number) {
  uint64_t requested = 0;

  // anything else than a tag is a hash or a number: `eez_get_el_block` rejected the rest
  if (block_is_anchor_tag(block)) return C4_SUCCESS;

  if (block.len == JSON_HASH_LEN) {
    bytes32_t hash     = {0};
    buffer_t  hash_buf = stack_buffer(hash);
    json_t    header   = {0};
    if (json_as_bytes(block, &hash_buf).len == 32 && memcmp(hash, anchor, 32) == 0) return C4_SUCCESS;
    // the number of the requested block is only needed to explain the error
    TRY_ASYNC(eth_get_block(ctx, block, false, &header));
    if (header.type != JSON_TYPE_OBJECT) THROW_ERROR_WITH("block %J not found on %s", block, conf->name);
    requested = json_get_uint64(header, "number");
    if (requested == anchor_number) THROW_ERROR_WITH("block %J is not part of the chain anchored on L1 (block %l has hash 0x%x)", block, anchor_number, bytes(anchor, 32));
  }
  else {
    requested = json_as_uint64(block);
    if (requested == anchor_number) return C4_SUCCESS;
  }

  if (requested > anchor_number)
    THROW_ERROR_WITH("block %l is newer than the last block of %s proven on L1 (%l); blocks after the L1 anchor are not supported yet", requested, conf->name, anchor_number);
  if (anchor_number - requested <= EEZ_EIP2935_HISTORY_WINDOW)
    THROW_ERROR_WITH("block %l is older than the last block of %s proven on L1 (%l); it needs an EIP-2935 proof, which is not supported yet", requested, conf->name, anchor_number);
  THROW_ERROR_WITH("block %l is more than %d blocks older than the last block of %s proven on L1 (%l); it needs a historic proof, which is not supported yet", requested, (uint32_t) EEZ_EIP2935_HISTORY_WINDOW, conf->name, anchor_number);
}

c4_status_t eez_get_el_block(prover_ctx_t* ctx, json_t block, eth_block_t* out, bool with_body) {
  const eez_chain_config_t* conf = eez_get_chain_config(ctx->chain_id);
  bytes32_t                 id   = {0};
  eez_el_snap_t             snap = {0};
  ssz_ob_t                  l1   = {0};
  if (!conf) THROW_ERROR("unsupported EEZ chain!");
  // `pending`, `earliest` and malformed identifiers have no proof: fail before any request is sent
  if (!block_is_anchor_tag(block) && !block_is_hex(block)) THROW_ERROR_WITH("block %J cannot be proven on %s", block, conf->name);

  snap_id(block, with_body, id);
  bytes_t existing = c4_state_cache_get(&ctx->state, id);
  if (existing.data) {
    fill_from_snap(out, (eez_el_snap_t*) existing.data);
    return C4_SUCCESS;
  }

  TRY_ASYNC(fetch_l1_anchor_proof(ctx, conf, l1_block_tag(block), &l1));
  TRY_ASYNC(read_anchor(ctx, conf, l1, &snap.account, snap.el_block_hash));
  snap.sync_data = ssz_get(&l1, "sync_data");

  TRY_ASYNC(fetch_l2_block(ctx, snap.el_block_hash, with_body, &snap.el_header, &snap.el_body));
  c4_status_t status = check_requested_block(ctx, conf, block, snap.el_block_hash, eth_el_header_get_uint64(snap.el_header, EL_BLOCK_NUMBER));
  if (status != C4_SUCCESS) {
    safe_free(snap.el_header.data);
    safe_free(snap.el_body.bytes.data);
    return status;
  }

  attach_snap(ctx, id, snap, out);
  return C4_SUCCESS;
}

bool eez_add_block_proof(prover_ctx_t* ctx, ssz_builder_t* builder, eth_block_t* block_data, blockroot_proof_t* historic) {
  (void) ctx;
  (void) historic;
  if (block_data->proof_type != C4_BLOCK_PROOF_TYPE_EEZ || !block_data->el_header.data || !block_data->eez.account.bytes.data)
    return false;

  ssz_builder_t proof = ssz_builder_for_type(ETH_SSZ_EEZ_L1_PROOF);
  ssz_add_bytes(&proof, "elHeader", block_data->el_header);
  ssz_add_bytes(&proof, "account", block_data->eez.account.bytes);
  ssz_add_builders(builder, "elProof", proof);
  return true;
}

void eez_register_block_proof_prover(void) {
  c4_register_block_proof_prover(C4_CHAIN_TYPE_EEZ, eez_add_block_proof, eez_get_el_block);
}
