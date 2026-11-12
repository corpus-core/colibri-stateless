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
#include "eip2935.h"
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

// Error for a block hash that is not on the chain L1 committed to: block identifier, block number, hash of the anchored chain at that number.
#define EEZ_ERR_NOT_ANCHORED "block %J is not part of the chain anchored on L1 (block %l has hash 0x%x)"

// Per-request snapshot of a resolved block. `el_header`, `el_body.bytes` and `anchor_header`
// live in the tail of the same allocation; `account`, `sync_data` and `history_proof` point
// into responses of data requests, which the prover state owns for the lifetime of the context.
typedef struct {
  bytes_t   el_header;     // RLP header of the resolved block (the anchor, or a block before it)
  ssz_ob_t  el_body;       // transactions + withdrawals, `def == NULL` when not requested
  bytes32_t el_block_hash; // hash of the resolved block: the hash of the anchor block as stored on L1, or the hash of a block before it as stored in the EIP-2935 history contract
  ssz_ob_t  account;       // `EthAccountProof` of the L1 prover response
  ssz_ob_t  sync_data;     // `sync_data` of the L1 prover response
  bytes_t   anchor_header; // RLP header of the anchor, only set for a block before the anchor
  json_t    history_proof; // `eth_getProof` of the EIP-2935 history contract at the anchor, only set with `anchor_header`
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
  out->proof_type                = C4_BLOCK_PROOF_TYPE_EEZ;
  out->el_header                 = snap->el_header;
  out->el_body                   = snap->el_body;
  out->slot                      = eth_el_header_get_uint64(snap->el_header, EL_BLOCK_NUMBER);
  out->eez.account               = snap->account;
  out->eez.sync_data             = snap->sync_data;
  out->eez.history.newer_header  = snap->anchor_header;
  out->eez.history.history_proof = snap->history_proof;
  memcpy(out->el_block_hash, snap->el_block_hash, 32);
}

/**
 * Stores a resolved block as a snapshot in the prover state and fills the output block.
 * Takes ownership of `src.el_header.data`, `src.el_body.bytes.data` and `src.anchor_header.data`.
 *
 * @param ctx prover context
 * @param id snapshot key
 * @param src resolved block with heap-owned header, body and anchor header
 * @param out output block
 */
