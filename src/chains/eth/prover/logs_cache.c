/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */
#include "logs_cache.h"

#ifdef PROVER_CACHE

#include "beacon.h"
#include "bytes.h"
#include "crypto.h"
#include "eth_req.h"
#include "logger.h"
#include "prover.h"
#include <stdlib.h>
#include <string.h>

#define MAX_TOPICS         4
#define MAX_BLOOM_VARIANTS 16

#if defined(_MSC_VER)
#define C4_ALIGN8 __declspec(align(8))
#else
#define C4_ALIGN8 _Alignas(8)
#endif

// --------- Result building structures to minimize recomputation across async calls ---------

/**
 * Linked list node for a single log match within a transaction.
 */
typedef struct event_result_s {
  uint32_t               log_idx;
  struct event_result_s* next;
} event_result_t;

/**
 * Linked list node for a transaction containing matched logs.
 */
typedef struct tx_result_s {
  uint32_t            tx_idx;
  event_result_t*     events;
  struct tx_result_s* next;
} tx_result_t;

/**
 * Linked list node for a block containing matched transactions.
 */
typedef struct block_result_s {
  uint64_t               block_number;
  json_t                 block_receipts; // filled when fetched
  tx_result_t*           txs;
  struct block_result_s* next;
} block_result_t;

/**
 * Request-local state to carry intermediate results and final JSON result across async calls.
 * This structure is cached in the prover context.
 */
typedef struct {
  uint64_t from_block;
  uint64_t to_block;
  bool     resolved;
  uint8_t  hit_counted;
  uint8_t  miss_counted;
  // Prepared filter
  bytes_t filter_blooms;             // n*256 bytes (n variants) or len=0 => bloom disabled
  bytes_t filter_addresses;          // m*20 bytes or len=0 => wildcard
  bytes_t filter_topics[MAX_TOPICS]; // per position: k*32 bytes or len=0 => wildcard
  // Results
  json_t          result;       // final logs array
  char*           result_owner; // owning pointer to result JSON string
  block_result_t* blocks;       // per-block matches (tx_idx + log_idx)
} log_cache_state_t;

/**
 * Compact representation of a log event for caching.
 * Data is not stored; it is retrieved from receipts when assembling results.
 */
typedef struct {
  address_t address;            // address emitted the event
  uint32_t  tx_index;           // transaction index in the block
  uint32_t  log_index;          // log index in the transaction
  uint8_t   topics_count;       // number of topics in the event
  bytes32_t topics[MAX_TOPICS]; // topics of the event
} cached_event_t;

/**
 * Cached block entry containing bloom filter and events.
 */
typedef struct block_entry_s {
  uint64_t        block_number;
  uint64_t        logs_bloom64[32]; // aligned storage for bloom 256 bytes for the bloomfilter (using uint64_t optimizes the the comparison)
  cached_event_t* events;           // dynamic array of events for the block
  uint32_t        events_count;     // size oif the events array
  uint32_t        events_cap;       // capacity of the events array
} block_entry_t;

/**
 * Global cache structure acting as a ring buffer for blocks.
 */
typedef struct {
  block_entry_t* blocks;       // cached blocks as ringbuffer
  uint32_t       blocks_count; // size of the blocks array
  uint32_t       blocks_limit; // the max number of blocks that can be cached
  uint64_t       start_number; // first block number in the cache
  uint32_t       start_idx;    // index of the first block in the blocks array
} logs_cache_t;

/**
 * Global metrics for cache performance and usage.
 */
typedef struct {
  uint64_t total_events;
  uint64_t total_txs; // we approximate by distinct tx_index per block when assembling
  uint64_t hits;
  uint64_t misses;
} logs_metrics_t;

static logs_cache_t   g_cache   = {0};
static logs_metrics_t g_metrics = {0};

/**
 * Resets the global cache, freeing all allocated memory and clearing metrics.
 */
