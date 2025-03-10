#include "beacon.h"
#include "beacon_types.h"
#include "eth_req.h"
#include "eth_tools.h"
#include "json.h"
#include "proofer.h"
#include "ssz.h"
#include "version.h"
#include <inttypes.h> // Include this header for PRIu64 and PRIx64
#include <stdlib.h>
#include <string.h>

static c4_status_t get_eth_proof(proofer_ctx_t* ctx, json_t address, json_t storage_key, json_t* proof, uint64_t block_number) {
  char     tmp[300];
  buffer_t buffer = stack_buffer(tmp);
  bprintf(&buffer, "[%J,[", address);
  if (storage_key.len && storage_key.len < 70)
    bprintf(&buffer, "%J", storage_key);
  bprintf(&buffer, "],\"0x%lx\"]", block_number);

  return c4_send_eth_rpc(ctx, "eth_getProof", tmp, proof);
}

static c4_status_t get_eth_code(proofer_ctx_t* ctx, json_t address, json_t* code, uint64_t block_number) {
  char     tmp[120];
  buffer_t buf = stack_buffer(tmp);
  return c4_send_eth_rpc(ctx, "eth_getCode", bprintf(&buf, "[%J,\"lastest\"]", address), code);
}

static void add_dynamic_byte_list(json_t bytes_list, ssz_builder_t* builder, char* name) {
  const ssz_def_t* account_proof_container = eth_ssz_verification_type(ETH_SSZ_VERIFY_ACCOUNT_PROOF);
  ssz_builder_t    list                    = {0};
  list.def                                 = (ssz_def_t*) &account_proof_container->def.container.elements[0];
  buffer_t tmp                             = {0};
  size_t   len                             = json_len(bytes_list);
  uint32_t offset                          = 0;
  for (size_t i = 0; i < len; i++)
    ssz_add_dynamic_list_bytes(&list, len, json_as_bytes(json_at(bytes_list, i), &tmp));

  ssz_ob_t list_ob = ssz_builder_to_bytes(&list);
  ssz_add_bytes(builder, name, list_ob.bytes);
  free(list_ob.bytes.data);
  buffer_free(&tmp);
}

static c4_status_t create_eth_account_proof(proofer_ctx_t* ctx, json_t eth_proof, beacon_block_t* block_data, bytes32_t body_root, bytes_t state_proof, json_t address) {

  json_t        json_code         = {0};
  buffer_t      tmp               = {0};
  ssz_builder_t eth_account_proof = ssz_builder_for_type(ETH_SSZ_VERIFY_ACCOUNT_PROOF);
  ssz_builder_t eth_state_proof   = ssz_builder_for_type(ETH_SSZ_VERIFY_STATE_PROOF);
  ssz_builder_t eth_data          = {0};

  // make sure we have the full code
  if (strcmp(ctx->method, "eth_getCode") == 0) TRY_ASYNC(get_eth_code(ctx, address, &json_code, 0));

  // build the state proof
  ssz_add_bytes(&eth_state_proof, "state_proof", state_proof);
  ssz_add_builders(&eth_state_proof, "header", c4_proof_add_header(block_data->header, body_root));
  ssz_add_bytes(&eth_state_proof, "sync_committee_bits", ssz_get(&block_data->sync_aggregate, "syncCommitteeBits").bytes);
  ssz_add_bytes(&eth_state_proof, "sync_committee_signature", ssz_get(&block_data->sync_aggregate, "syncCommitteeSignature").bytes);

  // build the account proof
  add_dynamic_byte_list(json_get(eth_proof, "accountProof"), &eth_account_proof, "accountProof");
  ssz_add_bytes(&eth_account_proof, "address", json_as_bytes(address, &tmp));
  ssz_add_bytes(&eth_account_proof, "balance", json_as_bytes(json_get(eth_proof, "balance"), &tmp));
  ssz_add_bytes(&eth_account_proof, "codeHash", json_as_bytes(json_get(eth_proof, "codeHash"), &tmp));
  ssz_add_bytes(&eth_account_proof, "nonce", json_as_bytes(json_get(eth_proof, "nonce"), &tmp));
  ssz_add_bytes(&eth_account_proof, "storageHash", json_as_bytes(json_get(eth_proof, "storageHash"), &tmp));
  ssz_add_bytes(&eth_account_proof, "storageProof", bytes(tmp.data.data, 0)); // for now, we add an empty list
  ssz_add_builders(&eth_account_proof, "state_proof", eth_state_proof);

  // build the data

  if (strcmp(ctx->method, "eth_getBalance") == 0) {
    eth_data.def = eth_ssz_verification_type(ETH_SSZ_DATA_UINT256);
    ssz_add_uint256(&eth_data, json_as_bytes(json_get(eth_proof, "balance"), &tmp));
  }
  else if (strcmp(ctx->method, "eth_getCode") == 0) {
    eth_data.def = eth_ssz_verification_type(ETH_SSZ_DATA_BYTES);
    json_as_bytes(json_code, &eth_data.fixed);
  }
  else if (strcmp(ctx->method, "eth_getNonce") == 0) {
    eth_data.def = eth_ssz_verification_type(ETH_SSZ_DATA_UINT256);
    ssz_add_uint256(&eth_data, json_as_bytes(json_get(eth_proof, "nonce"), &tmp));
  }
  else if (strcmp(ctx->method, "eth_getStorageAt") == 0) {
    eth_data.def = eth_ssz_verification_type(ETH_SSZ_DATA_UINT256);
    ssz_add_uint256(&eth_data, json_as_bytes(json_get(json_at(json_get(eth_proof, "storageProof"), 0), "value"), &tmp));
  }

  ctx->proof = eth_create_proof_request(
      ctx->chain_id,
      eth_data,
      eth_account_proof,
      NULL_SSZ_BUILDER);

  // empty sync_data
  buffer_free(&tmp);
  return C4_SUCCESS;
}

c4_status_t c4_proof_account(proofer_ctx_t* ctx) {
  bool           is_storage_at = strcmp(ctx->method, "eth_getStorageAt") == 0;
  json_t         address       = json_at(ctx->params, 0);
  json_t         storage_keys  = is_storage_at ? json_at(ctx->params, 1) : (json_t) {0};
  json_t         block_number  = json_at(ctx->params, is_storage_at ? 2 : 1);
  json_t         eth_proof     = {0};
  beacon_block_t block         = {0};
  bytes32_t      body_root;

  if (is_storage_at)
    CHECK_JSON(ctx->params, "[address,[bytes32],block]", "Invalid arguments for eth_getStorageAt: ");
  else
    CHECK_JSON(ctx->params, "[address,block]", "Invalid arguments for AccountProof: ");

  TRY_ASYNC(c4_beacon_get_block_for_eth(ctx, block_number, &block));
  TRY_ASYNC(get_eth_proof(ctx, address, storage_keys,
                          &eth_proof, ssz_get_uint64(&block.execution, "blockNumber")));

  bytes_t state_proof = ssz_create_proof(block.body, body_root, ssz_gindex(block.body.def, 2, "executionPayload", "stateRoot"));

  TRY_ASYNC_FINAL(
      create_eth_account_proof(ctx, eth_proof, &block, body_root, state_proof, address),
      free(state_proof.data));

  return C4_SUCCESS;
}