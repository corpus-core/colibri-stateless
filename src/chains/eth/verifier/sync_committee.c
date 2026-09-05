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

#include "sync_committee.h" // Includes c4_process_update_fn typedef
#include "beacon_types.h"
#include "crypto.h"
#include "eth_verify.h"
#include "json.h"
#include "lcu_wire.h"
#include "logger.h"
#include "plugin.h"
#include "ssz.h"
#include "version.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#ifdef ETH_ZKPROOF
#include "zk_verifier.h"
#endif

// Fork-specific BeaconState gindices have moved to `beacon_types.c` so all
// fork-aware selections live in one place. See:
//   `c4_current_sync_committee_gindex`, `c4_next_sync_committee_gindex`,
//   `c4_finalized_root_gindex`.

void c4_eth_eip191_digest_32(const bytes32_t message, bytes32_t out_digest) {
  static const char prefix[]                       = "\x19"
                                                     "Ethereum Signed Message:\n32";
  uint8_t           buf[(sizeof(prefix) - 1) + 32] = {0};
  memcpy(buf, prefix, sizeof(prefix) - 1);
  memcpy(buf + (sizeof(prefix) - 1), message, 32);
  keccak(bytes(buf, sizeof(buf)), out_digest);
}

// True if `ob` is the `checkpoint_proof` variant of either ETH_HEADER_PROOFS_UNION
// (ZKSyncData.checkpoint) or C4_ETH_SYNCDATA_BOOTSTRAP_UNION (LCSyncData.bootstrap).
// Single source of truth for the union-variant discrimination so a future rename of
// the SSZ field cannot silently bypass the cross-check at one of the call sites.
//
// Defined outside the `USE_CHECKPOINTZ` gate because `update_from_lc_sync_data` calls
// it unconditionally (to disambiguate the `bootstrap` union variant from a full
// LightClientBootstrap that would still be handled when USE_CHECKPOINTZ is OFF).
static inline bool is_checkpoint_proof_variant(ssz_ob_t ob) {
  return ob.def &&
         ob.def->type == SSZ_TYPE_CONTAINER &&
         strcmp(ob.def->name, "CheckpointProof") == 0;
}

#ifdef USE_CHECKPOINTZ
// SSZ ByteVector[48] -> two 32-byte chunks: chunk0 = bytes[0..32], chunk1 = bytes[32..48] || zero[16]
#define AGGREGATE_PUBKEY_FIRST_CHUNK 32
#define AGGREGATE_PUBKEY_TAIL_BYTES  16 // = 48 - 32
#define BEACON_BLOCK_HEADER_SIZE     112

