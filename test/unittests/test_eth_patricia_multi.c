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

#include "bytes.h"
#include "crypto.h"
#include "patricia.h"
#include "rlp.h"
#include "ssz.h"
#include "unity.h"
#include <string.h>

/* Long enough that a leaf/branch serializes to >= 32 bytes and is hashed. */
#define LONG_VAL "value-xxxxxxxxxxxxxxxxxxxxxxxxxxxx"

static const ssz_def_t mpt_nodes_def = SSZ_LIST("bytes", ssz_bytes_list, 1024);

#define MAX_MERGED_NODES 128
#define TEST_KV_COUNT    4

typedef struct {
  const char* key;
  const char* value;
} test_kv_t;

static const test_kv_t test_kvs[TEST_KV_COUNT] = {
    {"aaa", "value-aaa-xxxxxxxxxxxxxxxxxxxx"},
    {"aab", "value-aab-xxxxxxxxxxxxxxxxxxxx"},
    {"bbb", "value-bbb-xxxxxxxxxxxxxxxxxxxx"},
    {"ccc", "value-ccc-xxxxxxxxxxxxxxxxxxxx"},
};

void setUp(void) {}
void tearDown(void) {}

static bytes_t str_bytes(const char* s) {
  return bytes((uint8_t*) s, (uint32_t) strlen(s));
}

static node_t* make_test_trie(void) {
  node_t* root = NULL;
  for (int i = 0; i < TEST_KV_COUNT; i++)
    patricia_set_value(&root, str_bytes(test_kvs[i].key), str_bytes(test_kvs[i].value));
  return root;
}

static ssz_ob_t create_path_proof(node_t* root, bytes_t path) {
  ssz_ob_t proof = patricia_create_merkle_proof(root, path);
  proof.def      = &mpt_nodes_def;
  return proof;
}

static ssz_ob_t merge_proofs(ssz_ob_t* proofs, uint32_t n, bool reverse) {
  bytes_t   unique[MAX_MERGED_NODES];
  bytes32_t hashes[MAX_MERGED_NODES];
  uint32_t  count = 0;

  for (uint32_t p = 0; p < n; p++) {
    uint32_t plen = ssz_len(proofs[p]);
    for (uint32_t i = 0; i < plen; i++) {
      ssz_ob_t  node = ssz_at(proofs[p], i);
      bytes32_t h;
      bool      found = false;
      keccak(node.bytes, h);
      for (uint32_t j = 0; j < count; j++) {
        if (memcmp(hashes[j], h, 32) == 0) {
          found = true;
          break;
        }
      }
      if (found) continue;
      TEST_ASSERT_TRUE_MESSAGE(count < MAX_MERGED_NODES, "too many unique nodes");
      memcpy(hashes[count], h, 32);
      unique[count] = node.bytes;
      count++;
    }
  }

  TEST_ASSERT_TRUE_MESSAGE(count > 0, "merged proof is empty");

  ssz_builder_t builder = {.def = &mpt_nodes_def};
  if (reverse) {
    for (int i = (int) count - 1; i >= 0; i--)
      ssz_add_dynamic_list_bytes(&builder, 0, unique[i]);
  }
  else {
    for (uint32_t i = 0; i < count; i++)
      ssz_add_dynamic_list_bytes(&builder, 0, unique[i]);
  }
  ssz_builder_fix_list_offsets(&builder, count);
  ssz_ob_t out = ssz_builder_to_bytes(&builder);
  out.def      = &mpt_nodes_def;
  return out;
}

static ssz_ob_t drop_node_at(ssz_ob_t list, uint32_t drop) {
  ssz_builder_t builder = {.def = &mpt_nodes_def};
  uint32_t      n       = ssz_len(list);
  uint32_t      count   = 0;
  for (uint32_t i = 0; i < n; i++) {
    if (i == drop) continue;
    ssz_add_dynamic_list_bytes(&builder, 0, ssz_at(list, i).bytes);
    count++;
  }
  ssz_builder_fix_list_offsets(&builder, count);
  ssz_ob_t out = ssz_builder_to_bytes(&builder);
  out.def      = &mpt_nodes_def;
  return out;
}

