/*
 * Copyright (c) 2026 corpus.core
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

/**
 * Shared helpers for Beacon-API light-client wire framing:
 *   - `light_client/updates` list framing + `ForkDigest` -> fork resolution.
 *   - `light_client/bootstrap` size-based fork detection.
 *
 * Central home for the LCU/Bootstrap wire logic that used to live scattered
 * across `sync_committee.c`, `historic_proof.c`, `proof_sync.c` and
 * `period_store_lc.c`. The old slot-heuristic `c4_eth_get_fork_for_lcu`
 * has been replaced by:
 *
 *   - `c4_eth_fork_from_context` for LCU chunks (uses the 4-byte
 *     `context = compute_fork_digest(gvr, epoch)` from the wire prefix,
 *     including the Fulu/EIP-7892 blob-parameter mix-in).
 *   - `c4_eth_decode_bootstrap` for `LightClientBootstrap` blobs (uses
 *     the fixed container size plus `ssz_is_valid`).
 *
 * See `<repo>/AGENTS.md` and GitHub issue #356 for background.
 */

#ifndef lcu_wire_h__
#define lcu_wire_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "beacon_types.h"
#include "bytes.h"
#include "chains.h"
#include "ssz.h"
#include "state.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * Wire-format constants for `GET eth/v1/beacon/light_client/updates`.
 *
 * Each chunk is:
 *
 * ```text
 *   [0 .. 8)   uint64 LE length (= 4 + payload_len)
 *   [8 .. 12)  ForkDigest context (4 bytes)
 *   [12 ..)    SSZ payload
 * ```
 */
#define C4_LCU_WIRE_LENGTH_SIZE  8u
#define C4_LCU_WIRE_CONTEXT_SIZE 4u
#define C4_LCU_WIRE_PREFIX_SIZE  (C4_LCU_WIRE_LENGTH_SIZE + C4_LCU_WIRE_CONTEXT_SIZE)

/**
 * Bootstrap fork detection anchors.
 *
 * Bootstrap has no context/digest on the wire (Beacon API exposes the fork
 * via `Eth-Consensus-Version` response header, which we do not persist on
 * `data_request_t`). The Deneb / Electra / Fulu bootstrap containers are
 * *not* fixed size because the embedded `LightClientHeader.execution` has
 * a variable `extraData` list. Their fork-defining discriminator is instead
 * the size of the container's fixed portion -- i.e. the value of the leading
 * `header` offset in an SSZ container with one variable field first:
 *
 * - Deneb        : 4 + sync_committee(24624) + branch(5*32)  = 24788
 * - Electra/Fulu : 4 + sync_committee(24624) + branch(6*32)  = 24820
 *
 * The Gloas bootstrap has no variable field (Gloas replaces the whole
 * execution payload with just `executionBlockHash`), so its total size
 * is deterministic:
 *
 * - Gloas        : header(496) + sync_committee(24624) + branch(11*32) = 25472
 */
#define C4_ETH_DENEB_BOOTSTRAP_FIXED_SIZE   24788u
#define C4_ETH_ELECTRA_BOOTSTRAP_FIXED_SIZE 24820u
#define C4_ETH_GLOAS_BOOTSTRAP_SIZE         25472u // must equal `C4_GLOAS_BOOTSTRAP_SIZE` in bootstrap_gloas.h

/**
 * View into a single chunk of a light-client-update list, produced by
 * `c4_eth_walk_lcu_list`. The `update` is a *view* into the caller's
 * buffer -- no allocation, no ownership.
 */
typedef struct {
  uint32_t       index;   ///< 0-based position of the chunk inside the list
  fork_id_t      fork;    ///< fork resolved from the wire `context` (never `C4_FORK_INVALID`)
  ssz_ob_t       update;  ///< `.bytes` = payload (without the 12-byte wire prefix), `.def` = LCU SSZ def for `fork`
  const uint8_t* context; ///< pointer into the source buffer at the 4-byte context (borrowed, do not free)
} c4_lcu_chunk_t;

/**
 * Callback invoked once per chunk by `c4_eth_walk_lcu_list`.
 *
 * Return `false` to abort the walk; the walker returns `false`. Framing
 * may already have set `req->validated` (see `c4_eth_walk_lcu_list`).
 *
 * @param user opaque pointer passed through from the walker call
 * @param chunk borrowed view of the current chunk (invalidated after return)
 * @return `true` to continue, `false` to abort
 */
typedef bool (*c4_lcu_chunk_cb_t)(void* user, const c4_lcu_chunk_t* chunk);

