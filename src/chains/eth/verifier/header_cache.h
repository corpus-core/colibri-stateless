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

#ifndef header_cache_h__
#define header_cache_h__

#include "bytes.h"
#include "chains.h"
#include "ssz.h"

#ifdef __cplusplus
extern "C" {
#endif

// : Verified Header Cache
//
// A process-global LRU cache of **verified** execution-layer block headers, owned by the
// verifier. Entries are only written after the corresponding data has been cryptographically
// verified, so a cache hit is a trust anchor:
//
// - The verifier uses it to resolve the `blockHash` variant of `ETH_BLOCK_PROOF_UNION`
//   (a proof that only references a block hash because the header was already verified).
// - The (hybrid) prover uses it to cache verified `header_data` / execution payloads
//   fetched from a remote prover and to decide when a proof can reference a block by
//   hash only.
//
// The prover may depend on this module (prover -> verifier is an allowed dependency),
// but not vice versa. The whole module can be disabled with the `EL_HEADER_CACHE`
// CMake option (e.g. for embedded targets): all functions then become no-ops that
// always miss, which disables the `blockHash`-only proofs and hybrid caching.
//
// Thread safety: all cache operations are serialized by an internal mutex and
// `c4_header_cache_get_el_header()` returns an owned copy, so the verifier path is
// safe for multi-threaded bindings. The entry-pointer getters (`get_by_number` /
// `get_by_hash`) return borrowed pointers that are only valid until the next cache
// write; they are intended for the prover's hybrid flow, where the borrow is consumed
// within the same request iteration (same semantics as the previous prover-local cache).
// Poisoning is not possible since only verified data is written.

#ifndef HEADER_CACHE_SIZE
#define HEADER_CACHE_SIZE 256
#endif

/**
 * A single cached verified block header entry.
 *
 * All payload fields are optional and independently heap-allocated (via `bytes_dup()`):
 * - `el_header`: the RLP-encoded execution layer header (`keccak(el_header) == block_hash`)
 * - `header_data`: SSZ-encoded `ETH_BLOCK_HEADER_DATA` (14 fields, ~540 bytes)
 * - `execution`: full SSZ execution payload (for tx/receipt proofs needing all transactions)
 */
typedef struct {
  chain_id_t chain_id;
  uint64_t   block_number;
  bytes32_t  block_hash;
  bytes_t    el_header; // RLP-encoded EL header (empty when not cached)
  ssz_ob_t   el_body;   // optional full SSZ execution payload or at least the body with transactions and withdrawals
  uint64_t   last_used; // monotonic LRU counter (0 = slot unused), updated on every hit
} verified_header_entry_t;

#ifdef EL_HEADER_CACHE

/**
 * Looks up a verified header entry by block number. Only returns entries that
 * carry `header_data`. A hit marks the entry as recently used (LRU touch).
 *
 * @param chain_id the chain to match
 * @param block_number the block number to look up
 * @return pointer to the cached entry, or NULL on miss
 */
const verified_header_entry_t* c4_header_cache_get_by_number(chain_id_t chain_id, uint64_t block_number);

/**
 * Looks up a verified header entry by block hash. Only returns entries that
 * carry `header_data`. A hit marks the entry as recently used (LRU touch).
 *
 * @param chain_id the chain to match
 * @param block_hash 32-byte block hash to look up
 * @return pointer to the cached entry, or NULL on miss
 */
const verified_header_entry_t* c4_header_cache_get_by_hash(chain_id_t chain_id, const uint8_t* block_hash);

/**
 * Stores verified SSZ `ETH_BLOCK_HEADER_DATA` for a block. Merges into an existing
 * entry for the same (chain, block number, hash); evicts the least recently used
 * slot when the cache is full. The `header_data.bytes` are duplicated.
 *
 * @param chain_id the chain ID
 * @param block_number the block number
 * @param block_hash 32-byte block hash
 * @param el_header rlp-encoded el header (will be copied)
 * @param el_body optional SSZ-encoded execution payload or at least the body with transactions and withdrawals (will be copied)
 */
void c4_header_cache_put(chain_id_t chain_id, uint64_t block_number, const uint8_t* block_hash, bytes_t el_header, ssz_ob_t* el_body);

/**
 * Returns the verified RLP-encoded EL header for a block hash, or `NULL_BYTES` on miss.
 * A hit marks the entry as recently used (LRU touch), protecting actively referenced
 * headers from eviction. The returned bytes are an **owned copy** (allocated via
 * `bytes_dup()`); the caller must free them with `safe_free()`. This makes the result
 * immune to concurrent eviction or reorg resets in multi-threaded bindings.
 *
 * @param chain_id the chain ID
 * @param block_hash 32-byte execution block hash
 * @return heap-allocated copy of the cached RLP header (caller frees), or `NULL_BYTES` on miss
 */
bytes_t c4_header_cache_get_el_header(chain_id_t chain_id, const uint8_t* block_hash, ssz_ob_t* el_body);

/**
 * Checks whether a verified RLP EL header is cached for the block hash without
 * copying it. A hit marks the entry as recently used (LRU touch).
 *
 * @param chain_id the chain ID
 * @param block_hash 32-byte execution block hash
 * @return true if the RLP header is cached
 */
bool c4_header_cache_has_el_header(chain_id_t chain_id, const uint8_t* block_hash);

/**
 * Writes the hash of the newest verified block (highest block number) whose RLP header
 * is cached into `block_hash`. Used by clients to advertise their `last_block_hash` to
 * a remote prover, allowing it to omit the block proof (`blockHash` union variant).
 * The entry is LRU-touched so it stays resolvable until the proof response arrives.
 *
 * @param chain_id the chain ID
 * @param block_hash receives the 32-byte block hash on success
 * @return true if a cached header was found for the chain
 */
bool c4_header_cache_latest_block_hash(chain_id_t chain_id, bytes32_t block_hash);

/**
 * Frees all cached entries and resets the cache to its initial state
 * (used on shutdown and for deterministic tests).
 */
void c4_header_cache_clear(void);

#else // !EL_HEADER_CACHE: no-op stubs so callers compile without the cache (embedded targets)

static inline const verified_header_entry_t* c4_header_cache_get_by_number(chain_id_t chain_id, uint64_t block_number) {
  (void) chain_id;
  (void) block_number;
  return NULL;
}
static inline const verified_header_entry_t* c4_header_cache_get_by_hash(chain_id_t chain_id, const uint8_t* block_hash) {
  (void) chain_id;
  (void) block_hash;
  return NULL;
}
static inline void c4_header_cache_put(chain_id_t chain_id, uint64_t block_number, const uint8_t* block_hash, bytes_t el_header, ssz_ob_t* el_body) {
  (void) chain_id;
  (void) block_number;
  (void) block_hash;
  (void) el_body;
  (void) el_header;
}
static inline bytes_t c4_header_cache_get_el_header(chain_id_t chain_id, const uint8_t* block_hash, ssz_ob_t* el_body) {
  (void) chain_id;
  (void) block_hash;
  (void) el_body;
  return NULL_BYTES;
}
static inline bool c4_header_cache_has_el_header(chain_id_t chain_id, const uint8_t* block_hash) {
  (void) chain_id;
  (void) block_hash;
  return false;
}
static inline bool c4_header_cache_latest_block_hash(chain_id_t chain_id, bytes32_t block_hash) {
  (void) chain_id;
  (void) block_hash;
  return false;
}
static inline void c4_header_cache_clear(void) {}

#endif // EL_HEADER_CACHE

#ifdef __cplusplus
}
#endif

#endif // header_cache_h__
