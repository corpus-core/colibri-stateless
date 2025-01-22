
#include "../util/bytes.h"
#include "../util/crypto.h"
#include "../util/ssz.h"
#include "sync_committee.h"
#include "verify.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/** the combined GIndex of the blockhash in the block body path = executionPayload*/
#define BLOCKHASH_BLOCKBODY_GINDEX 812

static const char     GENESIS_VALIDATORS_ROOT[] = "\x4b\x36\x3d\xb9\x4e\x28\x61\x20\xd7\x6e\xb9\x05\x34\x0f\xdd\x4e\x54\xbf\xe9\xf0\x6b\xf3\x3f\xf6\xcf\x5a\xd2\x7f\x51\x1b\xfe\x95";
static const uint64_t FORK_EPOCHS[4]            = {74240, 144896, 194048, 269568};

static const ssz_def_t SIGNING_DATA[] = {
    SSZ_BYTES32("root"),
    SSZ_BYTES32("domain")};

static const ssz_def_t SIGNING_DATA_CONTAINER = SSZ_CONTAINER("SigningData", SIGNING_DATA);

static const ssz_def_t FORK_DATA[] = {
    SSZ_BYTE_VECTOR("version", 4),
    SSZ_BYTES32("state")};

static const ssz_def_t FORK_DATA_CONTAINER = SSZ_CONTAINER("ForkDate", FORK_DATA);

static uint8_t compute_fork_version(uint64_t epoch) {
  const size_t fork_epochs_len = sizeof(FORK_EPOCHS) / sizeof(FORK_EPOCHS[0]);
  for (size_t i = 0; i < fork_epochs_len; i++) {
    if (epoch < FORK_EPOCHS[i]) return i;
  }
  return fork_epochs_len;
}

static bool calculate_signing_message(verify_ctx_t* ctx, uint64_t slot, bytes32_t blockhash, bytes32_t signing_message) {
  uint8_t   buffer[64] = {0};
  bytes32_t root       = {0};

  // compute fork_data root hash to the seconf 32 bytes of bffer
  buffer[0] = compute_fork_version((slot - 1) >> 5);
  memcpy(buffer + 4, GENESIS_VALIDATORS_ROOT, 32);

  ssz_hash_tree_root(ssz_ob(FORK_DATA_CONTAINER, bytes(buffer, 36)), root);

  // build domain by replacing the first 4 bytes with the sync committee domain which creates the domain-data in the 2nd 32 bytes of buffer
  memcpy(buffer, blockhash, 32);
  memset(buffer + 32, 0, 4);
  memcpy(buffer + 36, root, 28);
  buffer[32] = 7; // Domain-Type SYNC_COMMITTEE

  ssz_hash_tree_root(ssz_ob(SIGNING_DATA_CONTAINER, bytes(buffer, 64)), signing_message);

  return true;
}

static bool verify_blockroot_signature(verify_ctx_t* ctx, ssz_ob_t* header, ssz_ob_t* sync_committee_bits, ssz_ob_t* sync_committee_signature) {
  bytes32_t       root       = {0};
  uint64_t        slot       = ssz_get_uint64(header, "slot");
  c4_sync_state_t sync_state = {0};

  if (slot == 0) RETURN_VERIFY_ERROR(ctx, "slot is missing in beacon header!");

  // compute blockhash
  ssz_hash_tree_root(*header, root);

  // compute signing message and store it in root again
  calculate_signing_message(ctx, slot, root, root);

  // get the validators and make sure we have the right ones for the requested period
  sync_state = c4_get_validators(slot >> 13);
  if (sync_state.validators.data == NULL) {
    ctx->first_missing_period = sync_state.last_period + 1;
    ctx->last_missing_period  = sync_state.current_period;
    ctx->success              = false;
    ctx->error                = "sync_committee transitions required to verify";
    return false;
  }

  if (!blst_verify(root, sync_committee_signature->bytes.data, sync_state.validators.data, 512, sync_committee_bits->bytes))
    RETURN_VERIFY_ERROR(ctx, "invalid blockhash signature!");

  return true;
}

static bool verify_beacon_header(ssz_ob_t* header, bytes32_t exec_blockhash, bytes_t blockhash_proof) {

  // check merkle proof
  ssz_ob_t  header_body_root = ssz_get(header, "bodyRoot");
  bytes32_t root_hash;
  ssz_verify_merkle_proof(blockhash_proof, exec_blockhash, BLOCKHASH_BLOCKBODY_GINDEX, root_hash);
  if (ssz_is_error(header_body_root) || header_body_root.bytes.len != 32 || memcmp(root_hash, header_body_root.bytes.data, 32)) return false;

  return true;
}

bool verify_blockhash_proof(verify_ctx_t* ctx) {
  ctx->type = PROOF_TYPE_BEACON_HEADER;

  ssz_ob_t header                   = ssz_get(&ctx->proof, "header");
  ssz_ob_t blockhash_proof          = ssz_get(&ctx->proof, "blockhash_proof");
  ssz_ob_t sync_committee_bits      = ssz_get(&ctx->proof, "sync_committee_bits");
  ssz_ob_t sync_committee_signature = ssz_get(&ctx->proof, "sync_committee_signature");

  if (ssz_is_error(header) || ssz_is_error(blockhash_proof)) RETURN_VERIFY_ERROR(ctx, "invalid proof, missing header or blockhash_proof!");
  if (ssz_is_error(sync_committee_bits) || sync_committee_bits.bytes.len != 64 || ssz_is_error(sync_committee_signature) || sync_committee_signature.bytes.len != 96) RETURN_VERIFY_ERROR(ctx, "invalid proof, missing sync committee bits or signature!");
  if (!ctx->data.def || !ssz_is_type(&ctx->data, &ssz_bytes32) || ctx->data.bytes.data == NULL || ctx->data.bytes.len != 32) RETURN_VERIFY_ERROR(ctx, "invalid data, data is not a bytes32!");
  if (!verify_beacon_header(&header, ctx->data.bytes.data, blockhash_proof.bytes)) RETURN_VERIFY_ERROR(ctx, "invalid merkle proof for blockhash!");
  if (!verify_blockroot_signature(ctx, &header, &sync_committee_bits, &sync_committee_signature)) RETURN_VERIFY_ERROR(ctx, "invalid blockhash signature!");

  ctx->success = true;
  return true;
}