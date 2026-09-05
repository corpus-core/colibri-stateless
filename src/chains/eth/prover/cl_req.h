/*
 * Copyright (c) 2025,2026 corpus.core
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

#ifndef cl_req_h__
#define cl_req_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "../util/bytes.h"
#include "../util/json.h"
#include "../util/ssz.h"
#include "../util/state.h"
#include "prover.h"

// Bitmask-based beacon client types for feature detection
#define BEACON_CLIENT_UNKNOWN    0x00000000
#define BEACON_CLIENT_NIMBUS     0x00000001
#define BEACON_CLIENT_LODESTAR   0x00000002
#define BEACON_CLIENT_PRYSM      0x00000004
#define BEACON_CLIENT_LIGHTHOUSE 0x00000008
#define BEACON_CLIENT_TEKU       0x00000010
#define BEACON_CLIENT_GRANDINE   0x00000020

#define BEACON_SUPPORTS_LIGHTCLIENT_UPDATE   (BEACON_CLIENT_NIMBUS | BEACON_CLIENT_LODESTAR)
#define BEACON_SUPPORTS_HISTORICAL_SUMMARIES (BEACON_CLIENT_NIMBUS | BEACON_CLIENT_LODESTAR)
#define BEACON_SUPPORTS_DEBUG_ENDPOINTS      (BEACON_CLIENT_NIMBUS | BEACON_CLIENT_LIGHTHOUSE)
#define BEACON_SUPPORTS_PARENT_ROOT_HEADERS  (BEACON_CLIENT_LODESTAR) // Nimbus: status-im/nimbus-eth2#7305
#define BEACON_SUPPORTS_STATE_PROOF          (BEACON_CLIENT_LODESTAR) // /eth/v0/beacon/proof/state/{state_id}

#ifndef DEFAULT_TTL
#define DEFAULT_TTL (3600 * 24) // 1 day
#endif

/**
 * `GET /eth/v1/beacon/states/head/finality_checkpoints`.
 *
 * On success `*result` is the inner `data` object (not the JSON-RPC envelope).
 *
 * @param ctx prover context
 * @param result receives `data.{current_justified,finalized}`
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`
 */
c4_status_t cl_get_finality_checkpoints(prover_ctx_t* ctx, json_t* result);

/**
 * `GET /eth/v1/beacon/headers?slot=`.
 *
 * A Nimbus/Lodestar empty-slot 404 becomes `JSON_TYPE_NOT_FOUND` instead of
 * `C4_ERROR`.
 *
 * @param ctx prover context
 * @param slot beacon slot to query
 * @param result receives the list envelope, or `JSON_TYPE_NOT_FOUND`
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`
 */
c4_status_t cl_get_headers_at_slot(prover_ctx_t* ctx, uint64_t slot, json_t* result);

/**
 * `GET /eth/v1/beacon/headers/{root}`.
 *
 * @param ctx prover context
 * @param root 32-byte beacon block root
 * @param result receives the full JSON envelope (`{data: {root, header, ...}}`)
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`
 */
c4_status_t cl_get_header_by_root(prover_ctx_t* ctx, bytes32_t root, json_t* result);

/**
 * `GET /eth/v1/beacon/headers?parent_root=` (Lodestar).
 *
 * @param ctx prover context
 * @param parent_root 32-byte parent block root
 * @param result receives the list envelope
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`
 */
c4_status_t cl_get_headers_by_parent_root(prover_ctx_t* ctx, bytes32_t parent_root, json_t* result);

/**
 * Header `message` from `GET /eth/v1/beacon/headers/{root}`.
 *
 * @param ctx prover context
 * @param root 32-byte beacon block root
 * @param message receives `data.header.message`
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`
 */
c4_status_t cl_get_header_message_by_root(prover_ctx_t* ctx, bytes32_t root, json_t* message);

/**
 * Historical summaries for a beacon state root (Nimbus or Lodestar URL).
 *
 * @param ctx prover context
 * @param state_root beacon state root
 * @param history_proof receives the JSON envelope (`historical_summaries` + `proof`)
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`
 */
c4_status_t cl_get_historical_summaries(prover_ctx_t* ctx, bytes_t state_root, json_t* history_proof);

