
#include "../util/bytes.h"
#include "../util/crypto.h"
#include "../util/ssz.h"
#include "eth_verify.h"
#include "sync_committee.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** the combined GIndex of the blockhash in the block body path = executionPayload*/
#define BLOCKHASH_BLOCKBODY_GINDEX 812

// combining the root with a domain to ensure uniqueness of the signing message
static const ssz_def_t SIGNING_DATA[] = {
    SSZ_BYTES32("root"),    // the hashed root of the data to sign
    SSZ_BYTES32("domain")}; // the domain of the data to sign

static const ssz_def_t SIGNING_DATA_CONTAINER = SSZ_CONTAINER("SigningData", SIGNING_DATA);

// the fork data is used to create the domain
static const ssz_def_t FORK_DATA[] = {
    SSZ_BYTE_VECTOR("version", 4), // the version of the fork
    SSZ_BYTES32("state")};         // the state of the Genisis Block

static const ssz_def_t FORK_DATA_CONTAINER = SSZ_CONTAINER("ForkDate", FORK_DATA);

bool eth_calculate_domain(chain_id_t chain_id, uint64_t slot, bytes32_t domain) {
  uint8_t   buffer[36] = {0};
  bytes32_t root       = {0};

  // compute fork_data root hash to the seconf 32 bytes of bffer
  buffer[0] = (uint8_t) c4_chain_fork_id(chain_id, (slot - 1) >> 5);
  if (!c4_chain_genesis_validators_root(chain_id, buffer + 4)) false;

  ssz_hash_tree_root(ssz_ob(FORK_DATA_CONTAINER, bytes(buffer, 36)), root);

  // build domain by replacing the first 4 bytes with the sync committee domain which creates the domain-data in the 2nd 32 bytes of buffer
  memset(domain, 0, 4);
  memcpy(domain + 4, root, 28);
  domain[0] = 7; // Domain-Type SYNC_COMMITTEE
  return true;
}

static bool calculate_signing_message(verify_ctx_t* ctx, uint64_t slot, bytes32_t blockhash, bytes32_t signing_message) {
  uint8_t buffer[64] = {0};
  memcpy(buffer, blockhash, 32);
  if (!eth_calculate_domain(ctx->chain_id, slot, buffer + 32)) RETURN_VERIFY_ERROR(ctx, "unsupported chain!");
  ssz_hash_tree_root(ssz_ob(SIGNING_DATA_CONTAINER, bytes(buffer, 64)), signing_message);
  return true;
}

c4_status_t c4_verify_blockroot_signature(verify_ctx_t* ctx, ssz_ob_t* header, ssz_ob_t* sync_committee_bits, ssz_ob_t* sync_committee_signature, uint64_t slot) {
  bytes32_t       root       = {0};
  c4_sync_state_t sync_state = {0};

  if (slot == 0) slot = ssz_get_uint64(header, "slot");
  if (slot == 0) THROW_ERROR("slot is missing in beacon header!");

  // get the validators and make sure we have the right ones for the requested period
  TRY_ASYNC(c4_get_validators(ctx, slot >> 13, &sync_state));

  // compute blockhash
  ssz_hash_tree_root(*header, root);

  // compute signing message and store it in root again
  calculate_signing_message(ctx, slot, root, root);

  // verify the signature
  bool valid = blst_verify(root, sync_committee_signature->bytes.data, sync_state.validators.data, 512, sync_committee_bits->bytes, sync_state.deserialized);

#ifndef C4_STATIC_MEMORY
  free(sync_state.validators.data);
#endif

  if (!valid)
    THROW_ERROR("invalid blockhash signature!");

  return C4_SUCCESS;
}

static bool verify_beacon_header(ssz_ob_t* header, bytes32_t exec_blockhash, bytes_t blockhash_proof) {

  // check merkle proof
  ssz_ob_t  header_body_root = ssz_get(header, "bodyRoot");
  bytes32_t root_hash;
  ssz_verify_single_merkle_proof(blockhash_proof, exec_blockhash, BLOCKHASH_BLOCKBODY_GINDEX, root_hash);
  if (ssz_is_error(header_body_root) || header_body_root.bytes.len != 32 || memcmp(root_hash, header_body_root.bytes.data, 32)) return false;

  return true;
}

bool verify_blockhash_proof(verify_ctx_t* ctx) {
  ssz_ob_t header                   = ssz_get(&ctx->proof, "header");
  ssz_ob_t blockhash_proof          = ssz_get(&ctx->proof, "blockhash_proof");
  ssz_ob_t sync_committee_bits      = ssz_get(&ctx->proof, "sync_committee_bits");
  ssz_ob_t sync_committee_signature = ssz_get(&ctx->proof, "sync_committee_signature");

  if (ssz_is_error(header) || ssz_is_error(blockhash_proof)) RETURN_VERIFY_ERROR(ctx, "invalid proof, missing header or blockhash_proof!");
  if (ssz_is_error(sync_committee_bits) || sync_committee_bits.bytes.len != 64 || ssz_is_error(sync_committee_signature) || sync_committee_signature.bytes.len != 96) RETURN_VERIFY_ERROR(ctx, "invalid proof, missing sync committee bits or signature!");
  if (!ctx->data.def || !ssz_is_type(&ctx->data, &ssz_bytes32) || ctx->data.bytes.data == NULL || ctx->data.bytes.len != 32) RETURN_VERIFY_ERROR(ctx, "invalid data, data is not a bytes32!");
  if (!verify_beacon_header(&header, ctx->data.bytes.data, blockhash_proof.bytes)) RETURN_VERIFY_ERROR(ctx, "invalid merkle proof for blockhash!");
  if (c4_verify_blockroot_signature(ctx, &header, &sync_committee_bits, &sync_committee_signature, 0) != C4_SUCCESS) return false;

  ctx->success = true;
  return true;
}