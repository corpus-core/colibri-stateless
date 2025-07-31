/*
 * Swift Storage Bridge Header
 * Functions for registering Swift storage callbacks with C core
 */

#ifndef SWIFT_STORAGE_BRIDGE_H
#define SWIFT_STORAGE_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Function pointer types for Swift callbacks
typedef void* (*swift_storage_get_fn)(const char* key, uint32_t* out_len);
typedef void (*swift_storage_set_fn)(const char* key, const uint8_t* data, uint32_t len);
typedef void (*swift_storage_delete_fn)(const char* key);

// Functions to register Swift callbacks
void swift_storage_bridge_register_get(swift_storage_get_fn fn);
void swift_storage_bridge_register_set(swift_storage_set_fn fn);
void swift_storage_bridge_register_delete(swift_storage_delete_fn fn);

// Initialize the storage plugin with Swift bridges
void swift_storage_bridge_initialize(void);

#ifdef __cplusplus
}
#endif

#endif // SWIFT_STORAGE_BRIDGE_H