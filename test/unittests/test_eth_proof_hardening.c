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

// Proofs are untrusted input. The first node of a Merkle Patricia proof and the complete
// storage proof of an account are processed before anything ties them to a trusted root,
// and the SSZ validation runs before any semantic check. These tests feed malformed
// proofs into exactly those places.

#include "bytes.h"
#include "c4_assert.h"
#include "eth_account.h"
#include "patricia.h"
#include "rlp.h"
#include "ssz.h"
#include "unity.h"

void setUp(void) {
  reset_local_filecache();
}

void tearDown(void) {
  reset_local_filecache();
}

// SSZ list of proof nodes (the type of `accountProof`).
static ssz_ob_t node_list(bytes_t* nodes, int count) {
  const ssz_def_t* account = eth_ssz_verification_type(ETH_SSZ_VERIFY_ACCOUNT_PROOF);
  ssz_builder_t    list    = {.def = ssz_get_def(account, "accountProof")};
  for (int i = 0; i < count; i++) ssz_add_dynamic_list_bytes(&list, count, nodes[i]);
  return ssz_builder_to_bytes(&list);
}

// Copy of a list of proof nodes without its last node.
static ssz_ob_t without_last(ssz_ob_t list) {
  uint32_t      len     = ssz_len(list);
  ssz_builder_t builder = {.def = list.def};
  TEST_ASSERT_TRUE(len >= 2);
  for (uint32_t i = 0; i + 1 < len; i++)
    ssz_add_dynamic_list_bytes(&builder, (int) (len - 1), ssz_at(list, i).bytes);
  return ssz_builder_to_bytes(&builder);
}

// Fills a trie with unrelated entries under hashed keys, so a proof runs through branch nodes.
static void add_filler(node_t** root, const char* domain) {
  for (uint32_t i = 0; i < 24; i++) {
    bytes32_t path  = {0};
    buffer_t  seed  = {0};
    buffer_t  value = {0};
    bprintf(&seed, "%s:%d", domain, i);
    keccak(seed.data, path);
    rlp_add_uint64(&value, (uint64_t) i + 1000);
    patricia_set_value(root, bytes(path, 32), value.data);
    buffer_free(&seed);
    buffer_free(&value);
  }
}

// ---------------------------------------------------------------------------
// Merkle Patricia proofs
// ---------------------------------------------------------------------------

// A proof which ends before the path is resolved proves nothing. It used to be reported as
// PATRICIA_FOUND with an empty value.
void test_truncated_proof_is_invalid(void) {
  node_t*   trie    = NULL;
  bytes32_t path    = {0};
  bytes32_t root    = {0};
  bytes_t   value   = NULL_BYTES;
  buffer_t  encoded = {0};
  add_filler(&trie, "truncated");
  keccak(bytes((uint8_t*) "key", 3), path);
  rlp_add_uint64(&encoded, 0xabcdef);
  patricia_set_value(&trie, bytes(path, 32), encoded.data);

  ssz_ob_t proof = patricia_create_merkle_proof(trie, bytes(path, 32));
  TEST_ASSERT_EQUAL_INT(PATRICIA_FOUND, patricia_verify(root, bytes(path, 32), proof, &value));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(patricia_get_root(trie).data, root, 32);
  TEST_ASSERT_TRUE(bytes_eq(value, encoded.data));

  // every proper prefix of the proof still starts at the authentic root
  ssz_ob_t shorter = proof;
  while (ssz_len(shorter) >= 2) {
    ssz_ob_t next = without_last(shorter);
    if (shorter.bytes.data != proof.bytes.data) safe_free(shorter.bytes.data);
    shorter = next;
    value   = bytes((uint8_t*) "x", 1);
    memset(root, 0, 32);
    TEST_ASSERT_EQUAL_INT(PATRICIA_INVALID, patricia_verify(root, bytes(path, 32), shorter, &value));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(patricia_get_root(trie).data, root, 32);
  }
  if (shorter.bytes.data != proof.bytes.data) safe_free(shorter.bytes.data);

  // no node at all
  ssz_ob_t empty = {.def = proof.def, .bytes = bytes(proof.bytes.data, 0)};
  TEST_ASSERT_EQUAL_INT(PATRICIA_INVALID, patricia_verify(root, bytes(path, 32), empty, &value));

  safe_free(proof.bytes.data);
  buffer_free(&encoded);
  patricia_node_free(trie);
}

