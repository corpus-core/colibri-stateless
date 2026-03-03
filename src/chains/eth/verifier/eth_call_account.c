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

#include "eth_call_account.h"
#include "plugin.h"
#include <string.h>

#define CACHE_VERSION 1

// :: Linked-list helpers

call_account_t* call_account_list_find(call_account_t* list, const address_t addr) {
  for (call_account_t* n = list; n; n = n->next)
    if (memcmp(n->address, addr, 20) == 0) return n;
  return NULL;
}

RETURNS_NONNULL call_account_t* call_account_list_get_or_create(call_account_t** list, const address_t addr) {
  call_account_t* n = call_account_list_find(*list, addr);
  if (n) return n;
  n = safe_calloc(1, sizeof(call_account_t));
  memcpy(n->address, addr, 20);
  n->next = *list;
  *list   = n;
  return n;
}

// :: Memory management

void call_storage_free_list(call_storage_t* s) {
  while (s) {
    call_storage_t* next = s->next;
    safe_free(s);
    s = next;
  }
}

void call_account_free(call_account_t* acc) {
  call_storage_free_list(acc->storage);
  if (acc->flags & ACCOUNT_FREE_CODE) safe_free(acc->code.data);
  safe_free(acc);
}

void call_account_free_list(call_account_t* list) {
  while (list) {
    call_account_t* next = list->next;
    call_account_free(list);
    list = next;
  }
}

// :: Storage slot helpers

void call_account_set_storage(call_account_t* account, const bytes32_t key, const bytes32_t value, storage_source_t source, uint64_t verified_at) {
  for (call_storage_t* s = account->storage; s; s = s->next) {
    if (memcmp(s->key, key, 32) == 0) {
      memcpy(s->src_value, value, 32);
      memcpy(s->post_value, value, 32);
      s->verified_at = verified_at;
      s->source      = source;
      s->modified    = false;
      return;
    }
  }
  call_storage_t* s = safe_calloc(1, sizeof(call_storage_t));
  memcpy(s->key, key, 32);
  memcpy(s->src_value, value, 32);
  memcpy(s->post_value, value, 32);
  s->source        = source;
  s->verified_at   = verified_at;
  s->next          = account->storage;
  account->storage = s;
}

// :: Access tracking helpers

void call_account_reset_accessed(call_account_t* list) {
  for (call_account_t* n = list; n; n = n->next) {
    n->flags &= ~ACCOUNT_ACCESSED;
    for (call_storage_t* s = n->storage; s; s = s->next) {
      s->accessed = false;
      s->modified = false;
      memcpy(s->post_value, s->src_value, 32);
    }
  }
}

// :: Serialization helpers

void eth_call_account_serialize(buffer_t* out, const call_account_t* account) {
  uint8_t tmp8[8];
  uint8_t tmp4[4];

  uint8_t version = CACHE_VERSION;
  buffer_append(out, bytes(&version, 1));

  uint32_to_le(tmp4, account->flags);
  buffer_append(out, bytes(tmp4, 4));

  uint64_to_le(tmp8, account->nonce);
  buffer_append(out, bytes(tmp8, 8));

  uint64_to_le(tmp8, account->verified_at);
  buffer_append(out, bytes(tmp8, 8));

  buffer_append(out, bytes((uint8_t*) account->storage_root, 32));
  buffer_append(out, bytes((uint8_t*) account->balance, 32));
  buffer_append(out, bytes((uint8_t*) account->code_hash, 32));

  uint32_t num_storage = 0;
  for (call_storage_t* s = account->storage; s; s = s->next) num_storage++;
  uint32_to_le(tmp4, num_storage);
  buffer_append(out, bytes(tmp4, 4));

  for (call_storage_t* s = account->storage; s; s = s->next) {
    buffer_append(out, bytes((uint8_t*) s->key, 32));
    buffer_append(out, bytes((uint8_t*) s->src_value, 32));
    uint8_t src = (uint8_t) s->source;
    buffer_append(out, bytes(&src, 1));
    uint64_to_le(tmp8, s->verified_at);
    buffer_append(out, bytes(tmp8, 8));
  }
}

