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
#include "../server/eth_clients.h"
#include "beacon.h"
#include "beacon_types.h"
#include "eth_compute_units.h"
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

static const ssz_def_t HISTORICAL_SUMMARY[] = {
    SSZ_BYTES32("block_summary_root"),
    SSZ_BYTES32("state_summary_root")};
static const ssz_def_t HISTORICAL_SUMMARY_CONTAINER = SSZ_CONTAINER("HISTORICAL_SUMMARY", HISTORICAL_SUMMARY);
static const ssz_def_t SUMMARIES                    = SSZ_LIST("summaries", HISTORICAL_SUMMARY_CONTAINER, 1 << 24);
static const ssz_def_t BLOCKS                       = SSZ_VECTOR("blocks", ssz_bytes32, 8192);

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
    eth_cu_add(ctx, CU_HISTORIC_HEADER_HOP); // each successful hop in the header chain
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

static c4_status_t get_historical_summaries(prover_ctx_t* ctx, beacon_block_t* block, json_t* history_proof) {
  if (ctx->state.error) return C4_ERROR;
  uint8_t  tmp[200] = {0};
  buffer_t buf      = stack_buffer(tmp);

  return c4_send_beacon_json_with_client_type(ctx, bprintf(&buf, "eth/v1/lodestar/states/0x%b/historical_summaries", ssz_get(&block->header, "stateRoot").bytes), NULL, 120, history_proof, BEACON_CLIENT_LODESTAR);
  /*
  json_t      history_proof2 = {0};
  c4_status_t status1        = c4_send_beacon_json_with_client_type(ctx, bprintf(&buf, "nimbus/v1/debug/beacon/states/0x%b/historical_summaries", ssz_get(&block->header, "stateRoot").bytes), NULL, 120, &history_proof1, BEACON_CLIENT_NIMBUS);
  if (ctx->state.error) {
    safe_free(ctx->state.error);
    ctx->state.error = NULL;
    status1          = C4_ERROR;
  }
  */
  /*
   // /eth/v1/lodestar/states/{state_id}/historical_summaries
   buffer_reset(&buf);
   c4_status_t status2 = c4_send_beacon_json_with_client_type(ctx, bprintf(&buf, "eth/v1/lodestar/states/0x%b/historical_summaries", ssz_get(&block->header, "stateRoot").bytes), NULL, 120, &history_proof2, BEACON_CLIENT_LODESTAR);
   if (ctx->state.error) {
     safe_free(ctx->state.error);
     ctx->state.error = NULL;
     status2          = C4_ERROR;
   }

   if (status1 == C4_SUCCESS && history_proof1.type == JSON_TYPE_OBJECT)
     *history_proof = history_proof1;
   else if (status2 == C4_SUCCESS && history_proof2.type == JSON_TYPE_OBJECT)
     *history_proof = history_proof2;
   else if (status1 == C4_PENDING)
     return C4_PENDING;
   else
     THROW_ERROR("Failed to get historical summaries! Looks like it is not supported by the beacon client!");
   return C4_SUCCESS;
   */
}

