// Regression test for eth_getStorageAt / eth_getProof request→proof binding.
//
// The raw account/trie verification is already correct: the account root is
// bound to the EL header's stateRoot, the keccak of the account's address is
// bound to the state-trie path, and the requested address is bound to
// `ctx->proof.address`. What was missing is the same treatment on the
// *request* side: the slot key(s) in `ctx->proof.storageProof` could differ
// from the caller's requested key and verification would still pass, and an
// explicit block number in the request could be answered by the same genuine
// proof from any block.
//
// This test takes the recorded eth_getStorageAt1 proof, passes it verbatim
// to the verifier with the original request (control) and with presumably
// identical call args that differ in exactly one field each, asserting each
// mutation is rejected with the named binding error.

#include "c4_assert.h"
#include "unity.h"

void setUp(void)    { reset_local_filecache(); }
void tearDown(void) { reset_local_filecache(); }

static void feed_requests(c4_state_t* state, char* fixture) {
  data_request_t* req;
  while ((req = c4_state_get_pending_request(state))) {
    char* name = c4_req_mockname(req);
    char  path[2048];
    snprintf(path, sizeof(path), "%s/%s", fixture, name);
    bytes_t content = read_testdata(path);
    if (!content.data) {
      fprintf(stderr, "Missing fixture request: %s\n", path);
      abort();
    }
    req->response = content;
    free(name);
  }
}

static bytes_t prove_storage_at(char* fixture) {
  prover_ctx_t* p = c4_prover_create(
      "eth_getStorageAt",
      "[\"0xa0b86991c6218b36c1d19d4a2e9eb0ce3606eb48\",\"0x7050c9e0f4ca769c69bd3a8ef740bc37934f8e2c036e5a723fd8ee048ed3f8c3\",\"latest\"]",
      C4_CHAIN_MAINNET, 0);
  c4_status_t status;
  while ((status = c4_prover_execute(p)) == C4_PENDING) feed_requests(&p->state, fixture);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, status, p->state.error ? p->state.error : "prover failed");
  bytes_t proof = bytes_dup(p->proof);
  c4_prover_free(p);
  return proof;
}

// Drives the public verifier with the given method/args against a real
// recorded proof, and asserts the named rejection cause appears.
static void assert_verifier_rejects(char* fixture, bytes_t proof, char* method, char* args, const char* expected_cause) {
  verify_ctx_t ctx = {0};
  c4_status_t  status;
  reset_local_filecache();
  set_state(C4_CHAIN_MAINNET, fixture);

  status = c4_verify_init(&ctx, proof, method, json_parse(args), C4_CHAIN_MAINNET, 0);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, status, ctx.state.error ? ctx.state.error : "verify_init failed");
  while ((status = c4_verify(&ctx)) == C4_PENDING) feed_requests(&ctx.state, fixture);

  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_ERROR, status, bprintf(NULL, "must reject args %s", args));
  TEST_ASSERT_FALSE_MESSAGE(ctx.success, "success must be false");
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(ctx.state.error, expected_cause),
                               bprintf(NULL, "expected cause containing '%s', got '%s'",
                                       expected_cause, ctx.state.error));
  c4_verify_free_data(&ctx);
}

// Drives the public verifier and asserts success.
static void assert_verifier_accepts(char* fixture, bytes_t proof, char* method, char* args) {
  verify_ctx_t ctx = {0};
  c4_status_t  status;
  reset_local_filecache();
  set_state(C4_CHAIN_MAINNET, fixture);

  status = c4_verify_init(&ctx, proof, method, json_parse(args), C4_CHAIN_MAINNET, 0);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, status, ctx.state.error ? ctx.state.error : "verify_init failed");
  while ((status = c4_verify(&ctx)) == C4_PENDING) feed_requests(&ctx.state, fixture);

  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, status, ctx.state.error ? ctx.state.error : "unexpected verify error");
  TEST_ASSERT_TRUE_MESSAGE(ctx.success, "success must be true");
  c4_verify_free_data(&ctx);
}

void test_original_request_still_accepts(void) {
  char* fixture = "eth_getStorageAt1";
  bytes_t proof = prove_storage_at(fixture);
  assert_verifier_accepts(fixture, proof, "eth_getStorageAt",
                          "[\"0xa0b86991c6218b36c1d19d4a2e9eb0ce3606eb48\",\"0x7050c9e0f4ca769c69bd3a8ef740bc37934f8e2c036e5a723fd8ee048ed3f8c3\",\"latest\"]");
  reset_local_filecache();
  free(proof.data);
}

void test_different_slot_rejected(void) {
  char* fixture = "eth_getStorageAt1";
  bytes_t proof = prove_storage_at(fixture);
  // Only the requested slot changes: 32 zero bytes vs the proven 0x7050c9e0...
  assert_verifier_rejects(fixture, proof, "eth_getStorageAt",
                          "[\"0xa0b86991c6218b36c1d19d4a2e9eb0ce3606eb48\",\"0x0000000000000000000000000000000000000000000000000000000000000000\",\"latest\"]",
                          "storage proof key does not match requested slot");
  reset_local_filecache();
  free(proof.data);
}

void test_different_explicit_block_rejected(void) {
  char* fixture = "eth_getStorageAt1";
  bytes_t proof = prove_storage_at(fixture);
  // Only the explicit block number changes: "0x1" vs the proven block.
  assert_verifier_rejects(fixture, proof, "eth_getStorageAt",
                          "[\"0xa0b86991c6218b36c1d19d4a2e9eb0ce3606eb48\",\"0x00\",\"0x1\"]",
                          "block number in request does not match the execution header");
  reset_local_filecache();
  free(proof.data);
}

void test_different_address_still_rejected(void) {
  char* fixture = "eth_getStorageAt1";
  bytes_t proof = prove_storage_at(fixture);
  // Use the *correct* proven slot so the address check is the one that fires.
  assert_verifier_rejects(fixture, proof, "eth_getStorageAt",
                          "[\"0x1111111111111111111111111111111111111111\",\"0x7050c9e0f4ca769c69bd3a8ef740bc37934f8e2c036e5a723fd8ee048ed3f8c3\",\"latest\"]",
                          "proof does not match the address in request");
  reset_local_filecache();
  free(proof.data);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_original_request_still_accepts);
  RUN_TEST(test_different_slot_rejected);
  RUN_TEST(test_different_explicit_block_rejected);
  RUN_TEST(test_different_address_still_rejected);
  return UNITY_END();
}
