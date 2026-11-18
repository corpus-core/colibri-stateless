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
#include "colibri_common.h"
#include "json.h"
#include "state.h"
#include "unity.h"
#include <stdlib.h>
#include <string.h>

#if !defined(NO_CHAIN_ETH)
#include "chain_spec.h"
#endif
#if !defined(NO_CHAIN_EEZ)
#include "eez_chains_conf.h"
#endif
#if !defined(NO_CHAIN_OP)
#include "op_chains_conf.h"
#endif
#if !defined(NO_CHAIN_ARB)
#include "arb_batch_poster.h"
#endif

// Drops any previously registered runtime chains so each test starts empty.
// Built-ins are untouched; `c4_reset_chain_registry` only clears the family /
// l1-mapping tables and does not know about chain-type-specific extras tables.
static void reset_all_runtime_registries(void) {
#if !defined(NO_CHAIN_ETH)
  c4_eth_reset_chain_spec_registry();
#endif
#if !defined(NO_CHAIN_EEZ)
  c4_eez_reset_chain_config_registry();
#endif
#if !defined(NO_CHAIN_OP)
  c4_op_reset_chain_config_registry();
#endif
#if !defined(NO_CHAIN_ARB)
  c4_arb_reset_chain_config_registry();
#endif
  c4_reset_chain_registry();
}

void setUp(void) { reset_all_runtime_registries(); }
void tearDown(void) {
  reset_all_runtime_registries();
}

// Builds a fresh `c4_state_t` on the stack. Error strings live on the heap and
// are freed by `c4_state_free` after every test case.
static c4_state_t make_state(void) {
  c4_state_t s = {0};
  return s;
}

#if !defined(NO_CHAIN_ETH)
// Valid JSON that describes a fictional ETH-family chain whose numeric id is
// NOT in the built-in table. Covers every mandatory field plus a couple of
// optional ones so we exercise the parser widely.
static const char ETH_VALID_JSON[] =
    "{"
    "\"chain_id\":424242,"
    "\"genesis_validators_root\":\"0x4b363db94e286120d76eb905340fdd4e54bfe9f06bf33ff6cf5ad27f511bfe95\","
    "\"fork_epochs\":[0,0,0,0,0,0,1536],"
    "\"fork_versions\":["
    "\"0x11223300\",\"0x11223301\",\"0x11223302\",\"0x11223303\","
    "\"0x11223304\",\"0x11223305\",\"0x11223306\",\"0x11223307\""
    "],"
    "\"slots_per_epoch_bits\":5,"
    "\"epochs_per_period_bits\":8,"
    "\"min_blob_base_fee\":1,"
    "\"blob_schedule\":[[1710338135,3338477]],"
    "\"blob_params\":[[0,9]]"
    "}";

void test_eth_register_chain_populates_spec(void) {
  c4_state_t s = make_state();
  json_t     j = json_parse(ETH_VALID_JSON);
  TEST_ASSERT_TRUE(c4_eth_register_chain(&s, j));
  TEST_ASSERT_NULL(s.error);

  const chain_spec_t* spec = c4_eth_get_chain_spec((chain_id_t) 424242);
  TEST_ASSERT_NOT_NULL(spec);
  TEST_ASSERT_EQUAL_UINT64(424242, spec->chain_id);
  TEST_ASSERT_EQUAL_INT(5, spec->slots_per_epoch_bits);
  TEST_ASSERT_EQUAL_INT(8, spec->epochs_per_period_bits);
  TEST_ASSERT_EQUAL_UINT64(1, spec->min_blob_base_fee);
  TEST_ASSERT_NOT_NULL(spec->fork_epochs);
  TEST_ASSERT_NOT_NULL(spec->fork_versions);
  TEST_ASSERT_NOT_NULL(spec->blob_schedule);
  TEST_ASSERT_NOT_NULL(spec->blob_params);
  TEST_ASSERT_NOT_NULL(spec->fork_version_func);

  // Spot-check that the version table is readable through the generic callback.
  uint8_t version[4] = {0};
  spec->fork_version_func(spec->chain_id, C4_FORK_PHASE0, version);
  TEST_ASSERT_EQUAL_HEX8(0x11, version[0]);
  TEST_ASSERT_EQUAL_HEX8(0x22, version[1]);
  TEST_ASSERT_EQUAL_HEX8(0x33, version[2]);
  TEST_ASSERT_EQUAL_HEX8(0x00, version[3]);

  spec->fork_version_func(spec->chain_id, C4_FORK_ELECTRA, version);
  TEST_ASSERT_EQUAL_HEX8(0x05, version[3]);

  c4_state_free(&s);
}

