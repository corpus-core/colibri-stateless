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

#include "../util/bytes.h"
#include "../util/crypto.h"
#include "../util/ssz.h"
#include "beacon_types.h"
#include "el_header.h"
#include "eth_verify.h"
#include "header_cache.h"
#include "logger.h"
#include "sync_committee.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef BLOCK_HASH_CACHE
#define BLOCKHASH_COUNT 10
static uint8_t  blockhash_cache[BLOCKHASH_COUNT * 32] = {0};
static uint32_t blockhash_cache_index                 = 0;

static bool is_already_validated(bytes32_t blockhash) {
  for (uint32_t i = 0; i < BLOCKHASH_COUNT; i++) {
    if (memcmp(blockhash_cache + 32 * i, blockhash, 32) == 0) return true;
  }
  return false;
}

static void add_to_blockhash_cache(bytes32_t blockhash) {
  memcpy(blockhash_cache + blockhash_cache_index * 32, blockhash, 32);
  blockhash_cache_index = (blockhash_cache_index + 1) % BLOCKHASH_COUNT;
}
#endif

// combining the root with a domain to ensure uniqueness of the signing message
static const ssz_def_t SIGNING_DATA[] = {
    SSZ_BYTES32("root"),    // the hashed root of the data to sign
    SSZ_BYTES32("domain")}; // the domain of the data to sign

static const ssz_def_t SIGNING_DATA_CONTAINER = SSZ_CONTAINER("SigningData", SIGNING_DATA);

// the fork data is used to create the domain
static const ssz_def_t FORK_DATA[] = {
    SSZ_BYTE_VECTOR("version", 4), // the version of the fork
    SSZ_BYTES32("state")};         // the state of the Genisis Block

static const ssz_def_t FORK_DATA_CONTAINER = SSZ_CONTAINER("ForkDate", FORK_DATA);

bool eth_calculate_domain(chain_id_t chain_id, uint64_t slot, bytes32_t domain) {
  uint8_t             buffer[36]  = {0};
  bytes32_t           base_digest = {0};
  const chain_spec_t* chain       = c4_eth_get_chain_spec(chain_id);

  // compute fork_data root hash to the seconf 32 bytes of bffer
  if (!chain) return false;
  chain->fork_version_func(chain_id, c4_chain_fork_id(chain_id, epoch_for_slot(slot - 1, chain)), buffer); // write fork_version as first 4 bytes into the buffer
  if (!c4_chain_genesis_validators_root(chain_id, buffer + 4)) false;                                      // add the genesis_validator_root as the the other 32 bytes to the buffer.

  ssz_hash_tree_root(ssz_ob(FORK_DATA_CONTAINER, bytes(buffer, 36)), base_digest); // calculate base_digest

  // build domain by replacing the first 4 bytes with the sync committee domain which creates the domain-data in the 2nd 32 bytes of buffer
  memcpy(domain, "\x07\x00\x00\x00", 4); // Domain-Type SYNC_COMMITTEE                // Domain-Type
  memcpy(domain + 4, base_digest, 28);   // last 28 bytes of the base_digest
  return true;
}

static bool calculate_signing_message(verify_ctx_t* ctx, uint64_t slot, bytes32_t blockhash, bytes32_t signing_message) {
  uint8_t buffer[64] = {0};
  memcpy(buffer, blockhash, 32);
  if (!eth_calculate_domain(ctx->chain_id, slot, buffer + 32)) RETURN_VERIFY_ERROR(ctx, "unsupported chain!");
  ssz_hash_tree_root(ssz_ob(SIGNING_DATA_CONTAINER, bytes(buffer, 64)), signing_message);
  return true;
}