static void reset_cache(void) {
  for (uint32_t i = 0; i < g_cache.blocks_count; i++) {
    if (g_cache.blocks[i].events) safe_free(g_cache.blocks[i].events);
  }
  safe_free(g_cache.blocks);
  g_cache.blocks       = NULL;
  g_cache.blocks_count = 0;
  g_cache.start_idx    = 0;
  g_cache.start_number = 0;

  memset(&g_metrics, 0, sizeof(g_metrics));
}
#define BLOOM_BYTE_LENGTH 256u

static inline void bloom_set(uint8_t* bloom, uint16_t idx) {
  uint16_t byte_index = (uint16_t) (BLOOM_BYTE_LENGTH - 1u - ((idx >> 3u) & 0xffu));
  uint8_t  bit_mask   = (uint8_t) (1u << (idx & 7u));
  bloom[byte_index] |= bit_mask;
}

// Build bloom from filter's address/topics and check subset of block bloom
static void bloom_add_element_buf(uint8_t bloom[256], bytes_t element) {
  bytes32_t hash = {0};
  keccak(element, hash);

  for (int i = 0; i < 6; i += 2) {
    uint16_t idx = (uint16_t) ((((uint16_t) hash[i] << 8u) | hash[i + 1]) & 0x7ffu);
    bloom_set(bloom, idx);
  }
}

/**
 * Retrieves a block entry from the cache or allocates a new one.
 * Handles ring buffer rotation and cache resizing.
 *
 * @param block_number The block number to retrieve or allocate.
 * @return Pointer to the block entry.
 */
static block_entry_t* push_block(uint64_t block_number) {
  // block is already in the cache?
  if (g_cache.start_number <= block_number && g_cache.start_number + g_cache.blocks_count > block_number)
    return g_cache.blocks + ((block_number - g_cache.start_number) % g_cache.blocks_count);

  // is previous block in the cache? if not, we need to reset in order to have a contiguous cache
  if (g_cache.start_number && g_cache.start_number + g_cache.blocks_count != block_number) {
    log_warn("logs_cache: non-contiguous block detected (got %l, expected %l). Resetting cache.", block_number,
             g_cache.start_number + g_cache.blocks_count);
    reset_cache();
  }

  // block-cache is full? we need to rotate and delete the oldest
  if (g_cache.blocks_count == g_cache.blocks_limit) {
    block_entry_t* oldest = &g_cache.blocks[g_cache.start_idx];            // current oldest block spot will be used for the new block.
    safe_free(oldest->events);                                             // clean up old events
    memset(oldest, 0, sizeof(block_entry_t));                              // clear the block entry
    g_cache.start_idx    = (g_cache.start_idx + 1) % g_cache.blocks_count; // rotate to next block
    g_cache.start_number = g_cache.blocks[g_cache.start_idx].block_number; // update the start number to the new oldest block
    return oldest;
  }

  g_cache.blocks_count++;
  g_cache.blocks           = safe_realloc(g_cache.blocks, g_cache.blocks_count * sizeof(block_entry_t));
  block_entry_t* new_block = &g_cache.blocks[g_cache.blocks_count - 1];
  new_block->block_number  = block_number;
  new_block->events        = NULL;
  new_block->events_count  = 0;
  new_block->events_cap    = 0;

  if (g_cache.blocks_count == 1) g_cache.start_number = block_number; // first block in the cache
  return new_block;
}

/**
 * Adds a single event to a block entry.
 * Resizes the event array if necessary.
 */
static void add_event(block_entry_t* e, address_t addr, uint32_t tx_index, uint32_t log_index, uint8_t topics_count, bytes32_t* topics) {
  if (e->events_count == e->events_cap) {
    e->events_cap = e->events_cap ? (e->events_cap * 2u) : 256u;
    e->events     = (cached_event_t*) safe_realloc(e->events, e->events_cap * sizeof(cached_event_t));
  }
  cached_event_t* ev = &e->events[e->events_count++];
  memcpy(ev->address, addr, ADDRESS_SIZE);
  ev->tx_index     = tx_index;
  ev->log_index    = log_index;
  ev->topics_count = topics_count > 4 ? 4 : topics_count;
  for (uint8_t i = 0; i < ev->topics_count; i++) memcpy(ev->topics[i], topics[i], BYTES32_SIZE);
  g_metrics.total_events++;
}

