#include "plugin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SYNC_STATES_DEFAULT 3

storage_plugin_t storage_conf = {0};
#ifdef FILE_STORAGE

static char* combine_filename(char* name) {
  const char* base_path = getenv("C4_STATES_DIR");
  if (base_path != NULL) {
    size_t length    = strlen(base_path) + strlen(name) + 2;
    char*  full_path = safe_malloc(length);
    if (full_path == NULL) return NULL;
    snprintf(full_path, length, "%s/%s", base_path, name);
    return full_path;
  }
  else
    return strdup(name);
}

static bool file_get(char* filename, buffer_t* data) {
  unsigned char buffer[1024];
  size_t        bytesRead;
  char*         full_path = combine_filename(filename);
  if (full_path == NULL) return false;

  FILE* file = strcmp(filename, "-") ? fopen(full_path, "rb") : stdin;
  safe_free(full_path);
  if (file == NULL) return false;

  while ((bytesRead = fread(buffer, 1, 1024, file)) > 0)
    buffer_append(data, bytes(buffer, bytesRead));

  if (file != stdin)
    fclose(file);
  return true;
}

static void file_set(char* key, bytes_t value) {
  char* full_path = combine_filename(key);
  if (full_path == NULL) return;
  FILE* file = fopen(full_path, "wb");
  safe_free(full_path);
  if (!file) return;
  fwrite(value.data, 1, value.len, file);
  fclose(file);
}
static void file_delete(char* filename) {
  char* full_path = combine_filename(filename);
  if (full_path == NULL) return;
  remove(full_path);
  safe_free(full_path);
}

#endif

void c4_get_storage_config(storage_plugin_t* plugin) {
  if (!storage_conf.max_sync_states) storage_conf.max_sync_states = MAX_SYNC_STATES_DEFAULT;
#ifdef FILE_STORAGE
  if (!storage_conf.get) {
    storage_conf.get = file_get;
    storage_conf.set = file_set;
    storage_conf.del = file_delete;
  }
#endif
  *plugin = storage_conf;
}

void c4_set_storage_config(storage_plugin_t* plugin) {
  storage_conf = *plugin;
  if (!storage_conf.max_sync_states) storage_conf.max_sync_states = MAX_SYNC_STATES_DEFAULT;
}
