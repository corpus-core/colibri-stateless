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

#include "beacon.h"
#include "beacon_types.h"
#include "el_header.h"
#include "eth_compute_units.h"
#include "eth_req.h"
#include "eth_tools.h"
#include "json.h"
#include "logger.h"
#include "plugin.h"
#include "prover.h"
#include "tx_cache.h"
#include "version.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  bytes_t  el_header;
  bytes_t  branch;
  gindex_t branch_gindex;
  ssz_ob_t cl_header;
  ssz_ob_t el_body;
  uint8_t  header_data[3 * 32 + 2 * 8];
} el_header_and_branch_t;

#ifdef PROVER_CACHE
static inline void create_cache_block_key(bytes32_t key, json_t block) {
  buffer_t buffer = {.allocated = -32, .data = {.data = key, .len = 0}};
  if (strncmp(block.start, "\"latest\"", 8) == 0)
    memcpy((char*) key + 1, "latest", 7);
  else if (strncmp(block.start, "\"safe\"", 6) == 0 || strncmp(block.start, "\"finalized\"", 11) == 0) {
    memcpy((char*) key, FINALITY_KEY, sizeof(FINALITY_KEY));
    return;
  }
  else if (block.start[1] == '0' && block.start[2] == 'x') {
    json_as_bytes(block, &buffer);
    if (block.len < 68) memmove(key + 1, key, buffer.data.len);
  }
  else
    memcpy(key + 1, block.start, block.len > 31 ? 31 : block.len);
  *key = 'S';
}
static beacon_head_t* c4_beacon_cache_get_slot(prover_ctx_t* ctx, json_t block) {
  bytes32_t key = {0};
  create_cache_block_key(key, block);
  beacon_head_t* cached = (beacon_head_t*) c4_prover_cache_get(ctx, key);
  // finalized and safe are both stored with the same key, but we decide here which to return.
  if (cached && strncmp(block.start, "\"finalized\"", 11) == 0) return cached + 1;
  return cached;
}
static bool c4_beacon_cache_get_blockdata(prover_ctx_t* ctx, bytes32_t block_root, eth_block_t* beacon_block) {
  bytes32_t key = {0};
  *key          = 'B';
  memcpy(key + 1, block_root + 1, 31);
  eth_block_t* cached_block = (eth_block_t*) c4_prover_cache_get(ctx, key);
  if (cached_block) {
    *beacon_block = *cached_block;
    return true;
  }
  return false;
}

c4_status_t c4_set_latest_block(prover_ctx_t* ctx, uint64_t latest_block_number) {
  eth_block_t block    = {0};
  uint8_t     tmp[100] = {0};
  buffer_t    buf      = stack_buffer(tmp);
  bytes32_t   key      = {0};

  TRY_ASYNC(c4_beacon_get_block_for_eth(ctx, json_parse(bprintf(&buf, "\"0x%lx\"", latest_block_number)), &block));
  if (block.proof_type != C4_BLOCK_PROOF_TYPE_BEACON)
    THROW_ERROR("latest-block cache requires beacon proof data");

  beacon_head_t head = {.slot = block.slot};
  memcpy(head.root, block.beacon.data_block_root, 32);
  bytes_t slot_data = bytes(&head, sizeof(beacon_head_t));
  log_info("Setting latest block %l (0x%lx) in cache", latest_block_number, latest_block_number);

  memcpy(key, "Slatest", 7);
  c4_prover_cache_invalidate(key);                                                      // invalidate oldkey
  c4_prover_cache_set(ctx, key, bytes_dup(slot_data).data, slot_data.len, 20000, free); // set the new key
  return C4_SUCCESS;
}

static void free_eth_block_t(eth_block_t* beacon_block) {
  free(beacon_block->beacon.cl_header.bytes.data);
  free(beacon_block->beacon.sync_aggregate.bytes.data);
  free(beacon_block->el_header.data);
  free(beacon_block->beacon.block_hash_branch.data);
  free(beacon_block->el_body.bytes.data);
  free(beacon_block->beacon.cl_body.bytes.data);
  free(beacon_block);
}

void c4_beacon_cache_update_blockdata(prover_ctx_t* ctx, eth_block_t* beacon_block, uint64_t latest_timestamp, bytes32_t block_root) {
  if (beacon_block->proof_type != C4_BLOCK_PROOF_TYPE_BEACON) return;
  bytes32_t key = {0};
  *key          = 'B';
  uint64_t ttl  = 1000 * DEFAULT_TTL;
  memcpy(key + 1, block_root + 1, 31);

  // cache the block
  size_t       full_size   = sizeof(eth_block_t) + beacon_block->el_body.bytes.len + beacon_block->beacon.cl_body.bytes.len + beacon_block->el_header.len + beacon_block->beacon.cl_header.bytes.len + beacon_block->beacon.block_hash_branch.len + beacon_block->beacon.sync_aggregate.bytes.len;
  eth_block_t* cache_block = safe_malloc(sizeof(eth_block_t));
  *cache_block             = *beacon_block;
  if (beacon_block->beacon.block_hash_branch.data) cache_block->beacon.block_hash_branch = bytes_dup(beacon_block->beacon.block_hash_branch);
  if (beacon_block->el_body.bytes.data) cache_block->el_body.bytes = bytes_dup(beacon_block->el_body.bytes);
  if (beacon_block->beacon.cl_body.bytes.data) cache_block->beacon.cl_body.bytes = bytes_dup(beacon_block->beacon.cl_body.bytes);
  if (beacon_block->el_header.data) cache_block->el_header = bytes_dup(beacon_block->el_header);
  if (beacon_block->beacon.cl_header.bytes.data) cache_block->beacon.cl_header.bytes = bytes_dup(beacon_block->beacon.cl_header.bytes);
  if (beacon_block->beacon.sync_aggregate.bytes.data) cache_block->beacon.sync_aggregate.bytes = bytes_dup(beacon_block->beacon.sync_aggregate.bytes);

  c4_prover_cache_set(ctx, key, cache_block, full_size, ttl, free_eth_block_t); // keep it for 1 day

  // cache the slot
  beacon_head_t head = {.slot = beacon_block->slot};
  memcpy(head.root, block_root, 32);
  bytes_t slot_data = bytes(&head, sizeof(beacon_head_t));

  memset(key, 0, 32);
  *key = 'S';
  if (latest_timestamp) {
    memcpy((char*) key, "Slatest", 8);
    uint64_t duration_ms = 20000;    // Default to minimum TTL
    c4_prover_cache_invalidate(key); // invalidate oldkey
    c4_prover_cache_set(ctx, key, bytes_dup(slot_data).data, slot_data.len, duration_ms, free);
  }
  *key = 'S';
  memcpy(key + 1, beacon_block->el_block_hash + 1, 31);
  c4_prover_cache_set(ctx, key, bytes_dup(slot_data).data, slot_data.len, ttl, free); // keep it for 1 day
  memset(key + 1, 0, 31);
  uint64_t block_number         = eth_el_header_get_uint64(beacon_block->el_header, EL_BLOCK_NUMBER);
  uint8_t  block_number_data[8] = {0};
  uint64_to_be(block_number_data, block_number);
  bytes_t block_number_bytes = bytes_remove_leading_zeros(bytes(block_number_data, 8));
  memcpy(key + 1, block_number_bytes.data, block_number_bytes.len);
  c4_prover_cache_set(ctx, key, bytes_dup(slot_data).data, slot_data.len, ttl, free); // keep it for 1 day

  // cache the transactions in the tx cache.
  ssz_ob_t  txs     = ssz_get(&beacon_block->el_body, "transactions");
  uint32_t  len     = ssz_len(txs);
  bytes32_t tx_hash = {0};
  // Reserve once for batch insertion to avoid per-insert cleanup
  c4_eth_tx_cache_reserve(len);
  for (uint32_t i = 0; i < len; i++) {
    keccak(ssz_at(txs, i).bytes, tx_hash);
    c4_eth_tx_cache_set(tx_hash, block_number, i);
  }
}

