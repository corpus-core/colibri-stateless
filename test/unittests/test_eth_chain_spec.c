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
#include "chains.h"
#include "crypto.h"
#include "el_header.h"
#include "state.h"
#include "sync_committee.h"
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
  TEST_ASSERT_EQUAL_UINT64(3338477ULL, eth_blob_base_fee_update_fraction(C4_CHAIN_MAINNET, 0));              // pre-Cancun
  TEST_ASSERT_EQUAL_UINT64(3338477ULL, eth_blob_base_fee_update_fraction(C4_CHAIN_MAINNET, 1710338134ULL));  // 1s before Cancun
  TEST_ASSERT_EQUAL_UINT64(3338477ULL, eth_blob_base_fee_update_fraction(C4_CHAIN_MAINNET, 1710338135ULL));  // Cancun activation
  TEST_ASSERT_EQUAL_UINT64(5007716ULL, eth_blob_base_fee_update_fraction(C4_CHAIN_MAINNET, 1746612311ULL));  // Prague
  TEST_ASSERT_EQUAL_UINT64(5007716ULL, eth_blob_base_fee_update_fraction(C4_CHAIN_MAINNET, 1764798551ULL));  // Fusaka/Osaka (unchanged from Prague)
  TEST_ASSERT_EQUAL_UINT64(8346193ULL, eth_blob_base_fee_update_fraction(C4_CHAIN_MAINNET, 1765290071ULL));  // BPO1
  TEST_ASSERT_EQUAL_UINT64(8346193ULL, eth_blob_base_fee_update_fraction(C4_CHAIN_MAINNET, 1767747670ULL));  // 1s before BPO2 → still BPO1
  TEST_ASSERT_EQUAL_UINT64(11684671ULL, eth_blob_base_fee_update_fraction(C4_CHAIN_MAINNET, 1767747671ULL)); // BPO2
  TEST_ASSERT_EQUAL_UINT64(11684671ULL, eth_blob_base_fee_update_fraction(C4_CHAIN_MAINNET, 0xffffffffULL)); // far future stays at newest known

  // Sepolia: quick smoke test.
  TEST_ASSERT_EQUAL_UINT64(3338477ULL, eth_blob_base_fee_update_fraction(C4_CHAIN_SEPOLIA, 1706655072ULL));  // Cancun
  TEST_ASSERT_EQUAL_UINT64(11684671ULL, eth_blob_base_fee_update_fraction(C4_CHAIN_SEPOLIA, 1761607008ULL)); // BPO2

  // Gnosis / Chiado: single flat update fraction across all forks, no BPO.
  TEST_ASSERT_EQUAL_UINT64(1112826ULL, eth_blob_base_fee_update_fraction(C4_CHAIN_GNOSIS, 1710181820ULL));        // Gnosis Cancun
  TEST_ASSERT_EQUAL_UINT64(1112826ULL, eth_blob_base_fee_update_fraction(C4_CHAIN_GNOSIS, 1776168380ULL));        // Gnosis Osaka
  TEST_ASSERT_EQUAL_UINT64(3338477ULL, eth_blob_base_fee_update_fraction(C4_CHAIN_GNOSIS, 1710181819ULL));        // pre-Cancun → default
  TEST_ASSERT_EQUAL_UINT64(1112826ULL, eth_blob_base_fee_update_fraction(C4_CHAIN_GNOSIS_CHIADO, 1706724940ULL)); // Chiado Cancun

  // Chains without a populated blob schedule (e.g. Plataberget) fall back to
  // the Cancun default regardless of timestamp.
  TEST_ASSERT_EQUAL_UINT64(3338477ULL, eth_blob_base_fee_update_fraction(C4_CHAIN_PLATABERGET, 1767747671ULL));
  TEST_ASSERT_EQUAL_UINT64(3338477ULL, eth_blob_base_fee_update_fraction(CHAIN(999999), 1767747671ULL));
}

// MIN_BLOB_BASE_FEE differs between Ethereum (1 wei) and Gnosis (1 gwei); the
// chain_spec_t override propagates through `eth_min_blob_base_fee`.
void test_min_blob_base_fee(void) {
  TEST_ASSERT_EQUAL_UINT64(1ULL, eth_min_blob_base_fee(C4_CHAIN_MAINNET));
  TEST_ASSERT_EQUAL_UINT64(1ULL, eth_min_blob_base_fee(C4_CHAIN_SEPOLIA));
  TEST_ASSERT_EQUAL_UINT64(1000000000ULL, eth_min_blob_base_fee(C4_CHAIN_GNOSIS));
  TEST_ASSERT_EQUAL_UINT64(1000000000ULL, eth_min_blob_base_fee(C4_CHAIN_GNOSIS_CHIADO));
  TEST_ASSERT_EQUAL_UINT64(1ULL, eth_min_blob_base_fee(C4_CHAIN_PLATABERGET));
  TEST_ASSERT_EQUAL_UINT64(1ULL, eth_min_blob_base_fee(CHAIN(999999)));
}