bool eth_call_account_deserialize(bytes_t data, call_account_t* out) {
  // Minimum: 1 + 4 + 8 + 8 + 32 + 32 + 32 + 4 = 121 bytes
  if (data.len < 121) return false;

  uint8_t* p       = data.data;
  uint8_t  version = *p++;
  if (version != CACHE_VERSION) return false;

  out->flags = uint32_from_le(p);
  p += 4;
  out->nonce = uint64_from_le(p);
  p += 8;
  out->verified_at = uint64_from_le(p);
  p += 8;
  memcpy(out->storage_root, p, 32);
  p += 32;
  memcpy(out->balance, p, 32);
  p += 32;
  memcpy(out->code_hash, p, 32);
  p += 32;

  uint32_t num_storage = uint32_from_le(p);
  p += 4;

  uint32_t expected_len = 121 + num_storage * 73;
  if (data.len < expected_len) return false;

  call_storage_t** sp = &out->storage;
  for (uint32_t i = 0; i < num_storage; i++) {
    call_storage_t* s = safe_calloc(1, sizeof(call_storage_t));
    memcpy(s->key, p, 32);
    p += 32;
    memcpy(s->src_value, p, 32);
    memcpy(s->post_value, p, 32);
    p += 32;
    s->source      = (storage_source_t) (*p++);
    s->verified_at = uint64_from_le(p);
    p += 8;
    *sp = s;
    sp  = &s->next;
  }
  return true;
}

// :: Cache key helpers

static void build_cache_key(buffer_t* buf, chain_id_t chain_id, const address_t addr) {
  bprintf(buf, "call_%l_%x", chain_id, bytes((uint8_t*) addr, 20));
}

// :: Cache load / save

call_account_t* eth_call_account_cache_load(verify_ctx_t* ctx, const address_t addr) {
  storage_plugin_t cache = {0};
  c4_get_storage_config(&cache);
  if (!cache.get) return NULL;

  char     tmp[80];
  buffer_t key_buf = stack_buffer(tmp);
  build_cache_key(&key_buf, ctx->chain_id, addr);

  buffer_t data = {0};
  if (!cache.get((char*) key_buf.data.data, &data)) return NULL;

  call_account_t* account = safe_calloc(1, sizeof(call_account_t));
  memcpy(account->address, addr, 20);
  if (!eth_call_account_deserialize(data.data, account)) {
    buffer_free(&data);
    call_account_free(account);
    return NULL;
  }
  buffer_free(&data);
  return account;
}

void eth_call_account_cache_save(verify_ctx_t* ctx, const address_t addr, call_account_t* account) {
  storage_plugin_t cache = {0};
  c4_get_storage_config(&cache);
  if (!cache.set) return;

  char     tmp[80];
  buffer_t key_buf = stack_buffer(tmp);
  build_cache_key(&key_buf, ctx->chain_id, addr);

  // Merge with existing disk state so parallel callers don't discard each other's slots.
  call_account_t disk      = {0};
  buffer_t       disk_buf  = {0};
  bool           have_disk = cache.get && cache.get((char*) key_buf.data.data, &disk_buf) && eth_call_account_deserialize(disk_buf.data, &disk);
  buffer_free(&disk_buf);

  if (have_disk) {
    if (disk.verified_at > account->verified_at) {
      memcpy(account->storage_root, disk.storage_root, 32);
      memcpy(account->balance, disk.balance, 32);
      memcpy(account->code_hash, disk.code_hash, 32);
      account->verified_at = disk.verified_at;
      account->flags |= disk.flags & (ACCOUNT_HAS_BALANCE | ACCOUNT_HAS_CODE_HASH | ACCOUNT_HAS_STORAGE_ROOT | ACCOUNT_HAS_NONCE);
    }
    for (call_storage_t* ds = disk.storage; ds; ds = ds->next) {
      call_storage_t* as = call_storage_find(account, ds->key);
      if (as) {
        if (ds->verified_at > as->verified_at) {
          memcpy(as->src_value, ds->src_value, 32);
          memcpy(as->post_value, ds->src_value, 32);
          as->verified_at = ds->verified_at;
          as->source      = ds->source;
        }
      }
      else
        call_account_set_storage(account, ds->key, ds->src_value, ds->source, ds->verified_at);
    }
    call_storage_free_list(disk.storage);
  }

  buffer_t data_buf = {0};
  eth_call_account_serialize(&data_buf, account);
  cache.set((char*) key_buf.data.data, data_buf.data);
  buffer_free(&data_buf);
}