INTERNAL c4_status_t c4_verify_checkpoint_proof(verify_ctx_t* ctx, ssz_ob_t checkpoint_proof, bytes32_t pubkeys_root) {
  // Defense-in-depth: surface malformed inputs as explicit errors before invoking the
  // `void`-returning `ssz_verify_single_merkle_proof`. All structural checks run *before*
  // the `ssz_get_uint64`/`ssz_get` field reads so a misshaped container cannot silently
  // produce a zero `slot` that turns into a Genesis-block lookup later.
  if (!checkpoint_proof.def || checkpoint_proof.def->type != SSZ_TYPE_CONTAINER)
    RETURN_VERIFY_ERROR_STATUS(ctx, "checkpoint_proof: missing or wrong SSZ type");

  ssz_ob_t header    = ssz_get(&checkpoint_proof, "header");
  ssz_ob_t aggregate = ssz_get(&checkpoint_proof, "aggregate_pubkey");
  ssz_ob_t proof_ob  = ssz_get(&checkpoint_proof, "proof");

  // BeaconBlockHeader is exactly 112 bytes (slot+proposer+parent+state+body = 8+8+32+32+32)
  // across all forks; reject anything else before any field read.
  if (header.bytes.len != BEACON_BLOCK_HEADER_SIZE || aggregate.bytes.len != 48)
    RETURN_VERIFY_ERROR_STATUS(ctx, "checkpoint_proof: malformed header/aggregate_pubkey");
  if (proof_ob.bytes.len == 0 || (proof_ob.bytes.len % 32) != 0)
    RETURN_VERIFY_ERROR_STATUS(ctx, "checkpoint_proof: malformed merkle proof");

  ssz_ob_t state_root = ssz_get(&header, "stateRoot");
  uint64_t slot       = ssz_get_uint64(&header, "slot");
  if (state_root.bytes.len != 32)
    RETURN_VERIFY_ERROR_STATUS(ctx, "checkpoint_proof: malformed state_root");
  // slot == 0 would address Genesis, which has neither a currentSyncCommittee nor a useful
  // checkpointz route; reject explicitly so a struct-truncation cannot reach the network.
  if (slot == 0)
    RETURN_VERIFY_ERROR_STATUS(ctx, "checkpoint_proof: slot is zero");

  uint64_t gindex = c4_current_sync_committee_gindex(ctx->chain_id, slot);
  if (gindex == 0)
    RETURN_VERIFY_ERROR_STATUS(ctx, "checkpoint_proof: unknown fork gindex");

  // 1. hash_tree_root(aggregate_pubkey) -- ByteVector[48] pads to 64 bytes (2 chunks).
  bytes32_t aggregate_root = {0};
  uint8_t   chunk1[32]     = {0};
  memcpy(chunk1, aggregate.bytes.data + AGGREGATE_PUBKEY_FIRST_CHUNK, AGGREGATE_PUBKEY_TAIL_BYTES);
  sha256_merkle(bytes_slice(aggregate.bytes, 0, AGGREGATE_PUBKEY_FIRST_CHUNK), bytes(chunk1, 32), aggregate_root);

  // 2. sync_committee_root = SHA256(pubkeys_root || aggregate_root) -- SyncCommittee has
  //    exactly two fields, so the container root is a single Merkle node.
  bytes32_t sync_committee_root = {0};
  sha256_merkle(bytes(pubkeys_root, 32), bytes(aggregate_root, 32), sync_committee_root);

  // 3. Walk the currentSyncCommittee branch up to the anchor header's state_root.
  bytes32_t computed_state_root = {0};
  ssz_verify_single_merkle_proof(proof_ob.bytes, sync_committee_root, gindex, computed_state_root);
  if (memcmp(computed_state_root, state_root.bytes.data, 32) != 0)
    RETURN_VERIFY_ERROR_STATUS(ctx, "checkpoint_proof: merkle proof does not match header.stateRoot");

  // 4. Anchor the header itself against the canonical chain via checkpointz. Wrap ERROR
  //    in RETURN_VERIFY_ERROR_STATUS so callers that inspect `ctx->success` see a uniform
  //    signal regardless of which step failed.
  bytes32_t header_root = {0};
  ssz_hash_tree_root(header, header_root);
  c4_status_t anchor_status = c4_verify_checkpointz_root(ctx, slot, header_root);
  if (anchor_status == C4_ERROR)
    RETURN_VERIFY_ERROR_STATUS(ctx, "checkpoint_proof: checkpointz anchor mismatch");
  return anchor_status; // SUCCESS or PENDING
}

// If `update` is the LCU whose `nextSyncCommittee.pubkeys` are the keys for
// `target_period`, hash those pubkeys into `out_root` and return true. Used by
// both the LCSyncData pre-scan and the verifier-driven WSP cross-check so they
// agree on the exact `attested_period` mapping.
static bool try_extract_lcu_pubkeys_root(ssz_ob_t update, const chain_spec_t* spec, uint32_t target_period, bytes32_t out_root) {
  ssz_ob_t attested        = ssz_get(&update, "attestedHeader");
  ssz_ob_t attested_beacon = ssz_get(&attested, "beacon");
  uint64_t attested_slot   = ssz_get_uint64(&attested_beacon, "slot");
  if ((uint32_t) period_for_slot(attested_slot, spec) != target_period) return false;
  ssz_ob_t next_sync    = ssz_get(&update, "nextSyncCommittee");
  ssz_ob_t next_pubkeys = ssz_get(&next_sync, "pubkeys");
  ssz_hash_tree_root(next_pubkeys, out_root);
  return true;
}
#endif // USE_CHECKPOINTZ

/**
 * Extract the `finalizedHeader.beacon` token from a light client update. Centralized so the
 * apply path and the WSP pre-scan agree on the same SSZ accessor sequence.
 */
static ssz_ob_t lcu_finalized_beacon(ssz_ob_t* update) {
  ssz_ob_t finalized = ssz_get(update, "finalizedHeader");
  return ssz_get(&finalized, "beacon");
}

