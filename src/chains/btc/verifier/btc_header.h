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

#ifndef btc_header_h__
#define btc_header_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "btc_target.h"
#include "verify.h"

/** Following headers required before a block is treated as final. */
#define BTC_FINALITY 6u
/** First mainnet height whose coinbase must encode the block height. */
#define BTC_BIP34_START 227836u

/**
 * Reports whether `getblockheader` asked for the raw 80-byte header.
 *
 * Bitcoin Core defaults the optional verbosity to `true`, which is the JSON object. Only an
 * explicit `false` or `0` selects the hex header this proof can return.
 *
 * @param params RPC parameter array
 * @return true when the call is exactly a block hash plus raw verbosity
 */
static inline bool btc_header_raw_requested(json_t params) {
  json_t verbose;
  if (json_len(params) != 2) return false;
  verbose = json_at(params, 1);
  if (verbose.type == JSON_TYPE_BOOLEAN) return !json_as_bool(verbose);
  if (verbose.type == JSON_TYPE_NUMBER) return json_as_uint64(verbose) == 0;
  return false;
}

/**
 * Verifies a `getblockheader` proof and caches the accepted targets.
 *
 * `ctx->proof` is the unwrapped `BtcHeaderProof`. The header must meet proof-of-work and the
 * cached difficulty, and at least `BTC_FINALITY` following headers must extend it. The coinbase
 * merkle proof binds `height`, except for the compiled checkpoint header, whose hash already
 * fixes that height. On success `ctx->data` is the raw 80-byte header and `cache` includes every
 * accepted target. On failure `cache` is unchanged.
 *
 * Omitted verbosity and the JSON object are rejected. Bitcoin Core's default is the object, and
 * fields such as median time and chainwork are not carried by this proof. Heights below
 * `BTC_BIP34_START` are rejected so an unbound height cannot be written into the target cache.
 *
 * @param ctx verification context
 * @param cache verified epoch targets
 * @return true when the header proof was accepted
 */
bool btc_verify_header_proof(verify_ctx_t* ctx, btc_target_cache_t* cache);

#ifdef __cplusplus
}
#endif

#endif
