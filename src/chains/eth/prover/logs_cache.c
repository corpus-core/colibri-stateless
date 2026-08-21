/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */
#include "logs_cache.h"

#ifdef PROVER_CACHE

#include "beacon.h"
#include "bytes.h"
#include "eth_bloom.h"
#include "eth_req.h"
#include "logger.h"
#include "prover.h"
#include <stdlib.h>
#include <string.h>

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
  bool     bloom_only; // true when bloomFilter was provided (PAP mode): skip event-level filtering
  // Prepared filter
  bytes_t filter_blooms;                        // n*256 bytes (n variants) or len=0 => bloom disabled
  bytes_t filter_addresses;                     // m*20 bytes or len=0 => wildcard
  bytes_t filter_topics[C4_ETH_LOG_MAX_TOPICS]; // per position: k*32 bytes or len=0 => wildcard
  // Backfill state (loading older blocks into cache on demand)
  uint64_t backfill_from;     // first block number to backfill
  uint32_t backfill_count;    // number of blocks to backfill
  json_t*  backfill_receipts; // array of json_t for loaded receipts (one per backfill block)
  bool     backfill_done;     // true after backfill blocks have been inserted into the cache
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
  address_t address;                       // address emitted the event
  uint32_t  tx_index;                      // transaction index in the block
  uint32_t  log_index;                     // log index in the transaction
  uint8_t   topics_count;                  // number of topics in the event
  bytes32_t topics[C4_ETH_LOG_MAX_TOPICS]; // topics of the event
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
  e->block_number = block_number;
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
  for (int i = 0; i < C4_ETH_LOG_MAX_TOPICS; i++)
    if (st->filter_topics[i].data) free(st->filter_topics[i].data);
  if (st->backfill_receipts) safe_free(st->backfill_receipts);
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
static inline bool topics_matches(bytes_t filter_topics[C4_ETH_LOG_MAX_TOPICS], bytes32_t* topics, uint8_t topics_count) {
  // Topics positional check (bytes)
  for (int p = 0; p < C4_ETH_LOG_MAX_TOPICS; p++) {
    bytes_t tp = filter_topics[p];
    if (tp.len == 0) continue; // wildcard
    if (topics_count <= p) return false;

    bool any = false;
    for (uint32_t i = 0; i < tp.len; i += 32) {
      if (memcmp(topics[p], tp.data + i, 32) == 0) {
        any = true;
        break;
      }
    }
    if (!any) return false;
  }
  return true;
}

/**
 * Phase 1: Build matches index.
 * Scans cached blocks in the requested range.
 * Uses bloom filters for fast rejection, then checks cached events.
 * Populates `st->blocks` with matching transactions and log indices.
 *
 * In `bloom_only` mode (PAP), all events of a bloom-matching block are
 * included without further address/topics filtering.
 */