// Built-in chain ids are returned from `c4_eth_get_chain_spec` and must not be
// shadowed by a runtime registration.
void test_eth_register_chain_rejects_duplicate(void) {
  // Mainnet (chain_id 1) is always registered in `chain_data[]`.
  const char* dup =
      "{"
      "\"chain_id\":1,"
      "\"genesis_validators_root\":\"0x0000000000000000000000000000000000000000000000000000000000000000\","
      "\"fork_epochs\":[0],"
      "\"fork_versions\":[\"0x00000000\",\"0x01000000\"]"
      "}";
  c4_state_t s = make_state();
  json_t     j = json_parse(dup);
  TEST_ASSERT_FALSE(c4_eth_register_chain(&s, j));
  TEST_ASSERT_NOT_NULL(s.error);
  c4_state_free(&s);
}

void test_eth_register_chain_rejects_mismatched_fork_version_count(void) {
  const char* bad =
      "{"
      "\"chain_id\":424243,"
      "\"genesis_validators_root\":\"0x0000000000000000000000000000000000000000000000000000000000000000\","
      "\"fork_epochs\":[0,0,0],"
      "\"fork_versions\":[\"0x00000000\",\"0x01000000\"]" // only 2 entries, but 3 epochs -> need 4
      "}";
  c4_state_t s = make_state();
  json_t     j = json_parse(bad);
  TEST_ASSERT_FALSE(c4_eth_register_chain(&s, j));
  TEST_ASSERT_NOT_NULL(s.error);
  c4_state_free(&s);
}

void test_eth_register_chain_rejects_invalid_schema(void) {
  const char* bad = "{\"chain_id\":424244}"; // missing required fields
  c4_state_t  s   = make_state();
  json_t      j   = json_parse(bad);
  TEST_ASSERT_FALSE(c4_eth_register_chain(&s, j));
  TEST_ASSERT_NOT_NULL(s.error);
  c4_state_free(&s);
}

// The generated dispatcher in `verifiers.h` must route C4_CHAIN_TYPE_ETHEREUM
// payloads through to `c4_eth_register_chain`.
void test_dispatcher_routes_to_eth_handler(void) {
  c4_state_t s = make_state();
  json_t     j = json_parse(ETH_VALID_JSON);
  TEST_ASSERT_TRUE(c4_register_chain(&s, C4_CHAIN_TYPE_ETHEREUM, j));
  TEST_ASSERT_NOT_NULL(c4_eth_get_chain_spec((chain_id_t) 424242));
  c4_state_free(&s);
}

// `chain_id` accepts both JSON numbers (`42`) and Eth-style hex-strings
// (`"0x..."`). Round-trip the hex form end-to-end.
void test_eth_register_chain_accepts_hex_chain_id(void) {
  const char* hex_json =
      "{"
      "\"chain_id\":\"0x67932\"," // 424242
      "\"genesis_validators_root\":\"0x0000000000000000000000000000000000000000000000000000000000000000\","
      "\"fork_epochs\":[0],"
      "\"fork_versions\":[\"0x00000000\",\"0x01000000\"]"
      "}";
  c4_state_t s = make_state();
  json_t     j = json_parse(hex_json);
  TEST_ASSERT_TRUE(c4_eth_register_chain(&s, j));
  TEST_ASSERT_NOT_NULL(c4_eth_get_chain_spec((chain_id_t) 424242));
  c4_state_free(&s);
}