/**
 * Adds a block with its logs to the cache.
 * Called when a new block is processed or fetched.
 *
 * @param block_number   The block number.
 * @param logs_bloom     The 256-byte logs bloom filter.
 * @param receipts_array JSON array of transaction receipts for the block.
 */
void c4_eth_logs_cache_add_block(uint64_t block_number, const uint8_t* logs_bloom, json_t receipts_array) {
  if (!c4_eth_logs_cache_is_enabled()) return;

  block_entry_t* e = push_block(block_number);
  if (!e) return;
  memcpy(e->logs_bloom64, logs_bloom, 256);

  // Extract events minimally from receipts
  bytes32_t tmp            = {0};
  uint32_t  tx_count_local = 0;
  address_t addr           = {0};
  json_for_each_value(receipts_array, r) {
    uint32_t tx_index = json_get_uint32(r, "transactionIndex");
    tx_count_local++;
    uint32_t li = 0;
    json_for_each_value(json_get(r, "logs"), log) {
      json_to_var(json_get(log, "address"), addr);

      // topics
      bytes32_t topics_arr[4] = {0};
      uint8_t   tc            = 0;
      json_for_each_value(json_get(log, "topics"), t) {
        if (tc >= 4) break;
        if (json_to_var(t, tmp) == 32) memcpy(topics_arr[tc++], tmp, 32);
      }
      add_event(e, addr, tx_index, li, tc, topics_arr);
      li++;
    }
  }
  g_metrics.total_txs += tx_count_local;
}

/**
 * Frees a linked list of transaction results.
 */
static void free_tx_results(tx_result_t* txs) {
  while (txs) {
    tx_result_t*    next_tx = txs->next;
    event_result_t* ev      = txs->events;
    while (ev) {
      event_result_t* ne = ev->next;
      free(ev);
      ev = ne;
    }
    free(txs);
    txs = next_tx;
  }
}

/**
 * Frees a linked list of block results.
 */
static void free_block_results(block_result_t* blocks) {
  while (blocks) {
    block_result_t* nb = blocks->next;
    free_tx_results(blocks->txs);
    free(blocks);
    blocks = nb;
  }
}

/**
 * Destructor for log cache state (prover cache entry).
 */
static void free_log_state(void* ptr) {
  if (!ptr) return;
  log_cache_state_t* st = (log_cache_state_t*) ptr;
  if (st->result_owner) free(st->result_owner);
  if (st->filter_blooms.data) free(st->filter_blooms.data);
  if (st->filter_addresses.data) free(st->filter_addresses.data);
  for (int i = 0; i < MAX_TOPICS; i++)
    if (st->filter_topics[i].data) free(st->filter_topics[i].data);
  free_block_results(st->blocks);
  free(st);
}

/**
 * Retrieves or creates the request-local log state from the prover context.
 */
static log_cache_state_t* get_log_state(prover_ctx_t* ctx) {
  bytes32_t key = {0};
  memcpy(key, "log_state", 9);
  log_cache_state_t* state = (log_cache_state_t*) c4_prover_cache_get_local(ctx, key);
  if (!state) {
    state = (log_cache_state_t*) safe_calloc(1, sizeof(log_cache_state_t));
    c4_prover_cache_set(ctx, key, state, (uint32_t) sizeof(log_cache_state_t), 0, free_log_state);
  }
  return state;
}

static inline tx_result_t* ensure_tx_result(block_result_t* b, uint32_t tx_idx) {
  for (tx_result_t* t = b->txs; t; t = t->next)
    if (t->tx_idx == tx_idx) return t;
  tx_result_t* t = (tx_result_t*) safe_calloc(1, sizeof(tx_result_t));
  t->tx_idx      = tx_idx;
  t->next        = b->txs;
  b->txs         = t;
  return t;
}

