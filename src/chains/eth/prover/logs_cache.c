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

#if defined(_MSC_VER)
#define C4_ALIGN8 __declspec(align(8))
#else
#define C4_ALIGN8 _Alignas(8)
#endif

typedef struct {
  address_t address;
  uint32_t  tx_index;
  uint32_t  log_index;
  uint8_t   topics_count;
  bytes32_t topics[4];
  // data field is not stored; we will read it from receipts when assembling results
} cached_event_t;

typedef struct block_entry_s {
  uint64_t              block_number;
  C4_ALIGN8 uint64_t    logs_bloom64[32]; // aligned storage for bloom
  cached_event_t*       events;
  uint32_t              events_count;
  uint32_t              events_cap;
  struct block_entry_s* prev;
  struct block_entry_s* next;
} block_entry_t;

static bool           g_enabled            = false;
static uint32_t       g_max_blocks         = 0; // capacity in blocks
static uint64_t       g_blocks_cached      = 0;
static uint64_t       g_total_events       = 0;
static uint64_t       g_total_txs          = 0; // we approximate by distinct tx_index per block when assembling
static uint64_t       g_hits_total         = 0;
static uint64_t       g_misses_total       = 0;
static uint64_t       g_bloom_skips_total  = 0;
static block_entry_t* g_head               = NULL; // oldest
static block_entry_t* g_tail               = NULL; // newest
static uint64_t       g_first_block_number = 0;
static uint64_t       g_last_block_number  = 0;

static void reset_cache(void) {
  block_entry_t* n = g_head;
  while (n) {
    block_entry_t* next = n->next;
    if (n->events) free(n->events);
    free(n);
    n = next;
  }
  g_head               = NULL;
  g_tail               = NULL;
  g_blocks_cached      = 0;
  g_total_events       = 0;
  g_total_txs          = 0;
  g_first_block_number = 0;
  g_last_block_number  = 0;
}

void c4_eth_logs_cache_enable(uint32_t max_blocks) {
  if (max_blocks == 0) {
    c4_eth_logs_cache_disable();
    return;
  }
  g_enabled    = true;
  g_max_blocks = max_blocks;
}

void c4_eth_logs_cache_disable(void) {
  g_enabled    = false;
  g_max_blocks = 0;
  reset_cache();
}

bool c4_eth_logs_cache_is_enabled(void) { return g_enabled && g_max_blocks > 0; }

static inline void bloom_set(uint8_t* bloom, uint64_t idx) { bloom[idx >> 3] |= (uint8_t) (1u << (idx & 7u)); }

static block_entry_t* push_block(uint64_t block_number) {
  block_entry_t* e = (block_entry_t*) calloc(1, sizeof(block_entry_t));
  if (!e) return NULL;
  e->block_number = block_number;
  e->prev         = g_tail;
  if (g_tail)
    g_tail->next = e;
  else
    g_head = e;
  g_tail = e;
  g_blocks_cached++;
  if (g_blocks_cached == 1) {
    g_first_block_number = block_number;
    g_last_block_number  = block_number;
  }
  else {
    g_last_block_number = block_number;
  }
  return e;
}

static void pop_head_block(void) {
  if (!g_head) return;
  block_entry_t* v = g_head;
  g_total_events -= v->events_count;
  if (v->events) free(v->events);
  g_head = v->next;
  if (g_head)
    g_head->prev = NULL;
  else
    g_tail = NULL;
  free(v);
  if (g_blocks_cached > 0) g_blocks_cached--;
  g_first_block_number = g_head ? g_head->block_number : 0;
  if (!g_head) g_last_block_number = 0;
}

static void ensure_capacity_for_new_block(void) {
  while (g_blocks_cached >= g_max_blocks && g_head) pop_head_block();
}

bool c4_eth_logs_cache_has_range(uint64_t from_block, uint64_t to_block) {
  if (!c4_eth_logs_cache_is_enabled() || g_blocks_cached == 0) return false;
  if (from_block > to_block) return false;
  if (g_first_block_number == 0 && g_last_block_number == 0) return false;
  // Require contiguous coverage
  return from_block >= g_first_block_number && to_block <= g_last_block_number &&
         (g_last_block_number - g_first_block_number + 1) == g_blocks_cached;
}

