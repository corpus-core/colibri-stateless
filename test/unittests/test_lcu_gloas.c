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

// Unit tests for src/chains/eth/prover/lcu_gloas.c -- specifically the
// synchronous helpers (SSZ assembly + Beacon-API wire wrapper). The async
// orchestrator c4_create_gloas_lcu needs Lodestar state-proof fixtures and
// is exercised separately once those are captured.

#include "beacon_types.h"
#include "bytes.h"
#include "c4_assert.h"
#include "lcu_gloas.h"
#include "ssz.h"
#include "unity.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

// BeaconBlockHeader: slot(8) + proposerIndex(8) + parentRoot(32) + stateRoot(32) + bodyRoot(32)
static void build_dummy_cl_header(uint8_t       out[112],
                                  uint64_t      slot,
                                  uint64_t      proposer_index,
                                  const uint8_t parent_root[32],
                                  const uint8_t state_root[32],
                                  const uint8_t body_root[32]) {
  memcpy(out + 0, &slot, 8);
  memcpy(out + 8, &proposer_index, 8);
  memcpy(out + 16, parent_root, 32);
  memcpy(out + 48, state_root, 32);
  memcpy(out + 80, body_root, 32);
}

// Compose a 496-byte GLOAS_LIGHT_CLIENT_HEADER directly (mirror of the internal
// compose_gloas_lc_header helper).
static void build_dummy_lc_header(uint8_t out[C4_GLOAS_LCU_HEADER_SIZE],
                                  uint8_t seed) {
  uint8_t parent_root[32] = {0}, state_root[32] = {0}, body_root[32] = {0};
  memset(parent_root, seed, 32);
  memset(state_root, (uint8_t) (seed + 1), 32);
  memset(body_root, (uint8_t) (seed + 2), 32);
  build_dummy_cl_header(out, /* slot */ 0x100 + seed, /* proposer_index */ seed,
                        parent_root, state_root, body_root);
  // executionBlockHash (32) + executionBranch (11 * 32)
  memset(out + 112, (uint8_t) (seed + 0x40), 32);
  for (uint32_t i = 0; i < 11; i++)
    memset(out + 112 + 32 + i * 32, (uint8_t) (seed + 0x50 + i), 32);
}

// SyncAggregate: BitVector[512] (64 B) + ByteVector[96] = 160 B fixed.
static void build_dummy_sync_aggregate(uint8_t out[C4_GLOAS_LCU_SYNC_AGGREGATE_SIZE]) {
  memset(out, 0, C4_GLOAS_LCU_SYNC_AGGREGATE_SIZE);
  for (uint32_t i = 0; i < 64; i++) out[i] = (uint8_t) (0xa0 + i);       // bits
  for (uint32_t i = 0; i < 96; i++) out[64 + i] = (uint8_t) (0xb0 + i);  // signature
}

// -----------------------------------------------------------------------------
// Fixed-size SSZ assembly
// -----------------------------------------------------------------------------