#endif

static c4_status_t get_finality_check_points(prover_ctx_t* ctx, json_t* result) {
  TRY_ASYNC(c4_send_beacon_json(ctx, "eth/v1/beacon/states/head/finality_checkpoints", NULL, 0, result));
  *result = json_get(*result, "data");
  return C4_SUCCESS;
}

// Child-header lookup:
// - Default: GET /eth/v1/beacon/headers?parent_root= (Beacon API, Lodestar).
// - C4_PROVER_FLAG_NIMBUS: Nimbus does not implement that query
//   (https://github.com/status-im/nimbus-eth2/issues/7305). Walk ?slot=
//   after the parent instead. Try parent+1 first (the usual case), then the
//   following slots up to MAX. Empty slots are HTTP 200 {"data":[]} or a
//   Nimbus 404 ("Block header/data has not been found"); both skip to the
//   next slot. Other request errors abort.
#define PARENT_CHILD_SCAN_MAX 32u
#define PARENT_CHILD_SLOT_TTL 6u // empty ?slot= and empty ?parent_root= must not stick for DEFAULT_TTL

static bool json_uint64_field(json_t obj, const char* key, uint64_t* out) {
  json_t v = json_get(obj, key);
  if (v.type == JSON_TYPE_NOT_FOUND || v.type == JSON_TYPE_INVALID || !v.start) return false;
  if (out) *out = json_as_uint64(v);
  return true;
}

static bool el_block_uses_gloas_path(const prover_ctx_t* ctx, json_t eth_block) {
  return c4_chain_schedules_fork(ctx->chain_id, C4_FORK_GLOAS) && json_uint64_field(eth_block, "slotNumber", NULL);
}

static json_t beacon_header_unwrap_data(json_t result) {
  json_t val = json_get(result, "data");
  if (val.type == JSON_TYPE_ARRAY) val = json_at(val, 0);
  return val;
}

static json_t beacon_header_message(json_t entry) {
  return json_get(json_get(entry, "header"), "message");
}

static bool beacon_header_root_equals(json_t entry, bytes32_t expected) {
  uint8_t  tmp[32];
  buffer_t buf = stack_buffer(tmp);
  bytes_t  got = json_get_bytes(entry, "root", &buf);
  return got.len == 32 && memcmp(got.data, expected, 32) == 0;
}

static bool beacon_header_parent_matches(json_t entry, bytes32_t parent_root) {
  uint8_t  tmp[32];
  buffer_t buf        = stack_buffer(tmp);
  bytes_t  got_parent = json_get_bytes(beacon_header_message(entry), "parent_root", &buf);
  return got_parent.len == 32 && memcmp(got_parent.data, parent_root, 32) == 0;
}

// Slot responses may list several headers (equivocation). Prefer canonical.
static json_t beacon_header_child_in_result(json_t result, bytes32_t parent_root) {
  json_t data    = json_get(result, "data");
  json_t chosen  = (json_t) {.type = JSON_TYPE_NOT_FOUND};
  json_t entries = data;

  if (data.type == JSON_TYPE_OBJECT) {
    if (beacon_header_parent_matches(data, parent_root)) return data;
    return chosen;
  }
  if (data.type != JSON_TYPE_ARRAY) return chosen;

  json_for_each_value(entries, entry) {
    if (entry.type != JSON_TYPE_OBJECT) continue;
    if (!beacon_header_parent_matches(entry, parent_root)) continue;
    if (json_as_bool(json_get(entry, "canonical"))) return entry;
    if (chosen.type != JSON_TYPE_OBJECT) chosen = entry;
  }
  return chosen;
}

static c4_status_t beacon_header_extract(prover_ctx_t* ctx, json_t entry, json_t* header, bytes32_t root) {
  if (root) {
    buffer_t buffer = {.allocated = -32, .data = {.data = root, .len = 0}};
    if (json_get_bytes(entry, "root", &buffer).len != 32) THROW_ERROR("Invalid beacon header root!");
  }
  *header = beacon_header_message(entry);
  if (header->type != JSON_TYPE_OBJECT) THROW_ERROR("Invalid header!");
  return C4_SUCCESS;
}

static bool beacon_slot_missing_error(const char* error) {
  if (!error) return false;
  // Nimbus: HTTP 404 {"code":404,"message":"Block header/data has not been found"}
  return strstr(error, "404") != NULL || strstr(error, "not been found") != NULL;
}

