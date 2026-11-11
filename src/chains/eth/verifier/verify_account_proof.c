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
#include "crypto.h"
#include "el_header.h"
#include "eth_account.h"
#include "eth_tx.h"
#include "eth_verify.h"
#include "json.h"
#include "patricia.h"
#include "rlp.h"
#include "ssz.h"
#include "sync_committee.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The block tag is always the last argument for every account-bearing RPC
// method (`eth_getBalance`, `eth_getCode`, `eth_getStorageAt`,
// `eth_getTransactionCount`, `eth_getProof`). Using `json_len - 1` keeps this
// in sync with `eth_verify_state_proof` (which reads the same slot for the
// blockhash/blocknumber match check) and avoids a method-name table that would
// drift as new methods are added.
// `eth_verify_state_proof` does not exist anymore; the match check is done by
// `eth_check_requested_block`.
// `check_account_request` ensures the number of arguments, so the last one
// really is the block identifier and not the address or a storage key.
static json_t account_block_tag(verify_ctx_t* ctx) {
  uint32_t n = json_len(ctx->args);
  return n ? json_at(ctx->args, n - 1) : (json_t) {0};
}

/**
 * Compares a storage key of the request with the key of a storage proof.
 *
 * @param ctx verification context (error sink)
 * @param req_key storage key of the request (32 bytes as hex string)
 * @param storage_proof entry of the `storageProof` list
 * @return `true` if the storage proof was built for the requested key
 */
static bool check_storage_key(verify_ctx_t* ctx, json_t req_key, ssz_ob_t storage_proof) {
  bytes32_t key       = {0};
  buffer_t  buf       = stack_buffer(key);
  bytes_t   proof_key = ssz_get(&storage_proof, "key").bytes;
  CHECK_JSON_VERIFY(req_key, "bytes32", "storage key: ");
  if (json_as_bytes(req_key, &buf).len != 32 || proof_key.len != 32 || memcmp(key, proof_key.data, 32) != 0)
    RETURN_VERIFY_ERROR(ctx, "proof does not match the storage key in request");
  return true;
}

/**
 * Checks that the proof can be an answer to the request at all.
 *
 * An account proof only answers the account methods: for every other method the data of
 * the request would be returned without ever being checked. The arguments must be the
 * ones of the method, and the storage proofs must be the ones of the requested keys in
 * the requested order, because the values are taken from them by position. The address
 * is compared in `eth_account_verify_data`, the block in `eth_check_requested_block`.
 *
 * @param ctx verification context holding method and arguments (error sink)
 * @param field account field of the method as returned by `eth_account_get_field`
 * @param storage_proofs the `storageProof` list of the proof
 * @return `true` if method and arguments match the shape of the proof
 */
static bool check_account_request(verify_ctx_t* ctx, eth_account_field_t field, ssz_ob_t storage_proofs) {
  bool     with_keys = field == ETH_ACCOUNT_STORAGE_HASH || field == ETH_ACCOUNT_PROOF;
  uint32_t proofs    = ssz_len(storage_proofs);

  if (field == ETH_ACCOUNT_NONE) RETURN_VERIFY_ERROR(ctx, "method mismatch for account proof");
  if (ctx->args.type != JSON_TYPE_ARRAY || json_len(ctx->args) != (with_keys ? 3 : 2)) RETURN_VERIFY_ERROR(ctx, "invalid arguments for account proof");
  CHECK_JSON_VERIFY(json_at(ctx->args, 0), "address", "address: ");

  if (field == ETH_ACCOUNT_STORAGE_HASH) {
    if (proofs != 1) RETURN_VERIFY_ERROR(ctx, "proof does not match the storage key in request");
    return check_storage_key(ctx, json_at(ctx->args, 1), ssz_at(storage_proofs, 0));
  }

  if (field == ETH_ACCOUNT_PROOF) {
    json_t keys = json_at(ctx->args, 1);
    if (keys.type != JSON_TYPE_ARRAY || json_len(keys) != proofs) RETURN_VERIFY_ERROR(ctx, "proof does not match the storage keys in request");
    for (uint32_t i = 0; i < proofs; i++) {
      if (!check_storage_key(ctx, json_at(keys, i), ssz_at(storage_proofs, i))) return false;
    }
  }
  return true;
}

/**
 * Allocates the zeroed buffer receiving the verified values of an account proof.
 *
 * `eth_getProof` returns one value per storage key, all other methods a single value.
 * The number of storage keys is taken from the proof, so the buffer lives on the heap.
 *
 * @param field account field of the method
 * @param storage_keys_len number of entries in the `storageProof` list
 * @return buffer of 32 bytes per value and at least 32 bytes (as `eth_verify_account_proof_exec` requires), owned by the caller
 */
