/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */

/*
 * Offline dispatch tests for the Ethereum prover method router
 * (`eth_prover_execute` in `src/chains/eth/prover/eth_prover.c`).
 *
 * These tests do NOT require recorded fixtures: they only assert how a method
 * name is routed. A recognized method dispatches to a proof generator, which
 * emits a pending data request (C4_PENDING) without ever setting the
 * "Unsupported method" error. An unrecognized method sets exactly that error.
 *
 * This locks in the change that added `colibri_proofBlock` as an alias handled
 * by `c4_proof_block`, so a future refactor cannot silently drop it and fall
 * through to the "Unsupported method" branch.
 */

#include "../../bindings/colibri_common.h"
#include "json.h"
#include "prover.h"
#include "state.h"
#include "unity.h"
#include <stdlib.h>
#include <string.h>

#define UNSUPPORTED_METHOD_MSG "Unsupported method"

void setUp(void) {}
void tearDown(void) {}

// Runs one prover pass for the given method/params and reports whether the
// dispatcher rejected it with the "Unsupported method" error.
static bool method_is_unsupported(const char* method, const char* params) {
  prover_ctx_t* ctx = c4_prover_create((char*) method, (char*) params, C4_CHAIN_MAINNET, 0);
  TEST_ASSERT_NOT_NULL(ctx);
  c4_prover_execute(ctx);
  bool unsupported = ctx->state.error != NULL && strcmp(ctx->state.error, UNSUPPORTED_METHOD_MSG) == 0;
  c4_prover_free(ctx);
  return unsupported;
}

// The new alias must be routed to c4_proof_block, i.e. recognized by the dispatcher.
void test_colibri_proofBlock_is_recognized(void) {
  TEST_ASSERT_FALSE_MESSAGE(method_is_unsupported("colibri_proofBlock", "[\"0x1\",true]"),
                            "colibri_proofBlock must be routed to c4_proof_block, not rejected as unsupported");
}

// The pre-existing block aliases must keep working (regression guard).
void test_block_aliases_recognized(void) {
  TEST_ASSERT_FALSE_MESSAGE(method_is_unsupported("eth_getBlockByNumber", "[\"0x1\",true]"),
                            "eth_getBlockByNumber must remain recognized");
  TEST_ASSERT_FALSE_MESSAGE(method_is_unsupported("eth_getBlockByHash",
                                                  "[\"0x0000000000000000000000000000000000000000000000000000000000000001\",true]"),
                            "eth_getBlockByHash must remain recognized");
}

// Negative control: proves the detection mechanism actually distinguishes
// recognized from unrecognized methods.
void test_bogus_method_is_unsupported(void) {
  TEST_ASSERT_TRUE_MESSAGE(method_is_unsupported("colibri_definitelyNotAMethod", "[]"),
                           "an unknown method must be rejected with the Unsupported method error");
}

// colibri_proofBlock must NOT be remote-delegated: in hybrid mode it has to be
// routed to the local sub-prover so it reuses the prover-side header cache and
// never leaks which transaction the caller is resolving. This is the core
// security guarantee behind the PAP hybrid tx path.
void test_colibri_proofBlock_is_not_remote_delegated(void) {
  TEST_ASSERT_FALSE_MESSAGE(c4_is_remote_delegated_prover_method("colibri_proofBlock"),
                            "colibri_proofBlock must be handled locally, not remote-delegated");
}

// The three block/header methods remain delegated so their immutable proofs stay
// CDN-cacheable (regression guard for the delegation list).
void test_block_methods_are_remote_delegated(void) {
  TEST_ASSERT_TRUE(c4_is_remote_delegated_prover_method("eth_getBlockHeader"));
  TEST_ASSERT_TRUE(c4_is_remote_delegated_prover_method("eth_getBlockByNumber"));
  TEST_ASSERT_TRUE(c4_is_remote_delegated_prover_method("eth_getBlockByHash"));
  TEST_ASSERT_FALSE(c4_is_remote_delegated_prover_method(NULL));
  TEST_ASSERT_FALSE(c4_is_remote_delegated_prover_method("eth_call"));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_colibri_proofBlock_is_recognized);
  RUN_TEST(test_block_aliases_recognized);
  RUN_TEST(test_bogus_method_is_unsupported);
  RUN_TEST(test_colibri_proofBlock_is_not_remote_delegated);
  RUN_TEST(test_block_methods_are_remote_delegated);
  return UNITY_END();
}
