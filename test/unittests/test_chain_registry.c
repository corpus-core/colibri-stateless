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

#include "chains.h"
#include "unity.h"

void setUp(void) { c4_reset_chain_registry(); }
void tearDown(void) { c4_reset_chain_registry(); }

// A registered mapping routes a plain `CHAIN()` id to the requested type.
// Uses `CHAIN(80002)` (Polygon Amoy testnet) because it is a real-world EVM id
// that the core does not know; `CHAIN(137)` cannot be used here anymore because
// `C4_CHAIN_POLYGON` is a built-in and is hard-rejected (see H-1 guard).
void test_register_chain_family_routes_an_evm_id(void) {
  TEST_ASSERT_EQUAL(C4_CHAIN_TYPE_ETHEREUM, c4_chain_type(CHAIN(80002)));
  TEST_ASSERT_TRUE(c4_register_chain_family(CHAIN(80002), C4_CHAIN_TYPE_POLYGON));
  TEST_ASSERT_EQUAL(C4_CHAIN_TYPE_POLYGON, c4_chain_type(CHAIN(80002)));
}

// Built-in switch cases cannot be overridden: `c4_register_chain_family` must
// hard-reject the registration for any `C4_CHAIN_*` constant. Preserves the
// core-routing invariant even against a buggy / malicious plugin.
void test_builtin_arbitrum_wins_over_the_registry(void) {
  TEST_ASSERT_FALSE(c4_register_chain_family(C4_CHAIN_ARB_ONE, C4_CHAIN_TYPE_POLYGON));
  TEST_ASSERT_EQUAL(C4_CHAIN_TYPE_ARBITRUM, c4_chain_type(C4_CHAIN_ARB_ONE));
}

// Same guard for every OP-Stack and EEZ built-in.
void test_builtin_op_and_eez_win_over_the_registry(void) {
  TEST_ASSERT_FALSE(c4_register_chain_family(C4_CHAIN_OP_BASE, C4_CHAIN_TYPE_POLYGON));
  TEST_ASSERT_EQUAL(C4_CHAIN_TYPE_OP, c4_chain_type(C4_CHAIN_OP_BASE));

  TEST_ASSERT_FALSE(c4_register_chain_family(C4_CHAIN_EEZ_ROLLUPX, C4_CHAIN_TYPE_POLYGON));
  TEST_ASSERT_EQUAL(C4_CHAIN_TYPE_EEZ, c4_chain_type(C4_CHAIN_EEZ_ROLLUPX));
}

// Regression for H-1 (security review): Ethereum-encoded built-in ids such as
// `C4_CHAIN_MAINNET=1`, `C4_CHAIN_POLYGON=137`, `C4_CHAIN_BSC=56`, …
// are NOT in the `switch` of `c4_chain_type`; without the explicit builtin
// guard the registry loop would silently reroute them and hijack mainnet
// routing. Exhaustive check so a dropped entry in `is_builtin_chain_id`
// is caught immediately.
void test_builtin_chain_ids_are_hard_rejected(void) {
  const chain_id_t builtins[] = {
      C4_CHAIN_MAINNET,
      C4_CHAIN_SEPOLIA,
      C4_CHAIN_GNOSIS,
      C4_CHAIN_GNOSIS_CHIADO,
      C4_CHAIN_PLATABERGET,
      C4_CHAIN_BTC_MAINNET,
      C4_CHAIN_BTC_TESTNET,
      C4_CHAIN_BTC_DEVNET,
      C4_CHAIN_SOL_MAINNET,
      C4_CHAIN_BSC,
      C4_CHAIN_POLYGON,
      C4_CHAIN_BASE,
      C4_CHAIN_ARBITRUM,
      C4_CHAIN_OPTIMISM,
      C4_CHAIN_CRONOS,
      C4_CHAIN_FUSE,
      C4_CHAIN_AVALANCHE,
      C4_CHAIN_MOONRIVER,
      C4_CHAIN_MOONBEAM,
      C4_CHAIN_TELOS,
      C4_CHAIN_HAIFA,
      C4_CHAIN_BOLT,
      C4_CHAIN_BOLT_TESTNET,
      C4_CHAIN_BOLT_DEVNET,
      C4_CHAIN_BOLT_STAGING,
      C4_CHAIN_BOLT_MAINNET,
      C4_CHAIN_OP_MAINNET,
      C4_CHAIN_OP_BASE,
      C4_CHAIN_OP_WORLDCHAIN,
      C4_CHAIN_OP_ZORA,
      C4_CHAIN_OP_UNICHAIN,
      C4_CHAIN_OP_INK,
      C4_CHAIN_OP_PGN,
      C4_CHAIN_OP_ORDERLY,
      C4_CHAIN_OP_MODE,
      C4_CHAIN_OP_FRAXTAL,
      C4_CHAIN_OP_MANTLE,
      C4_CHAIN_OP_KLAYTN,
      C4_CHAIN_ARB_ONE,
      C4_CHAIN_EEZ_ROLLUPX,
  };
  for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
    // Family registration must fail and leave `c4_chain_type` unchanged.
    chain_type_t before = c4_chain_type(builtins[i]);
    TEST_ASSERT_FALSE(c4_register_chain_family(builtins[i], C4_CHAIN_TYPE_POLYGON));
    TEST_ASSERT_EQUAL(before, c4_chain_type(builtins[i]));
    // L1 registration for a built-in L2 must also fail and leave the lookup
    // unchanged (cannot hijack sync-committee state).
    chain_id_t l1_before = c4_get_l1_chain_id_for_l2(builtins[i]);
    TEST_ASSERT_FALSE(c4_register_l1_for_l2(builtins[i], CHAIN(999)));
    TEST_ASSERT_EQUAL(l1_before, c4_get_l1_chain_id_for_l2(builtins[i]));
  }
}

