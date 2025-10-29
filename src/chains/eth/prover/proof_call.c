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
#include "eth_account.h"
#include "eth_req.h"
#include "eth_tools.h"
#include "json.h"
#include "logger.h"
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

static c4_status_t create_eth_call_proof(prover_ctx_t* ctx, ssz_builder_t account_proofs, beacon_block_t* block_data, json_t block_number, blockroot_proof_t* historic_proof) {

  ssz_builder_t eth_call_proof = ssz_builder_for_type(ETH_SSZ_VERIFY_CALL_PROOF);
  ssz_add_builders(&eth_call_proof, "accounts", account_proofs);
  ssz_add_builders(&eth_call_proof, "state_proof", eth_ssz_create_state_proof(ctx, block_number, block_data, historic_proof));

  ctx->proof = eth_create_proof_request(
      ctx->chain_id,
      NULL_SSZ_BUILDER,
      eth_call_proof,
      NULL_SSZ_BUILDER);

  return C4_SUCCESS;
}

static void add_account(prover_ctx_t* ctx, ssz_builder_t* builder, json_t values, bytes_t address, json_t code, int accounts_len) {
  builder->def                  = ssz_get_def(eth_ssz_verification_type(ETH_SSZ_VERIFY_CALL_PROOF), "accounts");
  bytes_t          key          = {0};
  buffer_t         buf          = {0};
  ssz_builder_t    account      = ssz_builder_for_def(builder->def->def.vector.type);
  const ssz_def_t* code_def     = ssz_get_def(account.def, "code");
  bool             include_code = ctx->flags & C4_PROVER_FLAG_INCLUDE_CODE;

  add_dynamic_byte_list(json_get(values, "accountProof"), &account, "accountProof");
  ssz_add_bytes(&account, "address", address);

  ssz_builder_t code_builder = ssz_builder_for_def(ssz_get_def(code_def, code.type == JSON_TYPE_NOT_FOUND || !include_code ? "code_used" : "code"));
  if (code.type == JSON_TYPE_NOT_FOUND)
    ssz_add_uint8(&code_builder, 0);
  else if (!include_code)
    ssz_add_uint8(&code_builder, 1);
  else
    json_as_bytes(code, &code_builder.fixed);

  ssz_add_builders(&account, "code", code_builder);

  ssz_builder_t storage_list = ssz_builder_for_def(ssz_get_def(account.def, "storageProof"));
  json_t        storage      = json_get(values, "storageProof");
  int           storage_len  = json_len(storage);
  int           idx          = 0;

  json_for_each_value(storage, val) {
    ssz_builder_t storage_key = ssz_builder_for_def(storage_list.def->def.vector.type);
    ssz_add_bytes(&storage_key, "key", json_as_bytes(json_get(val, "key"), &buf));
    add_dynamic_byte_list(json_get(val, "proof"), &storage_key, "proof");
    ssz_add_dynamic_list_builders(&storage_list, storage_len, storage_key);
  }
  ssz_add_builders(&account, "storageProof", storage_list);
  ssz_add_dynamic_list_builders(builder, accounts_len, account);

  buffer_free(&buf);
}

static c4_status_t handle_access_list(prover_ctx_t* ctx, json_t storage, bytes_t account, json_t code, int accounts_len, uint64_t block_number, address_t address, ssz_builder_t* builder) {
  json_t   eth_proof  = {0};
  buffer_t keys       = {0};
  json_t   keys_array = {0};
  if (storage.type == JSON_TYPE_OBJECT) {
    bprintf(&keys, "[");
    bytes_t key = {0};
    json_for_each_property(storage, val, key) {
      if (keys.data.len > 1)
        buffer_add_chars(&keys, ",\"");
      else
        buffer_add_chars(&keys, "\"");
      buffer_append(&keys, key);
      buffer_add_chars(&keys, "\"");
    }
    buffer_add_chars(&keys, "]");
    keys_array = (json_t) {.type = JSON_TYPE_ARRAY, .start = (const char*) keys.data.data, .len = keys.data.len};
  }
  else if (storage.type == JSON_TYPE_NOT_FOUND) {
    bprintf(&keys, "[]");
    keys_array = (json_t) {.type = JSON_TYPE_ARRAY, .start = (const char*) keys.data.data, .len = keys.data.len};
  }
  else
    keys_array = storage;

  TRY_ASYNC_FINAL(eth_get_proof(
                      ctx,
                      (json_t) {.type = JSON_TYPE_STRING, .start = (const char*) account.data - 1, .len = account.len + 2},
                      keys_array,
                      &eth_proof,
                      block_number),
                  buffer_free(&keys));

  if (code.type == JSON_TYPE_INVALID) { // invalid simply meand, we don't know.
    bytes32_t code_hash = {0};
    buffer_t  buf       = stack_buffer(code_hash);
    if (json_get_bytes(eth_proof, "codeHash", &buf).len != 32) THROW_ERROR("Invalid code hash!");

    if (memcmp(code_hash, EMPTY_HASH, 32) == 0)
      code.type = JSON_TYPE_NOT_FOUND;
    else if ((ctx->flags & C4_PROVER_FLAG_INCLUDE_CODE) == 0)
      code.type = JSON_TYPE_BOOLEAN;
    else
      TRY_ASYNC(eth_get_code(ctx, (json_t) {.type = JSON_TYPE_STRING, .start = (const char*) account.data - 1, .len = account.len + 2}, &code, 0));
  }

  add_account(
      ctx,
      builder, eth_proof,
      bytes(address, 20),
      code,
      accounts_len);
  return C4_SUCCESS;
}