static c4_status_t fetch_headers_at_slot(prover_ctx_t* ctx, uint64_t slot, json_t* result) {
  char     path[200]   = {0};
  buffer_t path_buffer = stack_buffer(path);

  bprintf(&path_buffer, "eth/v1/beacon/headers?slot=%l", slot);
  c4_status_t status = c4_send_beacon_json(ctx, path, NULL, PARENT_CHILD_SLOT_TTL, result);
  if (status != C4_ERROR || !beacon_slot_missing_error(ctx->state.error)) return status;

  safe_free(ctx->state.error);
  ctx->state.error = NULL;
  *result          = (json_t) {.type = JSON_TYPE_NOT_FOUND};
  return C4_SUCCESS;
}

static c4_status_t get_beacon_header_at_slot_with_parent(prover_ctx_t* ctx, uint64_t slot, bytes32_t expected_parent, json_t* header, bytes32_t root) {
  json_t result = {0};

  TRY_ASYNC(fetch_headers_at_slot(ctx, slot, &result));

  json_t entry = beacon_header_child_in_result(result, expected_parent);
  if (entry.type != JSON_TYPE_OBJECT)
    THROW_ERROR("No canonical beacon header at that slot with matching parentRoot!");
  TRY_ASYNC(beacon_header_extract(ctx, entry, header, root));
  uint64_t got_slot = 0;
  if (!json_uint64_field(*header, "slot", &got_slot) || got_slot != slot)
    THROW_ERROR("Beacon header slot does not match the requested slot!");
  return C4_SUCCESS;
}

static c4_status_t scan_child_headers(prover_ctx_t* ctx, uint64_t parent_slot, bytes32_t parent_root, json_t* header, bytes32_t root) {
  if (parent_slot > UINT64_MAX - PARENT_CHILD_SCAN_MAX) THROW_ERROR("Parent beacon slot is too large to scan for a child!");

  for (uint32_t off = 1; off <= PARENT_CHILD_SCAN_MAX; off++) {
    json_t result = {0};
    TRY_ASYNC(fetch_headers_at_slot(ctx, parent_slot + off, &result));
    json_t entry = beacon_header_child_in_result(result, parent_root);
    if (entry.type != JSON_TYPE_OBJECT) continue;
    return beacon_header_extract(ctx, entry, header, root);
  }

  *header = (json_t) {.type = JSON_TYPE_NOT_FOUND};
  return C4_SUCCESS;
}

static c4_status_t get_beacon_header_by_parent_hash_scan(prover_ctx_t* ctx, bytes32_t parent_root, json_t* header, bytes32_t root) {
  char     path[200]   = {0};
  json_t   parent_res  = {0};
  buffer_t path_buffer = stack_buffer(path);

  bprintf(&path_buffer, "eth/v1/beacon/headers/0x%x", bytes(parent_root, 32));
  TRY_ASYNC(c4_send_beacon_json(ctx, path, NULL, DEFAULT_TTL, &parent_res));

  json_t parent_entry = beacon_header_unwrap_data(parent_res);
  if (parent_entry.type != JSON_TYPE_OBJECT) THROW_ERROR("Parent beacon header not found!");
  if (!beacon_header_root_equals(parent_entry, parent_root)) THROW_ERROR("Parent beacon header root mismatch!");
  uint64_t parent_slot = 0;
  if (!json_uint64_field(beacon_header_message(parent_entry), "slot", &parent_slot))
    THROW_ERROR("Parent beacon header missing slot!");
  return scan_child_headers(ctx, parent_slot, parent_root, header, root);
}

static c4_status_t get_beacon_header_by_parent_hash_query(prover_ctx_t* ctx, bytes32_t parent_root, json_t* header, bytes32_t root) {
  char     path[200]   = {0};
  json_t   result      = {0};
  buffer_t path_buffer = stack_buffer(path);

  bprintf(&path_buffer, "eth/v1/beacon/headers?parent_root=0x%x", bytes(parent_root, 32));
  TRY_ASYNC(c4_send_beacon_json_with_client_type(ctx, path, NULL, PARENT_CHILD_SLOT_TTL, &result, BEACON_SUPPORTS_PARENT_ROOT_HEADERS));

  json_t entry = beacon_header_child_in_result(result, parent_root);
  if (entry.type != JSON_TYPE_OBJECT) {
    *header = (json_t) {.type = JSON_TYPE_NOT_FOUND};
    return C4_SUCCESS;
  }
  return beacon_header_extract(ctx, entry, header, root);
}

// parent_slot is a Nimbus-only hint (scan after that slot). NULL means the
// parent slot is unknown: fetch the parent header first. Slot 0 is valid, so
// this cannot be a zero sentinel. The Lodestar path ignores the slot and uses
// headers?parent_root=.
static c4_status_t find_child_header(prover_ctx_t* ctx, bytes32_t parent_root, const uint64_t* parent_slot, json_t* header, bytes32_t root) {
  if (ctx->flags & C4_PROVER_FLAG_NIMBUS) {
    if (parent_slot)
      return scan_child_headers(ctx, *parent_slot, parent_root, header, root);
    return get_beacon_header_by_parent_hash_scan(ctx, parent_root, header, root);
  }
  return get_beacon_header_by_parent_hash_query(ctx, parent_root, header, root);
}

static c4_status_t get_beacon_header_by_parent_hash(prover_ctx_t* ctx, bytes32_t parent_root, json_t* header, bytes32_t root) {
  return find_child_header(ctx, parent_root, NULL, header, root);
}

static c4_status_t determine_fork(prover_ctx_t* ctx, ssz_ob_t* block) {
  if (!block || !block->bytes.data) THROW_ERROR("no block data!");
  if (block->bytes.len < 108) THROW_ERROR_WITH("Invalid block data len=%d !", block->bytes.len);
  bytes_t  data   = block->bytes;
  uint32_t offset = uint32_from_le(data.data);
  if (offset > data.len - 8) THROW_ERROR_WITH("Invalid block data offset[%d] > data_len[%d] - 8 : %b !", offset, data.len, bytes(data.data, data.len < 200 ? data.len : 200));
  uint64_t            slot  = uint64_from_le(data.data + offset);
  const chain_spec_t* chain = c4_eth_get_chain_spec(ctx->chain_id);
  if (chain == NULL) THROW_ERROR("unsupported chain id!");
  fork_id_t fork = c4_chain_fork_id(ctx->chain_id, epoch_for_slot(slot, chain));
  block->def     = eth_ssz_type_for_fork(ETH_SSZ_SIGNED_BEACON_BLOCK_CONTAINER, fork, ctx->chain_id);
  if (!block->def) THROW_ERROR("Invalid fork id!");
  return ssz_is_valid(*block, true, &ctx->state) ? C4_SUCCESS : C4_ERROR;
}

