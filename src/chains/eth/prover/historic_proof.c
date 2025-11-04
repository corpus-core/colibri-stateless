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

#include "historic_proof.h"
#include "beacon.h"
#include "beacon_types.h"
#include "eth_req.h"
#include "eth_tools.h"
#include "json.h"
#include "logger.h"
#include "prover.h"
#include "ssz.h"
#include "sync_committee.h"
#include "version.h"
#include <inttypes.h> // Include this header for PRIu64 and PRIx64
#include <stdlib.h>
#include <string.h>
#define MAX_HISTORIC_PROOF_HEADER_DEPTH 10

typedef enum {
  BEACON_TYPE_NONE     = 0,
  BEACON_TYPE_LODESTAR = 1,
  BEACON_TYPE_NIMBUS   = 2,
} beacon_type_t;
static beacon_type_t beacon_type = BEACON_TYPE_NONE;

static const ssz_def_t HISTORICAL_SUMMARY[] = {
    SSZ_BYTES32("block_summary_root"),
    SSZ_BYTES32("state_summary_root")};
static const ssz_def_t HISTORICAL_SUMMARY_CONTAINER = SSZ_CONTAINER("HISTORICAL_SUMMARY", HISTORICAL_SUMMARY);
static const ssz_def_t SUMMARIES                    = SSZ_LIST("summaries", HISTORICAL_SUMMARY_CONTAINER, 1 << 24);
static const ssz_def_t BLOCKS                       = SSZ_VECTOR("blocks", ssz_bytes32, 8192);

static c4_status_t get_beacon_type(prover_ctx_t* ctx, beacon_type_t* type) {
  if (beacon_type != BEACON_TYPE_NONE) {
    *type = beacon_type;
    return C4_SUCCESS;
  }

  json_t result = {0};
  TRY_ASYNC(c4_send_beacon_json(ctx, "eth/v1/node/version", NULL, DEFAULT_TTL, &result));
  json_t version = json_get(json_get(result, "data"), "version");
  if (version.type != JSON_TYPE_STRING) THROW_ERROR("Invalid conses api response for version!");

  if (version.len > 10 && strncmp(version.start + 1, "Nimbus", 6) == 0)
    *type = BEACON_TYPE_NIMBUS;
  else if (version.len > 10 && strncmp(version.start + 1, "Lodestar", 8) == 0)
    *type = BEACON_TYPE_LODESTAR;
  else
    THROW_ERROR_WITH("Unsupported beacon client: %j", version);

  return C4_SUCCESS;
}

static c4_status_t get_beacon_header(prover_ctx_t* ctx, bytes32_t block_hash, json_t* header) {

  char     path[200]   = {0};
  json_t   result      = {0};
  buffer_t path_buffer = stack_buffer(path);
  bprintf(&path_buffer, "eth/v1/beacon/headers/0x%x", bytes(block_hash, 32));

  TRY_ASYNC(c4_send_beacon_json(ctx, path, NULL, DEFAULT_TTL, &result));

  json_t val = json_get(result, "data");
  if (val.type != JSON_TYPE_OBJECT) THROW_ERROR("Invalid header!");
  val     = json_get(val, "header");
  *header = json_get(val, "message");
  if (!header->start) THROW_ERROR("Invalid header!");
  return C4_SUCCESS;
}

static void verify_proof(char* name, bytes32_t leaf, bytes32_t root, bytes_t proof, gindex_t gindex) {
  bytes32_t out = {0};
  ssz_verify_single_merkle_proof(proof, leaf, gindex, out);
  buffer_t debug = {0};
  bprintf(&debug, "%s\n-leaf :0x%b\n", name, bytes(leaf, 32));
  bprintf(&debug, "-gidx :%l\n", gindex);
  bprintf(&debug, "-root :0x%b\n", bytes(root, 32));
  bprintf(&debug, "-res  :0x%b\n", bytes(out, 32));
  fbprintf(stdout, "%s\n", (char*) debug.data.data);
  safe_free(debug.data.data);
}