static c4_status_t c4_verify_headers_proof(verify_ctx_t* ctx, ssz_ob_t header, ssz_ob_t sync_committee_bits, ssz_ob_t sync_committee_signature, ssz_ob_t header_proof) {
  ssz_ob_t  headers           = ssz_get(&header_proof, "headers"); // the intermediate headers between the current block and the block with the signature
  ssz_ob_t  signed_header     = ssz_get(&header_proof, "header");  // the block matching the signature
  uint32_t  header_count      = ssz_len(headers);                  // the number of intermediate headers
  bytes32_t last_block_root   = {0};                               // last block root calculated from the current header
  uint8_t   header_bytes[112] = {0};                               // temp blockheader while calculating
  ssz_ob_t  header_ob         = {.bytes = bytes(header_bytes, sizeof(header_bytes)), .def = eth_ssz_type_for_denep(ETH_SSZ_BEACON_BLOCK_HEADER, C4_CHAIN_MAINNET)};
  ssz_hash_tree_root(header, last_block_root);

  for (size_t i = 0; i < header_count; i++) {
    ssz_ob_t h = ssz_at(headers, i);                  // we copy into the ssz header structure because the headers are only 80 bytes since the do not hold the parentRoot.
    memcpy(header_bytes, h.bytes.data, 16);           // slot and proposerIndex
    memcpy(header_bytes + 16, last_block_root, 32);   // parent root
    memcpy(header_bytes + 48, h.bytes.data + 16, 64); // state root and body root
    ssz_hash_tree_root(header_ob, last_block_root);   // compute the root of the header
  }

  if (memcmp(last_block_root, ssz_get(&signed_header, "parentRoot").bytes.data, 32))
    THROW_ERROR("invalid parent root for header proof!");

  return c4_verify_blockroot_signature(ctx, &signed_header, &sync_committee_bits, &sync_committee_signature, 0, NULL);
}

static c4_status_t c4_verify_historic_proof(verify_ctx_t* ctx, ssz_ob_t header, ssz_ob_t sync_committee_bits, ssz_ob_t sync_committee_signature, ssz_ob_t historic_proof) {
  bytes32_t block_root    = {0};
  bytes32_t state_root    = {0};
  ssz_ob_t  signed_header = ssz_get(&historic_proof, "header");

  // Derive the expected combined gindex locally. Never trust the value carried
  // in the proof: without this cross-check an attacker could supply any Merkle
  // path that terminates at some other `bytes32` position inside the BeaconState
  // tree (e.g. `block_roots[i]`, `state_roots[i]`,
  // `latest_block_header.parent_root`, ...) and have the verifier accept an
  // arbitrary `block_root` as a "historic block". The verifier below hashes
  // against `expected_gindex`, so both the position and the leaf are bound.
  uint64_t block_slot    = ssz_get_uint64(&header, "slot");
  uint64_t state_slot    = ssz_get_uint64(&signed_header, "slot");
  gindex_t expected_gidx = c4_historic_block_gindex(ctx->chain_id, block_slot, state_slot);
  uint64_t proof_gidx    = ssz_get_uint64(&historic_proof, "gindex");
  if (expected_gidx == 0)
    THROW_ERROR("invalid slot for historic proof gindex!");
  if (proof_gidx != (uint64_t) expected_gidx)
    THROW_ERROR("invalid gindex for historic proof!");

  ssz_hash_tree_root(header, block_root);
  ssz_verify_single_merkle_proof(ssz_get(&historic_proof, "proof").bytes, block_root, expected_gidx, state_root);

  if (memcmp(state_root, ssz_get(&signed_header, "stateRoot").bytes.data, 32))
    THROW_ERROR("invalid state root for historic proof!");

  return c4_verify_blockroot_signature(ctx, &signed_header, &sync_committee_bits, &sync_committee_signature, 0, NULL);
}

c4_status_t c4_verify_header(verify_ctx_t* ctx, ssz_ob_t header, ssz_ob_t block_proof) {
  ssz_ob_t header_proof             = ssz_get(&block_proof, "headerProof");
  ssz_ob_t sync_committee_bits      = ssz_get(&header_proof, "sync_committee_bits");
  ssz_ob_t sync_committee_signature = ssz_get(&header_proof, "sync_committee_signature");

  if (strcmp(header_proof.def->name, "signature_proof") == 0) // direct proof - the signature matches the current header
    return c4_verify_blockroot_signature(ctx, &header, &sync_committee_bits, &sync_committee_signature, 0, NULL);

  if (strcmp(header_proof.def->name, "headerProof") == 0) // header proof - the signature matches the signed header in the header_proof
    return c4_verify_headers_proof(ctx, header, sync_committee_bits, sync_committee_signature, header_proof);

  // historic proof
  return c4_verify_historic_proof(ctx, header, sync_committee_bits, sync_committee_signature, header_proof);
}