/**
 * Fetch a `SignedBeaconBlock` as SSZ and validate it (`ssz_is_valid`).
 *
 * @param ctx prover context
 * @param root 32-byte beacon block root, or NULL / all-zero to use `slot`
 * @param slot used when `root` is missing; both missing fetches `head`
 * @param signed_block output: validated signed container (not unwrapped)
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`
 */
c4_status_t cl_fetch_signed_beacon_block(prover_ctx_t* ctx, const uint8_t* root, uint64_t slot, ssz_ob_t* signed_block);

/**
 * `GET /eth/v0/beacon/proof/state/{state_root}?format=0x...` (Lodestar).
 *
 * Fetches a `CompactMultiProof {leaves, descriptor}`. The echoed descriptor
 * is compared to the request descriptor before return; reconstruction against
 * `state_root` stays the caller's job (`c4_ssz_compact_to_branch` /
 * `c4_ssz_compact_multi_extract`).
 *
 * `leaves_out` and `descriptor_out` point into the request response; do not free them.
 *
 * @param ctx prover context
 * @param state_root 32-byte beacon state root
 * @param descriptor compact-multi-proof descriptor (`format` query)
 * @param leaves_out borrowed `leaves` list
 * @param descriptor_out borrowed echoed descriptor
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`
 */
c4_status_t cl_get_state_proof(prover_ctx_t* ctx,
                               bytes32_t     state_root,
                               bytes_t       descriptor,
                               ssz_ob_t*     leaves_out,
                               ssz_ob_t*     descriptor_out);

/**
 * Beacon SSZ GET without a validating wrapper.
 *
 * Prefer a `cl_*` helper. This remains public for light-client update lists
 * (`def == NULL`, no list-level SSZ type yet; see #356). When `def` is set,
 * `ssz_is_valid` runs once and `validated` is set.
 *
 * @param ctx prover context
 * @param path beacon API path
 * @param query optional query string; may be NULL
 * @param def SSZ type for the body, or NULL to skip validation
 * @param ttl cache TTL in seconds
 * @param result receives `{.def, .bytes}` pointing at the request response
 * @param req optional; receives the underlying `data_request_t*`
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`
 */
c4_status_t c4_send_beacon_ssz(prover_ctx_t* ctx, char* path, char* query, const ssz_def_t* def, uint32_t ttl, ssz_ob_t* result, data_request_t** req);

/**
 * Non-throwing Beacon SSZ GET. Does not set `ctx->state.error`.
 *
 * On `C4_SUCCESS` read `(*out_req)->response`. No SSZ def is attached and no
 * validation runs — the caller must validate (used for bootstrap 404 fallback).
 *
 * @param ctx prover context
 * @param path beacon API path
 * @param query optional query string; may be NULL
 * @param ttl cache TTL in seconds
 * @param out_req receives the `data_request_t*`; set on every return status
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR` (`out_req->error` on error)
 */
c4_status_t c4_send_beacon_ssz_no_throw(prover_ctx_t* ctx, char* path, char* query, uint32_t ttl, data_request_t** out_req);

/**
 * In-process server request (`C4_DATA_TYPE_INTERN`). No schema validation.
 *
 * Used for period-store / ZK / OP preconf bytes that are not untrusted RPC.
 *
 * @param ctx prover context
 * @param path internal handler path
 * @param query optional query string; may be NULL
 * @param ttl cache TTL in seconds
 * @param result receives a view of `req->response`; do not free
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`
 */
c4_status_t c4_send_internal_request(prover_ctx_t* ctx, char* path, char* query, uint32_t ttl, bytes_t* result);

/**
 * Non-throwing variant of `c4_send_internal_request`. Does not set
 * `ctx->state.error`. Used when a failed internal path should fall back
 * (e.g. `lcu_updates` → beacon).
 *
 * @param ctx prover context
 * @param path internal handler path
 * @param query optional query string; may be NULL
 * @param ttl cache TTL in seconds
 * @param out_req receives the `data_request_t*`; set on every return status
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR` (`out_req->error` on error)
 */
c4_status_t c4_send_internal_request_no_throw(prover_ctx_t* ctx, char* path, char* query, uint32_t ttl, data_request_t** out_req);

#ifdef __cplusplus
}
#endif
#endif