static inline block_result_t* add_block_result(block_result_t** head, uint64_t block_number) {
  block_result_t* b = (block_result_t*) safe_calloc(1, sizeof(block_result_t));
  b->block_number   = block_number;
  b->next           = *head;
  *head             = b;
  return b;
}

/**
 * Checks if `small` bloom filter is a subset of `big` bloom filter.
 * Uses 64-bit operations for speed.
 */
static inline bool bloom_subset_of64(const uint64_t* small, const uint64_t* big) {
  for (int i = 0; i < 32; i++) {
    if ((small[i] & big[i]) != small[i]) return false;
  }
  return true;
}

/**
 * Checks if any of the bloom filter variants matches the block's logs bloom.
 *
 * @param variant_count Number of bloom variants generated from the filter.
 * @param variants Array of 256-byte bloom filters (flattened).
 * @param logs_bloom64 Block's logs bloom filter.
 * @return True if any variant is a subset of the block bloom.
 */
static inline bool bloom_matches(int variant_count, uint64_t* variants, uint64_t* logs_bloom64) {
  for (int vi = 0; vi < variant_count; vi++) {
    if (bloom_subset_of64(variants + (vi * 32), logs_bloom64)) return true;
  }
  return false;
}

/**
 * Checks if an address matches the filter addresses.
 *
 * @param addresses Concatenated 20-byte addresses from the filter. Length 0 means wildcard.
 * @param address The address to check.
 * @return True if match or wildcard.
 */
static inline bool address_matches(bytes_t addresses, address_t address) {
  if (addresses.len) {
    for (uint32_t i = 0; i < addresses.len; i += ADDRESS_SIZE) {
      if (memcmp(address, addresses.data + i, ADDRESS_SIZE) == 0) return true;
    }
    return false;
  }
  return true;
}

/**
 * Checks if event topics match the filter topics.
 *
 * @param filter_topics Array of topic filters (concatenated bytes). Length 0 means wildcard.
 * @param topics Array of event topics.
 * @param topics_count Number of topics in the event.
 * @return True if all non-wildcard positions match.
 */
static inline bool topics_matches(bytes_t filter_topics[MAX_TOPICS], bytes32_t* topics, uint8_t topics_count) {
  // Topics positional check (bytes)
  for (int p = 0; p < MAX_TOPICS; p++) {
    bytes_t tp = filter_topics[p];
    if (tp.len == 0) continue; // wildcard
    if (topics_count <= p) return false;

    bool any = false;
    for (uint32_t i = 0; i <= tp.len; i += 32) {
      if (memcmp(topics[p], tp.data + i, 32) == 0) {
        any = true;
        break;
      }
    }
    if (!any) return false;
  }
  return true;
}

// -------- Filter preparation (addresses/topics as bytes and bloom variants) --------

/**
 * Extracts addresses from filter JSON into a flat byte buffer.
 * Handles single address string or array of address strings.
 */
static void build_filter_addresses(json_t address_json, bytes_t* out_addresses) {
  uint8_t  tmp[ADDRESS_SIZE] = {0};
  buffer_t b                 = stack_buffer(tmp);
  switch (address_json.type) {
    case JSON_TYPE_STRING: {
      bytes_t a = json_as_bytes(address_json, &b);
      if (a.len == ADDRESS_SIZE) {
        *out_addresses = bytes_dup(a);
      }
      return;
    }
    case JSON_TYPE_ARRAY: {
      int count = 0;
      json_for_each_value(address_json, _) count++;
      if (count <= 0) return;
      uint8_t* buf = (uint8_t*) safe_malloc((size_t) count * ADDRESS_SIZE);
      int      i   = 0;
      json_for_each_value(address_json, a) {
        bytes_t ab = json_as_bytes(a, &b);
        if (ab.len == ADDRESS_SIZE) memcpy(buf + (i++ * ADDRESS_SIZE), ab.data, ADDRESS_SIZE);
      }
      *out_addresses = bytes(buf, (uint32_t) (i * ADDRESS_SIZE));
      return;
    }
    default:
      return;
  }
}

