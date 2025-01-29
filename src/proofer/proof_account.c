#include "proof_account.h"
#include "../util/json.h"
#include "../util/ssz.h"
#include "../verifier/types_beacon.h"
#include "../verifier/types_verify.h"
#include "ssz_types.h"
#include <inttypes.h> // Include this header for PRIu64 and PRIx64
#include <stdlib.h>
#include <string.h>

static ssz_ob_t get_execution_payload(proofer_ctx_t* ctx, ssz_ob_t block) {
  ssz_ob_t body = ssz_get(&block, "body");
  return ssz_get(&body, "executionPayload");
}

c4_status_t get_block(proofer_ctx_t* ctx, uint64_t slot, ssz_ob_t* block) {

  char path[100];
  if (slot == 0)
    sprintf(path, "eth/v2/beacon/blocks/head");
  else {
    sprintf(path, "eth/v2/beacon/blocks/%" PRIu64, slot);
  }

  bytes_t block_data;
  TRY_ASYNC(c4_send_beacon_ssz(ctx, path, NULL, &block_data));

  //  bytes_write(block_data, fopen("signed_block.ssz", "w"), true);

  ssz_ob_t signed_block = ssz_ob(SIGNED_BEACON_BLOCK_CONTAINER, block_data);
  *block                = ssz_get(&signed_block, "message");
  if (ssz_is_error(*block)) {
    ctx->error = strdup("Invalid block-format!");
    return C4_ERROR;
  }

  return C4_SUCCESS;
}

c4_status_t get_latest_block(proofer_ctx_t* ctx, ssz_ob_t* sig_block, ssz_ob_t* data_block) {

  TRY_ASYNC(get_block(ctx, 0, sig_block));

  uint64_t sig_slot = ssz_get_uint64(sig_block, "slot");
  TRY_ASYNC(get_block(ctx, sig_slot - 1, data_block));

  if (!sig_slot) {
    ctx->error = strdup("Invalid slot!");
    return C4_ERROR;
  }
  return C4_SUCCESS;
}

static c4_status_t get_eth_proof(proofer_ctx_t* ctx, json_t address, json_t* proof, uint64_t block_number) {
  char tmp[120];
  snprintf(tmp, sizeof(tmp), "[%.44s,[],\"0x%" PRIx64 "\"]", address.start, block_number);
  return c4_send_eth_rpc(ctx, "eth_getProof", tmp, proof);
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

static c4_status_t create_eth_account_proof(proofer_ctx_t* ctx, json_t eth_proof, ssz_ob_t syncAggregate, bytes32_t body_root, ssz_ob_t block, bytes_t state_proof, json_t address) {

  buffer_t      tmp               = {0};
  ssz_builder_t eth_account_proof = {0};
  ssz_builder_t eth_state_proof   = {0};
  ssz_builder_t beacon_header     = {0};
  ssz_builder_t c4_req            = {0};
  eth_account_proof.def           = (ssz_def_t*) &ETH_ACCOUNT_PROOF_CONTAINER;
  eth_state_proof.def             = (ssz_def_t*) (eth_account_proof.def->def.container.elements + 7); // TODO:  use the name to identify last element
  beacon_header.def               = (ssz_def_t*) &BEACON_BLOCKHEADER_CONTAINER;
  c4_req.def                      = (ssz_def_t*) &C4_REQUEST_CONTAINER;
  uint8_t union_selector          = 2; // TODO:  use the name to find the index based on the union definition

  // build the header
  ssz_add_bytes(&beacon_header, "slot", ssz_get(&block, "slot").bytes);
  ssz_add_bytes(&beacon_header, "proposerIndex", ssz_get(&block, "proposerIndex").bytes);
  ssz_add_bytes(&beacon_header, "parentRoot", ssz_get(&block, "parentRoot").bytes);
  ssz_add_bytes(&beacon_header, "stateRoot", ssz_get(&block, "stateRoot").bytes);
  ssz_add_bytes(&beacon_header, "bodyRoot", bytes(body_root, 32));

  // build the state proof
  ssz_add_bytes(&eth_state_proof, "state_proof", state_proof);
  ssz_add_builders(&eth_state_proof, "header", &beacon_header);
  ssz_add_bytes(&eth_state_proof, "sync_committee_bits", ssz_get(&syncAggregate, "syncCommitteeBits").bytes);
  ssz_add_bytes(&eth_state_proof, "sync_committee_signature", ssz_get(&syncAggregate, "syncCommitteeSignature").bytes);

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
  json_as_bytes(json_get(eth_proof, "balance"), &tmp);
  buffer_splice(&tmp, 0, 0, bytes(NULL, 33 - tmp.data.len)); // we add zeros at the beginning so have a fixed length of 32+ selector
  tmp.data.data[0] = 2;                                      // union selector for balance == index 2

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

  ssz_ob_t sig_block;
  ssz_ob_t data_block;
  json_t   eth_proof;
  TRY_ASYNC(get_latest_block(ctx, &sig_block, &data_block));
  ssz_ob_t execution_payload = get_execution_payload(ctx, data_block);
  uint64_t block_number      = ssz_get_uint64(&execution_payload, "blockNumber");

  TRY_ASYNC(get_eth_proof(ctx, address, &eth_proof, block_number));

  ssz_ob_t  sig_body = ssz_get(&sig_block, "body");
  ssz_ob_t  body     = ssz_get(&data_block, "body");
  bytes32_t body_root;
  buffer_t  state_proof = {0};
  uint32_t  gindex;

  char* path[] = {"executionPayload", "stateRoot"};
  ssz_hash_tree_root(body, body_root);
  ssz_create_proof(body, (char**) path, 2, &state_proof, &gindex);

  TRY_ASYNC_FINAL(
      create_eth_account_proof(ctx, eth_proof, ssz_get(&sig_body, "syncAggregate"),
                               body_root, data_block, state_proof.data, address),
      buffer_free(&state_proof));

  return C4_SUCCESS;
}