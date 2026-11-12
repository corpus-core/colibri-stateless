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

// Tests of the `eip2935` variant of `ETH_EL_PROOF_UNION` (`c4_verify_eip2935_block`) and
// of `eth_verify_storage_slot`, on which it is built.
//
// The proofs are synthetic: a history contract with one slot in small Merkle-Patricia
// tries, a "newer" header that commits to that state, and an "older" header whose hash
// the slot holds. The newer block is referenced by the `blockHash` variant and attached
// to the verify context as a verified header, so no consensus proof is involved and
// every rejection path can be reached with an otherwise consistent proof.

#include "c4_assert.h"
#include "eip2935.h"
#include "el_header.h"
#include "eth_account.h"
#include "eth_verify.h"
#include "header_cache.h"
#include "synth_proofs.h"
#include "unity.h"

#define NEWER SYNTH_EIP2935_NEWER

static const synth_eip2935_opts_t VALID = {.older = NEWER - 5, .account = {.storage_proofs = -1}};

void setUp(void) {
  reset_local_filecache();
  c4_header_cache_clear();
}

void tearDown(void) {
  reset_local_filecache();
  c4_header_cache_clear();
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

// Expects `c4_verify_eip2935_block` to accept the proof of `opts` and to hand out the older header.
static void assert_accepted(synth_eip2935_opts_t opts, const char* message) {
  synth_eip2935_case_t c      = synth_eip2935_case(opts, C4_CHAIN_MAINNET);
  bytes_t              header = NULL_BYTES;
  bytes32_t            hash   = {0};
  c4_status_t          status = c4_verify_eip2935_block(&c.ctx, c.proof, &header, hash);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, status, c.ctx.state.error ? c.ctx.state.error : message);
  TEST_ASSERT_NULL_MESSAGE(c.ctx.state.error, message);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(c.older_header.len, header.len, message);
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(c.older_header.data, header.data, header.len, message);
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(c.older_hash, hash, 32, message);
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(opts.older, eth_el_header_get_uint64(header, EL_BLOCK_NUMBER), message);
  synth_eip2935_free(&c);
}

// Expects `c4_verify_eip2935_block` to reject the proof of `opts` with an error containing
// `expected`, and not to cache the older header.
static void assert_rejected(synth_eip2935_opts_t opts, const char* expected) {
  synth_eip2935_case_t c      = synth_eip2935_case(opts, C4_CHAIN_MAINNET);
  bytes_t              header = NULL_BYTES;
  bytes32_t            hash   = {0};
  c4_status_t          status = c4_verify_eip2935_block(&c.ctx, c.proof, &header, hash);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_ERROR, status, expected);
  TEST_ASSERT_NOT_NULL_MESSAGE(c.ctx.state.error, expected);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(c.ctx.state.error, expected), c.ctx.state.error);
  TEST_ASSERT_NULL_MESSAGE(header.data, "no header must be handed out on error");
  TEST_ASSERT_NULL_MESSAGE(c4_header_cache_get_by_hash(C4_CHAIN_MAINNET, c.older_hash), "a rejected header must not be cached");
  synth_eip2935_free(&c);
}

// ---------------------------------------------------------------------------
// constants
// ---------------------------------------------------------------------------

void test_history_address_and_slot(void) {
  // 0x0000F90827F1C53a10cb7A02335B175320002935
  uint8_t address[20] = {0};
  hex_to_bytes("0000F90827F1C53a10cb7A02335B175320002935", -1, bytes(address, 20));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(address, ETH_EIP2935_HISTORY_ADDRESS, 20);
  TEST_ASSERT_EQUAL_INT(8191, ETH_EIP2935_HISTORY_WINDOW);

  bytes32_t slot;
  memset(slot, 0xee, 32);
  eth_eip2935_slot(0, slot);
  TEST_ASSERT_TRUE(bytes_all_zero(bytes(slot, 32)));

  // 8190 = 0x1ffe is the last slot, 8191 wraps to slot 0
  eth_eip2935_slot(8190, slot);
  TEST_ASSERT_TRUE(bytes_all_zero(bytes(slot, 30)));
  TEST_ASSERT_EQUAL_UINT8(0x1f, slot[30]);
  TEST_ASSERT_EQUAL_UINT8(0xfe, slot[31]);
  eth_eip2935_slot(8191, slot);
  TEST_ASSERT_TRUE(bytes_all_zero(bytes(slot, 32)));

  // 581168 % 8191 = 7798 = 0x1e76 (a block of RollupX)
  eth_eip2935_slot(581168, slot);
  TEST_ASSERT_EQUAL_UINT8(0x1e, slot[30]);
  TEST_ASSERT_EQUAL_UINT8(0x76, slot[31]);

  // the number is reduced before it is narrowed
  eth_eip2935_slot(UINT64_MAX, slot);
  TEST_ASSERT_TRUE(bytes_all_zero(bytes(slot, 30)));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX % 8191, ((uint64_t) slot[30] << 8) | slot[31]);
}

