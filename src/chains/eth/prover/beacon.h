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

#ifndef C4_BEACON_H
#define C4_BEACON_H

#include "../util/json.h"
#include "../util/ssz.h"
#include "prover.h"

// Bitmask-based beacon client types for feature detection
#define BEACON_CLIENT_UNKNOWN    0x00000000 // No specific client requirement
#define BEACON_CLIENT_NIMBUS     0x00000001 // (1 << 0)
#define BEACON_CLIENT_LODESTAR   0x00000002 // (1 << 1)
#define BEACON_CLIENT_PRYSM      0x00000004 // (1 << 2)
#define BEACON_CLIENT_LIGHTHOUSE 0x00000008 // (1 << 3)
#define BEACON_CLIENT_TEKU       0x00000010 // (1 << 4)
#define BEACON_CLIENT_GRANDINE   0x00000020 // (1 << 5)
// Add more clients as needed...

// Feature-based client combinations
#define BEACON_SUPPORTS_LIGHTCLIENT_UPDATE   (BEACON_CLIENT_NIMBUS | BEACON_CLIENT_LODESTAR)
#define BEACON_SUPPORTS_HISTORICAL_SUMMARIES (BEACON_CLIENT_NIMBUS | BEACON_CLIENT_LODESTAR)
#define BEACON_SUPPORTS_PARENT_ROOT_HEADERS  (BEACON_CLIENT_LODESTAR)
#define BEACON_SUPPORTS_DEBUG_ENDPOINTS      (BEACON_CLIENT_NIMBUS | BEACON_CLIENT_LIGHTHOUSE)

#ifdef __cplusplus
extern "C" {
#endif

#define FINALITY_KEY "FinalityRoots"
#define DEFAULT_TTL  (3600 * 24) // 1 day
// beacon block including the relevant parts for the proof

typedef struct {
  uint64_t  slot; // slot of the block
  bytes32_t root; // root of the block
} beacon_head_t;

typedef struct {
  uint64_t  slot;             // slot of the block
  ssz_ob_t  header;           // block header
  ssz_ob_t  execution;        // execution payload of the block
  ssz_ob_t  body;             // body of the block
  ssz_ob_t  sync_aggregate;   // sync aggregate with the signature of the block
  bytes32_t sign_parent_root; // the parentRoot of the block containing the signature
  bytes32_t data_block_root;  // the blockroot used for the data block
} beacon_block_t;

// get the beacon block for the given eth block number or hash
c4_status_t c4_eth_get_signblock_and_parent(prover_ctx_t* ctx, bytes32_t sig_root, bytes32_t data_root, ssz_ob_t* sig_block, ssz_ob_t* data_block, bytes32_t data_root_result);
c4_status_t c4_beacon_get_block_for_eth(prover_ctx_t* ctx, json_t block, beacon_block_t* beacon_block);

// creates a new header with the body_root passed and returns the ssz_builder_t, which must be freed
ssz_builder_t c4_proof_add_header(ssz_ob_t header, bytes32_t body_root);

c4_status_t c4_send_beacon_json(prover_ctx_t* ctx, char* path, char* query, uint32_t ttl, json_t* result);
c4_status_t c4_send_beacon_ssz(prover_ctx_t* ctx, char* path, char* query, const ssz_def_t* def, uint32_t ttl, ssz_ob_t* result);
c4_status_t c4_send_beacon_json_with_client_type(prover_ctx_t* ctx, char* path, char* query, uint32_t ttl, json_t* result, uint32_t client_type);
c4_status_t c4_send_beacon_ssz_with_client_type(prover_ctx_t* ctx, char* path, char* query, const ssz_def_t* def, uint32_t ttl, ssz_ob_t* result, uint32_t client_type);
c4_status_t c4_send_internal_request(prover_ctx_t* ctx, char* path, char* query, uint32_t ttl, bytes_t* result);
#ifdef PROVER_CACHE
c4_status_t c4_set_latest_block(prover_ctx_t* ctx, uint64_t latest_block_number);
c4_status_t c4_eth_update_finality(prover_ctx_t* ctx);

/*
 *  Updates the beacon block data in the cache.
 *
 *  This uses the following keys in the cache:
 *  - B<beacon_block_root> -> beacon_block_t
 *  - Slatest -> beacon_head_t
 *  - S<exec_block_hash> -> beacon_head_t
 *  - S<exec_block_number> -> beacon_head_t
 *
 *  @param ctx The context of the prover
 *  @param beacon_block The beacon block to update
 *  @param latest_timestamp The latest timestamp of the block
 *  @param block_root The root of the block
 */
void c4_beacon_cache_update_blockdata(prover_ctx_t* ctx, beacon_block_t* beacon_block, uint64_t latest_timestamp, bytes32_t block_root);

#endif

#ifdef __cplusplus
}
#endif

#endif