static c4_status_t check_historic_proof_header(prover_ctx_t* ctx, blockroot_proof_t* block_proof, beacon_block_t* src_block) {
  if (memcmp(src_block->data_block_root, src_block->sign_parent_root, 32) == 0) return C4_SUCCESS;
  json_t    proof_header  = {0};
  json_t    header        = {0};
  bytes32_t root          = {0};
  buffer_t  proof_headers = {0};
  bytes32_t header_data   = {0};
  buffer_t  header_buf    = stack_buffer(header_data);
  buffer_t  root_buf      = stack_buffer(root);
  buffer_t  proof         = {0};
  TRY_ASYNC(get_beacon_header(ctx, src_block->sign_parent_root, &header));
  json_get_bytes(header, "parent_root", &root_buf);
  proof_header = header;

  for (int i = 0; i <= MAX_HISTORIC_PROOF_HEADER_DEPTH; i++) {
    if (i == MAX_HISTORIC_PROOF_HEADER_DEPTH) {
      buffer_free(&proof_headers);
      THROW_ERROR("Max header limit reached!");
    }

    if (memcmp(root, src_block->data_block_root, 32) == 0) break;
    TRY_ASYNC_CATCH(get_beacon_header(ctx, root, &header), buffer_free(&proof));
    json_get_bytes(header, "parent_root", &root_buf);
    buffer_add_le(&proof, json_get_uint64(header, "slot"), 8);
    buffer_add_le(&proof, json_get_uint64(header, "proposer_index"), 8);
    buffer_append(&proof, json_get_bytes(header, "state_root", &header_buf));
    buffer_append(&proof, json_get_bytes(header, "body_root", &header_buf));
  }

  block_proof->sync_aggregate = src_block->sync_aggregate;
  block_proof->historic_proof = proof.data.len ? bytes_dup(proof.data) : bytes(NULL, 0);
  buffer_reset(&proof);
  buffer_add_le(&proof, json_get_uint64(proof_header, "slot"), 8);
  buffer_add_le(&proof, json_get_uint64(proof_header, "proposer_index"), 8);
  buffer_append(&proof, json_get_bytes(proof_header, "parent_root", &header_buf));
  buffer_append(&proof, json_get_bytes(proof_header, "state_root", &header_buf));
  buffer_append(&proof, json_get_bytes(proof_header, "body_root", &header_buf));

  block_proof->proof_header = proof.data;
  block_proof->type         = HISTORIC_PROOF_HEADER;
  return C4_SUCCESS;
}