void test_gloas_lcu_assemble_layout_and_validity(void) {
  uint8_t attested_header[C4_GLOAS_LCU_HEADER_SIZE]  = {0};
  uint8_t finalized_header[C4_GLOAS_LCU_HEADER_SIZE] = {0};
  build_dummy_lc_header(attested_header, /* seed */ 0x11);
  build_dummy_lc_header(finalized_header, /* seed */ 0x22);

  uint8_t next_sync_committee[C4_GLOAS_LCU_SYNC_COMMITTEE_SIZE] = {0};
  // Fill each pubkey with a deterministic pattern (only bytes[0..48) are
  // meaningful; ssz_is_valid on Vector[Pubkey48, 512] does not enforce
  // padding, so any bytes are fine here).
  for (uint32_t i = 0; i < 512; i++) {
    for (uint32_t j = 0; j < 48; j++)
      next_sync_committee[i * 48 + j] = (uint8_t) ((i + j) & 0xff);
  }
  for (uint32_t j = 0; j < 48; j++)
    next_sync_committee[24576 + j] = (uint8_t) (0xa0 + j);

  uint8_t next_sc_branch[C4_GLOAS_LCU_NEXT_SC_BRANCH_SIZE] = {0};
  for (uint32_t i = 0; i < 11; i++)
    memset(next_sc_branch + i * 32, (uint8_t) (0x30 + i), 32);

  uint8_t finality_branch[C4_GLOAS_LCU_FINALITY_BRANCH_SIZE] = {0};
  for (uint32_t i = 0; i < 9; i++)
    memset(finality_branch + i * 32, (uint8_t) (0x40 + i), 32);

  uint8_t sync_aggregate[C4_GLOAS_LCU_SYNC_AGGREGATE_SIZE] = {0};
  build_dummy_sync_aggregate(sync_aggregate);

  const uint64_t signature_slot = 0x0102030405060708ULL;

  bytes_t lcu = NULL_BYTES;
  bool    ok  = c4_gloas_lcu_assemble(
      bytes(attested_header, sizeof(attested_header)),
      bytes(next_sync_committee, sizeof(next_sync_committee)),
      bytes(next_sc_branch, sizeof(next_sc_branch)),
      bytes(finalized_header, sizeof(finalized_header)),
      bytes(finality_branch, sizeof(finality_branch)),
      bytes(sync_aggregate, sizeof(sync_aggregate)),
      signature_slot,
      &lcu);
  TEST_ASSERT_TRUE_MESSAGE(ok, "assemble must succeed with correctly-sized inputs");
  TEST_ASSERT_EQUAL_MESSAGE(C4_GLOAS_LCU_SSZ_SIZE, lcu.len,
                            "assembled LCU is 26424 bytes fixed");

  // Layout check: contiguous fixed-size concatenation, no offsets/gaps.
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(attested_header,
                                        lcu.data + 0, C4_GLOAS_LCU_HEADER_SIZE,
                                        "attestedHeader at offset 0");
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(next_sync_committee,
                                        lcu.data + 496, C4_GLOAS_LCU_SYNC_COMMITTEE_SIZE,
                                        "nextSyncCommittee at offset 496");
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(next_sc_branch,
                                        lcu.data + 25120, C4_GLOAS_LCU_NEXT_SC_BRANCH_SIZE,
                                        "nextSyncCommitteeBranch at offset 25120");
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(finalized_header,
                                        lcu.data + 25472, C4_GLOAS_LCU_HEADER_SIZE,
                                        "finalizedHeader at offset 25472");
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(finality_branch,
                                        lcu.data + 25968, C4_GLOAS_LCU_FINALITY_BRANCH_SIZE,
                                        "finalityBranch at offset 25968");
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(sync_aggregate,
                                        lcu.data + 26256, C4_GLOAS_LCU_SYNC_AGGREGATE_SIZE,
                                        "syncAggregate at offset 26256");

  // signatureSlot: little-endian encoding of the uint64.
  for (int i = 0; i < 8; i++) {
    uint8_t expected = (uint8_t) ((signature_slot >> (8 * i)) & 0xff);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(expected, lcu.data[26416 + i],
                                    "signatureSlot LE byte mismatch");
  }

  // SSZ validation against the canonical GLOAS_LIGHT_CLIENT_UPDATE def.
  const ssz_def_t* def = eth_get_light_client_update(C4_FORK_GLOAS);
  TEST_ASSERT_NOT_NULL_MESSAGE(def, "GLOAS_LIGHT_CLIENT_UPDATE def must be defined");
  ssz_ob_t   ob    = {.def = def, .bytes = lcu};
  c4_state_t state = {0};
  bool       valid = ssz_is_valid(ob, true, &state);
  if (!valid && state.error) {
    TEST_MESSAGE(state.error);
  }
  TEST_ASSERT_TRUE_MESSAGE(valid, "assembled bytes must validate against Gloas LCU SSZ def");
  c4_state_free(&state);

  // Extra: signatureSlot readable via SSZ accessor.
  TEST_ASSERT_EQUAL_UINT64(signature_slot, ssz_get_uint64(&ob, "signatureSlot"));

  safe_free(lcu.data);
}