// Round-trip `blob_schedule` and `blob_params` contents: the parser must
// keep the ordering we sent in (DESCENDING by timestamp / epoch) and append
// the `{0, 0}` sentinel used by the callers to stop iteration.
void test_eth_register_chain_preserves_blob_tables(void) {
  const char* j_str =
      "{"
      "\"chain_id\":424245,"
      "\"genesis_validators_root\":\"0x0000000000000000000000000000000000000000000000000000000000000000\","
      "\"fork_epochs\":[0],"
      "\"fork_versions\":[\"0x00000000\",\"0x01000000\"],"
      "\"blob_schedule\":[[1710338135,3338477],[1706655072,3338477]],"
      "\"blob_params\":[[419072,21],[0,9]]"
      "}";
  c4_state_t s = make_state();
  json_t     j = json_parse(j_str);
  TEST_ASSERT_TRUE(c4_eth_register_chain(&s, j));
  const chain_spec_t* spec = c4_eth_get_chain_spec((chain_id_t) 424245);
  TEST_ASSERT_NOT_NULL(spec);
  TEST_ASSERT_NOT_NULL(spec->blob_schedule);
  TEST_ASSERT_EQUAL_UINT64(1710338135, spec->blob_schedule[0].activation_timestamp);
  TEST_ASSERT_EQUAL_UINT64(3338477, spec->blob_schedule[0].update_fraction);
  TEST_ASSERT_EQUAL_UINT64(1706655072, spec->blob_schedule[1].activation_timestamp);
  TEST_ASSERT_EQUAL_UINT64(0, spec->blob_schedule[2].activation_timestamp); // sentinel

  TEST_ASSERT_NOT_NULL(spec->blob_params);
  TEST_ASSERT_EQUAL_UINT64(419072, spec->blob_params[0].epoch);
  TEST_ASSERT_EQUAL_UINT64(21, spec->blob_params[0].max_blobs_per_block);
  TEST_ASSERT_EQUAL_UINT64(0, spec->blob_params[2].max_blobs_per_block); // sentinel
  c4_state_free(&s);
}

// Two successive registrations with distinct ids must both be looked up
// afterwards; the extras array grows and both entries stay reachable.
void test_eth_register_chain_registers_multiple_chains(void) {
  const char* a =
      "{\"chain_id\":424246,"
      "\"genesis_validators_root\":\"0x0000000000000000000000000000000000000000000000000000000000000000\","
      "\"fork_epochs\":[0],"
      "\"fork_versions\":[\"0x00000000\",\"0x01000000\"]}";
  const char* b =
      "{\"chain_id\":424247,"
      "\"genesis_validators_root\":\"0x1111111111111111111111111111111111111111111111111111111111111111\","
      "\"fork_epochs\":[0],"
      "\"fork_versions\":[\"0xaa000000\",\"0xbb000000\"]}";
  c4_state_t s  = make_state();
  json_t     ja = json_parse(a);
  json_t     jb = json_parse(b);
  TEST_ASSERT_TRUE(c4_eth_register_chain(&s, ja));
  TEST_ASSERT_TRUE(c4_eth_register_chain(&s, jb));
  TEST_ASSERT_NOT_NULL(c4_eth_get_chain_spec((chain_id_t) 424246));
  TEST_ASSERT_NOT_NULL(c4_eth_get_chain_spec((chain_id_t) 424247));
  c4_state_free(&s);
}

// Guard against a NULL state pointer. Must fail cleanly instead of
// dereferencing. The previous behaviour of silently succeeding would make
// bindings miss any registration error.
void test_eth_register_chain_rejects_null_state(void) {
  json_t j = json_parse(ETH_VALID_JSON);
  TEST_ASSERT_FALSE(c4_eth_register_chain(NULL, j));
}
#endif // CHAIN_ETH

#if !defined(NO_CHAIN_EEZ)
static const char EEZ_VALID_JSON[] =
    "{"
    "\"chain_id\":555123,"
    "\"rollup_id\":7,"
    "\"proxy\":\"0x4b2760f84b83d6b3829e30a7573b0172b6d6402c\","
    "\"name\":\"RollupY\""
    "}";

