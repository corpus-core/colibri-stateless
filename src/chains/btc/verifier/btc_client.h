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

#ifndef btc_client_h__
#define btc_client_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "chains.h"
#include <stdint.h>

/** First byte of a Bitcoin client-state blob. Distinct from the Ethereum sync-state status values. */
#define BTC_CLIENT_STATE_TAG 0xB1u
/** Encoded size of a Bitcoin client-state blob: tag, height, header hash. */
#define BTC_CLIENT_STATE_SIZE 37u

/**
 * Last Bitcoin block the client has verified.
 *
 * A missing blob means the compiled checkpoint at `BTC_MAINNET_CHECKPOINT_HEIGHT`.
 * The persisted bytes are the `c4` property on a prover request, because they live
 * under the `states_<chain_id>` key that `c4_get_client_state` reads.
 *
 * ```
 * byte 0       BTC_CLIENT_STATE_TAG
 * bytes 1..4   height, little-endian
 * bytes 5..36  header hash, internal byte order
 * ```
 */
typedef struct {
  uint32_t height;
  uint8_t  hash[32];
} btc_client_state_t;

/**
 * Fills `state` with the compiled mainnet checkpoint.
 *
 * @param state receives the checkpoint height and header hash
 */
void btc_client_state_set_checkpoint(btc_client_state_t* state);

/**
 * Serializes `state`.
 *
 * @param state verified tip
 * @param out receives `BTC_CLIENT_STATE_SIZE` bytes; the previous contents are freed
 * @return true when `out` holds the blob
 */
bool btc_client_state_encode(const btc_client_state_t* state, buffer_t* out);

/**
 * Parses a client-state blob.
 *
 * The checkpoint height is accepted only with the compiled checkpoint header hash.
 * On failure `state` is left unchanged.
 *
 * @param blob bytes from `btc_client_state_encode` or from storage
 * @param state receives the tip
 * @return true when the blob is well formed
 */
bool btc_client_state_decode(bytes_t blob, btc_client_state_t* state);

/**
 * Loads the persisted tip for `chain_id`.
 *
 * A missing or corrupt blob fills the checkpoint and returns false.
 *
 * @param chain_id chain whose tip is loaded
 * @param state receives the tip
 * @return true when a stored blob was accepted
 */
bool btc_client_state_load(chain_id_t chain_id, btc_client_state_t* state);

/**
 * Writes the tip for `chain_id`.
 *
 * @param chain_id chain whose tip is stored
 * @param state verified tip
 * @return true when the storage backend accepted the blob
 */
bool btc_client_state_store(chain_id_t chain_id, const btc_client_state_t* state);

#ifdef __cplusplus
}
#endif

#endif