// ---------------------------------------------------------------------------
// eth_verify_storage_slot
// ---------------------------------------------------------------------------

void test_storage_slot(void) {
  uint8_t    contract[20];
  bytes32_t  slot;
  bytes32_t  stored;
  bytes32_t  value;
  c4_state_t state = {0};
  memset(contract, 0xc1, 20);
  memset(slot, 0x51, 32);
  memset(stored, 0xab, 32);
  memset(value, 0xee, 32);

  synth_t synth = synth_account_proof(contract, slot, bytes(stored, 32), (synth_opts_t) {.storage_proofs = -1});
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, eth_verify_storage_slot(&state, synth.account, contract, slot, bytes(synth.state_root, 32), value));
  TEST_ASSERT_NULL(state.error);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(stored, value, 32);

  // the same proof is no proof for another contract, another slot or another state
  uint8_t   other_contract[20];
  bytes32_t other;
  memset(other_contract, 0xc2, 20);
  memset(other, 0x52, 32);
  struct {
    const uint8_t* contract;
    const uint8_t* slot;
    bytes_t        root;
    const char*    expected;
  } cases[] = {
      {other_contract, slot, bytes(synth.state_root, 32), "account proof is not about the expected contract!"},
      {contract, other, bytes(synth.state_root, 32), "storage proof is not about the expected slot!"},
      {contract, slot, bytes(other, 32), "account proof of the contract does not match the state root!"},
      {contract, slot, bytes(synth.state_root, 31), "missing state root for the storage proof!"},
      {contract, slot, NULL_BYTES, "missing state root for the storage proof!"},
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    memset(value, 0xee, 32);
    TEST_ASSERT_EQUAL_INT_MESSAGE(C4_ERROR, eth_verify_storage_slot(&state, synth.account, cases[i].contract, cases[i].slot, cases[i].root, value), cases[i].expected);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(state.error, cases[i].expected), state.error);
    TEST_ASSERT_TRUE_MESSAGE(bytes_all_zero(bytes(value, 32)), "value must stay zero on error");
    c4_state_free(&state);
    state = (c4_state_t) {0};
  }
  safe_free(synth.account.bytes.data);
}

// A slot that is not in the trie has the value 0. That is never reported as a value:
// neither as an exclusion proof nor as a proof that stops above the leaf.
void test_storage_slot_without_value(void) {
  uint8_t   contract[20];
  bytes32_t slot;
  bytes32_t stored;
  memset(contract, 0xc1, 20);
  memset(slot, 0x51, 32);
  memset(stored, 0xab, 32);

  synth_opts_t variants[4] = {{.storage_proofs = -1, .slot_missing = true}, {.storage_proofs = -1, .truncate_storage = true}, {.storage_proofs = -1, .empty_storage_proof = true}, {.storage_proofs = -1, .truncate_account = true}};
  const char*  expected[4] = {"invalid storage proof of the slot!", "invalid storage proof of the slot!", "invalid storage proof of the slot!", "invalid account proof of the contract!"};
  for (int i = 0; i < 4; i++) {
    c4_state_t state = {0};
    bytes32_t  value;
    synth_t    synth = synth_account_proof(contract, slot, bytes(stored, 32), variants[i]);
    memset(value, 0xee, 32);
    TEST_ASSERT_EQUAL_INT_MESSAGE(C4_ERROR, eth_verify_storage_slot(&state, synth.account, contract, slot, bytes(synth.state_root, 32), value), expected[i]);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(state.error, expected[i]), state.error);
    TEST_ASSERT_TRUE(bytes_all_zero(bytes(value, 32)));
    c4_state_free(&state);
    safe_free(synth.account.bytes.data);
  }
}

