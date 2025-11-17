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

#include "sync_committee.h" // Includes c4_process_update_fn typedef
#include "beacon_types.h"
#include "eth_verify.h"
#include "json.h"
#include "plugin.h"
#include "ssz.h"
#include "version.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define DENEP_CURRENT_SYNC_COMMITTEE_GINDEX   54
#define ELECTRA_CURRENT_SYNC_COMMITTEE_GINDEX 86
#define DENEP_NEXT_SYNC_COMMITTEE_GINDEX      55
#define ELECTRA_NEXT_SYNC_COMMITTEE_GINDEX    87
#define DENEP_FINALIZED_ROOT_GINDEX           105
#define ELECTRA_FINALIZED_ROOT_GINDEX         169

INTERNAL uint64_t c4_current_sync_committee_gindex(chain_id_t chain_id, uint64_t slot) {
  const chain_spec_t* spec = c4_eth_get_chain_spec(chain_id);
  fork_id_t           fork = c4_chain_fork_id(chain_id, epoch_for_slot(slot, spec));
  return fork == C4_FORK_DENEB ? DENEP_CURRENT_SYNC_COMMITTEE_GINDEX : ELECTRA_CURRENT_SYNC_COMMITTEE_GINDEX;
}

static uint64_t next_sync_committee_gindex(chain_id_t chain_id, uint64_t slot) {
  const chain_spec_t* spec = c4_eth_get_chain_spec(chain_id);
  fork_id_t           fork = c4_chain_fork_id(chain_id, epoch_for_slot(slot, spec));
  return fork == C4_FORK_DENEB ? DENEP_NEXT_SYNC_COMMITTEE_GINDEX : ELECTRA_NEXT_SYNC_COMMITTEE_GINDEX;
}

static uint64_t finalized_root_gindex(chain_id_t chain_id, uint64_t slot) {
  const chain_spec_t* spec = c4_eth_get_chain_spec(chain_id);
  fork_id_t           fork = c4_chain_fork_id(chain_id, epoch_for_slot(slot, spec));
  return fork == C4_FORK_DENEB ? DENEP_FINALIZED_ROOT_GINDEX : ELECTRA_FINALIZED_ROOT_GINDEX;
}

static bool update_light_client_update(verify_ctx_t* ctx, ssz_ob_t* update) {

  bytes32_t sync_root             = {0};
  bytes32_t merkle_root           = {0};
  bytes32_t attested_blockhash    = {0};
  bytes32_t finalized_blockhash   = {0};
  bytes32_t finalized_header_root = {0};
  bytes32_t previous_pubkeys_hash = {0};
  // Extract components (no need for ssz_is_error checks after validation in c4_handle_client_updates)
  ssz_ob_t attested            = ssz_get(update, "attestedHeader");
  ssz_ob_t attested_header     = ssz_get(&attested, "beacon");
  ssz_ob_t finalized           = ssz_get(update, "finalizedHeader");
  ssz_ob_t finalized_header    = ssz_get(&finalized, "beacon");
  ssz_ob_t finality_branch     = ssz_get(update, "finalityBranch");
  ssz_ob_t sync_aggregate      = ssz_get(update, "syncAggregate");
  ssz_ob_t signature           = ssz_get(&sync_aggregate, "syncCommitteeSignature");
  ssz_ob_t sync_bits           = ssz_get(&sync_aggregate, "syncCommitteeBits");
  ssz_ob_t next_sync_branch    = ssz_get(update, "nextSyncCommitteeBranch");
  ssz_ob_t sync_committee      = ssz_get(update, "nextSyncCommittee");
  ssz_ob_t attested_state_root = ssz_get(&attested_header, "stateRoot");
  uint64_t attested_slot       = ssz_get_uint64(&attested_header, "slot");
  uint64_t finalized_slot      = ssz_get_uint64(&finalized_header, "slot");

  // calculate the attested blockhash
  ssz_hash_tree_root(attested_header, attested_blockhash);

  // verify the signature of the old sync committee against the attested header
  if (c4_verify_blockroot_signature(ctx, &attested_header, &sync_bits, &signature, attested_slot, previous_pubkeys_hash) != C4_SUCCESS)
    return false;

  // verify nextSyncCommittee merkle proof against attested state root
  ssz_hash_tree_root(sync_committee, sync_root);
  ssz_verify_single_merkle_proof(next_sync_branch.bytes, sync_root, next_sync_committee_gindex(ctx->chain_id, attested_slot), merkle_root);
  if (memcmp(merkle_root, attested_state_root.bytes.data, 32))
    RETURN_VERIFY_ERROR(ctx, "invalid merkle root for next sync committee!");

  // verify finalizedHeader merkle proof against attested state root
  ssz_hash_tree_root(finalized_header, finalized_header_root);
  ssz_verify_single_merkle_proof(finality_branch.bytes, finalized_header_root, finalized_root_gindex(ctx->chain_id, attested_slot), merkle_root);
  if (memcmp(merkle_root, attested_state_root.bytes.data, 32))
    RETURN_VERIFY_ERROR(ctx, "invalid merkle root for finalized header!");

  // calculate finalized blockhash and store it with the next sync committee
  // +1 because nextSyncCommittee is for the next period
  const chain_spec_t* spec   = c4_eth_get_chain_spec(ctx->chain_id);
  uint32_t            period = (attested_slot >> (spec->slots_per_epoch_bits + spec->epochs_per_period_bits)) + 1;
  return c4_set_sync_period(period, sync_committee, ctx->chain_id, previous_pubkeys_hash);
}