static void add_event(block_entry_t* e, address_t addr, uint32_t tx_index, uint32_t log_index, uint8_t topics_count, bytes32_t* topics) {
  if (e->events_count == e->events_cap) {
    uint32_t new_cap   = e->events_cap ? (e->events_cap * 2u) : 256u;
    void*    new_store = realloc(e->events, new_cap * sizeof(cached_event_t));
    if (!new_store) return;
    e->events     = (cached_event_t*) new_store;
    e->events_cap = new_cap;
  }
  cached_event_t* ev = &e->events[e->events_count++];
  memcpy(ev->address, addr, ADDRESS_SIZE);
  ev->tx_index     = tx_index;
  ev->log_index    = log_index;
  ev->topics_count = topics_count > 4 ? 4 : topics_count;
  for (uint8_t i = 0; i < ev->topics_count; i++) memcpy(ev->topics[i], topics[i], BYTES32_SIZE);
  g_total_events++;
}

void c4_eth_logs_cache_add_block(uint64_t block_number, const uint8_t* logs_bloom, json_t receipts_array) {
  if (!c4_eth_logs_cache_is_enabled()) return;

  // Enforce contiguity
  if (g_blocks_cached > 0 && block_number != g_last_block_number + 1) {
    log_warn("logs_cache: non-contiguous block detected (got %lu, expected %lu). Resetting cache.", block_number, g_last_block_number + 1);
    reset_cache();
  }

  ensure_capacity_for_new_block();
  block_entry_t* e = push_block(block_number);
  if (!e) return;
  memcpy(e->logs_bloom64, logs_bloom, 256);

  // Extract events minimally from receipts
  uint32_t tx_count_local = 0;
  json_for_each_value(receipts_array, r) {
    uint32_t tx_index = json_get_uint32(r, "transactionIndex");
    tx_count_local++;
    uint32_t li = 0;
    json_for_each_value(json_get(r, "logs"), log) {
      address_t addr = {0};
      {
        uint8_t  tmp[32]  = {0};
        buffer_t b        = stack_buffer(tmp);
        bytes_t  addr_raw = json_as_bytes(json_get(log, "address"), &b);
        if (addr_raw.len == ADDRESS_SIZE) memcpy(addr, addr_raw.data, ADDRESS_SIZE);
      }
      // topics
      bytes32_t topics_arr[4] = {0};
      uint8_t   tc            = 0;
      json_for_each_value(json_get(log, "topics"), t) {
        if (tc >= 4) break;
        uint8_t  tmp[32] = {0};
        buffer_t b       = stack_buffer(tmp);
        bytes_t  tv      = json_as_bytes(t, &b);
        if (tv.len == 32) {
          memcpy(topics_arr[tc], tv.data, 32);
          tc++;
        }
      }
      add_event(e, addr, tx_index, li, tc, topics_arr);
      li++;
    }
  }
  g_total_txs += tx_count_local;
}

static bool address_matches_filter(json_t filter, address_t addr) {
  json_t a = json_get(filter, "address");
  if (a.type == JSON_TYPE_INVALID || a.type == JSON_TYPE_NULL) return true;
  if (a.type == JSON_TYPE_STRING) {
    uint8_t  tmp[32]  = {0};
    buffer_t b        = stack_buffer(tmp);
    bytes_t  raw_addr = json_as_bytes(a, &b);
    return raw_addr.len == ADDRESS_SIZE && memcmp(raw_addr.data, addr, ADDRESS_SIZE) == 0;
  }
  if (a.type == JSON_TYPE_ARRAY) {
    json_for_each_value(a, av) {
      uint8_t  tmp[32]  = {0};
      buffer_t b        = stack_buffer(tmp);
      bytes_t  raw_addr = json_as_bytes(av, &b);
      if (raw_addr.len == ADDRESS_SIZE && memcmp(raw_addr.data, addr, ADDRESS_SIZE) == 0) return true;
    }
    return false;
  }
  return true;
}

static bool topics_position_match(json_t topic_pos, bytes32_t* ev_topics, uint8_t ev_tc) {
  if (topic_pos.type == JSON_TYPE_NULL) return true;
  if (topic_pos.type == JSON_TYPE_STRING) {
    if (ev_tc == 0) return false;
    uint8_t  tmp[32] = {0};
    buffer_t b       = stack_buffer(tmp);
    bytes_t  val     = json_as_bytes(topic_pos, &b);
    if (val.len != 32) return false;
    return memcmp(ev_topics[0], val.data, 32) == 0; // positionally first topic
  }
  if (topic_pos.type == JSON_TYPE_ARRAY) {
    // OR at this position; compare against ev_topics[pos]
    uint8_t  tmp[32] = {0};
    buffer_t b       = stack_buffer(tmp);
    // We will compare to the topic at this position: handled by caller
    // Here we just check if any matches the given candidate
    // This helper is used per position by caller.
    (void) b;
  }
  return true;
}