static c4_status_t get_block(prover_ctx_t* ctx, beacon_head_t* b, ssz_ob_t* block) {
  if (!block) THROW_ERROR("Invalid block data!");
  bytes_t  block_data;
  char     path[200];
  buffer_t buffer   = stack_buffer(path);
  bool     has_hash = b && !bytes_all_zero(bytes(b->root, 32));
  uint32_t ttl      = 6; // 6s for head-requests
  if (!b || (b->slot == 0 && !has_hash))
    buffer_add_chars(&buffer, "eth/v2/beacon/blocks/head");
  else if (has_hash) {
    bprintf(&buffer, "eth/v2/beacon/blocks/0x%x", bytes(b->root, 32));
    ttl = DEFAULT_TTL;
  }
  else
    bprintf(&buffer, "eth/v2/beacon/blocks/%l", b->slot);

  TRY_ASYNC(c4_send_beacon_ssz(ctx, path, NULL, NULL, ttl, block));
  TRY_ASYNC(determine_fork(ctx, block));

  *block = ssz_get(block, "message");
  return C4_SUCCESS;
}

static bool has_signature(ssz_ob_t* block) {
  if (!block || !block->bytes.data) return false;
  ssz_ob_t sig_body = ssz_get(block, "body");
  ssz_ob_t sync     = ssz_get(&sig_body, "syncAggregate");
  return !bytes_all_zero(bytes(sync.bytes.data, 64));
}

c4_status_t c4_eth_get_signblock_and_parent(prover_ctx_t* ctx, bytes32_t sign_hash, bytes32_t data_hash, ssz_ob_t* sig_block, ssz_ob_t* data_block, bytes32_t data_root_result) {

  beacon_head_t sign   = {0};
  beacon_head_t data   = {0};
  c4_status_t   status = C4_SUCCESS;

  // handle sign_block
  if (!sign_hash && data_hash) {
    json_t header = {0}; // we need to find the next block first
    TRY_ASYNC(get_beacon_header_by_parent_hash(ctx, data_hash, &header, sign.root));
    if (header.type == JSON_TYPE_NOT_FOUND) THROW_ERROR("The requested block has not been signed yet and cannot be verified!!");
  }
  else if (sign_hash)
    memcpy(sign.root, sign_hash, 32);

  TRY_ADD_ASYNC(status, get_block(ctx, &sign, sig_block));

  // check if we have a valid signature
  if (status == C4_SUCCESS && !has_signature(sig_block)) {
    if (bytes_all_zero(bytes(sign.root, 32))) { // we fetched the head block
      if (data_hash) THROW_ERROR("latest block has no signature");
      memcpy(sign.root, ssz_get(sig_block, "parentRoot").bytes.data, 32);
    }
    else {
      json_t header = {0}; // we need to find the next block first
      TRY_ASYNC(get_beacon_header_by_parent_hash(ctx, sign.root, &header, sign.root));
      if (header.type == JSON_TYPE_NOT_FOUND) THROW_ERROR("no block found with signature");
    }
    return c4_eth_get_signblock_and_parent(ctx, sign.root, data_hash, sig_block, data_block, data_root_result);
  }

  // handle data_block
  if (!data_hash && status == C4_SUCCESS)
    memcpy(data.root, ssz_get(sig_block, "parentRoot").bytes.data, 32);
  else if (data_hash)
    memcpy(data.root, data_hash, 32);
  else
    return status;

  TRY_ADD_ASYNC(status, get_block(ctx, &data, data_block));

  // make sure we know the data_root
  if (status == C4_SUCCESS && data_root_result && data_hash != data_root_result) {
    if (!bytes_all_zero(bytes(data.root, 32)))
      memcpy(data_root_result, data.root, 32);
    else
      ssz_hash_tree_root(*data_block, data_root_result);
  }

  return status;
}

static c4_status_t eth_parent_beacon_root(prover_ctx_t* ctx, json_t eth_block, bytes32_t out) {
  buffer_t buffer = {.allocated = -32, .data = {.data = out, .len = 0}};
  json_t   p_hash = json_get(eth_block, "parentBeaconBlockRoot");
  if (p_hash.len != 68) THROW_ERROR("The Block is not a Beacon Block!");
  if (json_as_bytes(p_hash, &buffer).len != 32) THROW_ERROR("Invalid parentBeaconBlockRoot!");
  return C4_SUCCESS;
}

// Looks up the CL child of eth_block.parentBeaconBlockRoot.
// Writes the child root into `root` and the parent CL root into `parent_root`.
static c4_status_t get_beacon_header_from_eth_block(prover_ctx_t* ctx, json_t eth_block, json_t* header, bytes32_t root, bytes32_t parent_root) {
  TRY_ASYNC(eth_parent_beacon_root(ctx, eth_block, parent_root));
  return get_beacon_header_by_parent_hash(ctx, parent_root, header, root);
}