// A second call with the same id overwrites the stored type.
void test_register_chain_family_is_idempotent(void) {
  TEST_ASSERT_TRUE(c4_register_chain_family(CHAIN(80002), C4_CHAIN_TYPE_POLYGON));
  TEST_ASSERT_TRUE(c4_register_chain_family(CHAIN(80002), C4_CHAIN_TYPE_SOLANA));
  TEST_ASSERT_EQUAL(C4_CHAIN_TYPE_SOLANA, c4_chain_type(CHAIN(80002)));
}

// Rejects: chain_id 0, type >= 16 (hook tables stop at slot 15).
void test_register_chain_family_rejects_invalid_input(void) {
  TEST_ASSERT_FALSE(c4_register_chain_family(0, C4_CHAIN_TYPE_POLYGON));
  TEST_ASSERT_FALSE(c4_register_chain_family(CHAIN(80002), (chain_type_t) 16));
  TEST_ASSERT_FALSE(c4_register_chain_family(CHAIN(80002), (chain_type_t) 200));
}

// Fills the table and rejects the overflow entry.
void test_register_chain_family_rejects_a_full_table(void) {
  for (uint32_t i = 0; i < C4_CHAIN_REGISTRY_MAX; i++)
    TEST_ASSERT_TRUE(c4_register_chain_family(CHAIN(10000 + i), C4_CHAIN_TYPE_POLYGON));
  // One more unique id must fail.
  TEST_ASSERT_FALSE(c4_register_chain_family(CHAIN(99999), C4_CHAIN_TYPE_POLYGON));
  // But an update to an existing id still succeeds.
  TEST_ASSERT_TRUE(c4_register_chain_family(CHAIN(10000), C4_CHAIN_TYPE_SOLANA));
}

// Unregistered ids keep the high-byte behaviour and known switches.
void test_c4_chain_type_falls_back_to_the_high_byte(void) {
  TEST_ASSERT_EQUAL(C4_CHAIN_TYPE_ETHEREUM, c4_chain_type(C4_CHAIN_MAINNET));
  TEST_ASSERT_EQUAL(C4_CHAIN_TYPE_POLKADOT, c4_chain_type(CHAIN_ID(C4_CHAIN_TYPE_POLKADOT, 42)));
  TEST_ASSERT_EQUAL(C4_CHAIN_TYPE_ARBITRUM, c4_chain_type(C4_CHAIN_ARB_ONE));
}

// The L1 registry only applies on the default branch.
void test_register_l1_for_l2_routes_a_new_l2(void) {
  chain_id_t plasma = CHAIN(2718);
  TEST_ASSERT_EQUAL(plasma, c4_get_l1_chain_id_for_l2(plasma));
  TEST_ASSERT_TRUE(c4_register_l1_for_l2(plasma, C4_CHAIN_MAINNET));
  TEST_ASSERT_EQUAL(C4_CHAIN_MAINNET, c4_get_l1_chain_id_for_l2(plasma));
}

