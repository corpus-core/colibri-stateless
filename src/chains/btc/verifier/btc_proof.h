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

#ifndef btc_proof_h__
#define btc_proof_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "btc_target.h"
#include "ssz.h"

/**
 * Applies the epoch path carried in Bitcoin `sync_data`.
 *
 * `sync_data` is the unwrapped `sync_data` field of `C4Request`, the same object
 * `c4_verify_init` stores on the verification context. `NONE` leaves `cache` unchanged.
 * `BtcEpochPath` checks that each step's `final_headers` extend that step's header,
 * then authorizes the headers with `btc_target_accept_path`. An empty finality list is valid.
 * On failure `cache` is unchanged. The block or transaction witness stays in `proof` and is
 * checked only after this update.
 *
 * @param sync_data unwrapped sync-data object from the request
 * @param cache verified epoch targets
 * @param limits jump bounds, or NULL for the defaults
 * @return true when sync data was absent or every step was accepted
 */
bool btc_sync_accept_epoch_path(ssz_ob_t sync_data, btc_target_cache_t* cache, const btc_target_limits_t* limits);

#ifdef __cplusplus
}
#endif

#endif
