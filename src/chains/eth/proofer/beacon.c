#include "beacon.h"
#include "beacon_types.h"
#include "eth_req.h"
#include "json.h"
#include "logger.h"
#include "proofer.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#ifdef PROOFER_CACHE
static inline void create_cache_block_key(bytes32_t key, json_t block) {
  buffer_t buffer = {.allocated = -32, .data = {.data = key, .len = 0}};
  if (strncmp(block.start, "\"latest\"", 8) == 0)
    sprintf((char*) key + 1, "%s", "latest");
  else if (strncmp(block.start, "\"safe\"", 6) == 0 || strncmp(block.start, "\"finalized\"", 12) == 0) {
    sprintf((char*) key, FINALITY_KEY);
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
static beacon_head_t* c4_beacon_cache_get_slot(proofer_ctx_t* ctx, json_t block) {
  bytes32_t key = {0};
  create_cache_block_key(key, block);
  beacon_head_t* cached = (beacon_head_t*) c4_proofer_cache_get(ctx, key);
  if (cached && strncmp(block.start, "\"finalized\"", 12) == 0) return cached + 1;
  return cached;
}
static bool c4_beacon_cache_get_blockdata(proofer_ctx_t* ctx, bytes32_t block_root, beacon_block_t* beacon_block) {
  bytes32_t key = {0};
  *key          = 'B';
  memcpy(key + 1, block_root + 1, 31);
  beacon_block_t* cached_block = (beacon_block_t*) c4_proofer_cache_get(ctx, key);
  if (cached_block) {
    *beacon_block = *cached_block;
    return true;
  }
  return false;
}

void c4_beacon_cache_update_blockdata(proofer_ctx_t* ctx, beacon_block_t* beacon_block, uint64_t latest_timestamp, bytes32_t block_root) {
  bytes32_t key = {0};
  *key          = 'B';
  uint64_t ttl  = 1000 * DEFAULT_TTL;
  memcpy(key + 1, block_root + 1, 31);

  // cache the block
  size_t full_size = sizeof(beacon_block_t) + beacon_block->header.bytes.len + beacon_block->sync_aggregate.bytes.len;
  void*  cached    = safe_malloc(full_size);
  memcpy(cached, beacon_block, sizeof(beacon_block_t));
  memcpy(cached + sizeof(beacon_block_t), beacon_block->header.bytes.data, beacon_block->header.bytes.len);
  memcpy(cached + sizeof(beacon_block_t) + beacon_block->header.bytes.len, beacon_block->sync_aggregate.bytes.data, beacon_block->sync_aggregate.bytes.len);
  beacon_block_t* block            = (beacon_block_t*) cached;
  block->header.bytes.data         = (uint8_t*) cached + sizeof(beacon_block_t);
  block->sync_aggregate.bytes.data = (uint8_t*) cached + sizeof(beacon_block_t) + beacon_block->header.bytes.len;
  block->body.bytes.data           = block->header.bytes.data + (beacon_block->body.bytes.data - beacon_block->header.bytes.data);
  block->execution.bytes.data      = block->header.bytes.data + (beacon_block->execution.bytes.data - beacon_block->header.bytes.data);
  c4_proofer_cache_set(ctx, key, block, full_size, ttl, free); // keep it for 1 day

  // cache the slot
  beacon_head_t head = {.slot = beacon_block->slot};
  memcpy(head.root, block_root, 32);
  bytes_t slot_data = bytes(&head, sizeof(beacon_head_t));

  memset(key, 0, 32);
  *key = 'S';
  if (latest_timestamp) {
    sprintf((char*) key, "%s", "Slatest");
    uint64_t now_unix_ms                  = current_unix_ms(); // Use Unix epoch time
    uint64_t block_interval_ms            = 12000;
    uint64_t buffer_ms                    = 2000; // buffer to make sure the block is actually available.
    uint64_t predicted_next_block_unix_ms = (latest_timestamp * 1000) + block_interval_ms + buffer_ms;

    uint64_t duration_ms = 1; // Default to minimum TTL
    if (predicted_next_block_unix_ms > now_unix_ms)
      duration_ms = predicted_next_block_unix_ms - now_unix_ms;
    else
      // Block prediction is already in the past or now, maybe the buffer wasn't enough
      // or clocks are skewed. Set a very short duration.
      log_warn("Predictive TTL calculation resulted in past time for Slatest (Block Ts: %l, Now: %l). Setting minimal TTL.", latest_timestamp, now_unix_ms / 1000);
    c4_proofer_cache_set(ctx, key, bytes_dup(slot_data).data, slot_data.len, duration_ms, free);
  }
  *key = 'S';
  memcpy(key + 1, ssz_get(&beacon_block->execution, "blockHash").bytes.data + 1, 31);
  c4_proofer_cache_set(ctx, key, bytes_dup(slot_data).data, slot_data.len, ttl, free); // keep it for 1 day
  memset(key + 1, 0, 31);
  uint8_t* block_number_src     = ssz_get(&beacon_block->execution, "blockNumber").bytes.data;
  uint8_t  block_number_data[8] = {0};
  for (int i = 0; i < 8; i++)
    block_number_data[7 - i] = block_number_src[i];
  bytes_t block_number = bytes_remove_leading_zeros(bytes(block_number_data, 8));
  memcpy(key + 1, block_number.data, block_number.len);
  c4_proofer_cache_set(ctx, key, bytes_dup(slot_data).data, slot_data.len, ttl, free); // keep it for 1 day
}

#endif

static c4_status_t get_finality_check_points(proofer_ctx_t* ctx, json_t* result) {
  TRY_ASYNC(c4_send_beacon_json(ctx, "eth/v1/beacon/states/head/finality_checkpoints", NULL, 0, result));
  *result = json_get(*result, "data");
  return C4_SUCCESS;
}

static c4_status_t get_beacon_header_by_parent_hash(proofer_ctx_t* ctx, bytes32_t parent_hash, json_t* header, bytes32_t root) {

  char     path[200]   = {0};
  json_t   result      = {0};
  buffer_t buffer      = {.allocated = -32, .data = {.data = root, .len = 0}};
  buffer_t path_buffer = stack_buffer(path);
  bprintf(&path_buffer, "eth/v1/beacon/headers?parent_root=0x%x", bytes(parent_hash, 32));

  TRY_ASYNC(c4_send_beacon_json(ctx, path, NULL, DEFAULT_TTL, &result));

  json_t val = json_get(result, "data");
  if (val.type == JSON_TYPE_ARRAY)
    val = json_at(val, 0);
  if (val.type != JSON_TYPE_OBJECT) {
    *header = val;
    return C4_SUCCESS;
  };
  if (root) json_get_bytes(val, "root", &buffer);
  val     = json_get(val, "header");
  *header = json_get(val, "message");

  if (!header->start) THROW_ERROR("Invalid header!");

  return C4_SUCCESS;
}

static c4_status_t determine_fork(proofer_ctx_t* ctx, ssz_ob_t* block) {
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

static c4_status_t get_block(proofer_ctx_t* ctx, beacon_head_t* b, ssz_ob_t* block) {
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

c4_status_t c4_eth_get_signblock_and_parent(proofer_ctx_t* ctx, bytes32_t sign_hash, bytes32_t data_hash, ssz_ob_t* sig_block, ssz_ob_t* data_block, bytes32_t data_root_result) {

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

static c4_status_t eth_get_block(proofer_ctx_t* ctx, json_t block, bool full_tx, json_t* result) {
  uint8_t  tmp[200] = {0};
  buffer_t buffer   = stack_buffer(tmp);
  return c4_send_eth_rpc(ctx, (char*) (block.len == 68 ? "eth_getBlockByHash" : "eth_getBlockByNumber"), bprintf(&buffer, "[%J,%s]", block, full_tx ? "true" : "false"), block.len == 68 ? DEFAULT_TTL : 12, result);
}
static c4_status_t get_beacon_header_from_eth_block(proofer_ctx_t* ctx, json_t eth_block, json_t* header, bytes32_t root, bytes32_t parent_root) {
  buffer_t buffer = {.allocated = -32, .data = {.data = parent_root, .len = 0}};
  json_t   p_hash = json_get(eth_block, "parentBeaconBlockRoot");
  if (p_hash.len != 68) THROW_ERROR("The Block is not a Beacon Block!");
  return get_beacon_header_by_parent_hash(ctx, json_as_bytes(p_hash, &buffer).data, header, root);
}

static inline c4_status_t eth_get_by_number(proofer_ctx_t* ctx, uint64_t block_number, bytes32_t sig_root, bytes32_t data_root) {
  char   tmp[100]  = {0};
  json_t eth_block = {0};
  json_t header    = {0};

  // if we have the blocknumber, we fetch the next block, since we know this is the signing block
  sprintf(tmp, "\"0x%" PRIx64 "\"", block_number + 1);
  TRY_ASYNC(eth_get_block(ctx, (json_t) {.start = tmp, .len = strlen(tmp), .type = JSON_TYPE_STRING}, false, &eth_block));

  // get the beacon block matching the parent hash
  return get_beacon_header_from_eth_block(ctx, eth_block, &header, sig_root, data_root);
}

static inline c4_status_t eth_get_by_hash(proofer_ctx_t* ctx, json_t block_hash, bytes32_t data_root) {
  // get the eth block from the blockhash or blocknumber
  json_t    eth_block   = {0};
  json_t    header      = {0};
  bytes32_t parent_root = {0};

  // eth_getBlockByHash
  TRY_ASYNC(eth_get_block(ctx, block_hash, false, &eth_block));

  // get the beacon block matching the parent hash
  return get_beacon_header_from_eth_block(ctx, eth_block, &header, data_root, parent_root);
}

static inline c4_status_t eth_get_final_hash(proofer_ctx_t* ctx, bool safe, bytes32_t hash) {
  json_t        result        = {0};
  beacon_head_t hashes[2]     = {0};
  buffer_t      buf_justified = {.allocated = -32, .data = {.data = hashes[0].root, .len = 0}};
  buffer_t      buf_finalized = {.allocated = -32, .data = {.data = hashes[1].root, .len = 0}};

  //  uint8_t* root = safe ? blockroot : blockroot + 32;
  TRY_ASYNC(get_finality_check_points(ctx, &result));
  json_get_bytes(json_get(result, "current_justified"), "root", &buf_justified);
  json_get_bytes(json_get(result, "finalized"), "root", &buf_finalized);

#ifdef PROOFER_CACHE
  bytes32_t key = {0};
  sprintf((char*) key, FINALITY_KEY);
  c4_proofer_cache_set(ctx, key, bytes_dup(bytes(hashes, sizeof(hashes))).data, sizeof(hashes), 1000 * 60 * 7, free); // 6 min
#endif
  memcpy(hash, hashes[safe ? 0 : 1].root, 32);
  return C4_SUCCESS;
}

#ifdef PROOFER_CACHE
c4_status_t c4_eth_update_finality(proofer_ctx_t* ctx) {
  bytes32_t     key  = {0};
  beacon_head_t hash = {0};
  sprintf((char*) key, FINALITY_KEY);
  c4_proofer_cache_invalidate(key);
  return eth_get_final_hash(ctx, false, &hash);
}
#endif

static inline c4_status_t eth_get_block_roots(proofer_ctx_t* ctx, json_t block, bytes32_t sig_root, bytes32_t data_root) {
#ifdef PROOFER_CACHE
  beacon_head_t* cached = c4_beacon_cache_get_slot(ctx, block);
  if (cached) {
    memcpy(data_root, cached->root, 32);
    return C4_SUCCESS;
  }
#endif

  if (strncmp(block.start, "\"latest\"", 8) == 0)
    return C4_SUCCESS; // latest -  we do nothing since 2 empty root_hashes are returned, which will trigger head-requests
  else if (strncmp(block.start, "\"safe\"", 6) == 0)
    TRY_ASYNC(eth_get_final_hash(ctx, true, data_root));
  else if (strncmp(block.start, "\"finalized\"", 12) == 0)
    TRY_ASYNC(eth_get_final_hash(ctx, false, data_root));
  else if (block.type == JSON_TYPE_STRING && block.len == 68) // blockhash
    TRY_ASYNC(eth_get_by_hash(ctx, block, data_root));
  else if (block.type == JSON_TYPE_STRING && block.len > 4 && block.start[1] == '0' && block.start[2] == 'x') // blocknumber
    TRY_ASYNC(eth_get_by_number(ctx, json_as_uint64(block), sig_root, data_root));
  else
    THROW_ERROR("Invalid block!");

  return C4_SUCCESS;
}

// main beacn_block method
c4_status_t c4_beacon_get_block_for_eth(proofer_ctx_t* ctx, json_t block, beacon_block_t* beacon_block) {
  ssz_ob_t  sig_block = {0}, data_block = {0}, sig_body = {0};
  bytes32_t sig_root  = {0};
  bytes32_t data_root = {0};

  // convert the execution block number to beacon block hashes
  TRY_ASYNC(eth_get_block_roots(ctx, block, sig_root, data_root));

#ifdef PROOFER_CACHE
  // is the data_root already cached?
  if (!bytes_all_zero(bytes(data_root, 32)) && c4_beacon_cache_get_blockdata(ctx, data_root, beacon_block))
    return C4_SUCCESS;
#endif

  TRY_ASYNC(c4_eth_get_signblock_and_parent(
      ctx,
      bytes_all_zero(bytes(sig_root, 32)) ? NULL : sig_root,
      bytes_all_zero(bytes(data_root, 32)) ? NULL : data_root,
      &sig_block, &data_block, data_root));

  sig_body                     = ssz_get(&sig_block, "body");
  beacon_block->slot           = ssz_get_uint64(&data_block, "slot");
  beacon_block->header         = data_block;
  beacon_block->body           = ssz_get(&data_block, "body");
  beacon_block->execution      = ssz_get(&beacon_block->body, "executionPayload");
  beacon_block->sync_aggregate = ssz_get(&sig_body, "syncAggregate");
  memcpy(beacon_block->sign_parent_root, ssz_get(&sig_block, "parentRoot").bytes.data, 32);
  memcpy(beacon_block->data_block_root, data_root, 32);

#ifdef PROOFER_CACHE
  if (strncmp(block.start, "\"latest\"", 8) == 0) { // for latest we take the timestamp, so we can define the ttl
    ssz_ob_t execution = ssz_get(&sig_body, "executionPayload");
    c4_beacon_cache_update_blockdata(ctx, beacon_block, ssz_get_uint64(&execution, "timestamp"), beacon_block->data_block_root);
  }
  else
    c4_beacon_cache_update_blockdata(ctx, beacon_block, 0, beacon_block->data_block_root);
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

c4_status_t c4_send_beacon_json(proofer_ctx_t* ctx, char* path, char* query, uint32_t ttl, json_t* result) {
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
    data_request->url      = (char*) buffer.data.data;
    data_request->encoding = C4_DATA_ENCODING_JSON;
    data_request->method   = C4_DATA_METHOD_GET;
    data_request->type     = C4_DATA_TYPE_BEACON_API;
    data_request->ttl      = ttl;
    c4_state_add_request(&ctx->state, data_request);
    return C4_PENDING;
  }

  return C4_SUCCESS;
}

c4_status_t c4_send_beacon_ssz(proofer_ctx_t* ctx, char* path, char* query, const ssz_def_t* def, uint32_t ttl, ssz_ob_t* result) {
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
      if (def)
        return ssz_is_valid(*result, true, &ctx->state) ? C4_SUCCESS : C4_ERROR;
      else
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
    data_request->type     = C4_DATA_TYPE_BEACON_API;
    data_request->ttl      = ttl;
    c4_state_add_request(&ctx->state, data_request);
    return C4_PENDING;
  }

  return C4_SUCCESS;
}

c4_status_t c4_send_internal_request(proofer_ctx_t* ctx, char* path, char* query, uint32_t ttl, bytes_t* result) {
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