// The first node is decoded before it is compared with a trusted root. Nodes which are no
// RLP list of 2 or 17 items must be rejected without reading anything else.
void test_undecodable_nodes_are_invalid(void) {
  uint8_t   string_node[]    = {0x83, 'a', 'b', 'c'};
  uint8_t   empty_list[]     = {0xc0};
  uint8_t   three_items[]    = {0xc3, 0x01, 0x02, 0x03};
  uint8_t   too_long[]       = {0xf8, 0x40, 0x01};       // announces 64 bytes, holds 1
  uint8_t   empty_path[]     = {0xc2, 0x80, 0x01};       // leaf whose path lacks the prefix byte
  uint8_t   short_hash[]     = {0xc4, 0x00, 0x82, 1, 2}; // extension pointing to a 2 byte "hash"
  bytes_t   cases[]          = {NULL_BYTES, bytes(string_node, 4), bytes(empty_list, 1), bytes(three_items, 4), bytes(too_long, 3), bytes(empty_path, 3), bytes(short_hash, 5)};
  bytes32_t path             = {0};
  bytes32_t root             = {0};
  uint8_t   branch_child[20] = {0};

  for (int i = 0; i < 7; i++) {
    bytes_t  value = NULL_BYTES;
    ssz_ob_t proof = node_list(cases + i, 1);
    TEST_ASSERT_EQUAL_INT_MESSAGE(PATRICIA_INVALID, patricia_verify(root, bytes(path, 32), proof, &value), "malformed node");
    safe_free(proof.bytes.data);
  }

  // branch whose child for nibble 0 is neither empty, a hash nor an embedded node
  branch_child[0] = 0xc0 + 19;
  branch_child[1] = 0x82;
  branch_child[2] = 1;
  branch_child[3] = 2;
  memset(branch_child + 4, 0x80, 16);
  bytes_t  node  = bytes(branch_child, 20);
  bytes_t  value = NULL_BYTES;
  ssz_ob_t proof = node_list(&node, 1);
  TEST_ASSERT_EQUAL_INT(PATRICIA_INVALID, patricia_verify(root, bytes(path, 32), proof, &value));
  safe_free(proof.bytes.data);
}

// A node embedded at child 0 of a branch has to be read from the branch itself. It used to
// be read from the bytes behind the node, here from a second proof element nobody hashes.
void test_embedded_child_at_index_0(void) {
  uint8_t   branch[20];
  uint8_t   unhashed[] = {0xc2, 0x31, 'B'};
  uint8_t   key[1]     = {0x01}; // nibbles 0, 1
  bytes32_t expected   = {0};
  bytes32_t root       = {0};
  bytes_t   value      = NULL_BYTES;

  // root branch with the leaf [path = nibble 1, value = 'A'] embedded at child 0
  branch[0] = 0xc0 + 19;
  branch[1] = 0xc2;
  branch[2] = 0x31;
  branch[3] = 'A';
  memset(branch + 4, 0x80, 16);
  keccak(bytes(branch, 20), expected);

  bytes_t  nodes[] = {bytes(branch, 20), bytes(unhashed, 3)};
  ssz_ob_t proof   = node_list(nodes, 2);
  // the embedded leaf ends the walk, so the node behind it makes the proof invalid
  TEST_ASSERT_EQUAL_INT(PATRICIA_INVALID, patricia_verify(root, bytes(key, 1), proof, &value));
  safe_free(proof.bytes.data);

  proof = node_list(nodes, 1);
  value = NULL_BYTES;
  TEST_ASSERT_EQUAL_INT(PATRICIA_FOUND, patricia_verify(root, bytes(key, 1), proof, &value));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, root, 32);
  TEST_ASSERT_EQUAL_UINT32(1, value.len);
  TEST_ASSERT_EQUAL_UINT8('A', value.data[0]);
  // the value is a view into the branch node of the proof
  TEST_ASSERT_TRUE(value.data > proof.bytes.data && value.data < proof.bytes.data + proof.bytes.len);

  // another key below the same child is proven absent by the embedded leaf
  uint8_t other[1] = {0x02};
  TEST_ASSERT_EQUAL_INT(PATRICIA_NOT_EXISTING, patricia_verify(root, bytes(other, 1), proof, &value));
  safe_free(proof.bytes.data);
}