static bool update_light_client_update(verify_ctx_t* ctx, ssz_ob_t* update) {

  bytes32_t sync_root             = {0};
  bytes32_t merkle_root           = {0};
  bytes32_t attested_blockhash    = {0};
  bytes32_t finalized_blockhash   = {0};
  bytes32_t finalized_header_root = {0};
  bytes32_t previous_pubkeys_hash = {0};
  // Extract components (no need for ssz_is_error checks after validation in c4_handle_client_updates)
  ssz_ob_t attested            = ssz_get(update, "attestedHeader");
  ssz_ob_t attested_header     = ssz_get(&attested, "beacon");
  ssz_ob_t finalized_header    = lcu_finalized_beacon(update);
  ssz_ob_t finality_branch     = ssz_get(update, "finalityBranch");
  ssz_ob_t sync_aggregate      = ssz_get(update, "syncAggregate");
  ssz_ob_t signature           = ssz_get(&sync_aggregate, "syncCommitteeSignature");
  ssz_ob_t sync_bits           = ssz_get(&sync_aggregate, "syncCommitteeBits");
  ssz_ob_t next_sync_branch    = ssz_get(update, "nextSyncCommitteeBranch");
  ssz_ob_t sync_committee      = ssz_get(update, "nextSyncCommittee");
  ssz_ob_t attested_state_root = ssz_get(&attested_header, "stateRoot");
  uint64_t attested_slot       = ssz_get_uint64(&attested_header, "slot");

  // calculate the attested blockhash
  ssz_hash_tree_root(attested_header, attested_blockhash);

  // verify the signature of the old sync committee against the attested header
  if (c4_verify_blockroot_signature(ctx, &attested_header, &sync_bits, &signature, attested_slot, previous_pubkeys_hash) != C4_SUCCESS)
    return false;

  // verify nextSyncCommittee merkle proof against attested state root
  ssz_hash_tree_root(sync_committee, sync_root);
  ssz_verify_single_merkle_proof(next_sync_branch.bytes, sync_root, c4_next_sync_committee_gindex(ctx->chain_id, attested_slot), merkle_root);
  if (memcmp(merkle_root, attested_state_root.bytes.data, 32))
    RETURN_VERIFY_ERROR(ctx, "invalid merkle root for next sync committee!");

  // verify finalizedHeader merkle proof against attested state root
  ssz_hash_tree_root(finalized_header, finalized_header_root);
  ssz_verify_single_merkle_proof(finality_branch.bytes, finalized_header_root, c4_finalized_root_gindex(ctx->chain_id, attested_slot), merkle_root);
  if (memcmp(merkle_root, attested_state_root.bytes.data, 32))
    RETURN_VERIFY_ERROR(ctx, "invalid merkle root for finalized header!");

  // calculate finalized blockhash and store it with the next sync committee
  // +1 because nextSyncCommittee is for the next period
  const chain_spec_t* spec   = c4_eth_get_chain_spec(ctx->chain_id);
  uint32_t            period = (attested_slot >> (spec->slots_per_epoch_bits + spec->epochs_per_period_bits)) + 1;
  return c4_set_sync_period(period, ssz_get(&sync_committee, "pubkeys").bytes, ctx->chain_id, previous_pubkeys_hash);
}
static bool verify_signatures(verify_ctx_t* ctx, ssz_ob_t checkpoint_ob, ssz_ob_t attested_header, ssz_ob_t signatures) {
  if (!checkpoint_ob.def || strcmp(checkpoint_ob.def->name, "headerProof"))
    RETURN_VERIFY_ERROR(ctx, "invalid checkpoint, must be a header_proof!");
  ssz_ob_t  signed_header = ssz_get(&checkpoint_ob, "header");
  bytes32_t checkpoint    = {0};
  if (memcmp(attested_header.bytes.data, signed_header.bytes.data, 112)) {
    ssz_ob_t  headers           = ssz_get(&checkpoint_ob, "headers"); // the intermediate headers between the current block and the block with the signature
    uint32_t  header_count      = ssz_len(headers);                   // the number of intermediate headers
    bytes32_t last_block_root   = {0};                                // last block root calculated from the current header
    uint8_t   header_bytes[112] = {0};                                // temp blockheader while calculating
    ssz_ob_t  header_ob         = {.bytes = bytes(header_bytes, 112), .def = attested_header.def};
    ssz_hash_tree_root(attested_header, last_block_root);

    for (size_t i = 0; i < header_count; i++) {
      ssz_ob_t h = ssz_at(headers, i);                  // we copy into the ssz header structure because the headers are only 80 bytes since the do not hold the parentRoot.
      memcpy(header_bytes, h.bytes.data, 16);           // slot and proposerIndex
      memcpy(header_bytes + 16, last_block_root, 32);   // parent root
      memcpy(header_bytes + 48, h.bytes.data + 16, 64); // state root and body root
      ssz_hash_tree_root(header_ob, last_block_root);   // compute the root of the header
    }

    if (memcmp(last_block_root, ssz_get(&signed_header, "parentRoot").bytes.data, 32))
      RETURN_VERIFY_ERROR(ctx, "invalid parent root in zkproof for header proof!");
    log_debug("verified all %d headers", header_count);
  }

  if (signatures.def->type != SSZ_TYPE_LIST) RETURN_VERIFY_ERROR(ctx, "invalid signatures!");
  ssz_hash_tree_root(signed_header, checkpoint);
  uint32_t signatures_len = ssz_len(signatures);
  if (signatures_len == 0) return ctx->witness_keys.len == 0;
  if (signatures_len > 16) RETURN_VERIFY_ERROR(ctx, "invalid number of signatures!");
  uint32_t witness_keys_found = 0;
  for (uint32_t i = 0; i < signatures_len; i++) {
    uint8_t   pub_keys[64] = {0};
    address_t address      = {0};
    bytes32_t digest       = {0};
    log_debug("verifiy %d of %d signatures", i, signatures_len);

    c4_eth_eip191_digest_32(checkpoint, digest);
    if (!secp256k1_recover(digest, ssz_at(signatures, i).bytes, pub_keys))
      RETURN_VERIFY_ERROR(ctx, "invalid signature!");
    keccak(bytes(pub_keys, 64), pub_keys);
    memcpy(address, pub_keys + 12, 20);
    if (bytes_all_zero(bytes(address, 20)))
      RETURN_VERIFY_ERROR(ctx, "invalid signature!");
    for (int j = 0, i = 0; j < ctx->witness_keys.len; j += 20, i++) {
      if (memcmp(address, ctx->witness_keys.data + j, 20) == 0) {
        witness_keys_found |= 1 << i;
        break;
      }
    }
  }
  if (witness_keys_found != (1 << ctx->witness_keys.len / 20) - 1) RETURN_VERIFY_ERROR(ctx, "some witness keys are missing!");
  return true;
}
/**
 * Determine the highest cached sync committee period for the given chain.
 * Returns 0 if no periods are stored.
 */