c4_status_t c4_get_eth_proofs(prover_ctx_t* ctx, json_t tx, json_t trace, uint64_t block_number, ssz_builder_t* builder, address_t miner) {
  c4_status_t status       = C4_SUCCESS;
  json_t      eth_proof    = {0};
  bytes_t     account      = {0};
  uint8_t     address[20]  = {0};
  int         accounts_len = 0;

  if (trace.type == JSON_TYPE_OBJECT) {
    json_for_each_property(trace, values, account) {
      hex_to_bytes((const char*) account.data, account.len, bytes(address, sizeof(address)));
      if (bytes_all_zero(bytes(address, 20)) || memcmp(address, miner, 20) == 0) continue;
      accounts_len++;
    }
    json_for_each_property(trace, values, account) {
      hex_to_bytes((const char*) account.data, account.len, bytes(address, sizeof(address)));
      if (bytes_all_zero(bytes(address, 20)) || memcmp(address, miner, 20) == 0) continue;
      TRY_ADD_ASYNC(status, handle_access_list(ctx, json_get(values, "storage"), account, json_get(values, "code"), accounts_len, block_number, address, builder));
    }
  }
  else {
    accounts_len = json_len(trace);
    json_for_each_value(trace, values) {
      buffer_t buf  = stack_buffer(address);
      json_t   addr = json_get(values, "address");
      account       = bytes(addr.start + 1, addr.len - 2);
      json_as_bytes(addr, &buf);
      TRY_ADD_ASYNC(status, handle_access_list(ctx, json_get(values, "storageKeys"), account, (json_t) {0}, accounts_len, block_number, address, builder));
    }
  }

  return status;
}

c4_status_t c4_proof_call(prover_ctx_t* ctx) {
  json_t            tx             = json_at(ctx->params, 0);
  json_t            block_number   = json_at(ctx->params, 1);
  beacon_block_t    block          = {0};
  json_t            trace          = {0};
  ssz_builder_t     accounts       = {0};
  blockroot_proof_t historic_proof = {0};
  c4_status_t       status         = C4_SUCCESS;

  // Validate arguments before processing
  CHECK_JSON(ctx->params, "[{to:address,data:bytes,gas?:hexuint,value?:hexuint,gasPrice?:hexuint,from?:address},block]", "Invalid transaction");
  TRY_ASYNC(c4_beacon_get_block_for_eth(ctx, block_number, &block));
  uint64_t target_block = ssz_get_uint64(&block.execution, "blockNumber");
  bytes_t  miner        = ssz_get(&block.execution, "feeRecipient").bytes;
  TRY_ADD_ASYNC(status, eth_debug_trace_call(ctx, tx, &trace, target_block));
  TRY_ADD_ASYNC(status, c4_check_blockroot_proof(ctx, &historic_proof, &block));
  TRY_ASYNC_CATCH(status, c4_free_block_proof(&historic_proof));
  TRY_ASYNC_CATCH(c4_get_eth_proofs(ctx, tx, trace, target_block, &accounts, miner.data), ssz_builder_free(&accounts); c4_free_block_proof(&historic_proof););

  status = create_eth_call_proof(ctx, accounts, &block, block_number, &historic_proof);
  c4_free_block_proof(&historic_proof);
  return status;
}