// Branch whose child 0 is the embedded leaf [path = nibble 1, value of `value_len` bytes],
// written into `branch` (at least 64 bytes). Returns the length of the node.
static uint32_t branch_with_embedded_leaf(uint8_t* branch, uint8_t value_len) {
  uint8_t leaf_len = (uint8_t) (2 + value_len); // path byte + string header + value
  branch[0]        = (uint8_t) (0xc0 + 1 + leaf_len + 16);
  branch[1]        = (uint8_t) (0xc0 + leaf_len);
  branch[2]        = 0x31;
  branch[3]        = (uint8_t) (0x80 + value_len);
  memset(branch + 4, 'A', value_len);
  memset(branch + 4 + value_len, 0x80, 16);
  return (uint32_t) leaf_len + 18;
}

// The longest node which is embedded has 31 bytes. One byte more and it has to be referenced
// by its hash.
void test_embedded_node_size_limit(void) {
  uint8_t   branch[64];
  uint8_t   key[1] = {0x01};
  bytes32_t root   = {0};
  bytes_t   value  = NULL_BYTES;

  bytes_t  node  = bytes(branch, branch_with_embedded_leaf(branch, 28)); // embedded node of 31 bytes
  ssz_ob_t proof = node_list(&node, 1);
  TEST_ASSERT_EQUAL_INT(PATRICIA_FOUND, patricia_verify(root, bytes(key, 1), proof, &value));
  TEST_ASSERT_EQUAL_UINT32(28, value.len);
  safe_free(proof.bytes.data);

  node  = bytes(branch, branch_with_embedded_leaf(branch, 29)); // 32 bytes
  proof = node_list(&node, 1);
  TEST_ASSERT_EQUAL_INT(PATRICIA_INVALID, patricia_verify(root, bytes(key, 1), proof, &value));
  safe_free(proof.bytes.data);
}

// An embedded node is shorter than 32 bytes. A longer list in the place of a child is not a
// node of any trie and must not be walked.
void test_embedded_node_has_to_be_short(void) {
  uint8_t   branch[3 + 40 + 16];
  uint8_t   key[1] = {0x01};
  bytes32_t root   = {0};
  bytes_t   value  = NULL_BYTES;
  branch[0]        = 0xf8;
  branch[1]        = 1 + 40 + 16;
  branch[2]        = 0xc0 + 40; // list of 40 bytes at child 0
  branch[3]        = 0x31;
  branch[4]        = 0xa6; // string of 38 bytes
  memset(branch + 5, 'A', 38);
  memset(branch + 43, 0x80, 16);
  bytes_t  node  = bytes(branch, sizeof(branch));
  ssz_ob_t proof = node_list(&node, 1);
  TEST_ASSERT_EQUAL_INT(PATRICIA_INVALID, patricia_verify(root, bytes(key, 1), proof, &value));
  safe_free(proof.bytes.data);
}

