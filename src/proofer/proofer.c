#include "proofer.h"
#include "../util/json.h"
#include "../util/state.h"
#include PROOFERS_PATH
#include "logger.h"
#include <stdlib.h>
#include <string.h>

#ifdef PROOFER_CACHE
#if defined(_WIN32) || defined(WIN32)
#include <windows.h>
#else
#include <sys/time.h>
#endif

uint64_t current_ms() {
  struct timeval te;
#ifdef _WIN32
  FILETIME       ft;
  ULARGE_INTEGER li;
  GetSystemTimeAsFileTime(&ft);
  li.LowPart  = ft.dwLowDateTime;
  li.HighPart = ft.dwHighDateTime;
  // Convert to microseconds from Jan 1, 1601
  // Then adjust to Unix epoch (Jan 1, 1970)
  uint64_t unix_time = (li.QuadPart - 116444736000000000LL) / 10;
  te.tv_sec          = unix_time / 1000000;
  te.tv_usec         = unix_time % 1000000;
#else
  gettimeofday(&te, NULL);
#endif
  return te.tv_sec * 1000L + te.tv_usec / 1000;
}

static cache_entry_t* global_cache          = NULL;
static uint64_t       global_cache_max_size = 1024 * 1024 * 100; // 10MB

void* c4_proofer_cache_get(proofer_ctx_t* ctx, bytes32_t key) {
  uint64_t key_start = *((uint64_t*) key); // optimize cache-loopus by first checking the first word before doing a memcmp
  for (cache_entry_t* entry = ctx->cache; entry; entry = entry->next) {
    if (*((uint64_t*) entry->key) == key_start && memcmp(entry->key, key, 32) == 0) return entry->value;
  }
  // if we are running in the worker-thread, we don't access the global cache anymore
  if (ctx->flags & C4_PROOFER_FLAG_UV_WORKER_REQUIRED) {
    log_warn("[CACHEMISS] trying to access the global cache with cachekey %x, but we are running in the worker-thread. Make sure you tried to access in queue thread first!", bytes(key, 32));
    return NULL;
  }

  for (cache_entry_t* entry = global_cache; entry; entry = entry->next) {
    if (*((uint64_t*) entry->key) == key_start && memcmp(entry->key, key, 32) == 0) {
      cache_entry_t* new_entry = calloc(1, sizeof(cache_entry_t));
      *new_entry               = *entry;
      new_entry->timestamp     = 0;
      new_entry->next          = ctx->cache;
      new_entry->src           = entry;
      ctx->cache               = new_entry;
      entry->use_counter++;
      return new_entry->value;
    }
  }
  return NULL;
}

void c4_proofer_cache_set(proofer_ctx_t* ctx, bytes32_t key, void* value, uint32_t size, uint64_t ttl, cache_free_cb free) {
  cache_entry_t* entry = calloc(1, sizeof(cache_entry_t));
  memcpy(entry->key, key, 32);
  entry->value     = value;
  entry->size      = size;
  entry->timestamp = ttl;
  entry->free      = free;
  entry->next      = ctx->cache;
  ctx->cache       = entry;
}
void c4_proofer_cache_cleanup(uint64_t now) {
  uint64_t        size = 0;
  cache_entry_t** prev = &global_cache;
  for (cache_entry_t* entry = *prev; entry; prev = &((*prev)->next), entry = *prev) {
    if ((entry->timestamp < now || size + entry->size > global_cache_max_size) && entry->use_counter == 0) {
      cache_entry_t* next = entry->next;
      if (entry->free) entry->free(entry->value);
      free(entry);
      *prev = next;
      continue;
    }
    size += entry->size;
  }
}

#endif

proofer_ctx_t* c4_proofer_create(char* method, char* params, chain_id_t chain_id, proofer_flags_t flags) {
  json_t params_json = json_parse(params);
  if (params_json.type != JSON_TYPE_ARRAY) return NULL;
  char* params_str = malloc(params_json.len + 1);
  memcpy(params_str, params_json.start, params_json.len);
  params_str[params_json.len] = 0;
  params_json.start           = params_str;
  proofer_ctx_t* ctx          = calloc(1, sizeof(proofer_ctx_t));
  ctx->method                 = strdup(method);
  ctx->params                 = params_json;
  ctx->chain_id               = chain_id;
  ctx->flags                  = flags;
  return ctx;
}
#ifdef PROOFER_CACHE
static cache_entry_t* find_cache_entry(cache_entry_t* cache, bytes32_t key) {
  uint64_t key_start = *((uint64_t*) key); // optimize cache-loopus by first checking the first word before doing a memcmp
  for (cache_entry_t* entry = cache; entry; entry = entry->next) {
    if (*((uint64_t*) entry->key) == key_start && memcmp(entry->key, key, 32) == 0) return entry;
  }
  return NULL;
}
#endif
void c4_proofer_free(proofer_ctx_t* ctx) {
  c4_state_free(&ctx->state);
  if (ctx->method) free(ctx->method);
  if (ctx->params.start) free((void*) ctx->params.start);
  if (ctx->proof.data) free(ctx->proof.data);
#ifdef PROOFER_CACHE
  while (ctx->cache) {
    cache_entry_t* next = ctx->cache->next;
    if (ctx->cache->timestamp && !ctx->cache->src && !find_cache_entry(global_cache, ctx->cache->key)) {
      // add it to global cache
      ctx->cache->src         = NULL;
      ctx->cache->use_counter = 0;
      ctx->cache->next        = global_cache;
      global_cache            = ctx->cache;
    }
    else {
      // free cache
      if (ctx->cache->src) ctx->cache->src->use_counter--;
      if (ctx->cache->free) ctx->cache->free(ctx->cache->value);
      free(ctx->cache);
    }
    ctx->cache = next;
  }
#endif
  free(ctx);
}

c4_status_t c4_proofer_status(proofer_ctx_t* ctx) {
  if (ctx->state.error) return C4_ERROR;
  if (ctx->proof.data) return C4_SUCCESS;
  if (c4_state_get_pending_request(&ctx->state)) return C4_PENDING;
  return C4_PENDING;
}

c4_status_t c4_proofer_execute(proofer_ctx_t* ctx) {
  // we alkways check the state first, so we don't execute if the result or error is already there.
  if (c4_state_get_pending_request(&ctx->state)) return C4_PENDING;
  if (ctx->state.error) return C4_ERROR;
  if (ctx->proof.data) return C4_SUCCESS;

  proofer_execute(ctx);

  return c4_proofer_status(ctx);
}
