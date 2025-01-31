#include "proof_account.h"
#include "../util/json.h"
#include "../util/ssz.h"
#include "../verifier/types_beacon.h"
#include "../verifier/types_verify.h"
#include "beacon.h"
#include "ssz_types.h"
#include <inttypes.h> // Include this header for PRIu64 and PRIx64
#include <stdlib.h>
#include <string.h>

static c4_status_t get_eth_proof(proofer_ctx_t* ctx, json_t address, json_t storage_key, json_t* proof, uint64_t block_number) {
  char storage[80] = {0};
  char tmp[200];
  if (storage_key.len && storage_key.len < sizeof(storage))
    memcpy(storage, storage_key.start, storage_key.len);

  snprintf(tmp, sizeof(tmp), "[%.44s,[%s],\"0x%" PRIx64 "\"]", address.start, storage, block_number);
  return c4_send_eth_rpc(ctx, "eth_getProof", tmp, proof);
}

static c4_status_t get_eth_code(proofer_ctx_t* ctx, json_t address, json_t* code, uint64_t block_number) {
  char   tmp[120];
  json_t data = {0};
  snprintf(tmp, sizeof(tmp), "[%.44s,\"latest\"]", address.start);
  return c4_send_eth_rpc(ctx, "eth_getCode", tmp, code);
}

static void add_dynamic_byte_list(json_t bytes_list, ssz_builder_t* builder, char* name) {
  ssz_builder_t list = {0};
  list.def           = (ssz_def_t*) &ETH_ACCOUNT_PROOF_CONTAINER.def.container.elements[0];

  buffer_t tmp    = {0};
  size_t   len    = json_len(bytes_list);
  uint32_t offset = 0;
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
  ssz_builder_t eth_account_proof = {0};
  ssz_builder_t eth_state_proof   = {0};
  ssz_builder_t beacon_header     = {0};
  ssz_builder_t c4_req            = {0};
  ssz_ob_t      block             = block_data->header;
  eth_account_proof.def           = (ssz_def_t*) &ETH_ACCOUNT_PROOF_CONTAINER;
  eth_state_proof.def             = (ssz_def_t*) (eth_account_proof.def->def.container.elements + 7); // TODO:  use the name to identify last element
  beacon_header.def               = (ssz_def_t*) &BEACON_BLOCKHEADER_CONTAINER;
  c4_req.def                      = (ssz_def_t*) &C4_REQUEST_CONTAINER;
  uint8_t union_selector          = 2; // TODO:  use the name to find the index based on the union definition

  // make sure we have the full code
  if (strcmp(ctx->method, "eth_getCode") == 0)
    TRY_ASYNC(get_eth_code(ctx, address, &json_code, 0));

  // build the header
  ssz_add_bytes(&beacon_header, "slot", ssz_get(&block, "slot").bytes);
  ssz_add_bytes(&beacon_header, "proposerIndex", ssz_get(&block, "proposerIndex").bytes);
  ssz_add_bytes(&beacon_header, "parentRoot", ssz_get(&block, "parentRoot").bytes);
  ssz_add_bytes(&beacon_header, "stateRoot", ssz_get(&block, "stateRoot").bytes);
  ssz_add_bytes(&beacon_header, "bodyRoot", bytes(body_root, 32));

  // build the state proof
  ssz_add_bytes(&eth_state_proof, "state_proof", state_proof);
  ssz_add_builders(&eth_state_proof, "header", &beacon_header);
  ssz_add_bytes(&eth_state_proof, "sync_committee_bits", ssz_get(&block_data->sync_aggregate, "syncCommitteeBits").bytes);
  ssz_add_bytes(&eth_state_proof, "sync_committee_signature", ssz_get(&block_data->sync_aggregate, "syncCommitteeSignature").bytes);

  // build the account proof
  buffer_grow(&eth_account_proof.fixed, 256);
  buffer_append(&eth_account_proof.fixed, bytes(&union_selector, 1)); // we add the union selector at the beginning
  add_dynamic_byte_list(json_get(eth_proof, "accountProof"), &eth_account_proof, "accountProof");
  ssz_add_bytes(&eth_account_proof, "address", json_as_bytes(address, &tmp));
  ssz_add_bytes(&eth_account_proof, "balance", json_as_bytes(json_get(eth_proof, "balance"), &tmp));
  ssz_add_bytes(&eth_account_proof, "codeHash", json_as_bytes(json_get(eth_proof, "codeHash"), &tmp));
  ssz_add_bytes(&eth_account_proof, "nonce", json_as_bytes(json_get(eth_proof, "nonce"), &tmp));
  ssz_add_bytes(&eth_account_proof, "storageHash", json_as_bytes(json_get(eth_proof, "storageHash"), &tmp));
  ssz_add_bytes(&eth_account_proof, "storageProof", bytes(tmp.data.data, 0)); // for now, we add an empty list
  ssz_add_builders(&eth_account_proof, "state_proof", &eth_state_proof);

  // build the data
  if (strcmp(ctx->method, "eth_getBalance") == 0) {
    union_selector = 1;
    json_as_bytes(json_get(eth_proof, "balance"), &tmp);
  }
  else if (strcmp(ctx->method, "eth_getCode") == 0) {
    union_selector = 2;
    json_as_bytes(json_code, &tmp);
  }
  else if (strcmp(ctx->method, "eth_getNonce") == 0) {
    union_selector = 1;
    json_as_bytes(json_get(eth_proof, "nonce"), &tmp);
  }
  else if (strcmp(ctx->method, "eth_getStorageAt") == 0) {
    union_selector = 1;
    json_as_bytes(json_get(json_at(json_get(eth_proof, "storageProof"), 0), "value"), &tmp);
  }
  //    json_as_bytes(json_get(eth_proof, "storageHash"), &tmp);

  if (union_selector == 1)
    buffer_splice(&tmp, 0, 0, bytes(NULL, 33 - tmp.data.len)); // we add zeros at the beginning so have a fixed length of 32+ selector
  else if (union_selector == 2)
    buffer_splice(&tmp, 0, 0, bytes(NULL, 1)); // make room for one byte
  tmp.data.data[0] = union_selector;           // union selector for bytes32 == index 1

  // build the request
  ssz_add_bytes(&c4_req, "data", tmp.data);
  ssz_add_builders(&c4_req, "proof", &eth_account_proof);

  // empty sync_data
  union_selector = 0;
  ssz_add_bytes(&c4_req, "sync_data", bytes(&union_selector, 1));

  buffer_free(&tmp);
  ctx->proof = ssz_builder_to_bytes(&c4_req).bytes;
  return C4_SUCCESS;
}