// Builds the concatenated merkle proof from a block root in `block_period` to
// the `state_root` of a recent beacon state, using `historical_summaries` of
// that recent state. Only used by `check_historic_proof_direct` below.
static c4_status_t build_historic_merkle_proof(
    prover_ctx_t* ctx,
    uint64_t      block_period,
    uint64_t      block_idx,
    bytes_t       blocks_roots,
    json_t        history_proof,
    uint64_t      recent_state_slot,
    bytes_t*      out_proof,
    gindex_t*     out_gindex) {

  if (!out_proof || !out_gindex) return c4_state_add_error(&ctx->state, "invalid output pointers!");
  *out_proof  = NULL_BYTES;
  *out_gindex = 0;

  const chain_spec_t* chain = c4_eth_get_chain_spec(ctx->chain_id);
  if (chain == NULL) return c4_state_add_error(&ctx->state, "unsupported chain id!");

  uint8_t  tmp[200] = {0};
  buffer_t buf      = stack_buffer(tmp);

  uint32_t offset_period = (uint32_t) (chain->fork_epochs[C4_FORK_BELLATRIX] >> chain->epochs_per_period_bits);
  // historical_summaries first appears at the Capella fork; periods before
  // that have no corresponding summary entry, so refuse to build a proof
  // (otherwise `summary_idx` would underflow uint64 -> uint32).
  if (block_period < offset_period)
    return c4_state_add_error(&ctx->state, "block_period predates historical_summaries fork");
  fork_id_t fork             = c4_chain_fork_id(ctx->chain_id, epoch_for_slot(recent_state_slot, chain)); // fork for the recent state
  json_t    data             = json_get(history_proof, "data");
  uint32_t  summary_idx      = (uint32_t) (block_period - offset_period);                                 // index from Capella fork (first summary entry)
  gindex_t  summaries_gidx   = (fork >= C4_FORK_ELECTRA ? 64 : 32) + 27;                                  // summaries field gindex in the state (idx 27)
  gindex_t  period_gidx      = ssz_gindex(&SUMMARIES, 2, summary_idx, "block_summary_root");              // gindex of the summary object
  gindex_t  block_gidx       = ssz_gindex(&BLOCKS, 1, block_idx);
  ssz_ob_t  blocks_ob        = {.bytes = blocks_roots, .def = &BLOCKS};
  buffer_t  full_proof       = {0};
  buffer_t  list_data        = {0};
  bytes32_t root             = {0};
  bytes32_t blocks_root      = {0};

  // Build summaries list from JSON
  json_for_each_value(json_get(data, "historical_summaries"), entry) {
    buffer_append(&list_data, json_get_bytes(entry, "block_summary_root", &buf));
    buffer_append(&list_data, json_get_bytes(entry, "state_summary_root", &buf));
  }

  ssz_ob_t summaries_ob     = {.bytes = list_data.data, .def = &SUMMARIES};
  bytes_t  block_idx_proof  = ssz_create_proof(blocks_ob, blocks_root, block_gidx);
  bytes_t  period_idx_proof = ssz_create_proof(summaries_ob, root, period_gidx);

  // Sanity: blocks_root we just computed must match the block_summary_root
  // recorded in the historical_summaries list for this period.
  ssz_ob_t summary_ob             = ssz_at(summaries_ob, summary_idx);
  bytes_t  blocks_root_in_summary = ssz_get(&summary_ob, "block_summary_root").bytes;
  if (memcmp(blocks_root, blocks_root_in_summary.data, 32) != 0) {
    log_info("blocks_root computed:    0x%b", bytes(blocks_root, 32));
    log_info("blocks_root in summary:  0x%b", blocks_root_in_summary);

    safe_free(block_idx_proof.data);
    safe_free(period_idx_proof.data);
    safe_free(list_data.data.data);
    return c4_state_add_error(&ctx->state, "blocks_root mismatch");
  }

  buffer_append(&full_proof, block_idx_proof);
  buffer_append(&full_proof, period_idx_proof);
  json_for_each_value(json_get(data, "proof"), entry)
      buffer_append(&full_proof, json_as_bytes(entry, &buf));

  *out_proof  = full_proof.data;
  *out_gindex = ssz_add_gindex(ssz_add_gindex(summaries_gidx, period_gidx), block_gidx);

  safe_free(block_idx_proof.data);
  safe_free(period_idx_proof.data);
  safe_free(list_data.data.data);

  return C4_SUCCESS;
}