// Gloas ePBS: EL.slotNumber is the execution beacon slot. The data block we
// merkle-prove is the next CL block (bid.parent_block_hash == EL.hash, gindex
// 2856); the sync-committee signature lives in the block after that.
static c4_status_t eth_get_gloas_roots(prover_ctx_t* ctx, json_t eth_block, bytes32_t sig_root, bytes32_t data_root) {
  uint64_t slot = 0;
  if (!json_uint64_field(eth_block, "slotNumber", &slot))
    THROW_ERROR("Gloas execution block is missing slotNumber!");

  bytes32_t parent_cl = {0};
  TRY_ASYNC(eth_parent_beacon_root(ctx, eth_block, parent_cl));

  json_t    ignored   = {0};
  bytes32_t exec_root = {0};
  TRY_ASYNC(get_beacon_header_at_slot_with_parent(ctx, slot, parent_cl, &ignored, exec_root));

  json_t data_header = {0};
  TRY_ASYNC(find_child_header(ctx, exec_root, &slot, &data_header, data_root));
  if (data_header.type == JSON_TYPE_NOT_FOUND)
    THROW_ERROR("The requested block has not been signed yet and cannot be verified!!");

  uint64_t data_slot = 0;
  if (!json_uint64_field(data_header, "slot", &data_slot))
    THROW_ERROR("Gloas data beacon header is missing slot!");

  json_t sig_header = {0};
  TRY_ASYNC(find_child_header(ctx, data_root, &data_slot, &sig_header, sig_root));
  if (sig_header.type == JSON_TYPE_NOT_FOUND)
    THROW_ERROR("The requested block has not been signed yet and cannot be verified!!");
  return C4_SUCCESS;
}

static c4_status_t eth_get_by_number(prover_ctx_t* ctx, uint64_t block_number, bytes32_t sig_root, bytes32_t data_root) {
  char   tmp[100]  = {0};
  json_t eth_block = {0};
  json_t header    = {0};

  if (c4_chain_schedules_fork(ctx->chain_id, C4_FORK_GLOAS)) {
    sbprintf(tmp, "\"0x%lx\"", block_number);
    TRY_ASYNC(eth_get_block(ctx, (json_t) {.start = tmp, .len = strlen(tmp), .type = JSON_TYPE_STRING}, false, &eth_block));
    if (eth_block.type == JSON_TYPE_NOT_FOUND || eth_block.type == JSON_TYPE_NULL)
      THROW_ERROR_WITH("The execution block %l can not be found!", block_number);
    if (el_block_uses_gloas_path(ctx, eth_block))
      return eth_get_gloas_roots(ctx, eth_block, sig_root, data_root);
    // Pre-Gloas block on a Gloas-scheduled chain: N has no slotNumber, continue via N+1.
  }

  // Pre-Gloas: EL N+1.parentBeaconBlockRoot is the data CL root; its child is the signing block.
  sbprintf(tmp, "\"0x%lx\"", block_number + 1);
  TRY_ASYNC(eth_get_block(ctx, (json_t) {.start = tmp, .len = strlen(tmp), .type = JSON_TYPE_STRING}, false, &eth_block));

  if (eth_block.type == JSON_TYPE_NOT_FOUND || eth_block.type == JSON_TYPE_NULL)
    THROW_ERROR_WITH("The Block after %l, which should contain the parentBeaconBlockRoot for the data block can not be found in the execution layer!", block_number);

  return get_beacon_header_from_eth_block(ctx, eth_block, &header, sig_root, data_root);
}

static c4_status_t eth_get_by_hash(prover_ctx_t* ctx, json_t block_hash, bytes32_t sig_root, bytes32_t data_root) {
  json_t    eth_block   = {0};
  json_t    header      = {0};
  bytes32_t parent_root = {0};

  TRY_ASYNC(eth_get_block(ctx, block_hash, false, &eth_block));

  if (el_block_uses_gloas_path(ctx, eth_block))
    return eth_get_gloas_roots(ctx, eth_block, sig_root, data_root);

  // Pre-Gloas: EL N.parentBeaconBlockRoot's child is the data CL block.
  // Only data_root is filled here; the caller looks up the signing child later.
  return get_beacon_header_from_eth_block(ctx, eth_block, &header, data_root, parent_root);
}

static inline c4_status_t eth_get_final_hash(prover_ctx_t* ctx, bool safe, bytes32_t hash, uint64_t* slot) {
  json_t        result        = {0};
  beacon_head_t hashes[2]     = {0};
  buffer_t      buf_justified = {.allocated = -32, .data = {.data = hashes[0].root, .len = 0}};
  buffer_t      buf_finalized = {.allocated = -32, .data = {.data = hashes[1].root, .len = 0}};

  //  uint8_t* root = safe ? blockroot : blockroot + 32;
  TRY_ASYNC(get_finality_check_points(ctx, &result));
  json_get_bytes(json_get(result, "current_justified"), "root", &buf_justified);
  json_get_bytes(json_get(result, "finalized"), "root", &buf_finalized);
  if (slot) {
    const chain_spec_t* chain = c4_eth_get_chain_spec(ctx->chain_id);
    json_t              cp    = json_get(result, safe ? "finalized" : "current_justified");
    *slot                     = json_get_uint64(cp, "epoch") << chain->slots_per_epoch_bits;
  }

#ifdef PROVER_CACHE
  bytes32_t key = {0};
  memcpy((char*) key, FINALITY_KEY, sizeof(FINALITY_KEY));
  c4_prover_cache_set(ctx, key, bytes_dup(bytes(hashes, sizeof(hashes))).data, sizeof(hashes), 1000 * 60 * 7, free); // 6 min
#endif
  if (hash) memcpy(hash, hashes[safe ? 0 : 1].root, 32);
  return C4_SUCCESS;
}

#ifdef PROVER_CACHE
c4_status_t c4_eth_update_finality(prover_ctx_t* ctx, bytes32_t checkpoint, uint64_t* slot) {
  bytes32_t key = {0};
  memcpy((char*) key, FINALITY_KEY, sizeof(FINALITY_KEY));
  c4_prover_cache_invalidate(key);
  return eth_get_final_hash(ctx, true, checkpoint, slot);
}
#endif

