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

// Tests for the Gloas SSZ preparation (EIP-7688 progressive containers plus
// EIP-7732 ePBS). The main goals are:
//   1. Verify that our progressive-container gindex helpers match the values
//      pinned in `specs/gloas/light-client/sync-protocol.md`.
//   2. Verify that fork dispatch (`eth_ssz_type_for_fork`, `eth_get_light_client_*`)
//      returns the correct Gloas variants and does not break Deneb/Electra.
//   3. Ensure the light-client containers accept sane payloads (structural
//      validation + hash_tree_root does not crash).

#include "beacon_types.h"
#include "bytes.h"
#include "c4_assert.h" // provides read_testdata() and ASSERT_HEX_STRING_EQUAL()
#include "chains.h"
#include "ssz.h"
#include "state.h"
#include "unity.h"
#include <string.h>

// `NOT_ASSIGNED_YET` is defined internally in beacon_types.c and represents
// an unscheduled fork epoch. Duplicated here so the canary test that guards
// against premature Gloas activation stays self-contained.
#define GLOAS_TEST_NOT_ASSIGNED_YET 0xffffffffffffffffULL

void setUp(void) {}
void tearDown(void) {}

// -----------------------------------------------------------------------------
// Minimal Gloas `BeaconState` for gindex verification.
// -----------------------------------------------------------------------------
//
// We do not care about the merkleization content here, only about field
// positions in the progressive container. So placeholder types (uint64/bytes32)
// are enough as long as the field count, ordering and the fixed nested
// `Checkpoint` structure at position 20 match the Gloas spec.

// `Checkpoint` is a regular container (epoch, root).
static const ssz_def_t GLOAS_TEST_CHECKPOINT_FIELDS[] = {
    SSZ_UINT64("epoch"),
    SSZ_BYTES32("root")};

// `SyncCommittee` layout is irrelevant for the outer gindex; use a placeholder.
static const ssz_def_t GLOAS_TEST_SYNC_COMMITTEE_FIELDS[] = {
    SSZ_UINT64("dummy")};

// Positions match `specs/gloas/beacon-chain.md` line 816 (BeaconState, 46 fields).
static const ssz_def_t GLOAS_TEST_BEACON_STATE_BASE[] = {
    SSZ_UINT64("genesis_time"),                                                // 0
    SSZ_BYTES32("genesis_validators_root"),                                    // 1
    SSZ_UINT64("slot"),                                                        // 2
    SSZ_UINT64("fork"),                                                        // 3
    SSZ_UINT64("latest_block_header"),                                         // 4
    SSZ_UINT64("block_roots"),                                                 // 5
    SSZ_UINT64("state_roots"),                                                 // 6
    SSZ_UINT64("historical_roots"),                                            // 7
    SSZ_UINT64("eth1_data"),                                                   // 8
    SSZ_UINT64("eth1_data_votes"),                                             // 9
    SSZ_UINT64("eth1_deposit_index"),                                          // 10
    SSZ_UINT64("validators"),                                                  // 11
    SSZ_UINT64("balances"),                                                    // 12
    SSZ_UINT64("randao_mixes"),                                                // 13
    SSZ_UINT64("slashings"),                                                   // 14
    SSZ_UINT64("previous_epoch_participation"),                                // 15
    SSZ_UINT64("current_epoch_participation"),                                 // 16
    SSZ_UINT64("justification_bits"),                                          // 17
    SSZ_UINT64("previous_justified_checkpoint"),                               // 18
    SSZ_UINT64("current_justified_checkpoint"),                                // 19
    SSZ_CONTAINER("finalized_checkpoint", GLOAS_TEST_CHECKPOINT_FIELDS),       // 20
    SSZ_UINT64("inactivity_scores"),                                           // 21
    SSZ_CONTAINER("current_sync_committee", GLOAS_TEST_SYNC_COMMITTEE_FIELDS), // 22
    SSZ_CONTAINER("next_sync_committee", GLOAS_TEST_SYNC_COMMITTEE_FIELDS),    // 23
    SSZ_BYTES32("latest_block_hash"),                                          // 24
    SSZ_UINT64("next_withdrawal_index"),                                       // 25
    SSZ_UINT64("next_withdrawal_validator_index"),                             // 26
    SSZ_UINT64("historical_summaries"),                                        // 27
    SSZ_UINT64("deposit_requests_start_index"),                                // 28
    SSZ_UINT64("deposit_balance_to_consume"),                                  // 29
    SSZ_UINT64("exit_balance_to_consume"),                                     // 30
    SSZ_UINT64("earliest_exit_epoch"),                                         // 31
    SSZ_UINT64("consolidation_balance_to_consume"),                            // 32
    SSZ_UINT64("earliest_consolidation_epoch"),                                // 33
    SSZ_UINT64("pending_deposits"),                                            // 34
    SSZ_UINT64("pending_partial_withdrawals"),                                 // 35
    SSZ_UINT64("pending_consolidations"),                                      // 36
    SSZ_UINT64("proposer_lookahead"),                                          // 37
    SSZ_UINT64("builders"),                                                    // 38
    SSZ_UINT64("next_withdrawal_builder_index"),                               // 39
    SSZ_UINT64("execution_payload_availability"),                              // 40
    SSZ_UINT64("builder_pending_payments"),                                    // 41
    SSZ_UINT64("builder_pending_withdrawals"),                                 // 42
    SSZ_UINT64("latest_execution_payload_bid"),                                // 43
    SSZ_UINT64("payload_expected_withdrawals"),                                // 44
    SSZ_UINT64("ptc_window")                                                   // 45
};

static const ssz_def_t GLOAS_TEST_BEACON_STATE_BASE_CONTAINER =
    SSZ_CONTAINER("GloasBeaconStateBase", GLOAS_TEST_BEACON_STATE_BASE);
static const ssz_def_t GLOAS_TEST_BEACON_STATE =
    SSZ_PROG_CONTAINER("GloasBeaconState", GLOAS_TEST_BEACON_STATE_BASE_CONTAINER, 0x3FFFFFFFFFFFULL); // [1]*46

