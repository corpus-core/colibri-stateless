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

#ifdef PAP

#include "pap_tx_cache.h"
#include "bytes.h"
#include "pap_tx_cache_types.h"
#include "plugin.h"
#include "ssz.h"
#include <string.h>
#include <time.h>

/* ── tx cache ── */

typedef struct {
  chain_id_t chain_id;
  bytes_t    ssz_data;
  ssz_ob_t   snapshot;
  uint64_t   max_block;
  uint64_t   last_updated;
} pap_cache_t;

static pap_cache_t g_cache = {0};

static void cache_free(void) {
  safe_free(g_cache.ssz_data.data);
  g_cache = (pap_cache_t) {0};
}

static void tx_cache_storage_key(chain_id_t chain_id, char* buf, size_t buf_len) {
  // bprintf instead of snprintf so embedded/verifier-only builds stay free of
  // the libc printf family. The buffer is treated as fixed-size (negative
  // `allocated`); bprintf clamps + NUL-terminates within `buf_len`.
  buffer_t b = (buffer_t) {.data = bytes((uint8_t*) buf, 0), .allocated = -(int32_t) buf_len};
  bprintf(&b, "tx_cache_%d", (uint32_t) chain_id);
}

static bool parse_snapshot(bytes_t ssz_data) {
  if (!ssz_data.data || ssz_data.len == 0) return false;
  g_cache.snapshot = (ssz_ob_t) {.bytes = ssz_data, .def = &PAP_TX_CACHE_SNAPSHOT};
  return ssz_len(g_cache.snapshot) > 0;
}

static void update_max_block(void) {
  g_cache.max_block = 0;
  uint32_t n        = ssz_len(g_cache.snapshot);
  for (uint32_t i = 0; i < n; i++) {
    ssz_ob_t block = ssz_at(g_cache.snapshot, i);
    uint64_t bn    = ssz_get_uint64(&block, "block_number");
    if (bn > g_cache.max_block) g_cache.max_block = bn;
  }
}

bool pap_tx_cache_load(chain_id_t chain_id) {
  if (g_cache.ssz_data.data && g_cache.chain_id == chain_id)
    return true;

  if (g_cache.chain_id != chain_id)
    cache_free();

  storage_plugin_t plugin = {0};
  c4_get_storage_config(&plugin);
  if (!plugin.get) return false;

  char     key[32] = {0};
  buffer_t buf     = {0};
  tx_cache_storage_key(chain_id, key, sizeof(key));

  if (!plugin.get(key, &buf) || buf.data.len == 0) {
    buffer_free(&buf);
    return false;
  }

  g_cache.chain_id = chain_id;
  g_cache.ssz_data = buf.data;

  if (!parse_snapshot(g_cache.ssz_data)) {
    cache_free();
    return false;
  }

  update_max_block();
  g_cache.last_updated = (uint64_t) time(NULL);
  return true;
}

void pap_tx_cache_populate_from_ssz(chain_id_t chain_id, bytes_t ssz_data) {
  cache_free();

  if (!ssz_data.data || ssz_data.len == 0 || ssz_data.len > PAP_TX_CACHE_MAX_SSZ_SIZE) return;

  g_cache.chain_id = chain_id;
  g_cache.ssz_data = bytes_dup(ssz_data);

  if (!parse_snapshot(g_cache.ssz_data)) {
    cache_free();
    return;
  }

  update_max_block();
  g_cache.last_updated = (uint64_t) time(NULL);

  storage_plugin_t plugin = {0};
  c4_get_storage_config(&plugin);
  if (plugin.set) {
    char key[32] = {0};
    tx_cache_storage_key(chain_id, key, sizeof(key));
    plugin.set(key, ssz_data);
  }
}

bool pap_tx_cache_get(chain_id_t chain_id, bytes32_t tx_hash,
                      uint64_t* block_number, uint32_t* tx_index) {
  if (!g_cache.ssz_data.data || g_cache.chain_id != chain_id) return false;

  uint32_t num_blocks = ssz_len(g_cache.snapshot);
  for (uint32_t b = 0; b < num_blocks; b++) {
    ssz_ob_t block   = ssz_at(g_cache.snapshot, b);
    uint64_t blk_num = ssz_get_uint64(&block, "block_number");
    ssz_ob_t hashes  = ssz_get(&block, "tx_hashes");
    uint32_t num_txs = ssz_len(hashes);
    for (uint32_t t = 0; t < num_txs; t++) {
      ssz_ob_t h = ssz_at(hashes, t);
      if (h.bytes.len == 32 && memcmp(h.bytes.data, tx_hash, 32) == 0) {
        if (block_number) *block_number = blk_num;
        if (tx_index) *tx_index = t;
        return true;
      }
    }
  }
  return false;
}

