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
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include "eth_call_cache.h"
#include "bytes.h"
#include "plugin.h"
#include <string.h>

// :: Serialization helpers

void eth_call_cache_write(buffer_t* out, const cached_account_t* account) {
  uint8_t tmp8[8];
  uint8_t tmp4[4];

  // verified_at (8 bytes LE)
  uint64_to_le(tmp8, account->verified_at);
  buffer_append(out, bytes(tmp8, 8));

  // storage_root, balance, code_hash (32 bytes each)
  buffer_append(out, bytes((uint8_t*) account->storage_root, 32));
  buffer_append(out, bytes((uint8_t*) account->balance, 32));
  buffer_append(out, bytes((uint8_t*) account->code_hash, 32));

  // num_storage (4 bytes LE)
  uint32_to_le(tmp4, account->num_storage);
  buffer_append(out, bytes(tmp4, 4));

  // storage entries
  for (uint32_t i = 0; i < account->num_storage; i++) {
    const cached_storage_t* s = &account->storage[i];
    buffer_append(out, bytes((uint8_t*) s->key, 32));
    buffer_append(out, bytes((uint8_t*) s->value, 32));
    uint64_to_le(tmp8, s->verified_at);
    buffer_append(out, bytes(tmp8, 8));
  }
}

bool eth_call_cache_read(bytes_t data, cached_account_t* out) {
  // Minimum size: 8 + 32 + 32 + 32 + 4 = 108 bytes
  if (data.len < 108) return false;

  const uint8_t* p = data.data;

  out->verified_at = uint64_from_le(p);
  p += 8;
  memcpy(out->storage_root, p, 32);
  p += 32;
  memcpy(out->balance, p, 32);
  p += 32;
  memcpy(out->code_hash, p, 32);
  p += 32;
  out->num_storage = uint32_from_le(p);
  p += 4;

  uint32_t expected_len = 108 + out->num_storage * 72;
  if (data.len < expected_len) return false;

  if (out->num_storage > 0) {
    out->storage = safe_calloc(out->num_storage, sizeof(cached_storage_t));
    for (uint32_t i = 0; i < out->num_storage; i++) {
      cached_storage_t* s = &out->storage[i];
      memcpy(s->key, p, 32);
      p += 32;
      memcpy(s->value, p, 32);
      p += 32;
      s->verified_at = uint64_from_le(p);
      p += 8;
    }
  }
  return true;
}

// :: Cache key helpers

static void build_cache_key(buffer_t* buf, chain_id_t chain_id, const address_t addr) {
  bprintf(buf, "call_%l_%x", chain_id, bytes((uint8_t*) addr, 20));
}

// :: Cache load / save

cached_account_t* eth_call_cache_load(verify_ctx_t* ctx, const address_t addr) {
  storage_plugin_t cache = {0};
  c4_get_storage_config(&cache);
  if (!cache.get) return NULL;

  char     tmp[80];
  buffer_t key_buf = stack_buffer(tmp);
  build_cache_key(&key_buf, ctx->chain_id, addr);

  buffer_t data = {0};
  if (!cache.get((char*) key_buf.data.data, &data)) return NULL;

  cached_account_t* account = safe_calloc(1, sizeof(cached_account_t));
  memcpy(account->address, addr, 20);
  if (!eth_call_cache_read(data.data, account)) {
    buffer_free(&data);
    safe_free(account);
    return NULL;
  }
  buffer_free(&data);
  return account;
}