static void xor_blob_mask(uint8_t out[4], const uint8_t base[32], uint64_t epoch, uint64_t max_blobs) {
  uint8_t   blob_in[16] = {0};
  bytes32_t mask        = {0};
  uint64_to_le(blob_in, epoch);
  uint64_to_le(blob_in + 8, max_blobs);
  sha256(bytes(blob_in, 16), mask);
  for (int i = 0; i < 4; i++)
    out[i] = (uint8_t) (base[i] ^ mask[i]);
}

void test_fork_digest_differs_across_forks(void) {
  uint8_t electra[4] = {0}, fulu[4] = {0}, plat_fulu[4] = {0}, gloas[4] = {0};
  TEST_ASSERT_TRUE(c4_eth_compute_fork_digest(C4_CHAIN_MAINNET, C4_FORK_ELECTRA, electra));
  TEST_ASSERT_TRUE(c4_eth_compute_fork_digest(C4_CHAIN_MAINNET, C4_FORK_FULU, fulu));
  TEST_ASSERT_TRUE(c4_eth_compute_fork_digest(C4_CHAIN_PLATABERGET, C4_FORK_FULU, plat_fulu));
  TEST_ASSERT_TRUE(c4_eth_compute_fork_digest(C4_CHAIN_PLATABERGET, C4_FORK_GLOAS, gloas));

  TEST_ASSERT_NOT_EQUAL_INT(0, memcmp(electra, fulu, 4));
  TEST_ASSERT_NOT_EQUAL_INT(0, memcmp(plat_fulu, gloas, 4));

  TEST_ASSERT_EQUAL_INT(C4_FORK_ELECTRA, c4_eth_fork_from_digest(C4_CHAIN_MAINNET, electra));
  TEST_ASSERT_EQUAL_INT(C4_FORK_FULU, c4_eth_fork_from_digest(C4_CHAIN_MAINNET, fulu));
  TEST_ASSERT_EQUAL_INT(C4_FORK_GLOAS, c4_eth_fork_from_digest(C4_CHAIN_PLATABERGET, gloas));
}

// BPO digests are built from `fork_data_root` + SHA256(epoch || max_blobs), not
// by reading the lookup cache. A cache that stored only compute(FULU) would
// fail these lookups.
static void assert_bpo_maps_to_fulu(chain_id_t chain_id, uint64_t bpo1_epoch, uint64_t bpo1_max,
                                    uint64_t bpo2_epoch, uint64_t bpo2_max) {
  bytes32_t           base = {0};
  const chain_spec_t* spec = c4_eth_get_chain_spec(chain_id);
  TEST_ASSERT_NOT_NULL(spec);
  TEST_ASSERT_TRUE(c4_eth_fork_data_root(spec, C4_FORK_FULU, base));

  uint8_t bpo1[4] = {0}, bpo2[4] = {0}, fulu[4] = {0};
  xor_blob_mask(bpo1, base, bpo1_epoch, bpo1_max);
  xor_blob_mask(bpo2, base, bpo2_epoch, bpo2_max);
  TEST_ASSERT_TRUE(c4_eth_compute_fork_digest(chain_id, C4_FORK_FULU, fulu));

  TEST_ASSERT_NOT_EQUAL_INT(0, memcmp(fulu, bpo1, 4));
  TEST_ASSERT_NOT_EQUAL_INT(0, memcmp(fulu, bpo2, 4));
  TEST_ASSERT_NOT_EQUAL_INT(0, memcmp(bpo1, bpo2, 4));
  TEST_ASSERT_EQUAL_INT(C4_FORK_FULU, c4_eth_fork_from_digest(chain_id, bpo1));
  TEST_ASSERT_EQUAL_INT(C4_FORK_FULU, c4_eth_fork_from_digest(chain_id, bpo2));
}