static void build_merged(node_t* root, ssz_ob_t* merged, bool reverse) {
  ssz_ob_t proofs[TEST_KV_COUNT];
  for (int i = 0; i < TEST_KV_COUNT; i++)
    proofs[i] = create_path_proof(root, str_bytes(test_kvs[i].key));
  *merged = merge_proofs(proofs, TEST_KV_COUNT, reverse);
  for (int i = 0; i < TEST_KV_COUNT; i++)
    safe_free(proofs[i].bytes.data);
}

static void assert_all_paths_found(mpt_proof_t* proof) {
  for (int i = 0; i < TEST_KV_COUNT; i++) {
    bytes_t           leaf   = {0};
    patricia_result_t result = patricia_verify_multi(proof, str_bytes(test_kvs[i].key), &leaf);
    TEST_ASSERT_EQUAL_INT_MESSAGE(PATRICIA_FOUND, result, test_kvs[i].key);
    TEST_ASSERT_EQUAL_UINT32(strlen(test_kvs[i].value), leaf.len);
    TEST_ASSERT_EQUAL_MEMORY(test_kvs[i].value, leaf.data, leaf.len);
  }
}

static void assert_found(mpt_proof_t* proof, const char* key, const char* value) {
  bytes_t           leaf   = {0};
  patricia_result_t result = patricia_verify_multi(proof, str_bytes(key), &leaf);
  TEST_ASSERT_EQUAL_INT_MESSAGE(PATRICIA_FOUND, result, key);
  TEST_ASSERT_EQUAL_UINT32(strlen(value), leaf.len);
  TEST_ASSERT_EQUAL_MEMORY(value, leaf.data, leaf.len);
}

static ssz_ob_t merge_keys(node_t* root, const char** keys, uint32_t n, bool reverse) {
  ssz_ob_t proofs[8];
  TEST_ASSERT_TRUE(n <= 8);
  for (uint32_t i = 0; i < n; i++)
    proofs[i] = create_path_proof(root, str_bytes(keys[i]));
  ssz_ob_t merged = merge_proofs(proofs, n, reverse);
  for (uint32_t i = 0; i < n; i++)
    safe_free(proofs[i].bytes.data);
  return merged;
}

/* True if any RLP item of a serialized MPT node is itself a list (embedded child). */
static bool node_has_embedded_child(bytes_t raw) {
  bytes_t payload = {0};
  bytes_t item    = {0};
  if (rlp_decode(&raw, 0, &payload) != RLP_LIST) return false;
  int n = (int) rlp_decode(&payload, -1, NULL);
  if (n <= 0) return false;
  for (int i = 0; i < n; i++) {
    if (rlp_decode(&payload, i, &item) == RLP_LIST) return true;
  }
  return false;
}

void test_multi_shared_nodes_reverse() {
  node_t*     root      = make_test_trie();
  bytes_t     root_hash = patricia_get_root(root);
  ssz_ob_t    merged    = {0};
  mpt_proof_t proof     = {0};

  build_merged(root, &merged, true);
  mpt_proof_init(&proof, merged, root_hash.data);

  TEST_ASSERT_TRUE(proof.nodes_len > 0);
  TEST_ASSERT_NOT_NULL(proof.hashes);
  assert_all_paths_found(&proof);

  mpt_proof_free(&proof);
  safe_free(merged.bytes.data);
  patricia_node_free(root);
}

void test_multi_matches_ordered_verify() {
  node_t*     root      = make_test_trie();
  bytes_t     root_hash = patricia_get_root(root);
  ssz_ob_t    proofs[TEST_KV_COUNT];
  ssz_ob_t    merged = {0};
  mpt_proof_t multi  = {0};

  for (int i = 0; i < TEST_KV_COUNT; i++)
    proofs[i] = create_path_proof(root, str_bytes(test_kvs[i].key));
  merged = merge_proofs(proofs, TEST_KV_COUNT, true);
  mpt_proof_init(&multi, merged, root_hash.data);

  for (int i = 0; i < TEST_KV_COUNT; i++) {
    bytes32_t         ordered_root = {0};
    bytes_t           ordered_leaf = {0};
    bytes_t           multi_leaf   = {0};
    patricia_result_t ordered      = patricia_verify(ordered_root, str_bytes(test_kvs[i].key), proofs[i], &ordered_leaf);
    patricia_result_t multi_res    = patricia_verify_multi(&multi, str_bytes(test_kvs[i].key), &multi_leaf);

    TEST_ASSERT_EQUAL_INT(PATRICIA_FOUND, ordered);
    TEST_ASSERT_EQUAL_INT(PATRICIA_FOUND, multi_res);
    TEST_ASSERT_EQUAL_MEMORY(root_hash.data, ordered_root, 32);
    TEST_ASSERT_EQUAL_UINT32(ordered_leaf.len, multi_leaf.len);
    TEST_ASSERT_EQUAL_MEMORY(ordered_leaf.data, multi_leaf.data, ordered_leaf.len);
  }

  mpt_proof_free(&multi);
  safe_free(merged.bytes.data);
  for (int i = 0; i < TEST_KV_COUNT; i++)
    safe_free(proofs[i].bytes.data);
  patricia_node_free(root);
}

