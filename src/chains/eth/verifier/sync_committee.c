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

#define NEXT_SYNC_COMMITTEE_GINDEX 55

static bool update_light_client_update(verify_ctx_t* ctx, ssz_ob_t* update, bytes32_t trusted_blockhash) {
  bytes32_t sync_root      = {0};
  bytes32_t merkle_root    = {0};
  bytes32_t blockhash      = {0};
  ssz_ob_t  attested       = ssz_get(update, "attestedHeader");
  ssz_ob_t  header         = ssz_get(&attested, "beacon");
  ssz_ob_t  sync_aggregate = ssz_get(update, "syncAggregate");
  ssz_ob_t  signature      = ssz_get(&sync_aggregate, "syncCommitteeSignature");
  ssz_ob_t  sync_bits      = ssz_get(&sync_aggregate, "syncCommitteeBits");
  ssz_ob_t  merkle_proof   = ssz_get(update, "nextSyncCommitteeBranch");
  ssz_ob_t  sync_committee = ssz_get(update, "nextSyncCommittee");
  ssz_ob_t  state_root     = ssz_get(&header, "stateRoot");
  uint64_t  slot           = ssz_get_uint64(&header, "slot");
  if (ssz_is_error(header) || ssz_is_error(state_root) || ssz_is_error(signature) || ssz_is_error(sync_bits) || ssz_is_error(merkle_proof) || ssz_is_error(sync_committee))
    RETURN_VERIFY_ERROR(ctx, "invalid light client update!");

  // calculate the blockhash
  ssz_hash_tree_root(header, blockhash);

  // verify the signature of the old sync committee for the next sync committee
  if (trusted_blockhash) {
    if (!bytes_all_zero(bytes(trusted_blockhash, 32)) && memcmp(trusted_blockhash, blockhash, 32)) RETURN_VERIFY_ERROR(ctx, "invalid blockhash!");
  }
  else {
    if (c4_verify_blockroot_signature(ctx, &header, &sync_bits, &signature, slot) != C4_SUCCESS) return false;
  }

  // create merkle root from proof
  ssz_hash_tree_root(sync_committee, sync_root);
  ssz_verify_single_merkle_proof(merkle_proof.bytes, sync_root, NEXT_SYNC_COMMITTEE_GINDEX, merkle_root);

  // verify the merkle root
  if (memcmp(merkle_root, state_root.bytes.data, 32)) RETURN_VERIFY_ERROR(ctx, "invalid merkle root in light client update!");

  return c4_set_sync_period(slot, blockhash, ssz_get(&sync_committee, "pubkeys").bytes, ctx->chain_id);
}

INTERNAL bool c4_update_from_sync_data(verify_ctx_t* ctx) {
  if (ssz_is_error(ctx->sync_data)) RETURN_VERIFY_ERROR(ctx, "invalid sync_data!");
  if (ctx->sync_data.def->type == SSZ_TYPE_NONE) return true;

  if (ctx->sync_data.def == eth_ssz_verification_type(ETH_SSZ_VERIFY_LIGHT_CLIENT_UPDATE_LIST)) {
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

INTERNAL bool c4_handle_client_updates(verify_ctx_t* ctx, bytes_t client_updates, bytes32_t trusted_blockhash) {

  uint64_t length  = 0;
  bool     success = true;
  if (client_updates.len && client_updates.data[0] == '{') {
    json_t json = json_parse((char*) client_updates.data);
    json_t msg  = json_get(json, "message");
    if (msg.start) {
      ctx->state.error = bprintf(NULL, "Invalid client updates: %j", msg);
      return false;
    };
  }
  for (uint32_t pos = 0; pos + 12 < client_updates.len; pos += length + 8) {
    length = uint64_from_le(client_updates.data + pos);
    if (pos + 8 + length > client_updates.len) {
      success = false;
      break;
    }

    ssz_ob_t client_update_ob = {
        .bytes = bytes(client_updates.data + pos + 8 + 4, length - 4),
        .def   = eth_ssz_verification_type(ETH_SSZ_VERIFY_LIGHT_CLIENT_UPDATE)};
    if (!update_light_client_update(ctx, &client_update_ob, trusted_blockhash)) {
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