void test_fork_digest_bpo_maps_to_fulu(void) {
  assert_bpo_maps_to_fulu(C4_CHAIN_MAINNET, 412672ULL, 15ULL, 419072ULL, 21ULL);
  assert_bpo_maps_to_fulu(C4_CHAIN_SEPOLIA, 274176ULL, 15ULL, 275712ULL, 21ULL);
}

void test_fork_digest_unknown_is_invalid_and_suggests_upgrade(void) {
  uint8_t unknown[4] = {0xde, 0xad, 0xbe, 0xef};
  TEST_ASSERT_EQUAL_INT(C4_FORK_INVALID, c4_eth_fork_from_digest(C4_CHAIN_MAINNET, unknown));
  TEST_ASSERT_EQUAL_INT(C4_FORK_INVALID, c4_eth_fork_from_digest(C4_CHAIN_MAINNET, NULL));
  TEST_ASSERT_NOT_EQUAL_INT(C4_FORK_MAX, c4_eth_fork_from_digest(C4_CHAIN_MAINNET, unknown));
  TEST_ASSERT_NOT_EQUAL_INT(C4_FORK_GLOAS, c4_eth_fork_from_digest(C4_CHAIN_MAINNET, unknown));

  c4_state_t state = {0};
  c4_eth_unknown_lcu_fork_error(&state, unknown);
  TEST_ASSERT_NOT_NULL(state.error);
  TEST_ASSERT_NOT_NULL(strstr(state.error, "unrecognized fork digest"));
  TEST_ASSERT_NOT_NULL(strstr(state.error, "please update the app"));
  TEST_ASSERT_NOT_NULL(strstr(state.error, "deadbeef"));
  safe_free(state.error);
}

void test_fork_digest_too_short_and_unknown_chain(void) {
  uint8_t four[4]   = {0x01, 0x02, 0x03, 0x04};
  uint8_t unused[4] = {0};

  TEST_ASSERT_EQUAL_INT(C4_FORK_INVALID, c4_eth_fork_from_digest(C4_CHAIN_MAINNET, NULL));
  TEST_ASSERT_EQUAL_INT(C4_FORK_INVALID, c4_eth_fork_from_digest(CHAIN(999999), four));
  bytes32_t root = {0};
  TEST_ASSERT_FALSE(c4_eth_compute_fork_digest(CHAIN(999999), C4_FORK_ELECTRA, unused));
  TEST_ASSERT_FALSE(c4_eth_compute_fork_digest(C4_CHAIN_MAINNET, C4_FORK_ELECTRA, NULL));
  TEST_ASSERT_FALSE(c4_eth_fork_data_root(NULL, C4_FORK_FULU, root));
}

void test_fork_digest_gnosis_empty_blob_schedule_uses_electra_max_2(void) {
  const chain_spec_t* spec = c4_eth_get_chain_spec(C4_CHAIN_GNOSIS);
  TEST_ASSERT_NOT_NULL(spec);
  TEST_ASSERT_NULL_MESSAGE(spec->blob_params, "Gnosis has an empty CL BLOB_SCHEDULE");
  TEST_ASSERT_EQUAL_UINT64(2ULL, spec->max_blobs_per_block_electra);

  uint64_t electra_epoch = spec->fork_epochs[C4_FORK_ELECTRA - 1];
  TEST_ASSERT_EQUAL_UINT64(1337856ULL, electra_epoch);

  bytes32_t fulu_base = {0}, electra_base = {0};
  TEST_ASSERT_TRUE(c4_eth_fork_data_root(spec, C4_FORK_FULU, fulu_base));
  TEST_ASSERT_TRUE(c4_eth_fork_data_root(spec, C4_FORK_ELECTRA, electra_base));

  uint8_t electra[4] = {0}, fulu[4] = {0}, fulu_xor2[4] = {0}, fulu_xor9[4] = {0};
  TEST_ASSERT_TRUE(c4_eth_compute_fork_digest(C4_CHAIN_GNOSIS, C4_FORK_ELECTRA, electra));
  TEST_ASSERT_TRUE(c4_eth_compute_fork_digest(C4_CHAIN_GNOSIS, C4_FORK_FULU, fulu));
  xor_blob_mask(fulu_xor2, fulu_base, electra_epoch, 2ULL);
  xor_blob_mask(fulu_xor9, fulu_base, electra_epoch, 9ULL);

  TEST_ASSERT_EQUAL_UINT8_ARRAY(electra_base, electra, 4);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(fulu_xor2, fulu, 4);
  TEST_ASSERT_NOT_EQUAL_INT(0, memcmp(electra, fulu, 4));
  TEST_ASSERT_NOT_EQUAL_INT(0, memcmp(fulu_xor2, fulu_xor9, 4));
  TEST_ASSERT_EQUAL_INT(C4_FORK_ELECTRA, c4_eth_fork_from_digest(C4_CHAIN_GNOSIS, electra));
  TEST_ASSERT_EQUAL_INT(C4_FORK_FULU, c4_eth_fork_from_digest(C4_CHAIN_GNOSIS, fulu_xor2));
  TEST_ASSERT_EQUAL_INT(C4_FORK_INVALID, c4_eth_fork_from_digest(C4_CHAIN_GNOSIS, fulu_xor9));

  const chain_spec_t* chiado = c4_eth_get_chain_spec(C4_CHAIN_GNOSIS_CHIADO);
  TEST_ASSERT_NOT_NULL(chiado);
  TEST_ASSERT_NULL(chiado->blob_params);
  TEST_ASSERT_EQUAL_UINT64(2ULL, chiado->max_blobs_per_block_electra);
}

