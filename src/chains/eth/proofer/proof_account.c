#include "beacon.h"
#include "beacon_types.h"
#include "chains.h"
#include "eth_account.h"
#include "eth_req.h"
#include "eth_tools.h"
#include "historic_proof.h"
#include "json.h"
#include "proofer.h"
#include "ssz.h"
#include "version.h"
#include <inttypes.h> // Include this header for PRIu64 and PRIx64
#include <stdlib.h>
#include <string.h>

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
  safe_free(list_ob.bytes.data);
  buffer_free(&tmp);
}

static ssz_builder_t create_storage_proof(proofer_ctx_t* ctx, const ssz_def_t* def, json_t storage_list) {
  ssz_builder_t storage_proof = {.def = def};
  bytes32_t     tmp;
  buffer_t      tmp_buffer = stack_buffer(tmp);
  int           len        = json_len(storage_list);
  json_for_each_value(storage_list, entry) {
    ssz_builder_t storage_builder = {.def = def->def.vector.type};
    ssz_add_bytes(&storage_builder, "key", json_as_bytes(json_get(entry, "key"), &tmp_buffer));
    add_dynamic_byte_list(json_get(entry, "proof"), &storage_builder, "proof");
    ssz_add_dynamic_list_builders(&storage_proof, len, storage_builder);
  }
  return storage_proof;
}

static c4_status_t create_eth_account_proof(proofer_ctx_t* ctx, json_t eth_proof, beacon_block_t* block_data, json_t address, json_t block_number, blockroot_proof_t historic_proof) {

  json_t        json_code         = {0};
  buffer_t      tmp               = {0};
  ssz_builder_t eth_data          = {0};
  ssz_builder_t eth_account_proof = ssz_builder_for_type(ETH_SSZ_VERIFY_ACCOUNT_PROOF);

  // make sure we have the full code
  if (strcmp(ctx->method, "eth_getCode") == 0) TRY_ASYNC(eth_get_code(ctx, address, &json_code, 0));

  // build the account proof
  add_dynamic_byte_list(json_get(eth_proof, "accountProof"), &eth_account_proof, "accountProof");
  ssz_add_bytes(&eth_account_proof, "address", json_as_bytes(address, &tmp));
  ssz_add_builders(&eth_account_proof, "storageProof", create_storage_proof(ctx, ssz_get_def(eth_account_proof.def, "storageProof"), json_get(eth_proof, "storageProof")));
  ssz_add_builders(&eth_account_proof, "state_proof", eth_ssz_create_state_proof(ctx, block_number, block_data, &historic_proof));

  // build the data only if we have code
  if (strcmp(ctx->method, "eth_getCode") == 0) {
    eth_data.def = eth_ssz_verification_type(ETH_SSZ_DATA_BYTES);
    json_as_bytes(json_code, &eth_data.fixed);
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
  bool              is_storage_at  = strcmp(ctx->method, "eth_getStorageAt") == 0;
  bool              is_proof       = strcmp(ctx->method, "eth_getProof") == 0;
  json_t            address        = json_at(ctx->params, 0);
  json_t            storage_keys   = is_storage_at || is_proof ? json_at(ctx->params, 1) : (json_t) {0};
  json_t            block_number   = json_at(ctx->params, is_storage_at || is_proof ? 2 : 1);
  json_t            eth_proof      = {0};
  beacon_block_t    block          = {0};
  blockroot_proof_t historic_proof = {0};
  c4_status_t       status         = C4_SUCCESS;

  if (is_storage_at)
    CHECK_JSON(ctx->params, "[address,bytes32,block]", "Invalid arguments for eth_getStorageAt: ");
  else if (is_proof)
    CHECK_JSON(ctx->params, "[address,[bytes32],block]", "Invalid arguments for eth_getProof: ");
  else
    CHECK_JSON(ctx->params, "[address,block]", "Invalid arguments for AccountProof: ");

  TRY_ASYNC(c4_beacon_get_block_for_eth(ctx, block_number, &block));
  TRY_ADD_ASYNC(status, eth_get_proof(ctx, address, storage_keys, &eth_proof, ssz_get_uint64(&block.execution, "blockNumber")));
  TRY_ADD_ASYNC(status, c4_check_historic_proof(ctx, &historic_proof, &block));
  if (status != C4_SUCCESS) {
    c4_free_block_proof(&historic_proof);
    return status;
  }

  status = create_eth_account_proof(ctx, eth_proof, &block, address, block_number, historic_proof);
  c4_free_block_proof(&historic_proof);
  return status;
}