static bool event_matches_topics(json_t topics, cached_event_t* ev) {
  if (topics.type == JSON_TYPE_INVALID || topics.type == JSON_TYPE_NULL) return true;
  if (topics.type != JSON_TYPE_ARRAY) return true;
  // Each position is ANDed; each position may be null or array-of-topics (OR)
  uint32_t pos = 0;
  json_for_each_value(topics, tpos) {
    if (tpos.type == JSON_TYPE_NULL) {
      pos++;
      continue;
    }
    if (ev->topics_count <= pos) return false;
    if (tpos.type == JSON_TYPE_STRING) {
      uint8_t  tmp[32] = {0};
      buffer_t b       = stack_buffer(tmp);
      bytes_t  tv      = json_as_bytes(tpos, &b);
      if (tv.len != 32 || memcmp(ev->topics[pos], tv.data, 32) != 0) return false;
    }
    else if (tpos.type == JSON_TYPE_ARRAY) {
      bool any = false;
      json_for_each_value(tpos, cand) {
        uint8_t  tmp[32] = {0};
        buffer_t b       = stack_buffer(tmp);
        bytes_t  tv      = json_as_bytes(cand, &b);
        if (tv.len == 32 && memcmp(ev->topics[pos], tv.data, 32) == 0) {
          any = true;
          break;
        }
      }
      if (!any) return false;
    }
    pos++;
  }
  return true;
}

// (removed old bloom_prefilter_block; bloom_subset_of + exact match on cached events is used)

// --------- Result building structures to minimize recomputation across async calls ---------
typedef struct event_result_s {
  uint32_t               log_idx;
  struct event_result_s* next;
} event_result_t;

typedef struct tx_result_s {
  uint32_t            tx_idx;
  event_result_t*     events;
  struct tx_result_s* next;
} tx_result_t;

typedef struct block_result_s {
  uint64_t               block_number;
  json_t                 block_receipts; // filled when fetched
  tx_result_t*           txs;
  struct block_result_s* next;
} block_result_t;

// Extend request-local state to carry intermediate results and final JSON result
typedef struct {
  uint64_t        from_block;
  uint64_t        to_block;
  uint8_t         resolved;
  uint8_t         hit_counted;
  uint8_t         miss_counted;
  json_t          result;       // final logs array
  char*           result_owner; // owning pointer to result JSON string
  block_result_t* blocks;       // per-block matches (tx_idx + log_idx)
} log_cache_state_t_ext;

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

static void free_block_results(block_result_t* blocks) {
  while (blocks) {
    block_result_t* nb = blocks->next;
    free_tx_results(blocks->txs);
    free(blocks);
    blocks = nb;
  }
}

static void free_log_state_ext(void* ptr) {
  if (!ptr) return;
  log_cache_state_t_ext* st = (log_cache_state_t_ext*) ptr;
  if (st->result_owner) free(st->result_owner);
  free_block_results(st->blocks);
  free(st);
}