// ---------------------------------------------------------------------------
// c4_verify_eip2935_block
// ---------------------------------------------------------------------------

void test_eip2935_valid(void) {
  assert_accepted(VALID, "five blocks before the newer block");
}

// The history window of block `n` is `n - 8191 .. n - 1`.
void test_eip2935_window(void) {
  synth_eip2935_opts_t opts = VALID;
  opts.older                = NEWER - 1;
  assert_accepted(opts, "the parent");
  opts.older = NEWER - 8191;
  assert_accepted(opts, "the oldest block of the window");

  // one block further the slot is the one of the parent: it holds another block
  opts.older = NEWER - 8192;
  assert_rejected(opts, "block is outside the EIP-2935 history window of the newer block!");
  opts.older = NEWER - 2 * 8191;
  assert_rejected(opts, "block is outside the EIP-2935 history window of the newer block!");
  opts.older = 1;
  assert_rejected(opts, "block is outside the EIP-2935 history window of the newer block!");

  // a block never holds its own hash, and none of a later block
  opts.older = NEWER;
  assert_rejected(opts, "block is outside the EIP-2935 history window of the newer block!");
  opts.older = NEWER + 1;
  assert_rejected(opts, "block is outside the EIP-2935 history window of the newer block!");
  opts.older = NEWER + 8191;
  assert_rejected(opts, "block is outside the EIP-2935 history window of the newer block!");
}

// A young chain: the window does not reach below block 0.
void test_eip2935_window_of_a_young_chain(void) {
  synth_eip2935_opts_t opts = VALID;
  opts.newer                = 100;
  opts.older                = 99;
  assert_accepted(opts, "block 99 in block 100");
  opts.older = 1;
  assert_accepted(opts, "block 1 in block 100");
}

// The genesis block is in the window of a young chain as well. Its number is 0, which is
// also what an unparsable number field gives: the header is accepted but not cached.
void test_eip2935_genesis_block(void) {
  synth_eip2935_opts_t opts   = VALID;
  bytes_t              header = NULL_BYTES;
  bytes32_t            hash   = {0};
  opts.newer                  = 100;
  opts.older                  = 0;
  synth_eip2935_case_t c      = synth_eip2935_case(opts, C4_CHAIN_MAINNET);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, c4_verify_eip2935_block(&c.ctx, c.proof, &header, hash), c.ctx.state.error ? c.ctx.state.error : "block 0 in block 100");
  TEST_ASSERT_EQUAL_UINT8_ARRAY(c.older_hash, hash, 32);
  TEST_ASSERT_EQUAL_UINT64(0, eth_el_header_get_uint64(header, EL_BLOCK_NUMBER));
#ifdef EL_HEADER_CACHE
  TEST_ASSERT_NULL(c4_header_cache_get_by_hash(C4_CHAIN_MAINNET, c.older_hash));
#endif
  synth_eip2935_free(&c);
}

// The newer block cannot prove itself: the proof of its own slot holds the block of 8191
// blocks before, and with the newer header as `elHeader` the window check fails.
void test_eip2935_newer_block_is_not_its_own_history(void) {
  synth_eip2935_opts_t opts  = VALID;
  synth_eip2935_case_t valid = synth_eip2935_case(VALID, C4_CHAIN_MAINNET);
  opts.older                 = NEWER;
  opts.older_header          = valid.newer_header;
  assert_rejected(opts, "block is outside the EIP-2935 history window of the newer block!");
  synth_eip2935_free(&valid);
}

// The value of another slot of the history contract is a valid block hash of the chain,
// but not the one of this block number.
void test_eip2935_slot_of_another_block(void) {
  synth_eip2935_opts_t opts = VALID;
  opts.slot_of_next         = true;
  assert_rejected(opts, "eip2935 proof is not about the history slot of the block!");
}

