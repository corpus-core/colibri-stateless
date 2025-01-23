#ifndef __PLUGIN_H__
#define __PLUGIN_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "bytes.h"

// storage plugin

typedef struct {
  bool (*get)(char* key, bytes_buffer_t* buffer);
  void (*set)(char* key, bytes_t value);
  void (*del)(char* key);
  uint32_t max_sync_states;
} storage_plugin_t;

void c4_get_storage_config(storage_plugin_t* plugin);
void c4_set_storage_config(storage_plugin_t* plugin);

#ifdef __cplusplus
}
#endif

#endif