c4_status_t c4_proof_account(proofer_ctx_t* ctx) {
  json_t address = json_at(ctx->params, 0);
  // json_t block   = json_at(ctx->params, 1);

  if (address.type != JSON_TYPE_STRING || address.len != 44 || address.start[1] != '0' || address.start[2] != 'x') {
    ctx->error = strdup("Invalid address");
    return C4_ERROR;
  }

  json_t         eth_proof;
  beacon_block_t block = {0};
  TRY_ASYNC(c4_beacon_get_block_for_eth(ctx, json_at(ctx->params, strcmp(ctx->method, "eth_getStorageAt") == 0 ? 2 : 1), &block));

  uint64_t block_number = ssz_get_uint64(&block.execution, "blockNumber");
  TRY_ASYNC(get_eth_proof(ctx, address, strcmp(ctx->method, "eth_getStorageAt") == 0 ? json_at(ctx->params, 1) : (json_t) {0}, &eth_proof, block_number));

  bytes32_t body_root;
  ssz_hash_tree_root(block.body, body_root);
  bytes_t state_proof = ssz_create_proof(block.body, ssz_gindex(block.body.def, 2, "executionPayload", "stateRoot"));

  TRY_ASYNC_FINAL(
      create_eth_account_proof(ctx, eth_proof, &block,
                               body_root, state_proof, address),
      free(state_proof.data));

  return C4_SUCCESS;
}