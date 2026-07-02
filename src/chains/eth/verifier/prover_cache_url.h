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

#ifndef eth_prover_cache_url_h__
#define eth_prover_cache_url_h__

#include "bytes.h"
#include "chains.h"
#include "json.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Builds the cache-friendly GET path for a delegated proof request:
 * `proof/<method>/<block>/<version>/<zk|std>/<c4>[?signers=0x..]`.
 *
 * - `<block>` is the raw JSON block identifier without quotes (tag or `0x...`); any additional
 *   arguments (e.g. the `includeTx` boolean of `eth_getBlockByNumber`) are intentionally omitted
 *   because they do not affect the proof.
 * - `<zk|std>` reflects whether the caller requested a ZK proof.
 * - `<c4>` is the hex-encoded `client_state` (or `0x` when empty).
 * - Optional `?signers=0x...` carries the witness/signer keys.
 *
 * The returned string is heap-allocated; ownership is transferred to the caller (typically
 * assigned to `data_request_t.url` and released via `c4_request_free`).
 *
 * @param method the delegated RPC method (e.g. `eth_getBlockHeader`, `eth_getBlockByNumber`, `eth_getBlockReceipts`)
 * @param block the JSON block identifier (tag or `0x...` hex)
 * @param version the client version number to embed
 * @param zk_proof whether the ZK proof variant should be used (path segment `zk` vs `std`)
 * @param client_state the client_state snapshot (may be empty)
 * @param witness_key the witness/signer keys (may be empty)
 * @return heap-allocated URL path
 */
char* c4_eth_build_delegated_block_get_url(const char* method, json_t block, uint32_t version,
                                           bool zk_proof, bytes_t client_state, bytes_t witness_key);

/**
 * Maps a block identifier to a client-side cache freshness bound in seconds (used as the
 * `Cache-Control: max-age=<n>` request header for shared caches/CDNs).
 *
 * Concrete block numbers/hashes are immutable and return 0 (no bound). Textual tags (`latest`,
 * `safe`, `justified`, `finalized`) return a chain-aware bound derived from `block_time` and
 * `slots_per_epoch`, mirroring the prover-side TTL used for the internal header cache.
 *
 * @param chain_id the chain id (used for `block_time` and `slots_per_epoch`)
 * @param block the JSON block identifier
 * @param light_client if true, the `latest` tag uses the full block time (else block_time/2)
 * @return TTL in seconds, or 0 for concrete blocks
 */
uint32_t c4_eth_block_ttl_s(chain_id_t chain_id, json_t block, bool light_client);

/**
 * Chain-module implementation of `c4_get_prover_cache_request` for Ethereum.
 *
 * Handles the cacheable delegated methods:
 *  - `eth_getBlockHeader`, `eth_getBlockByNumber`, `eth_getBlockReceipts`:
 *    URL derived from `params[0]` (block identifier), `ttl` from `c4_eth_block_ttl_s`.
 *
 * All other RPC methods return false so callers fall back to the POST + JSON payload path.
 * See `c4_get_prover_cache_request` in verify.h for full semantics.
 */
bool c4_eth_get_prover_cache_request(chain_id_t chain_id, const char* method, json_t params,
                                     uint32_t version, bool zk_proof, bool light_client,
                                     bytes_t client_state, bytes_t witness_key,
                                     char** url_out, uint32_t* ttl_out);

#ifdef __cplusplus
}
#endif

#endif /* eth_prover_cache_url_h__ */