static c4_status_t check_historic_proof_direct(prover_ctx_t* ctx, blockroot_proof_t* block_proof, beacon_block_t* src_block) {
  uint64_t            slot          = src_block->slot;
  c4_status_t         status        = C4_SUCCESS;
  beacon_block_t      block         = {0};
  json_t              history_proof = {0};
  uint8_t             tmp[200]      = {0};
  buffer_t            buf           = stack_buffer(tmp);
  buffer_t            buf2          = stack_buffer(tmp);
  const chain_spec_t* chain         = c4_eth_get_chain_spec(ctx->chain_id);
  bytes_t             blocks        = {0};
  beacon_type_t       beacon_type   = BEACON_TYPE_NONE;

  if (chain == NULL) THROW_ERROR("unsupported chain id!");
  if (!ctx->client_state.len || !(ctx->flags & C4_PROVER_FLAG_CHAIN_STORE)) return C4_SUCCESS; // no client state means we can't check for historic proofs and assume we simply use the synccommittee for this block.
  uint64_t state_period = block_proof->sync.oldest_period;                                     // this is the oldest period we have in the client state
  uint64_t block_period = block_proof->sync.required_period;                                   // the period of the target block
  if (!state_period) return C4_SUCCESS;                                                        // the client does not have a state yet, so he might as well get the head and verify the block.
  if (block_period >= state_period) return C4_SUCCESS;                                         // the target block is within the current range of the client

  TRY_ADD_ASYNC(status, c4_beacon_get_block_for_eth(ctx, json_parse("\"latest\""), &block)); // we get the latest because we know for latest we get the a proof for the state. Older sztates are not stored
  TRY_ADD_ASYNC(status, get_beacon_type(ctx, &beacon_type));                                 // make sure we which beacon client we are using
  TRY_ASYNC(status);                                                                         // wait for all async requests to finish

  TRY_ADD_ASYNC(status, c4_send_beacon_json(ctx, bprintf(&buf, beacon_type == BEACON_TYPE_NIMBUS ? "nimbus/v1/debug/beacon/states/0x%b/historical_summaries" : "eth/v1/lodestar/historical_summaries/0x%b", ssz_get(&block.header, "stateRoot").bytes), NULL, 120, &history_proof));
  TRY_ADD_ASYNC(status, c4_send_internal_request(ctx, bprintf(&buf2, "chain_store/%d/%d/blocks.ssz", (uint32_t) ctx->chain_id, block_period), NULL, 0, &blocks)); // get the blockd
  TRY_ASYNC(status);                                                                                                                                              // finish requests before continuing

  fork_id_t fork           = c4_chain_fork_id(ctx->chain_id, epoch_for_slot(block.slot, chain)); // current fork for the state
  json_t    data           = json_get(history_proof, "data");                                    // the the main json-object
  uint32_t  summary_idx    = block_period - 758;                                                 // the index starting from the  cappella fork, where we got zhe first Summary entry.
  uint32_t  block_idx      = slot % 8192;                                                        // idx within the period
  gindex_t  summaries_gidx = (fork >= C4_FORK_ELECTRA ? 64 : 32) + 27;                           // the gindex of the field for the summaries in the state. summaries have the index 27 in the state.
  gindex_t  period_gidx    = ssz_gindex(&SUMMARIES, 2, summary_idx, "block_summary_root");       // the gindex of the single summary-object we need to proof
  gindex_t  block_gidx     = ssz_gindex(&BLOCKS, 1, block_idx);
  ssz_ob_t  blocks_ob      = {.bytes = blocks, .def = &BLOCKS};
  buffer_t  full_proof     = {0};
  buffer_t  list_data      = {0};
  bytes32_t root           = {0};
  bytes32_t body_root      = {0};
  bytes32_t blocks_root    = {0};

  // create summary-list
  json_for_each_value(json_get(data, "historical_summaries"), entry) {
    buffer_append(&list_data, json_get_bytes(entry, "block_summary_root", &buf));
    buffer_append(&list_data, json_get_bytes(entry, "state_summary_root", &buf));
  }

  // create the proofs
  ssz_ob_t summaries_ob           = {.bytes = list_data.data, .def = &SUMMARIES};
  bytes_t  block_idx_proof        = ssz_create_proof(blocks_ob, blocks_root, block_gidx);
  bytes_t  period_idx_proof       = ssz_create_proof(summaries_ob, root, period_gidx);
  bytes_t  block_root_expected    = ssz_at(blocks_ob, block_idx).bytes;
  ssz_ob_t summary_ob             = ssz_at(summaries_ob, summary_idx);
  bytes_t  blocks_root_in_summary = ssz_get(&summary_ob, "block_summary_root").bytes;

  if (memcmp(blocks_root, blocks_root_in_summary.data, 32) != 0) {
    log_info("block_root_expected: 0x%b", block_root_expected);
    log_info("blocks_root1: 0x%b", bytes(blocks_root, 32));
    log_info("blocks_root_in_summary: 0x%b", blocks_root_in_summary);

    safe_free(block_idx_proof.data);
    safe_free(period_idx_proof.data);
    safe_free(list_data.data.data);
    THROW_ERROR("blocks_root mismatch");
  }

  // combine the proofs
  buffer_append(&full_proof, block_idx_proof);
  buffer_append(&full_proof, period_idx_proof);               // add the proof from summary to the root of the list.
  json_for_each_value(json_get(data, "proof"), entry)         // add the proof from the root of the list to the root of the state.
      buffer_append(&full_proof, json_as_bytes(entry, &buf)); // as provided by lodestar

  // calc header
  ssz_hash_tree_root(block.body, body_root);
  block_proof->historic_proof = full_proof.data;
  block_proof->gindex         = ssz_add_gindex(ssz_add_gindex(summaries_gidx, period_gidx), block_gidx);
  block_proof->sync_aggregate = block.sync_aggregate;
  block_proof->proof_header   = bytes(safe_malloc(112), 112);
  block_proof->type           = HISTORIC_PROOF_DIRECT;
  memcpy(block_proof->proof_header.data, block.header.bytes.data, 112 - 32);
  memcpy(block_proof->proof_header.data + 112 - 32, body_root, 32);

  safe_free(block_idx_proof.data);
  safe_free(period_idx_proof.data);
  safe_free(list_data.data.data);

  return C4_SUCCESS;
}

void ssz_add_header_proof(ssz_builder_t* builder, beacon_block_t* block_data, blockroot_proof_t block_proof) {
  ssz_builder_t bp             = ssz_builder_for_def(ssz_get_def(builder->def, "header_proof")->def.container.elements + block_proof.type);
  ssz_ob_t      sync_aggregate = block_proof.sync_aggregate;

  switch (block_proof.type) {
    case HISTORIC_PROOF_HEADER:
      ssz_add_bytes(&bp, "headers", block_proof.historic_proof);
      ssz_add_bytes(&bp, "header", block_proof.proof_header);
      break;

    case HISTORIC_PROOF_DIRECT: {
      ssz_add_bytes(&bp, "proof", block_proof.historic_proof);
      ssz_add_bytes(&bp, "header", block_proof.proof_header);
      ssz_add_uint64(&bp, (uint64_t) block_proof.gindex);
      break;
    }
    case HISTORIC_PROOF_NONE:
      sync_aggregate = block_data->sync_aggregate;
      break;
  }
  ssz_add_bytes(&bp, "sync_committee_bits", ssz_get(&sync_aggregate, "syncCommitteeBits").bytes);
  ssz_add_bytes(&bp, "sync_committee_signature", ssz_get(&sync_aggregate, "syncCommitteeSignature").bytes);

  ssz_add_builders(builder, "header_proof", bp);
}