// Built-in EEZ/Arbitrum mappings beat the registry (hard-reject; see
// `test_builtin_chain_ids_are_hard_rejected` for the exhaustive variant).
void test_register_l1_for_l2_cannot_override_builtins(void) {
  TEST_ASSERT_FALSE(c4_register_l1_for_l2(C4_CHAIN_ARB_ONE, CHAIN(999)));
  TEST_ASSERT_EQUAL(C4_CHAIN_MAINNET, c4_get_l1_chain_id_for_l2(C4_CHAIN_ARB_ONE));

  TEST_ASSERT_FALSE(c4_register_l1_for_l2(C4_CHAIN_EEZ_ROLLUPX, CHAIN(999)));
  TEST_ASSERT_EQUAL(C4_CHAIN_MAINNET, c4_get_l1_chain_id_for_l2(C4_CHAIN_EEZ_ROLLUPX));
}

// Rejects: l2 == 0, l1 == 0 and l2 == l1 (self-referential loop). Both zero
// checks guard callers that treat `0` as "unknown L1" sentinel.
void test_register_l1_for_l2_rejects_invalid_input(void) {
  TEST_ASSERT_FALSE(c4_register_l1_for_l2(0, C4_CHAIN_MAINNET));
  TEST_ASSERT_FALSE(c4_register_l1_for_l2(CHAIN(42), 0));
  TEST_ASSERT_FALSE(c4_register_l1_for_l2(CHAIN(42), CHAIN(42)));
}

// Full L1 table rejects new entries but still updates existing ones.
void test_register_l1_for_l2_rejects_a_full_table(void) {
  for (uint32_t i = 0; i < C4_CHAIN_REGISTRY_MAX; i++)
    TEST_ASSERT_TRUE(c4_register_l1_for_l2(CHAIN(20000 + i), C4_CHAIN_MAINNET));
  TEST_ASSERT_FALSE(c4_register_l1_for_l2(CHAIN(99999), C4_CHAIN_MAINNET));
  TEST_ASSERT_TRUE(c4_register_l1_for_l2(CHAIN(20000), C4_CHAIN_SEPOLIA));
}

// Reset empties both tables.
void test_reset_chain_registry_clears_both_tables(void) {
  TEST_ASSERT_TRUE(c4_register_chain_family(CHAIN(80002), C4_CHAIN_TYPE_POLYGON));
  TEST_ASSERT_TRUE(c4_register_l1_for_l2(CHAIN(80002), C4_CHAIN_MAINNET));
  c4_reset_chain_registry();
  TEST_ASSERT_EQUAL(C4_CHAIN_TYPE_ETHEREUM, c4_chain_type(CHAIN(80002)));
  TEST_ASSERT_EQUAL(CHAIN(80002), c4_get_l1_chain_id_for_l2(CHAIN(80002)));
}

// Upper boundary of the valid `type` range: 15 is the last accepted slot; 16 is
// the first rejected one. Guards against off-by-one in `(unsigned) type >= 16`.
void test_register_chain_family_accepts_boundary_type_15(void) {
  TEST_ASSERT_TRUE(c4_register_chain_family(CHAIN(777), (chain_type_t) 15));
  TEST_ASSERT_EQUAL(15, c4_chain_type(CHAIN(777)));
  // And type == 14 (C4_CHAIN_TYPE_TELOS) must also be accepted.
  TEST_ASSERT_TRUE(c4_register_chain_family(CHAIN(778), C4_CHAIN_TYPE_TELOS));
  TEST_ASSERT_EQUAL(C4_CHAIN_TYPE_TELOS, c4_chain_type(CHAIN(778)));
}

// After the table is full, an overwrite on an existing id must still succeed
// and must NOT grow the table (next unique id would otherwise unexpectedly fit).
void test_register_chain_family_overwrite_does_not_grow_table(void) {
  for (uint32_t i = 0; i < C4_CHAIN_REGISTRY_MAX; i++)
    TEST_ASSERT_TRUE(c4_register_chain_family(CHAIN(30000 + i), C4_CHAIN_TYPE_POLYGON));

  // Table is full: any unique id must be rejected.
  TEST_ASSERT_FALSE(c4_register_chain_family(CHAIN(40000), C4_CHAIN_TYPE_POLYGON));

  // Overwrite the first slot several times -- length must stay at MAX, so the
  // new unique id must still be rejected after each overwrite.
  for (int i = 0; i < 3; i++) {
    TEST_ASSERT_TRUE(c4_register_chain_family(CHAIN(30000), C4_CHAIN_TYPE_SOLANA));
    TEST_ASSERT_FALSE(c4_register_chain_family(CHAIN(40000), C4_CHAIN_TYPE_POLYGON));
  }
  TEST_ASSERT_EQUAL(C4_CHAIN_TYPE_SOLANA, c4_chain_type(CHAIN(30000)));
}

