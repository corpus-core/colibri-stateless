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
#include "chains.h"
#include "el_header.h"
#include "unity.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void assert_fork_version(const chain_spec_t* spec, fork_id_t fork,
                                uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) {
  uint8_t version[4] = {0};
  spec->fork_version_func(spec->chain_id, fork, version);
  TEST_ASSERT_EQUAL_HEX8(b0, version[0]);
  TEST_ASSERT_EQUAL_HEX8(b1, version[1]);
  TEST_ASSERT_EQUAL_HEX8(b2, version[2]);
  TEST_ASSERT_EQUAL_HEX8(b3, version[3]);
}

void test_plataberget_genesis_validators_root(void) {
  const uint8_t expected[32] = {
      0xbb, 0x4a, 0x1a, 0x9e, 0x3f, 0x7f, 0x4e, 0x10,
      0xed, 0xcd, 0x73, 0x4e, 0x4a, 0xcc, 0x3b, 0x5f,
      0xfd, 0x4f, 0x83, 0x0e, 0xfe, 0x0a, 0xf2, 0x74,
      0x8f, 0xa4, 0x58, 0xcf, 0xee, 0x5d, 0x26, 0x58};
  bytes32_t actual = {0};

  TEST_ASSERT_TRUE(c4_chain_genesis_validators_root(C4_CHAIN_PLATABERGET, actual));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, actual, 32);
  TEST_ASSERT_FALSE(memcmp(actual, c4_eth_get_chain_spec(C4_CHAIN_SEPOLIA)->genesis_validators_root, 32) == 0);
}

void test_plataberget_fork_versions(void) {
  const chain_spec_t* spec = c4_eth_get_chain_spec(C4_CHAIN_PLATABERGET);
  TEST_ASSERT_NOT_NULL(spec);
  TEST_ASSERT_EQUAL_UINT64(C4_CHAIN_PLATABERGET, spec->chain_id);

  // config.yaml: GENESIS 0x10733183, then +0x10 per fork up to Gloas 0x80733183
  assert_fork_version(spec, C4_FORK_PHASE0, 0x10, 0x73, 0x31, 0x83);
  assert_fork_version(spec, C4_FORK_ALTAIR, 0x20, 0x73, 0x31, 0x83);
  assert_fork_version(spec, C4_FORK_BELLATRIX, 0x30, 0x73, 0x31, 0x83);
  assert_fork_version(spec, C4_FORK_CAPELLA, 0x40, 0x73, 0x31, 0x83);
  assert_fork_version(spec, C4_FORK_DENEB, 0x50, 0x73, 0x31, 0x83);
  assert_fork_version(spec, C4_FORK_ELECTRA, 0x60, 0x73, 0x31, 0x83);
  assert_fork_version(spec, C4_FORK_FULU, 0x70, 0x73, 0x31, 0x83);
  assert_fork_version(spec, C4_FORK_GLOAS, 0x80, 0x73, 0x31, 0x83);
}

void test_plataberget_fork_id_genesis_at_fulu(void) {
  // Altair..Fulu all activate at epoch 0; Gloas at 1536.
  TEST_ASSERT_EQUAL_INT(C4_FORK_FULU, c4_chain_fork_id(C4_CHAIN_PLATABERGET, 0));
  TEST_ASSERT_EQUAL_INT(C4_FORK_FULU, c4_chain_fork_id(C4_CHAIN_PLATABERGET, 1535));
  TEST_ASSERT_EQUAL_INT(C4_FORK_GLOAS, c4_chain_fork_id(C4_CHAIN_PLATABERGET, 1536));
  TEST_ASSERT_EQUAL_INT(C4_FORK_GLOAS, c4_chain_fork_id(C4_CHAIN_PLATABERGET, 2000));
}