// Tries with short keys and values hold embedded nodes at every level. Each key has to be
// found with its value and each missing key has to be proven absent.
void test_embedded_nodes_of_a_built_trie(void) {
  const char* keys[] = {"a", "b", "ab", "abc", "abd", "b0", "c", "ca", "do", "dog", "doge", "horse"};
  // Keys leaving the trie at an extension ("z", "aa") are not part of the list: the proof
  // builder adds the node behind such an extension, which the verifier rejects as surplus.
  const char* missing[] = {"", "d", "abe", "dogs", "hors"};
  node_t*     trie      = NULL;
  for (int i = 0; i < 12; i++) patricia_set_value(&trie, bytes((uint8_t*) keys[i], (uint32_t) strlen(keys[i])), bytes((uint8_t*) keys[i], (uint32_t) strlen(keys[i])));

  for (int i = 0; i < 12; i++) {
    bytes32_t root  = {0};
    bytes_t   value = NULL_BYTES;
    bytes_t   key   = bytes((uint8_t*) keys[i], (uint32_t) strlen(keys[i]));
    ssz_ob_t  proof = patricia_create_merkle_proof(trie, key);
    TEST_ASSERT_EQUAL_INT_MESSAGE(PATRICIA_FOUND, patricia_verify(root, key, proof, &value), keys[i]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(patricia_get_root(trie).data, root, 32);
    TEST_ASSERT_TRUE_MESSAGE(bytes_eq(value, key), keys[i]);
    safe_free(proof.bytes.data);
  }
  for (int i = 0; i < 5; i++) {
    bytes32_t root  = {0};
    bytes_t   value = NULL_BYTES;
    bytes_t   key   = bytes((uint8_t*) missing[i], (uint32_t) strlen(missing[i]));
    ssz_ob_t  proof = patricia_create_merkle_proof(trie, key);
    TEST_ASSERT_EQUAL_INT_MESSAGE(PATRICIA_NOT_EXISTING, patricia_verify(root, key, proof, &value), missing[i]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(patricia_get_root(trie).data, root, 32);
    safe_free(proof.bytes.data);
  }
  patricia_node_free(trie);
}

// ---------------------------------------------------------------------------
// storage proofs of an account
// ---------------------------------------------------------------------------

typedef struct {
  int  value_len;     // length of the value of the first slot; 0 = the slot does not exist
  bool value_is_list; // the leaf holds an RLP list instead of a string
  bool truncate;      // drop the leaf node of the first storage proof
  bool second_slot;   // add a storage proof for a second slot which does not exist
  bool no_account;    // the account is not part of the state
  bool empty_proofs;  // the storage proofs hold no nodes, as for an account without storage
} storage_case_t;

// Builds an `EthAccountProof` of a synthetic account whose storage holds one slot as
// described by `c`. Both tries are self-built: this is what a prover can send before the
// state root is compared with a header. The object is owned by the caller.
static ssz_ob_t account_with_storage(storage_case_t c, bytes32_t state_root) {
  uint8_t   address[20];
  uint8_t   value[128];
  bytes32_t slot         = {0};
  bytes32_t other        = {0};
  bytes32_t path         = {0};
  bytes32_t code_hash    = {0};
  buffer_t  buf          = {0};
  node_t*   storage      = NULL;
  node_t*   accounts     = NULL;
  uint8_t   no_block[33] = {0}; // selector 0 (`blockHash`) + 32 zero bytes
  memset(address, 0xc0, 20);
  memset(value, 0xee, sizeof(value));
  memset(code_hash, 0x5a, 32);
  slot[31]  = 7;
  other[31] = 8;

  add_filler(&storage, "storage");
  keccak(bytes(slot, 32), path);
  if (c.value_len) {
    rlp_add_item(&buf, bytes(value, (uint32_t) c.value_len));
    if (c.value_is_list) rlp_to_list(&buf);
    patricia_set_value(&storage, bytes(path, 32), buf.data);
    buffer_reset(&buf);
  }
  ssz_ob_t storage_proof = patricia_create_merkle_proof(storage, bytes(path, 32));
  if (c.truncate) {
    ssz_ob_t full = storage_proof;
    storage_proof = without_last(full);
    safe_free(full.bytes.data);
  }
  keccak(bytes(other, 32), path);
  ssz_ob_t other_proof = patricia_create_merkle_proof(storage, bytes(path, 32));
  if (c.empty_proofs) {
    storage_proof.bytes.len = 0;
    other_proof.bytes.len   = 0;
  }

  // account: [nonce, balance, storageHash, codeHash]
  rlp_add_uint64(&buf, 1);
  rlp_add_uint64(&buf, 0);
  rlp_add_item(&buf, patricia_get_root(storage));
  rlp_add_item(&buf, bytes(code_hash, 32));
  rlp_to_list(&buf);
  add_filler(&accounts, "accounts");
  keccak(bytes(address, 20), path);
  if (!c.no_account) patricia_set_value(&accounts, bytes(path, 32), buf.data);
  memcpy(state_root, patricia_get_root(accounts).data, 32);
  ssz_ob_t account_proof = patricia_create_merkle_proof(accounts, bytes(path, 32));

  ssz_builder_t    account      = ssz_builder_for_type(ETH_SSZ_VERIFY_ACCOUNT_PROOF);
  const ssz_def_t* storage_def  = ssz_get_def(account.def, "storageProof");
  ssz_builder_t    storage_list = {.def = storage_def};
  int              proofs       = c.second_slot ? 2 : 1;
  for (int i = 0; i < proofs; i++) {
    ssz_builder_t entry = {.def = storage_def->def.vector.type};
    ssz_add_bytes(&entry, "key", bytes((i ? other : slot), 32));
    ssz_add_bytes(&entry, "proof", i ? other_proof.bytes : storage_proof.bytes);
    ssz_add_dynamic_list_builders(&storage_list, proofs, entry);
  }
  ssz_add_bytes(&account, "accountProof", account_proof.bytes);
  ssz_add_bytes(&account, "address", bytes(address, 20));
  ssz_add_builders(&account, "storageProof", storage_list);
  ssz_add_bytes(&account, "elProof", bytes(no_block, 33));

  safe_free(storage_proof.bytes.data);
  safe_free(other_proof.bytes.data);
  safe_free(account_proof.bytes.data);
  patricia_node_free(storage);
  patricia_node_free(accounts);
  buffer_free(&buf);
  return ssz_builder_to_bytes(&account);
}

// Runs the account verification which every account and call proof passes through. The
// value buffer is surrounded by guard bytes to detect writes outside of it.
static bool verify_storage_case(storage_case_t c, uint8_t* values, uint32_t values_len, char** error) {
  struct {
    uint8_t before[256];
    uint8_t values[64];
    uint8_t after[256];
  } mem;
  bytes32_t    state_root = {0};
  bytes32_t    expected   = {0};
  verify_ctx_t ctx        = {.method = "eth_getStorageAt", .chain_id = C4_CHAIN_MAINNET};
  ssz_ob_t     account    = account_with_storage(c, expected);
  memset(&mem, 0x55, sizeof(mem));
  TEST_ASSERT_TRUE(values_len <= sizeof(mem.values));
  TEST_ASSERT_TRUE_MESSAGE(ssz_is_valid(account, true, NULL), "the proof has to pass the SSZ validation");

  bool valid = eth_verify_account_proof_exec(&ctx, &account, state_root, ETH_ACCOUNT_STORAGE_HASH, bytes(mem.values, values_len));
  for (int i = 0; i < 256; i++) {
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x55, mem.before[i], "write in front of the values");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x55, mem.after[i], "write behind the values");
  }
  for (uint32_t i = values_len; i < sizeof(mem.values); i++) TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x55, mem.values[i], "write behind the values");
  if (valid) TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, state_root, 32);
  if (values) memcpy(values, mem.values, values_len);
  if (error) *error = ctx.state.error ? strdup(ctx.state.error) : NULL;
  c4_state_free(&ctx.state);
  safe_free(account.bytes.data);
  return valid;
}