// A second call with the same L2 overwrites the stored L1 (mirrors the family
// idempotency test). Documents a behaviour the registry contract promises in
// the header comment.
void test_register_l1_for_l2_is_idempotent(void) {
  chain_id_t plasma = CHAIN(2718);
  TEST_ASSERT_TRUE(c4_register_l1_for_l2(plasma, C4_CHAIN_MAINNET));
  TEST_ASSERT_TRUE(c4_register_l1_for_l2(plasma, C4_CHAIN_SEPOLIA));
  TEST_ASSERT_EQUAL(C4_CHAIN_SEPOLIA, c4_get_l1_chain_id_for_l2(plasma));
}

// After filling the L1 table, overwriting an existing L2 must not consume a
// new slot (otherwise a unique L2 could sneak in after an overwrite).
void test_register_l1_for_l2_overwrite_does_not_grow_table(void) {
  for (uint32_t i = 0; i < C4_CHAIN_REGISTRY_MAX; i++)
    TEST_ASSERT_TRUE(c4_register_l1_for_l2(CHAIN(50000 + i), C4_CHAIN_MAINNET));

  TEST_ASSERT_FALSE(c4_register_l1_for_l2(CHAIN(60000), C4_CHAIN_MAINNET));
  for (int i = 0; i < 3; i++) {
    TEST_ASSERT_TRUE(c4_register_l1_for_l2(CHAIN(50000), C4_CHAIN_SEPOLIA));
    TEST_ASSERT_FALSE(c4_register_l1_for_l2(CHAIN(60000), C4_CHAIN_MAINNET));
  }
  TEST_ASSERT_EQUAL(C4_CHAIN_SEPOLIA, c4_get_l1_chain_id_for_l2(CHAIN(50000)));
}

// The two registries are independent: writing to the chain-family table must
// not affect L1 lookups, and vice versa. A defensive check against an
// accidental shared index/length.
void test_chain_family_and_l1_registries_are_independent(void) {
  chain_id_t plasma = CHAIN(2718);
  TEST_ASSERT_TRUE(c4_register_chain_family(plasma, C4_CHAIN_TYPE_POLYGON));
  // Family entry must not implicitly create an L1 mapping.
  TEST_ASSERT_EQUAL(plasma, c4_get_l1_chain_id_for_l2(plasma));

  c4_reset_chain_registry();
  TEST_ASSERT_TRUE(c4_register_l1_for_l2(plasma, C4_CHAIN_MAINNET));
  // L1 entry must not implicitly set the chain family.
  TEST_ASSERT_EQUAL(C4_CHAIN_TYPE_ETHEREUM, c4_chain_type(plasma));
}

// `l1 == 0` is rejected: callers use `0` as the "unknown" sentinel and must
// not be able to steer the sync-committee lookup into that sentinel through
// a plugin. Covered as a dedicated test so a future API loosening is noticed.
void test_register_l1_for_l2_rejects_l1_zero(void) {
  TEST_ASSERT_FALSE(c4_register_l1_for_l2(CHAIN(42), 0));
  TEST_ASSERT_EQUAL(CHAIN(42), c4_get_l1_chain_id_for_l2(CHAIN(42)));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_register_chain_family_routes_an_evm_id);
  RUN_TEST(test_builtin_arbitrum_wins_over_the_registry);
  RUN_TEST(test_builtin_op_and_eez_win_over_the_registry);
  RUN_TEST(test_builtin_chain_ids_are_hard_rejected);
  RUN_TEST(test_register_chain_family_is_idempotent);
  RUN_TEST(test_register_chain_family_rejects_invalid_input);
  RUN_TEST(test_register_chain_family_rejects_a_full_table);
  RUN_TEST(test_c4_chain_type_falls_back_to_the_high_byte);
  RUN_TEST(test_register_l1_for_l2_routes_a_new_l2);
  RUN_TEST(test_register_l1_for_l2_cannot_override_builtins);
  RUN_TEST(test_register_l1_for_l2_rejects_invalid_input);
  RUN_TEST(test_register_l1_for_l2_rejects_a_full_table);
  RUN_TEST(test_reset_chain_registry_clears_both_tables);
  RUN_TEST(test_register_chain_family_accepts_boundary_type_15);
  RUN_TEST(test_register_chain_family_overwrite_does_not_grow_table);
  RUN_TEST(test_register_l1_for_l2_is_idempotent);
  RUN_TEST(test_register_l1_for_l2_overwrite_does_not_grow_table);
  RUN_TEST(test_chain_family_and_l1_registries_are_independent);
  RUN_TEST(test_register_l1_for_l2_rejects_l1_zero);
  return UNITY_END();
}