static uint32_t cached_highest_period(chain_id_t chain_id) {
  c4_chain_state_t chain_state = c4_get_chain_state(chain_id);
  if (chain_state.status != C4_STATE_SYNC_PERIODS) return 0;
  uint32_t highest = 0;
  for (int i = 0; i < MAX_SYNC_PERIODS && chain_state.data.periods[i] != 0; i++)
    if (chain_state.data.periods[i] > highest) highest = chain_state.data.periods[i];
  return highest;
}

/**
 * Check whether the gap between `highest_known_period` and `target_period` exceeds the
 * Weak Subjectivity Period for the given chain. Used to decide whether the prover-supplied
 * sync data requires a checkpointz anchor.
 */
static bool wsp_exceeded(const chain_spec_t* spec, uint32_t highest_known_period, uint32_t target_period) {
  if (!spec || target_period <= highest_known_period) return false;
  uint64_t epoch_diff = ((uint64_t) (target_period - highest_known_period)) << spec->epochs_per_period_bits;
  return epoch_diff > spec->weak_subjectivity_epochs;
}

static c4_status_t update_from_lc_sync_data(verify_ctx_t* ctx) {
  ssz_ob_t            bootstrap             = ssz_get(&ctx->sync_data, "bootstrap");
  ssz_ob_t            updates               = ssz_get(&ctx->sync_data, "update");
  const chain_spec_t* spec                  = c4_eth_get_chain_spec(ctx->chain_id);
  bool                have_checkpoint_proof = is_checkpoint_proof_variant(bootstrap);

  if (bootstrap.def && bootstrap.def->type == SSZ_TYPE_CONTAINER && !have_checkpoint_proof) {
    // Full LightClientBootstrap from a trusted checkpoint -- that checkpoint is itself
    // the trust anchor, so no WSP round-trip is required for the bootstrap step.
    c4_chain_state_t chain_state = c4_get_chain_state(ctx->chain_id);
    if (chain_state.status == C4_STATE_SYNC_EMPTY) RETURN_VERIFY_ERROR_STATUS(ctx, "bootstrap data found, but no checkpoint set!");
    if (chain_state.status == C4_STATE_SYNC_CHECKPOINT && c4_handle_bootstrap(ctx, bootstrap.bytes, chain_state.data.checkpoint) != C4_SUCCESS) return C4_ERROR;
  }

  uint32_t updates_len = ssz_len(updates);

#ifdef USE_CHECKPOINTZ
  // CheckpointProof pre-scan: run the bootstrap-pubkeys cross-check *before* the apply
  // loop persists any new sync committee period via `update_light_client_update` ->
  // `c4_set_sync_period`. The SSZ-level reads below are safe at this point because no
  // BLS trust is required to extract `attestedHeader.beacon.slot` and
  // `nextSyncCommittee.pubkeys` from the SSZ-validated updates -- the bytes either parse
  // or they do not. Without this ordering, a long-range attack with compromised pre-WSP
  // validator keys could persist forged committees on the first PENDING/ERROR round and
  // bypass the check on subsequent retries.
  if (have_checkpoint_proof && spec) {
    ssz_ob_t cp_header = ssz_get(&bootstrap, "header");
    uint64_t cp_slot   = ssz_get_uint64(&cp_header, "slot");
    uint32_t cp_period = (uint32_t) period_for_slot(cp_slot, spec);
    if (cp_period == 0) RETURN_VERIFY_ERROR_STATUS(ctx, "CheckpointProof: bootstrap period is zero");
    uint32_t target_lcu_period = cp_period - 1;

    bytes32_t lcu_pubkeys_root = {0};
    bool      found_lcu        = false;
    for (uint32_t i = 0; i < updates_len; i++) {
      ssz_ob_t update = ssz_union(ssz_at(updates, i));
      if (try_extract_lcu_pubkeys_root(update, spec, target_lcu_period, lcu_pubkeys_root)) {
        found_lcu = true;
        break;
      }
    }
    if (!found_lcu) RETURN_VERIFY_ERROR_STATUS(ctx, "CheckpointProof: no LCU covers the bootstrap period");

    TRY_ASYNC(c4_verify_checkpoint_proof(ctx, bootstrap, lcu_pubkeys_root));
  }
  else if (spec && updates_len) {
    // Legacy WSP pre-scan: only kicks in when the prover did NOT send a CheckpointProof.
    // Anchors the highest finalized header against checkpointz (weaker than the
    // pubkeys-cross-check above, but backwards-compatible with provers that have not
    // been upgraded to send `checkpoint_proof`).
    uint32_t  highest_known          = cached_highest_period(ctx->chain_id);
    uint64_t  highest_finalized_slot = 0;
    bytes32_t highest_finalized_root = {0};

    for (uint32_t i = 0; i < updates_len; i++) {
      ssz_ob_t update           = ssz_union(ssz_at(updates, i));
      ssz_ob_t finalized_beacon = lcu_finalized_beacon(&update);
      uint64_t slot             = ssz_get_uint64(&finalized_beacon, "slot");
      if (slot > highest_finalized_slot) {
        highest_finalized_slot = slot;
        ssz_hash_tree_root(finalized_beacon, highest_finalized_root);
      }
    }

    if (highest_finalized_slot) {
      // Map the finalized header to the period whose next sync committee these updates would
      // teach us about; matches the period that update_light_client_update() will persist.
      uint32_t target_period = (uint32_t) period_for_slot(highest_finalized_slot, spec) + 1;
      if (wsp_exceeded(spec, highest_known, target_period)) {
        c4_status_t wsp_status = c4_verify_checkpointz_root(ctx, highest_finalized_slot, highest_finalized_root);
        if (wsp_status != C4_SUCCESS) return wsp_status; // PENDING re-enters cleanly; ERROR is already recorded on ctx->state
      }
    }
  }
#else
  (void) spec;
#endif

  // WSP anchor passed (or not required) -- now it is safe to apply and persist the updates.
  for (uint32_t i = 0; i < updates_len; i++) {
    ssz_ob_t update = ssz_union(ssz_at(updates, i));
    if (!update_light_client_update(ctx, &update)) return C4_ERROR;
  }

  // we may want to clean up the sync data, so we don't sync again.
  ctx->sync_data.def = &ssz_none;
  return C4_SUCCESS;
}