static c4_status_t get_el_header_and_branch(prover_ctx_t* ctx, el_header_and_branch_t* el_header_and_branch, fork_id_t fork, ssz_ob_t data_block, bytes32_t block_root) {

  ssz_ob_t  el_body                 = {0};
  bytes32_t cache_key               = {0};
  bytes32_t body_root               = {0};
  bytes_t   generated_header        = {0};
  bytes_t   generated_branch        = {0};
  gindex_t  generated_branch_gindex = 0;
  memcpy(cache_key, "ELH_", 4);
  memcpy(cache_key + 4, block_root + 4, 28);

  bytes_t cached_value = c4_state_cache_get(&ctx->state, cache_key);
  if (cached_value.data) {
    *el_header_and_branch = *(el_header_and_branch_t*) cached_value.data;
    return C4_SUCCESS;
  }

  if (fork < C4_FORK_GLOAS) {
    // generate the el_header and branch
    ssz_ob_t            body      = ssz_get(&data_block, "body");
    ssz_ob_t            execution = ssz_get(&body, "executionPayload");
    eth_el_header_ctx_t el_ctx    = {
           .execution_payload = execution,
           .fork              = fork,
           .state             = &ctx->state,
           .chain_id          = ctx->chain_id,
           .beacon_block      = data_block,
    };
    bytes_t parent_root = ssz_get(&data_block, "parentRoot").bytes;
    if (parent_root.len == 32) memcpy(el_ctx.parent_root, parent_root.data, 32);
    TRY_ASYNC(eth_el_header_build_from_ep(&generated_header, &el_ctx));
    generated_branch_gindex = c4_execution_block_hash_gindex(ctx->chain_id, ssz_get_uint64(&data_block, "slot"));
    generated_branch        = ssz_create_proof(body, body_root, generated_branch_gindex);

    // TODO optimize instead allocating the content twice, calculate the size and use the memory directly
    ssz_builder_t body_builder = ssz_builder_for_type(ETH_SSZ_EL_BLOCK_CONTENT);
    ssz_add_bytes(&body_builder, "transactions", ssz_get(&execution, "transactions").bytes);
    ssz_add_bytes(&body_builder, "withdrawals", ssz_get(&execution, "withdrawals").bytes);
    el_body = ssz_builder_to_bytes(&body_builder);
  }

  else {
    ssz_ob_t      body              = ssz_get(&data_block, "body");
    ssz_ob_t      bid               = ssz_get(&body, "signedExecutionPayloadBid");
    ssz_ob_t      message           = ssz_get(&bid, "message");
    uint8_t*      parent_block_hash = ssz_get(&message, "parentBlockHash").bytes.data;
    ssz_builder_t body_builder      = ssz_builder_for_type(ETH_SSZ_EL_BLOCK_CONTENT);
    json_t        result            = {0};
    buffer_t      buffer            = {0};
    char          tmp[100]          = {0};
    sbprintf(tmp, "[\"0x%x\"]", bytes(parent_block_hash, 32));

    TRY_ASYNC(c4_send_eth_rpc(ctx, "debug_getRawBlock", tmp, DEFAULT_TTL, &result, NULL));
    buffer_grow(&buffer, result.len / 2 + 1);
    TRY_ASYNC_FINAL(eth_el_header_get_from_raw_block(&ctx->state, json_as_bytes(result, &buffer), &generated_header, &body_builder), buffer_free(&buffer));

    generated_branch_gindex = c4_execution_block_hash_gindex(ctx->chain_id, ssz_get_uint64(&data_block, "slot"));
    generated_branch        = ssz_create_proof(body, body_root, generated_branch_gindex);
    el_body                 = ssz_builder_to_bytes(&body_builder);
  }

  size_t                  size  = sizeof(el_header_and_branch_t) + generated_header.len + generated_branch.len + el_body.bytes.len;
  el_header_and_branch_t* value = safe_malloc(size);

  // build header
  value->cl_header = (ssz_ob_t) {.def = eth_ssz_type_for_denep(ETH_SSZ_BEACON_BLOCK_HEADER, ctx->chain_id), .bytes = bytes(value->header_data, 112)};
  memcpy(value->header_data, data_block.bytes.data, 112 - 32); // everything except body
  memcpy(value->header_data + 112 - 32, body_root, 32);

  // cache the generated header
  value->branch_gindex = generated_branch_gindex;
  value->el_header     = bytes_cpy(value, sizeof(el_header_and_branch_t), generated_header);
  value->branch        = bytes_cpy(value, sizeof(el_header_and_branch_t) + generated_header.len, generated_branch);
  value->el_body.bytes = bytes_cpy(value, sizeof(el_header_and_branch_t) + generated_header.len + generated_branch.len, el_body.bytes);
  value->el_body.def   = el_body.def;

  safe_free(generated_header.data);
  safe_free(generated_branch.data);
  safe_free(el_body.bytes.data);

  c4_state_cache_set(&ctx->state, cache_key, bytes((void*) value, size));
  *el_header_and_branch = *value;

  return C4_SUCCESS;
}

static inline c4_status_t eth_get_block_roots(prover_ctx_t* ctx, json_t block, bytes32_t sig_root, bytes32_t data_root) {
  CHECK_JSON(block, block.len == 68 ? "bytes32" : "block", "block identifier");
#ifdef PROVER_CACHE
  beacon_head_t* cached = c4_beacon_cache_get_slot(ctx, block);
  TRACE_ADD_STR(ctx, "block_tag_cached", cached ? "cached" : "missed");
  if (cached) {
    memcpy(data_root, cached->root, 32);
    return C4_SUCCESS;
  }
#endif

  if (strncmp(block.start, "\"latest\"", 8) == 0)
    return C4_SUCCESS; // latest -  we do nothing since 2 empty root_hashes are returned, which will trigger head-requests
  else if (strncmp(block.start, "\"safe\"", 6) == 0)
    TRY_ASYNC(eth_get_final_hash(ctx, true, data_root, NULL));
  else if (strncmp(block.start, "\"finalized\"", 11) == 0)
    TRY_ASYNC(eth_get_final_hash(ctx, false, data_root, NULL));
  else if (block.type == JSON_TYPE_STRING && block.len == 68) // blockhash
    TRY_ASYNC(eth_get_by_hash(ctx, block, sig_root, data_root));
  else if (block.type == JSON_TYPE_STRING && block.len > 4 && block.start[1] == '0' && block.start[2] == 'x') // blocknumber
    TRY_ASYNC(eth_get_by_number(ctx, json_as_uint64(block), sig_root, data_root));
  else
    THROW_ERROR_WITH("Invalid block: %J", block);

  return C4_SUCCESS;
}

c4_status_t c4_beacon_get_block_for_eth_with_body(prover_ctx_t* ctx, json_t block, eth_block_t* beacon_block) {
  if (ctx->flags & C4_PROVER_FLAG_HYBRID)
    return c4_hybrid_get_block_for_eth(ctx, block, beacon_block, true);
  c4_get_el_block_extra_fn extra = c4_block_proof_get_fn(c4_chain_type(ctx->chain_id));
  if (extra) return extra(ctx, block, beacon_block, true);
  return c4_beacon_get_block_for_eth(ctx, block, beacon_block);
}