static log_cache_state_t_ext* get_log_state_ext(prover_ctx_t* ctx) {
  bytes32_t key = {0};
  memcpy(key, "log_state", 9);
  log_cache_state_t_ext* state = (log_cache_state_t_ext*) c4_prover_cache_get_local(ctx, key);
  if (!state) {
    state = (log_cache_state_t_ext*) safe_calloc(1, sizeof(log_cache_state_t_ext));
    c4_prover_cache_set(ctx, key, state, (uint32_t) sizeof(log_cache_state_t_ext), 0, free_log_state_ext);
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

// Build bloom from filter's address/topics and check subset of block bloom
static void bloom_add_element_buf(uint8_t bloom[256], bytes_t element) {
  bytes32_t h = {0};
  keccak(element, h);
  uint64_t idx1 = (((uint64_t) h[0] << 8) | h[1]) & 2047u;
  uint64_t idx2 = (((uint64_t) h[2] << 8) | h[3]) & 2047u;
  uint64_t idx3 = (((uint64_t) h[4] << 8) | h[5]) & 2047u;
  bloom_set(bloom, idx1);
  bloom_set(bloom, idx2);
  bloom_set(bloom, idx3);
}

static void build_filter_bloom(json_t filter, uint8_t bloom[256]) {
  memset(bloom, 0, 256);
  // address
  json_t a = json_get(filter, "address");
  if (a.type == JSON_TYPE_STRING) {
    uint8_t  tmp[32] = {0};
    buffer_t b       = stack_buffer(tmp);
    bytes_t  v       = json_as_bytes(a, &b);
    if (v.len == ADDRESS_SIZE) bloom_add_element_buf(bloom, v);
  }
  else if (a.type == JSON_TYPE_ARRAY) {
    json_for_each_value(a, av) {
      uint8_t  tmp[32] = {0};
      buffer_t b       = stack_buffer(tmp);
      bytes_t  v       = json_as_bytes(av, &b);
      if (v.len == ADDRESS_SIZE) bloom_add_element_buf(bloom, v);
    }
  }
  // topics positions: OR all given concrete topics
  json_t topics = json_get(filter, "topics");
  if (topics.type == JSON_TYPE_ARRAY) {
    json_for_each_value(topics, tpos) {
      if (tpos.type == JSON_TYPE_STRING) {
        uint8_t  tmp[32] = {0};
        buffer_t b       = stack_buffer(tmp);
        bytes_t  v       = json_as_bytes(tpos, &b);
        if (v.len == 32) bloom_add_element_buf(bloom, v);
      }
      else if (tpos.type == JSON_TYPE_ARRAY) {
        json_for_each_value(tpos, cand) {
          uint8_t  tmp[32] = {0};
          buffer_t b       = stack_buffer(tmp);
          bytes_t  v       = json_as_bytes(cand, &b);
          if (v.len == 32) bloom_add_element_buf(bloom, v);
        }
      }
    }
  }
}

static inline bool bloom_subset_of(const uint8_t small[256], const uint8_t big[256]) {
  // Compare in 64-bit chunks to reduce operations (32 ops vs 256 byte-wise ops)
  for (int i = 0; i < 32; i++) {
    uint64_t s = 0, b = 0;
    memcpy(&s, small + (i * 8), 8);
    memcpy(&b, big + (i * 8), 8);
    if ((s & ~b) != 0) return false;
  }
  return true;
}

static inline bool bloom_subset_of64(const uint64_t* small, const uint64_t* big) {
  for (int i = 0; i < 32; i++) {
    if ((small[i] & ~big[i]) != 0) return false;
  }
  return true;
}

// Phase 1: Build block/tx/event matches using cached per-block events (no RPC)
static void build_match_index(json_t filter, uint64_t from_block, uint64_t to_block, block_result_t** out_blocks) {
  _Alignas(8) uint64_t filter_bloom64[32] = {0};
  build_filter_bloom(filter, (uint8_t*) filter_bloom64);
  for (block_entry_t* e = g_head; e; e = e->next) {
    if (e->block_number < from_block || e->block_number > to_block) continue;
    if (!bloom_subset_of64(filter_bloom64, e->logs_bloom64)) {
      g_bloom_skips_total++;
      continue;
    }
    // Confirm by scanning cached events
    block_result_t* block_res = NULL;
    for (uint32_t i = 0; i < e->events_count; i++) {
      cached_event_t* ev = &e->events[i];
      if (!address_matches_filter(filter, ev->address)) continue;
      if (!event_matches_topics(json_get(filter, "topics"), ev)) continue;
      if (!block_res) block_res = add_block_result(out_blocks, e->block_number);
      tx_result_t* txr = ensure_tx_result(block_res, ev->tx_index);
      // append event
      event_result_t* er = (event_result_t*) safe_calloc(1, sizeof(event_result_t));
      er->log_idx        = ev->log_index;
      er->next           = txr->events;
      txr->events        = er;
    }
  }
}

// Phase 2: Ensure receipts fetched for selected blocks (async prefetch)
static c4_status_t ensure_receipts_for_matches(prover_ctx_t* ctx, block_result_t* blocks) {
  c4_status_t status  = C4_SUCCESS;
  uint8_t     tmp[64] = {0};
  buffer_t    b       = stack_buffer(tmp);
  for (block_result_t* br = blocks; br; br = br->next) {
    if (br->block_receipts.type == JSON_TYPE_INVALID || br->block_receipts.type == JSON_TYPE_NOT_FOUND || br->block_receipts.start == NULL) {
      buffer_reset(&b);
      json_t block_param = json_parse(bprintf(&b, "\"0x%lx\"", br->block_number));
      TRY_ADD_ASYNC(status, eth_getBlockReceipts(ctx, block_param, &br->block_receipts));
    }
  }
  return status;
}

// Phase 3: Build final JSON using fetched receipts and match index
static c4_status_t build_result_json_from_matches(prover_ctx_t* ctx, log_cache_state_t_ext* st, json_t filter, json_t* out_logs) {
  buffer_t out_buf = {0};
  bprintf(&out_buf, "[");
  bool first = true;
  for (block_result_t* br = st->blocks; br; br = br->next) {
    json_t receipts = br->block_receipts;
    if (receipts.type == JSON_TYPE_INVALID || receipts.type == JSON_TYPE_NOT_FOUND || receipts.start == NULL) continue;
    for (tx_result_t* tx = br->txs; tx; tx = tx->next) {
      json_t rxs = json_at(receipts, tx->tx_idx);
      if (rxs.type == JSON_TYPE_INVALID || rxs.type == JSON_TYPE_NOT_FOUND) continue;
      json_t logs = json_get(rxs, "logs");
      for (event_result_t* ev = tx->events; ev; ev = ev->next) {
        json_t logj = json_at(logs, ev->log_idx);
        if (logj.type == JSON_TYPE_INVALID || logj.type == JSON_TYPE_NOT_FOUND) continue;
        if (!first) buffer_add_chars(&out_buf, ",");
        buffer_add_json(&out_buf, logj);
        first = false;
      }
    }
  }
  buffer_add_chars(&out_buf, "]");
  // Persist result string until context end
  st->result_owner = (char*) out_buf.data.data;
  st->result       = json_parse(st->result_owner);
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

// (removed old minimal state struct; consolidated into log_cache_state_t_ext)

c4_status_t c4_eth_logs_cache_scan(prover_ctx_t* ctx, json_t filter, json_t* out_logs, bool* served_from_cache) {
  if (served_from_cache) *served_from_cache = false;
  if (!c4_eth_logs_cache_is_enabled()) {
    // do not count repeatedly; only count when enabled and range covered check fails
    return C4_SUCCESS;
  }
  // Resolve and persist the numeric range across async invocations
  log_cache_state_t_ext* st = get_log_state_ext(ctx);

  // If result already built, return it
  if (st->result.start) {
    *out_logs = st->result;
    if (served_from_cache) *served_from_cache = true;
    return C4_SUCCESS;
  }

  if (!st->resolved) {
    TRY_ASYNC(get_exec_blocknumber(ctx, json_get(filter, "fromBlock"), &st->from_block));
    TRY_ASYNC(get_exec_blocknumber(ctx, json_get(filter, "toBlock"), &st->to_block));
    st->resolved = 1;
  }

  if (st->from_block > st->to_block)
    THROW_ERROR_WITH("Invalid block range: fromBlock %l > toBlock %l", st->from_block, st->to_block);

  if (!c4_eth_logs_cache_has_range(st->from_block, st->to_block)) {
    if (!st->miss_counted) {
      g_misses_total++;
      st->miss_counted = 1;
    }
    return C4_SUCCESS;
  }

  // Build match index on first pass
  if (!st->blocks) {
    build_match_index(filter, st->from_block, st->to_block, &st->blocks);
    // No matches -> empty result immediately
    if (!st->blocks) {
      buffer_t out_buf = {0};
      bprintf(&out_buf, "[]");
      st->result_owner = (char*) out_buf.data.data;
      st->result       = json_parse(st->result_owner);
      *out_logs        = st->result;
      if (served_from_cache) *served_from_cache = true;
      // keep ownership to free at end
      if (!st->hit_counted) {
        g_hits_total++;
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
    g_hits_total++;
    st->hit_counted = 1;
  }
  return C4_SUCCESS;
}

void c4_eth_logs_cache_stats(uint64_t* blocks, uint64_t* txs, uint64_t* events) {
  if (blocks) *blocks = g_blocks_cached;
  if (txs) *txs = g_total_txs;
  if (events) *events = g_total_events;
}
void c4_eth_logs_cache_counters(uint64_t* hits, uint64_t* misses, uint64_t* bloom_skips) {
  if (hits) *hits = g_hits_total;
  if (misses) *misses = g_misses_total;
  if (bloom_skips) *bloom_skips = g_bloom_skips_total;
}
uint64_t c4_eth_logs_cache_first_block(void) { return g_first_block_number; }
uint64_t c4_eth_logs_cache_last_block(void) { return g_last_block_number; }
uint32_t c4_eth_logs_cache_capacity_blocks(void) { return g_max_blocks; }

#endif // PROVER_CACHE
