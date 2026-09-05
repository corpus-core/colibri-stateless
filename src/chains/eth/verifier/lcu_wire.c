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

#include "lcu_wire.h"

#include "beacon_types.h"
#include "bytes.h"
#include "crypto.h"
#include "logger.h"
#include "ssz.h"
#include "state.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// Local mirror of the `ForkData` SSZ container used for fork-digest
// computation. Kept file-local because `FORK_DATA_CONTAINER` in
// `beacon_header.c` is `static` and we do not want to widen its scope
// (the digest is only 4 bytes, hashing is cheap, and keeping a private
// container avoids surprising other callers).
static const ssz_def_t LCU_WIRE_FORK_DATA[] = {
    SSZ_BYTE_VECTOR("version", 4),
    SSZ_BYTES32("state")};

static const ssz_def_t LCU_WIRE_FORK_DATA_CONTAINER =
    SSZ_CONTAINER("ForkData", LCU_WIRE_FORK_DATA);

// LCU-capable forks in ascending order. Kept as a private table so
// `fork_from_context` scans exactly the forks the SSZ union supports
// (mirrors `eth_get_light_client_update`).
static const fork_id_t LCU_WIRE_CANDIDATE_FORKS[] = {
    C4_FORK_DENEB,
    C4_FORK_ELECTRA,
    C4_FORK_FULU,
    C4_FORK_GLOAS};

// Loads the 4-byte `fork_version` for `(chain_id, fork)` into `out`.
// Returns false if the chain spec is unknown.
static bool load_fork_version(chain_id_t chain_id, fork_id_t fork, uint8_t out[4]) {
  const chain_spec_t* spec = c4_eth_get_chain_spec(chain_id);
  if (!spec || !spec->fork_version_func) return false;
  spec->fork_version_func(chain_id, fork, out);
  return true;
}

// `get_blob_parameters(epoch)` from the Fulu spec. `cl_blob_schedule` is
// ASCENDING; the loop keeps the last entry with `entry.epoch <= epoch`
// (equivalent to the spec's reverse-sorted first-match). If none apply,
// falls back to `(ELECTRA_FORK_EPOCH, electra_max_blobs_per_block)`.
static void get_blob_parameters(chain_id_t chain_id, const chain_spec_t* spec, uint64_t epoch,
                                uint64_t* out_epoch, uint64_t* out_max_blobs) {
  *out_epoch     = c4_chain_fork_epoch(chain_id, C4_FORK_ELECTRA);
  *out_max_blobs = (spec && spec->electra_max_blobs_per_block) ? spec->electra_max_blobs_per_block : 9;
  if (!spec || !spec->cl_blob_schedule) return;
  for (const eth_cl_blob_schedule_t* e = spec->cl_blob_schedule; e->epoch || e->max_blobs_per_block; e++) {
    if (epoch >= e->epoch) {
      *out_epoch     = e->epoch;
      *out_max_blobs = e->max_blobs_per_block;
    }
  }
}

bool c4_eth_compute_fork_digest(chain_id_t chain_id, uint64_t epoch, uint8_t out[4]) {
  if (!out) return false;
  const chain_spec_t* spec = c4_eth_get_chain_spec(chain_id);
  if (!spec) return false;

  fork_id_t fork = c4_chain_fork_id(chain_id, epoch);
  uint8_t   buffer[36] = {0};
  if (!load_fork_version(chain_id, fork, buffer)) return false;
  if (!c4_chain_genesis_validators_root(chain_id, buffer + 4)) return false;

  bytes32_t base = {0};
  ssz_hash_tree_root(ssz_ob(LCU_WIRE_FORK_DATA_CONTAINER, bytes(buffer, 36)), base);

  uint64_t fulu_epoch = c4_chain_fork_epoch(chain_id, C4_FORK_FULU);
  if (fulu_epoch == UINT64_MAX || epoch < fulu_epoch) {
    memcpy(out, base, 4);
    return true;
  }

  // Fulu / EIP-7892: xor the 32-byte base digest with sha256(epoch || max_blobs)
  // and take the first 4 bytes.
  uint64_t blob_epoch = 0;
  uint64_t max_blobs  = 0;
  get_blob_parameters(chain_id, spec, epoch, &blob_epoch, &max_blobs);
  uint8_t mix[16] = {0};
  uint64_to_le(mix, blob_epoch);
  uint64_to_le(mix + 8, max_blobs);
  bytes32_t hashed = {0};
  sha256(bytes(mix, 16), hashed);
  for (int i = 0; i < 4; i++) out[i] = (uint8_t) (base[i] ^ hashed[i]);
  return true;
}