/**
 * Extracts topics from filter JSON into per-position flat byte buffers.
 * Handles single topic or array of topics (OR condition) per position.
 */
static void build_filter_topics(json_t topics_json, bytes_t out_topics[MAX_TOPICS]) {
  memset(out_topics, 0, sizeof(bytes_t) * MAX_TOPICS);
  if (topics_json.type != JSON_TYPE_ARRAY) return;
  uint8_t  tmp[32] = {0};
  buffer_t b       = stack_buffer(tmp);
  int      pos     = 0;
  json_for_each_value(topics_json, tpos) {
    if (pos >= MAX_TOPICS) break;
    if (tpos.type == JSON_TYPE_STRING) {
      bytes_t v = json_as_bytes(tpos, &b);
      if (v.len == 32) {
        uint8_t* buf = (uint8_t*) safe_malloc(32);
        memcpy(buf, v.data, 32);
        out_topics[pos] = bytes(buf, 32);
      }
    }
    else if (tpos.type == JSON_TYPE_ARRAY) {
      // Collect OR list
      int count = json_len(tpos);
      if (count > 0) {
        uint8_t* buf = (uint8_t*) safe_malloc((size_t) count * 32);
        int      i   = 0;
        json_for_each_value(tpos, cand) {
          bytes_t v = json_as_bytes(cand, &b);
          if (v.len == 32) memcpy(buf + (i++ * 32), v.data, 32);
        }
        out_topics[pos] = bytes(buf, (uint32_t) (32 * i));
      }
    }
    // else: null => wildcard (len=0)
    pos++;
  }
}

/**
 * Generates all combinations of bloom filters for the given addresses and topics.
 * Used for fast pre-filtering of blocks using the block's logs bloom.
 * If too many variants would be generated, returns 0 to disable bloom filtering.
 *
 * @param addresses Filter addresses.
 * @param topics Filter topics.
 * @param out_variants Output array for generated bloom filters.
 * @return Number of variants generated, or 0 if limit exceeded.
 */
static int build_bloom_variants(bytes_t addresses, bytes_t topics[MAX_TOPICS], uint64_t out_variants[MAX_BLOOM_VARIANTS][32]) {
  int addr_count         = (int) (addresses.len / ADDRESS_SIZE);
  int counts[MAX_TOPICS] = {0};
  int positions          = MAX_TOPICS;
  for (int p = 0; p < MAX_TOPICS; p++) counts[p] = (int) topics[p].len / 32;
  // Calculate total combinations, cap
  int total = (addr_count ? addr_count : 1);
  for (int p = 0; p < MAX_TOPICS; p++) {
    int c = counts[p] ? counts[p] : 1;
    if (total > (MAX_BLOOM_VARIANTS / c)) return 0; // disable bloom prefilter
    total *= c;
  }
  // Mixed-radix indices: addr + topics
  int idx_addr        = 0;
  int idx[MAX_TOPICS] = {0, 0, 0, 0};
  for (int v = 0; v < total && v < MAX_BLOOM_VARIANTS; v++) {
    uint8_t* bloom = (uint8_t*) out_variants[v];
    memset(bloom, 0, 256);
    if (addr_count) bloom_add_element_buf(bloom, bytes(addresses.data + (idx_addr * ADDRESS_SIZE), ADDRESS_SIZE));
    for (int p = 0; p < MAX_TOPICS; p++) {
      if (!counts[p]) continue; // wildcard
      bloom_add_element_buf(bloom, bytes(topics[p].data + (idx[p] * 32), 32));
    }
    // increment
    if (addr_count) {
      idx_addr++;
      if (idx_addr < addr_count) continue;
      idx_addr = 0;
    }
    for (int p = MAX_TOPICS - 1; p >= 0; p--) {
      if (counts[p] < 2) continue; // skip wildcard (0) and single-option (1) positions
      idx[p]++;
      if (idx[p] < counts[p]) break;
      idx[p] = 0;
    }
  }
  return total;
}

