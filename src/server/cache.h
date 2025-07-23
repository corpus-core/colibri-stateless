// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#ifndef SERVER_CACHE_H
#define SERVER_CACHE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration of the memcached client structure
typedef struct mc_s mc_t;

/**
 * Callback function type for memcached operations.
 * @param data User-provided context data
 * @param value Retrieved value (NULL if not found or error)
 * @param value_len Length of the value
 */
typedef void (*memcache_cb)(void* data, char* value, size_t value_len);

/**
 * Create a new memcached client.
 * @param pool_size Number of connection pool size
 * @return Newly created memcached client or NULL on error
 */
mc_t* memcache_new(unsigned int pool_size, const char* host, int port);

/**
 * Free the memcached client and its resources.
 * @param client_p Pointer to the memcached client pointer
 */
void memcache_free(mc_t** client_p);

/**
 * Get a value from memcached.
 * @param client The memcached client
 * @param key The key to get
 * @param keylen Length of the key
 * @param data User-provided context data that will be passed to the callback
 * @param cb Callback function to be called on completion
 * @return 0 on success, or an error code
 */
int memcache_get(mc_t* client, char* key, size_t keylen, void* data, memcache_cb cb);

/**
 * Set a value in memcached.
 * @param client The memcached client
 * @param key The key to set
 * @param keylen Length of the key
 * @param value The value to set
 * @param value_len Length of the value
 * @param ttl Time-to-live in seconds
 * @return 0 on success, or an error code
 */
int memcache_set(mc_t* client, char* key, size_t keylen, char* value, size_t value_len,
                 uint32_t ttl);

#ifdef __cplusplus
}
#endif

#endif /* SERVER_CACHE_H */