void test_gloas_state_gindexes(void) {
  // Runtime field-count check instead of `_Static_assert`: MSVC's C frontend
  // rejects `_Static_assert` at file scope (C2143/C2059), so keep the guard
  // portable across Clang/GCC/MSVC.
  TEST_ASSERT_EQUAL_size_t_MESSAGE(
      46, sizeof(GLOAS_TEST_BEACON_STATE_BASE) / sizeof(ssz_def_t),
      "Gloas BeaconState must have exactly 46 fields");

  // Values pinned by specs/gloas/light-client/sync-protocol.md
  TEST_ASSERT_EQUAL_UINT64(2945, ssz_gindex(&GLOAS_TEST_BEACON_STATE, 1, "current_sync_committee"));
  TEST_ASSERT_EQUAL_UINT64(2946, ssz_gindex(&GLOAS_TEST_BEACON_STATE, 1, "next_sync_committee"));

  // finalized_checkpoint at container position 20 (gindex 367), .root is
  // field 1 of the Checkpoint container → 367 * 2 + 1 = 735.
  TEST_ASSERT_EQUAL_UINT64(367, ssz_gindex(&GLOAS_TEST_BEACON_STATE, 1, "finalized_checkpoint"));
  TEST_ASSERT_EQUAL_UINT64(735, ssz_gindex(&GLOAS_TEST_BEACON_STATE, 2, "finalized_checkpoint", "root"));
}

// -----------------------------------------------------------------------------
// Dispatch tests
// -----------------------------------------------------------------------------

void test_gloas_dispatch_lc_update(void) {
  const ssz_def_t* deneb   = eth_get_light_client_update(C4_FORK_DENEB);
  const ssz_def_t* electra = eth_get_light_client_update(C4_FORK_ELECTRA);
  const ssz_def_t* fulu    = eth_get_light_client_update(C4_FORK_FULU);
  const ssz_def_t* gloas   = eth_get_light_client_update(C4_FORK_GLOAS);

  TEST_ASSERT_NOT_NULL_MESSAGE(deneb, "Deneb LC update missing");
  TEST_ASSERT_NOT_NULL_MESSAGE(electra, "Electra LC update missing");
  TEST_ASSERT_NOT_NULL_MESSAGE(fulu, "Fulu LC update missing");
  TEST_ASSERT_NOT_NULL_MESSAGE(gloas, "Gloas LC update missing");

  TEST_ASSERT_EQUAL_STRING("DenepLightClientUpdate", deneb->name);
  TEST_ASSERT_EQUAL_STRING("ElectraLightClientUpdate", electra->name);
  // Fulu reuses the Electra container.
  TEST_ASSERT_EQUAL_PTR(electra, fulu);
  TEST_ASSERT_EQUAL_STRING("GloasLightClientUpdate", gloas->name);

  // Union indices must remain stable for wire compatibility.
  TEST_ASSERT_EQUAL_PTR(deneb + 1, electra);
  TEST_ASSERT_EQUAL_PTR(deneb + 2, gloas);

  TEST_ASSERT_NULL(eth_get_light_client_update((fork_id_t) 42));
}

void test_gloas_dispatch_lc_bootstrap(void) {
  const ssz_def_t* deneb   = eth_get_light_client_bootstrap(C4_FORK_DENEB);
  const ssz_def_t* electra = eth_get_light_client_bootstrap(C4_FORK_ELECTRA);
  const ssz_def_t* gloas   = eth_get_light_client_bootstrap(C4_FORK_GLOAS);

  TEST_ASSERT_NOT_NULL(deneb);
  TEST_ASSERT_NOT_NULL(electra);
  TEST_ASSERT_NOT_NULL(gloas);

  TEST_ASSERT_EQUAL_STRING("DenepLightClientBootstrap", deneb->name);
  TEST_ASSERT_EQUAL_STRING("ElectraLightClientBootstrap", electra->name);
  TEST_ASSERT_EQUAL_STRING("GloasLightClientBootstrap", gloas->name);

  // Pre-Electra forks (incl. Deneb) share the Deneb bootstrap.
  TEST_ASSERT_EQUAL_PTR(deneb, eth_get_light_client_bootstrap(C4_FORK_CAPELLA));
  TEST_ASSERT_EQUAL_PTR(deneb, eth_get_light_client_bootstrap(C4_FORK_BELLATRIX));
  // Fulu keeps the Electra layout.
  TEST_ASSERT_EQUAL_PTR(electra, eth_get_light_client_bootstrap(C4_FORK_FULU));

  TEST_ASSERT_NULL(eth_get_light_client_bootstrap((fork_id_t) 99));
}

void test_gloas_dispatch_ssz_type_for_fork(void) {
  // For Gloas the dispatcher should return the Gloas signed-block container
  // (its name is "signedBeaconBlock" and it consists of `message` + `signature`).
  const ssz_def_t* signed_block = eth_ssz_type_for_fork(ETH_SSZ_SIGNED_BEACON_BLOCK_CONTAINER, C4_FORK_GLOAS, 1);
  TEST_ASSERT_NOT_NULL(signed_block);
  TEST_ASSERT_EQUAL_STRING("signedBeaconBlock", signed_block->name);

  // BEACON_BLOCK_HEADER is fork-stable and falls through to Deneb via Electra.
  const ssz_def_t* header = eth_ssz_type_for_fork(ETH_SSZ_BEACON_BLOCK_HEADER, C4_FORK_GLOAS, 1);
  TEST_ASSERT_NOT_NULL(header);
}

// -----------------------------------------------------------------------------
// Union layout regression tests (wire-format compatibility)
// -----------------------------------------------------------------------------
//
// The bootstrap/update unions carry wire-serialized SSZ union tags: the first
// byte of the on-wire representation names the variant index. Reordering the
// unions would silently change wire format for previously-released clients, so
// the exact indices Deneb=1, Electra=2, CheckpointProof=3, Gloas=4 (bootstrap)
// and Deneb=0, Electra=1, Gloas=2 (update) are frozen. These tests fail loudly
// if a future contributor accidentally swaps or inserts a variant.

void test_gloas_bootstrap_union_layout(void) {
  // Walk the union from the Deneb entry (index 1) and confirm neighbouring
  // slots match the frozen wire indices.
  const ssz_def_t* d = eth_get_light_client_bootstrap(C4_FORK_DENEB);
  const ssz_def_t* e = eth_get_light_client_bootstrap(C4_FORK_ELECTRA);
  const ssz_def_t* g = eth_get_light_client_bootstrap(C4_FORK_GLOAS);
  TEST_ASSERT_NOT_NULL(d);
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_NOT_NULL(g);

  // Deneb at union[1], Electra at union[2], CheckpointProof at union[3], Gloas at union[4].
  TEST_ASSERT_EQUAL_PTR_MESSAGE(d + 1, e, "Electra bootstrap must be at bootstrap-union index 2");
  TEST_ASSERT_EQUAL_PTR_MESSAGE(d + 3, g, "Gloas bootstrap must be at bootstrap-union index 4");

  // union[3] holds the WSP anchor `CheckpointProof` and must stay between
  // Electra and Gloas so pre-Gloas clients still parse index 3 correctly.
  TEST_ASSERT_EQUAL_STRING_MESSAGE("CheckpointProof", (d + 2)->name,
                                   "CheckpointProof must stay at bootstrap-union index 3");

  // union[0] is SSZ_NONE and must remain the empty variant.
  TEST_ASSERT_EQUAL_INT_MESSAGE(SSZ_TYPE_NONE, (d - 1)->type,
                                "bootstrap-union index 0 must remain SSZ_NONE");
}