c4_status_t c4_verify_blockroot_signature(verify_ctx_t* ctx, ssz_ob_t* header, ssz_ob_t* sync_committee_bits, ssz_ob_t* sync_committee_signature, uint64_t slot, bytes32_t pubkey_hash) {
  bytes32_t            root       = {0};
  c4_sync_validators_t sync_state = {0};
  bool                 valid      = false;
  const chain_spec_t*  spec       = c4_eth_get_chain_spec(ctx->chain_id);

  if (slot == 0) slot = ssz_get_uint64(header, "slot") + 1;
  if (slot == 0) THROW_ERROR("slot is missing in beacon header!");
  if (!spec) THROW_ERROR("unsupported chain id!");

  uint32_t period = slot >> (spec->slots_per_epoch_bits + spec->epochs_per_period_bits);

  // compute blockhash
  ssz_hash_tree_root(*header, root);

  // compute signing message and store it in root again
  calculate_signing_message(ctx, slot, root, root);

#ifdef BLOCK_HASH_CACHE
  valid = is_already_validated(root);
#endif

  log_debug("get validatore for period %d", period);
  // get the validators and make sure we have the right ones for the requested period
  TRY_ASYNC(c4_get_validators(ctx, period, &sync_state, pubkey_hash));
  // verify the signature
  log_debug("verify bls signature");
  valid = valid || blst_verify(root, sync_committee_signature->bytes.data, sync_state.validators.data, 512, sync_committee_bits->bytes, sync_state.deserialized);
  log_debug("bls signature verified: %d", valid);
  // Edge case: Period transition without immediate finality
  // If the signature is invalid, try with the previous period's validators
  // This can happen when finality is delayed at the start of a new period
  // and the old sync committee keys are still valid
  if (!valid && period > 0) {
    log_debug("try to get validators from previous period %d", period - 1);
#ifndef C4_STATIC_MEMORY
    safe_free(sync_state.validators.data);
#endif
    // Try to get validators from previous period
    TRY_ASYNC(c4_get_validators(ctx, period - 1, &sync_state, NULL));

    // Verify again with previous period's validators
    valid = blst_verify(root, sync_committee_signature->bytes.data, sync_state.validators.data, 512, sync_committee_bits->bytes, sync_state.deserialized);
  }

#ifndef C4_STATIC_MEMORY
  safe_free(sync_state.validators.data);
#endif

  if (!valid)
    THROW_ERROR("invalid blockhash signature!");
#ifdef BLOCK_HASH_CACHE
  add_to_blockhash_cache(root);
#endif

  return C4_SUCCESS;
}

// Resolves the blockHash union variant: the proof references the block only by hash,
// which means the prover assumed the verifier has already verified and cached this
// header (hybrid mode or a client-provided last_block_hash).
static c4_status_t verify_block_by_blockhash(verify_ctx_t* ctx, ssz_ob_t block, bytes_t* el_header, bytes32_t block_hash) {
  if (block.bytes.len != 32) THROW_ERROR("invalid block hash length!");

  // the header may already be attached to this ctx as a cache snapshot
  // (re-execution after C4_PENDING or multiple proofs referencing the same block)
  data_request_t* snapshot = c4_state_get_data_request_by_id(&ctx->state, block.bytes.data);
  if (snapshot && snapshot->type == C4_DATA_TYPE_CACHE && snapshot->response.data) {
    *el_header = snapshot->response;
    memcpy(block_hash, block.bytes.data, 32);
    return C4_SUCCESS;
  }

#ifdef EL_HEADER_CACHE
  bytes_t cached = c4_header_cache_get_el_header(ctx->chain_id, block.bytes.data, NULL);
  if (cached.data) {
    // hand the copy over to the state as a C4_DATA_TYPE_CACHE snapshot: it stays valid
    // for the lifetime of the verify_ctx and is freed automatically by c4_state_free().
    snapshot           = safe_calloc(1, sizeof(data_request_t));
    snapshot->chain_id = ctx->chain_id;
    snapshot->type     = C4_DATA_TYPE_CACHE;
    snapshot->encoding = C4_DATA_ENCODING_SSZ;
    snapshot->response = cached;
    memcpy(snapshot->id, block.bytes.data, 32);
    c4_state_add_request(&ctx->state, snapshot);

    *el_header = cached;
    memcpy(block_hash, block.bytes.data, 32);
    return C4_SUCCESS;
  }
#endif
  // TODO(cache-miss fallback): instead of failing, a remote-prover verifier could issue
  // an eth_getBlockHeader data request here and verify the full block proof (handles the
  // rare eviction race between advertising last_block_hash and receiving the response).
  THROW_ERROR("referenced block header not found in the verifier cache!");
}