void test_eip2935_header_does_not_match(void) {
  bytes32_t            other;
  synth_eip2935_opts_t opts = VALID;
  memset(other, 0x99, 32);
  opts.stored = other;
  assert_rejected(opts, "header does not match the block hash stored in the EIP-2935 history contract!");

  // a slot that was never written
  opts                      = VALID;
  opts.account.slot_missing = true;
  assert_rejected(opts, "invalid storage proof of the slot!");
}

// The hash in the contract is the only thing that authenticates the header, so a preimage
// that is no header must not be handed on.
void test_eip2935_stored_value_is_no_header(void) {
  uint8_t              no_header[] = {0xc3, 0x01, 0x02, 0x03};
  synth_eip2935_opts_t opts        = VALID;
  opts.older_header                = bytes(no_header, sizeof(no_header));
  assert_rejected(opts, "the block stored in the EIP-2935 history contract is not a valid execution header!");

  uint8_t string_item[] = {0x83, 0x01, 0x02, 0x03};
  opts.older_header     = bytes(string_item, sizeof(string_item));
  assert_rejected(opts, "the block stored in the EIP-2935 history contract is not a valid execution header!");
}

void test_eip2935_account_proof(void) {
  uint8_t              other[20];
  synth_eip2935_opts_t opts = VALID;
  memset(other, 0xc7, 20);

  // a proof about another contract that stores the same value in the same slot
  opts.account.address = other;
  assert_rejected(opts, "account proof is not about the expected contract!");

  opts                        = VALID;
  opts.account.storage_proofs = 2;
  assert_rejected(opts, "account proof must contain exactly one storage proof!");
  opts.account.storage_proofs = 0;
  assert_rejected(opts, "account proof must contain exactly one storage proof!");

  opts                            = VALID;
  opts.account.wrong_storage_hash = true;
  assert_rejected(opts, "storage proof of the slot does not match the storage hash of the contract!");

  opts                          = VALID;
  opts.account.truncate_storage = true;
  assert_rejected(opts, "invalid storage proof of the slot!");

  opts                          = VALID;
  opts.account.truncate_account = true;
  assert_rejected(opts, "invalid account proof of the contract!");

  // the newer block commits to another state
  opts             = VALID;
  opts.other_state = true;
  assert_rejected(opts, "account proof of the contract does not match the state root!");
}

// The newer block must be verified. A hash nobody verified is not enough.
void test_eip2935_newer_block_must_be_verified(void) {
  synth_eip2935_opts_t opts = VALID;
  opts.unknown_newer        = true;
  assert_rejected(opts, "referenced block header not found in the verifier cache!");
}