// Values up to 32 bytes are returned right-aligned, a missing slot is zero.
void test_storage_values(void) {
  uint8_t value[32];
  uint8_t expected[32];

  TEST_ASSERT_TRUE(verify_storage_case((storage_case_t) {.value_len = 32}, value, 32, NULL));
  memset(expected, 0xee, 32);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, value, 32);

  TEST_ASSERT_TRUE(verify_storage_case((storage_case_t) {.value_len = 3}, value, 32, NULL));
  memset(expected, 0, 29);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, value, 32);

  TEST_ASSERT_TRUE(verify_storage_case((storage_case_t) {.value_len = 0}, value, 32, NULL));
  memset(expected, 0, 32);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, value, 32);
}

// A storage proof without its leaf used to be accepted as the value 0.
void test_truncated_storage_proof_is_rejected(void) {
  char* error = NULL;
  TEST_ASSERT_FALSE(verify_storage_case((storage_case_t) {.value_len = 32, .truncate = true}, NULL, 32, &error));
  TEST_ASSERT_NOT_NULL(error);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(error, "invalid storage proof"), error);
  safe_free(error);
}

// A storage value longer than 32 bytes used to be copied in front of the 32 byte buffer of
// the caller. Storage root and account are self-consistent, so nothing else stops it.
void test_oversized_storage_value_is_rejected(void) {
  int lengths[] = {33, 64, 100, 128};
  for (int i = 0; i < 4; i++) {
    char* error = NULL;
    TEST_ASSERT_FALSE(verify_storage_case((storage_case_t) {.value_len = lengths[i]}, NULL, 32, &error));
    TEST_ASSERT_NOT_NULL(error);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(error, "invalid storage value"), error);
    safe_free(error);
  }
}