static c4_status_t update_from_zk_sync_data(verify_ctx_t* ctx) {
#ifdef ETH_ZKPROOF
  bytes32_t           previous_pubkeys_hash = {0};
  const chain_spec_t* spec                  = c4_eth_get_chain_spec(ctx->chain_id);
  bytes_t             vk_hash               = ssz_get(&ctx->sync_data, "vk_hash").bytes;
  bytes_t             proof                 = ssz_get(&ctx->sync_data, "proof").bytes;
  ssz_ob_t            header                = ssz_get(&ctx->sync_data, "header");
  uint64_t            attested_slot         = ssz_get_uint64(&header, "slot");
  ssz_ob_t            pub_keys              = ssz_get(&ctx->sync_data, "pubkeys");
  ssz_ob_t            signatures            = ssz_get(&ctx->sync_data, "signatures");
  uint32_t            period                = (attested_slot >> (spec->slots_per_epoch_bits + spec->epochs_per_period_bits)) + 1;
  c4_chain_state_t    chain_state           = c4_get_chain_state(ctx->chain_id);

  // do we already have this period?
  if (chain_state.status == C4_STATE_SYNC_PERIODS) {
    for (int i = 0; i < MAX_SYNC_PERIODS; i++) {
      if (chain_state.data.periods[i] == period) {
        log_debug("period %d already exists", period);
        ctx->sync_data.def = &ssz_none;
        return C4_SUCCESS;
      }
    }
  }

  // Trust-anchor first, ZK-proof second. The ZK-proof verification is the heaviest
  // operation in this function (~hundreds of ms for a Groth16 verify). If the WSP
  // anchor returns C4_PENDING (checkpointz round-trip in flight), the host re-enters
  // this function on the next loop iteration -- so any work done *before* a PENDING
  // return is paid for twice. Pushing the ZK-proof past every PENDING-capable step
  // makes the second pass cheap (cached checkpointz response → straight through to
  // the ZK-proof). Witness-anchor and synchronous SSZ accessors stay first because
  // they cannot return PENDING.
  bool have_witness_anchor = ctx->witness_keys.len > 0 && ssz_len(signatures) > 0;
  if (have_witness_anchor) {
    if (!verify_signatures(ctx, ssz_get(&ctx->sync_data, "checkpoint"), header, signatures))
      RETURN_VERIFY_ERROR_STATUS(ctx, "invalid checkpoint signatures!");
  }
  else if (ctx->witness_keys.len > 0) {
    // Witness keys configured but no signatures supplied -- this is a configuration mismatch.
    RETURN_VERIFY_ERROR_STATUS(ctx, "checkpoint_witness_keys configured but prover did not deliver witness signatures");
  }
  else {
#ifdef USE_CHECKPOINTZ
    uint32_t highest_known = cached_highest_period(ctx->chain_id);
    if (wsp_exceeded(spec, highest_known, period)) {
      ssz_ob_t checkpoint = ssz_get(&ctx->sync_data, "checkpoint");

      // CheckpointProof variant: bootstrap-derived anchor with full pubkeys-cross-check.
      // The double-trust model requires the chain-of-trust (ZK proof public output =
      // `pubkeys`) and the canonical anchor (checkpointz-confirmed Bootstrap header)
      // to vouch for the same `currentSyncCommittee`. The cross-check happens *before*
      // ZK-proof verification, so a forged `pubkeys` blob would be caught here without
      // having to first absorb the cost of a 100 ms+ Groth16 verify on bogus data.
      if (is_checkpoint_proof_variant(checkpoint)) {
        bytes32_t zk_pubkeys_root = {0};
        ssz_hash_tree_root(pub_keys, zk_pubkeys_root);
        TRY_ASYNC(c4_verify_checkpoint_proof(ctx, checkpoint, zk_pubkeys_root));
      }
      else {
        // Legacy header_proof / signature_proof / historic_proof variants: anchor only
        // the checkpoint header itself against checkpointz (no committee cross-check).
        // Anchor against the checkpoint header (epoch boundary by construction in
        // `period_store_zk_ssz.c`), NOT the attested header (typically mid-epoch);
        // checkpointz only serves epoch-boundary blocks. `historic_proof` carries the
        // same `header` field as `header_proof`, so both share the anchor logic.
        bool      have_header_field = checkpoint.def && (strcmp(checkpoint.def->name, "headerProof") == 0 ||
                                                    strcmp(checkpoint.def->name, "historic_proof") == 0);
        ssz_ob_t  anchor_header     = have_header_field
                                          ? ssz_get(&checkpoint, "header")
                                          : header; // signature_proof has no embedded anchor header; falls back to attested
        uint64_t  anchor_slot       = ssz_get_uint64(&anchor_header, "slot");
        bytes32_t anchor_root       = {0};
        ssz_hash_tree_root(anchor_header, anchor_root);
        c4_status_t wsp_status = c4_verify_checkpointz_root(ctx, anchor_slot, anchor_root);
        if (wsp_status == C4_PENDING) return C4_PENDING;
        if (wsp_status == C4_ERROR) RETURN_VERIFY_ERROR_STATUS(ctx, "Weak subjectivity check failed for ZK sync data");
      }
    }
#else
    if (wsp_exceeded(spec, cached_highest_period(ctx->chain_id), period))
      log_warn("ZK sync data crosses the Weak Subjectivity Period but USE_CHECKPOINTZ is disabled -- anchor cannot be verified");
#endif
  }

  // Trust anchor passed; now the expensive ZK-proof verification.
  uint8_t pub_inputs[136] = {0};
  memcpy(pub_inputs, spec->zk_sync_keys_root, 32); // root-anchor
  ssz_hash_tree_root(pub_keys, pub_inputs + 32);   // next_keys_root
  uint64_to_le(pub_inputs + 64, period);           // next_period
  ssz_hash_tree_root(header, pub_inputs + 72);     // attested_header_root
  if (!eth_calculate_domain(ctx->chain_id, attested_slot, pub_inputs + 104)) RETURN_VERIFY_ERROR_STATUS(ctx, "unsupported chain!");
  if (!c4_verify_zk_proof(proof, bytes(pub_inputs, 136), vk_hash.data)) RETURN_VERIFY_ERROR_STATUS(ctx, "invalid zk_proof!");

  if (!c4_set_sync_period(period, pub_keys.bytes, ctx->chain_id, previous_pubkeys_hash)) RETURN_VERIFY_ERROR_STATUS(ctx, "failed to store next sync committee!");
  log_debug("zk proof verified successfully for period %d!", period);

  // we may want to clean up the sync data, so we don't sync again.
  ctx->sync_data.def = &ssz_none;
  return C4_SUCCESS;
#else
  RETURN_VERIFY_ERROR_STATUS(ctx, "zk_proof not supported!");
#endif
}

