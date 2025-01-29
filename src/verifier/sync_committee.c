#include "sync_committee.h"
#include "../util/json.h"
#include "../util/plugin.h"
#include "../util/ssz.h"
#include "default_synccommittee.h"
#include "types_beacon.h"
#include "types_verify.h"
#include "verify.h"
#include <string.h>

#define STATES                     "states"
#define NEXT_SYNC_COMMITTEE_GINDEX 55

// the sync state of the sync committee. This is used to store the verfied validators as state within the verifier.
const ssz_def_t SYNC_STATE[] = {
    SSZ_VECTOR("validators", ssz_bls_pubky, 512), // the list of the validators
    SSZ_UINT32("period")};                        // the period of the sync committee

const ssz_def_t SYNC_STATE_CONTAINER = SSZ_CONTAINER("SyncState", SYNC_STATE);

const c4_sync_state_t c4_get_validators(uint32_t period) {

  storage_plugin_t storage_conf  = {0};
  ssz_ob_t         sync_state_ob = ssz_ob(SYNC_STATE_CONTAINER, bytes((uint8_t*) default_synccommittee, default_synccommittee_len));
  uint32_t         last_period   = ssz_get_uint32(&sync_state_ob, "period");

  c4_get_storage_config(&storage_conf);
  if (!storage_conf.get || period == last_period)
    return (c4_sync_state_t) {
        .current_period = period,
        .needs_cleanup  = false,
        .last_period    = last_period,
        .validators     = last_period != period ? NULL_BYTES : ssz_get(&sync_state_ob, "validators").bytes};

  char name[100];
  sprintf(name, "sync_%d", period);
  buffer_t tmp  = {0};
  tmp.allocated = 512 * 48;
  if (storage_conf.get(name, &tmp) && tmp.data.data != NULL) return (c4_sync_state_t) {
      .current_period = period,
      .needs_cleanup  = true,
      .last_period    = period,
      .validators     = tmp.data};

  // find the latest
  tmp.allocated = 4 * storage_conf.max_sync_states;
  if (storage_conf.get(STATES, &tmp) && tmp.data.data) {
    for (uint32_t i = 0; i < tmp.data.len; i += 4) {
      uint32_t state = *(uint32_t*) (tmp.data.data + i);
      if (state < period && state > last_period)
        last_period = state;
    }
    buffer_free(&tmp);
  }

  return (c4_sync_state_t) {
      .current_period = period,
      .last_period    = last_period,
      .needs_cleanup  = false,
      .validators     = NULL_BYTES};
}

static bool store_sync(verify_ctx_t* ctx, bytes_t pubkeys, uint32_t period) {
  char             name[100];
  storage_plugin_t storage_conf = {0};
  c4_get_storage_config(&storage_conf);
  if (!storage_conf.set) RETURN_VERIFY_ERROR(ctx, "no storage plugin set!");

  // cleanup
  buffer_t tmp = {0};
  storage_conf.get(STATES, &tmp);
  if (tmp.data.len % 4 == 0) {
    size_t pos = tmp.data.len;
    if (tmp.data.len < storage_conf.max_sync_states * 4)
      buffer_append(&tmp, bytes(NULL, 4));
    else {
      uint32_t oldest = 0;
      for (uint32_t i = 0; i < tmp.data.len; i += 4) {
        uint32_t state = *((uint32_t*) (tmp.data.data + i));
        if (!oldest || state < oldest) {
          oldest = state;
          pos    = i;
        }
      }
      sprintf(name, "sync_%d", oldest);
      storage_conf.del(name);
    }
    *(uint32_t*) (tmp.data.data + pos) = period;
  }

  sprintf(name, "sync_%d", period);
  storage_conf.set(name, pubkeys);
  storage_conf.set(STATES, tmp.data);
  buffer_free(&tmp);

  return true;
}