void eth_call_cache_save(verify_ctx_t* ctx, const address_t addr, cached_account_t* account) {
  storage_plugin_t cache = {0};
  c4_get_storage_config(&cache);
  if (!cache.set) return;

  char     tmp[80];
  buffer_t key_buf = stack_buffer(tmp);
  build_cache_key(&key_buf, ctx->chain_id, addr);

  // Merge with existing disk state so parallel callers don't discard each other's slots.
  cached_account_t disk     = {0};
  buffer_t         disk_buf = {0};
  bool have_disk = cache.get && cache.get((char*) key_buf.data.data, &disk_buf)
                   && eth_call_cache_read(disk_buf.data, &disk);
  buffer_free(&disk_buf);

  if (have_disk) {
    if (disk.verified_at > account->verified_at) {
      memcpy(account->storage_root, disk.storage_root, 32);
      memcpy(account->balance, disk.balance, 32);
      memcpy(account->code_hash, disk.code_hash, 32);
      account->verified_at = disk.verified_at;
    }
    for (uint32_t i = 0; i < disk.num_storage; i++) {
      bool found = false;
      for (uint32_t j = 0; j < account->num_storage; j++) {
        if (memcmp(account->storage[j].key, disk.storage[i].key, 32) == 0) {
          if (disk.storage[i].verified_at > account->storage[j].verified_at) {
            memcpy(account->storage[j].value, disk.storage[i].value, 32);
            account->storage[j].verified_at = disk.storage[i].verified_at;
          }
          found = true;
          break;
        }
      }
      if (!found)
        eth_call_cache_set_storage(account, disk.storage[i].key, disk.storage[i].value, disk.storage[i].verified_at);
    }
    safe_free(disk.storage);
  }

  buffer_t data_buf = {0};
  eth_call_cache_write(&data_buf, account);
  cache.set((char*) key_buf.data.data, data_buf.data);
  buffer_free(&data_buf);
}

// :: Linked-list helpers

cached_account_t* eth_call_cache_find(cached_account_t* list, const address_t addr) {
  for (cached_account_t* n = list; n; n = n->next) {
    if (memcmp(n->address, addr, 20) == 0) return n;
  }
  return NULL;
}

cached_account_t* eth_call_cache_get_or_create(cached_account_t** list, const address_t addr) {
  cached_account_t* n = eth_call_cache_find(*list, addr);
  if (n) return n;
  n = safe_calloc(1, sizeof(cached_account_t));
  memcpy(n->address, addr, 20);
  n->next = *list;
  *list   = n;
  return n;
}

// :: Storage slot helpers

bool eth_call_cache_get_storage(const cached_account_t* account, const bytes32_t key, bytes32_t value_out) {
  for (uint32_t i = 0; i < account->num_storage; i++) {
    if (memcmp(account->storage[i].key, key, 32) == 0) {
      memcpy(value_out, account->storage[i].value, 32);
      return true;
    }
  }
  return false;
}

void eth_call_cache_set_storage(cached_account_t* account, const bytes32_t key, const bytes32_t value, uint64_t verified_at) {
  for (uint32_t i = 0; i < account->num_storage; i++) {
    if (memcmp(account->storage[i].key, key, 32) == 0) {
      memcpy(account->storage[i].value, value, 32);
      account->storage[i].verified_at = verified_at;
      return;
    }
  }
  // New entry
  account->storage    = safe_realloc(account->storage, (account->num_storage + 1) * sizeof(cached_storage_t));
  cached_storage_t* s = &account->storage[account->num_storage];
  memset(s, 0, sizeof(cached_storage_t));
  memcpy(s->key, key, 32);
  memcpy(s->value, value, 32);
  s->verified_at = verified_at;
  account->num_storage++;
}

// :: Access tracking helpers

void eth_call_cache_reset_accessed(cached_account_t* list) {
  for (cached_account_t* n = list; n; n = n->next)
    for (uint32_t i = 0; i < n->num_storage; i++)
      n->storage[i].accessed = false;
}

void eth_call_cache_mark_accessed(cached_account_t* account, const bytes32_t key) {
  for (uint32_t i = 0; i < account->num_storage; i++) {
    if (memcmp(account->storage[i].key, key, 32) == 0) {
      account->storage[i].accessed = true;
      return;
    }
  }
}

// :: Memory management

void eth_call_cache_free(cached_account_t* account) {
  if (!account) return;
  safe_free(account->storage);
  safe_free(account);
}

void eth_call_cache_free_list(cached_account_t* list) {
  while (list) {
    cached_account_t* next = list->next;
    eth_call_cache_free(list);
    list = next;
  }
}

// :: pap_call_state_t lifecycle

void pap_call_state_free(void* ptr) {
  if (!ptr) return;
  pap_call_state_t* state = (pap_call_state_t*) ptr;
  eth_call_cache_free_list(state->accounts);
  safe_free(state->call_result.data);
  free_emitted_logs(state->logs);
  safe_free(state);
}