/**
 * Phase 1: Build matches index.
 * Scans cached blocks in the requested range.
 * Uses bloom filters for fast rejection, then checks cached events.
 * Populates `st->blocks` with matching transactions and log indices.
 */
static void build_match_index(log_cache_state_t* st) {
  int       variant_count = (int) st->filter_blooms.len / 256;
  uint64_t* variants      = (uint64_t*) st->filter_blooms.data;
  for (uint64_t bn = st->from_block; bn <= st->to_block; bn++) {
    block_entry_t* e = g_cache.blocks + ((g_cache.start_idx + (uint32_t) (bn - g_cache.start_number)) % g_cache.blocks_count);
    if (!bloom_matches(variant_count, variants, e->logs_bloom64)) continue;
    // Confirm by scanning cached events
    block_result_t* block_res = NULL;
    for (uint32_t i = 0; i < e->events_count; i++) {
      cached_event_t* ev = &e->events[i];
      if (!address_matches(st->filter_addresses, ev->address)) continue;
      if (!topics_matches(st->filter_topics, ev->topics, ev->topics_count)) continue;

      // we have a match, add it to the index
      if (!block_res) block_res = add_block_result(&st->blocks, e->block_number);
      tx_result_t* txr = ensure_tx_result(block_res, ev->tx_index);
      // append event
      event_result_t* er = (event_result_t*) safe_calloc(1, sizeof(event_result_t));
      er->log_idx        = ev->log_index;
      er->next           = txr->events;
      txr->events        = er;
    }
  }
}

/**
 * Phase 2: Ensure receipts fetched for selected blocks.
 * Triggers async `eth_getBlockReceipts` for any block in the results that lacks receipts.
 *
 * @return C4_SUCCESS if all requests initiated (or already present), or error status.
 */
static c4_status_t ensure_receipts_for_matches(prover_ctx_t* ctx, block_result_t* blocks) {
  c4_status_t status  = C4_SUCCESS;
  uint8_t     tmp[64] = {0};
  buffer_t    b       = stack_buffer(tmp);
  for (block_result_t* br = blocks; br; br = br->next) {
    if (br->block_receipts.type == JSON_TYPE_INVALID || br->block_receipts.start == NULL) {
      buffer_reset(&b);
      TRY_ADD_ASYNC(status, eth_getBlockReceipts(ctx, json_parse(bprintf(&b, "\"0x%lx\"", br->block_number)), &br->block_receipts));
    }
  }
  return status;
}

/**
 * Phase 3: Build final JSON result.
 * Combines fetched receipts with the match index to produce the standard `eth_getLogs` output.
 *
 * @param out_logs Output pointer for the result JSON.
 */
static c4_status_t build_result_json_from_matches(prover_ctx_t* ctx, log_cache_state_t* st, json_t filter, json_t* out_logs) {
  buffer_t out_buf = {0};
  buffer_add_chars(&out_buf, "[");
  bool first = true;
  for (block_result_t* br = st->blocks; br; br = br->next) {
    json_t receipts = br->block_receipts;
    if (receipts.type == JSON_TYPE_INVALID || receipts.start == NULL) continue;
    for (tx_result_t* tx = br->txs; tx; tx = tx->next) {
      json_t rxs = json_at(receipts, tx->tx_idx);
      if (rxs.type == JSON_TYPE_INVALID || rxs.type == JSON_TYPE_NOT_FOUND) continue;
      json_t logs = json_get(rxs, "logs");
      for (event_result_t* ev = tx->events; ev; ev = ev->next) {
        json_t logj = json_at(logs, ev->log_idx);
        if (logj.type != JSON_TYPE_OBJECT) continue;
        if (!first) buffer_add_chars(&out_buf, ",");
        buffer_add_json(&out_buf, logj);
        first = false;
      }
    }
  }
  buffer_add_chars(&out_buf, "]");
  // Persist result string until context end
  st->result_owner = buffer_as_string(out_buf);
  st->result       = (json_t) {.start = st->result_owner, .len = out_buf.data.len, .type = JSON_TYPE_ARRAY}; //  json_parse(st->result_owner);
  *out_logs        = st->result;
  // DO NOT buffer_free(out_buf); ownership moved to st->result_owner
  return C4_SUCCESS;
}