void test_plataberget_fork_epochs_schedule(void) {
  const chain_spec_t* spec = c4_eth_get_chain_spec(C4_CHAIN_PLATABERGET);
  TEST_ASSERT_NOT_NULL(spec);

  // fork_epochs is indexed from Altair (fork_id - 1). Pin the table directly:
  // fork_id(epoch) cannot distinguish Fulu=0 from Fulu=1536 because Gloas also
  // activates at 1536.
  TEST_ASSERT_EQUAL_UINT64(0ULL, spec->fork_epochs[C4_FORK_ALTAIR - 1]);
  TEST_ASSERT_EQUAL_UINT64(0ULL, spec->fork_epochs[C4_FORK_BELLATRIX - 1]);
  TEST_ASSERT_EQUAL_UINT64(0ULL, spec->fork_epochs[C4_FORK_CAPELLA - 1]);
  TEST_ASSERT_EQUAL_UINT64(0ULL, spec->fork_epochs[C4_FORK_DENEB - 1]);
  TEST_ASSERT_EQUAL_UINT64(0ULL, spec->fork_epochs[C4_FORK_ELECTRA - 1]);
  TEST_ASSERT_EQUAL_UINT64(0ULL, spec->fork_epochs[C4_FORK_FULU - 1]);
  TEST_ASSERT_EQUAL_UINT64(1536ULL, spec->fork_epochs[C4_FORK_GLOAS - 1]);
  TEST_ASSERT_EQUAL_UINT64(0xfffffffffffffffeULL, spec->fork_epochs[C4_FORK_GLOAS]);
}

void test_plataberget_gloas_gindexes_reachable(void) {
  const chain_spec_t* spec = c4_eth_get_chain_spec(C4_CHAIN_PLATABERGET);
  TEST_ASSERT_NOT_NULL(spec);

  uint64_t fulu_slot  = slot_for_epoch(0ULL, spec);
  uint64_t gloas_slot = slot_for_epoch(1536ULL, spec);

  // Fulu keeps the Electra state layout for these fields.
  TEST_ASSERT_EQUAL_UINT64(86, c4_current_sync_committee_gindex(C4_CHAIN_PLATABERGET, fulu_slot));
  TEST_ASSERT_EQUAL_UINT64(87, c4_next_sync_committee_gindex(C4_CHAIN_PLATABERGET, fulu_slot));
  TEST_ASSERT_EQUAL_UINT64(169, c4_finalized_root_gindex(C4_CHAIN_PLATABERGET, fulu_slot));

  TEST_ASSERT_EQUAL_UINT64(2945, c4_current_sync_committee_gindex(C4_CHAIN_PLATABERGET, gloas_slot));
  TEST_ASSERT_EQUAL_UINT64(2946, c4_next_sync_committee_gindex(C4_CHAIN_PLATABERGET, gloas_slot));
  TEST_ASSERT_EQUAL_UINT64(735, c4_finalized_root_gindex(C4_CHAIN_PLATABERGET, gloas_slot));
}

void test_fork_id_epoch_zero_still_phase0_on_public_networks(void) {
  TEST_ASSERT_EQUAL_INT(C4_FORK_PHASE0, c4_chain_fork_id(C4_CHAIN_MAINNET, 0));
  TEST_ASSERT_EQUAL_INT(C4_FORK_PHASE0, c4_chain_fork_id(C4_CHAIN_SEPOLIA, 0));
  TEST_ASSERT_EQUAL_INT(C4_FORK_PHASE0, c4_chain_fork_id(C4_CHAIN_GNOSIS, 0));
  TEST_ASSERT_EQUAL_INT(C4_FORK_PHASE0, c4_chain_fork_id(C4_CHAIN_GNOSIS_CHIADO, 0));
}

void test_sepolia_fork_schedule_unchanged(void) {
  TEST_ASSERT_EQUAL_INT(C4_FORK_ALTAIR, c4_chain_fork_id(C4_CHAIN_SEPOLIA, 50));
  TEST_ASSERT_EQUAL_INT(C4_FORK_BELLATRIX, c4_chain_fork_id(C4_CHAIN_SEPOLIA, 100));
  TEST_ASSERT_EQUAL_INT(C4_FORK_FULU, c4_chain_fork_id(C4_CHAIN_SEPOLIA, 272640));
  TEST_ASSERT_EQUAL_INT(C4_FORK_GLOAS, c4_chain_fork_id(C4_CHAIN_SEPOLIA, 0xffffffffffffffffULL));

  const chain_spec_t* spec = c4_eth_get_chain_spec(C4_CHAIN_SEPOLIA);
  TEST_ASSERT_NOT_NULL(spec);
  assert_fork_version(spec, C4_FORK_ALTAIR, 0x90, 0x00, 0x00, 0x70);
  assert_fork_version(spec, C4_FORK_FULU, 0x90, 0x00, 0x00, 0x75);
}

