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

#include "arb_verify.h"
#include "arb_batch_poster.h"
#include "beacon_types.h"
#include "chains.h"
#include "common.h"
#include "el_header.h"
#include "eth_account.h"
#include "plugin.h"
#include "sync_committee.h"
#include <string.h>

static c4_status_t accept_batch_poster(verify_ctx_t* ctx, ssz_ob_t sync) {
  address_t inbox    = {0};
  bytes32_t slot     = {0};
  bytes32_t value    = {0};
  bytes32_t hash     = {0};
  bytes_t   header   = NULL_BYTES;
  ssz_ob_t  signer   = ssz_get(&sync, "signer");
  ssz_ob_t  account  = ssz_get(&sync, "account");
  ssz_ob_t  el_proof = ssz_get(&account, "elProof");
  if (!arb_sequencer_inbox(ctx->chain_id, inbox)) THROW_ERROR("no sequencer inbox for this chain");
  if (!ssz_is(el_proof, ETH_SSZ_NAME_CL_PROOF)) THROW_ERROR("batch poster proof must use a clProof");

  TRY_AS_L1(c4_verify_block(ctx, el_proof, &header, hash));
  bytes_t  state_root = eth_el_header_get(header, EL_STATE_ROOT);
  uint64_t number     = eth_el_header_get_uint64(header, EL_BLOCK_NUMBER);
  if (state_root.len != 32 || number == 0) THROW_ERROR("batch poster proof has no L1 header");
  if (!arb_batch_poster_l1_is_current(ctx->chain_id, number))
    THROW_ERROR("batch poster proof is older than the stored one");

  arb_batch_poster_storage_key(signer.bytes.data, slot);
  TRY_ASYNC(eth_verify_storage_slot(&ctx->state, account, inbox, slot, state_root, value));
  if (!bytes_all_zero(bytes(value, 31)) || value[31] != 1)
    THROW_ERROR("batch poster storage value must be 1");

  // The timestamp is the moment this proof was accepted, including a repeat of the
  // same L1 block. The client uses it to decide when the last check is too old.
  c4_set_cached_batch_poster(ctx->chain_id, signer.bytes.data, current_unix_ms(), number);
  return C4_SUCCESS;
}

static c4_status_t apply_batch_poster_sync(verify_ctx_t* ctx) {
  if (!ssz_is(ctx->sync_data, ETH_SSZ_NAME_ARB_SIGNER_SYNC_DATA))
    return C4_SUCCESS;

  ssz_ob_t outer     = ctx->sync_data;
  ctx->sync_data     = ssz_get(&outer, "sync_data");
  c4_status_t status = c4_update_from_sync_data(ctx); // always uses l1!
  if (status == C4_SUCCESS) status = accept_batch_poster(ctx, outer);
  if (status != C4_SUCCESS) { // restore in case of pending
    ctx->sync_data = outer;
    return status;
  }

  // clear the sync data so that we don't try to apply it again
  ctx->sync_data.def = &ssz_none;
  return C4_SUCCESS;
}

method_type_t c4_arb_get_method_type(chain_id_t chain_id, char* method, json_t params, verify_flags_t flags) {
  if (c4_chain_type(chain_id) != C4_CHAIN_TYPE_ARBITRUM) return METHOD_UNDEFINED;
  return c4_eth_classify_method(method, params, flags);
}

const ssz_def_t* c4_arb_get_request_type(chain_type_t chain_type) {
  return chain_type == C4_CHAIN_TYPE_ARBITRUM ? eth_ssz_verification_type(ETH_SSZ_VERIFY_REQUEST) : NULL;
}

void arb_init_rpc_ctx(c4_init_ctx_t* ctx) {
  (void) ctx;
  arb_register_block_proof_verify();
}

bool c4_arb_verify(verify_ctx_t* ctx) {
  arb_register_block_proof_verify();
  if (c4_chain_type(ctx->chain_id) != C4_CHAIN_TYPE_ARBITRUM) return false;
  // Pending and error stay in ctx->state. Restoring the outer sync object keeps
  // the next entry on this path until the L1 update and the account proof finish.
  if (apply_batch_poster_sync(ctx) != C4_SUCCESS) return true;
  return c4_eth_verify_request(ctx);
}