static void build_match_index(log_cache_state_t* st) {
  int       variant_count = (int) st->filter_blooms.len / 256;
  uint64_t* variants      = (uint64_t*) st->filter_blooms.data;
  for (uint64_t bn = st->from_block; bn <= st->to_block; bn++) {
    block_entry_t* e = g_cache.blocks + ((g_cache.start_idx + (uint32_t) (bn - g_cache.start_number)) % g_cache.blocks_count);
    if (variant_count && !bloom_matches(variant_count, variants, e->logs_bloom64)) continue;
    block_result_t* block_res = NULL;
    for (uint32_t i = 0; i < e->events_count; i++) {
      cached_event_t* ev = &e->events[i];
      if (!st->bloom_only) {
        if (!address_matches(st->filter_addresses, ev->address)) continue;
        if (!topics_matches(st->filter_topics, ev->topics, ev->topics_count)) continue;
      }
      if (!block_res) block_res = add_block_result(&st->blocks, e->block_number);
      tx_result_t*    txr = ensure_tx_result(block_res, ev->tx_index);
      event_result_t* er  = (event_result_t*) safe_calloc(1, sizeof(event_result_t));
      er->log_idx         = ev->log_index;
      er->next            = txr->events;
      txr->events         = er;
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

// -------- Backfill helpers --------

/**
 * Computes a block-level logs bloom by OR-ing all receipt `logsBloom` fields.
 *
 * @param receipts JSON array of transaction receipts.
 * @param out      Output buffer (256 bytes, zeroed first).
 */
static void compute_block_bloom_from_receipts(json_t receipts, uint8_t out[256]) {
  C4_ALIGN8 uint64_t bloom64[32] = {0};
  uint8_t            tmp[256]    = {0};
  buffer_t           buf         = stack_buffer(tmp);
  json_for_each_value(receipts, r) {
    buffer_reset(&buf);
    bytes_t rb = json_get_bytes(r, "logsBloom", &buf);
    if (rb.len == 256) {
      uint64_t* src = (uint64_t*) rb.data;
      for (int i = 0; i < 32; i++) bloom64[i] |= src[i];
    }
  }
  memcpy(out, bloom64, 256);
}

/**
 * Extends the cache backwards by `count` empty block slots so that blocks
 * `[from_block, from_block+count)` can be filled via `c4_eth_logs_cache_add_block`.
 *
 * Preconditions (caller must guarantee):
 * - `from_block + count == g_cache.start_number` (contiguous prepend)
 * - `g_cache.blocks_count + count <= g_cache.blocks_limit`
 *
 * The ring buffer is linearised (start_idx becomes 0) and the existing
 * entries are shifted to make room at the front.
 */
static void prepend_blocks(uint64_t from_block, uint32_t count) {
  if (!count) return;
  uint32_t old_count = g_cache.blocks_count;
  uint32_t new_count = old_count + count;
  if (new_count > g_cache.blocks_limit) return;

  // Linearise: copy existing entries into a contiguous temp array
  block_entry_t* linear = (block_entry_t*) safe_malloc(new_count * sizeof(block_entry_t));
  memset(linear, 0, (size_t) count * sizeof(block_entry_t));
  for (uint32_t i = 0; i < old_count; i++) {
    uint32_t src      = (g_cache.start_idx + i) % old_count;
    linear[count + i] = g_cache.blocks[src];
  }

  safe_free(g_cache.blocks);
  g_cache.blocks       = linear;
  g_cache.blocks_count = new_count;
  g_cache.start_idx    = 0;
  g_cache.start_number = from_block;
}

/**
 * Parses a `bloomFilter` JSON array of hex strings into a flat bloom buffer.
 * Each entry is expected to be a 256-byte hex string representing one bloom variant.
 *
 * @param bloom_json  JSON array of hex-encoded bloom filters.
 * @param out_blooms  Receives n*256 bytes (one bloom per entry).
 * @return Number of valid 256-byte blooms parsed, or 0 on failure / empty.
 */
static int parse_bloom_filter_array(json_t bloom_json, bytes_t* out_blooms) {
  if (bloom_json.type != JSON_TYPE_ARRAY) return 0;
  int count = json_len(bloom_json);
  if (count <= 0 || count > C4_ETH_BLOOM_MAX_VARIANTS) return 0;
  uint8_t* buf      = (uint8_t*) safe_calloc((size_t) count, 256);
  int      n        = 0;
  uint8_t  tmp[256] = {0};
  buffer_t b        = stack_buffer(tmp);
  json_for_each_value(bloom_json, entry) {
    if (n >= count) break;
    buffer_reset(&b);
    bytes_t v = json_as_bytes(entry, &b);
    if (v.len == 256) {
      memcpy(buf + (size_t) n * 256, v.data, 256);
      n++;
    }
  }
  if (n > 0) {
    *out_blooms = bytes(buf, (uint32_t) (n * 256));
  }
  else {
    safe_free(buf);
  }
  return n;
}

/**
 * Attempts to backfill older blocks into the cache so that
 * `[st->from_block, st->to_block]` is covered.  Only blocks *before* the
 * current cache start are loaded; the newest blocks are never evicted.
 * The total cache size must stay within `blocks_limit`.
 *
 * @param ctx Prover context (for async receipt fetching).
 * @param st  Request-local log cache state with resolved block range.
 * @return `C4_SUCCESS` when backfill is complete and the range is cached,
 *         `C4_PENDING` while receipts are still being fetched,
 *         `C4_ERROR`   if the range cannot be covered (too old / too large).
 */
static c4_status_t backfill_cache(prover_ctx_t* ctx, log_cache_state_t* st) {
  // Already done in a previous call?
  if (st->backfill_done) return C4_SUCCESS;

  uint64_t cache_start = g_cache.start_number;
  uint64_t cache_end   = g_cache.start_number + g_cache.blocks_count; // exclusive

  // to_block must already be in the cache (kept fresh by head_update)
  if (st->to_block >= cache_end || g_cache.blocks_count == 0)
    THROW_ERROR_WITH("logs_cache: toBlock %l is beyond the cache end %l", st->to_block, cache_end ? cache_end - 1 : 0);

  // How many blocks can we add without exceeding the limit?
  uint32_t room           = g_cache.blocks_limit - g_cache.blocks_count;
  uint64_t effective_from = max64(st->from_block, cache_start > room ? cache_start - room : 0);

  if (effective_from >= cache_start) {
    // Nothing to backfill — range should already be in cache.
    // If it is not, the range is simply not coverable.
    if (!c4_eth_logs_cache_has_range(st->from_block, st->to_block))
      THROW_ERROR_WITH("logs_cache: requested range [%l, %l] exceeds cache capacity (%u blocks, oldest cached: %l)",
                       st->from_block, st->to_block, g_cache.blocks_limit, cache_start);
    st->backfill_done = true;
    return C4_SUCCESS;
  }

  if (effective_from > st->from_block)
    THROW_ERROR_WITH("logs_cache: requested fromBlock %l is too old, cache can only reach back to %l (limit %u)",
                     st->from_block, effective_from, g_cache.blocks_limit);

  uint32_t needed = (uint32_t) (cache_start - effective_from);

  // Allocate receipt pointers (once)
  if (!st->backfill_receipts) {
    st->backfill_from     = effective_from;
    st->backfill_count    = needed;
    st->backfill_receipts = (json_t*) safe_calloc(needed, sizeof(json_t));
  }

  // Request all missing block receipts in parallel
  c4_status_t status  = C4_SUCCESS;
  uint8_t     tmp[64] = {0};
  buffer_t    b       = stack_buffer(tmp);
  for (uint32_t i = 0; i < st->backfill_count; i++) {
    if (st->backfill_receipts[i].start) continue; // already loaded
    buffer_reset(&b);
    TRY_ADD_ASYNC(status, eth_getBlockReceipts(ctx, json_parse(bprintf(&b, "\"0x%lx\"", st->backfill_from + i)), &st->backfill_receipts[i]));
  }
  TRY_ASYNC(status);

  // Re-validate preconditions: cache may have changed during async receipt fetch
  cache_start = g_cache.start_number;
  if (st->backfill_from + st->backfill_count != cache_start ||
      st->backfill_count > g_cache.blocks_limit - g_cache.blocks_count)
    THROW_ERROR_WITH("logs_cache: cache changed during backfill (expected start %l, got %l)", st->backfill_from + st->backfill_count, cache_start);

  prepend_blocks(st->backfill_from, st->backfill_count);
  for (uint32_t i = 0; i < st->backfill_count; i++) {
    uint8_t block_bloom[256];
    compute_block_bloom_from_receipts(st->backfill_receipts[i], block_bloom);
    c4_eth_logs_cache_add_block(st->backfill_from + i, block_bloom, st->backfill_receipts[i]);
  }
  st->backfill_done = true;
  log_info("logs_cache: backfilled %u blocks [%l, %l)", st->backfill_count, st->backfill_from, st->backfill_from + st->backfill_count);
  return C4_SUCCESS;
}

/**
 * Scans the logs cache for matches against the filter.
 *
 * This function operates in multiple phases across async calls:
 * 1. Range resolution (fromBlock/toBlock to numbers).
 * 2. Filter build: parse `bloomFilter` array (PAP) or address/topics.
 * 3. Backfill: load missing older blocks into the cache if needed.
 * 4. Match index: scan cached blocks/events to find matches.
 * 5. Receipt fetch: ensure receipts for matched blocks.
 * 6. Result build: assemble final JSON.
 *
 * When `bloomFilter` is present in the filter (PAP / Pragmatic Adaptive Privacy mode), all events of
 * bloom-matching blocks are returned (no address/topics filtering) and
 * a missing cache range causes `C4_ERROR` instead of a silent fallback.
 *
 * @param ctx The prover context.
 * @param filter The filter JSON object (fromBlock, toBlock, address, topics, or bloomFilter).
 * @param out_logs Output pointer for the logs JSON array.
 * @param served_from_cache Output flag, set to true if served from cache.
 * @return C4_SUCCESS or error status.
 */
c4_status_t c4_eth_logs_cache_scan(prover_ctx_t* ctx, json_t filter, json_t* out_logs, bool* served_from_cache) {
  if (served_from_cache) *served_from_cache = false;
  if (!c4_eth_logs_cache_is_enabled()) return C4_SUCCESS;

  log_cache_state_t* st = get_log_state(ctx);

  // If result already built, return it
  if (st->result.start) {
    *out_logs = st->result;
    if (served_from_cache) *served_from_cache = true;
    return C4_SUCCESS;
  }

  // Resolve numeric block range (persisted across async calls)
  if (!st->resolved) {
    TRY_ASYNC(get_exec_blocknumber(ctx, json_get(filter, "fromBlock"), &st->from_block));
    TRY_ASYNC(get_exec_blocknumber(ctx, json_get(filter, "toBlock"), &st->to_block));
    if (st->from_block > st->to_block)
      THROW_ERROR_WITH("Invalid block range: fromBlock %l > toBlock %l", st->from_block, st->to_block);

    // Detect PAP bloom-only mode: bloomFilter array in the filter object
    json_t bf = json_get(filter, "bloomFilter");
    if (bf.type == JSON_TYPE_ARRAY) {
      int n = parse_bloom_filter_array(bf, &st->filter_blooms);
      if (n > 0)
        st->bloom_only = true;
      else
        THROW_ERROR_WITH("bloomFilter array is empty or contains invalid entries");
    }
    st->resolved = true;
  }

  // Check if the requested range is already covered by the cache
  if (!c4_eth_logs_cache_has_range(st->from_block, st->to_block)) {
    if (st->bloom_only) {
      // PAP mode: attempt backfill, error if impossible
      TRY_ASYNC(backfill_cache(ctx, st));
    }
    else {
      // Normal mode: signal cache miss so the caller falls back to RPC
      if (!st->miss_counted) {
        g_metrics.misses++;
        st->miss_counted = 1;
      }
      return C4_SUCCESS;
    }
  }

  // Build filter (addresses/topics/bloom variants) and match index on first pass
  if (!st->blocks) {
    if (!st->bloom_only && st->filter_blooms.len == 0 && !st->filter_addresses.len) {
      c4_eth_parse_filter_addresses(json_get(filter, "address"), &st->filter_addresses);
      c4_eth_parse_filter_topics(json_get(filter, "topics"), st->filter_topics);
      st->filter_blooms = c4_eth_create_bloomfilter(filter);
    }
    build_match_index(st);
    // No matches -> empty result immediately
    if (!st->blocks) {
      st->result_owner = strdup("[]");
      st->result       = json_parse(st->result_owner);
      *out_logs        = st->result;
      if (served_from_cache) *served_from_cache = true;
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

#endif // PROVER_CACHE
