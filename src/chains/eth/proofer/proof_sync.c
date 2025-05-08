#include "beacon_types.h"
#include "eth_tools.h"
#include "eth_verify.h"
#include "proofer.h"
#include "ssz.h"
#include "sync_committee.h"
#include <stdlib.h>

#define DENEP_NEXT_SYNC_COMMITTEE_GINDEX   55
#define ELECTRA_NEXT_SYNC_COMMITTEE_GINDEX 87

static uint64_t next_sync_committee_gindex(chain_id_t chain_id, uint64_t slot) {
  fork_id_t fork = c4_chain_fork_id(chain_id, epoch_for_slot(slot));
  return fork == C4_FORK_DENEB ? DENEP_NEXT_SYNC_COMMITTEE_GINDEX : ELECTRA_NEXT_SYNC_COMMITTEE_GINDEX;
}

static c4_status_t req_client_update(proofer_ctx_t* ctx, uint32_t period, uint32_t count, chain_id_t chain_id, bytes_t* data) {
  buffer_t tmp = {0};
  bprintf(&tmp, "eth/v1/beacon/light_client/updates?start_period=%d&count=%d", period, count);

  data_request_t* req = c4_state_get_data_request_by_url(&ctx->state, (char*) tmp.data.data);
  if (req) buffer_free(&tmp);
  if (req && req->response.data) {
    *data = req->response;
    return C4_SUCCESS;
  }
  else if (req && req->error) {
    ctx->state.error = strdup(req->error);
    return C4_ERROR;
  }
  data_request_t* new_req = safe_calloc(1, sizeof(data_request_t));
  new_req->chain_id       = chain_id;
  new_req->url            = (char*) tmp.data.data;
  new_req->encoding       = C4_DATA_ENCODING_SSZ;
  new_req->type           = C4_DATA_TYPE_BEACON_API;
  c4_state_add_request(&ctx->state, new_req);
  return C4_PENDING;
}

typedef struct {

  ssz_ob_t new_pubkeys;
  ssz_ob_t old_pubkeys;
  ssz_ob_t signature_bits;
  ssz_ob_t signature;
  gindex_t gidx;
  bytes_t  proof;
  bytes_t  slot;
  bytes_t  proposer_index;
} period_data_t;