void test_gloas_lcu_assemble_size_mismatches_rejected(void) {
  uint8_t attested_ok[C4_GLOAS_LCU_HEADER_SIZE]           = {0};
  uint8_t finalized_ok[C4_GLOAS_LCU_HEADER_SIZE]          = {0};
  uint8_t nsc_ok[C4_GLOAS_LCU_SYNC_COMMITTEE_SIZE]        = {0};
  uint8_t sc_branch_ok[C4_GLOAS_LCU_NEXT_SC_BRANCH_SIZE]  = {0};
  uint8_t fin_branch_ok[C4_GLOAS_LCU_FINALITY_BRANCH_SIZE] = {0};
  uint8_t sync_agg_ok[C4_GLOAS_LCU_SYNC_AGGREGATE_SIZE]   = {0};

  bytes_t out = NULL_BYTES;

  // Short attested_header.
  uint8_t attested_short[C4_GLOAS_LCU_HEADER_SIZE - 1] = {0};
  TEST_ASSERT_FALSE_MESSAGE(
      c4_gloas_lcu_assemble(
          bytes(attested_short, sizeof(attested_short)),
          bytes(nsc_ok, sizeof(nsc_ok)),
          bytes(sc_branch_ok, sizeof(sc_branch_ok)),
          bytes(finalized_ok, sizeof(finalized_ok)),
          bytes(fin_branch_ok, sizeof(fin_branch_ok)),
          bytes(sync_agg_ok, sizeof(sync_agg_ok)),
          0,
          &out),
      "short attested_header must be rejected");
  TEST_ASSERT_NULL(out.data);

  // Short nextSyncCommittee.
  uint8_t nsc_short[C4_GLOAS_LCU_SYNC_COMMITTEE_SIZE - 1] = {0};
  TEST_ASSERT_FALSE_MESSAGE(
      c4_gloas_lcu_assemble(
          bytes(attested_ok, sizeof(attested_ok)),
          bytes(nsc_short, sizeof(nsc_short)),
          bytes(sc_branch_ok, sizeof(sc_branch_ok)),
          bytes(finalized_ok, sizeof(finalized_ok)),
          bytes(fin_branch_ok, sizeof(fin_branch_ok)),
          bytes(sync_agg_ok, sizeof(sync_agg_ok)),
          0,
          &out),
      "short nextSyncCommittee must be rejected");
  TEST_ASSERT_NULL(out.data);

  // Short nextSyncCommitteeBranch.
  uint8_t sc_branch_short[C4_GLOAS_LCU_NEXT_SC_BRANCH_SIZE - 1] = {0};
  TEST_ASSERT_FALSE_MESSAGE(
      c4_gloas_lcu_assemble(
          bytes(attested_ok, sizeof(attested_ok)),
          bytes(nsc_ok, sizeof(nsc_ok)),
          bytes(sc_branch_short, sizeof(sc_branch_short)),
          bytes(finalized_ok, sizeof(finalized_ok)),
          bytes(fin_branch_ok, sizeof(fin_branch_ok)),
          bytes(sync_agg_ok, sizeof(sync_agg_ok)),
          0,
          &out),
      "short nextSyncCommitteeBranch must be rejected");
  TEST_ASSERT_NULL(out.data);

  // Short finalized_header.
  uint8_t finalized_short[C4_GLOAS_LCU_HEADER_SIZE - 1] = {0};
  TEST_ASSERT_FALSE_MESSAGE(
      c4_gloas_lcu_assemble(
          bytes(attested_ok, sizeof(attested_ok)),
          bytes(nsc_ok, sizeof(nsc_ok)),
          bytes(sc_branch_ok, sizeof(sc_branch_ok)),
          bytes(finalized_short, sizeof(finalized_short)),
          bytes(fin_branch_ok, sizeof(fin_branch_ok)),
          bytes(sync_agg_ok, sizeof(sync_agg_ok)),
          0,
          &out),
      "short finalized_header must be rejected");
  TEST_ASSERT_NULL(out.data);

  // Short finalityBranch.
  uint8_t fin_branch_short[C4_GLOAS_LCU_FINALITY_BRANCH_SIZE - 1] = {0};
  TEST_ASSERT_FALSE_MESSAGE(
      c4_gloas_lcu_assemble(
          bytes(attested_ok, sizeof(attested_ok)),
          bytes(nsc_ok, sizeof(nsc_ok)),
          bytes(sc_branch_ok, sizeof(sc_branch_ok)),
          bytes(finalized_ok, sizeof(finalized_ok)),
          bytes(fin_branch_short, sizeof(fin_branch_short)),
          bytes(sync_agg_ok, sizeof(sync_agg_ok)),
          0,
          &out),
      "short finalityBranch must be rejected");
  TEST_ASSERT_NULL(out.data);

  // Short syncAggregate.
  uint8_t sync_agg_short[C4_GLOAS_LCU_SYNC_AGGREGATE_SIZE - 1] = {0};
  TEST_ASSERT_FALSE_MESSAGE(
      c4_gloas_lcu_assemble(
          bytes(attested_ok, sizeof(attested_ok)),
          bytes(nsc_ok, sizeof(nsc_ok)),
          bytes(sc_branch_ok, sizeof(sc_branch_ok)),
          bytes(finalized_ok, sizeof(finalized_ok)),
          bytes(fin_branch_ok, sizeof(fin_branch_ok)),
          bytes(sync_agg_short, sizeof(sync_agg_short)),
          0,
          &out),
      "short syncAggregate must be rejected");
  TEST_ASSERT_NULL(out.data);

  // NULL out_ssz.
  TEST_ASSERT_FALSE_MESSAGE(
      c4_gloas_lcu_assemble(
          bytes(attested_ok, sizeof(attested_ok)),
          bytes(nsc_ok, sizeof(nsc_ok)),
          bytes(sc_branch_ok, sizeof(sc_branch_ok)),
          bytes(finalized_ok, sizeof(finalized_ok)),
          bytes(fin_branch_ok, sizeof(fin_branch_ok)),
          bytes(sync_agg_ok, sizeof(sync_agg_ok)),
          0,
          NULL),
      "NULL out_ssz must be rejected");

  // NULL data pointer with correct length must be rejected (defense in depth).
  bytes_t null_attested = {.data = NULL, .len = C4_GLOAS_LCU_HEADER_SIZE};
  TEST_ASSERT_FALSE_MESSAGE(
      c4_gloas_lcu_assemble(
          null_attested,
          bytes(nsc_ok, sizeof(nsc_ok)),
          bytes(sc_branch_ok, sizeof(sc_branch_ok)),
          bytes(finalized_ok, sizeof(finalized_ok)),
          bytes(fin_branch_ok, sizeof(fin_branch_ok)),
          bytes(sync_agg_ok, sizeof(sync_agg_ok)),
          0,
          &out),
      "NULL data pointer must be rejected");
  TEST_ASSERT_NULL(out.data);
}