INTERNAL bool c4_update_from_sync_data(verify_ctx_t* ctx) {
  if (ssz_is_error(ctx->sync_data)) RETURN_VERIFY_ERROR(ctx, "invalid sync_data!");
  if (ctx->sync_data.def->type == SSZ_TYPE_NONE) return true;

  if (strcmp(ctx->sync_data.def->name, "LCSyncData") == 0) {
    ssz_ob_t bootstrap = ssz_get(&ctx->sync_data, "bootstrap");
    ssz_ob_t updates   = ssz_get(&ctx->sync_data, "update");

    // do we have bootstrap data?
    if (bootstrap.def->type == SSZ_TYPE_CONTAINER) {
      c4_chain_state_t chain_state = c4_get_chain_state(ctx->chain_id);
      if (chain_state.status == C4_STATE_SYNC_EMPTY) RETURN_VERIFY_ERROR(ctx, "bootstrap data found, but no checkpoint set!");
      if (chain_state.status == C4_STATE_SYNC_CHECKPOINT && c4_handle_bootstrap(ctx, bootstrap.bytes, chain_state.data.checkpoint) != C4_SUCCESS) return false;
    }

    // run all light client updates
    uint32_t updates_len = ssz_len(updates);
    for (uint32_t i = 0; i < updates_len; i++) {
      ssz_ob_t update = ssz_union(ssz_at(updates, i));
      if (!update_light_client_update(ctx, &update)) return false;
    }

    // we may want to clean up the sync data, so we don't sync again.
    ctx->sync_data.def = &ssz_none;
    return true;
  }
  else
    RETURN_VERIFY_ERROR(ctx, "unknown sync_data type!");
}

fork_id_t c4_eth_get_fork_for_lcu(chain_id_t chain_id, bytes_t data) {
  if (data.len < 4) return 0;
  uint32_t offset = uint32_from_le(data.data);
  if (offset + 8 > data.len) return 0;
  uint64_t            slot = uint64_from_le(data.data + offset);
  const chain_spec_t* spec = c4_eth_get_chain_spec(chain_id);
  return c4_chain_fork_id(chain_id, epoch_for_slot(slot, spec));
}

/**
 * Detects the format of light client updates (Standard SSZ or Lighthouse variant).
 * Lighthouse format uses a different offset structure.
 *
 * @param data The raw bytes of light client updates
 * @return true if Lighthouse format, false for standard format
 */
static bool detect_update_format(bytes_t data) {
  // Lighthouse detection: check if length is sufficient, second offset is non-zero, and first value is reasonable
  return data.len > UPDATE_PREFIX_SIZE &&
         !bytes_all_zero(bytes_slice(data, SSZ_OFFSET_SIZE, SSZ_OFFSET_SIZE)) &&
         uint32_from_le(data.data) < 1000;
}

/**
 * Process light client updates with a callback function for each update.
 * This handles both standard SSZ and Lighthouse formats.
 *
 * @param ctx Verification context
 * @param light_client_updates Raw bytes containing one or more light client updates
 * @param process_update Callback function to process each individual update
 * @return true if all updates were processed successfully, false otherwise
 */
INTERNAL bool c4_process_light_client_updates(verify_ctx_t* ctx, bytes_t light_client_updates, bool (*process_update)(verify_ctx_t*, ssz_ob_t*)) {
  uint64_t length     = 0;
  bool     success    = true;
  bool     lighthouse = detect_update_format(light_client_updates);
  int      idx        = 0;

  for (uint32_t pos = 0; pos + UPDATE_PREFIX_SIZE < light_client_updates.len; pos += length + SSZ_LENGTH_SIZE, idx++) {
    uint32_t data_offset        = pos + SSZ_LENGTH_SIZE + SSZ_OFFSET_SIZE;
    uint32_t data_length_offset = SSZ_OFFSET_SIZE;

    if (lighthouse) {
      // Check bounds before reading offset
      if (idx * SSZ_OFFSET_SIZE + SSZ_OFFSET_SIZE > light_client_updates.len) {
        success = false;
        c4_state_add_error(&ctx->state, "invalid lighthouse index exceeds data bounds!");
        break;
      }
      pos = uint32_from_le(light_client_updates.data + (idx * SSZ_OFFSET_SIZE));
      if (pos + UPDATE_PREFIX_SIZE > light_client_updates.len) {
        success = false;
        c4_state_add_error(&ctx->state, "invalid offset in lighthouse client update!");
        break;
      }
      data_offset = pos + LIGHTHOUSE_OFFSET_SIZE + SSZ_OFFSET_SIZE;
    }

    length = uint64_from_le(light_client_updates.data + pos);

    // Check for integer overflow and bounds
    if (length > UPDATE_PREFIX_SIZE && (pos + SSZ_LENGTH_SIZE + length > light_client_updates.len || pos + SSZ_LENGTH_SIZE + length < pos)) {
      success = false;
      c4_state_add_error(&ctx->state, "invalid length causes overflow or exceeds bounds!");
      break;
    }

    bytes_t          light_client_update_bytes = bytes(light_client_updates.data + data_offset, length - data_length_offset);
    fork_id_t        fork                      = c4_eth_get_fork_for_lcu(ctx->chain_id, light_client_update_bytes);
    const ssz_def_t* light_client_update_def   = eth_get_light_client_update(fork);

    if (!light_client_update_def) {
      success = false;
      break;
    }

    ssz_ob_t light_client_update_ob = {.bytes = light_client_update_bytes, .def = light_client_update_def};

    // Validate SSZ structure (checks offsets and ensures all properties exist)
    if (!ssz_is_valid(light_client_update_ob, true, &ctx->state)) {
      success = false;
      c4_state_add_error(&ctx->state, "Invalid SSZ structure in light client update");
      break;
    }

    // Process this update using the callback
    if (!process_update(ctx, &light_client_update_ob)) {
      success = false;
      break;
    }
  }

  return success;
}

INTERNAL bool c4_handle_client_updates(verify_ctx_t* ctx, bytes_t light_client_updates) {
  // Check for JSON error message
  if (light_client_updates.len && light_client_updates.data[0] == '{') {
    json_t json = json_parse((char*) light_client_updates.data);
    json_t msg  = json_get(json, "message");
    if (msg.start) {
      ctx->state.error = bprintf(NULL, "Invalid light client updates: %j", msg);
      return false;
    };
  }

  // Process all light client updates using the general processor
  return c4_process_light_client_updates(ctx, light_client_updates, update_light_client_update);
}