// A leaf holding a list is no storage value. It used to be accepted as the value 0.
void test_storage_value_has_to_be_a_string(void) {
  char* error = NULL;
  TEST_ASSERT_FALSE(verify_storage_case((storage_case_t) {.value_len = 3, .value_is_list = true}, NULL, 32, &error));
  TEST_ASSERT_NOT_NULL(error);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(error, "invalid storage value"), error);
  safe_free(error);
}

// Every value of the result is defined by the proof. The value of a second slot which does
// not exist used to keep whatever the buffer of the caller held before.
void test_values_of_all_slots_are_initialized(void) {
  uint8_t values[64];
  uint8_t expected[64];
  TEST_ASSERT_TRUE(verify_storage_case((storage_case_t) {.value_len = 2, .second_slot = true}, values, 64, NULL));
  memset(expected, 0, 64);
  expected[30] = 0xee;
  expected[31] = 0xee;
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, values, 64);
}

// An account which does not exist has no storage. Its slots are proven absent by storage
// proofs without nodes and read as zero; a storage proof with nodes contradicts the account.
void test_storage_of_a_missing_account(void) {
  uint8_t values[64];
  uint8_t expected[64] = {0};
  char*   error        = NULL;

  memset(values, 0xff, sizeof(values));
  TEST_ASSERT_TRUE(verify_storage_case((storage_case_t) {.value_len = 32, .no_account = true, .empty_proofs = true, .second_slot = true}, values, 64, &error));
  TEST_ASSERT_NULL(error);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, values, 64);

  TEST_ASSERT_FALSE(verify_storage_case((storage_case_t) {.value_len = 32, .no_account = true}, NULL, 32, &error));
  TEST_ASSERT_NOT_NULL(error);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(error, "empty storage hash can not have values"), error);
  safe_free(error);
}

