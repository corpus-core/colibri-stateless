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

#ifndef btc_target_h__
#define btc_target_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "bytes.h"
#include "chains.h"

/** Blocks per Bitcoin difficulty epoch. */
#define BTC_EPOCH_LENGTH 2016u
/** Expected epoch duration, in seconds (2016 * 10 minutes). */
#define BTC_TARGET_TIMESPAN 1209600u
/** Default number of epochs a proof may jump without an intermediate step. */
#define BTC_TARGET_MAX_DAPS 20u
/** Default bound, in percent, on how much easier a multi-epoch jump may be. */
#define BTC_TARGET_MAX_PERCENT 10u
/** Maximum epoch steps carried in one proof. */
#define BTC_TARGET_PATH_LIMIT 40u
/** Maximum cached epoch targets, including the compiled checkpoint. */
#define BTC_TARGET_CACHE_MAX 512u
/** Mainnet compact pow-limit (`nBits` of the genesis block). */
#define BTC_MAINNET_POW_BITS 0x1d00ffffu
/**
 * Compiled mainnet anchor, block 840000.
 *
 * The header hash is `0000000000000000000320283a032748cef8227873ff4872689bf23f1cda83a5`.
 * Its difficulty epoch is the trust anchor for the target cache. A header that meets its own
 * proof-of-work is still not on the main chain until its `nBits` is authorized from this anchor.
 */
#define BTC_MAINNET_CHECKPOINT_HEIGHT 840000u

/**
 * Difficulty-epoch number of a block height.
 *
 * @param height block height
 * @return `height / BTC_EPOCH_LENGTH`
 */
uint32_t btc_dap(uint32_t height);

/**
 * Returns the compiled mainnet checkpoint header.
 *
 * The 80 bytes have static lifetime.
 *
 * @return the checkpoint header
 */
bytes_t btc_mainnet_checkpoint_header(void);

/**
 * Computes the next epoch compact target.
 *
 * `timespan` is `last_time - first_time`, clamped to `[timespan/4, timespan*4]`, where the nominal
 * timespan is `BTC_TARGET_TIMESPAN`. The result is `old * clamped / BTC_TARGET_TIMESPAN`, saturated
 * at `pow_limit`. This is the consensus retarget. It is meaningful only when `first_time` and
 * `last_time` are the timestamps of the first and last blocks of one epoch on a chain already tied
 * to the checkpoint. Two headers by themselves do not prove that those timestamps are 2015 blocks apart.
 *
 * @param old_bits compact target of the epoch that just finished
 * @param first_time timestamp of the first block of that epoch
 * @param last_time timestamp of the last block of that epoch
 * @param pow_limit 32-byte little-endian network pow-limit
 * @param new_bits receives the compact target of the next epoch
 * @return true when `new_bits` was written
 */
bool btc_target_retarget(uint32_t old_bits, uint32_t first_time, uint32_t last_time, const uint8_t pow_limit[32], uint32_t* new_bits);

/**
 * Limits for accepting a target that is not already cached.
 *
 * `max_daps` is the farthest epoch distance a single step may cover. `max_percent` limits how much
 * easier the target may become on a jump of more than one epoch. A one-epoch step uses the consensus
 * factor-of-four clamp instead of `max_percent`. A harder target is accepted inside `max_daps`.
 */
typedef struct {
  uint32_t max_daps;
  uint32_t max_percent;
} btc_target_limits_t;

/**
 * Fills `limits` with `BTC_TARGET_MAX_DAPS` and `BTC_TARGET_MAX_PERCENT`.
 *
 * @param limits receives the defaults
 */
void btc_target_limits_default(btc_target_limits_t* limits);

/**
 * One verified epoch target.
 *
 * `bits` is the compact target shared by every block in `dap`.
 */
typedef struct {
  uint32_t dap;
  uint32_t bits;
} btc_target_entry_t;

/**
 * Verified epoch targets.
 *
 * The compiled checkpoint epoch is always present after `btc_target_cache_init` and after a successful
 * decode. Entries are ordered by `dap`.
 */
typedef struct {
  btc_target_entry_t entries[BTC_TARGET_CACHE_MAX];
  uint32_t           count;
} btc_target_cache_t;

/**
 * Seeds `cache` with the compiled mainnet checkpoint.
 *
 * @param cache receives one checkpoint entry
 */
void btc_target_cache_init(btc_target_cache_t* cache);

/**
 * Finds the cached epoch nearest to `dap`.
 *
 * When two epochs are equally near, the harder decoded target wins. A compact value that does not
 * decode loses to one that does. If the targets are equal, the lower epoch number wins.
 *
 * @param cache verified targets
 * @param dap epoch to look up
 * @param bits receives the cached compact target; may be NULL
 * @param found_dap receives the epoch that was selected; may be NULL
 * @return true when the cache contains at least one entry
 */