// -----------------------------------------------------------------------------
// Beacon-API wire wrapper
// -----------------------------------------------------------------------------

void test_gloas_lcu_wrap_beacon_response_prefix(void) {
  // Build a synthetic LCU payload -- the wrapper does not inspect the body,
  // only sizes.
  uint8_t lcu_bytes[C4_GLOAS_LCU_SSZ_SIZE] = {0};
  for (uint32_t i = 0; i < C4_GLOAS_LCU_SSZ_SIZE; i++)
    lcu_bytes[i] = (uint8_t) (i & 0xff);

  // Arbitrary 4-byte fork_version. The wrapper copies these bytes verbatim
  // and never inspects them; the concrete Gloas value depends on the chain
  // (`mainnet_fork_version` -> 0x07000000, plataberget -> 0x80733183, ...).
  uint8_t fork_version[4] = {0xde, 0xad, 0xbe, 0xef};
  bytes_t wire            = c4_gloas_lcu_wrap_beacon_response(
      bytes(lcu_bytes, sizeof(lcu_bytes)), fork_version);
  TEST_ASSERT_NOT_NULL_MESSAGE(wire.data, "wrap must produce a buffer");
  TEST_ASSERT_EQUAL_MESSAGE(C4_GLOAS_LCU_WIRE_SIZE, wire.len,
                            "wire response = 12 + LCU_SSZ_SIZE bytes");

  // length = 4 + 26424 = 26428 encoded LE.
  uint64_t expected_length = 4ULL + (uint64_t) C4_GLOAS_LCU_SSZ_SIZE;
  for (int i = 0; i < 8; i++) {
    uint8_t expected = (uint8_t) ((expected_length >> (8 * i)) & 0xff);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(expected, wire.data[i],
                                    "length prefix LE byte mismatch");
  }
  // fork_version at offset 8..12.
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(fork_version, wire.data + 8, 4,
                                        "fork_version at offset 8");
  // Payload starts at offset 12.
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(
      lcu_bytes, wire.data + C4_GLOAS_LCU_WIRE_PREFIX_SIZE,
      C4_GLOAS_LCU_SSZ_SIZE, "payload copied verbatim at offset 12");

  safe_free(wire.data);
}

void test_gloas_lcu_wrap_beacon_response_rejects_invalid(void) {
  uint8_t lcu_bytes[C4_GLOAS_LCU_SSZ_SIZE] = {0};
  uint8_t fork_version[4]                  = {0xde, 0xad, 0xbe, 0xef};

  // NULL data pointer.
  bytes_t null_input = {.data = NULL, .len = C4_GLOAS_LCU_SSZ_SIZE};
  bytes_t r          = c4_gloas_lcu_wrap_beacon_response(null_input, fork_version);
  TEST_ASSERT_NULL_MESSAGE(r.data, "NULL data pointer must be rejected");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, r.len, "returned bytes_t must be zero on failure");

  // Wrong LCU length -> NULL_BYTES.
  bytes_t short_lcu = {.data = lcu_bytes, .len = C4_GLOAS_LCU_SSZ_SIZE - 1};
  r                 = c4_gloas_lcu_wrap_beacon_response(short_lcu, fork_version);
  TEST_ASSERT_NULL_MESSAGE(r.data, "wrong LCU length must be rejected");
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_gloas_lcu_assemble_layout_and_validity);
  RUN_TEST(test_gloas_lcu_assemble_size_mismatches_rejected);
  RUN_TEST(test_gloas_lcu_wrap_beacon_response_prefix);
  RUN_TEST(test_gloas_lcu_wrap_beacon_response_rejects_invalid);
  return UNITY_END();
}
