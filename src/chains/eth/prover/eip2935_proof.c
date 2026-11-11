/*
 * Copyright (c) 2025,2026 corpus.core GmbH
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

#include "beacon_types.h"
#include "bytes.h"
#include "eip2935.h"
#include "eth_req.h"
#include "eth_tools.h"
#include "json.h"
#include "prover.h"
#include "ssz.h"
#include <string.h>

// Length of a quoted address (`"0x` + 40 nibbles + `"`) and of a quoted 32-byte word, each with a terminator.
#define JSON_ADDRESS_SIZE 45
#define JSON_BYTES32_SIZE 69

c4_status_t c4_eth_get_eip2935_proof(prover_ctx_t* ctx, uint64_t block_number, uint64_t newer_block_number, json_t* history_proof, bytes32_t block_hash) {
  char      address[JSON_ADDRESS_SIZE] = {0};
  char      key[JSON_BYTES32_SIZE]     = {0};
  bytes32_t slot                       = {0};
  bytes32_t stored_bytes               = {0};
  buffer_t  value                      = stack_buffer(stored_bytes);

  memset(block_hash, 0, 32);
  eth_eip2935_slot(block_number, slot);
  sbprintf(address, "\"0x%x\"", bytes((uint8_t*) ETH_EIP2935_HISTORY_ADDRESS, ADDRESS_SIZE));
  sbprintf(key, "\"0x%x\"", bytes(slot, 32));

  TRY_ASYNC(eth_get_proof(ctx,
                          (json_t) {.start = address, .len = strlen(address), .type = JSON_TYPE_STRING},
                          (json_t) {.start = key, .len = strlen(key), .type = JSON_TYPE_STRING},
                          history_proof, newer_block_number));

  json_t storage = json_get(*history_proof, "storageProof");
  if (storage.type != JSON_TYPE_ARRAY || json_len(storage) != 1) THROW_ERROR("eth_getProof of the EIP-2935 history contract must return exactly one storage proof");

  // the answer must be about the requested slot (a node may strip the leading zeros of the key)
  bytes32_t returned_key = {0};
  bytes_t   raw_key      = json_as_bytes(json_get(json_at(storage, 0), "key"), &value);
  if (raw_key.len <= 32) memcpy(returned_key + 32 - raw_key.len, raw_key.data, raw_key.len);
  if (!raw_key.data || raw_key.len > 32 || memcmp(returned_key, slot, 32) != 0) THROW_ERROR("eth_getProof of the EIP-2935 history contract returned the proof of another slot");

  // the value is a quantity: no leading zeros, at most 32 bytes
  bytes_t stored = json_as_bytes(json_get(json_at(storage, 0), "value"), &value);
  if (stored.len > 32) THROW_ERROR("eth_getProof of the EIP-2935 history contract returned an invalid storage value");
  memcpy(block_hash + 32 - stored.len, stored.data, stored.len);
  return C4_SUCCESS;
}

void c4_eth_add_eip2935_proof(ssz_builder_t* builder, bytes_t el_header, json_t history_proof, ssz_builder_t newer_block_proof) {
  ssz_builder_t proof   = ssz_builder_for_type(ETH_SSZ_EIP2935_PROOF);
  ssz_builder_t account = {.def = ssz_get_def(proof.def, "account")};

  eth_add_account_proof_fields(&account, history_proof, bytes((uint8_t*) ETH_EIP2935_HISTORY_ADDRESS, ADDRESS_SIZE));
  ssz_add_builders(&account, "elProof", newer_block_proof);

  ssz_add_bytes(&proof, "elHeader", el_header);
  ssz_add_builders(&proof, "account", account);
  ssz_add_builders(builder, "elProof", proof);
}
