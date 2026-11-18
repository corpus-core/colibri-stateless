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

#include "arb_batch_poster.h"
#include "arb_prover.h"
#include "beacon_types.h"
#include "crypto.h"
#include "ssz.h"
#include "version.h"
#include <string.h>

#define ARB_L1_PROOF_TTL 12

// Registered by the host (server build points this at the sequencer-feed cache).
// Stays NULL in builds without a feed, in which case
// `C4_PROVER_FLAG_ARB_BATCH_POSTER` must not be set by anyone.
//
// Threading: see `arb_register_signer_lookup` in arb_prover.h. The pointer is
// written at init/shutdown and read without a lock from worker threads. We do
// not use `_Atomic` because the project does not yet depend on `stdatomic.h`
// (compat with older MSVC toolchains); aligned pointer stores are atomic on
// every supported target, and the hook is only ever flipped between a stable
// function pointer and NULL at shutdown.
static arb_signer_lookup_fn_t _signer_lookup = NULL;

void arb_register_signer_lookup(arb_signer_lookup_fn_t fn) {
  _signer_lookup = fn;
}

// Resolves the signer for this chain through the registered hook. Each call
// re-queries, so if the feed advertises a new signer between prepare/sync the
// prover produces a different storage slot and therefore a different L1
// request id; the previously queued request stays in `ctx->state` unused until
// the context is freed. That is intentional: it keeps the prover stateless
// with respect to the feed.
static c4_status_t resolve_signer(prover_ctx_t* ctx, uint8_t out[20]) {
  // Snapshot the pointer so a racing `arb_register_signer_lookup(NULL)` cannot
  // flip us from a successful null-check into a NULL call. Harmless on today's
  // targets because stores are atomic, but cheap defence-in-depth.
  arb_signer_lookup_fn_t fn = _signer_lookup;
  if (!fn) THROW_ERROR("no Arbitrum signer lookup registered");
  if (!fn(c4_chain_specific_id(ctx->chain_id), out))
    THROW_ERROR("no signed Arbitrum feed message to prove the batch poster from");
  return C4_SUCCESS;
}

static c4_status_t load_l1_account(prover_ctx_t* ctx, ssz_ob_t* response, uint8_t signer[20]) {
  if (!(ctx->flags & C4_PROVER_FLAG_ARB_BATCH_POSTER)) return C4_SUCCESS;
  TRY_ASYNC(resolve_signer(ctx, signer));

  uint8_t inbox[20] = {0};
  uint8_t slot[32]  = {0};
  if (!arb_sequencer_inbox(ctx->chain_id, inbox)) THROW_ERROR("no sequencer inbox for this chain");
  arb_batch_poster_storage_key(signer, slot);

  bytes32_t  id      = {0};
  buffer_t   payload = {0};
  chain_id_t l1      = c4_get_l1_chain_id_for_l2(ctx->chain_id);
  bprintf(&payload, "{\"method\":\"eth_getStorageAt\",\"params\":[\"0x%x\",\"0x%x\",\"latest\"]", bytes(inbox, 20), bytes(slot, 32));
  c4_chain_request_id(id, l1, payload.data);

  data_request_t* req = c4_state_get_data_request_by_id(&ctx->state, id);
  if (!req) {
    // Written here instead of `c4_append_prover_request_props`: that helper would
    // attach this chain's batch poster and could read another client's storage.
    // `last_block_hash` stays absent so the L1 block is a full clProof.
    bprintf(&payload, ",\"version\":%d", c4_current_version_number());
    if (ctx->client_state.data && ctx->client_state.len)
      bprintf(&payload, ",\"c4\":\"0x%x\"", ctx->client_state);
    if (ctx->flags & C4_PROVER_FLAG_ZK_PROOF)
      bprintf(&payload, ",\"zk_proof\":true");
    if (ctx->witness_key.data && ctx->witness_key.len && ctx->witness_key.len % 20 == 0)
      bprintf(&payload, ",\"signers\":\"0x%x\"", ctx->witness_key);
    bprintf(&payload, "}");

    req           = safe_calloc(1, sizeof(data_request_t));
    req->type     = C4_DATA_TYPE_PROVER;
    req->chain_id = l1;
    req->method   = C4_DATA_METHOD_POST;
    req->encoding = C4_DATA_ENCODING_SSZ;
    req->payload  = payload.data;
    req->ttl      = ARB_L1_PROOF_TTL;
    memcpy(req->id, id, 32);
    c4_state_add_request(&ctx->state, req);
    return C4_PENDING;
  }

  buffer_free(&payload);
  if (c4_state_is_pending(req)) return C4_PENDING;
  if (req->error) THROW_ERROR_WITH("L1 prover request for the batch poster failed: %s", req->error);
  if (req->response.len && req->response.data[0] == '{')
    THROW_ERROR_WITH("L1 prover rejected the batch poster request: %r", req->response);

  *response = (ssz_ob_t) {.def = eth_ssz_verification_type(ETH_SSZ_VERIFY_REQUEST), .bytes = req->response};
  if (!req->validated) {
    if (!ssz_is_valid(*response, true, &ctx->state)) THROW_ERROR(" (invalid response of the L1 prover)");
    req->validated = true;
  }
  return C4_SUCCESS;
}