INTERNAL c4_status_t c4_update_from_sync_data(verify_ctx_t* ctx) {
  if (ssz_is_error(ctx->sync_data)) RETURN_VERIFY_ERROR_STATUS(ctx, "invalid sync_data!");
  if (ctx->sync_data.def->type == SSZ_TYPE_NONE) return C4_SUCCESS;

  log_debug("c4_update_from_sync_data: %s", (char*) ctx->sync_data.def->name);
  if (strcmp(ctx->sync_data.def->name, "LCSyncData") == 0)
    return update_from_lc_sync_data(ctx);
  else if (strcmp(ctx->sync_data.def->name, "ZKSyncDataV6") == 0)
    return update_from_zk_sync_data(ctx);
  else
    RETURN_VERIFY_ERROR_STATUS(ctx, "unknown sync_data type!");
}

/**
 * Detects the format of light client updates (Standard SSZ or Lighthouse variant).
 * Lighthouse format uses a different offset structure.
 *
 * @param data The raw bytes of light client updates
 * @return true if Lighthouse format, false for standard format
 */
static bool detect_update_format(bytes_t data) {
  // Lighthouse detection: check if length is sufficient, second offset is non-zero, and first value is reasonable
  return data.len > UPDATE_PREFIX_SIZE &&
         !bytes_all_zero(bytes_slice(data, SSZ_OFFSET_SIZE, SSZ_OFFSET_SIZE)) &&
         uint32_from_le(data.data) < 1000;
}