void test_eez_register_chain_populates_config(void) {
  c4_state_t s = make_state();
  json_t     j = json_parse(EEZ_VALID_JSON);
  TEST_ASSERT_TRUE(c4_eez_register_chain(&s, j));

  const eez_chain_config_t* cfg = eez_get_chain_config((chain_id_t) 555123);
  TEST_ASSERT_NOT_NULL(cfg);
  TEST_ASSERT_EQUAL_UINT64(7, cfg->rollup_id);
  TEST_ASSERT_EQUAL_HEX8(0x4b, cfg->proxy[0]);
  TEST_ASSERT_EQUAL_HEX8(0x2c, cfg->proxy[19]);
  TEST_ASSERT_EQUAL_STRING("RollupY", cfg->name);

  c4_state_free(&s);
}

void test_eez_register_chain_rejects_short_proxy(void) {
  const char* bad =
      "{\"chain_id\":555124,\"rollup_id\":1,\"proxy\":\"0xdeadbeef\"}";
  c4_state_t s = make_state();
  json_t     j = json_parse(bad);
  TEST_ASSERT_FALSE(c4_eez_register_chain(&s, j));
  TEST_ASSERT_NOT_NULL(s.error);
  c4_state_free(&s);
}

void test_dispatcher_routes_to_eez_handler(void) {
  c4_state_t s = make_state();
  json_t     j = json_parse(EEZ_VALID_JSON);
  TEST_ASSERT_TRUE(c4_register_chain(&s, C4_CHAIN_TYPE_EEZ, j));
  TEST_ASSERT_NOT_NULL(eez_get_chain_config((chain_id_t) 555123));
  c4_state_free(&s);
}
#endif // CHAIN_EEZ

#if !defined(NO_CHAIN_OP)
static const char OP_VALID_JSON[] =
    "{"
    "\"chain_id\":777001,"
    "\"sequencer_address\":\"0xAAAA45d9549EDA09E70937013520214382Ffc4A2\""
    "}";

void test_op_register_chain_populates_config(void) {
  c4_state_t s = make_state();
  json_t     j = json_parse(OP_VALID_JSON);
  TEST_ASSERT_TRUE(c4_op_register_chain(&s, j));

  const op_chain_config_t* cfg = op_get_chain_config(777001);
  TEST_ASSERT_NOT_NULL(cfg);
  TEST_ASSERT_EQUAL_UINT64(777001, cfg->chain_id);
  TEST_ASSERT_NOT_NULL(cfg->sequencer_address);
  TEST_ASSERT_EQUAL_HEX8(0xAA, (uint8_t) cfg->sequencer_address[0]);

  c4_state_free(&s);
}

void test_op_register_chain_rejects_duplicate(void) {
  // Chain 10 (OP mainnet) is in `op_chain_configs[]` -> runtime duplicate.
  const char* dup =
      "{\"chain_id\":10,\"sequencer_address\":\"0x0000000000000000000000000000000000000000\"}";
  c4_state_t s = make_state();
  json_t     j = json_parse(dup);
  TEST_ASSERT_FALSE(c4_op_register_chain(&s, j));
  TEST_ASSERT_NOT_NULL(s.error);
  c4_state_free(&s);
}

void test_dispatcher_routes_to_op_handler(void) {
  c4_state_t s = make_state();
  json_t     j = json_parse(OP_VALID_JSON);
  TEST_ASSERT_TRUE(c4_register_chain(&s, C4_CHAIN_TYPE_OP, j));
  TEST_ASSERT_NOT_NULL(op_get_chain_config(777001));
  c4_state_free(&s);
}
#endif // CHAIN_OP

#if !defined(NO_CHAIN_ARB)
static const char ARB_VALID_JSON[] =
    "{"
    "\"chain_id\":888001,"
    "\"sequencer_inbox\":\"0x1c479675ad559dc151f6ec7ed3fbf8cee79582b6\""
    "}";