void test_gloas_update_union_layout(void) {
  // The update union is contiguous with no NONE prefix: Deneb=0, Electra/Fulu=1, Gloas=2.
  const ssz_def_t* d = eth_get_light_client_update(C4_FORK_DENEB);
  const ssz_def_t* e = eth_get_light_client_update(C4_FORK_ELECTRA);
  const ssz_def_t* g = eth_get_light_client_update(C4_FORK_GLOAS);
  TEST_ASSERT_NOT_NULL(d);
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_NOT_NULL(g);

  TEST_ASSERT_EQUAL_PTR_MESSAGE(d + 1, e, "Electra update must be at update-union index 1");
  TEST_ASSERT_EQUAL_PTR_MESSAGE(d + 2, g, "Gloas update must be at update-union index 2");

  // The container names embedded in the union are the on-wire discriminators
  // used by the `ssz_dump` output and by `c4_eth_get_fork_for_lcu` callers.
  TEST_ASSERT_EQUAL_STRING("DenepLightClientUpdate", d->name);
  TEST_ASSERT_EQUAL_STRING("ElectraLightClientUpdate", e->name);
  TEST_ASSERT_EQUAL_STRING("GloasLightClientUpdate", g->name);
}

// -----------------------------------------------------------------------------
// Gindex helper coverage (Deneb / Electra / Fulu branches)
// -----------------------------------------------------------------------------
//
// The `c4_*_gindex` helpers dispatch by fork; the Gloas branch is currently
// unreachable at runtime because every chain_spec has `fork_epochs[GLOAS-1] ==
// NOT_ASSIGNED_YET`. These tests exercise the two reachable branches against
// the real mainnet chain_spec so a regression in `fork_at_slot` or in the
// fork-comparison thresholds surfaces immediately. The Gloas gindex values
// themselves are covered by `test_gloas_state_gindexes` above.

void test_gloas_gindex_helpers_pre_gloas_branches(void) {
  const chain_spec_t* spec = c4_eth_get_chain_spec(C4_CHAIN_MAINNET);
  TEST_ASSERT_NOT_NULL(spec);

  // Mainnet fork epochs (beacon_types.c): 74240,144896,194048,269568,364032,411392,NOT_ASSIGNED_YET
  //  index 3 = Deneb (269568), index 4 = Electra (364032), index 5 = Fulu (411392).
  // Pick epochs safely inside each fork window.
  uint64_t deneb_slot   = slot_for_epoch(300000ULL, spec); // Deneb (>= 269568, < 364032)
  uint64_t electra_slot = slot_for_epoch(400000ULL, spec); // Electra (>= 364032, < 411392)
  uint64_t fulu_slot    = slot_for_epoch(500000ULL, spec); // Fulu (>= 411392)

  // Deneb: (54, 55, 105)
  TEST_ASSERT_EQUAL_UINT64(54, c4_current_sync_committee_gindex(C4_CHAIN_MAINNET, deneb_slot));
  TEST_ASSERT_EQUAL_UINT64(55, c4_next_sync_committee_gindex(C4_CHAIN_MAINNET, deneb_slot));
  TEST_ASSERT_EQUAL_UINT64(105, c4_finalized_root_gindex(C4_CHAIN_MAINNET, deneb_slot));

  // Electra: (86, 87, 169)
  TEST_ASSERT_EQUAL_UINT64(86, c4_current_sync_committee_gindex(C4_CHAIN_MAINNET, electra_slot));
  TEST_ASSERT_EQUAL_UINT64(87, c4_next_sync_committee_gindex(C4_CHAIN_MAINNET, electra_slot));
  TEST_ASSERT_EQUAL_UINT64(169, c4_finalized_root_gindex(C4_CHAIN_MAINNET, electra_slot));

  // Fulu keeps the Electra state layout for these fields.
  TEST_ASSERT_EQUAL_UINT64(86, c4_current_sync_committee_gindex(C4_CHAIN_MAINNET, fulu_slot));
  TEST_ASSERT_EQUAL_UINT64(87, c4_next_sync_committee_gindex(C4_CHAIN_MAINNET, fulu_slot));
  TEST_ASSERT_EQUAL_UINT64(169, c4_finalized_root_gindex(C4_CHAIN_MAINNET, fulu_slot));

  // Cross-check on Gnosis (different slots_per_epoch_bits / epochs_per_period_bits).
  const chain_spec_t* g_spec = c4_eth_get_chain_spec(C4_CHAIN_GNOSIS);
  TEST_ASSERT_NOT_NULL(g_spec);
  // Gnosis Electra epoch = 1714688; pick something above that.
  uint64_t gnosis_electra_slot = slot_for_epoch(1800000ULL, g_spec);
  TEST_ASSERT_EQUAL_UINT64(86, c4_current_sync_committee_gindex(C4_CHAIN_GNOSIS, gnosis_electra_slot));
  TEST_ASSERT_EQUAL_UINT64(87, c4_next_sync_committee_gindex(C4_CHAIN_GNOSIS, gnosis_electra_slot));
  TEST_ASSERT_EQUAL_UINT64(169, c4_finalized_root_gindex(C4_CHAIN_GNOSIS, gnosis_electra_slot));
}

// -----------------------------------------------------------------------------
// Deferred-activation canary
// -----------------------------------------------------------------------------
//
// Several code paths carry `TODO(gloas)` markers that must be revisited before
// Gloas is scheduled on any chain (see `eth_tx.h`, `eth_account.h`,
// `beacon_types.c::c4_block_header_gindexes`, `historic_proof.c::summaries_gidx`).
// This test fails as soon as any chain_spec assigns a real epoch to Gloas, so a
// contributor who schedules Gloas cannot forget the follow-up work.

