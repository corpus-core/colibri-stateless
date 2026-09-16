/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef SERVER_CACHE_H
#define SERVER_CACHE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mc_s mc_t;

/**
 * Completion callback for asynchronous memcached GET.
 *
 * @param data user context passed to `memcache_get`
 * @param value retrieved value (NULL if missing or error)
 * @param value_len length of `value` in bytes
 */
typedef void (*memcache_cb)(void* data, char* value, size_t value_len);

/**
 * Creates a libmemcached-backed client with a connection pool.
 *
 * @param pool_size number of connections in the pool
 * @param host memcached host name
 * @param port memcached port
 * @return client handle, or NULL on allocation/configuration failure
 */
mc_t* memcache_new(unsigned int pool_size, const char* host, int port);

/**
 * Frees a memcached client and clears `*client_p`.
 *
 * @param client_p pointer to client pointer (set to NULL on success)
 */
void memcache_free(mc_t** client_p);

/**
 * Asynchronously reads a key from memcached (invokes `cb` on the libuv thread pool).
 *
 * @param client memcached client
 * @param key key bytes
 * @param keylen length of `key`
 * @param data user context for `cb`
 * @param cb completion callback
 * @return `0` if the request was queued, non-zero error code otherwise
 */
int memcache_get(mc_t* client, char* key, size_t keylen, void* data, memcache_cb cb);

/**
 * Stores a value in memcached with a TTL.
 *
 * @param client memcached client
 * @param key key bytes
 * @param keylen length of `key`
 * @param value value bytes
 * @param value_len length of `value`
 * @param ttl expiration in seconds
 * @return `0` on success, non-zero error code otherwise
 */
int memcache_set(mc_t* client, char* key, size_t keylen, char* value, size_t value_len,
                 uint32_t ttl);

#ifdef __cplusplus
}
#endif

#endif /* SERVER_CACHE_H */