static c4_status_t check_historic_proof_direct(prover_ctx_t* ctx, blockroot_proof_t* block_proof, beacon_block_t* src_block) {
  uint64_t            slot          = src_block->slot;
  c4_status_t         status        = C4_SUCCESS;
  beacon_block_t      block         = {0};
  json_t              history_proof = {0};
  uint8_t             tmp[200]      = {0};
  buffer_t            buf2          = stack_buffer(tmp);
  const chain_spec_t* chain         = c4_eth_get_chain_spec(ctx->chain_id);
  bytes_t             blocks        = {0};

  if (chain == NULL) THROW_ERROR("unsupported chain id!");
  if (!ctx->client_state.len || !(ctx->flags & C4_PROVER_FLAG_CHAIN_STORE)) return C4_SUCCESS; // no client state means we can't check for historic proofs and assume we simply use the synccommittee for this block.
  uint64_t state_period = block_proof->sync.oldest_period;                                     // this is the oldest period we have in the client state
  uint64_t block_period = block_proof->sync.required_period;                                   // the period of the target block
  if (!state_period) return C4_SUCCESS;                                                        // the client does not have a state yet, so he might as well get the head and verify the block.
  if (block_period >= state_period) return C4_SUCCESS;                                         // the target block is within the current range of the client

  // Historic-direct path: the actual sub-requests below are billed via their
  // respective helpers; this constant covers the server-side composition work
  // (summary list build, gindex math, proof concatenation, hash_tree_root).
  eth_cu_add(ctx, CU_HISTORIC_DIRECT);

  TRY_ASYNC(c4_beacon_get_block_for_eth(ctx, json_parse("\"latest\""), &block)); // we get the latest because we know for latest we get the a proof for the state. Older sztates are not stored
  TRY_ADD_ASYNC(status, get_historical_summaries(ctx, &block, &history_proof));
  TRY_ADD_ASYNC(status, c4_send_internal_request(ctx, bprintf(&buf2, "period_store/%d/blocks.ssz", block_period), NULL, 0, &blocks)); // get the blockd
  TRY_ASYNC(status);                                                                                                                  // finish requests before continuing

  bytes_t   historic_proof = {0};
  gindex_t  combined_gidx  = 0;
  bytes32_t body_root      = {0};
  // CU accounting for the SSZ proof construction below (in addition to the
  // CU_HISTORIC_DIRECT base above): two single-leaf merkle proofs over the
  // 8192-block roots and the historical_summaries list.
  eth_cu_add(ctx, 2 * CU_SSZ_PROOF);
  TRY_ASYNC(build_historic_merkle_proof(ctx, block_period, slot % 8192, blocks, history_proof, block.slot, &historic_proof, &combined_gidx));

  ssz_hash_tree_root(block.body, body_root);
  block_proof->historic_proof = historic_proof;
  block_proof->gindex         = combined_gidx;
  block_proof->sync_aggregate = block.sync_aggregate;
  block_proof->proof_header   = bytes(safe_malloc(112), 112);
  block_proof->type           = HISTORIC_PROOF_DIRECT;
  memcpy(block_proof->proof_header.data, block.header.bytes.data, 112 - 32);
  memcpy(block_proof->proof_header.data + 112 - 32, body_root, 32);

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

// Fetch and SSZ-validate a LightClientBootstrap for an explicit beacon block root.
// On success `*out_bootstrap` is set to a typed SSZ object pointing at the response
// bytes; the request layer owns the underlying buffer (lives for the prover_ctx).
static c4_status_t fetch_bootstrap_by_root(prover_ctx_t* ctx, bytes32_t header_root, ssz_ob_t* out_bootstrap) {
  char path[200] = {0};
  sbprintf(path, "eth/v1/beacon/light_client/bootstrap/0x%x", bytes(header_root, 32));
  ssz_ob_t result = {0};
  TRY_ASYNC(c4_send_beacon_ssz(ctx, path, NULL, NULL, DEFAULT_TTL, &result));

  const ssz_def_t* bootstrap_union_def = ssz_get_def(C4_ETH_REQUEST_SYNCDATA_UNION + 1, "bootstrap");
  fork_id_t        fork                = c4_eth_get_fork_for_lcu(ctx->chain_id, result.bytes);
  if (fork == 0) THROW_ERROR("Invalid bootstrap data: cannot determine fork!");
  // Mirror the verifier-side mapping in `sync_committee_state.c`: pre-Electra forks
  // (incl. Deneb) use the Deneb container; Electra and later use the Electra container.
  result.def = &bootstrap_union_def->def.container.elements[fork <= C4_FORK_DENEB ? 1 : 2];
  if (!ssz_is_valid(result, true, &ctx->state)) THROW_ERROR("Invalid bootstrap data!");
  *out_bootstrap = result;
  return C4_SUCCESS;
}

static c4_status_t fetch_bootstrap_data(prover_ctx_t* ctx, syncdata_state_t* sync_data, ssz_ob_t* bootstrap) {
  if (!sync_data->checkpoint) return C4_SUCCESS;
  return fetch_bootstrap_by_root(ctx, sync_data->checkpoint, bootstrap);
}

// Build the ETH_CHECKPOINT_PROOF SSZ object from a parsed LightClientBootstrap.
// On success the caller owns `out->bytes.data` (must safe_free after use).
//
// Note: `currentSyncCommitteeBranch` in the bootstrap is a fixed-depth VECTOR
// (5 for Deneb, 6 for Electra); the CheckpointProof's `proof` is a LIST to keep
// one container valid across both forks. The on-wire byte layout is identical
// (concatenated 32-byte chunks), so we copy the raw branch bytes through.
//
// The field list is duplicated from `ETH_CHECKPOINT_PROOF` in
// `src/chains/eth/ssz/verify_proof_types.h` because that header is not
// self-contained (it relies on TU-local static defs from `verify_types.c`).
// SSZ field order and types must stay in sync; both are validated to produce
// identical hash_tree_roots in `test_wsp_checkpoint_proof.c`.
static const ssz_def_t LOCAL_ETH_CHECKPOINT_PROOF[] = {
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER),
    SSZ_BYTE_VECTOR("aggregate_pubkey", 48),
    SSZ_LIST("proof", ssz_bytes32, 16)};