void test_gloas_activation_epoch_still_reserved(void) {
  const chain_id_t chains[] = {
      C4_CHAIN_MAINNET,
      C4_CHAIN_SEPOLIA,
      C4_CHAIN_GNOSIS,
      C4_CHAIN_GNOSIS_CHIADO};

  for (size_t i = 0; i < sizeof(chains) / sizeof(chains[0]); i++) {
    const chain_spec_t* spec = c4_eth_get_chain_spec(chains[i]);
    TEST_ASSERT_NOT_NULL(spec);
    // fork_epochs is indexed from Altair (fork_id - 1) -> Gloas is C4_FORK_GLOAS - 1 = 6.
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(
        GLOAS_TEST_NOT_ASSIGNED_YET,
        spec->fork_epochs[C4_FORK_GLOAS - 1],
        "A chain has scheduled Gloas: revisit TODO(gloas) markers in eth_tx.h, "
        "eth_account.h, beacon_types.c (c4_block_header_gindexes) and "
        "historic_proof.c (summaries_gidx) before activation.");
  }
}

// -----------------------------------------------------------------------------
// Sync-committee branch depths across forks
// -----------------------------------------------------------------------------
//
// The bootstrap `currentSyncCommitteeBranch` depth is fork-specific: Deneb=5,
// Electra=6, Gloas=11. Same story for update finality/next branches. Cross-check
// against the values documented in `verify_types.c` and `sync_committee.h` so
// none of the three variants can silently drift.

void test_all_forks_bootstrap_branch_depths(void) {
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(5, DENEP_LIGHT_CLIENT_BOOTSTRAP[2].def.vector.len,
                                   "Deneb bootstrap currentSyncCommitteeBranch must be depth 5");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(6, ELECTRA_LIGHT_CLIENT_BOOTSTRAP[2].def.vector.len,
                                   "Electra bootstrap currentSyncCommitteeBranch must be depth 6");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(11, GLOAS_LIGHT_CLIENT_BOOTSTRAP[2].def.vector.len,
                                   "Gloas bootstrap currentSyncCommitteeBranch must be depth 11");

  // Deneb / Electra light-client updates also carry the branch depths encoded
  // as vector lengths; sanity-check them alongside Gloas so the DENEP/ELECTRA
  // extern declarations are exercised.
  TEST_ASSERT_EQUAL_STRING("nextSyncCommitteeBranch", DENEP_LIGHT_CLIENT_UPDATE[2].name);
  TEST_ASSERT_EQUAL_STRING("nextSyncCommitteeBranch", ELECTRA_LIGHT_CLIENT_UPDATE[2].name);
  TEST_ASSERT_EQUAL_UINT32(5, DENEP_LIGHT_CLIENT_UPDATE[2].def.vector.len);
  TEST_ASSERT_EQUAL_UINT32(6, ELECTRA_LIGHT_CLIENT_UPDATE[2].def.vector.len);
  TEST_ASSERT_EQUAL_STRING("finalityBranch", DENEP_LIGHT_CLIENT_UPDATE[4].name);
  TEST_ASSERT_EQUAL_STRING("finalityBranch", ELECTRA_LIGHT_CLIENT_UPDATE[4].name);
  // finalityBranch: Deneb=6, Electra=7 (per the consensus spec constants); the
  // Gloas value of 9 is checked by `test_gloas_lc_update_shape`.
  TEST_ASSERT_EQUAL_UINT32(6, DENEP_LIGHT_CLIENT_UPDATE[4].def.vector.len);
  TEST_ASSERT_EQUAL_UINT32(7, ELECTRA_LIGHT_CLIENT_UPDATE[4].def.vector.len);
}

// -----------------------------------------------------------------------------
// Gloas execution-block-hash gindex (documented as 2856, depth 11)
// -----------------------------------------------------------------------------
//
// The Gloas light-client protocol proves the execution block_hash against
// `body.signed_execution_payload_bid.message.parent_block_hash`. `beacon_gloas.c`
// pins this gindex to 2856 (depth 11) in a doc comment. The value depends on
// the whole progressive-container layout of `BeaconBlockBody`, so a
// spec-driven test guarantees the doc constant and the SSZ definitions cannot
// drift apart.

#ifdef PROVER
void test_gloas_execution_block_hash_gindex(void) {
  // Resolve the Gloas beacon-block-body definition via the public dispatcher;
  // the body container itself is `static` inside beacon_gloas.c.
  const ssz_def_t* body = eth_ssz_type_for_gloas(ETH_SSZ_BEACON_BLOCK_BODY_CONTAINER, C4_CHAIN_MAINNET);
  TEST_ASSERT_NOT_NULL(body);
  TEST_ASSERT_EQUAL_STRING("beaconBlockBody", body->name);

  // The bid path: body -> signedExecutionPayloadBid -> message -> parentBlockHash.
  gindex_t gindex = ssz_gindex(body, 3, "signedExecutionPayloadBid", "message", "parentBlockHash");
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(
      2856, gindex,
      "specs/gloas/light-client/sync-protocol.md pins EXECUTION_BLOCK_HASH_GINDEX_GLOAS = 2856");

  // The executionBranch vector length in GLOAS_LIGHT_CLIENT_HEADER must match
  // floor(log2(2856)) = 11 -- otherwise the branch buffer size and the gindex
  // are out of sync and merkle proofs against `body_root` would fail.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(11, GLOAS_LIGHT_CLIENT_HEADER[2].def.vector.len,
                                   "executionBranch depth must match the gindex depth");
}

void test_gloas_dispatch_body_container_shape(void) {
  // ETH_SSZ_BEACON_BLOCK_BODY_CONTAINER must resolve to the Gloas 13-field body,
  // not fall through to Electra. Verify structurally so a broken dispatcher
  // (e.g. accidental `default:` fall-through) fails here.
  const ssz_def_t* body = eth_ssz_type_for_gloas(ETH_SSZ_BEACON_BLOCK_BODY_CONTAINER, C4_CHAIN_MAINNET);
  TEST_ASSERT_NOT_NULL(body);
  // EIP-7732 body layout: EL payload removed, three new bid-related fields added.
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, ssz_gindex(body, 1, "executionPayload"),
                                   "Gloas body must not contain executionPayload (removed by EIP-7732)");
  TEST_ASSERT_NOT_EQUAL_UINT64_MESSAGE(0, ssz_gindex(body, 1, "signedExecutionPayloadBid"),
                                       "Gloas body must contain signedExecutionPayloadBid");
  TEST_ASSERT_NOT_EQUAL_UINT64_MESSAGE(0, ssz_gindex(body, 1, "payloadAttestations"),
                                       "Gloas body must contain payloadAttestations");
  TEST_ASSERT_NOT_EQUAL_UINT64_MESSAGE(0, ssz_gindex(body, 1, "parentExecutionRequests"),
                                       "Gloas body must contain parentExecutionRequests");

  // The execution-payload dispatcher returns the payload field of the envelope,
  // which is where the execution payload actually lives in Gloas.
  const ssz_def_t* ep = eth_ssz_type_for_gloas(ETH_SSZ_EXECUTION_PAYLOAD_CONTAINER, C4_CHAIN_MAINNET);
  TEST_ASSERT_NOT_NULL(ep);
  TEST_ASSERT_EQUAL_STRING_MESSAGE("payload", ep->name,
                                   "In Gloas the ExecutionPayload lives in the envelope, not the body");
}
#endif // PROVER