void test_mainnet_gloas_still_unassigned(void) {
  TEST_ASSERT_EQUAL_INT(C4_FORK_FULU, c4_chain_fork_id(C4_CHAIN_MAINNET, 411392));
  TEST_ASSERT_EQUAL_INT(C4_FORK_GLOAS, c4_chain_fork_id(C4_CHAIN_MAINNET, 0xffffffffffffffffULL));
}

// Pins the rotated recursive ZK trust anchors (current_keys_root of the first
// post-rotation proof: mainnet 1845, sepolia 1348, gnosis 3643). A silent
// change here would make every ZK sync proof verify against the wrong committee.
void test_zk_sync_trust_anchors(void) {
  const uint8_t mainnet[32] = {
      0xc6, 0x10, 0xd3, 0xcf, 0x3f, 0xf6, 0xf4, 0x02,
      0x48, 0xad, 0xe8, 0x12, 0xe5, 0x70, 0x85, 0x7e,
      0x74, 0x12, 0xaf, 0x35, 0x45, 0xcb, 0xee, 0x91,
      0x75, 0xcf, 0x54, 0xcc, 0xcf, 0xa2, 0x21, 0x3c};
  const uint8_t sepolia[32] = {
      0xee, 0x5c, 0x88, 0x0d, 0x52, 0x41, 0x66, 0xb4,
      0xb1, 0xd3, 0xed, 0xda, 0xba, 0xea, 0xcb, 0x3f,
      0xdf, 0x1e, 0x40, 0xc9, 0x00, 0x8f, 0x25, 0x6e,
      0x35, 0x7e, 0x72, 0x2d, 0x80, 0xba, 0x97, 0x25};
  const uint8_t gnosis[32] = {
      0x19, 0x97, 0x24, 0x9f, 0x4d, 0xd2, 0xf3, 0x66,
      0x53, 0x05, 0x2f, 0x43, 0x8c, 0xe4, 0x80, 0x9a,
      0x2d, 0xb7, 0xfa, 0xb8, 0xa3, 0x3f, 0x49, 0xc2,
      0x2f, 0x61, 0x32, 0xd2, 0xa1, 0x07, 0xb8, 0xe0};

  const chain_spec_t* m = c4_eth_get_chain_spec(C4_CHAIN_MAINNET);
  const chain_spec_t* s = c4_eth_get_chain_spec(C4_CHAIN_SEPOLIA);
  const chain_spec_t* g = c4_eth_get_chain_spec(C4_CHAIN_GNOSIS);
  TEST_ASSERT_NOT_NULL(m);
  TEST_ASSERT_NOT_NULL(s);
  TEST_ASSERT_NOT_NULL(g);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(mainnet, m->zk_sync_keys_root, 32);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(sepolia, s->zk_sync_keys_root, 32);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(gnosis, g->zk_sync_keys_root, 32);
}

void test_chain_schedules_fork(void) {
  TEST_ASSERT_TRUE(c4_chain_schedules_fork(C4_CHAIN_PLATABERGET, C4_FORK_GLOAS));
  TEST_ASSERT_TRUE(c4_chain_schedules_fork(C4_CHAIN_PLATABERGET, C4_FORK_FULU));
  TEST_ASSERT_FALSE(c4_chain_schedules_fork(C4_CHAIN_MAINNET, C4_FORK_GLOAS));
  TEST_ASSERT_FALSE(c4_chain_schedules_fork(C4_CHAIN_SEPOLIA, C4_FORK_GLOAS));
  TEST_ASSERT_TRUE(c4_chain_schedules_fork(C4_CHAIN_MAINNET, C4_FORK_FULU));
  TEST_ASSERT_FALSE(c4_chain_schedules_fork(C4_CHAIN_MAINNET, C4_FORK_PHASE0));
  TEST_ASSERT_FALSE(c4_chain_schedules_fork(CHAIN(999999), C4_FORK_GLOAS));
}

