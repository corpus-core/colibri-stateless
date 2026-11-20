// Regression test for proof-type substitution: an attacker-served SSZ
// C4Request whose `proof` union carries an *empty* `eth_verifyLogs`
// container used to be accepted for every proofable method, because the
// dispatcher routed on the union tag alone and never checked that the proof
// type matched the requested method. The fix binds method -> expected proof
// type at dispatch time.
//
// The crafted 51-byte request below is the payload in
// repro_empty_logs.c: arbitrary uint256 result,
// empty LogsProof list, no sync data. Before the fix all four proofable
// methods accepted it; after the fix they must fail with a proof-type
// mismatch while the method whose expected type is `eth_verifyLogs`
// continues to accept it.

#include "c4_assert.h"
#include "unity.h"

void setUp(void)    { reset_local_filecache(); }
void tearDown(void) { reset_local_filecache(); }

static void write_le32(uint8_t* p, uint32_t n) {
  for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t) (n >> (8 * i));
}

// Builds the 51-byte crafted request that substitutes an empty LogsProof for a
// method that expects an AccountProof / CallProof / BlockProof.
static bytes_t proof_type_substitution_request(void) {
  static uint8_t raw[51] = {1, 3, 0, 2};
  write_le32(raw + 4, 16);
  write_le32(raw + 8, 49);
  write_le32(raw + 12, 50);
  raw[16] = 3; /* data selector: arbitrary UINT256 value */
  raw[17] = 0xef;
  raw[18] = 0xbe;
  raw[19] = 0xad;
  raw[20] = 0xde;
  raw[49] = 4; /* proof selector: empty eth_verifyLogs (LogsProof, len 0) */
  raw[50] = 0; /* sync_data selector: none */
  return bytes(raw, sizeof(raw));
}

// Drives the public verifier on the crafted request and asserts the outcome.
static void assert_rejected(char* method, char* args) {
  bytes_t      request = proof_type_substitution_request();
  verify_ctx_t ctx     = {0};
  c4_status_t  status  = c4_verify_from_bytes(&ctx, request, method, json_parse(args), C4_CHAIN_MAINNET);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_ERROR, status, "crafted proof-type-substituted request must be rejected");
  TEST_ASSERT_FALSE_MESSAGE(ctx.success, "crafted proof-type-substituted request must not succeed");
  TEST_ASSERT_NOT_NULL_MESSAGE(ctx.state.error, "rejection must carry an error message");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(ctx.state.error, "proof type mismatch"),
                               bprintf(NULL, "rejection must carry the proof-type mismatch cause, got: %s", ctx.state.error));
  c4_verify_free_data(&ctx);
}

void test_empty_logs_proof_rejected_for_account_methods(void) {
  char* account = "[\"0x1111111111111111111111111111111111111111\",\"latest\"]";
  assert_rejected("eth_getBalance", account);
  assert_rejected("eth_getTransactionCount", account);
  assert_rejected("eth_blockNumber", "[]");
  assert_rejected("eth_call", "[{\"to\":\"0x1111111111111111111111111111111111111111\",\"data\":\"0x\"},\"latest\"]");
}

void test_proof_type_match_still_accepted_for_verifyLogs(void) {
  // The same crafted request, but the method now *expects* an eth_verifyLogs
  // proof: the proof type matches, and empty logs verify as "no
  // logs".
  bytes_t      request = proof_type_substitution_request();
  verify_ctx_t ctx     = {0};
  c4_status_t  status  = c4_verify_from_bytes(&ctx, request, "eth_verifyLogs",
                                              json_parse("[[{\"address\":[\"0xdac17f958d2ee523a2206206994597c13d831ec7\"],\"fromBlock\":\"0x14d7970\",\"toBlock\":\"0x14d7970\"}],[]]"),
                                              C4_CHAIN_MAINNET);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, status, "matching proof type must still be verified");
  TEST_ASSERT_TRUE_MESSAGE(ctx.success, "matching proof type must succeed");
  c4_verify_free_data(&ctx);
}

void test_real_logs_fixture_still_passes(void) {
  // End-to-end control: eth_getLogs proof must continue to verify.
  verify("eth_getLogs1", "eth_getLogs",
         "[{\"address\":[\"0xdac17f958d2ee523a2206206994597c13d831ec7\"],\"fromBlock\":\"0x14d7970\",\"toBlock\":\"0x14d7970\"}]",
         C4_CHAIN_MAINNET);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_logs_proof_rejected_for_account_methods);
  RUN_TEST(test_proof_type_match_still_accepted_for_verifyLogs);
  RUN_TEST(test_real_logs_fixture_still_passes);
  return UNITY_END();
}
