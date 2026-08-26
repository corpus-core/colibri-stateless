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

#ifndef patricia_h__
#define patricia_h__

#include "bytes.h"
#include "ssz.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef struct node node_t;
typedef enum {
  PATRICIA_INVALID      = 0,
  PATRICIA_FOUND        = 1,
  PATRICIA_NOT_EXISTING = 2,
} patricia_result_t;

/**
 * Cached, reusable view over an unordered list of MPT nodes.
 *
 * `nodes` is a view (not owned). The SSZ definition and the node bytes must
 * remain valid until `mpt_proof_free()`. `hashes` is allocated by
 * `mpt_proof_init()` and holds the Keccak-256 of each node so multiple
 * path lookups can reuse the same hashed set.
 *
 * Node order in `nodes` does not matter. Verification walks from `root`
 * by looking up the next node hash.
 */
typedef struct {
  uint32_t   nodes_len; /**< number of nodes in the list                       */
  ssz_ob_t   nodes;     /**< SSZ list of serialized RLP nodes (order ignored)  */
  bytes32_t* hashes;    /**< cached Keccak-256 of each node (`nodes_len`)      */
  bytes32_t  root;      /**< expected trie root hash                           */
} mpt_proof_t;

/**
 * Incremental builder for an unordered multi-path MPT proof.
 *
 * Insert all values first, then add each path. Shared hashed nodes are
 * stored only once. Two ways to populate the trie:
 *
 * 1. Borrow an existing tree (`root != NULL`). The caller keeps ownership
 *    of the tree and must copy `patricia_get_root()` only if they free it
 *    themselves later.
 * 2. Let the builder own the tree (`root == NULL`) and insert with
 *    `mpt_builder_set_value()`. Copy the root hash **before** `finish()`,
 *    because finish frees an owned tree.
 *
 * End with `mpt_builder_finish()` (caller owns the SSZ list) or abort
 * with `mpt_builder_free()`. Do not call both; finish already releases
 * builder resources. A subsequent `free` is a no-op.
 *
 * Example:
 * ```c
 * mpt_builder_t b = {0};
 * bytes32_t     root_hash;
 * mpt_proof_t   proof = {0};
 *
 * mpt_builder_init(&b, NULL);
 * mpt_builder_set_value(&b, key_a, val_a);
 * mpt_builder_set_value(&b, key_b, val_b);
 * mpt_builder_add_proof(&b, key_a);
 * mpt_builder_add_proof(&b, key_b);
 * memcpy(root_hash, patricia_get_root(b.root).data, 32);
 * ssz_ob_t nodes = mpt_builder_finish(&b);
 *
 * mpt_proof_init(&proof, nodes, root_hash);
 * patricia_verify_multi(&proof, key_a, &leaf);
 * mpt_proof_free(&proof);
 * safe_free(nodes.bytes.data);
 * ```
 */
typedef struct {
  bool          free_root; /**< true if `init` created/owns the trie          */
  ssz_builder_t builder;   /**< SSZ list of unique serialized nodes           */
  uint32_t      len;       /**< number of unique hashed nodes added so far    */
  node_t*       root;      /**< trie used to walk proofs                      */
} mpt_builder_t;
/**
 * Hash every node once and store the result in `proof` for reuse.
 *
 * Must be paired with `mpt_proof_free()`. Calling init again without free
 * leaks the previous `hashes` allocation. Invalid SSZ, empty lists, or more
 * than 65536 nodes fail closed (`hashes == NULL`); verify then returns
 * `PATRICIA_INVALID`.
 *
 * @param proof output struct (must not be NULL)
 * @param nodes SSZ list of serialized MPT nodes (order does not matter)
 * @param root expected trie root hash
 */
void mpt_proof_init(mpt_proof_t* proof, ssz_ob_t nodes, bytes32_t root);

/**
 * Free the cached hash table. Does not free `nodes` (caller-owned).
 *
 * @param proof proof previously initialized with `mpt_proof_init()` (NULL-safe)
 */
void mpt_proof_free(mpt_proof_t* proof);

