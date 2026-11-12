/*
 * Copyright 2025,2026 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef eez_blockhash_store_h__
#define eez_blockhash_store_h__

#include "bytes.h"
#include "chains/eth/verifier/eip2935.h" // ETH_EIP2935_HISTORY_WINDOW
#include "chains/eth/verifier/patricia.h"
#include "json.h"
#include "server.h"
#include "util/chains.h"
#include "util/common.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Snapshot of the EIP-2935 history contract of an L2 at the anchor block whose hash
 * was most recently committed on L1 (`L2ExecutionPerformed`).
 *
 * The 8191 slot values are copied verbatim from the storage of block `anchor_number`
 * on L2. Slots that are not yet populated (fresh chain) are zero. `account_proof` is
 * the complete account proof of `ETH_EIP2935_HISTORY_ADDRESS` at `anchor_number`
 * returned by `eth_getProof` and still has to match `storage_hash` -- the watcher only
 * writes a snapshot after that check.
 */
typedef struct {
  bytes32_t anchor_hash;                        /** 32-byte L2 block hash emitted in the L1 event */
  uint64_t  anchor_number;                      /** L2 block number of `anchor_hash` */
  uint64_t  l1_block_number;                    /** L1 block the event was emitted in */
  bytes32_t storage_hash;                       /** `storageHash` of the account proof, matches the trie we build */
  bytes32_t code_hash;                          /** `codeHash` of the account proof */
  bytes32_t nonce;                              /** `nonce` of the account proof (be, mostly single byte) */
  bytes32_t balance;                            /** `balance` of the account proof */
  bytes32_t hashes[ETH_EIP2935_HISTORY_WINDOW]; /** slot value for every EIP-2935 slot (0 = never populated) */
  bytes_t*  account_proof_nodes;                /** raw RLP nodes of the account proof */
  uint32_t  account_proof_count;                /** number of entries in `account_proof_nodes` */
} eez_bh_snapshot_t;

/**
 * Starts the blockhash-store watcher.
 *
 * The watcher polls L1 for `L2ExecutionPerformed` events targeting this server's
 * rollup, fetches the matching L2 history state, verifies the storage root, and
 * publishes a snapshot that the EEZ prover path can consume. The watcher only runs
 * when all of these are true:
 *
 * - the server is configured for an EEZ chain (`c4_chain_type(chain_id) == C4_CHAIN_TYPE_EEZ`)
 * - `eez_config.l1_rpc` is set (and registered with the HTTP client as the L1 RPC list)
 * - `eez_config.blockhash_store` is set to a writable directory
 *
 * @param server initialized HTTP server
 */
void eez_bh_init(http_server_t* server);

/**
 * Stops the watcher and releases its state.
 *
 * Safe to call multiple times and before `eez_bh_init`.
 *
 * @param server HTTP server, used only for chain context
 */
void eez_bh_shutdown(http_server_t* server);

/**
 * Returns the current in-memory snapshot, or `NULL` while none is available yet.
 *
 * The caller must not free the returned pointer. The snapshot is replaced in place
 * when the watcher detects a new L1 event; readers that keep the pointer across
 * event-loop ticks must be prepared for it to be freed.
 *
 * @return the active snapshot or `NULL`
 */
const eez_bh_snapshot_t* eez_bh_current(void);

/**
 * Serializes a snapshot to the compact on-disk format written by the watcher.
 *
 * Layout (little-endian for all integers):
 *   [0..4)    magic "EEZB"
 *   [4..6)    version (uint16, currently 1)
 *   [6..14)   anchor_number (uint64)
 *   [14..22)  l1_block_number (uint64)
 *   [22..54)  anchor_hash
 *   [54..86)  storage_hash
 *   [86..118) code_hash
 *   [118..150) nonce (left-padded big-endian)
 *   [150..182) balance (left-padded big-endian)
 *   [182..184) account_proof_count (uint16)
 *   [184..]   for each proof node: uint16 len | bytes
 *   [tail]    8191 * 32 bytes of slot values
 *
 * @param snap snapshot to serialize (must not be `NULL`)
 * @param out buffer that receives the serialized bytes (appended)
 */