// EIP-7892 BLOB_BASE_FEE_UPDATE_FRACTION per active fork; the timestamps come
// from go-ethereum's `params/config.go` (mainnet + sepolia). Pre-Cancun input
// as well as chains without a schedule fall back to the Cancun value.
void test_blob_base_fee_update_fraction(void) {
  // Mainnet: probe each fork boundary (activation-1 vs. activation).
  TEST_ASSERT_EQUAL_UINT64(3338477ULL,  eth_blob_base_fee_update_fraction(C4_CHAIN_MAINNET, 0));                    // pre-Cancun
  TEST_ASSERT_EQUAL_UINT64(3338477ULL,  eth_blob_base_fee_update_fraction(C4_CHAIN_MAINNET, 1710338134ULL));        // 1s before Cancun
  TEST_ASSERT_EQUAL_UINT64(3338477ULL,  eth_blob_base_fee_update_fraction(C4_CHAIN_MAINNET, 1710338135ULL));        // Cancun activation
  TEST_ASSERT_EQUAL_UINT64(5007716ULL,  eth_blob_base_fee_update_fraction(C4_CHAIN_MAINNET, 1746612311ULL));        // Prague
  TEST_ASSERT_EQUAL_UINT64(5007716ULL,  eth_blob_base_fee_update_fraction(C4_CHAIN_MAINNET, 1764798551ULL));        // Fusaka/Osaka (unchanged from Prague)
  TEST_ASSERT_EQUAL_UINT64(8346193ULL,  eth_blob_base_fee_update_fraction(C4_CHAIN_MAINNET, 1765290071ULL));        // BPO1
  TEST_ASSERT_EQUAL_UINT64(8346193ULL,  eth_blob_base_fee_update_fraction(C4_CHAIN_MAINNET, 1767747670ULL));        // 1s before BPO2 → still BPO1
  TEST_ASSERT_EQUAL_UINT64(11684671ULL, eth_blob_base_fee_update_fraction(C4_CHAIN_MAINNET, 1767747671ULL));        // BPO2
  TEST_ASSERT_EQUAL_UINT64(11684671ULL, eth_blob_base_fee_update_fraction(C4_CHAIN_MAINNET, 0xffffffffULL));        // far future stays at newest known

  // Sepolia: quick smoke test.
  TEST_ASSERT_EQUAL_UINT64(3338477ULL,  eth_blob_base_fee_update_fraction(C4_CHAIN_SEPOLIA, 1706655072ULL));        // Cancun
  TEST_ASSERT_EQUAL_UINT64(11684671ULL, eth_blob_base_fee_update_fraction(C4_CHAIN_SEPOLIA, 1761607008ULL));        // BPO2

  // Chains without a populated blob schedule (e.g. Plataberget, Gnosis) fall
  // back to the Cancun default regardless of timestamp.
  TEST_ASSERT_EQUAL_UINT64(3338477ULL, eth_blob_base_fee_update_fraction(C4_CHAIN_PLATABERGET, 1767747671ULL));
  TEST_ASSERT_EQUAL_UINT64(3338477ULL, eth_blob_base_fee_update_fraction(C4_CHAIN_GNOSIS, 1767747671ULL));
  TEST_ASSERT_EQUAL_UINT64(3338477ULL, eth_blob_base_fee_update_fraction(CHAIN(999999), 1767747671ULL));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_plataberget_genesis_validators_root);
  RUN_TEST(test_plataberget_fork_versions);
  RUN_TEST(test_plataberget_fork_id_genesis_at_fulu);
  RUN_TEST(test_plataberget_fork_epochs_schedule);
  RUN_TEST(test_plataberget_gloas_gindexes_reachable);
  RUN_TEST(test_fork_id_epoch_zero_still_phase0_on_public_networks);
  RUN_TEST(test_sepolia_fork_schedule_unchanged);
  RUN_TEST(test_mainnet_gloas_still_unassigned);
  RUN_TEST(test_zk_sync_trust_anchors);
  RUN_TEST(test_chain_schedules_fork);
  RUN_TEST(test_blob_base_fee_update_fraction);
  return UNITY_END();
}