void test_arb_register_chain_adds_inbox(void) {
  c4_state_t s = make_state();
  json_t     j = json_parse(ARB_VALID_JSON);
  TEST_ASSERT_TRUE(c4_arb_register_chain(&s, j));

  uint8_t addr[20] = {0};
  TEST_ASSERT_TRUE(arb_sequencer_inbox((chain_id_t) 888001, addr));
  TEST_ASSERT_EQUAL_HEX8(0x1c, addr[0]);
  TEST_ASSERT_EQUAL_HEX8(0xb6, addr[19]);

  c4_state_free(&s);
}

void test_arb_register_chain_rejects_duplicate_of_builtin(void) {
  // `C4_CHAIN_ARB_ONE` already has a hard-coded sequencer inbox.
  char dup[160];
  snprintf(dup, sizeof(dup),
           "{\"chain_id\":%llu,\"sequencer_inbox\":\"0x0000000000000000000000000000000000000000\"}",
           (unsigned long long) C4_CHAIN_ARB_ONE);
  c4_state_t s = make_state();
  json_t     j = json_parse(dup);
  TEST_ASSERT_FALSE(c4_arb_register_chain(&s, j));
  TEST_ASSERT_NOT_NULL(s.error);
  c4_state_free(&s);
}

void test_dispatcher_routes_to_arb_handler(void) {
  c4_state_t s = make_state();
  json_t     j = json_parse(ARB_VALID_JSON);
  TEST_ASSERT_TRUE(c4_register_chain(&s, C4_CHAIN_TYPE_ARBITRUM, j));
  uint8_t addr[20] = {0};
  TEST_ASSERT_TRUE(arb_sequencer_inbox((chain_id_t) 888001, addr));
  c4_state_free(&s);
}
#endif // CHAIN_ARB

// Any chain-type without a registered handler must land in the default arm of
// the generated dispatcher and surface a meaningful error.
void test_dispatcher_rejects_unknown_chain_type(void) {
  c4_state_t s = make_state();
  // C4_CHAIN_TYPE_SOLANA has no verifier plugin in this build -> no case arm.
  json_t j = json_parse("{\"chain_id\":1}");
  TEST_ASSERT_FALSE(c4_register_chain(&s, C4_CHAIN_TYPE_SOLANA, j));
  TEST_ASSERT_NOT_NULL(s.error);
  c4_state_free(&s);
}

/* ────────────────────────── c4_configure_chain ────────────────────────── */

#if !defined(NO_CHAIN_ETH)
// Wraps a chain-type payload into the JSON envelope expected by
// `c4_configure_chain` (a `"chain_type":...` plus the inner fields).
static const char CONFIGURE_ETH_STRING_TYPE[] =
    "{"
    "\"chain_type\":\"ethereum\","
    "\"chain_id\":424250,"
    "\"genesis_validators_root\":\"0x0000000000000000000000000000000000000000000000000000000000000000\","
    "\"fork_epochs\":[0],"
    "\"fork_versions\":[\"0x00000000\",\"0x01000000\"]"
    "}";

// Numeric `chain_type` must still be accepted so bindings can forward the raw
// enum value verbatim.
static const char CONFIGURE_ETH_NUMERIC_TYPE[] =
    "{"
    "\"chain_type\":0," // C4_CHAIN_TYPE_ETHEREUM
    "\"chain_id\":424251,"
    "\"genesis_validators_root\":\"0x0000000000000000000000000000000000000000000000000000000000000000\","
    "\"fork_epochs\":[0],"
    "\"fork_versions\":[\"0x00000000\",\"0x01000000\"]"
    "}";

void test_configure_chain_accepts_string_type(void) {
  char* err = c4_configure_chain(CONFIGURE_ETH_STRING_TYPE);
  TEST_ASSERT_NULL(err);
  TEST_ASSERT_NOT_NULL(c4_eth_get_chain_spec((chain_id_t) 424250));
}