fork_id_t c4_eth_fork_from_context(chain_id_t chain_id, const uint8_t context[4]) {
  if (!context) return C4_FORK_INVALID;
  const chain_spec_t* spec = c4_eth_get_chain_spec(chain_id);
  if (!spec) return C4_FORK_INVALID;

  // 1) Spec-correct path: match against the digest at every scheduled
  //    LCU-capable fork's activation epoch.
  for (size_t i = 0; i < sizeof(LCU_WIRE_CANDIDATE_FORKS) / sizeof(LCU_WIRE_CANDIDATE_FORKS[0]); i++) {
    fork_id_t fork = LCU_WIRE_CANDIDATE_FORKS[i];
    if (!c4_chain_schedules_fork(chain_id, fork)) continue;
    uint64_t epoch = c4_chain_fork_epoch(chain_id, fork);
    if (epoch == UINT64_MAX) continue;
    uint8_t digest[4] = {0};
    if (!c4_eth_compute_fork_digest(chain_id, epoch, digest)) continue;
    if (memcmp(digest, context, 4) == 0) return fork;
  }

  // 2) BPO entries share a fork_version with Fulu (or later) but produce a
  //    different digest. Match those too; the fork id follows the entry's epoch.
  if (spec->cl_blob_schedule) {
    for (const eth_cl_blob_schedule_t* e = spec->cl_blob_schedule; e->epoch || e->max_blobs_per_block; e++) {
      uint8_t digest[4] = {0};
      if (!c4_eth_compute_fork_digest(chain_id, e->epoch, digest)) continue;
      if (memcmp(digest, context, 4) == 0) return c4_chain_fork_id(chain_id, e->epoch);
    }
  }

  // 3) Compatibility fallback: match against raw `fork_version`. This keeps
  //    period-store `lcu.ssz` files written by older builds working.
  for (size_t i = 0; i < sizeof(LCU_WIRE_CANDIDATE_FORKS) / sizeof(LCU_WIRE_CANDIDATE_FORKS[0]); i++) {
    fork_id_t fork = LCU_WIRE_CANDIDATE_FORKS[i];
    if (!c4_chain_schedules_fork(chain_id, fork)) continue;
    uint8_t version[4] = {0};
    if (!load_fork_version(chain_id, fork, version)) continue;
    if (memcmp(version, context, 4) == 0) return fork;
  }

  return C4_FORK_INVALID;
}

// Reads a chunk header at `pos`. Returns false and (optionally) records an
// error if the header does not fit or `length` is out of bounds. On success
// writes the total chunk length (including the 8-byte length prefix) into
// `*out_chunk_len` and the payload span into `*out_payload`.
static bool read_chunk(bytes_t     data,
                       uint32_t    pos,
                       uint32_t    idx,
                       c4_state_t* state,
                       uint32_t*   out_chunk_len,
                       bytes_t*    out_payload,
                       const uint8_t** out_context) {
  // 64-bit compare: `pos + 12` as uint32 wraps near UINT32_MAX.
  if ((uint64_t) pos + C4_LCU_WIRE_PREFIX_SIZE > (uint64_t) data.len) {
    if (state) c4_state_add_error(state, "LCU list: truncated chunk header");
    return false;
  }
  uint64_t length = uint64_from_le(data.data + pos);
  // `length` counts the 4-byte context plus the SSZ payload.
  if (length < C4_LCU_WIRE_CONTEXT_SIZE) {
    if (state) c4_state_add_error(state, "LCU list: chunk length below context size");
    return false;
  }
  // Compare against remaining bytes after the 8-byte length prefix.
  // Do NOT compute `pos + 8 + length` in uint64: a hostile `length` near
  // UINT64_MAX wraps that sum and would pass a `chunk_end > data.len` check,
  // then yield `chunk_len == 0` (infinite walk) or a huge truncated payload.
  uint64_t remaining = (uint64_t) data.len - (uint64_t) pos - C4_LCU_WIRE_LENGTH_SIZE;
  if (length > remaining) {
    if (state) c4_state_add_error(state, "LCU list: chunk length exceeds buffer");
    return false;
  }

  *out_chunk_len = (uint32_t) (C4_LCU_WIRE_LENGTH_SIZE + length);
  *out_payload   = bytes(data.data + pos + C4_LCU_WIRE_PREFIX_SIZE,
                         (uint32_t) (length - C4_LCU_WIRE_CONTEXT_SIZE));
  *out_context   = data.data + pos + C4_LCU_WIRE_LENGTH_SIZE;
  (void) idx;
  return true;
}