// -----------------------------------------------------------------------------
// Light-client container shape checks
// -----------------------------------------------------------------------------

void test_gloas_lc_header_shape(void) {
  // GLOAS_LIGHT_CLIENT_HEADER = { beacon: BEACON_BLOCK_HEADER,
  //                               executionBlockHash: Bytes32,
  //                               executionBranch: Vector[Bytes32, 11] }
  TEST_ASSERT_EQUAL_STRING("beacon", GLOAS_LIGHT_CLIENT_HEADER[0].name);
  TEST_ASSERT_EQUAL_STRING("executionBlockHash", GLOAS_LIGHT_CLIENT_HEADER[1].name);
  TEST_ASSERT_EQUAL_STRING("executionBranch", GLOAS_LIGHT_CLIENT_HEADER[2].name);
  TEST_ASSERT_EQUAL_UINT32(11, GLOAS_LIGHT_CLIENT_HEADER[2].def.vector.len);
}

void test_gloas_lc_bootstrap_shape(void) {
  TEST_ASSERT_EQUAL_STRING("header", GLOAS_LIGHT_CLIENT_BOOTSTRAP[0].name);
  TEST_ASSERT_EQUAL_STRING("currentSyncCommittee", GLOAS_LIGHT_CLIENT_BOOTSTRAP[1].name);
  TEST_ASSERT_EQUAL_STRING("currentSyncCommitteeBranch", GLOAS_LIGHT_CLIENT_BOOTSTRAP[2].name);
  // Depth 11 for Gloas (previously 6 in Electra, 5 in Deneb).
  TEST_ASSERT_EQUAL_UINT32(11, GLOAS_LIGHT_CLIENT_BOOTSTRAP[2].def.vector.len);
}

void test_gloas_lc_update_shape(void) {
  TEST_ASSERT_EQUAL_STRING("attestedHeader", GLOAS_LIGHT_CLIENT_UPDATE[0].name);
  TEST_ASSERT_EQUAL_STRING("nextSyncCommittee", GLOAS_LIGHT_CLIENT_UPDATE[1].name);
  TEST_ASSERT_EQUAL_STRING("nextSyncCommitteeBranch", GLOAS_LIGHT_CLIENT_UPDATE[2].name);
  TEST_ASSERT_EQUAL_STRING("finalizedHeader", GLOAS_LIGHT_CLIENT_UPDATE[3].name);
  TEST_ASSERT_EQUAL_STRING("finalityBranch", GLOAS_LIGHT_CLIENT_UPDATE[4].name);
  TEST_ASSERT_EQUAL_STRING("syncAggregate", GLOAS_LIGHT_CLIENT_UPDATE[5].name);
  TEST_ASSERT_EQUAL_STRING("signatureSlot", GLOAS_LIGHT_CLIENT_UPDATE[6].name);
  // Depth 11 for nextSyncCommittee, depth 9 for finality (Gloas ProgressiveContainer).
  TEST_ASSERT_EQUAL_UINT32(11, GLOAS_LIGHT_CLIENT_UPDATE[2].def.vector.len);
  TEST_ASSERT_EQUAL_UINT32(9, GLOAS_LIGHT_CLIENT_UPDATE[4].def.vector.len);
}

// -----------------------------------------------------------------------------
// Roundtrip: build a minimal Gloas LightClientHeader and hash_tree_root it.
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Bootstrap roundtrip: build a minimal Gloas LightClientBootstrap and validate.
// -----------------------------------------------------------------------------
//
// This mirrors the on-wire shape a Beacon API bootstrap would take once Gloas
// activates. We cannot verify the merkle proof (no reference vectors from a
// live Gloas chain), but `ssz_is_valid` exercises the offset table math and
// `hash_tree_root` runs the full progressive-container merkleization -- a
// smoke test that catches any layout mismatch between the header inside the
// bootstrap and the top-level container.

void test_gloas_lc_bootstrap_hash_tree_root(void) {
  // LightClientHeader (496 bytes) + SyncCommittee (pubkeys[512][48] + agg[48] = 24624) +
  //   currentSyncCommitteeBranch (11 * 32 = 352) = 25472 bytes total.
  //
  // Bootstrap is a fixed-size container (all children are fixed), so no offset
  // table is required.
  static uint8_t buf[496 + 24624 + 352] = {0};
  // Header: match the layout of test_gloas_lc_header_hash_tree_root.
  buf[0] = 0x2a;               // slot = 42
  buf[8] = 0x07;               // proposer_index = 7
  memset(buf + 16, 0xaa, 32);  // parent_root
  memset(buf + 48, 0xbb, 32);  // state_root
  memset(buf + 80, 0xcc, 32);  // body_root
  memset(buf + 112, 0xdd, 32); // executionBlockHash
  for (int i = 0; i < 11; i++)
    memset(buf + 144 + i * 32, (uint8_t) (0xe0 + i), 32);
  // SyncCommittee pubkeys: cycle through non-zero bytes so hash_tree_root has
  // something to chew on (validity check does not care about the actual keys).
  for (int i = 0; i < 512; i++)
    memset(buf + 496 + i * 48, (uint8_t) (i & 0xff), 48);
  memset(buf + 496 + 512 * 48, 0x11, 48); // aggregate pubkey
  memset(buf + 496 + 24624, 0x22, 352);   // currentSyncCommitteeBranch (11 * 32)

  const ssz_def_t bootstrap_def = SSZ_CONTAINER("LightClientBootstrap", GLOAS_LIGHT_CLIENT_BOOTSTRAP);
  ssz_ob_t        bootstrap     = {.bytes = bytes(buf, sizeof(buf)), .def = &bootstrap_def};

  c4_state_t state = {0};
  TEST_ASSERT_TRUE_MESSAGE(ssz_is_valid(bootstrap, true, &state),
                           state.error ? state.error : "Gloas LC bootstrap must validate");

  // Accessors must land on the correct sub-containers.
  ssz_ob_t header = ssz_get(&bootstrap, "header");
  TEST_ASSERT_EQUAL_UINT32(496, header.bytes.len);
  ssz_ob_t sync = ssz_get(&bootstrap, "currentSyncCommittee");
  TEST_ASSERT_EQUAL_UINT32(24624, sync.bytes.len);
  ssz_ob_t branch = ssz_get(&bootstrap, "currentSyncCommitteeBranch");
  TEST_ASSERT_EQUAL_UINT32(11 * 32, branch.bytes.len);

  // hash_tree_root must be deterministic (regression against non-idempotent
  // internal caches or state carried between calls).
  bytes32_t root_a = {0};
  bytes32_t root_b = {0};
  ssz_hash_tree_root(bootstrap, root_a);
  ssz_hash_tree_root(bootstrap, root_b);
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(root_a, root_b, 32, "hash_tree_root must be deterministic");
}