void c4_free_block_proof(blockroot_proof_t* block_proof) {
  if (block_proof->type == HISTORIC_PROOF_NONE) return;
  safe_free(block_proof->historic_proof.data);
  safe_free(block_proof->proof_header.data);
}

static c4_status_t fetch_bootstrap_data(prover_ctx_t* ctx, syncdata_state_t* sync_data, ssz_ob_t* bootstrap) {
  if (!sync_data->checkpoint) return C4_SUCCESS;
  char path[200] = {0};
  sbprintf(path, "eth/v1/beacon/light_client/bootstrap/0x%x", bytes(sync_data->checkpoint, 32));
  // send request for checkpoint
  ssz_ob_t result = {0};
  //  ssz_def_t def    = SSZ_CONTAINER("bootstrap", ELECTRA_LIGHT_CLIENT_BOOTSTRAP);
  TRY_ASYNC(c4_send_beacon_ssz(ctx, path, NULL, NULL, DEFAULT_TTL, &result));

  const ssz_def_t* bootstrap_union_def = ssz_get_def(C4_ETH_REQUEST_SYNCDATA_UNION + 1, "bootstrap");
  fork_id_t        fork                = c4_eth_get_fork_for_lcu(ctx->chain_id, result.bytes);
  result.def                           = &bootstrap_union_def->def.container.elements[fork == C4_FORK_DENEB ? 1 : 2]; // get the correct bootstrap definition for the fork
  if (!ssz_is_valid(result, true, &ctx->state)) THROW_ERROR("Invalid bootstrap data!");
  *bootstrap = result;

  return C4_SUCCESS;
}

static c4_status_t fetch_updates_data(prover_ctx_t* ctx, syncdata_state_t* sync_data, ssz_builder_t* updates) {
  ssz_ob_t result     = {0};
  uint32_t count      = (uint32_t) (sync_data->required_period - sync_data->newest_period);
  char     query[100] = {0};
  sbprintf(query, "start_period=%l&count=%l", sync_data->newest_period, sync_data->required_period - sync_data->newest_period);
  TRY_ASYNC(c4_send_beacon_ssz(ctx, "eth/v1/beacon/light_client/updates", query, NULL, DEFAULT_TTL, &result));

  if (!updates) return C4_SUCCESS;

  bytes_t  client_updates = result.bytes;
  uint64_t length         = 0;
  for (uint32_t pos = 0; pos + UPDATE_PREFIX_SIZE < client_updates.len; pos += length + SSZ_LENGTH_SIZE) {
    uint32_t data_offset        = pos + SSZ_LENGTH_SIZE + SSZ_OFFSET_SIZE;
    uint32_t data_length_offset = SSZ_OFFSET_SIZE;
    length                      = uint64_from_le(client_updates.data + pos);

    if (pos + SSZ_LENGTH_SIZE + length > client_updates.len && length > UPDATE_PREFIX_SIZE) break;

    bytes_t   client_update_bytes = bytes(client_updates.data + data_offset, length - data_length_offset);
    fork_id_t fork                = c4_eth_get_fork_for_lcu(ctx->chain_id, client_update_bytes);
    ssz_ob_t  update              = {.bytes = client_update_bytes, .def = eth_get_light_client_update(fork)};
    if (!update.def) THROW_ERROR("Invalid update data!");

    bytes_t prefixed = bytes(safe_malloc(update.bytes.len + 1), update.bytes.len + 1);
    memcpy(prefixed.data + 1, update.bytes.data, update.bytes.len);
    prefixed.data[0] = (uint8_t) (fork == C4_FORK_DENEB ? 0 : 1);
    ssz_add_dynamic_list_bytes(updates, count, prefixed);
    safe_free(prefixed.data);
  }

  return C4_SUCCESS;
}

