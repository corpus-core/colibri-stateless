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

#include "beacon.h"
#include "beacon_types.h"
#include "chains.h"
#include "eth_account.h"
#include "eth_req.h"
#include "eth_tools.h"
#include "historic_proof.h"
#include "json.h"
#include "op_prover.h"
#include "op_tools.h"
#include "op_types.h"
#include "prover.h"
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

static ssz_builder_t create_storage_proof(prover_ctx_t* ctx, const ssz_def_t* def, json_t storage_list) {
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

static c4_status_t create_eth_account_proof(prover_ctx_t* ctx, json_t eth_proof, json_t address, json_t block_number, ssz_builder_t block_proof) {

  json_t        json_code         = {0};
  address_t     address_buf       = {0};
  buffer_t      tmp               = stack_buffer(address_buf);
  ssz_builder_t eth_data          = {0};
  ssz_builder_t eth_account_proof = ssz_builder_for_op_type(OP_SSZ_VERIFY_ACCOUNT_PROOF);

  // make sure we have the full code
  if (strcmp(ctx->method, "eth_getCode") == 0) TRY_ASYNC(eth_get_code(ctx, address, &json_code, 0));

  // build the account proof
  add_dynamic_byte_list(json_get(eth_proof, "accountProof"), &eth_account_proof, "accountProof");
  ssz_add_bytes(&eth_account_proof, "address", json_as_bytes(address, &tmp));
  ssz_add_builders(&eth_account_proof, "storageProof", create_storage_proof(ctx, ssz_get_def(eth_account_proof.def, "storageProof"), json_get(eth_proof, "storageProof")));
  ssz_add_builders(&eth_account_proof, "block_proof", block_proof);

  // build the data only if we have code
  if (strcmp(ctx->method, "eth_getCode") == 0) {
    eth_data.def = eth_ssz_verification_type(ETH_SSZ_DATA_BYTES);
    json_as_bytes(json_code, &eth_data.fixed);
  }

  ctx->proof = op_create_proof_request(
      ctx->chain_id,
      eth_data,
      eth_account_proof,
      NULL_SSZ_BUILDER);

  return C4_SUCCESS;
}

c4_status_t c4_op_proof_account(prover_ctx_t* ctx) {
  bool          is_storage_at       = strcmp(ctx->method, "eth_getStorageAt") == 0;
  bool          is_proof            = strcmp(ctx->method, "eth_getProof") == 0;
  json_t        address             = json_at(ctx->params, 0);
  json_t        storage_keys        = is_storage_at || is_proof ? json_at(ctx->params, 1) : (json_t) {0};
  json_t        block_number        = json_at(ctx->params, is_storage_at || is_proof ? 2 : 1);
  json_t        eth_proof           = {0};
  c4_status_t   status              = C4_SUCCESS;
  ssz_builder_t block_proof         = {0};
  uint64_t      block_number_uint64 = 0;

  if (is_storage_at)
    CHECK_JSON(ctx->params, "[address,bytes32,block]", "Invalid arguments for eth_getStorageAt: ");
  else if (is_proof)
    CHECK_JSON(ctx->params, "[address,[bytes32],block]", "Invalid arguments for eth_getProof: ");
  else
    CHECK_JSON(ctx->params, "[address,block]", "Invalid arguments for AccountProof: ");
  TRY_ASYNC(c4_op_create_block_proof(ctx, block_number, &block_proof));
  ssz_ob_t* execution_payload = op_get_execution_payload(&block_proof);
  block_number_uint64         = ssz_get_uint64(execution_payload, "blockNumber");
  safe_free(execution_payload);
  TRY_ASYNC_CATCH(eth_get_proof(ctx, address, storage_keys, &eth_proof, block_number_uint64), ssz_builder_free(&block_proof));
  TRY_ASYNC_CATCH(create_eth_account_proof(ctx, eth_proof, address, block_number, block_proof), ssz_builder_free(&block_proof));
  return C4_SUCCESS;
}