static c4_status_t get_exec_blocknumber(prover_ctx_t* ctx, json_t block, uint64_t* out_block_number) {
  if (!out_block_number) return C4_ERROR;
  beacon_block_t beacon_block = {0};
  if (block.type == JSON_TYPE_NOT_FOUND || block.type == JSON_TYPE_INVALID) block = json_parse("\"latest\"");
  if (block.type != JSON_TYPE_STRING) THROW_ERROR_WITH("Invalid block: %J", block);
  if (strncmp(block.start, "\"0x", 3) == 0) {
    *out_block_number = json_as_uint64(block);
    return C4_SUCCESS;
  }
  TRY_ASYNC(c4_beacon_get_block_for_eth(ctx, block, &beacon_block));
  *out_block_number = ssz_get_uint64(&beacon_block.execution, "blockNumber");
  return C4_SUCCESS;
}

// (removed old minimal state struct; consolidated into log_cache_state_t)

/**
 * Scans the logs cache for matches against the filter.
 *
 * This function operates in 3 phases across multiple async calls (due to `eth_getBlockReceipts`):
 * 1. Range Check & Filter Build: Checks if range is covered. Builds filter structures.
 * 2. Match Index: Scans cached blocks/events to find matches (Phase 1).
 * 3. Receipt Fetch: Ensures receipts are available for matched blocks (Phase 2).
 * 4. Result Build: Assembles final JSON from receipts using the match index (Phase 3).
 *
 * @param ctx The prover context.
 * @param filter The filter JSON object (fromBlock, toBlock, address, topics).
 * @param out_logs Output pointer for the logs JSON array.
 * @param served_from_cache Output flag, set to true if served from cache.
 * @return C4_SUCCESS or error status.
 */
c4_status_t c4_eth_logs_cache_scan(prover_ctx_t* ctx, json_t filter, json_t* out_logs, bool* served_from_cache) {
  if (served_from_cache) *served_from_cache = false;
  if (!c4_eth_logs_cache_is_enabled()) return C4_SUCCESS;

  // Resolve and persist the numeric range across async invocations
  log_cache_state_t* st = get_log_state(ctx);

  // If result already built, return it
  if (st->result.start) {
    *out_logs = st->result;
    if (served_from_cache) *served_from_cache = true;
    return C4_SUCCESS;
  }

  // check blockrange
  if (!st->resolved) {
    TRY_ASYNC(get_exec_blocknumber(ctx, json_get(filter, "fromBlock"), &st->from_block));
    TRY_ASYNC(get_exec_blocknumber(ctx, json_get(filter, "toBlock"), &st->to_block));
    if (st->from_block > st->to_block)
      THROW_ERROR_WITH("Invalid block range: fromBlock %l > toBlock %l", st->from_block, st->to_block);
    st->resolved = true;
  }

  // we always need to check the blockrange,
  // since the range may have changed since last call
  if (!c4_eth_logs_cache_has_range(st->from_block, st->to_block)) {
    if (!st->miss_counted) {
      g_metrics.misses++;
      st->miss_counted = 1;
    }
    return C4_SUCCESS;
  }

  // Build filter (addresses/topics/bloom variants) and match index on first pass
  if (!st->blocks) {
    if (st->filter_blooms.len == 0 && !st->filter_addresses.len) {
      build_filter_addresses(json_get(filter, "address"), &st->filter_addresses);
      build_filter_topics(json_get(filter, "topics"), st->filter_topics);
      uint64_t tmp_variants[MAX_BLOOM_VARIANTS][32];
      int      vcount = build_bloom_variants(st->filter_addresses, st->filter_topics, tmp_variants);
      if (vcount > 0) {
        st->filter_blooms = bytes(safe_malloc((size_t) vcount * 256), (uint32_t) (vcount * 256));
        memcpy(st->filter_blooms.data, tmp_variants, (size_t) vcount * 256);
      }
    }
    build_match_index(st);
    // No matches -> empty result immediately
    if (!st->blocks) {
      st->result_owner = strdup("[]");
      st->result       = json_parse(st->result_owner);
      *out_logs        = st->result;
      if (served_from_cache) *served_from_cache = true;
      // keep ownership to free at end
      if (!st->hit_counted) {
        g_metrics.hits++;
        st->hit_counted = 1;
      }
      return C4_SUCCESS;
    }
  }

  // Ensure receipts for the matched blocks
  TRY_ASYNC(ensure_receipts_for_matches(ctx, st->blocks));

  // Build final JSON and store in state
  TRY_ASYNC(build_result_json_from_matches(ctx, st, filter, out_logs));
  if (served_from_cache) *served_from_cache = true;
  if (!st->hit_counted) {
    g_metrics.hits++;
    st->hit_counted = 1;
  }
  return C4_SUCCESS;
}