// Trampoline that adapts the walker's `c4_lcu_chunk_cb_t` to the verifier's
// `(verify_ctx_t*, ssz_ob_t*)` callback signature.
typedef struct {
  verify_ctx_t* ctx;
  bool (*process_update)(verify_ctx_t*, ssz_ob_t*);
} lcu_verify_cb_ctx_t;

static bool lcu_verify_chunk_cb(void* user, const c4_lcu_chunk_t* chunk) {
  lcu_verify_cb_ctx_t* c = (lcu_verify_cb_ctx_t*) user;
  // `chunk->update` is const in intent, but `ssz_get` / callers may mutate
  // ancillary caches inside the ssz_ob_t. Copy locally to keep the
  // walker-owned view untouched.
  ssz_ob_t update = chunk->update;
  return c->process_update(c->ctx, &update);
}

// Legacy fallback for the Lighthouse variant of the update list. Lighthouse
// clients emit a per-list SSZ offset table instead of the Beacon-API framing,
// so we cannot feed it into `c4_eth_walk_lcu_list`. Kept as a pre-existing
// compatibility path -- issue #356 leaves the Lighthouse format out of scope.
static bool process_lighthouse_updates(verify_ctx_t* ctx, bytes_t light_client_updates, bool (*process_update)(verify_ctx_t*, ssz_ob_t*)) {
  uint64_t length  = 0;
  bool     success = true;
  int      idx     = 0;

  for (uint32_t pos = 0; pos + UPDATE_PREFIX_SIZE < light_client_updates.len; pos += length + SSZ_LENGTH_SIZE, idx++) {
    uint32_t data_offset = pos + SSZ_LENGTH_SIZE + SSZ_OFFSET_SIZE;

    // Check bounds before reading offset
    if (idx * SSZ_OFFSET_SIZE + SSZ_OFFSET_SIZE > light_client_updates.len) {
      success = false;
      c4_state_add_error(&ctx->state, "invalid lighthouse index exceeds data bounds!");
      break;
    }
    pos = uint32_from_le(light_client_updates.data + (idx * SSZ_OFFSET_SIZE));
    if (pos + UPDATE_PREFIX_SIZE > light_client_updates.len) {
      success = false;
      c4_state_add_error(&ctx->state, "invalid offset in lighthouse client update!");
      break;
    }
    data_offset = pos + LIGHTHOUSE_OFFSET_SIZE + SSZ_OFFSET_SIZE;

    length = uint64_from_le(light_client_updates.data + pos);

    // Check for integer overflow and bounds
    if (length < SSZ_OFFSET_SIZE ||
        (length > UPDATE_PREFIX_SIZE && (pos + SSZ_LENGTH_SIZE + length > light_client_updates.len || pos + SSZ_LENGTH_SIZE + length < pos))) {
      success = false;
      c4_state_add_error(&ctx->state, "invalid length causes overflow or exceeds bounds!");
      break;
    }

    bytes_t light_client_update_bytes = bytes(light_client_updates.data + data_offset, length - SSZ_OFFSET_SIZE);
    // Lighthouse framing puts its own header between the length prefix and
    // the payload -- the 4 bytes at `pos+8` are NOT a ForkDigest. Recover
    // the fork from the attested-header slot inside the payload (the
    // pre-#356 heuristic), which is out of scope for the shared walker.
    uint64_t            slot = 0;
    if (light_client_update_bytes.len >= 4) {
      uint32_t hdr_off = uint32_from_le(light_client_update_bytes.data);
      // 64-bit compare: `hdr_off + 8` as uint32 wraps for hdr_off > UINT32_MAX-8.
      if ((uint64_t) hdr_off + 8u <= (uint64_t) light_client_update_bytes.len)
        slot = uint64_from_le(light_client_update_bytes.data + hdr_off);
      else if (light_client_update_bytes.len >= 8)
        slot = uint64_from_le(light_client_update_bytes.data);
    }
    const chain_spec_t* spec                   = c4_eth_get_chain_spec(ctx->chain_id);
    fork_id_t           fork                   = c4_chain_fork_id(ctx->chain_id, epoch_for_slot(slot, spec));
    const ssz_def_t*    light_client_update_def = eth_get_light_client_update(fork);

    if (!light_client_update_def) {
      c4_state_add_error(&ctx->state, "light client update: unknown/unsupported fork");
      success = false;
      break;
    }

    ssz_ob_t light_client_update_ob = {.bytes = light_client_update_bytes, .def = light_client_update_def};

    if (!ssz_is_valid(light_client_update_ob, true, &ctx->state)) {
      success = false;
      c4_state_add_error(&ctx->state, "Invalid SSZ structure in light client update");
      break;
    }

    // Process this update using the callback
    if (!process_update(ctx, &light_client_update_ob)) {
      success = false;
      break;
    }
  }

  return success;
}