// Verifies the clProof union variant: checks the RLP header against the beacon block
// (merkle proof to the body root) and verifies the CL header signature chain.
static c4_status_t verify_block_by_blockproof(verify_ctx_t* ctx, ssz_ob_t block, bytes_t* el_header, bytes32_t block_hash) {
  bytes_t   raw_header = ssz_get(&block, "elHeader").bytes;
  bytes32_t body_root  = {0};
  gindex_t  gindex     = ssz_get_uint64(&block, "gindex");
  ssz_ob_t  cl_header  = ssz_get(&block, "clHeader");
  keccak(raw_header, block_hash);
  ssz_verify_single_merkle_proof(
      ssz_get(&block, "blockhashBranch").bytes, block_hash,
      gindex,
      body_root);

  if (memcmp(body_root, ssz_get(&cl_header, "bodyRoot").bytes.data, 32))
    THROW_ERROR("invalid body root for cl proof!");

  // Pin the branch target to the fork-specific EL block-hash leaf inside
  // `BeaconBlockBody`. Without this cross-check a crafted proof could point at
  // any other bytes32 in the body (e.g. `graffiti`, `execution_payload.extra_data`)
  // and pass off arbitrary 32 bytes as the block hash. See `beacon_types.h` for
  // the exact per-fork semantics (Deneb..Fulu: `execution_payload.block_hash`,
  // Gloas: `signed_execution_payload_bid.message.parent_block_hash`).
  gindex_t expected_gindex = c4_execution_block_hash_gindex(ctx->chain_id, ssz_get_uint64(&cl_header, "slot"));
  if (expected_gindex == 0)
    THROW_ERROR("unsupported fork for cl proof gindex!");
  if (gindex != expected_gindex)
    THROW_ERROR("invalid gindex for cl proof!");

  TRY_ASYNC(c4_verify_header(ctx, cl_header, block));

#ifdef EL_HEADER_CACHE
  // the header is now fully verified: cache it so follow-up proofs can reference
  // this block by hash only (blockHash variant of ETH_BLOCK_PROOF_UNION).
  // A block number of 0 indicates an unparsable header field, do not key the cache on it.
  uint64_t block_number = eth_el_header_get_uint64(raw_header, EL_BLOCK_NUMBER);
  if (block_number) c4_header_cache_put(ctx->chain_id, block_number, block_hash, raw_header, NULL);
#endif
  *el_header = raw_header; // borrowed from the proof, valid for the lifetime of the ctx
  return C4_SUCCESS;
}

#ifndef C4_BLOCK_PROOF_HOOKS
#define C4_BLOCK_PROOF_HOOKS 16
#endif
static c4_verify_block_extra_fn verify_block_hooks[C4_BLOCK_PROOF_HOOKS];

void c4_register_block_proof_verify(chain_type_t chain_type, c4_verify_block_extra_fn fn) {
  if ((unsigned) chain_type >= C4_BLOCK_PROOF_HOOKS) return;
  verify_block_hooks[chain_type] = fn;
}

c4_status_t c4_verify_block(verify_ctx_t* ctx, ssz_ob_t block, bytes_t* el_header, bytes32_t block_hash) {
  if (!block.def) THROW_ERROR("invalid block type!");
  if (strcmp(block.def->name, "blockHash") == 0)
    return verify_block_by_blockhash(ctx, block, el_header, block_hash);

  if (strcmp(block.def->name, "clProof") == 0)
    return verify_block_by_blockproof(ctx, block, el_header, block_hash);

  unsigned type = (unsigned) c4_chain_type(ctx->chain_id);
  if (type < C4_BLOCK_PROOF_HOOKS && verify_block_hooks[type])
    return verify_block_hooks[type](ctx, block, el_header, block_hash);

  THROW_ERROR("invalid block type!");
}