void c4_eth_logs_cache_stats(uint64_t* blocks, uint64_t* txs, uint64_t* events) {
  if (blocks) *blocks = g_cache.blocks_count;
  if (txs) *txs = g_metrics.total_txs;
  if (events) *events = g_metrics.total_events;
}
void c4_eth_logs_cache_counters(uint64_t* hits, uint64_t* misses, uint64_t* bloom_skips) {
  if (hits) *hits = g_metrics.hits;
  if (misses) *misses = g_metrics.misses;
  if (bloom_skips) *bloom_skips = 0; // unused
}
uint64_t c4_eth_logs_cache_first_block(void) { return g_cache.start_number; }
uint64_t c4_eth_logs_cache_last_block(void) { return g_cache.start_number + g_cache.blocks_count - 1; }
uint32_t c4_eth_logs_cache_capacity_blocks(void) { return g_cache.blocks_count; }

void c4_eth_logs_cache_enable(uint32_t max_blocks) {
  g_cache.blocks_limit = max_blocks;
}

void c4_eth_logs_cache_disable(void) {
  g_cache.blocks_limit = 0;
  reset_cache();
}

bool c4_eth_logs_cache_is_enabled(void) {
  return g_cache.blocks_limit > 0;
}

bool c4_eth_logs_cache_has_range(uint64_t from_block, uint64_t to_block) {
  if (g_cache.blocks_count == 0 || from_block > to_block) return false;
  return from_block >= g_cache.start_number && to_block < g_cache.start_number + g_cache.blocks_count;
}

bytes_t c4_eth_create_bloomfilter(json_t filter) {
  bytes_t result             = {0};
  bytes_t addresses          = {0};
  bytes_t topics[MAX_TOPICS] = {0};
  build_filter_addresses(json_get(filter, "address"), &addresses);
  build_filter_topics(json_get(filter, "topics"), topics);
  uint64_t tmp_variants[MAX_BLOOM_VARIANTS][32];
  int      vcount = build_bloom_variants(addresses, topics, tmp_variants);
  if (vcount > 0) {
    result = bytes(safe_malloc((size_t) vcount * 256), (uint32_t) (vcount * 256));
    memcpy(result.data, tmp_variants, (size_t) vcount * 256);
  }
  safe_free(addresses.data);
  for (int i = 0; i < MAX_TOPICS; i++) safe_free(topics[i].data);
  return result;
}

#endif // PROVER_CACHE