bool pap_tx_cache_is_loaded(chain_id_t chain_id) {
  return g_cache.ssz_data.data && g_cache.chain_id == chain_id;
}

void pap_tx_cache_reset(void) {
  cache_free();
}

static void append_block_to_builder(ssz_builder_t* list, ssz_ob_t block, uint32_t* count) {
  ssz_builder_t bb = ssz_builder_for_def(&PAP_TX_CACHE_BLOCK);
  ssz_add_uint64(&bb, ssz_get_uint64(&block, "block_number"));
  ssz_add_bytes(&bb, "tx_hashes", ssz_get(&block, "tx_hashes").bytes);
  ssz_add_dynamic_list_builders(list, 0, bb);
  (*count)++;
}

void pap_tx_cache_merge_from_ssz(chain_id_t chain_id, bytes_t ssz_data) {
  if (!ssz_data.data || ssz_data.len == 0 || ssz_data.len > PAP_TX_CACHE_MAX_SSZ_SIZE) return;

  if (!g_cache.ssz_data.data || g_cache.chain_id != chain_id) {
    pap_tx_cache_populate_from_ssz(chain_id, ssz_data);
    return;
  }

  ssz_ob_t new_snapshot = {.bytes = ssz_data, .def = &PAP_TX_CACHE_SNAPSHOT};
  uint32_t new_len      = ssz_len(new_snapshot);
  if (new_len == 0) {
    g_cache.last_updated = (uint64_t) time(NULL);
    return;
  }

  ssz_builder_t merged = ssz_builder_for_def(&PAP_TX_CACHE_SNAPSHOT);
  uint32_t      count  = 0;

  uint32_t old_len = ssz_len(g_cache.snapshot);
  for (uint32_t i = 0; i < old_len; i++) {
    ssz_ob_t block = ssz_at(g_cache.snapshot, i);
    uint64_t bn    = ssz_get_uint64(&block, "block_number");

    bool dup = false;
    for (uint32_t j = 0; j < new_len; j++) {
      ssz_ob_t nb = ssz_at(new_snapshot, j);
      if (ssz_get_uint64(&nb, "block_number") == bn) {
        dup = true;
        break;
      }
    }
    if (dup) continue;

    append_block_to_builder(&merged, block, &count);
  }

  for (uint32_t j = 0; j < new_len; j++)
    append_block_to_builder(&merged, ssz_at(new_snapshot, j), &count);

  ssz_builder_fix_list_offsets(&merged, count);
  ssz_ob_t merged_ob = ssz_builder_to_bytes(&merged);

  if (merged_ob.bytes.len > PAP_TX_CACHE_MAX_SSZ_SIZE) {
    safe_free(merged_ob.bytes.data);
    return;
  }

  cache_free();
  g_cache.chain_id = chain_id;
  g_cache.ssz_data = merged_ob.bytes;

  if (!parse_snapshot(g_cache.ssz_data)) {
    cache_free();
    return;
  }

  update_max_block();
  g_cache.last_updated = (uint64_t) time(NULL);

  storage_plugin_t plugin = {0};
  c4_get_storage_config(&plugin);
  if (plugin.set) {
    char key[32] = {0};
    tx_cache_storage_key(chain_id, key, sizeof(key));
    plugin.set(key, g_cache.ssz_data);
  }
}

uint64_t pap_tx_cache_max_block(chain_id_t chain_id) {
  if (!g_cache.ssz_data.data || g_cache.chain_id != chain_id) return 0;
  return g_cache.max_block;
}

uint64_t pap_tx_cache_last_updated(chain_id_t chain_id) {
  if (!g_cache.ssz_data.data || g_cache.chain_id != chain_id) return 0;
  return g_cache.last_updated;
}

/* ── pending transaction list ── */

#define PAP_PENDING_MAX_ENTRIES 256
#define PAP_PENDING_ENTRY_SIZE  40 /* 32 (tx_hash) + 8 (timestamp LE) */

static void pending_storage_key(chain_id_t chain_id, char* buf, size_t buf_len) {
  buffer_t b = (buffer_t) {.data = bytes((uint8_t*) buf, 0), .allocated = -(int32_t) buf_len};
  bprintf(&b, "tx_pending_%d", (uint32_t) chain_id);
}