void eez_bh_serialize(const eez_bh_snapshot_t* snap, buffer_t* out);

/**
 * Parses a snapshot previously written by `eez_bh_serialize`.
 *
 * The returned snapshot owns its `account_proof_nodes`; free it with
 * `eez_bh_snapshot_free`. Returns `NULL` on malformed input.
 *
 * @param blob serialized bytes
 * @return parsed snapshot or `NULL`
 */
eez_bh_snapshot_t* eez_bh_deserialize(bytes_t blob);

/**
 * Frees a snapshot created by `eez_bh_deserialize`.
 *
 * NULL-safe.
 *
 * @param snap snapshot to free
 */
void eez_bh_snapshot_free(eez_bh_snapshot_t* snap);

/**
 * Builds a Patricia Merkle Trie out of the 8191 EIP-2935 slot values and checks that
 * its root matches `storage_hash`.
 *
 * On a match, ownership of the trie is transferred to `*out_trie` (free it with
 * `patricia_node_free` when done). On a mismatch, the trie is freed internally and
 * `*out_trie` is set to `NULL`. `hashes` must point to `ETH_EIP2935_HISTORY_WINDOW` entries; the
 * function treats all-zero entries as empty slots and skips them (consistent with how
 * the Ethereum storage trie omits zero-valued slots).
 *
 * Exposed for unit tests and tooling; the watcher uses it internally.
 *
 * @param hashes 8191 slot values indexed by `block_number mod 8191`
 * @param storage_hash expected trie root (e.g. `storageHash` from an account proof)
 * @param out_trie receives the trie on match, `NULL` otherwise
 * @return `true` on root match, `false` otherwise
 */
bool eez_bh_build_and_verify_trie(const bytes32_t* hashes, const bytes32_t storage_hash, node_t** out_trie);

/**
 * Internal-call handler the EEZ prover routes EIP-2935 proof requests through when it
 * runs inside the HTTP server. The handler consumes paths of the form:
 *
 * ```
 * eez_blockhash/<anchor_number>/<slot_index>
 * ```
 *
 * and answers with a synthesized JSON `eth_getProof` result that is bit-compatible with
 * `JSON_ETH_PROOF_FIELDS`. Returns `true` when it handled the path, `false` otherwise
 * (so other internal handlers get their chance).
 *
 * @param r internal request to serve
 * @return `true` when the path matched and the handler took ownership of the request
 */
bool c4_handle_eez_blockhash(single_request_t* r);

/**
 * One parsed `L2ExecutionPerformed` event. Shared with unit tests via `eez_bh_parse_log`.
 */
typedef struct {
  uint64_t  rollup_id;       /** value of the indexed `rollupId` topic */
  bytes32_t new_root;        /** `newRoot` from the data field (L2 block hash) */
  uint64_t  l1_block_number; /** L1 block number the event was emitted in */
} eez_bh_event_t;

/**
 * Parses one log entry of the `L2ExecutionPerformed(uint64 indexed rollupId, bytes32 newRoot, uint256 etherBalance)`
 * event.
 *
 * ABI layout (what `eth_getLogs` returns):
 * - `topics[0]` event signature
 * - `topics[1]` 32-byte big-endian pad of the indexed `rollupId`
 * - `data`      64 bytes = `newRoot (32) || etherBalance (32)`
 *
 * Returns `false` when the entry is malformed or the upper 24 bytes of the `rollupId`
 * topic are not zero (indicating a value outside the `uint64` range). Exposed for unit
 * tests when `TEST` is defined; otherwise `static` inside `blockhash_store.c`.
 *
 * @param entry JSON log entry as returned by `eth_getLogs`
 * @param out receives the parsed event on success
 * @return `true` when parsing succeeded
 */
C4_STATIC_UNLESS_TEST bool eez_bh_parse_log(json_t entry, eez_bh_event_t* out);

#ifdef __cplusplus
}
#endif

#endif // eez_blockhash_store_h__
