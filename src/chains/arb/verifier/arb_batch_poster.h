/*
 * Copyright (c) 2025,2026 corpus.core GmbH
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

#ifndef arb_batch_poster_h__
#define arb_batch_poster_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "chains.h"
#include <stdint.h>

/** `SequencerInbox.isBatchPoster` mapping slot. */
#define ARB_IS_BATCH_POSTER_SLOT 3u

/**
 * Copies the SequencerInbox address of `chain_id`.
 *
 * Arbitrum One uses `0x1c479675ad559DC151F6Ec7ed3FbF8ceE79582B6`.
 *
 * @param chain_id client chain
 * @param address receives 20 bytes (must not be NULL)
 * @return true when this chain has a configured inbox
 */
bool arb_sequencer_inbox(chain_id_t chain_id, uint8_t address[20]);

/**
 * Storage key of `isBatchPoster[poster]`.
 *
 * ```
 * keccak256(pad32(poster) || pad32(3))
 * ```
 *
 * For `0xC1b634853Cb333D3aD8663715b08f41A3Aec47cc` the key is
 * `0xa10aa54071443520884ed767b0684edf43acec528b7da83ab38ce60126562660`.
 *
 * @param poster 20-byte batch poster (must not be NULL)
 * @param out 32-byte key (must not be NULL)
 */
void arb_batch_poster_storage_key(const uint8_t poster[20], uint8_t out[32]);

/**
 * Returns whether an L1 block number may replace the cached batch poster.
 *
 * No cache, or `l1_number` greater than or equal to the stored number, is accepted.
 * A smaller number is a replay of a proof from before the current one.
 *
 * @param chain_id chain whose cache is read
 * @param l1_number L1 block number of the new proof
 * @return true when the proof is not older than the cache
 */
bool arb_batch_poster_l1_is_current(chain_id_t chain_id, uint64_t l1_number);

#ifdef __cplusplus
}
#endif

#endif
