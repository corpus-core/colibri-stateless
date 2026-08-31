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

static c4_status_t check_historic_proof_header(prover_ctx_t* ctx, blockroot_proof_t* block_proof, eth_block_t* src_block) {
  if (memcmp(src_block->beacon.data_block_root, src_block->beacon.sign_parent_root, 32) == 0) return C4_SUCCESS;
  json_t    proof_header  = {0};
  json_t    header        = {0};
  bytes32_t root          = {0};
  buffer_t  proof_headers = {0};
  bytes32_t header_data   = {0};
  buffer_t  header_buf    = stack_buffer(header_data);
  buffer_t  root_buf      = stack_buffer(root);
  buffer_t  proof         = {0};
  TRY_ASYNC(get_beacon_header(ctx, src_block->beacon.sign_parent_root, &header));
  json_get_bytes(header, "parent_root", &root_buf);
  proof_header = header;

  for (int i = 0; i <= MAX_HISTORIC_PROOF_HEADER_DEPTH; i++) {
    if (i == MAX_HISTORIC_PROOF_HEADER_DEPTH) {
      buffer_free(&proof_headers);
      THROW_ERROR("Max header limit reached!");
    }

    if (memcmp(root, src_block->beacon.data_block_root, 32) == 0) break;
    TRY_ASYNC_CATCH(get_beacon_header(ctx, root, &header), buffer_free(&proof));
    json_get_bytes(header, "parent_root", &root_buf);
    buffer_add_le(&proof, json_get_uint64(header, "slot"), 8);
    buffer_add_le(&proof, json_get_uint64(header, "proposer_index"), 8);
    buffer_append(&proof, json_get_bytes(header, "state_root", &header_buf));
    buffer_append(&proof, json_get_bytes(header, "body_root", &header_buf));
    eth_cu_add(ctx, CU_HISTORIC_HEADER_HOP); // each successful hop in the header chain
  }

  block_proof->sync_aggregate = src_block->beacon.sync_aggregate;
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

static c4_status_t get_historical_summaries(prover_ctx_t* ctx, eth_block_t* block, json_t* history_proof) {
  if (ctx->state.error) return C4_ERROR;
  uint8_t     tmp[200] = {0};
  buffer_t    buf      = stack_buffer(tmp);
  bytes_t     state    = ssz_get(&block->beacon.cl_header, "stateRoot").bytes;
  bool        nimbus   = (ctx->flags & C4_PROVER_FLAG_NIMBUS) != 0;
  const char* path     = nimbus ? "nimbus/v1/debug/beacon/states/0x%b/historical_summaries"
                                : "eth/v1/lodestar/states/0x%b/historical_summaries";
  uint32_t    client   = nimbus ? BEACON_CLIENT_NIMBUS : BEACON_CLIENT_LODESTAR;
  return c4_send_beacon_json_with_client_type(ctx, bprintf(&buf, path, state), NULL, 120, history_proof, client);
}

#ifdef TEST
c4_status_t c4_test_get_historical_summaries(prover_ctx_t* ctx, eth_block_t* block, json_t* history_proof) {
  return get_historical_summaries(ctx, block, history_proof);
}
#endif

static c4_status_t check_historic_proof_direct(prover_ctx_t* ctx, blockroot_proof_t* block_proof, eth_block_t* src_block) {
  uint64_t            slot          = src_block->slot;
  c4_status_t         status        = C4_SUCCESS;
  eth_block_t         block         = {0};
  json_t              history_proof = {0};
  uint8_t             tmp[200]      = {0};
  buffer_t            buf           = stack_buffer(tmp);
  buffer_t            buf2          = stack_buffer(tmp);
  const chain_spec_t* chain         = c4_eth_get_chain_spec(ctx->chain_id);
  bytes_t             blocks        = {0};

  if (chain == NULL) THROW_ERROR("unsupported chain id!");
  // A historic-direct proof needs the server-side period_store (blocks.ssz + summaries),
  // so CHAIN_STORE is required. We intentionally no longer bail out when the client has no
  // state: a fresh client that requests a block older than the (finalized) period it is
  // about to sync to must also receive a historical_summaries proof -- otherwise it would
  // need the block's own, older sync committee (which the finalized-anchored CheckpointProof
  // cannot vouch for).
  if (!(ctx->flags & C4_PROVER_FLAG_CHAIN_STORE)) return C4_SUCCESS;
  uint64_t state_period = block_proof->sync.post_sync_period ? block_proof->sync.post_sync_period : block_proof->sync.oldest_period; // the newest period the client will hold after syncing
  uint64_t block_period = block_proof->sync.block_period ? block_proof->sync.block_period : block_proof->sync.required_period;       // the period of the target block
  if (!state_period) return C4_SUCCESS;                                                                                              // the client does not have a state yet, so he might as well get the head and verify the block.
  if (block_period >= state_period) return C4_SUCCESS;                                                                               // the target block is within the current range of the client

  // Historic-direct path: the actual sub-requests below are billed via their
  // respective helpers; this constant covers the server-side composition work
  // (summary list build, gindex math, proof concatenation, hash_tree_root).
  eth_cu_add(ctx, CU_HISTORIC_DIRECT);

  TRY_ASYNC(c4_beacon_get_block_for_eth(ctx, json_parse("\"latest\""), &block)); // we get the latest because we know for latest we get the a proof for the state. Older sztates are not stored
  TRY_ADD_ASYNC(status, get_historical_summaries(ctx, &block, &history_proof));
  TRY_ADD_ASYNC(status, c4_send_internal_request(ctx, bprintf(&buf2, "period_store/%d/blocks.ssz", block_period), NULL, 0, &blocks)); // get the blockd
  TRY_ASYNC(status);                                                                                                                  // finish requests before continuing

  uint32_t offset_period = (uint32_t) (chain->fork_epochs[C4_FORK_CAPELLA - 1] >> chain->epochs_per_period_bits);
  json_t   data          = json_get(history_proof, "data"); // the main json-object
  uint32_t summary_idx   = block_period - offset_period;    // index starting from the Capella fork, where the first HistoricalSummary was appended.
  uint32_t block_idx     = slot % 8192;                     // idx within the period
  // Sub-gindexes for the two SSZ sub-proofs we construct below. `historical_summaries`
  // remains a classical `List[HistoricalSummary, HISTORICAL_ROOTS_LIMIT]` (EIP-7688 keeps
  // it non-progressive), so both are fork-independent. The combined proof gindex is
  // resolved via `c4_historic_block_gindex` (single source of truth for both prover and
  // verifier) so a manipulated gindex cannot smuggle in a different BeaconState field.
  gindex_t  period_gidx = ssz_gindex(&SUMMARIES, 2, summary_idx, "block_summary_root"); // gindex of the single summary-object we need to proof
  gindex_t  block_gidx  = ssz_gindex(&BLOCKS, 1, block_idx);
  ssz_ob_t  blocks_ob   = {.bytes = blocks, .def = &BLOCKS};
  buffer_t  full_proof  = {0};
  buffer_t  list_data   = {0};
  bytes32_t root        = {0};
  bytes32_t body_root   = {0};
  bytes32_t blocks_root = {0};

  // create summary-list
  json_for_each_value(json_get(data, "historical_summaries"), entry) {
    buffer_append(&list_data, json_get_bytes(entry, "block_summary_root", &buf));
    buffer_append(&list_data, json_get_bytes(entry, "state_summary_root", &buf));
  }

  // create the proofs (two genuinely separate single-leaf proofs)
  ssz_ob_t summaries_ob = {.bytes = list_data.data, .def = &SUMMARIES};
  eth_cu_add(ctx, 2 * CU_SSZ_PROOF);
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
  ssz_hash_tree_root(block.beacon.cl_body, body_root);
  block_proof->historic_proof = full_proof.data;
  // Combined proof gindex resolved through the shared helper. The verifier will
  // compute the exact same value from `chain_id`, `block.slot` (of the target
  // header) and `state_slot` (of the anchoring `signed_header`, which for the
  // direct path is the same `block.slot` we hand in as `proof_header` below).
  block_proof->gindex         = c4_historic_block_gindex(ctx->chain_id, slot, block.slot);
  block_proof->sync_aggregate = block.beacon.sync_aggregate;
  block_proof->proof_header   = bytes(safe_malloc(112), 112);
  block_proof->type           = HISTORIC_PROOF_DIRECT;
  memcpy(block_proof->proof_header.data, block.beacon.cl_header.bytes.data, 112 - 32);
  memcpy(block_proof->proof_header.data + 112 - 32, body_root, 32);

  safe_free(block_idx_proof.data);
  safe_free(period_idx_proof.data);
  safe_free(list_data.data.data);

  return C4_SUCCESS;
}

void ssz_add_header_proof(ssz_builder_t* builder, eth_block_t* block_data, blockroot_proof_t block_proof) {
  ssz_builder_t bp             = ssz_builder_for_def(ssz_get_def(builder->def, "headerProof")->def.container.elements + block_proof.type);
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
      sync_aggregate = block_data->beacon.sync_aggregate;
      break;
  }
  ssz_add_bytes(&bp, "sync_committee_bits", ssz_get(&sync_aggregate, "syncCommitteeBits").bytes);
  ssz_add_bytes(&bp, "sync_committee_signature", ssz_get(&sync_aggregate, "syncCommitteeSignature").bytes);

  ssz_add_builders(builder, "headerProof", bp);
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
  ssz_ob_t result    = {0};
  char     path[200] = {0};
  sbprintf(path, "eth/v1/beacon/light_client/bootstrap/0x%x", bytes(header_root, 32));
  TRY_ASYNC(c4_send_beacon_ssz(ctx, path, NULL, NULL, DEFAULT_TTL, &result));

  fork_id_t fork = c4_eth_get_fork_for_lcu(ctx->chain_id, result.bytes);
  if (fork == 0) THROW_ERROR("Invalid bootstrap data: cannot determine fork!");
  // Single source of truth for fork -> bootstrap container mapping.
  result.def = eth_get_light_client_bootstrap(fork);
  if (!result.def) THROW_ERROR("Invalid bootstrap data: unsupported fork!");
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
static ssz_ob_t build_checkpoint_proof_ob(ssz_ob_t bootstrap) {
  ssz_ob_t      header           = ssz_get(&bootstrap, "header");
  ssz_ob_t      beacon           = ssz_get(&header, "beacon");
  ssz_ob_t      current_sync     = ssz_get(&bootstrap, "currentSyncCommittee");
  ssz_ob_t      aggregate_pubkey = ssz_get(&current_sync, "aggregatePubkey");
  ssz_ob_t      branch           = ssz_get(&bootstrap, "currentSyncCommitteeBranch");
  ssz_builder_t bp               = ssz_builder_for_def(eth_ssz_verification_type(ETH_SSZ_VERIFY_CHECKPOINT_PROOF));
  ssz_add_bytes(&bp, "header", beacon.bytes);
  ssz_add_bytes(&bp, "aggregate_pubkey", aggregate_pubkey.bytes);
  ssz_add_bytes(&bp, "proof", branch.bytes);
  return ssz_builder_to_bytes(&bp);
}

// Fetch the current finalized BeaconBlock, then a LightClientBootstrap for its
// block_root, and build the slim CheckpointProof from it. On success the caller
// owns `out->bytes.data`. Used by both the ZK and LC sync-data paths so they
// share the same async sequencing and avoid duplicated fin-block roundtrips.
static c4_status_t fetch_finalized_checkpoint_proof(prover_ctx_t* ctx, ssz_ob_t* out) {
  eth_block_t fin       = {0};
  ssz_ob_t    bootstrap = {0};
  TRY_ASYNC(c4_beacon_get_block_for_eth(ctx, json_parse("\"finalized\""), &fin));
  if (fin.proof_type != C4_BLOCK_PROOF_TYPE_BEACON)
    THROW_ERROR("finalized checkpoint requires beacon proof data");
  TRY_ASYNC(fetch_bootstrap_by_root(ctx, fin.beacon.data_block_root, &bootstrap));
  *out = build_checkpoint_proof_ob(bootstrap);
  return C4_SUCCESS;
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
    // Union tag for `C4_ETH_SYNCDATA_UPDATE_UNION`:
    //   0 = DenepLightClientUpdate, 1 = ElectraLightClientUpdate (also used for Fulu),
    //   2 = GloasLightClientUpdate.
    uint8_t union_tag = 0;
    if (fork >= C4_FORK_GLOAS)
      union_tag = 2;
    else if (fork >= C4_FORK_ELECTRA)
      union_tag = 1;
    prefixed.data[0] = union_tag;
    ssz_add_dynamic_list_bytes(updates, count, prefixed);
    safe_free(prefixed.data);
  }

  return C4_SUCCESS;
}

