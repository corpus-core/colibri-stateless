/*
 * Copyright 2025,2026 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef arb_feed_client_h__
#define arb_feed_client_h__

#include "chains.h"
#include "json.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * Keeps this broadcast message when it carries a block hash and a 65-byte `signatureV2`.
 *
 * The stored tail is the Nitro digest suffix after `blockHash`:
 * `blockMetadata | be64(delayedMessagesRead) | kind | poster | be64(l1BlockNumber) | be64(timestamp) | [requestId] | [l1BaseFee] | l2Msg`.
 * The most recently received signed message replaces the head. A negative sequence number is ignored.
 *
 * @param message one object from the feed `messages` array
 * @return true when this message is now the head
 */
bool arb_feed_store_message(json_t message);

/**
 * Copies the newest feed message that had a block hash and a signature.
 *
 * @param block_hash 32-byte L2 block hash
 * @param signature 65-byte secp256k1 signature
 * @param tail receives a newly allocated tail; the caller frees it
 * @param tail_len length of `tail`
 * @param sequence feed sequence number of that message
 * @return false when no signed block has been seen yet
 */
bool arb_feed_copy_head(uint8_t block_hash[32], uint8_t signature[65], uint8_t** tail, uint32_t* tail_len, uint64_t* sequence);

/**
 * Returns the sender address of the current head's `signatureV2` for `chain_id`.
 *
 * Internally caches the recovered address for the lifetime of the current head:
 * the first caller pays for the ecrecover, every following caller reads from
 * the cache under the feed head mutex. The cache is invalidated as soon as a
 * new signed message replaces the head.
 *
 * @param chain_id the Arbitrum chain id the signer should be recovered for
 * @param out_signer 20-byte buffer that receives the recovered address
 * @return `true` on success. `false` when no signed message has been seen yet
 *         or the cached head signature cannot be recovered.
 */
bool arb_feed_get_head_signer(chain_id_t chain_id, uint8_t out_signer[20]);

/**
 * Opens the sequencer feed configured in `arb_config.feed_url` and logs each
 * message's sequence number and block hash.
 *
 * The connection lives on the libuv default loop. A second call while the
 * client is already running does nothing. An empty URL is logged and ignored.
 */
void arb_feed_start(void);

/**
 * Closes the feed socket and stops reconnects.
 *
 * Safe to call when the client was never started.
 */
void arb_feed_stop(void);

#endif