static c4_status_t extract_sync_data(proofer_ctx_t* ctx, bytes_t data, period_data_t* period) {
  bytes32_t        domain          = {0};
  bytes32_t        aggregate       = {0};
  fork_id_t        fork            = c4_eth_get_fork_for_lcu(ctx->chain_id, data);
  const ssz_def_t* update_list_def = eth_get_light_client_update_list(fork);
  const ssz_def_t* def             = update_list_def ? update_list_def->def.vector.type : NULL;
  if (data.len < 12 || !def) THROW_ERROR("invalid client_update");
  ssz_ob_t old_update = {.bytes = bytes(data.data + 12, uint64_from_le(data.data) - 4), .def = def};
  if (old_update.bytes.len + 24 > data.len) THROW_ERROR("invalid client_update");
  ssz_ob_t new_update = {.bytes = bytes(data.data + 24 + old_update.bytes.len, uint64_from_le(data.data + 12 + old_update.bytes.len) - 4), .def = def};
  if (new_update.bytes.len + 24 + old_update.bytes.len > data.len) THROW_ERROR("invalid client_update");

  ssz_ob_t old_sync_keys  = ssz_get(&old_update, "nextSyncCommittee");
  ssz_ob_t new_sync_keys  = ssz_get(&new_update, "nextSyncCommittee");
  ssz_ob_t sync_aggregate = ssz_get(&new_update, "syncAggregate");
  ssz_ob_t light_header   = ssz_get(&new_update, "attestedHeader");
  ssz_ob_t header         = ssz_get(&light_header, "beacon");
  period->old_pubkeys     = ssz_get(&old_sync_keys, "pubkeys");
  period->new_pubkeys     = ssz_get(&new_sync_keys, "pubkeys");
  period->signature_bits  = ssz_get(&sync_aggregate, "syncCommitteeBits");
  period->signature       = ssz_get(&sync_aggregate, "syncCommitteeSignature");
  period->slot            = ssz_get(&header, "slot").bytes;
  period->proposer_index  = ssz_get(&header, "proposerIndex").bytes;
  bytes_t  state_proof    = ssz_get(&new_update, "nextSyncCommitteeBranch").bytes;
  ssz_ob_t aggrgated_pub  = ssz_get(&new_sync_keys, "aggregatePubkey");

  if (!eth_calculate_domain(ctx->chain_id, ssz_get_uint64(&header, "slot"), domain)) THROW_ERROR("unsupported chain!");
  //
  memcpy(aggregate, aggrgated_pub.bytes.data + 32, 16);
  sha256_merkle(bytes_slice(aggrgated_pub.bytes, 0, 32), bytes(aggregate, 32), aggregate);

  // define  ssz
  ssz_def_t SIGNING_DATA[] = {
      SSZ_BYTES32("BeaconBlockHeader"),
      SSZ_BYTES32("domain")}; // the domain of the data to sign
  ssz_def_t SIGNING_DATA_CONTAINER   = SSZ_CONTAINER("SigningData", SIGNING_DATA);
  SIGNING_DATA[0]                    = *eth_ssz_type_for_fork(ETH_SSZ_BEACON_BLOCK_HEADER, C4_FORK_DENEB);
  ssz_builder_t signgin_data_builder = ssz_builder_for_def(&SIGNING_DATA_CONTAINER);
  ssz_add_bytes(&signgin_data_builder, "BeaconBlockHeader", header.bytes);
  ssz_add_bytes(&signgin_data_builder, "domain", bytes(domain, 32));
  ssz_ob_t signing_data = ssz_builder_to_bytes(&signgin_data_builder);
  gindex_t state_gidx   = ssz_gindex(signing_data.def, 2, "BeaconBlockHeader", "stateRoot");
  bytes_t  header_proof = ssz_create_proof(signing_data, domain, state_gidx);
  bytes_t  full_proof   = bytes(malloc(header_proof.len + state_proof.len + 32), header_proof.len + state_proof.len + 32);
  memcpy(full_proof.data, aggregate, 32);
  memcpy(full_proof.data + 32, state_proof.data, state_proof.len);
  memcpy(full_proof.data + 32 + state_proof.len, header_proof.data, header_proof.len);
  memcpy(full_proof.data + 32 + state_proof.len + header_proof.len, domain, 32);
  safe_free(header_proof.data);
  safe_free(signing_data.bytes.data);
  period->proof = full_proof;
  period->gidx  = ssz_add_gindex(state_gidx, next_sync_committee_gindex(ctx->chain_id, ssz_get_uint64(&header, "slot"))) * 2; // header -> stateRoot -> .... next_sync ->  pubKeys

  return C4_SUCCESS;
}

static c4_status_t create_proof(proofer_ctx_t* ctx, period_data_t* period) {
  ssz_builder_t proof = ssz_builder_for_type(ETH_SSZ_VERIFY_SYNC_PROOF);
  ssz_add_bytes(&proof, "oldKeys", period->old_pubkeys.bytes);
  ssz_add_bytes(&proof, "newKeys", period->new_pubkeys.bytes);
  ssz_add_bytes(&proof, "syncCommitteeBits", period->signature_bits.bytes);
  ssz_add_bytes(&proof, "syncCommitteeSignature", period->signature.bytes);
  ssz_add_uint64(&proof, period->gidx);
  ssz_add_bytes(&proof, "proof", period->proof);
  ssz_add_bytes(&proof, "slot", period->slot);
  ssz_add_bytes(&proof, "proposerIndex", period->proposer_index);
  safe_free(period->proof.data);

  ctx->proof = eth_create_proof_request(
      ctx->chain_id,
      NULL_SSZ_BUILDER,
      proof,
      NULL_SSZ_BUILDER);
  return C4_SUCCESS;
}

c4_status_t c4_proof_sync(proofer_ctx_t* ctx) {
  bytes_t       data          = NULL_BYTES;
  period_data_t period_values = {0};
  json_t        period_data   = json_at(ctx->params, 0);
  uint32_t      period        = json_as_uint32(period_data);

  if (period == 0) THROW_ERROR_WITH("Invalid period: %j", period_data);
  TRY_ASYNC(req_client_update(ctx, period - 2, 2, ctx->chain_id, &data));
  TRY_ASYNC(extract_sync_data(ctx, data, &period_values));
  return create_proof(ctx, &period_values);
}