c4_status_t c4_beacon_fill_becaon_block_from_eth(prover_ctx_t* ctx,
                                                 eth_block_t* beacon_block, bytes32_t data_root, ssz_ob_t data_block, ssz_ob_t sig_block) {

  chain_spec_t*          chain    = c4_eth_get_chain_spec(ctx->chain_id);
  el_header_and_branch_t el_data  = {0};
  ssz_ob_t               sig_body = ssz_get(&sig_block, "body");
  fork_id_t              fork     = c4_chain_fork_id(ctx->chain_id, epoch_for_slot(ssz_get_uint64(&data_block, "slot"), chain));

  TRY_ASYNC(get_el_header_and_branch(ctx, &el_data, fork, data_block, data_root));
  beacon_block->proof_type                      = C4_BLOCK_PROOF_TYPE_BEACON;
  beacon_block->el_header                       = el_data.el_header;
  beacon_block->beacon.block_hash_branch        = el_data.branch;
  beacon_block->beacon.block_hash_branch_gindex = el_data.branch_gindex;
  beacon_block->el_body                         = el_data.el_body;
  beacon_block->beacon.cl_header                = el_data.cl_header;
  beacon_block->slot                            = ssz_get_uint64(&data_block, "slot");
  beacon_block->beacon.cl_body                  = ssz_get(&data_block, "body");
  beacon_block->beacon.sync_aggregate           = ssz_get(&sig_body, "syncAggregate");
  memcpy(beacon_block->beacon.sign_parent_root, ssz_get(&sig_block, "parentRoot").bytes.data, 32);
  memcpy(beacon_block->beacon.data_block_root, data_root, 32);
  keccak(beacon_block->el_header, beacon_block->el_block_hash);
  ssz_hash_tree_root(beacon_block->beacon.cl_body, beacon_block->beacon.cl_body_root); // TODO: we are already calculating the body_root when creating the branch, why not reuse it?

  return C4_SUCCESS;
}

c4_status_t c4_beacon_get_block_for_eth(prover_ctx_t* ctx, json_t block, eth_block_t* beacon_block) {

  if (ctx->flags & C4_PROVER_FLAG_HYBRID)
    return c4_hybrid_get_block_for_eth(ctx, block, beacon_block, false);

  c4_get_el_block_extra_fn extra = c4_block_proof_get_fn(c4_chain_type(ctx->chain_id));
  if (extra) return extra(ctx, block, beacon_block, false);

  ssz_ob_t  sig_block = {0}, data_block = {0};
  bytes32_t sig_root  = {0};
  bytes32_t data_root = {0};

  // convert the execution block number to beacon block hashes
  TRY_ASYNC(eth_get_block_roots(ctx, block, sig_root, data_root));

#ifdef PROVER_CACHE
  // is the data_root already cached?
  if (!bytes_all_zero(bytes(data_root, 32)) && c4_beacon_cache_get_blockdata(ctx, data_root, beacon_block))
    return C4_SUCCESS;
#endif

  // get beacon data
  TRY_ASYNC(c4_eth_get_signblock_and_parent(
      ctx,
      bytes_all_zero(bytes(sig_root, 32)) ? NULL : sig_root,
      bytes_all_zero(bytes(data_root, 32)) ? NULL : data_root,
      &sig_block, &data_block, data_root));

  TRY_ASYNC(c4_beacon_fill_becaon_block_from_eth(ctx, beacon_block, data_root, data_block, sig_block));
#ifdef PROVER_CACHE
  c4_beacon_cache_update_blockdata(ctx, beacon_block, strncmp(block.start, "\"latest\"", 8) == 0, beacon_block->beacon.data_block_root);
#endif

  return C4_SUCCESS;
}

ssz_builder_t c4_proof_add_header(ssz_ob_t block, bytes32_t body_root) {
  // we use MAINNET hardcoded since the header is the same for all chains
  ssz_builder_t beacon_header = {.def = eth_ssz_type_for_denep(ETH_SSZ_BEACON_BLOCK_HEADER, C4_CHAIN_MAINNET), .dynamic = {0}, .fixed = {0}};
  ssz_add_bytes(&beacon_header, "slot", ssz_get(&block, "slot").bytes);
  ssz_add_bytes(&beacon_header, "proposerIndex", ssz_get(&block, "proposerIndex").bytes);
  ssz_add_bytes(&beacon_header, "parentRoot", ssz_get(&block, "parentRoot").bytes);
  ssz_add_bytes(&beacon_header, "stateRoot", ssz_get(&block, "stateRoot").bytes);
  ssz_add_bytes(&beacon_header, "bodyRoot", bytes(body_root, 32));
  return beacon_header;
}
c4_status_t c4_send_beacon_json(prover_ctx_t* ctx, char* path, char* query, uint32_t ttl, json_t* result) {
  return c4_send_beacon_json_with_client_type(ctx, path, query, ttl, result, 0);
}

c4_status_t c4_send_beacon_json_with_client_type(prover_ctx_t* ctx, char* path, char* query, uint32_t ttl, json_t* result, uint32_t client_type) {
#ifdef HTTP_SERVER
  client_type |= ctx->client_type;
#endif
  eth_cu_add(ctx, CU_BEACON_JSON);
  bytes32_t id     = {0};
  buffer_t  buffer = {0};
  buffer_add_chars(&buffer, path);
  if (query) {
    buffer_add_chars(&buffer, "?");
    buffer_add_chars(&buffer, query);
  }
  sha256(buffer.data, id);
  data_request_t* data_request = c4_state_get_data_request_by_id(&ctx->state, id);
  if (data_request) {
    buffer_free(&buffer);
    if (c4_state_is_pending(data_request)) return C4_PENDING;
    if (!data_request->error && data_request->response.data) {
      json_t response = json_parse((char*) data_request->response.data);
      if (response.type == JSON_TYPE_INVALID) THROW_ERROR("Invalid JSON response");
      *result = response;
      return C4_SUCCESS;
    }
    else
      THROW_ERROR(data_request->error ? data_request->error : "Data request failed");
  }
  else {
    data_request = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
    memcpy(data_request->id, id, 32);
    data_request->url                   = (char*) buffer.data.data;
    data_request->encoding              = C4_DATA_ENCODING_JSON;
    data_request->method                = C4_DATA_METHOD_GET;
    data_request->type                  = C4_DATA_TYPE_BEACON_API;
    data_request->ttl                   = ttl;
    data_request->preferred_client_type = client_type;
    c4_state_add_request(&ctx->state, data_request);
    return C4_PENDING;
  }

  return C4_SUCCESS;
}