void test_gloas_lc_header_hash_tree_root(void) {
  // BeaconBlockHeader is fixed-size 112 bytes:
  //   slot (u64) | proposer_index (u64) | parent_root (32) | state_root (32) | body_root (32)
  // Followed by executionBlockHash (32) and executionBranch (11 * 32 = 352).
  // Total: 112 + 32 + 352 = 496 bytes.
  uint8_t buf[496] = {0};
  buf[0]           = 0x2a;     // slot = 42
  buf[8]           = 0x07;     // proposer_index = 7
  memset(buf + 16, 0xaa, 32);  // parent_root
  memset(buf + 48, 0xbb, 32);  // state_root
  memset(buf + 80, 0xcc, 32);  // body_root
  memset(buf + 112, 0xdd, 32); // executionBlockHash
  for (int i = 0; i < 11; i++) {
    memset(buf + 144 + i * 32, (uint8_t) (0xe0 + i), 32);
  }

  const ssz_def_t header_def = SSZ_CONTAINER("LightClientHeader", GLOAS_LIGHT_CLIENT_HEADER);
  ssz_ob_t        header     = {.bytes = bytes(buf, sizeof(buf)), .def = &header_def};

  c4_state_t state = {0};
  TEST_ASSERT_TRUE_MESSAGE(ssz_is_valid(header, true, &state),
                           state.error ? state.error : "Gloas LC header must validate");

  // Verify accessors resolve into the correct byte ranges.
  bytes_t exec_hash = ssz_get(&header, "executionBlockHash").bytes;
  TEST_ASSERT_EQUAL_UINT32(32, exec_hash.len);
  TEST_ASSERT_EQUAL_UINT8(0xdd, exec_hash.data[0]);

  bytes_t branch = ssz_get(&header, "executionBranch").bytes;
  TEST_ASSERT_EQUAL_UINT32(11 * 32, branch.len);

  // hash_tree_root must not crash and must be deterministic.
  bytes32_t root_a = {0};
  bytes32_t root_b = {0};
  ssz_hash_tree_root(header, root_a);
  ssz_hash_tree_root(header, root_b);
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(root_a, root_b, 32, "hash_tree_root must be deterministic");
}

// -----------------------------------------------------------------------------
// Real Glamsterdam-testnet block: full SSZ roundtrip against reference vectors
// -----------------------------------------------------------------------------
//
// `test/data/ssz/block_gloas.ssz` is the SSZ-encoded `SignedBeaconBlock` of a
// real Glamsterdam-testnet block, fetched via its block_root
//   0xf154895ca213a8785468dc6a342f1c6eda6e3c0664408421b9bf8e01ff7ac47f
// (which equals `hash_tree_root(BeaconBlock)`, i.e. the message part of the
// `SignedBeaconBlock`). `block_gloas.json` is the equivalent JSON.
//
// This is the strongest reference vector we currently have: if any of the
// Gloas SSZ definitions (progressive containers, the ExecutionPayloadBid
// layout, or the merkleization for progressive containers) drifts from the
// spec, either `ssz_is_valid`, one of the `ssz_get` accessors, or the
// `hash_tree_root` comparison below will fail.

