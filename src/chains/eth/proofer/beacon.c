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
  else if (block.start[1] == '0' && block.start[2] == 'x')
    json_as_bytes(block, &buffer);
  else
    memcpy(key + 1, block.start, block.len > 31 ? 31 : block.len);
  *key = 'S';
}
static beacon_head_t* c4_beacon_cache_get_slot(proofer_ctx_t* ctx, json_t block) {
  bytes32_t key = {0};
  create_cache_block_key(key, block);
  return (beacon_head_t*) c4_proofer_cache_get(ctx, key);
}
static bool c4_beacon_cache_get_blockdata(proofer_ctx_t* ctx, uint64_t slot, beacon_block_t* beacon_block) {
  bytes32_t key = {0};
  *key          = 'B';
  uint64_to_be(key + 1, slot);
  beacon_block_t* cached_block = (beacon_block_t*) c4_proofer_cache_get(ctx, key);
  if (cached_block) {
    *beacon_block = *cached_block;
    return true;
  }
  return false;
}

// TODO switch to blockhash in order to handle reorgs
void c4_beacon_cache_update_blockdata(proofer_ctx_t* ctx, beacon_block_t* beacon_block, uint64_t latest_timestamp, bytes32_t block_root) {
  bytes32_t key = {0};
  *key          = 'B';
  uint64_t ttl  = 1000 * 3600 * 24;
  uint64_to_be(key + 1, beacon_block->slot);

  // cache the block
  size_t full_size = sizeof(beacon_block_t) + beacon_block->header.bytes.len + beacon_block->sync_aggregate.bytes.len;
  void*  cached    = malloc(full_size);
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

static c4_status_t get_beacon_header_by_parent_hash(proofer_ctx_t* ctx, char* hash, json_t* header, bytes32_t root) {

  json_t   result = {0};
  buffer_t buffer = {.allocated = -32, .data = {.data = root, .len = 0}};
  char     path[100];
  sprintf(path, "eth/v1/beacon/headers?parent_root=%s", hash);

  TRY_ASYNC(c4_send_beacon_json(ctx, path, NULL, &result));

  json_t val = json_get(result, "data");
  if (root) json_get_bytes(val, "root", &buffer);
  val     = json_get(val, "header");
  *header = json_get(val, "message");

  if (!header->start) THROW_ERROR("Invalid header!");

  return C4_SUCCESS;
}

static c4_status_t get_beacon_header_by_hash(proofer_ctx_t* ctx, char* hash, json_t* header, bytes32_t root) {

  json_t   result = {0};
  buffer_t buffer = {.allocated = -32, .data = {.data = root, .len = 0}};
  char     path[100];
  sprintf(path, "eth/v1/beacon/headers/%s", hash);

  TRY_ASYNC(c4_send_beacon_json(ctx, path, NULL, &result));

  json_t val = json_get(result, "data");
  if (root) json_get_bytes(val, "root", &buffer);
  val     = json_get(val, "header");
  *header = json_get(val, "message");

  if (!header->start) THROW_ERROR("Invalid header!");

  return C4_SUCCESS;
}

static c4_status_t get_block(proofer_ctx_t* ctx, beacon_head_t* b, ssz_ob_t* block) {

  bytes_t  block_data;
  char     path[200];
  buffer_t buffer   = stack_buffer(path);
  bool     has_hash = b && !bytes_all_zero(bytes(b->root, 32));
  if (!b || (b->slot == 0 && !has_hash))
    buffer_add_chars(&buffer, "eth/v2/beacon/blocks/head");
  else if (has_hash)
    bprintf(&buffer, "eth/v2/beacon/blocks/0x%x", bytes(b->root, 32));
  else
    bprintf(&buffer, "eth/v2/beacon/blocks/%l", b->slot);

  TRY_ASYNC(c4_send_beacon_ssz(ctx, path, NULL, eth_ssz_type_for_fork(ETH_SSZ_SIGNED_BEACON_BLOCK_CONTAINER, C4_FORK_DENEB), block));
  *block = ssz_get(block, "message");
  return C4_SUCCESS;
}

c4_status_t c4_eth_get_sigblock_and_parent(proofer_ctx_t* ctx, beacon_head_t* sign_hash, beacon_head_t* data_hash, ssz_ob_t* sig_block, ssz_ob_t* data_block) {

  c4_status_t status        = C4_SUCCESS;
  json_t      parent_header = {0};
  bytes32_t   parent_hash   = {0};
  TRY_ADD_ASYNC(status, get_block(ctx, sign_hash, sig_block));
  beacon_head_t parent = {0};
  if (data_hash) parent = *data_hash;

  if (!data_hash) {
    if (sign_hash->slot)
      parent.slot = sign_hash->slot - 1; // we try to fetch the parent
    else if (status == C4_SUCCESS)
      memcpy(parent.root, ssz_get(sig_block, "parentRoot").bytes.data, 32);
    else
      return status;
  }

  /*
    if (b->slot) {
      char slot_chars[10] = {0};
      sprintf(slot_chars, "%" PRIu64, parent.slot);
      TRY_ADD_ASYNC(status, get_beacon_header_by_hash(ctx, slot_chars, &parent_header, parent_hash));
    }
  */
  TRY_ADD_ASYNC(status, get_block(ctx, &parent, data_block));

  if (status == C4_SUCCESS && sign_hash->slot && parent_header.type == JSON_TYPE_OBJECT && memcmp(ssz_get(sig_block, "parentRoot").bytes.data, parent_hash, 32) != 0) {
    // this means

    // TODO check the parent_hash
    // the only issues here, we would need to calculate the root_hash since we only have the body
    // but this is expensive
  }

  return status;
}

static c4_status_t eth_get_block(proofer_ctx_t* ctx, json_t block, bool full_tx, json_t* result) {
  uint8_t  tmp[200] = {0};
  buffer_t buffer   = stack_buffer(tmp);
  return c4_send_eth_rpc(ctx, (char*) (block.len == 68 ? "eth_getBlockByHash" : "eth_getBlockByNumber"), bprintf(&buffer, "[%J,%s]", block, full_tx ? "true" : "false"), result);
}

static inline c4_status_t eth_get_latest(proofer_ctx_t* ctx, ssz_ob_t* sig_block, ssz_ob_t* data_block) {
  beacon_head_t head = {0};
  return c4_eth_get_sigblock_and_parent(ctx, &head, NULL, sig_block, data_block);
}

// main beacn_block method
c4_status_t c4_beacon_get_block_for_eth(proofer_ctx_t* ctx, json_t block, beacon_block_t* beacon_block) {
  uint8_t       tmp[100]  = {0};
  beacon_head_t sign_hash = {0};
  ssz_ob_t      sig_block, data_block, sig_body;
#ifdef PROOFER_CACHE
  beacon_head_t* cached = c4_beacon_cache_get_slot(ctx, block);
  if (cached && c4_beacon_cache_get_blockdata(ctx, cached->slot, beacon_block))
    return C4_SUCCESS;
  else {
#endif
    if (strncmp(block.start, "\"latest\"", 8) == 0)
      TRY_ASYNC(eth_get_latest(ctx, &sig_block, &data_block));
    else if (strncmp(block.start, "\"safe\"", 6) == 0) {
      // TODO handle safe block
    }
    else {
      if (block.type != JSON_TYPE_STRING || block.len < 5 || block.start[1] != '0' || block.start[2] != 'x') THROW_ERROR("Invalid block!");

      // get the eth block from the blockhash or blocknumber
      json_t    eth_block = {0};
      json_t    header    = {0};
      bool      is_hash   = block.len == 68;
      bytes32_t root      = {0};

      if (is_hash)
        // eth_getBlockByHash
        TRY_ASYNC(eth_get_block(ctx, block, false, &eth_block));
      else {
        // if we have the blocknumber, we fetch the next block, since we know this is the signing block
        sprintf((char*) tmp, "\"0x%" PRIx64 "\"", json_as_uint64(block) + 1);
        // eth_getBlockByNumber +1
        TRY_ASYNC(eth_get_block(ctx, (json_t) {.start = (char*) tmp, .len = strlen((char*) tmp), .type = JSON_TYPE_STRING}, false, &eth_block));

        // TODO handle not existing block +1!
      }

      // get the beacon block matching the parent hash
      json_t p_hash = json_get(eth_block, "parentBeaconBlockRoot");
      if (p_hash.len != 68) THROW_ERROR("The Block is not a Beacon Block!");
      memcpy(tmp, p_hash.start + 1, p_hash.len - 2); // extract the hash as string
      TRY_ASYNC(get_beacon_header_by_parent_hash(ctx, (char*) tmp, &header, root));

      if (!is_hash) { // blocknumber
        // we already know the parent block, so we can fetch them directly.
        memcpy(sign_hash.root, root, 32); // signing_hash is the root_hash of the header we fetched
        beacon_head_t data_hash = {0};
        buffer_t      buffer    = stack_buffer(data_hash.root);
        json_as_bytes(p_hash, &buffer); // data_hash is parent_hash
        TRY_ASYNC(c4_eth_get_sigblock_and_parent(ctx, &sign_hash, &data_hash, &sig_block, &data_block));
      }
      else {
        // we have the data block, but we need to find the signing block
        beacon_head_t data_hash = {0};
        memcpy(data_hash.root, root, 32);
        buffer_t buffer = stack_buffer(tmp);
        bprintf(&buffer, "0x%x", bytes(root, 32));
        TRY_ASYNC(get_beacon_header_by_parent_hash(ctx, (char*) tmp, &header, root));
        memcpy(sign_hash.root, root, 32);
        TRY_ASYNC(c4_eth_get_sigblock_and_parent(ctx, &sign_hash, &data_hash, &sig_block, &data_block));
      }
    }
#ifdef PROOFER_CACHE
  }
#endif

  sig_body                     = ssz_get(&sig_block, "body");
  beacon_block->slot           = ssz_get_uint64(&data_block, "slot");
  beacon_block->header         = data_block;
  beacon_block->body           = ssz_get(&data_block, "body");
  beacon_block->execution      = ssz_get(&beacon_block->body, "executionPayload");
  beacon_block->sync_aggregate = ssz_get(&sig_body, "syncAggregate");

#ifdef PROOFER_CACHE
  bytes_t root_hash = ssz_get(&sig_block, "parentRoot").bytes;
  if (strncmp(block.start, "\"latest\"", 8) == 0) {
    ssz_ob_t execution = ssz_get(&sig_body, "executionPayload");
    c4_beacon_cache_update_blockdata(ctx, beacon_block, ssz_get_uint64(&execution, "timestamp"), root_hash.data);
  }
  else
    c4_beacon_cache_update_blockdata(ctx, beacon_block, 0, root_hash.data);
#endif

  return C4_SUCCESS;
}

ssz_builder_t c4_proof_add_header(ssz_ob_t block, bytes32_t body_root) {
  ssz_builder_t beacon_header = {.def = eth_ssz_type_for_denep(ETH_SSZ_BEACON_BLOCK_HEADER), .dynamic = {0}, .fixed = {0}};
  ssz_add_bytes(&beacon_header, "slot", ssz_get(&block, "slot").bytes);
  ssz_add_bytes(&beacon_header, "proposerIndex", ssz_get(&block, "proposerIndex").bytes);
  ssz_add_bytes(&beacon_header, "parentRoot", ssz_get(&block, "parentRoot").bytes);
  ssz_add_bytes(&beacon_header, "stateRoot", ssz_get(&block, "stateRoot").bytes);
  ssz_add_bytes(&beacon_header, "bodyRoot", bytes(body_root, 32));
  return beacon_header;
}

c4_status_t c4_send_beacon_json(proofer_ctx_t* ctx, char* path, char* query, json_t* result) {
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
    data_request = (data_request_t*) calloc(1, sizeof(data_request_t));
    memcpy(data_request->id, id, 32);
    data_request->url      = (char*) buffer.data.data;
    data_request->encoding = C4_DATA_ENCODING_JSON;
    data_request->method   = C4_DATA_METHOD_GET;
    data_request->type     = C4_DATA_TYPE_BEACON_API;
    c4_state_add_request(&ctx->state, data_request);
    return C4_PENDING;
  }

  return C4_SUCCESS;
}

c4_status_t c4_send_beacon_ssz(proofer_ctx_t* ctx, char* path, char* query, const ssz_def_t* def, ssz_ob_t* result) {
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
      return ssz_is_valid(*result, true, &ctx->state) ? C4_SUCCESS : C4_ERROR;
    }
    else
      THROW_ERROR(data_request->error ? data_request->error : "Data request failed");
  }
  else {
    data_request = (data_request_t*) calloc(1, sizeof(data_request_t));
    memcpy(data_request->id, id, 32);
    data_request->url      = (char*) buffer.data.data;
    data_request->encoding = C4_DATA_ENCODING_SSZ;
    data_request->method   = C4_DATA_METHOD_GET;
    data_request->type     = C4_DATA_TYPE_BEACON_API;
    c4_state_add_request(&ctx->state, data_request);
    return C4_PENDING;
  }

  return C4_SUCCESS;
}