c4_status_t c4_get_syncdata_proof(prover_ctx_t* ctx, syncdata_state_t* sync_data, ssz_builder_t* builder) {
  // nothing to be done - no data to be added.
  if ((ctx->flags & C4_PROVER_FLAG_INCLUDE_SYNC) == 0) return C4_SUCCESS;
  if (sync_data->checkpoint_period == 0 && sync_data->required_period <= sync_data->newest_period) return C4_SUCCESS;

  builder->def            = C4_ETH_REQUEST_SYNCDATA_UNION + 1; // TODO find a way to better handle this in the future, so updates on ssz will not break the build.
  ssz_ob_t      bootstrap = {.def = &ssz_none};
  ssz_builder_t updates   = ssz_builder_for_def(ssz_get_def(builder->def, "update"));
  if (sync_data->checkpoint_period) TRY_ASYNC(fetch_bootstrap_data(ctx, sync_data, &bootstrap));
  if (sync_data->required_period > sync_data->newest_period) TRY_ASYNC(fetch_updates_data(ctx, sync_data, &updates));

  ssz_add_ob(builder, "bootstrap", bootstrap);
  ssz_add_builders(builder, "update", updates);
  return C4_SUCCESS;
}

/**
 * updates the sync_data, but also runs the request to fetch the bootstrap or updates data.
 */
static c4_status_t update_syncdata_state(prover_ctx_t* ctx, syncdata_state_t* sync_data, const chain_spec_t* chain) {
  if (!ctx->client_state.data || !ctx->client_state.len || !sync_data || !chain) return C4_SUCCESS;
  c4_chain_state_t chain_state = c4_state_deserialize(ctx->client_state);
  sync_data->status            = chain_state.status;
  switch (sync_data->status) {
    case C4_STATE_SYNC_EMPTY: return C4_SUCCESS;
    case C4_STATE_SYNC_PERIODS:
      for (int i = 0; i < MAX_SYNC_PERIODS && chain_state.data.periods[i]; i++) {
        if (!sync_data->oldest_period || chain_state.data.periods[i] < sync_data->oldest_period) sync_data->oldest_period = chain_state.data.periods[i];
        if (!sync_data->newest_period || chain_state.data.periods[i] > sync_data->newest_period) sync_data->newest_period = chain_state.data.periods[i];
      }
      break;
    case C4_STATE_SYNC_CHECKPOINT: {
      if ((ctx->flags & C4_PROVER_FLAG_INCLUDE_SYNC) == 0) return C4_SUCCESS;
      // we put the pointer to ctx->client_state.data + 1 because the first byte is the status of the sync data.
      sync_data->checkpoint = ctx->client_state.data + 1; // chain_state.data.checkpoint;
      ssz_ob_t result       = {0};
      TRY_ASYNC(fetch_bootstrap_data(ctx, sync_data, &result));

      ssz_ob_t header              = ssz_get(&result, "header");
      ssz_ob_t beacon              = ssz_get(&header, "beacon");
      sync_data->checkpoint_period = (uint64_t) (ssz_get_uint64(&beacon, "slot") >> (chain->epochs_per_period_bits + chain->slots_per_epoch_bits));
      sync_data->newest_period     = sync_data->checkpoint_period;
      sync_data->oldest_period     = sync_data->checkpoint_period;

      break;
    }
  }

  if ((ctx->flags & C4_PROVER_FLAG_INCLUDE_SYNC) && sync_data->newest_period < sync_data->required_period)
    return fetch_updates_data(ctx, sync_data, NULL);
  return C4_SUCCESS;
}

c4_status_t c4_check_blockroot_proof(prover_ctx_t* ctx, blockroot_proof_t* block_proof, beacon_block_t* src_block) {
  const chain_spec_t* chain = c4_eth_get_chain_spec(ctx->chain_id);
  if (!chain) THROW_ERROR("unsupported chain id!");

  // set the periods in the sync daata
  // we also allow pending requests here
  block_proof->sync.required_period = max64(block_proof->sync.required_period, (uint64_t) (src_block->slot >> (chain->epochs_per_period_bits + chain->slots_per_epoch_bits)));
  c4_status_t update_status         = update_syncdata_state(ctx, &block_proof->sync, chain);

  // we continue, if only light_clientupdates are pending, but we wait for checkpoints, since we need to make decisions based on the checkpoint period.
  if (update_status == C4_ERROR || (update_status == C4_PENDING && block_proof->sync.checkpoint && !block_proof->sync.checkpoint_period)) return update_status;

  // should we use historic summaries?
  TRY_ASYNC(check_historic_proof_direct(ctx, block_proof, src_block));
  if (block_proof->historic_proof.len) return update_status;

  // no proof means we use the current, but do we we need headers-proof?
  TRY_ASYNC(check_historic_proof_header(ctx, block_proof, src_block));
  return update_status;
}