static const ssz_def_t CHECKPOINT_PROOF_CONTAINER = SSZ_CONTAINER("checkpoint_proof", LOCAL_ETH_CHECKPOINT_PROOF);

static c4_status_t build_checkpoint_proof_ob(ssz_ob_t bootstrap, ssz_ob_t* out) {
  ssz_ob_t header           = ssz_get(&bootstrap, "header");
  ssz_ob_t beacon           = ssz_get(&header, "beacon");
  ssz_ob_t current_sync     = ssz_get(&bootstrap, "currentSyncCommittee");
  ssz_ob_t aggregate_pubkey = ssz_get(&current_sync, "aggregatePubkey");
  ssz_ob_t branch           = ssz_get(&bootstrap, "currentSyncCommitteeBranch");

  if (beacon.bytes.len == 0 || aggregate_pubkey.bytes.len != 48 || branch.bytes.len == 0 || (branch.bytes.len % 32) != 0)
    return C4_ERROR; // bootstrap was already SSZ-validated -- this is defensive

  ssz_builder_t bp = ssz_builder_for_def(&CHECKPOINT_PROOF_CONTAINER);
  ssz_add_bytes(&bp, "header", beacon.bytes);
  ssz_add_bytes(&bp, "aggregate_pubkey", aggregate_pubkey.bytes);
  ssz_add_bytes(&bp, "proof", branch.bytes);
  *out = ssz_builder_to_bytes(&bp);
  return C4_SUCCESS;
}

// Fetch the current finalized BeaconBlock, then a LightClientBootstrap for its
// block_root, and build the slim CheckpointProof from it. On success the caller
// owns `out->bytes.data`. Used by both the ZK and LC sync-data paths so they
// share the same async sequencing and avoid duplicated fin-block roundtrips.
static c4_status_t fetch_finalized_checkpoint_proof(prover_ctx_t* ctx, ssz_ob_t* out) {
  beacon_block_t fin = {0};
  TRY_ASYNC(c4_beacon_get_block_for_eth(ctx, json_parse("\"finalized\""), &fin));
  ssz_ob_t bootstrap = {0};
  TRY_ASYNC(fetch_bootstrap_by_root(ctx, fin.data_block_root, &bootstrap));
  return build_checkpoint_proof_ob(bootstrap, out);
}