static void attach_snap(prover_ctx_t* ctx, bytes32_t id, eez_el_snap_t src, eth_block_t* out) {
  uint32_t       body_len   = src.el_body.bytes.data ? src.el_body.bytes.len : 0;
  uint32_t       anchor_len = src.anchor_header.data ? src.anchor_header.len : 0;
  uint32_t       size       = (uint32_t) sizeof(eez_el_snap_t) + src.el_header.len + body_len + anchor_len;
  eez_el_snap_t* snap       = safe_calloc(1, size);
  uint8_t*       tail       = (uint8_t*) (snap + 1);

  *snap = src;
  memcpy(tail, src.el_header.data, src.el_header.len);
  snap->el_header = bytes(tail, src.el_header.len);
  tail += src.el_header.len;
  if (body_len) {
    memcpy(tail, src.el_body.bytes.data, body_len);
    snap->el_body.bytes = bytes(tail, body_len);
    tail += body_len;
  }
  if (anchor_len) {
    memcpy(tail, src.anchor_header.data, anchor_len);
    snap->anchor_header = bytes(tail, anchor_len);
  }
  safe_free(src.el_header.data);
  safe_free(src.el_body.bytes.data);
  safe_free(src.anchor_header.data);

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
  bytes32_t  slot    = {0};
  bytes32_t  id      = {0};
  buffer_t   payload = {0};
  chain_id_t l1      = c4_get_l1_chain_id_for_l2(conf->chain_id);

  eez_root_slot(conf->rollup_id, slot);
  bprintf(&payload, "{\"method\":\"eth_getStorageAt\",\"params\":[\"0x%x\",\"0x%x\",\"%s\"]", bytes((uint8_t*) conf->proxy, ADDRESS_SIZE), bytes(slot, 32), l1_tag);
  // The id derives from method + params only, so it is stable across re-entries.
  c4_chain_request_id(id, l1, payload.data);

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
    req->chain_id = l1;
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
    if (!ssz_is_valid(*response, true, &ctx->state)) THROW_ERROR(" (invalid response of the L1 prover)");
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
  if (!l1_block.def || !l1_block.def->name || strcmp(l1_block.def->name, ETH_SSZ_NAME_CL_PROOF) != 0)
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
 * Releases the header and the body of a fetched L2 block and resets both.
 *
 * @param el_header heap-owned RLP header
 * @param el_body heap-owned body, `bytes.data == NULL` when there is none
 */
static void free_l2_block(bytes_t* el_header, ssz_ob_t* el_body) {
  safe_free(el_header->data);
  safe_free(el_body->bytes.data);
  *el_header = NULL_BYTES;
  *el_body   = (ssz_ob_t) {0};
}

/**
 * Fetches the header (and body) of a block from the L2 execution node and checks that
 * the header hashes to the expected block hash. The hash is one the prover already
 * trusts: the anchor stored on L1, or a hash read from the EIP-2935 history contract.
 *
 * With a body the raw block is used (`debug_getRawBlock`), which yields the exact header
 * bytes. Without a body the header is rebuilt from `eth_getBlockByHash`.
 *
 * @param ctx prover context
 * @param block_hash 32-byte hash of the block (the anchor or a block before it)
 * @param with_body whether transactions and withdrawals are needed
 * @param el_header receives the heap-owned RLP header
 * @param el_body receives the heap-owned body when `with_body` is set
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`
 */
static c4_status_t fetch_l2_block(prover_ctx_t* ctx, bytes32_t block_hash, bool with_body, bytes_t* el_header, ssz_ob_t* el_body) {
  bytes32_t hash = {0};
  *el_header     = NULL_BYTES;
  *el_body       = (ssz_ob_t) {0};

  if (with_body) {
    bytes_t raw_block = NULL_BYTES;
    TRY_ASYNC(eth_debug_get_raw_block(ctx, block_hash, &raw_block));
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
    char   hash_json[JSON_HASH_LEN + 1] = {0}; // + terminator
    json_t block                        = {0};
    sbprintf(hash_json, "\"0x%x\"", bytes(block_hash, 32));
    TRY_ASYNC(eth_get_block(ctx, (json_t) {.start = hash_json, .len = JSON_HASH_LEN, .type = JSON_TYPE_STRING}, false, &block));
    if (block.type != JSON_TYPE_OBJECT) THROW_ERROR_WITH("the L2 node does not know the block 0x%x", bytes(block_hash, 32));
    TRY_ASYNC(eth_el_header_build_from_json(&ctx->state, el_header, header_fork(block), block));
  }

  keccak(*el_header, hash);
  if (memcmp(hash, block_hash, 32) != 0) {
    free_l2_block(el_header, el_body);
    THROW_ERROR_WITH("the L2 header does not hash to the block 0x%x", bytes(block_hash, 32));
  }
  return C4_SUCCESS;
}

/**
 * Reads a block identifier as a block hash.
 *
 * @param block JSON block identifier
 * @param hash receives the 32-byte hash
 * @return `true` if `block` is a quoted 32-byte hex string
 */
static bool block_as_hash(json_t block, bytes32_t hash) {
  memset(hash, 0, 32);
  // a token of this length holds exactly 64 nibbles, so nothing is padded
  return block.len == JSON_HASH_LEN && json_as_bytes32(block, hash);
}

/**
 * Resolves a block that was requested by number, or by a hash other than the anchor.
 *
 * This is where an EEZ chain decides how a block is proven:
 *
 * | requested block                     | proof                                              |
 * |-------------------------------------|----------------------------------------------------|
 * | the anchor                          | `eezL1Proof`                                       |
 * | up to 8191 blocks before the anchor | `eip2935` around the `eezL1Proof` of the anchor    |
 * | newer than the anchor               | error, not supported yet                           |
 * | older than that                     | error, needs a historic proof, not supported yet   |
 *
 * For a block before the anchor the hash is read from the EIP-2935 history contract in
 * the state of the anchor, so the block is on the chain L1 committed to. The storage
 * proof is not verified here: a lying L2 node yields a proof the verifier rejects.
 *
 * On success `snap->el_header`, `snap->el_body` and `snap->el_block_hash` describe the
 * resolved block; header and body are heap-owned. `snap->anchor_header` (a heap-owned copy
 * of `anchor_header`) and `snap->history_proof` are set if and only if the block is older
 * than the anchor. On any other status `snap` owns nothing.
 *
 * @param ctx prover context
 * @param conf configuration of the rollup
 * @param block JSON block identifier of the request (hash or number)
 * @param with_body whether transactions and withdrawals are needed
 * @param anchor 32-byte hash of the anchor block
 * @param anchor_header RLP header of the anchor, borrowed
 * @param snap receives header, body, hash and (for a block before the anchor) anchor header and history proof of the resolved block
 * @return `C4_SUCCESS`; `C4_PENDING` while the number of a requested hash, the history proof (`eth_getProof`) or the block itself is being fetched; `C4_ERROR`
 */
static c4_status_t resolve_block(prover_ctx_t* ctx, const eez_chain_config_t* conf, json_t block, bool with_body, bytes32_t anchor, bytes_t anchor_header, eez_el_snap_t* snap) {
  uint64_t  anchor_number  = eth_el_header_get_uint64(anchor_header, EL_BLOCK_NUMBER);
  uint64_t  requested      = 0;
  bytes32_t requested_hash = {0};
  bytes32_t stored_hash    = {0};
  json_t    history        = {0};
  // anything else than a tag is a hash or a number: `eez_get_el_block` rejected the rest
  bool by_hash = block_as_hash(block, requested_hash);

  if (by_hash) {
    json_t header = {0};
    // the hash alone does not say where the block is: the number selects the history slot
    TRY_ASYNC(eth_get_block(ctx, block, false, &header));
    if (header.type != JSON_TYPE_OBJECT) THROW_ERROR_WITH("block %J not found on %s", block, conf->name);
    requested = json_get_uint64(header, "number");
    // the anchor hash never gets here, so a block of the same height is on another fork
    if (requested == anchor_number) THROW_ERROR_WITH(EEZ_ERR_NOT_ANCHORED, block, anchor_number, bytes(anchor, 32));
  }
  else {
    requested = json_as_uint64(block);
    if (requested == anchor_number) {
      TRY_ASYNC(fetch_l2_block(ctx, anchor, with_body, &snap->el_header, &snap->el_body));
      memcpy(snap->el_block_hash, anchor, 32);
      return C4_SUCCESS;
    }
  }

  if (requested > anchor_number)
    THROW_ERROR_WITH("block %l is newer than the last block of %s proven on L1 (%l); blocks after the L1 anchor are not supported yet", requested, conf->name, anchor_number);
  if (anchor_number - requested > ETH_EIP2935_HISTORY_WINDOW)
    THROW_ERROR_WITH("block %l is more than %d blocks older than the last block of %s proven on L1 (%l); it needs a historic proof, which is not supported yet", requested, (uint32_t) ETH_EIP2935_HISTORY_WINDOW, conf->name, anchor_number);

  // within the history window of the anchor: the anchor state holds the hash of the block
  TRY_ASYNC(c4_eth_get_eip2935_proof(ctx, requested, anchor_number, &history, stored_hash));
  if (bytes_all_zero(bytes(stored_hash, 32)))
    THROW_ERROR_WITH("the EIP-2935 history contract of %s holds no hash for block %l in the state of block %l", conf->name, requested, anchor_number);
  if (by_hash && memcmp(stored_hash, requested_hash, 32) != 0)
    THROW_ERROR_WITH(EEZ_ERR_NOT_ANCHORED, block, requested, bytes(stored_hash, 32));

  TRY_ASYNC(fetch_l2_block(ctx, stored_hash, with_body, &snap->el_header, &snap->el_body));
  // the slot is `number % 8191`: a header of another height would mean the contract (or the node) is off
  if (eth_el_header_get_uint64(snap->el_header, EL_BLOCK_NUMBER) != requested) {
    free_l2_block(&snap->el_header, &snap->el_body);
    THROW_ERROR_WITH("the EIP-2935 history contract of %s does not hold block %l in its slot", conf->name, requested);
  }

  // the anchor header is part of the proof of a block before the anchor
  memcpy(snap->el_block_hash, stored_hash, 32);
  snap->history_proof = history;
  snap->anchor_header = bytes_dup(anchor_header);
  return C4_SUCCESS;
}

c4_status_t eez_get_el_block(prover_ctx_t* ctx, json_t block, eth_block_t* out, bool with_body) {
  const eez_chain_config_t* conf      = eez_get_chain_config(ctx->chain_id);
  bytes32_t                 id        = {0};
  bytes32_t                 anchor    = {0};
  bytes32_t                 requested = {0};
  eez_el_snap_t             snap      = {0};
  ssz_ob_t                  l1        = {0};
  if (!conf) THROW_ERROR("unsupported EEZ chain!");
  // `pending`, `earliest` and malformed identifiers have no proof: fail before any request is sent
  if (!block_is_anchor_tag(block) && !block_is_hex(block)) THROW_ERROR_WITH("block %J cannot be proven on %s", block, conf->name);

  snap_id(block, with_body, id);
  bytes_t existing = c4_state_cache_get(&ctx->state, id);
  if (existing.data) {
    fill_from_snap(out, (eez_el_snap_t*) existing.data);
    return C4_SUCCESS;
  }

  // The L1 proof always comes first: it tells which block is the anchor, and only then
  // can be decided whether the requested block needs an EIP-2935 proof on top of it.
  TRY_ASYNC(fetch_l1_anchor_proof(ctx, conf, l1_block_tag(block), &l1));
  TRY_ASYNC(read_anchor(ctx, conf, l1, &snap.account, anchor));
  snap.sync_data = ssz_get(&l1, "sync_data");

  if (block_is_anchor_tag(block) || (block_as_hash(block, requested) && memcmp(requested, anchor, 32) == 0)) {
    TRY_ASYNC(fetch_l2_block(ctx, anchor, with_body, &snap.el_header, &snap.el_body));
    memcpy(snap.el_block_hash, anchor, 32);
  }
  else {
    // a number or another hash: the header of the anchor gives the height to compare with
    bytes_t  anchor_header = NULL_BYTES;
    ssz_ob_t no_body       = {0};
    TRY_ASYNC(fetch_l2_block(ctx, anchor, false, &anchor_header, &no_body));
    c4_status_t status = resolve_block(ctx, conf, block, with_body, anchor, anchor_header, &snap);
    safe_free(anchor_header.data); // `resolve_block` keeps its own copy for a block before the anchor
    TRY_ASYNC(status);
  }

  attach_snap(ctx, id, snap, out);
  return C4_SUCCESS;
}

bool eez_add_block_proof(prover_ctx_t* ctx, ssz_builder_t* builder, eth_block_t* block_data, blockroot_proof_t* historic) {
  (void) ctx;
  (void) historic;
  if (block_data->proof_type != C4_BLOCK_PROOF_TYPE_EEZ || !block_data->el_header.data || !block_data->eez.account.bytes.data)
    return false;

  // The anchor is always proven through L1. A block before the anchor wraps that proof:
  // its hash is a storage value of the EIP-2935 history contract in the state of the anchor.
  const eth_block_eip2935_t* history  = &block_data->eez.history;
  bool                       older    = history->newer_header.data && history->history_proof.start;
  ssz_builder_t              l1_proof = ssz_builder_for_type(ETH_SSZ_EEZ_L1_PROOF);
  ssz_add_bytes(&l1_proof, "elHeader", older ? history->newer_header : block_data->el_header);
  ssz_add_bytes(&l1_proof, "account", block_data->eez.account.bytes);

  if (older)
    c4_eth_add_eip2935_proof(builder, block_data->el_header, history->history_proof, l1_proof);
  else
    ssz_add_builders(builder, "elProof", l1_proof);
  return true;
}

void eez_register_block_proof_prover(void) {
  c4_register_block_proof_prover(C4_CHAIN_TYPE_EEZ, eez_add_block_proof, eez_get_el_block);
}