/**
 * Computes the 4-byte `ForkDigest` for `(chain_id, epoch)`.
 *
 * Pre-Fulu this is `hash_tree_root(ForkData{version, gvr})[:4]`. From Fulu
 * onward (EIP-7892) the digest is mixed with the active blob parameters:
 *
 * ```text
 * xor(base_digest, sha256(uint64_le(blob_epoch) || uint64_le(max_blobs)))[:4]
 * ```
 *
 * so BPO forks (same `fork_version`, different blob limit) produce distinct
 * digests. Beacon nodes emit this value as the 4-byte LCU-list context.
 *
 * @param chain_id chain to resolve
 * @param epoch epoch the digest is computed for (typically the attested-header epoch)
 * @param out receives the 4 fork-digest bytes on success (untouched on failure)
 * @return `true` on success, `false` if the chain or fork version cannot be resolved
 */
bool c4_eth_compute_fork_digest(chain_id_t chain_id, uint64_t epoch, uint8_t out[4]);

/**
 * Resolves a 4-byte wire `context` to a fork id.
 *
 * Compares `context` against `compute_fork_digest` at every scheduled
 * LCU-capable fork's activation epoch and at every CL `BLOB_SCHEDULE`
 * entry (BPO1, BPO2, ...). As a compatibility fallback, `context` is also
 * matched against the raw 4-byte `fork_version` of each fork -- this
 * catches legacy period-store `lcu.ssz` files that wrote `fork_version`
 * instead of `ForkDigest`.
 *
 * @param chain_id chain the wire data belongs to
 * @param context 4-byte context / fork-version bytes from the wire prefix
 * @return matching `fork_id_t`, or `C4_FORK_INVALID` if no scheduled fork matches
 */
fork_id_t c4_eth_fork_from_context(chain_id_t chain_id, const uint8_t context[4]);

/**
 * Validates the framing of a Beacon-API `light_client/updates` list and, for
 * each chunk, resolves the fork from the wire `context`, optionally runs
 * `ssz_is_valid` on the payload, and invokes `cb`.
 *
 * Framing rules (per Beacon API spec):
 * - Each chunk: `[uint64 LE length][4 B context][payload of length-4 bytes]`.
 * - `length >= 4` (must include the 4-byte context).
 * - Chunks tile the buffer with no gaps or overlaps and no tail garbage.
 * - Empty buffer (`data.len == 0`) is a valid empty list.
 *
 * When `validate_ssz` is `true`, each chunk's payload is checked with
 * `ssz_is_valid` (recursive) before `cb` runs, and the chunk's fork is
 * cross-checked against the fork implied by `attestedHeader.beacon.slot`.
 * A mismatch is rejected as if the framing were invalid.
 *
 * On successful **framing** (chunks tile the buffer) and `req != NULL`,
 * `req->validated` is set to `true` before Pass 2. That is the #356
 * contract: `validated` means the list structure is well-formed, not that
 * every payload passed `ssz_is_valid` or every callback returned true.
 * A later semantic failure still returns `false` and records an error.
 * Errors are appended to `state`.
 *
 * @param chain_id chain the wire data belongs to
 * @param data raw wire bytes; the walker never mutates or frees this buffer
 * @param state receives error messages (may be `NULL` to suppress)
 * @param req optional request whose `validated` flag is set on success
 * @param validate_ssz run `ssz_is_valid` + slot cross-check per chunk
 * @param cb per-chunk callback (may be `NULL` to only validate framing)
 * @param user opaque pointer forwarded to `cb`
 * @return `true` iff the whole list validates and no callback aborted
 */
bool c4_eth_walk_lcu_list(chain_id_t        chain_id,
                          bytes_t           data,
                          c4_state_t*       state,
                          data_request_t*   req,
                          bool              validate_ssz,
                          c4_lcu_chunk_cb_t cb,
                          void*             user);

/**
 * Decodes a `LightClientBootstrap` blob by detecting the fork from the
 * container size, then running `ssz_is_valid`.
 *
 * On success: `out->bytes = data`, `out->def` resolves to the correct
 * fork variant, and (when `req != NULL`) `req->validated = true`.
 * The `data` buffer is *not* copied -- `out->bytes.data` aliases it and
 * remains owned by the caller.
 *
 * @param chain_id chain the bootstrap belongs to (used for `ssz_is_valid` context)
 * @param data raw SSZ bytes of the bootstrap
 * @param state receives error messages (may be `NULL` to suppress)
 * @param req optional request whose `validated` flag is set on success
 * @param out receives the typed SSZ view on success
 * @return `true` on success, `false` if the size is unknown or SSZ validation fails
 */
bool c4_eth_decode_bootstrap(chain_id_t      chain_id,
                             bytes_t         data,
                             c4_state_t*     state,
                             data_request_t* req,
                             ssz_ob_t*       out);

#ifdef __cplusplus
}
#endif

#endif
