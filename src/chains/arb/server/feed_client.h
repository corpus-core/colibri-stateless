/*
 * Copyright 2025,2026 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef arb_feed_client_h__
#define arb_feed_client_h__

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