// The proof that an account does not exist may end in the leaf of another account. The
// result of eth_getProof must describe the empty account and not that neighbour.
void test_get_proof_of_a_missing_account(void) {
  uint8_t   address[20];
  uint8_t   no_block[33] = {0}; // selector 0 (`blockHash`) + 32 zero bytes
  uint8_t   zero[32]     = {0};
  uint8_t   values[32]   = {0};
  bytes32_t path         = {0};
  bytes32_t state_root   = {0};
  buffer_t  neighbour    = {0};
  node_t*   accounts     = NULL;
  ssz_ob_t  proof        = {0};

  // a state of 24 accounts with nonce 5 and balance 1000
  rlp_add_uint64(&neighbour, 5);
  rlp_add_uint64(&neighbour, 1000);
  rlp_add_item(&neighbour, bytes(EMPTY_ROOT_HASH, 32));
  rlp_add_item(&neighbour, bytes(EMPTY_HASH, 32));
  rlp_to_list(&neighbour);
  for (uint32_t i = 0; i < 24; i++) {
    buffer_t seed = {0};
    bprintf(&seed, "neighbour:%d", i);
    keccak(seed.data, path);
    patricia_set_value(&accounts, bytes(path, 32), neighbour.data);
    buffer_free(&seed);
  }

  // look for an address whose exclusion proof ends in the leaf of one of them
  bool ends_in_leaf = false;
  for (int fill = 1; fill < 256 && !ends_in_leaf; fill++) {
    bytes_t value = NULL_BYTES;
    bytes_t last  = NULL_BYTES;
    memset(address, fill, 20);
    keccak(bytes(address, 20), path);
    proof        = patricia_create_merkle_proof(accounts, bytes(path, 32));
    last         = ssz_at(proof, ssz_len(proof) - 1).bytes;
    ends_in_leaf = patricia_verify(state_root, bytes(path, 32), proof, &value) == PATRICIA_NOT_EXISTING &&
                   rlp_decode(&last, 0, &last) == RLP_LIST && rlp_decode(&last, -1, NULL) == 2;
    if (!ends_in_leaf) safe_free(proof.bytes.data);
  }
  TEST_ASSERT_TRUE_MESSAGE(ends_in_leaf, "no exclusion proof ending in a leaf found");

  ssz_builder_t builder = ssz_builder_for_type(ETH_SSZ_VERIFY_ACCOUNT_PROOF);
  ssz_add_bytes(&builder, "accountProof", proof.bytes);
  ssz_add_bytes(&builder, "address", bytes(address, 20));
  ssz_add_bytes(&builder, "storageProof", NULL_BYTES);
  ssz_add_bytes(&builder, "elProof", bytes(no_block, 33));
  ssz_ob_t account = ssz_builder_to_bytes(&builder);
  TEST_ASSERT_TRUE(ssz_is_valid(account, true, NULL));

  char*        args = bprintf(NULL, "[\"0x%x\",[],\"latest\"]", bytes(address, 20));
  verify_ctx_t ctx  = {.method = "eth_getProof", .chain_id = C4_CHAIN_MAINNET, .proof = account, .data = {.def = &ssz_none}, .args = json_parse(args)};
  TEST_ASSERT_TRUE(eth_verify_account_proof_exec(&ctx, &account, state_root, ETH_ACCOUNT_STORAGE_HASH, bytes(values, 32)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(patricia_get_root(accounts).data, state_root, 32);
  TEST_ASSERT_TRUE_MESSAGE(eth_account_verify_data(&ctx, address, ETH_ACCOUNT_PROOF, bytes(values, 0)), ctx.state.error ? ctx.state.error : "no error");

  TEST_ASSERT_EQUAL_UINT8_ARRAY(zero, ssz_get(&ctx.data, "balance").bytes.data, 32);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(zero, ssz_get(&ctx.data, "nonce").bytes.data, 32);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(EMPTY_HASH, ssz_get(&ctx.data, "codeHash").bytes.data, 32);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(EMPTY_ROOT_HASH, ssz_get(&ctx.data, "storageHash").bytes.data, 32);

  safe_free(ctx.data.bytes.data);
  c4_state_free(&ctx.state);
  safe_free(args);
  safe_free(account.bytes.data);
  safe_free(proof.bytes.data);
  patricia_node_free(accounts);
  buffer_free(&neighbour);
}

// ---------------------------------------------------------------------------
// SSZ validation
// ---------------------------------------------------------------------------

// `EthAccountProof` whose `elProof` nests `levels` further account proofs through the
// `eezL1Proof` variant. The object is owned by the caller.
static ssz_ob_t nested_account_proof(uint32_t levels) {
  const ssz_def_t* account_def = eth_ssz_verification_type(ETH_SSZ_VERIFY_ACCOUNT_PROOF);
  const ssz_def_t* el_proof    = ssz_get_def(account_def, "elProof");
  uint8_t          address[20] = {0};
  buffer_t         inner       = {0};
  uint8_t          selector    = 0;
  TEST_ASSERT_NOT_NULL(el_proof);
  TEST_ASSERT_EQUAL_INT(SSZ_TYPE_UNION, el_proof->type);

  // innermost `elProof`: `blockHash` of 32 zero bytes
  buffer_append(&inner, bytes(&selector, 1));
  buffer_append(&inner, bytes(NULL, 32));
  for (uint32_t i = 0;; i++) {
    ssz_builder_t account = ssz_builder_for_def(account_def);
    ssz_add_bytes(&account, "accountProof", NULL_BYTES);
    ssz_add_bytes(&account, "address", bytes(address, 20));
    ssz_add_bytes(&account, "storageProof", NULL_BYTES);
    ssz_add_bytes(&account, "elProof", inner.data);
    ssz_ob_t ob = ssz_builder_to_bytes(&account);
    if (i == levels) {
      buffer_free(&inner);
      return ob;
    }

    // wrap the account into the `eezL1Proof` variant of the next `elProof`
    const ssz_def_t* variant = NULL;
    for (uint32_t v = 0; v < el_proof->def.container.len; v++) {
      if (strcmp(el_proof->def.container.elements[v].name, "eezL1Proof") == 0) {
        variant  = el_proof->def.container.elements + v;
        selector = (uint8_t) v;
      }
    }
    TEST_ASSERT_NOT_NULL_MESSAGE(variant, "eezL1Proof is a variant of the elProof");
    ssz_builder_t l1 = ssz_builder_for_def(variant);
    ssz_add_bytes(&l1, "elHeader", NULL_BYTES);
    ssz_add_bytes(&l1, "account", ob.bytes);
    ssz_ob_t l1_ob = ssz_builder_to_bytes(&l1);
    buffer_reset(&inner);
    buffer_append(&inner, bytes(&selector, 1));
    buffer_append(&inner, l1_ob.bytes);
    safe_free(l1_ob.bytes.data);
    safe_free(ob.bytes.data);
  }
}

// The type of an account proof refers to itself, so the nesting of a request is only limited
// by its size. The validation has to stop instead of following it down the stack.
void test_ssz_nesting_is_limited(void) {
  c4_state_t state = {0};

  ssz_ob_t flat = nested_account_proof(0);
  TEST_ASSERT_TRUE(ssz_is_valid(flat, true, &state));
  safe_free(flat.bytes.data);

  // an EEZ block proof inside an EIP-2935 proof nests two account proofs
  ssz_ob_t two = nested_account_proof(2);
  TEST_ASSERT_TRUE_MESSAGE(ssz_is_valid(two, true, &state), state.error ? state.error : "two levels are valid");
  safe_free(two.bytes.data);

  // each level adds three objects (union, variant, account): 10 levels end at SSZ_MAX_NESTING
  ssz_ob_t at_limit = nested_account_proof(10);
  TEST_ASSERT_TRUE_MESSAGE(ssz_is_valid(at_limit, true, &state), state.error ? state.error : "nesting at the limit is valid");
  safe_free(at_limit.bytes.data);

  uint32_t levels[] = {11, 50, 1000, 20000};
  for (int i = 0; i < 4; i++) {
    ssz_ob_t deep = nested_account_proof(levels[i]);
    TEST_ASSERT_FALSE(ssz_is_valid(deep, true, &state));
    TEST_ASSERT_NOT_NULL(state.error);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(state.error, "nested too deeply"), state.error);
    c4_state_free(&state);
    // without recursion only the outermost object is checked
    TEST_ASSERT_TRUE(ssz_is_valid(deep, false, NULL));
    safe_free(deep.bytes.data);
  }
}

// The same through the public entry point: the request is rejected before any proof code runs.
void test_deeply_nested_request_is_rejected(void) {
  uint8_t       version[4] = {0};
  uint8_t       none[1]    = {0};
  buffer_t      proof      = {0};
  uint8_t       selector   = 1; // AccountProof
  ssz_ob_t      account    = nested_account_proof(20000);
  ssz_builder_t request    = ssz_builder_for_def(c4_get_request_type(C4_CHAIN_TYPE_ETHEREUM));
  verify_ctx_t  ctx        = {0};
  buffer_append(&proof, bytes(&selector, 1));
  buffer_append(&proof, account.bytes);
  ssz_add_bytes(&request, "version", bytes(version, 4));
  ssz_add_bytes(&request, "data", bytes(none, 1));
  ssz_add_bytes(&request, "proof", proof.data);
  ssz_add_bytes(&request, "sync_data", bytes(none, 1));
  ssz_ob_t ob = ssz_builder_to_bytes(&request);

  c4_status_t status = c4_verify_from_bytes(&ctx, ob.bytes, "eth_getBalance", json_parse("[\"0x0000000000000000000000000000000000000000\",\"latest\"]"), C4_CHAIN_MAINNET);
  TEST_ASSERT_EQUAL_INT(C4_ERROR, status);
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(ctx.state.error, "nested too deeply"), ctx.state.error);
  c4_verify_free_data(&ctx);
  safe_free(ob.bytes.data);
  safe_free(account.bytes.data);
  buffer_free(&proof);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_truncated_proof_is_invalid);
  RUN_TEST(test_undecodable_nodes_are_invalid);
  RUN_TEST(test_embedded_child_at_index_0);
  RUN_TEST(test_embedded_node_size_limit);
  RUN_TEST(test_embedded_node_has_to_be_short);
  RUN_TEST(test_embedded_nodes_of_a_built_trie);
  RUN_TEST(test_storage_values);
  RUN_TEST(test_truncated_storage_proof_is_rejected);
  RUN_TEST(test_oversized_storage_value_is_rejected);
  RUN_TEST(test_storage_value_has_to_be_a_string);
  RUN_TEST(test_values_of_all_slots_are_initialized);
  RUN_TEST(test_storage_of_a_missing_account);
  RUN_TEST(test_get_proof_of_a_missing_account);
  RUN_TEST(test_ssz_nesting_is_limited);
  RUN_TEST(test_deeply_nested_request_is_rejected);
  return UNITY_END();
}