static bool convert_to_ssz(prover_ctx_t* ctx, data_request_t* data_request, ssz_ob_t* result) {
  json_t json_result = json_parse((const char*) result->bytes.data);
  json_t data        = json_get(json_result, "data");

  if (data.type != JSON_TYPE_OBJECT) {
    c4_state_add_error(&ctx->state, "Invalid JSON response");
    return false;
  }

  if (result->def == NULL) {
    // so we are getting a block, but we need to figure out the definition
    uint64_t            slot  = json_get_uint64(json_get(data, "message"), "slot");
    const chain_spec_t* chain = c4_eth_get_chain_spec(ctx->chain_id);
    if (chain == NULL) {
      c4_state_add_error(&ctx->state, "unsupported chain id!");
      return false;
    }
    fork_id_t fork = c4_chain_fork_id(ctx->chain_id, epoch_for_slot(slot, chain));
    result->def    = eth_ssz_type_for_fork(ETH_SSZ_SIGNED_BEACON_BLOCK_CONTAINER, fork, ctx->chain_id);
    if (!result->def) {
      c4_state_add_error(&ctx->state, "no definition for ETH_SSZ_SIGNED_BEACON_BLOCK_CONTAINER!");
      return false;
    }
  }
  ssz_ob_t ssz_result = ssz_from_json(data, result->def, &ctx->state);
  if (!ssz_result.bytes.data) {
    c4_state_add_error(&ctx->state, "Invalid SSZ response");
    return false;
  }

  //  buffer_t buffer = {0};
  //  bprintf(&buffer, "%z", ssz_result);
  //  bytes_write(result->bytes, fopen("block_src.json", "wb"), true);
  //  bytes_write(buffer.data, fopen("block_ssz.json", "wb"), true);
  //  buffer_free(&buffer);

  safe_free(data_request->response.data);
  data_request->response = ssz_result.bytes;
  result->bytes          = ssz_result.bytes;
  return true;
}
c4_status_t c4_send_beacon_ssz(prover_ctx_t* ctx, char* path, char* query, const ssz_def_t* def, uint32_t ttl, ssz_ob_t* result) {
  return c4_send_beacon_ssz_with_client_type(ctx, path, query, def, ttl, result, 0);
}
c4_status_t c4_send_beacon_ssz_with_client_type(prover_ctx_t* ctx, char* path, char* query, const ssz_def_t* def, uint32_t ttl, ssz_ob_t* result, uint32_t client_type) {
#ifdef HTTP_SERVER
  client_type |= ctx->client_type;
#endif
  eth_cu_add(ctx, CU_BEACON_SSZ);
  bytes32_t id     = {0};
  buffer_t  buffer = {0};
  buffer_add_chars(&buffer, path);
  if (query) {
    buffer_add_chars(&buffer, "?");
    buffer_add_chars(&buffer, query);
  }
  sha256(buffer.data, id);
  data_request_t* data_request = c4_state_get_data_request_by_id(&ctx->state, id);
  if (data_request) {
    buffer_free(&buffer);
    if (c4_state_is_pending(data_request)) return C4_PENDING;
    if (!data_request->error && data_request->response.data) {
      *result = (ssz_ob_t) {.def = def, .bytes = data_request->response};
      if (!data_request->validated) {
        if (result->bytes.len > 20 && result->bytes.data[0] == '{' && result->bytes.data[1] == '"' && !convert_to_ssz(ctx, data_request, result)) return C4_ERROR;
        if (def && !ssz_is_valid(*result, true, &ctx->state)) return C4_ERROR;
        data_request->validated = true;
      }
      return C4_SUCCESS;
    }
    else
      THROW_ERROR(data_request->error ? data_request->error : "Data request failed");
  }
  else {
    data_request = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
    memcpy(data_request->id, id, 32);
    data_request->url                   = (char*) buffer.data.data;
    data_request->encoding              = C4_DATA_ENCODING_SSZ;
    data_request->method                = C4_DATA_METHOD_GET;
    data_request->type                  = C4_DATA_TYPE_BEACON_API;
    data_request->ttl                   = ttl;
    data_request->preferred_client_type = client_type;
    c4_state_add_request(&ctx->state, data_request);
    return C4_PENDING;
  }

  return C4_SUCCESS;
}

c4_status_t c4_send_internal_request(prover_ctx_t* ctx, char* path, char* query, uint32_t ttl, bytes_t* result) {
  eth_cu_add(ctx, CU_INTERNAL_REQUEST);
  bytes32_t id     = {0};
  buffer_t  buffer = {0};
  buffer_add_chars(&buffer, path);
  if (query) {
    buffer_add_chars(&buffer, "?");
    buffer_add_chars(&buffer, query);
  }
  sha256(buffer.data, id);
  data_request_t* data_request = c4_state_get_data_request_by_id(&ctx->state, id);
  if (data_request) {
    buffer_free(&buffer);
    if (c4_state_is_pending(data_request)) return C4_PENDING;
    if (!data_request->error && data_request->response.data) {
      *result = data_request->response;
      return C4_SUCCESS;
    }
    else
      THROW_ERROR(data_request->error ? data_request->error : "Data request failed");
  }
  else {
    data_request = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
    memcpy(data_request->id, id, 32);
    data_request->url      = (char*) buffer.data.data;
    data_request->encoding = C4_DATA_ENCODING_SSZ;
    data_request->method   = C4_DATA_METHOD_GET;
    data_request->type     = C4_DATA_TYPE_INTERN;
    data_request->ttl      = ttl;
    c4_state_add_request(&ctx->state, data_request);
    return C4_PENDING;
  }

  return C4_SUCCESS;
}