/**
 * Verify an ordered Patricia Merkle proof (nodes from root to leaf).
 *
 * @param root output: calculated root hash of the first proof node
 * @param path key as raw bytes (converted to nibbles internally)
 * @param proof SSZ list of serialized nodes in walk order
 * @param last_value optional output leaf (view into the proof bytes)
 * @return `PATRICIA_FOUND`, `PATRICIA_NOT_EXISTING`, or `PATRICIA_INVALID`
 */
patricia_result_t patricia_verify(bytes32_t root, bytes_t path, ssz_ob_t proof, bytes_t* last_value);

/**
 * Verify a path against an unordered, shared set of MPT nodes.
 *
 * Walks from `proof->root` by looking up each successor hash in the cached
 * table. A missing successor is always `PATRICIA_INVALID` (exclusion must
 * be proven by a node, never by omitting one). `leaf` is a view into
 * `proof->nodes` and stays valid until those bytes are freed.
 *
 * @param proof initialized multi-proof (must not be NULL)
 * @param path key as raw bytes (converted to nibbles internally)
 * @param leaf optional output leaf (view into the node bytes)
 * @return `PATRICIA_FOUND`, `PATRICIA_NOT_EXISTING`, or `PATRICIA_INVALID`
 */
patricia_result_t patricia_verify_multi(mpt_proof_t* proof, bytes_t path, bytes_t* leaf);

ssz_ob_t patricia_create_merkle_proof(node_t* root, bytes_t path);
void     patricia_set_value(node_t** root, bytes_t path, bytes_t value);
void     patricia_node_free(node_t* node);
node_t*  patricia_clone_tree(node_t* node);
bytes_t  patricia_get_root(node_t* node);

/**
 * Start a multi-proof builder.
 *
 * If `root` is NULL the builder owns the trie (create it with
 * `mpt_builder_set_value`) and will free it on `finish`/`free`.
 * If `root` is set, the caller keeps ownership and the builder only
 * walks that tree.
 *
 * @param builder builder to initialize (must not be NULL)
 * @param root existing trie, or NULL to own a new empty trie
 */
void mpt_builder_init(mpt_builder_t* builder, node_t* root);

/**
 * Abort the builder and free its resources.
 *
 * Frees an owned trie and the unfinished SSZ buffers. Safe on NULL and
 * after `mpt_builder_finish()`. Do not use after `finish` if you still
 * need the returned SSZ bytes — those are caller-owned, not freed here.
 *
 * @param builder builder from `mpt_builder_init()` (NULL-safe)
 */
void mpt_builder_free(mpt_builder_t* builder);

/**
 * Seal the node list and release builder resources.
 *
 * Returns an unordered SSZ list of unique serialized hashed nodes
 * (`List[bytes]`). The caller owns `bytes.data` and must `safe_free` it.
 * An owned trie is freed here; copy `patricia_get_root(builder->root)`
 * into a `bytes32_t` **before** calling finish.
 *
 * @param builder initialized builder (must not be NULL)
 * @return SSZ list of unique nodes, or empty on NULL builder
 */
ssz_ob_t mpt_builder_finish(mpt_builder_t* builder);

/**
 * Walk `path` from the current root and append any hashed node not yet
 * in the list.
 *
 * Order does not matter. Adding the same path twice is a no-op for
 * already stored nodes. Call only after all values are inserted;
 * changing the trie afterwards leaves previously serialized nodes stale.
 *
 * @param builder initialized builder (must not be NULL)
 * @param path key as raw bytes (same encoding as `patricia_set_value`)
 */
void mpt_builder_add_proof(mpt_builder_t* builder, bytes_t path);

/**
 * Insert or update a value in the builder's trie.
 *
 * Used when the builder owns the tree (`init` with `root == NULL`) or
 * to mutate a borrowed tree. Must run before the matching
 * `mpt_builder_add_proof()` calls.
 *
 * @param builder initialized builder (must not be NULL)
 * @param path key as raw bytes
 * @param value leaf value (copied into the trie)
 */
void mpt_builder_set_value(mpt_builder_t* builder, bytes_t path, bytes_t value);

#ifdef TEST
void patricia_dump(node_t* root);
#endif

#ifdef __cplusplus
}
#endif

#endif