static bool update_light_client_update(verify_ctx_t* ctx, ssz_ob_t* update) {
  bytes32_t sync_root      = {0};
  bytes32_t merkle_root    = {0};
  ssz_ob_t  attested       = ssz_get(update, "attestedHeader");
  ssz_ob_t  header         = ssz_get(&attested, "beacon");
  ssz_ob_t  sync_aggregate = ssz_get(update, "syncAggregate");
  ssz_ob_t  signature      = ssz_get(&sync_aggregate, "syncCommitteeSignature");
  ssz_ob_t  sync_bits      = ssz_get(&sync_aggregate, "syncCommitteeBits");
  ssz_ob_t  merkle_proof   = ssz_get(update, "nextSyncCommitteeBranch");
  ssz_ob_t  sync_committee = ssz_get(update, "nextSyncCommittee");
  ssz_ob_t  state_root     = ssz_get(&header, "stateRoot");
  uint64_t  slot           = ssz_get_uint64(update, "signatureSlot");
  if (ssz_is_error(header) || ssz_is_error(state_root) || ssz_is_error(signature) || ssz_is_error(sync_bits) || ssz_is_error(merkle_proof) || ssz_is_error(sync_committee))
    RETURN_VERIFY_ERROR(ctx, "invalid light client update!");

  // verify the signature of the old sync committee for the next sync committee
  if (!c4_verify_blockroot_signature(ctx, &header, &sync_bits, &signature, slot)) RETURN_VERIFY_ERROR(ctx, "invalid signature in light client update!");

  // create merkle root from proof
  ssz_hash_tree_root(sync_committee, sync_root);
  ssz_verify_merkle_proof(merkle_proof.bytes, sync_root, NEXT_SYNC_COMMITTEE_GINDEX, merkle_root);

  // verify the merkle root
  if (memcmp(merkle_root, state_root.bytes.data, 32)) RETURN_VERIFY_ERROR(ctx, "invalid merkle root in light client update!");

  return store_sync(ctx, ssz_get(&sync_committee, "pubkeys").bytes, (uint32_t) (ssz_get_uint64(&header, "slot") >> 13) + 1);
}

bool c4_update_from_sync_data(verify_ctx_t* ctx) {
  if (ssz_is_error(ctx->sync_data)) RETURN_VERIFY_ERROR(ctx, "invalid sync_data!");
  if (ctx->sync_data.def->type == SSZ_TYPE_NONE) return true;

  // check the sync_data type
  if (ctx->sync_data.def->type == SSZ_TYPE_LIST) {
    for (int i = 0; i < ssz_len(ctx->sync_data); i++) {
      ssz_ob_t update = ssz_at(ctx->sync_data, i);
      if (ssz_is_error(update)) RETURN_VERIFY_ERROR(ctx, "invalid sync_data!");
      if (ssz_is_type(&update, LIGHT_CLIENT_UPDATE)) {
        if (!update_light_client_update(ctx, &update)) return false;
      }
      else
        RETURN_VERIFY_ERROR(ctx, "unknown sync_data type!");
    }
  }
  return true;
}

bool c4_handle_client_updates(bytes_t client_updates) {

  buffer_t updates = {0};
  if (client_updates.len && client_updates.data[0] == '{') {
    json_t json = json_parse((char*) client_updates.data);
    json_t msg  = json_get(json, "message");
    if (msg.start) return false;
  }

  uint32_t pos = 0;
  while (pos < client_updates.len) {
    updates.data.len = 0;
    uint64_t length  = uint64_from_le(client_updates.data + pos);
    buffer_grow(&updates, length + 100);
    buffer_append(&updates, bytes(NULL, 15)); // 3 offsets + 3 union bytes
    uint64_to_le(updates.data.data, 12);      // offset for data
    uint64_to_le(updates.data.data + 4, 13);  // offset for proof
    uint64_to_le(updates.data.data + 8, 14);  // offset for sync
    updates.data.data[14] = 1;                // union type for lightclient updates

    ssz_builder_t builder = {0};
    builder.def           = (ssz_def_t*) (C4_REQUEST_SYNCDATA_UNION + 1); // union type for lightclient updates
    ssz_add_dynamic_list_bytes(&builder, 1, bytes(client_updates.data + pos + 8 + 4, length - 4));
    bytes_t list_data = ssz_builder_to_bytes(&builder).bytes;
    buffer_append(&updates, list_data);
    free(list_data.data);

    verify_ctx_t sync_ctx = {0};
    c4_verify_from_bytes(&sync_ctx, updates.data);
    if (sync_ctx.error) return false;

    pos += length + 8;
  }

  buffer_free(&updates);

  // each entry:
  //  - 8 bytes (uint64) length
  //- 4 bytes forDigest
  //- LightClientUpdate

  // wrap into request
  return true;
}
