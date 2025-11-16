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

bool verify_account_proof(verify_ctx_t* ctx) {
  bytes32_t           state_root       = {0};
  ssz_ob_t            state_proof      = ssz_get(&ctx->proof, "state_proof");
  ssz_ob_t            header           = ssz_get(&state_proof, "header");
  bytes_t             verified_address = ssz_get(&ctx->proof, "address").bytes;
  eth_account_field_t field            = eth_account_get_field(ctx);
  bytes32_t           value            = {0};
  uint32_t            storage_keys_len = ssz_len(ssz_get(&ctx->proof, "storageProof"));
#ifdef _MSC_VER
  bytes_t values = field == ETH_ACCOUNT_PROOF ? bytes(_alloca(32 * storage_keys_len), 32 * storage_keys_len) : bytes(value, 32);
#else
  bytes_t values = field == ETH_ACCOUNT_PROOF ? bytes(alloca(32 * storage_keys_len), 32 * storage_keys_len) : bytes(value, 32);
#endif

  if (!eth_verify_account_proof_exec(ctx, &ctx->proof, state_root, field == ETH_ACCOUNT_PROOF ? ETH_ACCOUNT_STORAGE_HASH : field, values)) RETURN_VERIFY_ERROR(ctx, "invalid account proof!");
  if (!eth_verify_state_proof(ctx, state_proof, state_root)) return false;
  if (c4_verify_header(ctx, header, state_proof) != C4_SUCCESS) return false;
  if (field && !eth_account_verify_data(ctx, verified_address.data, field, values)) RETURN_VERIFY_ERROR(ctx, "invalid account data!");

  ctx->success = true;
  return true;
}