static bytes_t alloc_values(eth_account_field_t field, uint32_t storage_keys_len) {
  uint32_t count = field == ETH_ACCOUNT_PROOF ? storage_keys_len : 1;
  return bytes(safe_calloc(count ? count : 1, 32), 32 * count);
}

bool verify_hybrid_account_proof(verify_ctx_t* ctx) {
  if (!(ctx->flags & VERIFY_FLAG_HYBRID)) RETURN_VERIFY_ERROR(ctx, "hybrid account proof requires VERIFY_FLAG_HYBRID");
  ssz_ob_t header_data = ssz_get(&ctx->proof, "header_data");
  if (!header_data.bytes.data) RETURN_VERIFY_ERROR(ctx, "missing header_data in hybrid proof");
  bytes32_t           state_root       = {0};
  bytes32_t           expected_root    = {0};
  bytes_t             verified_address = ssz_get(&ctx->proof, "address").bytes;
  eth_account_field_t field            = eth_account_get_field(ctx);
  ssz_ob_t            storage_proofs   = ssz_get(&ctx->proof, "storageProof");

  if (!check_account_request(ctx, field, storage_proofs)) return false;

  ssz_ob_t sr_ob = ssz_get(&header_data, "stateRoot");
  if (sr_ob.bytes.len != 32) RETURN_VERIFY_ERROR(ctx, "missing stateRoot in header_data");
  memcpy(expected_root, sr_ob.bytes.data, 32);

  bytes_t values       = alloc_values(field, ssz_len(storage_proofs));
  bool    proof_ok     = eth_verify_account_proof_exec(ctx, &ctx->proof, state_root, field == ETH_ACCOUNT_PROOF ? ETH_ACCOUNT_STORAGE_HASH : field, values);
  bool    root_matches = proof_ok && memcmp(state_root, expected_root, 32) == 0;
  bool    data_matches = root_matches && eth_account_verify_data(ctx, verified_address.data, field, values);
  safe_free(values.data);
  if (!proof_ok) RETURN_VERIFY_ERROR(ctx, "invalid account proof!");
  if (!root_matches) RETURN_VERIFY_ERROR(ctx, "stateRoot mismatch between account proof and header_data");
  if (!data_matches) RETURN_VERIFY_ERROR(ctx, "invalid account data!");

  // Hybrid proofs always carry the full header_data, so the timestamp is
  // unconditionally available for the freshness gate.
  if (!eth_check_latest_freshness(ctx, eth_json_is_latest(account_block_tag(ctx)), true,
                                  ssz_get_uint64(&header_data, "timestamp"))) return false;

  ctx->success = true;
  return true;
}

bool verify_account_proof(verify_ctx_t* ctx) {
  bytes_t             el_header        = {0};
  bytes32_t           block_hash       = {0};
  bytes32_t           state_root       = {0};
  bytes_t             verified_address = ssz_get(&ctx->proof, "address").bytes;
  eth_account_field_t field            = eth_account_get_field(ctx);
  ssz_ob_t            storage_proofs   = ssz_get(&ctx->proof, "storageProof");

  if (!check_account_request(ctx, field, storage_proofs)) return false;
  if (c4_verify_block(ctx, ssz_get(&ctx->proof, "elProof"), &el_header, block_hash) != C4_SUCCESS) return false;
  if (!eth_check_requested_block(ctx, account_block_tag(ctx), el_header, block_hash)) return false;

  bytes_t values       = alloc_values(field, ssz_len(storage_proofs));
  bool    proof_ok     = eth_verify_account_proof_exec(ctx, &ctx->proof, state_root, field == ETH_ACCOUNT_PROOF ? ETH_ACCOUNT_STORAGE_HASH : field, values);
  bool    root_matches = proof_ok && memcmp(state_root, eth_el_header_get(el_header, EL_STATE_ROOT).data, 32) == 0;
  bool    data_matches = root_matches && eth_account_verify_data(ctx, verified_address.data, field, values);
  safe_free(values.data);
  if (!proof_ok) RETURN_VERIFY_ERROR(ctx, "invalid account proof!");
  if (!root_matches) RETURN_VERIFY_ERROR(ctx, "stateRoot mismatch between account proof and execution header!");
  if (!data_matches) RETURN_VERIFY_ERROR(ctx, "invalid account data!");
  if (!eth_check_latest_freshness(ctx, eth_json_is_latest(account_block_tag(ctx)), true, eth_el_header_get_uint64(el_header, EL_TIMESTAMP))) return false;

  ctx->success = true;
  return true;
}