// Two hops of 5000 cover a block 10000 behind the anchor. One hop cannot: the
// history window is 8191. The leaf of the chain is still a cached `blockHash`.
void test_eip2935_may_be_nested(void) {
  const uint64_t       newest = NEWER;
  const uint64_t       middle = NEWER - 5000;
  const uint64_t       oldest = NEWER - 10000;
  synth_eip2935_opts_t direct = VALID;
  direct.older                = oldest;
  assert_rejected(direct, "block is outside the EIP-2935 history window of the newer block!");

  bytes32_t state = {0};
  memset(state, 0x11, 32);
  bytes_t   oldest_header = synth_header(oldest, state, 0x22);
  bytes32_t oldest_hash   = {0};
  bytes32_t oldest_slot   = {0};
  keccak(oldest_header, oldest_hash);
  eth_eip2935_slot(oldest, oldest_slot);

  // The account trie does not contain `elProof`, so this root is the one the middle header commits to.
  synth_opts_t probe_opts = {.storage_proofs = -1};
  synth_t      probe      = synth_account_proof(ETH_EIP2935_HISTORY_ADDRESS, oldest_slot, bytes(oldest_hash, 32), probe_opts);
  bytes_t      middle_hdr = synth_header(middle, probe.state_root, 0x55);
  safe_free(probe.account.bytes.data);

  synth_eip2935_opts_t inner_opts = {.older = middle, .newer = newest, .older_header = middle_hdr, .account = {.storage_proofs = -1}};
  synth_eip2935_case_t inner      = synth_eip2935_case(inner_opts, C4_CHAIN_MAINNET);

  buffer_t wrapped  = {0};
  uint8_t  selector = 5; // frozen index of `eip2935` in ETH_EL_PROOF_UNION
  buffer_append(&wrapped, bytes(&selector, 1));
  buffer_append(&wrapped, inner.proof.bytes);

  synth_opts_t outer_opts    = {.storage_proofs = -1, .el_proof = wrapped.data};
  synth_t      outer_account = synth_account_proof(ETH_EIP2935_HISTORY_ADDRESS, oldest_slot, bytes(oldest_hash, 32), outer_opts);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(probe.state_root, outer_account.state_root, 32);
  TEST_ASSERT_EQUAL_STRING("eip2935", ssz_get(&outer_account.account, "elProof").def->name);

  ssz_builder_t builder = ssz_builder_for_type(ETH_SSZ_EIP2935_PROOF);
  ssz_add_bytes(&builder, "elHeader", oldest_header);
  ssz_add_bytes(&builder, "account", outer_account.account.bytes);
  ssz_ob_t outer = ssz_builder_to_bytes(&builder);

  c4_state_t shape = {0};
  TEST_ASSERT_TRUE_MESSAGE(ssz_is_valid(outer, true, &shape), shape.error ? shape.error : "nested proof is valid SSZ");
  c4_state_free(&shape);

  bytes_t   header = NULL_BYTES;
  bytes32_t hash   = {0};
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, c4_verify_block(&inner.ctx, outer, &header, hash), inner.ctx.state.error ? inner.ctx.state.error : "no error");
  TEST_ASSERT_EQUAL_UINT32(oldest_header.len, header.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(oldest_header.data, header.data, header.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(oldest_hash, hash, 32);
  TEST_ASSERT_EQUAL_UINT64(oldest, eth_el_header_get_uint64(header, EL_BLOCK_NUMBER));
#ifdef EL_HEADER_CACHE
  TEST_ASSERT_NOT_NULL(c4_header_cache_get_by_hash(C4_CHAIN_MAINNET, oldest_hash));
  TEST_ASSERT_NOT_NULL(c4_header_cache_get_by_hash(C4_CHAIN_MAINNET, inner.older_hash));
  TEST_ASSERT_NULL(c4_header_cache_get_by_hash(C4_CHAIN_MAINNET, inner.newer_hash));
#endif

  safe_free(outer.bytes.data);
  safe_free(outer_account.account.bytes.data);
  buffer_free(&wrapped);
  safe_free(oldest_header.data);
  safe_free(middle_hdr.data);
  synth_eip2935_free(&inner);
}

// Other variants as the newer block go through `c4_verify_block` and its rules.
void test_eip2935_inner_variants(void) {
  synth_eip2935_opts_t opts = VALID;
  opts.inner_selector       = 3; // witnessProof
  assert_rejected(opts, "witnessProof is not implemented yet");
  // no hook proves an `eezL1Proof` on Ethereum mainnet
  opts.inner_selector = 4;
  assert_rejected(opts, "invalid block type!");
}

void test_eip2935_wrong_variant(void) {
  synth_eip2935_case_t c      = synth_eip2935_case(VALID, C4_CHAIN_MAINNET);
  bytes_t              header = NULL_BYTES;
  bytes32_t            hash   = {0};
  ssz_ob_t             other  = {.def = eth_ssz_verification_type(ETH_SSZ_VERIFY_ACCOUNT_PROOF), .bytes = c.proof.bytes};
  TEST_ASSERT_EQUAL_INT(C4_ERROR, c4_verify_eip2935_block(&c.ctx, other, &header, hash));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(c.ctx.state.error, "expected eip2935"), c.ctx.state.error);
  c4_state_free(&c.ctx.state);
  c.ctx.state = (c4_state_t) {0};
  TEST_ASSERT_EQUAL_INT(C4_ERROR, c4_verify_eip2935_block(&c.ctx, (ssz_ob_t) {.bytes = c.proof.bytes}, &header, hash));
  synth_eip2935_free(&c);
}