static c4_status_t fetch_updates_data(prover_ctx_t* ctx, syncdata_state_t* sync_data, ssz_builder_t* updates) {
  ssz_ob_t result     = {0};
  uint32_t count      = (uint32_t) (sync_data->required_period - sync_data->newest_period);
  char     query[100] = {0};
  sbprintf(query, "start_period=%l&count=%l", sync_data->newest_period, sync_data->required_period - sync_data->newest_period);
//  if (ctx->flags & C4_PROVER_FLAG_CHAIN_STORE)
//    TRY_ASYNC(c4_send_internal_request(ctx, "lcu_updates", query, 0, &result.bytes));
//  else
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
  if (ctx->flags & C4_PROVER_FLAG_HYBRID) return C4_SUCCESS; // no need to handle this for hybrid mode.
  if ((ctx->flags & C4_PROVER_FLAG_INCLUDE_SYNC) == 0 && !(ctx->flags & C4_PROVER_FLAG_ZK_PROOF)) return C4_SUCCESS;
  if (ctx->flags & C4_PROVER_FLAG_ZK_PROOF && (ctx->flags & C4_PROVER_FLAG_CHAIN_STORE)==0) return C4_SUCCESS;
  if ((ctx->flags & C4_PROVER_FLAG_ZK_PROOF) && ((sync_data->newest_period == 0 && sync_data->checkpoint_period == 0) ||
                                                 (sync_data->newest_period && sync_data->newest_period < sync_data->required_period))) {
    // we need a zk_proof (if available) for the required period.
    builder->def             = C4_ETH_REQUEST_SYNCDATA_UNION + 2; // TODO find a way to better handle this in the future, so updates on ssz will not break the build.
    zk_proof_data_t zk_proof = {0};
    eth_cu_add(ctx, CU_ZK_PROOF_INCLUDE); // ZK proof attached to the sync section

    // The witness-key path keeps the original header_proof checkpoint embedded in
    // `zk_proof.ssz` because the witness BLS signatures vouch for the signed header
    // directly. Without witness keys we anchor the sync committee instead against an
    // independently checkpointz-confirmed LightClientBootstrap (double-trust model):
    // both the verified ZK proof's pubkeys and the bootstrap's currentSyncCommittee
    // must hash to the same root -- an attacker would have to compromise the ZK
    // anchor AND the checkpointz provider.
    //
    // Async sequencing: build the checkpoint_proof first (may return PENDING on
    // beacon/bootstrap roundtrips, in which case `checkpoint_ob` stays zero-initialised
    // and no cleanup is needed); fetch the ZK proof second (allocates signatures);
    // then assemble the builder. This ordering guarantees no leak on any PENDING return.
    bool     need_checkpoint_proof = (ctx->witness_key.len == 0);
    ssz_ob_t checkpoint_ob         = {0};
    if (need_checkpoint_proof)
      TRY_ASYNC(fetch_finalized_checkpoint_proof(ctx, &checkpoint_ob));

    c4_status_t zk_status = c4_fetch_zk_proof_data(ctx, &zk_proof, sync_data->required_period);
    if (zk_status != C4_SUCCESS) {
      safe_free(checkpoint_ob.bytes.data);
      return zk_status;
    }
    ssz_add_bytes(builder, "vk_hash", ssz_get(&zk_proof.sync_proof, "vk_hash").bytes);
    ssz_add_bytes(builder, "proof", ssz_get(&zk_proof.sync_proof, "proof").bytes);
    ssz_add_ob(builder, "header", ssz_get(&zk_proof.sync_proof, "header"));
    ssz_add_ob(builder, "pubkeys", ssz_get(&zk_proof.sync_proof, "pubkeys"));

    if (need_checkpoint_proof) {
      ssz_add_ob(builder, "checkpoint", checkpoint_ob);
      safe_free(checkpoint_ob.bytes.data);
    }
    else {
      ssz_add_ob(builder, "checkpoint", ssz_get(&zk_proof.sync_proof, "checkpoint"));
    }

    ssz_add_bytes(builder, "signatures", zk_proof.signatures);
    safe_free(zk_proof.signatures.data);
    return C4_SUCCESS;
  }
  if (sync_data->checkpoint_period == 0 && sync_data->required_period <= sync_data->newest_period) return C4_SUCCESS;

  builder->def            = C4_ETH_REQUEST_SYNCDATA_UNION + 1; // TODO find a way to better handle this in the future, so updates on ssz will not break the build.
  ssz_ob_t      bootstrap = {.def = &ssz_none};
  ssz_ob_t      cp_ob     = {0}; // owns checkpoint_proof bytes (free at end)
  ssz_builder_t updates   = ssz_builder_for_def(ssz_get_def(builder->def, "update"));

  if (sync_data->checkpoint_period) {
    // Client supplied a checkpoint -- this is the bootstrap-init use case; the full
    // LightClientBootstrap binds the verifier to that trusted checkpoint directly.
    TRY_ASYNC(fetch_bootstrap_data(ctx, sync_data, &bootstrap));
  }
  else {
    // No client checkpoint. If the update path crosses the Weak Subjectivity Period
    // (almost always true once a single period was missed -- WSP = 256 epochs = 1 period),
    // the verifier has no canonical anchor: the LCU chain alone is vulnerable to a
    // long-range attack. Attach a CheckpointProof built from a fresh, checkpointz-
    // anchored LightClientBootstrap so the verifier can cross-check the chain-of-trust
    // pubkeys (LCU chain tail) against the bootstrap's currentSyncCommittee.
    //
    // Note: the chain spec is dereferenced inside the `if (chain && ...)` guard, not
    // before, so a missing spec (unsupported chain) skips the WSP path cleanly.
    const chain_spec_t* chain = c4_eth_get_chain_spec(ctx->chain_id);
    if (chain && sync_data->required_period > sync_data->newest_period) {
      uint64_t epoch_gap = ((uint64_t) (sync_data->required_period - sync_data->newest_period)) << chain->epochs_per_period_bits;
      if (epoch_gap > chain->weak_subjectivity_epochs) {
        TRY_ASYNC(fetch_finalized_checkpoint_proof(ctx, &cp_ob));
        bootstrap = cp_ob;
      }
    }
  }

  // After this point `cp_ob` may own heap memory; any early-return must `safe_free` it.
  // `fetch_updates_data` can return PENDING, so we cannot rely on TRY_ASYNC here.
  if (sync_data->required_period > sync_data->newest_period) {
    c4_status_t updates_status = fetch_updates_data(ctx, sync_data, &updates);
    if (updates_status != C4_SUCCESS) {
      safe_free(cp_ob.bytes.data);
      return updates_status;
    }
  }

  ssz_add_ob(builder, "bootstrap", bootstrap);
  ssz_add_builders(builder, "update", updates);
  safe_free(cp_ob.bytes.data);
  return C4_SUCCESS;
}