// Framing-only walk. Verifies that every chunk header fits, `length >= 4`,
// and chunks tile `data` end-to-end (no gaps, no tail). Does NOT touch the
// payload; SSZ / fork checks are performed in a second pass.
static bool validate_framing(bytes_t data, c4_state_t* state, uint32_t* out_count) {
  uint32_t pos   = 0;
  uint32_t count = 0;
  while (pos < data.len) {
    uint32_t       chunk_len = 0;
    bytes_t        payload   = NULL_BYTES;
    const uint8_t* context   = NULL;
    if (!read_chunk(data, pos, count, state, &chunk_len, &payload, &context))
      return false;
    pos += chunk_len;
    count++;
  }
  // Loop invariant `pos <= data.len` is guaranteed by `read_chunk`; if we
  // exit the loop `pos == data.len`, so there is no explicit tail check.
  if (out_count) *out_count = count;
  return true;
}

bool c4_eth_walk_lcu_list(chain_id_t        chain_id,
                          bytes_t           data,
                          c4_state_t*       state,
                          data_request_t*   req,
                          bool              validate_ssz,
                          c4_lcu_chunk_cb_t cb,
                          void*             user) {
  // Empty list is a valid framing.
  if (data.len == 0) {
    if (req) req->validated = true;
    return true;
  }
  if (!data.data) {
    if (state) c4_state_add_error(state, "LCU list: NULL buffer with non-zero length");
    return false;
  }

  // Pass 1: framing only. This is what earns the request its `validated`
  // flag -- the acceptance criterion in issue #356 explicitly ties
  // `validated` to structural framing, not to per-chunk SSZ.
  uint32_t count = 0;
  if (!validate_framing(data, state, &count)) return false;
  if (req) req->validated = true;

  // Pass 2: fork resolution, optional SSZ + slot cross-check, callback.
  uint32_t pos = 0;
  uint32_t idx = 0;
  while (pos < data.len) {
    uint32_t       chunk_len = 0;
    bytes_t        payload   = NULL_BYTES;
    const uint8_t* context   = NULL;
    // Framing already validated -- this cannot fail unless memory changed
    // under us; keep the check as defense-in-depth.
    if (!read_chunk(data, pos, idx, state, &chunk_len, &payload, &context))
      return false;

    fork_id_t fork = c4_eth_fork_from_context(chain_id, context);
    if (fork == C4_FORK_INVALID) {
      if (state) c4_state_add_error(state, "LCU chunk: unknown fork context (digest/version mismatch)");
      return false;
    }

    const ssz_def_t* def = eth_get_light_client_update(fork);
    if (!def) {
      if (state) c4_state_add_error(state, "LCU chunk: no SSZ def for resolved fork");
      return false;
    }

    ssz_ob_t update = {.bytes = payload, .def = def};

    if (validate_ssz) {
      if (!ssz_is_valid(update, true, state)) {
        if (state) c4_state_add_error(state, "LCU chunk: invalid SSZ payload");
        return false;
      }
      // Slot cross-check: the fork implied by `attestedHeader.beacon.slot`
      // must match the context-derived fork. Signature-slot is intentionally
      // *not* cross-checked here -- per the light-client spec the fork used
      // for `sync_aggregate` verification can differ from the digest fork
      // (which follows the attested header).
      ssz_ob_t            attested = ssz_get(&update, "attestedHeader");
      ssz_ob_t            beacon   = ssz_get(&attested, "beacon");
      uint64_t            slot     = ssz_get_uint64(&beacon, "slot");
      const chain_spec_t* spec     = c4_eth_get_chain_spec(chain_id);
      fork_id_t           slot_fork = c4_chain_fork_id(chain_id, epoch_for_slot(slot, spec));
      if (slot_fork != fork) {
        if (state) c4_state_add_error(state, "LCU chunk: attested-header slot fork disagrees with context digest");
        return false;
      }
    }

    if (cb) {
      c4_lcu_chunk_t chunk = {
          .index   = idx,
          .fork    = fork,
          .update  = update,
          .context = context,
      };
      if (!cb(user, &chunk)) return false;
    }

    pos += chunk_len;
    idx++;
  }

  (void) count;
  return true;
}