static ssz_ob_t pending_load_list(chain_id_t chain_id) {
  storage_plugin_t plugin = {0};
  c4_get_storage_config(&plugin);
  if (!plugin.get) return (ssz_ob_t) {0};

  char     key[32] = {0};
  buffer_t buf     = {0};
  pending_storage_key(chain_id, key, sizeof(key));

  if (!plugin.get(key, &buf) || buf.data.len == 0) {
    buffer_free(&buf);
    return (ssz_ob_t) {0};
  }

  if (buf.data.len > (uint32_t) PAP_PENDING_MAX_ENTRIES * PAP_PENDING_ENTRY_SIZE) {
    buffer_free(&buf);
    return (ssz_ob_t) {0};
  }

  bytes_t owned = bytes_dup(buf.data);
  buffer_free(&buf);
  return (ssz_ob_t) {.bytes = owned, .def = &PAP_PENDING_TX_LIST};
}

static void pending_save_list(chain_id_t chain_id, bytes_t ssz_data) {
  storage_plugin_t plugin = {0};
  c4_get_storage_config(&plugin);
  if (!plugin.set) return;

  char key[32] = {0};
  pending_storage_key(chain_id, key, sizeof(key));
  plugin.set(key, ssz_data);
}

static void pending_write_entry(buffer_t* buf, const bytes32_t tx_hash, uint64_t ts) {
  buffer_append(buf, bytes((uint8_t*) tx_hash, 32));
  uint8_t ts_le[8];
  uint64_to_le(ts_le, ts);
  buffer_append(buf, bytes(ts_le, 8));
}

void pap_tx_cache_add_pending(chain_id_t chain_id, bytes32_t tx_hash) {
  uint64_t now  = (uint64_t) time(NULL);
  ssz_ob_t list = pending_load_list(chain_id);

  buffer_t out   = {0};
  uint32_t count = 0;
  bool     found = false;

  uint32_t num = list.bytes.data ? ssz_len(list) : 0;
  for (uint32_t i = 0; i < num && count < PAP_PENDING_MAX_ENTRIES; i++) {
    ssz_ob_t entry = ssz_at(list, i);
    ssz_ob_t hash  = ssz_get(&entry, "tx_hash");
    uint64_t ts    = ssz_get_uint64(&entry, "timestamp");

    if (now - ts > PAP_PENDING_TX_TTL_S) continue;

    if (hash.bytes.len == 32 && memcmp(hash.bytes.data, tx_hash, 32) == 0) {
      found = true;
      pending_write_entry(&out, tx_hash, now);
    }
    else
      buffer_append(&out, entry.bytes);
    count++;
  }

  if (!found && count < PAP_PENDING_MAX_ENTRIES) {
    pending_write_entry(&out, tx_hash, now);
    count++;
  }

  safe_free(list.bytes.data);

  pending_save_list(chain_id, out.data);
  buffer_free(&out);
}

bool pap_tx_cache_is_pending(chain_id_t chain_id, bytes32_t tx_hash) {
  uint64_t now  = (uint64_t) time(NULL);
  ssz_ob_t list = pending_load_list(chain_id);
  if (!list.bytes.data) return false;

  uint32_t num = ssz_len(list);
  for (uint32_t i = 0; i < num; i++) {
    ssz_ob_t entry = ssz_at(list, i);
    ssz_ob_t hash  = ssz_get(&entry, "tx_hash");
    uint64_t ts    = ssz_get_uint64(&entry, "timestamp");
    if (now - ts > PAP_PENDING_TX_TTL_S) continue;
    if (hash.bytes.len == 32 && memcmp(hash.bytes.data, tx_hash, 32) == 0) {
      safe_free(list.bytes.data);
      return true;
    }
  }

  safe_free(list.bytes.data);
  return false;
}

void pap_tx_cache_remove_pending(chain_id_t chain_id, bytes32_t tx_hash) {
  uint64_t now  = (uint64_t) time(NULL);
  ssz_ob_t list = pending_load_list(chain_id);
  if (!list.bytes.data) return;

  buffer_t out = {0};
  uint32_t num = ssz_len(list);
  for (uint32_t i = 0; i < num; i++) {
    ssz_ob_t entry = ssz_at(list, i);
    ssz_ob_t hash  = ssz_get(&entry, "tx_hash");
    uint64_t ts    = ssz_get_uint64(&entry, "timestamp");

    if (now - ts > PAP_PENDING_TX_TTL_S) continue;
    if (hash.bytes.len == 32 && memcmp(hash.bytes.data, tx_hash, 32) == 0) continue;

    buffer_append(&out, entry.bytes);
  }

  safe_free(list.bytes.data);

  pending_save_list(chain_id, out.data);
  buffer_free(&out);
}

#endif /* PAP */
