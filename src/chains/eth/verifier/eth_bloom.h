/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */
#ifndef eth_bloom_h__
#define eth_bloom_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "bytes.h"
#include "json.h"
#include "ssz.h"

#define C4_ETH_LOG_MAX_TOPICS     4
#define C4_ETH_BLOOM_MAX_VARIANTS 16

/**
 * Computes EVM bloom filter variants from a JSON filter object.
 *
 * Parses `address` and `topics` from the filter, then generates all
 * combinatorial bloom variants (one per address/topic combination).
 *
 * @param filter JSON object with optional `address` and `topics` fields.
 * @return Concatenated 256-byte blooms (`n * 256` bytes). Caller must free.
 *         Returns `NULL_BYTES` if no variants can be generated.
 */
bytes_t c4_eth_create_bloomfilter(json_t filter);

/**
 * Computes the query bloom variants for an `eth_getLogs` filter, transparently
 * supporting both plain and PAP (Pragmatic Adaptive Privacy) filters.
 *
 * If the filter contains a `bloomFilter` array (PAP mode), its hex-encoded
 * 256-byte entries are concatenated directly. Otherwise the variants are derived
 * from `address` and `topics` via `c4_eth_create_bloomfilter`.
 *
 * @param filter JSON filter object (`address`/`topics` or `bloomFilter`).
 * @return Concatenated 256-byte blooms (`n * 256` bytes). Caller must free.
 *         Returns `NULL_BYTES` if no variants can be derived.
 */
bytes_t c4_eth_filter_query_blooms(json_t filter);

/**
 * Decides whether a block can be proven to contain no matching log by bloom alone.
 *
 * A block is bloom-negative when none of the query bloom variants is a bit-subset
 * of the block's `logsBloom`: if any required bit is missing from the block bloom,
 * no matching log can exist. If there are no variants or the block bloom has an
 * unexpected length, the function conservatively returns `false` (a full receipts
 * check is required).
 *
 * @param query_blooms Concatenated 256-byte query bloom variants (`n * 256` bytes).
 * @param block_bloom The block's `logsBloom` (must be 256 bytes).
 * @return `true` if the block provably contains no matching log, `false` otherwise.
 */
bool c4_eth_bloom_negative(bytes_t query_blooms, bytes_t block_bloom);

/**
 * Parses address filter(s) from JSON into a flat byte buffer.
 *
 * Handles a single hex address string or an array of hex address strings.
 * Each valid 20-byte address is appended to the output buffer.
 *
 * @param address_json JSON string or array of address strings.
 * @param out Receives `n * 20` bytes of concatenated addresses. Caller must free.
 */
void c4_eth_parse_filter_addresses(json_t address_json, bytes_t* out_addresses);

/**
 * Parses topic filters from JSON into per-position flat byte buffers.
 *
 * Each position may be `null` (wildcard), a single hex topic string,
 * or an array of hex topic strings (OR condition).
 *
 * @param topics_json JSON array of topic positions.
 * @param out_topics Per-position byte buffers (`k * 32` bytes each). Caller must free each non-empty entry.
 */
void c4_eth_parse_filter_topics(json_t topics_json, bytes_t out_topics[C4_ETH_LOG_MAX_TOPICS]);

/**
 * Filters an SSZ log list, keeping only entries matching address and topic criteria.
 *
 * Iterates the SSZ list of `EthReceiptDataLog` containers and returns a new
 * list containing only the logs whose `address` and `topics` fields match
 * the given filter. The caller must free `result.bytes.data`.
 *
 * @param logs SSZ list of log containers (e.g. `ETH_SSZ_DATA_LOGS` type).
 * @param filter JSON filter object with optional `address` and `topics` fields.
 * @return New SSZ list with matching logs. Caller must free `bytes.data`.
 */
ssz_ob_t c4_eth_filter_logs(ssz_ob_t logs, json_t filter);

#ifdef __cplusplus
}
#endif

#endif
