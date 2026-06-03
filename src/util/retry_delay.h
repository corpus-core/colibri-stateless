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

#ifndef retry_delay_h__
#define retry_delay_h__

#include "chains.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// :: Adaptive same-node retry delay

/**
 * Use-case category for the oblivious-node (`-32001 data non availability`)
 * delayed-retry loop. Pass this constant to `c4_retry_delay_for` and
 * `c4_retry_delay_observe`.
 *
 * Distinct categories learn independent base delays so a future use case
 * (e.g. waiting for a pending transaction receipt) can coexist without
 * one polluting the other's learner.
 *
 * **Lifetime contract**: category strings are kept by reference (not copied)
 * inside the in-memory cache. Only pass string literals or otherwise
 * statically-allocated strings here; a heap pointer that is later freed
 * would leave a dangling reference in the cache.
 */
#define C4_RETRY_CATEGORY_OBLIVIOUS "oblivious"

/**
 * Initial base delay (in milliseconds) used when no learned value exists yet.
 * 1s matches the empirically observed warm-up of the reference oblivious node.
 */
#define C4_RETRY_DELAY_DEFAULT_MS 1000U

/**
 * Floor for the learned base delay (ms). Prevents the asymmetric downward
 * probe from collapsing below realistic network-RTT territory.
 */
#define C4_RETRY_DELAY_MIN_MS 100U

/**
 * Cap for the learned base delay (ms) and for the exponential backoff. Bounds
 * the total wait so a single very slow node cannot stall the request for
 * arbitrarily long.
 */
#define C4_RETRY_DELAY_MAX_MS 8000U

/**
 * Computes the delay (in milliseconds) the host should wait before the next
 * same-node retry for the given (`category`, `chain`) pair.
 *
 * The delay is `min(base << retry_count, C4_RETRY_DELAY_MAX_MS)` where `base`
 * is loaded lazily from the storage plugin via the key
 * `rdelay_<category>_<chain_id>` and falls back to
 * `C4_RETRY_DELAY_DEFAULT_MS` if no persisted value exists (or the plugin is
 * not registered).
 *
 * The returned delay is always >= `C4_RETRY_DELAY_MIN_MS` (the clamp is applied
 * on load) and <= `C4_RETRY_DELAY_MAX_MS` (the shift saturates safely even for
 * very large `retry_count` values).
 *
 * **Side effects**: in FILE_STORAGE / MEMORY_STORAGE builds the first call
 * for an unseen `(category, chain)` pair may touch the storage plugin via
 * `c4_get_storage_config`, which auto-installs the compile-time default
 * backend when no host plugin is registered.
 *
 * **Threading**: not thread-safe. The learner uses module-level globals; if
 * multiple threads can drive retries concurrently the caller must serialize
 * access (see `src/util/AGENTS.md`).
 *
 * @param category    Use-case category (a `C4_RETRY_CATEGORY_*` string literal,
 *                    see lifetime contract on `C4_RETRY_CATEGORY_OBLIVIOUS`)
 * @param chain       Chain identifier (part of the persistence key)
 * @param retry_count Number of delayed retries already performed (typically
 *                    `data_request_t.retry_count` before scheduling the next)
 * @return Delay in milliseconds before the next retry
 */
uint32_t c4_retry_delay_for(const char* category, chain_id_t chain, uint16_t retry_count);

/**
 * Feeds a successful request outcome back into the learner.
 *
 * `retry_count == 0` means the very first request succeeded (the node was
 * already warm); the base is probed slightly downward. `retry_count > 0`
 * means delayed retries were needed; the cumulative scheduled wait at the
 * successful retry (`base * (2^retry_count - 1)`) is used as the warm-up
 * estimate and the base is moved towards it.
 *
 * Adaptation is intentionally asymmetric: it reacts fast when more delay is
 * needed (so a slow node converges quickly) and probes downward slowly (to
 * avoid the converse — gradually drifting into an extra retry per request).
 * The new base is clamped to `[C4_RETRY_DELAY_MIN_MS, C4_RETRY_DELAY_MAX_MS]`
 * and persisted via the storage plugin when one is registered.
 *
 * Only call this from a code path where the same-node retry loop for the
 * given category was active (e.g. for `C4_RETRY_CATEGORY_OBLIVIOUS`, only
 * when the request was routed to an oblivious node). Otherwise the learner
 * would be polluted by unrelated traffic.
 *
 * @param category    Same string previously passed to `c4_retry_delay_for`
 * @param chain       Chain identifier
 * @param retry_count Number of delayed retries that were performed before
 *                    the request finally succeeded
 */
void c4_retry_delay_observe(const char* category, chain_id_t chain, uint16_t retry_count);

/**
 * Drops all cached (`category`, `chain`) -> base entries from the in-memory
 * cache, forcing the next call to re-read from the storage plugin. Does not
 * touch persisted values. Intended for tests and process-reset scenarios.
 */
void c4_retry_delay_reset(void);

#ifdef __cplusplus
}
#endif

#endif