c4_status_t c4_get_syncdata_proof(prover_ctx_t* ctx, syncdata_state_t* sync_data, ssz_builder_t* builder) {
  // nothing to be done - no data to be added.
  if (ctx->flags & C4_PROVER_FLAG_HYBRID) return C4_SUCCESS; // no need to handle this for hybrid mode.
  if ((ctx->flags & C4_PROVER_FLAG_INCLUDE_SYNC) == 0 && !(ctx->flags & C4_PROVER_FLAG_ZK_PROOF)) return C4_SUCCESS;
  if (ctx->flags & C4_PROVER_FLAG_ZK_PROOF && (ctx->flags & C4_PROVER_FLAG_CHAIN_STORE) == 0) return C4_SUCCESS;
  if ((ctx->flags & C4_PROVER_FLAG_ZK_PROOF) && ((sync_data->newest_period == 0 && sync_data->checkpoint_period == 0) ||
                                                 (sync_data->newest_period && sync_data->newest_period < sync_data->required_period))) {
    // we need a zk_proof (if available) for the required period. Always `ZKSyncDataV6`
    // (union index 3, 356-byte SP1 v6 proof). The builder layout AND the union selector
    // are derived from this def, so it must match the read def used in
    // `c4_fetch_zk_proof_data`.
    builder->def             = eth_ssz_verification_type(c4_zk_syncdata_type());
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
    //
    // The CheckpointProof anchor can only be processed by clients from version 1.1.28
    // onwards, so only request/embed it when the consumer is new enough.
    bool     need_checkpoint_proof = ctx->witness_key.len == 0 && ctx->version >= c4_version_number(1, 1, 28);
    ssz_ob_t checkpoint_ob         = {0};
    if (need_checkpoint_proof)
      TRY_ASYNC(fetch_finalized_checkpoint_proof(ctx, &checkpoint_ob));

    TRY_ASYNC_CATCH(c4_fetch_zk_proof_data(ctx, &zk_proof, sync_data->required_period), safe_free(checkpoint_ob.bytes.data));

    ssz_add_bytes(builder, "vk_hash", ssz_get(&zk_proof.sync_proof, "vk_hash").bytes);
    ssz_add_bytes(builder, "proof", ssz_get(&zk_proof.sync_proof, "proof").bytes);
    ssz_add_ob(builder, "header", ssz_get(&zk_proof.sync_proof, "header"));
    ssz_add_ob(builder, "pubkeys", ssz_get(&zk_proof.sync_proof, "pubkeys"));

    if (need_checkpoint_proof) {
      ssz_add_ob(builder, "checkpoint", checkpoint_ob);
      safe_free(checkpoint_ob.bytes.data);
    }
    else
      ssz_add_ob(builder, "checkpoint", ssz_get(&zk_proof.sync_proof, "checkpoint"));

    ssz_add_bytes(builder, "signatures", zk_proof.signatures);
    safe_free(zk_proof.signatures.data);
    return C4_SUCCESS;
  }
  if (sync_data->checkpoint_period == 0 && sync_data->required_period <= sync_data->newest_period) return C4_SUCCESS;

  builder->def            = eth_ssz_verification_type(ETH_SSZ_VERIFY_LC_SYNCDATA);
  ssz_ob_t      bootstrap = {.def = &ssz_none};
  ssz_ob_t      cp_ob     = {0}; // owns checkpoint_proof bytes (free at end)
  ssz_builder_t updates   = ssz_builder_for_def(ssz_get_def(builder->def, "update"));

  if (sync_data->checkpoint_period)
    // Client supplied a checkpoint -- this is the bootstrap-init use case; the full
    // LightClientBootstrap binds the verifier to that trusted checkpoint directly.
    TRY_ASYNC(fetch_bootstrap_data(ctx, sync_data, &bootstrap));
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
  if (sync_data->required_period > sync_data->newest_period)
    TRY_ASYNC_CATCH(fetch_updates_data(ctx, sync_data, &updates), safe_free(cp_ob.bytes.data));

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
  sync_data->block_period      = sync_data->required_period;
  sync_data->post_sync_period  = sync_data->oldest_period;

  switch (sync_data->status) {
    case C4_STATE_SYNC_EMPTY:
      if (ctx->flags & C4_PROVER_FLAG_ZK_PROOF && ctx->flags & C4_PROVER_FLAG_CHAIN_STORE) {
        // Anchor the sync committee to the current *finalized* period. The double-trust
        // CheckpointProof is always built from the `finalized` bootstrap
        // (`fetch_finalized_checkpoint_proof`), so the ZK proof's committee MUST be for the
        // finalized period as well -- otherwise the verifier's checkpoint cross-check
        // (hash(zk_pubkeys) through currentSyncCommitteeBranch == header.stateRoot) fails
        // whenever the requested block sits in an older period than the finalized head.
        // The block itself is then verified against a recent state via historical_summaries
        // (HISTORIC_PROOF_DIRECT, see `check_historic_proof_direct`), not by pulling its own
        // (older) sync committee.
        eth_block_t fin = {0};
        TRY_ASYNC(c4_beacon_get_block_for_eth(ctx, json_parse("\"finalized\""), &fin));
        uint64_t fin_period = (uint64_t) (fin.slot >> (chain->slots_per_epoch_bits + chain->epochs_per_period_bits));
        if (fin_period > sync_data->required_period) sync_data->required_period = fin_period;

        // we only fetch them so we safe time in case we download the proof files later.
        c4_status_t status          = c4_fetch_zk_proof_data(ctx, &zk_proof, sync_data->required_period);
        sync_data->post_sync_period = sync_data->required_period;
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
      sync_data->post_sync_period = sync_data->newest_period;
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
      sync_data->post_sync_period  = sync_data->checkpoint_period;

      break;
    }
    case C4_STATE_SYNC_BLOCKHASH_HEADER:
    case C4_STATE_SYNC_EXECUTION_PAYLOAD:
      // OP-Stack-specific chain states must not be sent to the ETH prover.
      THROW_ERROR("unexpected OP-Stack chain state for ETH prover");
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
      eth_block_t fin = {0};
      TRY_ASYNC(c4_beacon_get_block_for_eth(ctx, json_parse("\"finalized\""), &fin));
      uint32_t current_period = (uint32_t) (fin.slot >> (chain->slots_per_epoch_bits + chain->epochs_per_period_bits));
      if ((uint64_t) current_period > sync_data->required_period) {
        sync_data->required_period  = (uint64_t) current_period;
        sync_data->post_sync_period = sync_data->required_period;
      }
    }
  }

  // if there is a gap, fetch the light client updates
  if ((ctx->flags & C4_PROVER_FLAG_ZK_PROOF) && (ctx->flags & C4_PROVER_FLAG_CHAIN_STORE) && sync_data->newest_period < sync_data->required_period)
    return c4_fetch_zk_proof_data(ctx, &zk_proof, sync_data->required_period);
  else if ((ctx->flags & C4_PROVER_FLAG_INCLUDE_SYNC) && sync_data->newest_period < sync_data->required_period)
    return fetch_updates_data(ctx, sync_data, NULL);
  return C4_SUCCESS;
}

c4_status_t c4_check_blockroot_proof(prover_ctx_t* ctx, blockroot_proof_t* block_proof, eth_block_t* src_block) {
  if (ctx->flags & C4_PROVER_FLAG_HYBRID) return C4_SUCCESS;
  // hybrid / cache / sequencer blocks have no CL header to prove.
  if (src_block->proof_type != C4_BLOCK_PROOF_TYPE_BEACON) return C4_SUCCESS;
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