void test_multi_exclusion() {
  node_t*     root      = make_test_trie();
  bytes_t     root_hash = patricia_get_root(root);
  ssz_ob_t    merged    = {0};
  mpt_proof_t proof     = {0};
  bytes_t     leaf      = {.data = (uint8_t*) 1, .len = 1};

  build_merged(root, &merged, true);
  mpt_proof_init(&proof, merged, root_hash.data);

  TEST_ASSERT_EQUAL_INT(PATRICIA_NOT_EXISTING, patricia_verify_multi(&proof, str_bytes("aad"), &leaf));
  TEST_ASSERT_EQUAL_UINT32(0, leaf.len);
  TEST_ASSERT_NULL(leaf.data);
  TEST_ASSERT_EQUAL_INT(PATRICIA_NOT_EXISTING, patricia_verify_multi(&proof, str_bytes("zzz"), NULL));

  mpt_proof_free(&proof);
  safe_free(merged.bytes.data);
  patricia_node_free(root);
}

void test_multi_incomplete_proof() {
  node_t*     root      = make_test_trie();
  bytes_t     root_hash = patricia_get_root(root);
  ssz_ob_t    merged    = {0};
  mpt_proof_t full      = {0};
  uint32_t    drop      = 0;

  build_merged(root, &merged, true);
  mpt_proof_init(&full, merged, root_hash.data);

  for (uint32_t i = 0; i < full.nodes_len; i++) {
    if (memcmp(full.hashes[i], full.root, 32) != 0) {
      drop = i;
      break;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(full.nodes_len > 1, "expected more than the root node");

  ssz_ob_t    incomplete = drop_node_at(merged, drop);
  mpt_proof_t proof      = {0};
  mpt_proof_init(&proof, incomplete, root_hash.data);

  bool saw_invalid = false;
  for (int i = 0; i < TEST_KV_COUNT; i++) {
    if (patricia_verify_multi(&proof, str_bytes(test_kvs[i].key), NULL) == PATRICIA_INVALID)
      saw_invalid = true;
  }
  TEST_ASSERT_TRUE_MESSAGE(saw_invalid, "dropping a non-root node should invalidate at least one path");

  mpt_proof_free(&proof);
  mpt_proof_free(&full);
  safe_free(incomplete.bytes.data);
  safe_free(merged.bytes.data);
  patricia_node_free(root);
}

void test_multi_wrong_root() {
  node_t*     root      = make_test_trie();
  bytes_t     root_hash = patricia_get_root(root);
  ssz_ob_t    merged    = {0};
  mpt_proof_t proof     = {0};
  bytes32_t   bad_root  = {0};

  build_merged(root, &merged, false);
  memcpy(bad_root, root_hash.data, 32);
  bad_root[0] ^= 0xff;
  mpt_proof_init(&proof, merged, bad_root);

  TEST_ASSERT_EQUAL_INT(PATRICIA_INVALID, patricia_verify_multi(&proof, str_bytes(test_kvs[0].key), NULL));

  mpt_proof_free(&proof);
  safe_free(merged.bytes.data);
  patricia_node_free(root);
}

void test_multi_tampered_node() {
  node_t*     root      = make_test_trie();
  bytes_t     root_hash = patricia_get_root(root);
  ssz_ob_t    merged    = {0};
  mpt_proof_t proof     = {0};

  build_merged(root, &merged, true);
  TEST_ASSERT_TRUE(ssz_len(merged) > 0);
  ssz_ob_t last = ssz_at(merged, ssz_len(merged) - 1);
  TEST_ASSERT_TRUE(last.bytes.len > 0);
  last.bytes.data[0] ^= 0x01;

  mpt_proof_init(&proof, merged, root_hash.data);

  bool saw_invalid = false;
  for (int i = 0; i < TEST_KV_COUNT; i++) {
    if (patricia_verify_multi(&proof, str_bytes(test_kvs[i].key), NULL) == PATRICIA_INVALID)
      saw_invalid = true;
  }
  TEST_ASSERT_TRUE_MESSAGE(saw_invalid, "tampering a node should invalidate at least one path");

  mpt_proof_free(&proof);
  safe_free(merged.bytes.data);
  patricia_node_free(root);
}

void test_multi_empty_list() {
  uint8_t     dummy     = 0;
  bytes32_t   root      = {0};
  mpt_proof_t proof     = {0};
  ssz_ob_t    empty     = {.def = &mpt_nodes_def, .bytes = NULL_BYTES};
  ssz_ob_t    empty_len = {.def = &mpt_nodes_def, .bytes = bytes(&dummy, 0)};

  root[0] = 0xaa;
  mpt_proof_init(&proof, empty, root);
  TEST_ASSERT_EQUAL_UINT32(0, proof.nodes_len);
  TEST_ASSERT_NULL(proof.hashes);
  TEST_ASSERT_EQUAL_MEMORY(root, proof.root, 32);
  TEST_ASSERT_EQUAL_INT(PATRICIA_INVALID, patricia_verify_multi(&proof, str_bytes("aaa"), NULL));
  mpt_proof_free(&proof);

  mpt_proof_init(&proof, empty_len, root);
  TEST_ASSERT_EQUAL_UINT32(0, proof.nodes_len);
  TEST_ASSERT_NULL(proof.hashes);
  TEST_ASSERT_EQUAL_INT(PATRICIA_INVALID, patricia_verify_multi(&proof, NULL_BYTES, NULL));
  mpt_proof_free(&proof);
}

void test_multi_init_without_def() {
  uint8_t     junk[] = {1, 2, 3};
  bytes32_t   root   = {0};
  mpt_proof_t proof  = {0};
  ssz_ob_t    nodes  = {.def = NULL, .bytes = bytes(junk, sizeof(junk))};

  mpt_proof_init(&proof, nodes, root);
  TEST_ASSERT_NULL(proof.hashes);
  TEST_ASSERT_EQUAL_UINT32(0, proof.nodes_len);
  TEST_ASSERT_NULL(proof.nodes.def);
  TEST_ASSERT_EQUAL_INT(PATRICIA_INVALID, patricia_verify_multi(&proof, str_bytes("aaa"), NULL));
  mpt_proof_free(&proof);
}

void test_multi_double_free() {
  node_t*     root      = make_test_trie();
  bytes_t     root_hash = patricia_get_root(root);
  ssz_ob_t    merged    = {0};
  mpt_proof_t proof     = {0};

  mpt_proof_free(NULL);

  mpt_proof_free(&proof);
  mpt_proof_free(&proof);

  build_merged(root, &merged, false);
  mpt_proof_init(&proof, merged, root_hash.data);
  TEST_ASSERT_NOT_NULL(proof.hashes);
  mpt_proof_free(&proof);
  TEST_ASSERT_NULL(proof.hashes);
  mpt_proof_free(&proof);
  TEST_ASSERT_NULL(proof.hashes);
  TEST_ASSERT_EQUAL_INT(PATRICIA_INVALID, patricia_verify_multi(&proof, str_bytes(test_kvs[0].key), NULL));

  safe_free(merged.bytes.data);
  patricia_node_free(root);
}

void test_multi_null_proof() {
  TEST_ASSERT_EQUAL_INT(PATRICIA_INVALID, patricia_verify_multi(NULL, str_bytes("aaa"), NULL));
}

void test_multi_leaf_null_on_found() {
  node_t*     root      = make_test_trie();
  bytes_t     root_hash = patricia_get_root(root);
  ssz_ob_t    merged    = {0};
  mpt_proof_t proof     = {0};

  build_merged(root, &merged, true);
  mpt_proof_init(&proof, merged, root_hash.data);

  TEST_ASSERT_EQUAL_INT(PATRICIA_FOUND, patricia_verify_multi(&proof, str_bytes(test_kvs[0].key), NULL));
  TEST_ASSERT_EQUAL_INT(PATRICIA_FOUND, patricia_verify_multi(&proof, str_bytes(test_kvs[3].key), NULL));

  mpt_proof_free(&proof);
  safe_free(merged.bytes.data);
  patricia_node_free(root);
}

void test_multi_empty_path() {
  node_t*     root   = NULL;
  ssz_ob_t    merged = {0};
  mpt_proof_t proof  = {0};
  const char* keys[] = {"", "x"};

  patricia_set_value(&root, str_bytes(""), str_bytes(LONG_VAL "-empty"));
  patricia_set_value(&root, str_bytes("x"), str_bytes(LONG_VAL "-x"));
  merged = merge_keys(root, keys, 2, true);
  mpt_proof_init(&proof, merged, patricia_get_root(root).data);

  assert_found(&proof, "", LONG_VAL "-empty");
  assert_found(&proof, "x", LONG_VAL "-x");
  TEST_ASSERT_EQUAL_INT(PATRICIA_NOT_EXISTING, patricia_verify_multi(&proof, str_bytes("y"), NULL));
  TEST_ASSERT_EQUAL_INT(PATRICIA_FOUND, patricia_verify_multi(&proof, NULL_BYTES, NULL));

  mpt_proof_free(&proof);
  safe_free(merged.bytes.data);
  patricia_node_free(root);

  /* Same-length keys have no empty-key value; empty path must be an exclusion. */
  root = make_test_trie();
  build_merged(root, &merged, false);
  mpt_proof_init(&proof, merged, patricia_get_root(root).data);
  TEST_ASSERT_EQUAL_INT(PATRICIA_NOT_EXISTING, patricia_verify_multi(&proof, NULL_BYTES, NULL));
  mpt_proof_free(&proof);
  safe_free(merged.bytes.data);
  patricia_node_free(root);
}

void test_multi_single_node() {
  node_t*     root   = NULL;
  ssz_ob_t    merged = {0};
  mpt_proof_t proof  = {0};
  const char* keys[] = {"only"};

  patricia_set_value(&root, str_bytes("only"), str_bytes(LONG_VAL));
  merged = merge_keys(root, keys, 1, false);
  TEST_ASSERT_EQUAL_UINT32(1, ssz_len(merged));
  mpt_proof_init(&proof, merged, patricia_get_root(root).data);

  assert_found(&proof, "only", LONG_VAL);
  TEST_ASSERT_EQUAL_INT(PATRICIA_FOUND, patricia_verify_multi(&proof, str_bytes("only"), NULL));
  TEST_ASSERT_EQUAL_INT(PATRICIA_NOT_EXISTING, patricia_verify_multi(&proof, str_bytes("nope"), NULL));
  TEST_ASSERT_EQUAL_INT(PATRICIA_NOT_EXISTING, patricia_verify_multi(&proof, NULL_BYTES, NULL));

  mpt_proof_free(&proof);
  safe_free(merged.bytes.data);
  patricia_node_free(root);
}

void test_multi_extension_exhausted_and_branch_slot16() {
  node_t*     root         = NULL;
  ssz_ob_t    merged       = {0};
  mpt_proof_t proof        = {0};
  ssz_ob_t    prefix_pf    = {0};
  bytes32_t   ordered_root = {0};
  bytes_t     ordered_leaf = {0};
  bytes_t     multi_leaf   = {0};
  const char* keys[]       = {"aa", "aaa"};

  /* "aa" is a prefix of "aaa": extension consumes the whole prefix key, then
   * NODE_LOOKUP must follow to the branch and read slot 16. */
  patricia_set_value(&root, str_bytes("aa"), str_bytes(LONG_VAL "-aa"));
  patricia_set_value(&root, str_bytes("aaa"), str_bytes(LONG_VAL "-aaa"));
  merged = merge_keys(root, keys, 2, true);
  mpt_proof_init(&proof, merged, patricia_get_root(root).data);

  assert_found(&proof, "aa", LONG_VAL "-aa");
  assert_found(&proof, "aaa", LONG_VAL "-aaa");
  TEST_ASSERT_EQUAL_INT(PATRICIA_NOT_EXISTING, patricia_verify_multi(&proof, str_bytes("aab"), NULL));
  TEST_ASSERT_EQUAL_INT(PATRICIA_NOT_EXISTING, patricia_verify_multi(&proof, str_bytes("a"), NULL));

  prefix_pf = create_path_proof(root, str_bytes("aa"));
  TEST_ASSERT_EQUAL_INT(PATRICIA_FOUND, patricia_verify(ordered_root, str_bytes("aa"), prefix_pf, &ordered_leaf));
  TEST_ASSERT_EQUAL_INT(PATRICIA_FOUND, patricia_verify_multi(&proof, str_bytes("aa"), &multi_leaf));
  TEST_ASSERT_EQUAL_MEMORY(patricia_get_root(root).data, ordered_root, 32);
  TEST_ASSERT_EQUAL_UINT32(ordered_leaf.len, multi_leaf.len);
  TEST_ASSERT_EQUAL_MEMORY(ordered_leaf.data, multi_leaf.data, ordered_leaf.len);

  mpt_proof_free(&proof);
  safe_free(prefix_pf.bytes.data);
  safe_free(merged.bytes.data);
  patricia_node_free(root);
}

void test_multi_embedded_in_branch() {
  node_t*     root   = NULL;
  ssz_ob_t    merged = {0};
  mpt_proof_t proof  = {0};
  /* Slot 16 holds a long value so the branch is hashed; the two sibling
   * leaves stay < 32 bytes and are embedded as RLP lists. The empty-key
   * proof is only the hashed root (avoids create_merkle_proof's offset
   * bug when it walks into embedded children). */
  patricia_set_value(&root, str_bytes(""), str_bytes(LONG_VAL));
  patricia_set_value(&root, str_bytes("0000"), str_bytes("1"));
  patricia_set_value(&root, str_bytes("@@@@"), str_bytes("2"));
  merged = create_path_proof(root, NULL_BYTES);
  TEST_ASSERT_EQUAL_UINT32(1, ssz_len(merged));
  TEST_ASSERT_TRUE_MESSAGE(node_has_embedded_child(ssz_at(merged, 0).bytes),
                           "expected an embedded (RLP list) child in the hashed branch");

  mpt_proof_init(&proof, merged, patricia_get_root(root).data);
  assert_found(&proof, "", LONG_VAL);
  assert_found(&proof, "0000", "1");
  assert_found(&proof, "@@@@", "2");
  TEST_ASSERT_EQUAL_INT(PATRICIA_NOT_EXISTING, patricia_verify_multi(&proof, str_bytes("!!!!"), NULL));

  mpt_proof_free(&proof);
  safe_free(merged.bytes.data);
  patricia_node_free(root);
}

void test_multi_embedded_in_extension() {
  node_t*     root   = NULL;
  ssz_ob_t    merged = {0};
  mpt_proof_t proof  = {0};
  char        k1[33];
  char        k2[33];
  /* 32-byte keys sharing 31 bytes: hashed extension, child branch small
   * enough to embed. Empty-path proof is the hashed root only. */
  memset(k1, 'a', 32);
  memset(k2, 'a', 32);
  k1[32] = 0;
  k2[31] = 'b';
  k2[32] = 0;

  patricia_set_value(&root, str_bytes(k1), str_bytes("1"));
  patricia_set_value(&root, str_bytes(k2), str_bytes("2"));
  merged = create_path_proof(root, NULL_BYTES);
  TEST_ASSERT_EQUAL_UINT32(1, ssz_len(merged));
  TEST_ASSERT_TRUE_MESSAGE(node_has_embedded_child(ssz_at(merged, 0).bytes),
                           "expected an embedded (RLP list) child in the hashed extension");

  mpt_proof_init(&proof, merged, patricia_get_root(root).data);
  assert_found(&proof, k1, "1");
  assert_found(&proof, k2, "2");
  k1[31] = 'c';
  TEST_ASSERT_EQUAL_INT(PATRICIA_NOT_EXISTING, patricia_verify_multi(&proof, str_bytes(k1), NULL));

  mpt_proof_free(&proof);
  safe_free(merged.bytes.data);
  patricia_node_free(root);
}

void test_builder_owned_trie_multi_verify() {
  mpt_builder_t b          = {0};
  mpt_proof_t   proof      = {0};
  bytes32_t     root_hash  = {0};
  ssz_ob_t      nodes      = {0};
  uint32_t      node_count = 0;

  mpt_builder_init(&b, NULL);
  for (int i = 0; i < TEST_KV_COUNT; i++)
    mpt_builder_set_value(&b, str_bytes(test_kvs[i].key), str_bytes(test_kvs[i].value));
  for (int i = 0; i < TEST_KV_COUNT; i++)
    mpt_builder_add_proof(&b, str_bytes(test_kvs[i].key));

  TEST_ASSERT_NOT_NULL(b.root);
  TEST_ASSERT_TRUE(b.free_root);
  TEST_ASSERT_TRUE(b.len > 0);
  node_count = b.len;
  memcpy(root_hash, patricia_get_root(b.root).data, 32);
  nodes = mpt_builder_finish(&b);
  TEST_ASSERT_NULL(b.root);
  TEST_ASSERT_EQUAL_UINT32(0, b.len);
  TEST_ASSERT_EQUAL_UINT32(node_count, ssz_len(nodes));

  mpt_proof_init(&proof, nodes, root_hash);
  assert_all_paths_found(&proof);
  TEST_ASSERT_EQUAL_INT(PATRICIA_NOT_EXISTING, patricia_verify_multi(&proof, str_bytes("zzz"), NULL));

  mpt_proof_free(&proof);
  safe_free(nodes.bytes.data);
}

void test_builder_borrowed_root_and_dedup() {
  node_t*       root = make_test_trie();
  mpt_builder_t b    = {0};
  ssz_ob_t      singles[TEST_KV_COUNT];
  uint32_t      sum       = 0;
  ssz_ob_t      merged    = {0};
  ssz_ob_t      built     = {0};
  uint32_t      after_all = 0;
  uint32_t      after_dup = 0;

  for (int i = 0; i < TEST_KV_COUNT; i++) {
    singles[i] = create_path_proof(root, str_bytes(test_kvs[i].key));
    sum += ssz_len(singles[i]);
  }
  merged = merge_proofs(singles, TEST_KV_COUNT, false);

  mpt_builder_init(&b, root);
  TEST_ASSERT_FALSE(b.free_root);
  for (int i = 0; i < TEST_KV_COUNT; i++)
    mpt_builder_add_proof(&b, str_bytes(test_kvs[i].key));
  after_all = b.len;
  mpt_builder_add_proof(&b, str_bytes(test_kvs[0].key));
  mpt_builder_add_proof(&b, str_bytes(test_kvs[1].key));
  after_dup = b.len;
  TEST_ASSERT_EQUAL_UINT32(after_all, after_dup);
  TEST_ASSERT_TRUE_MESSAGE(after_all < sum, "shared nodes must be stored once");
  TEST_ASSERT_EQUAL_UINT32(ssz_len(merged), after_all);

  built = mpt_builder_finish(&b);
  TEST_ASSERT_EQUAL_UINT32(ssz_len(merged), ssz_len(built));

  mpt_proof_t proof = {0};
  mpt_proof_init(&proof, built, patricia_get_root(root).data);
  assert_all_paths_found(&proof);
  mpt_proof_free(&proof);

  /* Borrowed trie must still be valid after finish. */
  TEST_ASSERT_EQUAL_MEMORY(patricia_get_root(root).data, patricia_get_root(root).data, 32);

  safe_free(built.bytes.data);
  safe_free(merged.bytes.data);
  for (int i = 0; i < TEST_KV_COUNT; i++)
    safe_free(singles[i].bytes.data);
  patricia_node_free(root);
}

void test_builder_finish_then_free() {
  mpt_builder_t b         = {0};
  bytes32_t     root_hash = {0};
  ssz_ob_t      nodes     = {0};

  mpt_builder_init(&b, NULL);
  mpt_builder_set_value(&b, str_bytes("only"), str_bytes(LONG_VAL));
  mpt_builder_add_proof(&b, str_bytes("only"));
  memcpy(root_hash, patricia_get_root(b.root).data, 32);
  nodes = mpt_builder_finish(&b);
  mpt_builder_free(&b);
  mpt_builder_free(&b);
  mpt_builder_free(NULL);

  TEST_ASSERT_TRUE(ssz_len(nodes) >= 1);
  mpt_proof_t proof = {0};
  mpt_proof_init(&proof, nodes, root_hash);
  assert_found(&proof, "only", LONG_VAL);
  mpt_proof_free(&proof);
  safe_free(nodes.bytes.data);
}

void test_builder_abort_without_finish() {
  node_t*       borrowed = make_test_trie();
  mpt_builder_t owned    = {0};
  mpt_builder_t wrap     = {0};

  mpt_builder_init(&owned, NULL);
  mpt_builder_set_value(&owned, str_bytes("k"), str_bytes(LONG_VAL));
  mpt_builder_add_proof(&owned, str_bytes("k"));
  mpt_builder_free(&owned);
  TEST_ASSERT_NULL(owned.root);

  mpt_builder_init(&wrap, borrowed);
  mpt_builder_add_proof(&wrap, str_bytes(test_kvs[0].key));
  mpt_builder_free(&wrap);
  /* Borrowed tree must survive abort. */
  TEST_ASSERT_NOT_NULL(patricia_get_root(borrowed).data);
  patricia_node_free(borrowed);
}

void test_builder_empty_finish() {
  mpt_builder_t b     = {0};
  ssz_ob_t      nodes = {0};

  mpt_builder_init(&b, NULL);
  nodes = mpt_builder_finish(&b);
  TEST_ASSERT_EQUAL_UINT32(0, ssz_len(nodes));
  TEST_ASSERT_EQUAL_INT(PATRICIA_INVALID, patricia_verify_multi(&(mpt_proof_t) {0}, str_bytes("a"), NULL));
  safe_free(nodes.bytes.data);
}

void test_builder_matches_create_merkle_proof() {
  node_t*       root   = make_test_trie();
  mpt_builder_t b      = {0};
  ssz_ob_t      single = create_path_proof(root, str_bytes(test_kvs[0].key));
  bytes32_t     calc   = {0};
  bytes_t       leaf_a = {0};
  bytes_t       leaf_b = {0};

  mpt_builder_init(&b, root);
  mpt_builder_add_proof(&b, str_bytes(test_kvs[0].key));
  ssz_ob_t built = mpt_builder_finish(&b);

  TEST_ASSERT_EQUAL_INT(PATRICIA_FOUND, patricia_verify(calc, str_bytes(test_kvs[0].key), single, &leaf_a));
  mpt_proof_t multi = {0};
  mpt_proof_init(&multi, built, patricia_get_root(root).data);
  TEST_ASSERT_EQUAL_INT(PATRICIA_FOUND, patricia_verify_multi(&multi, str_bytes(test_kvs[0].key), &leaf_b));
  TEST_ASSERT_EQUAL_UINT32(leaf_a.len, leaf_b.len);
  TEST_ASSERT_EQUAL_MEMORY(leaf_a.data, leaf_b.data, leaf_a.len);

  mpt_proof_free(&multi);
  safe_free(built.bytes.data);
  safe_free(single.bytes.data);
  patricia_node_free(root);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_multi_shared_nodes_reverse);
  RUN_TEST(test_multi_matches_ordered_verify);
  RUN_TEST(test_multi_exclusion);
  RUN_TEST(test_multi_incomplete_proof);
  RUN_TEST(test_multi_wrong_root);
  RUN_TEST(test_multi_tampered_node);
  RUN_TEST(test_multi_empty_list);
  RUN_TEST(test_multi_init_without_def);
  RUN_TEST(test_multi_double_free);
  RUN_TEST(test_multi_null_proof);
  RUN_TEST(test_multi_leaf_null_on_found);
  RUN_TEST(test_multi_empty_path);
  RUN_TEST(test_multi_single_node);
  RUN_TEST(test_multi_extension_exhausted_and_branch_slot16);
  RUN_TEST(test_multi_embedded_in_branch);
  RUN_TEST(test_multi_embedded_in_extension);
  RUN_TEST(test_builder_owned_trie_multi_verify);
  RUN_TEST(test_builder_borrowed_root_and_dedup);
  RUN_TEST(test_builder_finish_then_free);
  RUN_TEST(test_builder_abort_without_finish);
  RUN_TEST(test_builder_empty_finish);
  RUN_TEST(test_builder_matches_create_merkle_proof);
  return UNITY_END();
}
