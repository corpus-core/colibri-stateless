/*
 * Copyright 2025,2026 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef arb_preconf_h__
#define arb_preconf_h__

#include "bytes.h"
#include "json.h"

/**
 * Builds an `arbFeedProof` container from one RPC response and the feed tail.
 *
 * `rpc_json` is a JSON-RPC object. `result` is either the hex string returned by
 * `debug_getRawHeader` or the block object returned by `eth_getBlockByHash`.
 * A block object is RLP-encoded here. The header is kept only when `keccak256`
 * of those bytes equals `block_hash`. `signed_tail` is stored uncompressed
 * (union index 1 of `ETH_SEQUENCER_PAYLOAD_UNION`).
 *
 * @param rpc_json parsed JSON-RPC response
 * @param block_hash expected L2 block hash (32 bytes)
 * @param signature sequencer signature (65 bytes)
 * @param signed_tail Nitro digest suffix after `blockHash`
 * @param err receives an allocated message when the result is empty; may be NULL
 * @return owned SSZ bytes, or empty bytes on failure
 */
bytes_t arb_preconf_encode(json_t rpc_json, bytes_t block_hash, bytes_t signature, bytes_t signed_tail, char** err);

#endif
