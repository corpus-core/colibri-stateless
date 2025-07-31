/*
 * Swift Storage Bridge - C functions that call Swift storage implementations
 * Similar to jni_bridge.c but for Swift instead of JNI
 *
 * CRITICAL: Uses real C header files to ensure ABI compatibility
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Use the REAL header files from the C library (correct ABI)
#include "../../../../src/util/bytes.h"
#include "../../../../src/util/plugin.h"

// Function pointer types for Swift callbacks
typedef void* (*swift_storage_get_fn)(const char* key, uint32_t* out_len);
typedef void (*swift_storage_set_fn)(const char* key, const uint8_t* data, uint32_t len);
typedef void (*swift_storage_delete_fn)(const char* key);

// Global variables to cache Swift function pointers
static swift_storage_get_fn    g_swift_get    = NULL;
static swift_storage_set_fn    g_swift_set    = NULL;
static swift_storage_delete_fn g_swift_delete = NULL;

// Bridge functions (called by C core via storage_plugin_t function pointers)

static bool bridge_storage_get(char* key, buffer_t* buffer) {
  if (!g_swift_get) {
    fprintf(stderr, "Swift Storage Bridge Error: get function not registered\n");
    return false;
  }

  uint32_t data_len = 0;
  void*    data_ptr = g_swift_get(key, &data_len);

  if (!data_ptr || data_len == 0) {
    // Key not found or no data
    return false;
  }

  // Create bytes_t to append to buffer
  bytes_t data_to_append = {
      .len  = data_len,
      .data = (uint8_t*) data_ptr};

  // Append data to buffer (buffer_append handles resizing)
  uint32_t result = buffer_append(buffer, data_to_append);

  // Free the Swift-allocated memory
  free(data_ptr);

  return result > 0;
}

static void bridge_storage_set(char* key, bytes_t value) {
  if (!g_swift_set) {
    fprintf(stderr, "Swift Storage Bridge Error: set function not registered\n");
    return;
  }

  g_swift_set(key, value.data, value.len);
}

static void bridge_storage_del(char* key) {
  if (!g_swift_delete) {
    fprintf(stderr, "Swift Storage Bridge Error: delete function not registered\n");
    return;
  }

  g_swift_delete(key);
}

// Functions to register Swift callbacks (called from Swift)
void swift_storage_bridge_register_get(swift_storage_get_fn fn) {
  g_swift_get = fn;
  printf("🗄️ Swift storage get function registered\n");
}

void swift_storage_bridge_register_set(swift_storage_set_fn fn) {
  g_swift_set = fn;
  printf("🗄️ Swift storage set function registered\n");
}

void swift_storage_bridge_register_delete(swift_storage_delete_fn fn) {
  g_swift_delete = fn;
  printf("🗄️ Swift storage delete function registered\n");
}

// Initialize the storage plugin with Swift bridges
void swift_storage_bridge_initialize() {
  storage_plugin_t plugin = {
      .get             = bridge_storage_get,
      .set             = bridge_storage_set,
      .del             = bridge_storage_del,
      .max_sync_states = 10};

  c4_set_storage_config(&plugin);
  printf("🗄️ Swift Storage Bridge initialized with correct ABI\n");
}