bool btc_target_cache_find(const btc_target_cache_t* cache, uint32_t dap, uint32_t* bits, uint32_t* found_dap);

/**
 * Inserts or replaces the compact target of `dap`.
 *
 * The compiled checkpoint epoch keeps its compiled `nBits`. A different value for that epoch is rejected
 * and the cache is left unchanged.
 *
 * @param cache verified targets
 * @param dap epoch number
 * @param bits compact target
 * @return false when `cache` is NULL, the checkpoint bits differ from the compiled value, or a new epoch would exceed `BTC_TARGET_CACHE_MAX`
 */
bool btc_target_cache_put(btc_target_cache_t* cache, uint32_t dap, uint32_t bits);

/**
 * Serializes the cache.
 *
 * The layout is little-endian: 4-byte magic `BTC1`, entry count, then `count` pairs of `dap` and `bits`.
 *
 * @param cache verified targets
 * @param out receives the blob; the previous contents are freed
 * @return true when `out` holds the blob
 */
bool btc_target_cache_encode(const btc_target_cache_t* cache, buffer_t* out);

/**
 * Parses a cache blob.
 *
 * The checkpoint epoch must be present with its compiled compact target. On failure `cache` is left
 * unchanged.
 *
 * @param blob bytes from `btc_target_cache_encode` or from storage
 * @param cache receives the entries
 * @return true when the blob is well formed and keeps the checkpoint
 */
bool btc_target_cache_decode(bytes_t blob, btc_target_cache_t* cache);

/**
 * Loads the persisted cache for `chain_id`.
 *
 * The storage key is `btc_target_<chain_id>`. A missing blob seeds the checkpoint and returns false.
 * A corrupt blob is rejected, `cache` is seeded from the checkpoint, and the function returns false.
 *
 * @param chain_id chain whose cache is loaded
 * @param cache receives the cache
 * @return true when a stored blob was accepted
 */
bool btc_target_cache_load(chain_id_t chain_id, btc_target_cache_t* cache);

/**
 * Writes the cache for `chain_id`.
 *
 * @param chain_id chain whose cache is stored
 * @param cache verified targets
 * @return true when the storage backend accepted the blob
 */
bool btc_target_cache_store(chain_id_t chain_id, const btc_target_cache_t* cache);

/**
 * Decides whether `bits` at `height` is allowed by the cache.
 *
 * The cache is not modified. `limits` NULL selects the defaults. `max_percent` above 1000000 is rejected.
 *
 * @param cache verified targets
 * @param limits jump bounds, or NULL for the defaults
 * @param height block height whose epoch is being checked
 * @param bits compact target claimed for that block
 * @return true when the claim matches a cached epoch or a bounded jump from the nearest one
 */
bool btc_target_authorize(const btc_target_cache_t* cache, const btc_target_limits_t* limits, uint32_t height, uint32_t bits);

/**
 * Checks proof-of-work and difficulty, then caches the epoch target.
 *
 * The checkpoint header is accepted only at `BTC_MAINNET_CHECKPOINT_HEIGHT`. `height` is not read
 * from the header; binding it to the coinbase script is a later check. On failure `cache` is unchanged.
 *
 * @param cache verified targets
 * @param limits jump bounds, or NULL for the defaults
 * @param height claimed block height
 * @param header 80-byte header
 * @return true when the header is accepted and its epoch target is cached
 */
bool btc_target_accept_header(btc_target_cache_t* cache, const btc_target_limits_t* limits, uint32_t height, bytes_t header);

/**
 * Walks a monotonic sequence of epoch headers and caches each accepted target.
 *
 * Each step must pass `btc_target_accept_header` against the cache built by the previous step, and
 * the epoch numbers must be strictly increasing or strictly decreasing. `count` must be from 1 to
 * `BTC_TARGET_PATH_LIMIT`. `headers` is `count` concatenated 80-byte headers. On failure `cache`
 * is unchanged. The caller passes the steps from the request's `sync_data`, which replaces a
 * separate target RPC: the same request carries the intermediate epochs when the block is farther
 * than `max_daps` from a cached epoch.
 *
 * @param cache verified targets
 * @param limits jump bounds, or NULL for the defaults
 * @param heights `count` block heights, one per header
 * @param headers concatenated headers
 * @param count number of steps
 * @return true when every step was accepted
 */
bool btc_target_accept_path(btc_target_cache_t* cache, const btc_target_limits_t* limits, const uint32_t* heights, bytes_t headers, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif
