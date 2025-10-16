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

#include "sync_committee.h"
#include "beacon_types.h"
#include "eth_verify.h"
#include "json.h"
#include "plugin.h"
#include "ssz.h"
#include "version.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define DENEP_NEXT_SYNC_COMMITTEE_GINDEX   55
#define ELECTRA_NEXT_SYNC_COMMITTEE_GINDEX 87
#define DENEP_FINALIZED_ROOT_GINDEX        105
#define ELECTRA_FINALIZED_ROOT_GINDEX      169

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

static bool update_light_client_update(verify_ctx_t* ctx, ssz_ob_t* update, bytes32_t trusted_checkpoint) {

  bytes32_t sync_root             = {0};
  bytes32_t merkle_root           = {0};
  bytes32_t attested_blockhash    = {0};
  bytes32_t finalized_blockhash   = {0};
  bytes32_t finalized_header_root = {0};
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
  if (trusted_checkpoint) {
    if (!bytes_all_zero(bytes(trusted_checkpoint, 32)) && memcmp(trusted_checkpoint, attested_blockhash, 32))
      RETURN_VERIFY_ERROR(ctx, "invalid blockhash!");
  }
  else {
    if (c4_verify_blockroot_signature(ctx, &attested_header, &sync_bits, &signature, attested_slot) != C4_SUCCESS)
      return false;
  }

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
  uint32_t            period = (finalized_slot >> (spec->slots_per_epoch_bits + spec->epochs_per_period_bits)) + 1;
  ssz_hash_tree_root(finalized_header, finalized_blockhash);
  return c4_set_sync_period(period, finalized_slot, finalized_blockhash, sync_committee, ctx->chain_id);
}

INTERNAL bool c4_update_from_sync_data(verify_ctx_t* ctx) {
  if (ssz_is_error(ctx->sync_data)) RETURN_VERIFY_ERROR(ctx, "invalid sync_data!");
  if (ctx->sync_data.def->type == SSZ_TYPE_NONE) return true;

  if (ctx->sync_data.def == eth_get_light_client_update_list(C4_FORK_DENEB) ||
      ctx->sync_data.def == eth_get_light_client_update_list(C4_FORK_ELECTRA)) {
    uint32_t updates_len = ssz_len(ctx->sync_data);
    for (uint32_t i = 0; i < updates_len; i++) {
      ssz_ob_t update = ssz_at(ctx->sync_data, i);
      if (ssz_is_error(update)) RETURN_VERIFY_ERROR(ctx, "invalid sync_data!");
      if (!update_light_client_update(ctx, &update, NULL)) return false;
    }
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

INTERNAL bool c4_handle_client_updates(verify_ctx_t* ctx, bytes_t client_updates, bytes32_t trusted_checkpoint) {

  uint64_t length  = 0;
  bool     success = true;
  // just to make sure the result is not a json with an error message
  if (client_updates.len && client_updates.data[0] == '{') {
    json_t json = json_parse((char*) client_updates.data);
    json_t msg  = json_get(json, "message");
    if (msg.start) {
      ctx->state.error = bprintf(NULL, "Invalid client updates: %j", msg);
      return false;
    };
  }

  // detect lighthouse
  bool lighthouse = client_updates.len > 12 && !bytes_all_zero(bytes_slice(client_updates, 4, 4)) && uint32_from_le(client_updates.data) < 1000;
  int  idx        = 0;

  //  bytes_write(client_updates, fopen("client_updates.ssz", "wb"), true);

  for (uint32_t pos = 0; pos + 12 < client_updates.len; pos += length + 8, idx++) {
    uint32_t data_offset        = pos + 8 + 4;
    uint32_t data_length_offset = 4;
    if (lighthouse) {
      pos = uint32_from_le(client_updates.data + (idx * 4));
      if (pos + 12 > client_updates.len) {
        success = false;
        c4_state_add_error(&ctx->state, "invalid offset in lighthouse client update!");
        break;
      }
      data_offset = pos + 16 + 4;
      //      data_length_offset = 0;
    }
    length = uint64_from_le(client_updates.data + pos);

    if (pos + 8 + length > client_updates.len && length > 12) {
      success = false;
      break;
    }
    bytes_t          client_update_bytes = bytes(client_updates.data + data_offset, length - data_length_offset);
    fork_id_t        fork                = c4_eth_get_fork_for_lcu(ctx->chain_id, client_update_bytes);
    const ssz_def_t* client_update_list  = eth_get_light_client_update_list(fork);
    if (!client_update_list) {
      success = false;
      break;
    }
    //    bytes_write(client_update_bytes, fopen("client_updates_bytes.ssz", "wb"), true);

    ssz_ob_t client_update_ob = {
        .bytes = client_update_bytes,
        .def   = client_update_list->def.vector.type};

    // Validate SSZ structure (checks offsets and ensures all properties exist)
    if (!ssz_is_valid(client_update_ob, true, &ctx->state)) {
      success = false;
      c4_state_add_error(&ctx->state, "Invalid SSZ structure in light client update");
      break;
    }

    if (!update_light_client_update(ctx, &client_update_ob, trusted_checkpoint)) {
      success = false;
      break;
    }
  }

  // each entry:
  //  - 8 bytes (uint64) length
  //- 4 bytes forDigest
  //- LightClientUpdate

  // wrap into request
  return success;
}