// Resolves the bootstrap fork from the wire layout:
//   - Gloas has no variable field, so we match on the total size.
//   - Deneb / Electra / Fulu have a leading `header` offset (variable field)
//     whose value equals the container's fixed portion -- 24788 for Deneb
//     and 24820 for Electra/Fulu. We match on that offset instead of the
//     total size, which depends on `execution.extraData` and is not fixed.
static fork_id_t detect_bootstrap_fork(bytes_t data) {
  if (data.len == C4_ETH_GLOAS_BOOTSTRAP_SIZE) return C4_FORK_GLOAS;
  if (data.len < C4_ETH_DENEB_BOOTSTRAP_FIXED_SIZE) return C4_FORK_INVALID;
  uint32_t hdr_offset = uint32_from_le(data.data);
  // The header offset must lie strictly inside the buffer.
  if (hdr_offset >= data.len) return C4_FORK_INVALID;
  if (hdr_offset == C4_ETH_DENEB_BOOTSTRAP_FIXED_SIZE) return C4_FORK_DENEB;
  if (hdr_offset == C4_ETH_ELECTRA_BOOTSTRAP_FIXED_SIZE) return C4_FORK_ELECTRA;
  return C4_FORK_INVALID;
}

bool c4_eth_decode_bootstrap(chain_id_t      chain_id,
                             bytes_t         data,
                             c4_state_t*     state,
                             data_request_t* req,
                             ssz_ob_t*       out) {
  if (!out) return false;
  if (data.len > 0 && !data.data) {
    if (state) c4_state_add_error(state, "Bootstrap: NULL buffer with non-zero length");
    return false;
  }
  fork_id_t fork = detect_bootstrap_fork(data);
  if (fork == C4_FORK_INVALID) {
    if (state) c4_state_add_error(state, "Bootstrap: cannot resolve fork from wire layout");
    return false;
  }

  // Defense-in-depth: reject a chain that has not scheduled the fork implied
  // by the size / layout. This prevents e.g. accepting a Gloas-sized blob on
  // a chain that has not yet activated Gloas. Only meaningful for Electra
  // and later -- Deneb is unconditionally present on all supported chains.
  if (fork >= C4_FORK_ELECTRA && !c4_chain_schedules_fork(chain_id, fork)) {
    if (state) c4_state_add_error(state, "Bootstrap: fork implied by wire layout is not scheduled on chain");
    return false;
  }

  const ssz_def_t* def = eth_get_light_client_bootstrap(fork);
  if (!def) {
    if (state) c4_state_add_error(state, "Bootstrap: no SSZ def for resolved fork");
    return false;
  }

  ssz_ob_t view = {.bytes = data, .def = def};
  if (!(req && req->validated)) {
    if (!ssz_is_valid(view, true, state)) {
      if (state) c4_state_add_error(state, "Bootstrap: invalid SSZ structure");
      return false;
    }
    if (req) req->validated = true;
  }

  *out = view;
  return true;
}