/**
 * Process light client updates with a callback function for each update.
 *
 * Uses `c4_eth_walk_lcu_list` for the Beacon-API framing (which also marks
 * the enclosing `data_request_t` as `validated` on structural success -- see
 * issue #356). Falls back to a Lighthouse-specific parser when the input is
 * detected as the offset-table variant.
 *
 * @param ctx Verification context
 * @param light_client_updates Raw bytes containing one or more light client updates
 * @param process_update Callback function to process each individual update
 * @return true if all updates were processed successfully, false otherwise
 */
INTERNAL bool c4_process_light_client_updates(verify_ctx_t* ctx, bytes_t light_client_updates, bool (*process_update)(verify_ctx_t*, ssz_ob_t*)) {
  if (detect_update_format(light_client_updates))
    return process_lighthouse_updates(ctx, light_client_updates, process_update);

  // Standard Beacon-API format: hand off to the shared walker, which also
  // marks the request `validated` on framing success.
  data_request_t* src_req = c4_state_get_data_request_by_response(&ctx->state, light_client_updates);
  lcu_verify_cb_ctx_t cb_ctx = {.ctx = ctx, .process_update = process_update};
  return c4_eth_walk_lcu_list(ctx->chain_id, light_client_updates, &ctx->state, src_req,
                              /*validate_ssz*/ true, lcu_verify_chunk_cb, &cb_ctx);
}

INTERNAL bool c4_handle_client_updates(verify_ctx_t* ctx, bytes_t light_client_updates) {
  // Check for JSON error message
  if (light_client_updates.len && light_client_updates.data[0] == '{') {
    json_t json = json_parse((char*) light_client_updates.data);
    json_t msg  = json_get(json, "message");
    if (msg.start) {
      ctx->state.error = bprintf(NULL, "Invalid light client updates: %j", msg);
      return false;
    };
  }

  // Process all light client updates using the general processor
  return c4_process_light_client_updates(ctx, light_client_updates, update_light_client_update);
}