#ifdef PROVER
void test_gloas_signed_beacon_block_ssz_roundtrip(void) {
  bytes_t data = read_testdata("ssz/block_gloas.ssz");
  TEST_ASSERT_NOT_NULL_MESSAGE(data.data, "ssz/block_gloas.ssz is missing from test data");
  // Sanity check on file size so we notice truncated/replaced fixtures early.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1428, data.len,
                                   "block_gloas.ssz is expected to be 1428 bytes");

  const ssz_def_t* signed_block_def = eth_ssz_type_for_gloas(
      ETH_SSZ_SIGNED_BEACON_BLOCK_CONTAINER, C4_CHAIN_MAINNET);
  TEST_ASSERT_NOT_NULL(signed_block_def);
  TEST_ASSERT_EQUAL_STRING("signedBeaconBlock", signed_block_def->name);

  ssz_ob_t signed_block = {.bytes = data, .def = signed_block_def};

  // Full recursive structural validation (offset tables, list lengths, ...).
  c4_state_t state = {0};
  TEST_ASSERT_TRUE_MESSAGE(ssz_is_valid(signed_block, true, &state),
                           state.error ? state.error
                                       : "Gloas SignedBeaconBlock must validate");

  // block_root = hash_tree_root(BeaconBlock message).
  ssz_ob_t message = ssz_get(&signed_block, "message");
  TEST_ASSERT_NOT_NULL_MESSAGE(message.def, "ssz_get(message) must not fail");

  bytes32_t block_root = {0};
  ssz_hash_tree_root(message, block_root);
  ASSERT_HEX_STRING_EQUAL(
      "0xf154895ca213a8785468dc6a342f1c6eda6e3c0664408421b9bf8e01ff7ac47f",
      block_root, 32,
      "hash_tree_root(BeaconBlock) must match the block_root the block was fetched by");

  // -- Top-level BeaconBlock fields (compared against block_gloas.json) --
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(222963, ssz_get_uint64(&message, "slot"),
                                   "slot mismatch vs JSON");
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(503453, ssz_get_uint64(&message, "proposerIndex"),
                                   "proposer_index mismatch vs JSON");

  bytes_t parent_root = ssz_get(&message, "parentRoot").bytes;
  TEST_ASSERT_EQUAL_UINT32(32, parent_root.len);
  ASSERT_HEX_STRING_EQUAL(
      "0x083e5a8fdd6318914c46b429c9513de1d3dc11b5eeea8945d8ac9a8d41fa6b0b",
      parent_root.data, 32, "parent_root mismatch vs JSON");

  bytes_t state_root = ssz_get(&message, "stateRoot").bytes;
  TEST_ASSERT_EQUAL_UINT32(32, state_root.len);
  ASSERT_HEX_STRING_EQUAL(
      "0x7b2db6afb0e09b2547a2664857df00ea2397e41f454090d108989b630193ccbb",
      state_root.data, 32, "state_root mismatch vs JSON");

  // SignedBeaconBlock.signature at the outer level.
  bytes_t sig = ssz_get(&signed_block, "signature").bytes;
  TEST_ASSERT_EQUAL_UINT32(96, sig.len);
  ASSERT_HEX_STRING_EQUAL(
      "0x94b07360104d6988865d5cabd1ebcba1485eca8e85ced430072582ebae269688"
      "c17d679baf8fce9e9746b4f52b1510211611bd05b807e4c6458b6335552fa40e"
      "702c9d8ca458ef0c1ffdeefca539601abe6f090f6bce0d377a75ad70c79c0e82",
      sig.data, 96, "SignedBeaconBlock.signature mismatch vs JSON");

  // -- BeaconBlockBody accessors (13-field progressive container) --
  ssz_ob_t body = ssz_get(&message, "body");
  TEST_ASSERT_NOT_NULL_MESSAGE(body.def, "ssz_get(body) must not fail");

  bytes_t randao = ssz_get(&body, "randaoReveal").bytes;
  TEST_ASSERT_EQUAL_UINT32(96, randao.len);
  ASSERT_HEX_STRING_EQUAL(
      "0x8cd05bb5a98b0526464b5cfa2a4ca2549043757848482793a9881f04391932f8"
      "2b8b3e31df66cb6689af6a9fe9af129507af2581af5eebb35db239e39b3b0341"
      "b24641952626a464320bec9675e7327ea144177064863966c1c440219678b0ee",
      randao.data, 96, "randao_reveal mismatch vs JSON");

  bytes_t graffiti = ssz_get(&body, "graffiti").bytes;
  TEST_ASSERT_EQUAL_UINT32(32, graffiti.len);
  ASSERT_HEX_STRING_EQUAL(
      "0x6e696d6275732d657269676f6e2d310000000000000000000000000000000000",
      graffiti.data, 32, "graffiti mismatch vs JSON");

  // eth1_data.deposit_count = 0 (empty pre-merge-style anchor).
  ssz_ob_t eth1_data = ssz_get(&body, "eth1Data");
  TEST_ASSERT_NOT_NULL(eth1_data.def);
  TEST_ASSERT_EQUAL_UINT64(0, ssz_get_uint64(&eth1_data, "depositCount"));

  // Progressive lists that are empty in this block.
  TEST_ASSERT_EQUAL_UINT32(0, ssz_len(ssz_get(&body, "proposerSlashings")));
  TEST_ASSERT_EQUAL_UINT32(0, ssz_len(ssz_get(&body, "attesterSlashings")));
  TEST_ASSERT_EQUAL_UINT32(0, ssz_len(ssz_get(&body, "deposits")));
  TEST_ASSERT_EQUAL_UINT32(0, ssz_len(ssz_get(&body, "voluntaryExits")));
  TEST_ASSERT_EQUAL_UINT32(0, ssz_len(ssz_get(&body, "blsToExecutionChanges")));

  // -- signed_execution_payload_bid.message (Gloas ExecutionPayloadBid) --
  //
  // The bid is the new EIP-7732 vehicle for the execution block hash and the
  // sole EL-facing anchor left inside the body. Any layout drift here breaks
  // the Gloas light-client header proof against gindex 2856.
  ssz_ob_t signed_bid = ssz_get(&body, "signedExecutionPayloadBid");
  TEST_ASSERT_NOT_NULL(signed_bid.def);
  ssz_ob_t bid = ssz_get(&signed_bid, "message");
  TEST_ASSERT_NOT_NULL(bid.def);

  TEST_ASSERT_EQUAL_UINT64_MESSAGE(222963, ssz_get_uint64(&bid, "slot"),
                                   "bid.slot mismatch vs JSON");
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(300000000ULL, ssz_get_uint64(&bid, "gasLimit"),
                                   "bid.gas_limit mismatch vs JSON");
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(32, ssz_get_uint64(&bid, "builderIndex"),
                                   "bid.builder_index mismatch vs JSON");
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(289640105ULL, ssz_get_uint64(&bid, "value"),
                                   "bid.value mismatch vs JSON");
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, ssz_get_uint64(&bid, "executionPayment"),
                                   "bid.execution_payment mismatch vs JSON");

  bytes_t bid_parent_block_hash = ssz_get(&bid, "parentBlockHash").bytes;
  TEST_ASSERT_EQUAL_UINT32(32, bid_parent_block_hash.len);
  ASSERT_HEX_STRING_EQUAL(
      "0x4d8994978964090ffc380d07ea6c912369f796a36b5715d4b06ffc5e36bed76f",
      bid_parent_block_hash.data, 32,
      "bid.parent_block_hash mismatch vs JSON (this is the value the Gloas LC "
      "header executionBlockHash proof anchors against, gindex 2856)");

  bytes_t bid_parent_block_root = ssz_get(&bid, "parentBlockRoot").bytes;
  TEST_ASSERT_EQUAL_UINT32(32, bid_parent_block_root.len);
  ASSERT_HEX_STRING_EQUAL(
      "0x083e5a8fdd6318914c46b429c9513de1d3dc11b5eeea8945d8ac9a8d41fa6b0b",
      bid_parent_block_root.data, 32, "bid.parent_block_root mismatch vs JSON");
  // Cross-check: bid.parent_block_root == BeaconBlock.parent_root.
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(
      parent_root.data, bid_parent_block_root.data, 32,
      "bid.parent_block_root must equal BeaconBlock.parent_root");

  bytes_t bid_block_hash = ssz_get(&bid, "blockHash").bytes;
  TEST_ASSERT_EQUAL_UINT32(32, bid_block_hash.len);
  ASSERT_HEX_STRING_EQUAL(
      "0x7461c15a2e403ec46304b487b67d35782c82a0011cecdf5ec187e7556dbc6742",
      bid_block_hash.data, 32, "bid.block_hash mismatch vs JSON");

  bytes_t bid_fee_recipient = ssz_get(&bid, "feeRecipient").bytes;
  TEST_ASSERT_EQUAL_UINT32(20, bid_fee_recipient.len);
  ASSERT_HEX_STRING_EQUAL(
      "0xf97e180c050e5ab072211ad2c213eb5aee4df134",
      bid_fee_recipient.data, 20, "bid.fee_recipient mismatch vs JSON");

  bytes_t bid_exec_reqs_root = ssz_get(&bid, "executionRequestsRoot").bytes;
  TEST_ASSERT_EQUAL_UINT32(32, bid_exec_reqs_root.len);
  ASSERT_HEX_STRING_EQUAL(
      "0x87b69a306c8e430d0857f7c4ac5e27cecffa1108d43c2e5df7388056fea7a423",
      bid_exec_reqs_root.data, 32, "bid.execution_requests_root mismatch vs JSON");

  // Progressive list inside the ProgressiveContainer bid: exactly one KZG commitment.
  ssz_ob_t kzg_commitments = ssz_get(&bid, "blobKzgCommitments");
  TEST_ASSERT_NOT_NULL(kzg_commitments.def);
  TEST_ASSERT_EQUAL_UINT32(1, ssz_len(kzg_commitments));
  ssz_ob_t kzg0 = ssz_at(kzg_commitments, 0);
  TEST_ASSERT_EQUAL_UINT32(48, kzg0.bytes.len);
  ASSERT_HEX_STRING_EQUAL(
      "0x84928f06a090aff94cd08889dc5fb30e4c7194abfa50579a6b55c181ffd4ce37"
      "d9e86dca7cb08a623fd4e870faf5ee99",
      kzg0.bytes.data, 48, "blob_kzg_commitments[0] mismatch vs JSON");

  // -- attestations (progressive list of ProgressiveContainer Attestation) --
  ssz_ob_t attestations = ssz_get(&body, "attestations");
  TEST_ASSERT_NOT_NULL(attestations.def);
  TEST_ASSERT_EQUAL_UINT32(1, ssz_len(attestations));
  ssz_ob_t att = ssz_at(attestations, 0);
  TEST_ASSERT_NOT_NULL(att.def);
  ssz_ob_t att_data = ssz_get(&att, "data");
  TEST_ASSERT_NOT_NULL(att_data.def);
  TEST_ASSERT_EQUAL_UINT64(222962, ssz_get_uint64(&att_data, "slot"));
  TEST_ASSERT_EQUAL_UINT64(0, ssz_get_uint64(&att_data, "index"));
  bytes_t att_bbroot = ssz_get(&att_data, "beaconBlockRoot").bytes;
  ASSERT_HEX_STRING_EQUAL(
      "0x083e5a8fdd6318914c46b429c9513de1d3dc11b5eeea8945d8ac9a8d41fa6b0b",
      att_bbroot.data, 32, "attestations[0].data.beacon_block_root mismatch");
  ssz_ob_t att_target = ssz_get(&att_data, "target");
  TEST_ASSERT_EQUAL_UINT64(6967, ssz_get_uint64(&att_target, "epoch"));
  ssz_ob_t att_source = ssz_get(&att_data, "source");
  TEST_ASSERT_EQUAL_UINT64(6966, ssz_get_uint64(&att_source, "epoch"));

  // -- payload_attestations (Gloas-only progressive list) --
  ssz_ob_t payload_atts = ssz_get(&body, "payloadAttestations");
  TEST_ASSERT_NOT_NULL(payload_atts.def);
  TEST_ASSERT_EQUAL_UINT32(1, ssz_len(payload_atts));
  ssz_ob_t patt      = ssz_at(payload_atts, 0);
  ssz_ob_t patt_data = ssz_get(&patt, "data");
  TEST_ASSERT_NOT_NULL(patt_data.def);
  TEST_ASSERT_EQUAL_UINT64(222962, ssz_get_uint64(&patt_data, "slot"));
  bytes_t patt_bbroot = ssz_get(&patt_data, "beaconBlockRoot").bytes;
  ASSERT_HEX_STRING_EQUAL(
      "0x083e5a8fdd6318914c46b429c9513de1d3dc11b5eeea8945d8ac9a8d41fa6b0b",
      patt_bbroot.data, 32, "payload_attestations[0].data.beacon_block_root mismatch");

  // -- parent_execution_requests: all five progressive lists empty in this block --
  ssz_ob_t parent_exec_reqs = ssz_get(&body, "parentExecutionRequests");
  TEST_ASSERT_NOT_NULL(parent_exec_reqs.def);
  TEST_ASSERT_EQUAL_UINT32(0, ssz_len(ssz_get(&parent_exec_reqs, "deposits")));
  TEST_ASSERT_EQUAL_UINT32(0, ssz_len(ssz_get(&parent_exec_reqs, "withdrawals")));
  TEST_ASSERT_EQUAL_UINT32(0, ssz_len(ssz_get(&parent_exec_reqs, "consolidations")));
  TEST_ASSERT_EQUAL_UINT32(0, ssz_len(ssz_get(&parent_exec_reqs, "builderDeposits")));
  TEST_ASSERT_EQUAL_UINT32(0, ssz_len(ssz_get(&parent_exec_reqs, "builderExits")));

  // hash_tree_root must be deterministic across repeated calls (regression
  // against non-idempotent internal caches).
  bytes32_t block_root_again = {0};
  ssz_hash_tree_root(message, block_root_again);
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(
      block_root, block_root_again, 32,
      "hash_tree_root must be deterministic across repeated calls");

  safe_free(data.data);
}
#endif // PROVER

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_gloas_state_gindexes);
  RUN_TEST(test_gloas_dispatch_lc_update);
  RUN_TEST(test_gloas_dispatch_lc_bootstrap);
  RUN_TEST(test_gloas_dispatch_ssz_type_for_fork);
  RUN_TEST(test_gloas_bootstrap_union_layout);
  RUN_TEST(test_gloas_update_union_layout);
  RUN_TEST(test_gloas_gindex_helpers_pre_gloas_branches);
  RUN_TEST(test_gloas_activation_epoch_still_reserved);
  RUN_TEST(test_all_forks_bootstrap_branch_depths);
#ifdef PROVER
  RUN_TEST(test_gloas_execution_block_hash_gindex);
  RUN_TEST(test_gloas_dispatch_body_container_shape);
  RUN_TEST(test_gloas_signed_beacon_block_ssz_roundtrip);
#endif
  RUN_TEST(test_gloas_lc_header_shape);
  RUN_TEST(test_gloas_lc_bootstrap_shape);
  RUN_TEST(test_gloas_lc_update_shape);
  RUN_TEST(test_gloas_lc_bootstrap_hash_tree_root);
  RUN_TEST(test_gloas_lc_header_hash_tree_root);
  return UNITY_END();
}