/**
 * updates the sync_data, but also runs the request to fetch the bootstrap or updates data.
 */
static c4_status_t update_syncdata_state(prover_ctx_t* ctx, syncdata_state_t* sync_data, const chain_spec_t* chain) {
  if (!sync_data || !chain) return C4_SUCCESS;
  zk_proof_data_t  zk_proof    = {0};
  c4_chain_state_t chain_state = c4_state_deserialize(ctx->client_state);
  sync_data->status            = chain_state.status;
  switch (sync_data->status) {
    case C4_STATE_SYNC_EMPTY:
      if (ctx->flags & C4_PROVER_FLAG_ZK_PROOF && ctx->flags & C4_PROVER_FLAG_CHAIN_STORE) {
        // we only fetch them so we safe time in case we download the proof files later.
        c4_status_t status = c4_fetch_zk_proof_data(ctx, &zk_proof, sync_data->required_period);
        if (status == C4_SUCCESS)
          safe_free(zk_proof.signatures.data);
        return status;
      }
      return C4_SUCCESS;
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

  // Long-offline edge case (e.g. 6-month gap): if the block we want to prove sits
  // more than the WSP behind the *current* canonical head, the CheckpointProof
  // anchor (built later from a fresh LightClientBootstrap in `c4_get_syncdata_proof`)
  // would be too far ahead of the chain-of-trust for the LCU/ZK pubkeys to
  // cross-check. Bump `required_period` to the current period so the chain-of-trust
  // extends all the way to the canonical anchor; the actual block validation
  // remains independent (via historical_summaries Merkle proofs against a recent
  // state). This is a no-op in the common case where the gap is within the WSP.
  if ((ctx->flags & C4_PROVER_FLAG_ZK_PROOF) && sync_data->required_period) {
    uint64_t epoch_gap = (((uint64_t) sync_data->required_period) - (uint64_t) sync_data->newest_period) << chain->epochs_per_period_bits;
    if (sync_data->newest_period && sync_data->required_period > sync_data->newest_period && epoch_gap > chain->weak_subjectivity_epochs) {
      beacon_block_t fin = {0};
      TRY_ASYNC(c4_beacon_get_block_for_eth(ctx, json_parse("\"finalized\""), &fin));
      uint32_t current_period = (uint32_t) (fin.slot >> (chain->slots_per_epoch_bits + chain->epochs_per_period_bits));
      if ((uint64_t) current_period > sync_data->required_period)
        sync_data->required_period = (uint64_t) current_period;
    }
  }

  // if there is a gap, fetch the light client updates
  if ((ctx->flags & C4_PROVER_FLAG_ZK_PROOF) && (ctx->flags & C4_PROVER_FLAG_CHAIN_STORE) && sync_data->newest_period < sync_data->required_period)
    return c4_fetch_zk_proof_data(ctx, &zk_proof, sync_data->required_period);
  else if ((ctx->flags & C4_PROVER_FLAG_INCLUDE_SYNC) && sync_data->newest_period < sync_data->required_period)
    return fetch_updates_data(ctx, sync_data, NULL);
  return C4_SUCCESS;
}

c4_status_t c4_check_blockroot_proof(prover_ctx_t* ctx, blockroot_proof_t* block_proof, beacon_block_t* src_block) {
  if (ctx->flags & C4_PROVER_FLAG_HYBRID) return C4_SUCCESS;
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