void test_configure_chain_accepts_numeric_type(void) {
  char* err = c4_configure_chain(CONFIGURE_ETH_NUMERIC_TYPE);
  TEST_ASSERT_NULL(err);
  TEST_ASSERT_NOT_NULL(c4_eth_get_chain_spec((chain_id_t) 424251));
}

void test_configure_chain_propagates_handler_error(void) {
  // Missing all mandatory fields except chain_id -> inner handler must reject.
  const char* broken =
      "{\"chain_type\":\"ethereum\",\"chain_id\":424252}";
  char* err = c4_configure_chain(broken);
  TEST_ASSERT_NOT_NULL(err);
  // Inner handler stamped its own prefix.
  TEST_ASSERT_NOT_NULL(strstr(err, "c4_eth_register_chain"));
  free(err);
}
#endif // CHAIN_ETH

void test_configure_chain_rejects_missing_chain_type(void) {
  char* err = c4_configure_chain("{\"chain_id\":1}");
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_NOT_NULL(strstr(err, "chain_type"));
  free(err);
}

void test_configure_chain_rejects_unknown_type_string(void) {
  char* err = c4_configure_chain("{\"chain_type\":\"solana\"}");
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_NOT_NULL(strstr(err, "unknown chain_type"));
  free(err);
}

void test_configure_chain_rejects_non_object(void) {
  char* err = c4_configure_chain("[]");
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_NOT_NULL(strstr(err, "JSON object"));
  free(err);
}

void test_configure_chain_rejects_null_conf(void) {
  char* err = c4_configure_chain(NULL);
  TEST_ASSERT_NOT_NULL(err);
  free(err);
  err = c4_configure_chain("");
  TEST_ASSERT_NOT_NULL(err);
  free(err);
}

int main(void) {
  UNITY_BEGIN();
#if !defined(NO_CHAIN_ETH)
  RUN_TEST(test_eth_register_chain_populates_spec);
  RUN_TEST(test_eth_register_chain_rejects_duplicate);
  RUN_TEST(test_eth_register_chain_rejects_mismatched_fork_version_count);
  RUN_TEST(test_eth_register_chain_rejects_invalid_schema);
  RUN_TEST(test_dispatcher_routes_to_eth_handler);
  RUN_TEST(test_eth_register_chain_accepts_hex_chain_id);
  RUN_TEST(test_eth_register_chain_preserves_blob_tables);
  RUN_TEST(test_eth_register_chain_registers_multiple_chains);
  RUN_TEST(test_eth_register_chain_rejects_null_state);
#endif
#if !defined(NO_CHAIN_EEZ)
  RUN_TEST(test_eez_register_chain_populates_config);
  RUN_TEST(test_eez_register_chain_rejects_short_proxy);
  RUN_TEST(test_dispatcher_routes_to_eez_handler);
#endif
#if !defined(NO_CHAIN_OP)
  RUN_TEST(test_op_register_chain_populates_config);
  RUN_TEST(test_op_register_chain_rejects_duplicate);
  RUN_TEST(test_dispatcher_routes_to_op_handler);
#endif
#if !defined(NO_CHAIN_ARB)
  RUN_TEST(test_arb_register_chain_adds_inbox);
  RUN_TEST(test_arb_register_chain_rejects_duplicate_of_builtin);
  RUN_TEST(test_dispatcher_routes_to_arb_handler);
#endif
  RUN_TEST(test_dispatcher_rejects_unknown_chain_type);

#if !defined(NO_CHAIN_ETH)
  RUN_TEST(test_configure_chain_accepts_string_type);
  RUN_TEST(test_configure_chain_accepts_numeric_type);
  RUN_TEST(test_configure_chain_propagates_handler_error);
#endif
  RUN_TEST(test_configure_chain_rejects_missing_chain_type);
  RUN_TEST(test_configure_chain_rejects_unknown_type_string);
  RUN_TEST(test_configure_chain_rejects_non_object);
  RUN_TEST(test_configure_chain_rejects_null_conf);

  return UNITY_END();
}
