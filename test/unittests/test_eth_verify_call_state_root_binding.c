// Regression test for direct `eth_call` EL-header ↔ account-state binding.
//
// The direct call-verify path built and checked an account trie from the
// supplied accounts, verified the EL header in isolation, but never compared
// the two stateRoots. A real EL header from a different call could therefore
// be paired with a hand-built one-account trie that carries attacker-chosen
// bytecode, the EVM outputs attacker data, and the call result was accepted
// because neither side's stateRoot was ever compared to the other.
//
// The test builds a forged call proof whose account trie root disagrees with
// an otherwise-real EL header (test data from eth_getStorageAt1), drives the
// public verifier on an `eth_call` for that address, and asserts rejection.

#include "c4_assert.h"
#include "call_ctx.h"
#include "el_header.h"
#include "eth_account.h"
#include "eth_tools.h"
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

static bytes_t build_call_proof_with_mismatched_account_root(bytes_t storage_proof_request) {
  ssz_ob_t request = {.bytes = storage_proof_request, .def = eth_ssz_verification_type(ETH_SSZ_VERIFY_REQUEST)};
  ssz_ob_t proof    = ssz_get(&request, "proof");
  ssz_ob_t el_proof = ssz_get(&proof, "elProof");

  // Hand-build a one-account Patricia trie whose leaf encodes the attacker's
  // code bytes. This trie's root is the *wrong* state root for the EL header
  // reused below: it is self-consistent but does not match the real chain.
  uint8_t   address[20];
  memset(address, 0x11, sizeof(address));
  uint8_t   code[] = {0x60, 0x2a, 0x60, 0x00, 0x52, 0x60, 0x20, 0x60, 0x00, 0xf3}; // PUSH1 42, PUSH1 0, MSTORE8/RETURN
  bytes32_t address_hash = {0}, code_hash = {0};
  keccak(bytes(address, sizeof(address)), address_hash);
  keccak(bytes(code, sizeof(code)), code_hash);

  // account_rlp = RLP([nonce=1, balance=1, storageRoot=EMPTY, codeHash])
  uint8_t account_rlp[70] = {0xf8, 0x44, 0x01, 0x01, 0xa0};
  memcpy(account_rlp + 5, EMPTY_ROOT_HASH, 32);
  account_rlp[37] = 0xa0;
  memcpy(account_rlp + 38, code_hash, 32);

  // leaf = RLP([ RLP(prefix || address_hash), RLP(account_rlp) ])
  uint8_t leaf[108] = {0xf8, 0x6a, 0xa1, 0x20};
  memcpy(leaf + 4, address_hash, 32);
  leaf[36] = 0xb8;
  leaf[37] = (uint8_t) sizeof(account_rlp);
  memcpy(leaf + 38, account_rlp, sizeof(account_rlp));

  ssz_builder_t call = ssz_builder_for_type(ETH_SSZ_VERIFY_CALL_PROOF);
  ssz_builder_t accounts = ssz_builder_for_def(ssz_get_def(call.def, "accounts"));
  ssz_builder_t account = ssz_builder_for_def(accounts.def->def.vector.type);
  ssz_builder_t ap = ssz_builder_for_def(ssz_get_def(account.def, "accountProof"));
  ssz_add_dynamic_list_bytes(&ap, 1, bytes(leaf, sizeof(leaf)));
  ssz_add_builders(&account, "accountProof", ap);
  ssz_add_bytes(&account, "address", bytes(address, sizeof(address)));
  ssz_builder_t code_builder = ssz_builder_for_def(ssz_get_def(ssz_get_def(account.def, "code"), "code"));
  buffer_append(&code_builder.fixed, bytes(code, sizeof(code)));
  ssz_add_builders(&account, "code", code_builder);
  ssz_builder_t storage = ssz_builder_for_def(ssz_get_def(account.def, "storageProof"));
  ssz_add_builders(&account, "storageProof", storage);
  ssz_add_dynamic_list_builders(&accounts, 1, account);
  ssz_add_builders(&call, "accounts", accounts);
  ssz_builder_t el = ssz_builder_for_def(el_proof.def);
  buffer_append(&el.fixed, el_proof.bytes);
  ssz_add_builders(&call, "elProof", el);
  return eth_create_proof_request(1, NULL_SSZ_BUILDER, call, NULL_SSZ_BUILDER);
}

void test_call_rejects_account_root_that_disagrees_with_el_header() {
  char* fixture      = "eth_getStorageAt1";
  char* storage_args = "[\"0xa0b86991c6218b36c1d19d4a2e9eb0ce3606eb48\",\"0x7050c9e0f4ca769c69bd3a8ef740bc37934f8e2c036e5a723fd8ee048ed3f8c3\",\"latest\"]";
  reset_local_filecache();
  set_state(1, fixture);

  // Use the real eth_getStorageAt-proof fixture's EL proof, which is signed
  // and committed to the beacon chain, so the header itself is trustworthy.
  // The forged part is only the account trie (which is attacker-chosen).
  bytes_t original = NULL_BYTES;
  {
    prover_ctx_t* p = c4_prover_create("eth_getStorageAt", storage_args, 1, 0);
    c4_status_t   status;
    while ((status = c4_prover_execute(p)) == C4_PENDING) feed_requests(&p->state, fixture);
    TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, status, p->state.error ? p->state.error : "prover failed");
    original = bytes_dup(p->proof);
    c4_prover_free(p);
  }
  TEST_ASSERT_NOT_NULL(original.data);

  bytes_t forged = build_call_proof_with_mismatched_account_root(original);

  verify_ctx_t ctx = {0};
  char* args = "[{\"to\":\"0x1111111111111111111111111111111111111111\",\"from\":\"0x1111111111111111111111111111111111111111\",\"data\":\"0x\"},\"latest\"]";
  c4_status_t status = c4_verify_init(&ctx, forged, "eth_call", json_parse(args), 1, 0);
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, status);

  reset_local_filecache();
  set_state(1, fixture);
  while ((status = c4_verify(&ctx)) == C4_PENDING) feed_requests(&ctx.state, fixture);

  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_ERROR, status,
                                "a call proof whose account stateRoot disagrees with the EL header must be rejected");
  TEST_ASSERT_FALSE_MESSAGE(ctx.success, "rejection must set success=false");
  TEST_ASSERT_NOT_NULL_MESSAGE(ctx.state.error, "rejection must name the cause");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(ctx.state.error, "state root mismatch"),
                               bprintf(NULL, "expected a state root mismatch error, got: %s",
                                       ctx.state.error ? ctx.state.error : "none"));

  c4_verify_free_data(&ctx);
  free(original.data);
  free(forged.data);
}

void test_call_accepts_call_proof_with_matching_account_root() {
  // Control: the repo's own eth_call fixture passes the binding check.
  char* method = "eth_call";
  char* args   = "[{\"to\":\"0xA0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48\",\"data\":\"0x70a0823100000000000000000000000037305b1cd40574e4c5ce33f8e8306be057fd7341\"},\"latest\"]";
  char* dir    = "eth_call1";
  char* expected_result = "\"0x0000000000000000000000000000000000000000000000000011c04f6834033e\"";
  verify_count(dir, method, args, C4_CHAIN_MAINNET, 1,
               C4_PROVER_FLAG_INCLUDE_CODE | C4_PROVER_FLAG_USE_DEBUG_TRACE, 0, expected_result, false);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_call_rejects_account_root_that_disagrees_with_el_header);
  RUN_TEST(test_call_accepts_call_proof_with_matching_account_root);
  return UNITY_END();
}