// The history contract is the same on every chain, so `c4_verify_block` accepts the
// variant itself. The newer block of this synthetic proof is a cached `blockHash`.
void test_eip2935_is_accepted_on_mainnet(void) {
  synth_eip2935_case_t c      = synth_eip2935_case(VALID, C4_CHAIN_MAINNET);
  bytes_t              header = NULL_BYTES;
  bytes32_t            hash   = {0};
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, c4_verify_block(&c.ctx, c.proof, &header, hash), c.ctx.state.error ? c.ctx.state.error : "no error");
  TEST_ASSERT_EQUAL_UINT32(c.older_header.len, header.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(c.older_header.data, header.data, header.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(c.older_hash, hash, 32);
  TEST_ASSERT_TRUE(c.ctx.chain_id == C4_CHAIN_MAINNET);
  synth_eip2935_free(&c);
}

// An OP-Stack chain accepts it the same way. The newer block is still a cached hash;
// the sequencer hook is not involved. The verified header is cached for that chain.
void test_eip2935_is_accepted_on_op_chains(void) {
  synth_eip2935_case_t c      = synth_eip2935_case(VALID, C4_CHAIN_OP_MAINNET);
  bytes_t              header = NULL_BYTES;
  bytes32_t            hash   = {0};
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, c4_verify_block(&c.ctx, c.proof, &header, hash), c.ctx.state.error ? c.ctx.state.error : "no error");
  TEST_ASSERT_EQUAL_UINT8_ARRAY(c.older_header.data, header.data, header.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(c.older_hash, hash, 32);
  TEST_ASSERT_TRUE(c.ctx.chain_id == C4_CHAIN_OP_MAINNET);
#ifdef EL_HEADER_CACHE
  TEST_ASSERT_NOT_NULL(c4_header_cache_get_by_hash(C4_CHAIN_OP_MAINNET, c.older_hash));
  TEST_ASSERT_NULL(c4_header_cache_get_by_hash(C4_CHAIN_MAINNET, c.older_hash));
#endif
  synth_eip2935_free(&c);
}

#ifdef EL_HEADER_CACHE
// A verified older header can be referenced by hash afterwards.
void test_eip2935_caches_the_header(void) {
  synth_eip2935_case_t c      = synth_eip2935_case(VALID, C4_CHAIN_MAINNET);
  bytes_t              header = NULL_BYTES;
  bytes32_t            hash   = {0};
  TEST_ASSERT_NULL(c4_header_cache_get_by_hash(C4_CHAIN_MAINNET, c.older_hash));
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, c4_verify_eip2935_block(&c.ctx, c.proof, &header, hash));
  TEST_ASSERT_NOT_NULL(c4_header_cache_get_by_hash(C4_CHAIN_MAINNET, c.older_hash));
  TEST_ASSERT_NOT_NULL(c4_header_cache_get_by_number(C4_CHAIN_MAINNET, VALID.older));
  // the newer block was only attached to this context, it is not promoted to the cache
  TEST_ASSERT_NULL(c4_header_cache_get_by_hash(C4_CHAIN_MAINNET, c.newer_hash));
  synth_eip2935_free(&c);
}
#endif

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_history_address_and_slot);
  RUN_TEST(test_storage_slot);
  RUN_TEST(test_storage_slot_without_value);
  RUN_TEST(test_eip2935_valid);
  RUN_TEST(test_eip2935_window);
  RUN_TEST(test_eip2935_window_of_a_young_chain);
  RUN_TEST(test_eip2935_genesis_block);
  RUN_TEST(test_eip2935_newer_block_is_not_its_own_history);
  RUN_TEST(test_eip2935_slot_of_another_block);
  RUN_TEST(test_eip2935_header_does_not_match);
  RUN_TEST(test_eip2935_stored_value_is_no_header);
  RUN_TEST(test_eip2935_account_proof);
  RUN_TEST(test_eip2935_newer_block_must_be_verified);
  RUN_TEST(test_eip2935_may_be_nested);
  RUN_TEST(test_eip2935_inner_variants);
  RUN_TEST(test_eip2935_wrong_variant);
  RUN_TEST(test_eip2935_is_accepted_on_mainnet);
  RUN_TEST(test_eip2935_is_accepted_on_op_chains);
#ifdef EL_HEADER_CACHE
  RUN_TEST(test_eip2935_caches_the_header);
#endif
  return UNITY_END();
}