c4_status_t arb_batch_poster_prepare(prover_ctx_t* ctx) {
  ssz_ob_t    ignored    = {0};
  uint8_t     signer[20] = {0};
  c4_status_t status     = load_l1_account(ctx, &ignored, signer);
  // The block fetch continues while the L1 request is in flight.
  if (status == C4_PENDING) return C4_SUCCESS;
  return status;
}

c4_status_t arb_batch_poster_sync(prover_ctx_t* ctx, ssz_builder_t* builder) {
  if (!(ctx->flags & C4_PROVER_FLAG_ARB_BATCH_POSTER)) return C4_SUCCESS;

  ssz_ob_t    response   = {0};
  uint8_t     signer[20] = {0};
  c4_status_t status     = load_l1_account(ctx, &response, signer);
  if (status != C4_SUCCESS) return status;

  ssz_ob_t account = ssz_get(&response, "proof");
  if (!ssz_is_type(&account, eth_ssz_verification_type(ETH_SSZ_VERIFY_ACCOUNT_PROOF)))
    THROW_ERROR("L1 prover did not return an account proof for the batch poster");
  ssz_ob_t el_proof = ssz_get(&account, "elProof");
  if (!el_proof.def || !el_proof.def->name || strcmp(el_proof.def->name, ETH_SSZ_NAME_CL_PROOF) != 0)
    THROW_ERROR("L1 prover did not prove the batch poster with a clProof");

  uint8_t  inbox[20] = {0};
  uint8_t  slot[32]  = {0};
  ssz_ob_t proven    = ssz_get(&account, "address");
  ssz_ob_t stored    = ssz_get(&account, "storageProof");
  if (!arb_sequencer_inbox(ctx->chain_id, inbox) || !proven.bytes.data || proven.bytes.len != 20 ||
      memcmp(proven.bytes.data, inbox, 20) != 0)
    THROW_ERROR("L1 account proof is not the sequencer inbox");
  arb_batch_poster_storage_key(signer, slot);
  if (!stored.def || ssz_len(stored) != 1) THROW_ERROR("batch poster proof must contain one storage entry");
  ssz_ob_t entry = ssz_at(stored, 0);
  ssz_ob_t key   = ssz_get(&entry, "key");
  if (!key.bytes.data || key.bytes.len != 32 || memcmp(key.bytes.data, slot, 32) != 0)
    THROW_ERROR("batch poster storage key does not match the signer");

  ssz_ob_t l1_sync = ssz_get(&response, "sync_data");
  if (l1_sync.def && l1_sync.def->name && strcmp(l1_sync.def->name, ETH_SSZ_NAME_ARB_SIGNER_SYNC_DATA) == 0)
    THROW_ERROR("L1 prover returned a nested batch poster sync");

  ssz_builder_t sync = ssz_builder_for_type(ETH_SSZ_VERIFY_ARB_SIGNER_SYNC);
  ssz_add_bytes(&sync, "signer", bytes(signer, 20));
  ssz_add_ob(&sync, "account", account);
  if (!l1_sync.def || l1_sync.def->type == SSZ_TYPE_NONE) {
    uint8_t none = 0;
    ssz_add_bytes(&sync, "sync_data", bytes(&none, 1));
  }
  else
    ssz_add_ob(&sync, "sync_data", l1_sync);
  *builder = sync;
  return C4_SUCCESS;
}
