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

#ifdef _MSC_VER
#include <malloc.h>
#endif

// The block tag is always the last argument for every account-bearing RPC
// method (`eth_getBalance`, `eth_getCode`, `eth_getStorageAt`,
// `eth_getTransactionCount`, `eth_getProof`). Using `json_len - 1` keeps this
// in sync with `eth_verify_state_proof` (which reads the same slot for the
// blockhash/blocknumber match check) and avoids a method-name table that would
// drift as new methods are added.
static json_t account_block_tag(verify_ctx_t* ctx) {
  uint32_t n = json_len(ctx->args);
  return n ? json_at(ctx->args, n - 1) : (json_t) {0};
}

bool verify_hybrid_account_proof(verify_ctx_t* ctx) {
  if (!(ctx->flags & VERIFY_FLAG_HYBRID)) RETURN_VERIFY_ERROR(ctx, "hybrid account proof requires VERIFY_FLAG_HYBRID");
  ssz_ob_t header_data = ssz_get(&ctx->proof, "header_data");
  if (!header_data.bytes.data) RETURN_VERIFY_ERROR(ctx, "missing header_data in hybrid proof");
  bytes32_t           state_root       = {0};
  bytes32_t           expected_root    = {0};
  bytes_t             verified_address = ssz_get(&ctx->proof, "address").bytes;
  eth_account_field_t field            = eth_account_get_field(ctx);
  bytes32_t           value            = {0};
  uint32_t            storage_keys_len = ssz_len(ssz_get(&ctx->proof, "storageProof"));
#ifdef _MSC_VER
  bytes_t values = field == ETH_ACCOUNT_PROOF ? bytes(_alloca(32 * storage_keys_len), 32 * storage_keys_len) : bytes(value, 32);
#else
  bytes_t values = field == ETH_ACCOUNT_PROOF ? bytes(alloca(32 * storage_keys_len), 32 * storage_keys_len) : bytes(value, 32);
#endif

  ssz_ob_t sr_ob = ssz_get(&header_data, "stateRoot");
  if (sr_ob.bytes.len != 32) RETURN_VERIFY_ERROR(ctx, "missing stateRoot in header_data");
  memcpy(expected_root, sr_ob.bytes.data, 32);

  if (!eth_verify_account_proof_exec(ctx, &ctx->proof, state_root, field == ETH_ACCOUNT_PROOF ? ETH_ACCOUNT_STORAGE_HASH : field, values)) RETURN_VERIFY_ERROR(ctx, "invalid account proof!");
  if (memcmp(state_root, expected_root, 32) != 0) RETURN_VERIFY_ERROR(ctx, "stateRoot mismatch between account proof and header_data");
  if (field && !eth_account_verify_data(ctx, verified_address.data, field, values)) RETURN_VERIFY_ERROR(ctx, "invalid account data!");

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
  bytes32_t           value            = {0};
  uint32_t            storage_keys_len = ssz_len(ssz_get(&ctx->proof, "storageProof"));
#ifdef _MSC_VER
  bytes_t values = field == ETH_ACCOUNT_PROOF ? bytes(_alloca(32 * storage_keys_len), 32 * storage_keys_len) : bytes(value, 32);
#else
  bytes_t values = field == ETH_ACCOUNT_PROOF ? bytes(alloca(32 * storage_keys_len), 32 * storage_keys_len) : bytes(value, 32);
#endif

  if (c4_verify_block(ctx, ssz_get(&ctx->proof, "block"), &el_header, block_hash) != C4_SUCCESS) return false;
  if (!eth_verify_account_proof_exec(ctx, &ctx->proof, state_root, field == ETH_ACCOUNT_PROOF ? ETH_ACCOUNT_STORAGE_HASH : field, values)) RETURN_VERIFY_ERROR(ctx, "invalid account proof!");
  if (memcmp(state_root, eth_el_header_get(el_header, EL_STATE_ROOT).data, 32) != 0) RETURN_VERIFY_ERROR(ctx, "stateRoot mismatch between account proof and execution header!");
  if (field && !eth_account_verify_data(ctx, verified_address.data, field, values)) RETURN_VERIFY_ERROR(ctx, "invalid account data!");
  if (!eth_check_latest_freshness(ctx, eth_json_is_latest(account_block_tag(ctx)), true, eth_el_header_get_uint64(el_header, EL_TIMESTAMP))) return false;

  ctx->success = true;
  return true;
}