void test_fork_digest_unscheduled_gloas_on_mainnet(void) {
  TEST_ASSERT_FALSE(c4_chain_schedules_fork(C4_CHAIN_MAINNET, C4_FORK_GLOAS));

  uint8_t gloas[4] = {0}, electra[4] = {0}, fulu[4] = {0};
  TEST_ASSERT_TRUE(c4_eth_compute_fork_digest(C4_CHAIN_MAINNET, C4_FORK_GLOAS, gloas));
  TEST_ASSERT_TRUE(c4_eth_compute_fork_digest(C4_CHAIN_MAINNET, C4_FORK_ELECTRA, electra));
  TEST_ASSERT_TRUE(c4_eth_compute_fork_digest(C4_CHAIN_MAINNET, C4_FORK_FULU, fulu));
  TEST_ASSERT_NOT_EQUAL_INT(0, memcmp(gloas, electra, 4));
  TEST_ASSERT_NOT_EQUAL_INT(0, memcmp(gloas, fulu, 4));

  bytes32_t           base = {0};
  const chain_spec_t* spec = c4_eth_get_chain_spec(C4_CHAIN_MAINNET);
  TEST_ASSERT_NOT_NULL(spec);
  TEST_ASSERT_TRUE(c4_eth_fork_data_root(spec, C4_FORK_GLOAS, base));
  uint8_t expected[4] = {0};
  xor_blob_mask(expected, base, 419072ULL, 21ULL);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, gloas, 4);

  TEST_ASSERT_EQUAL_INT(C4_FORK_INVALID, c4_eth_fork_from_digest(C4_CHAIN_MAINNET, gloas));
  TEST_ASSERT_NOT_EQUAL_INT(C4_FORK_MAX, c4_eth_fork_from_digest(C4_CHAIN_MAINNET, gloas));
}

void test_fork_max_bounds_lookup(void) {
  uint8_t unused[4]  = {0};
  uint8_t garbage[4] = {1, 2, 3, 4};
  uint8_t at_max[4]  = {0};
  TEST_ASSERT_EQUAL_INT(C4_FORK_GLOAS, C4_FORK_MAX);
  TEST_ASSERT_FALSE(c4_eth_compute_fork_digest(C4_CHAIN_MAINNET, (fork_id_t) (C4_FORK_MAX + 1), unused));
  TEST_ASSERT_TRUE(c4_eth_compute_fork_digest(C4_CHAIN_PLATABERGET, C4_FORK_MAX, at_max));
  TEST_ASSERT_EQUAL_INT(C4_FORK_GLOAS, c4_eth_fork_from_digest(C4_CHAIN_PLATABERGET, at_max));
  TEST_ASSERT_EQUAL_INT(C4_FORK_INVALID, c4_eth_fork_from_digest(CHAIN(999999), garbage));
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
  RUN_TEST(test_min_blob_base_fee);
  RUN_TEST(test_fork_digest_differs_across_forks);
  RUN_TEST(test_fork_digest_bpo_maps_to_fulu);
  RUN_TEST(test_fork_digest_unknown_is_invalid_and_suggests_upgrade);
  RUN_TEST(test_fork_digest_too_short_and_unknown_chain);
  RUN_TEST(test_fork_digest_gnosis_empty_blob_schedule_uses_electra_max_2);
  RUN_TEST(test_fork_digest_unscheduled_gloas_on_mainnet);
  RUN_TEST(test_fork_max_bounds_lookup);
  return UNITY_END();
}
