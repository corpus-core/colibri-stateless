/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "configure.h"
#include "chains.h"
#include "logger.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include "win_compat.h"
#endif

static void load_config_file();

static char**   args        = NULL;
static int      args_count  = 0;
static buffer_t help_buffer = {0};

// Track the config file path for saving changes
static char* current_config_file_path = NULL;

// Config parameter registry for dynamic Web-UI
#define MAX_CONFIG_PARAMS 80
static config_param_t config_params[MAX_CONFIG_PARAMS];
static int            config_params_count = 0;

// Register a config parameter for Web-UI
static void register_config_param(char* env_name, char* arg_name, char* descr, config_param_type_t type, void* value_ptr, int min, int max) {
  if (config_params_count >= MAX_CONFIG_PARAMS) return;
  config_param_t* p = &config_params[config_params_count++];
  p->name           = env_name;
  p->arg_name       = arg_name;
  p->description    = descr;
  p->type           = type;
  p->value_ptr      = value_ptr;
  p->min            = min;
  p->max            = max;
}

// Get all registered config parameters (for Web-UI)
const config_param_t* c4_get_config_params(int* count) {
  *count = config_params_count;
  return config_params;
}

static char* get_arg(char* name, char shortcut, bool has_value) {
  int len = strlen(name);
  for (int i = 0; i < args_count - (has_value ? 1 : 0); i++) {
    if (args[i][0] == '-') {
      if (args[i][1] == '-') {
        if (strcmp(args[i] + 2, name) == 0) return has_value ? args[i + 1] : (char*) "true";
        if (strncmp(args[i] + 2, name, len) == 0 && args[i][len + 2] == '=') return args[i] + len + 3;
      }
      else {
        for (int j = 1; j < strlen(args[i]); j++) {
          if (args[i][j] == shortcut) return has_value ? args[i + 1] : (char*) "true";
        }
      }
    }
  }
  return NULL;
}
static void add_help_line(char shortcut, char* name, char* env_name, char* descr, char* default_value) {
  int l = help_buffer.data.len;
  if (shortcut)
    bprintf(&help_buffer, "  -%c, --%s", shortcut, name);
  else if (env_name)
    bprintf(&help_buffer, "      --%s", name);
  while (help_buffer.data.len - l < 41) bprintf(&help_buffer, " ");
  l = help_buffer.data.len;
  buffer_add_chars(&help_buffer, env_name);
  while (help_buffer.data.len - l < 36) bprintf(&help_buffer, " ");
  bprintf(&help_buffer, "%s ( default:%s )\n", descr, default_value);
}

int conf_string(char** target, char* env_name, char* arg_nane, char shortcut, char* descr) {
  add_help_line(shortcut, arg_nane, env_name, descr, *target);
  register_config_param(env_name, arg_nane, descr, CONFIG_PARAM_STRING, target, 0, 0);
  char* val       = getenv(env_name);
  char* arg_value = get_arg(arg_nane, shortcut, true);
  if (arg_value)
    val = arg_value;
  if (val) *target = val;
  return 0;
}

int conf_key(bytes32_t target, char* env_name, char* arg_nane, char shortcut, char* descr) {
  add_help_line(shortcut, arg_nane, env_name, descr, "");
  register_config_param(env_name, arg_nane, descr, CONFIG_PARAM_KEY, target, 0, 0);
  char* val       = getenv(env_name);
  char* arg_value = get_arg(arg_nane, shortcut, true);
  if (arg_value)
    val = arg_value;
  if (val && *val) {
    if (val[0] == '0' && val[1] == 'x' && strlen(val) == 66)
      hex_to_bytes(val, -1, bytes(target, 32));
  }
  return 0;
}

int conf_int(int* target, char* env_name, char* arg_nane, char shortcut, char* descr, int min, int max) {
  char* default_value = bprintf(NULL, "%d", *target);
  add_help_line(shortcut, arg_nane, env_name, descr, default_value);
  safe_free(default_value);
  register_config_param(env_name, arg_nane, descr, CONFIG_PARAM_INT, target, min, max);
  char* env_value = getenv(env_name);
  char* arg_value = get_arg(arg_nane, shortcut, max != 1);
  int   val       = 0;
  bool  set       = false;
  if (env_value) {
    val = max == 1 ? (strcmp(env_value, "true") == 0 || strcmp(env_value, "1") == 0) : atoi(env_value);
    set = true;
  }
  if (arg_value) {
    val = max == 1 ? (strcmp(arg_value, "true") == 0 || strcmp(arg_value, "1") == 0) : atoi(arg_value);
    set = true;
  }
  if (!set) return 0;
  if (val < min || val > max) {
    log_error("Invalid value for %s: %d (must be between %d and %d)", env_name, (uint32_t) val, (uint32_t) min, (uint32_t) max);
    return 1;
  }
  *target = val;
  return 0;
}

// Trim whitespace from both ends of a string
static char* trim(char* str) {
  if (!str) return NULL;
  // Trim leading space
  while (isspace((unsigned char) *str)) str++;
  if (*str == 0) return str;
  // Trim trailing space
  char* end = str + strlen(str) - 1;
  while (end > str && isspace((unsigned char) *end)) end--;
  end[1] = '\0';
  return str;
}

// Load configuration from a file
// Format: KEY=VALUE (one per line, # for comments)
static void load_config_file() {
  // Check for explicit config file path from command line
  char* explicit_config = get_arg("config", 'f', true);
  if (explicit_config) {
    FILE* f = fopen(explicit_config, "r");
    if (!f) {
      // If an explicit config path is provided but file doesn't exist,
      // accept the path and continue with defaults. The Web/API can create/save later.
      log_warn("Warning: Config file not found, using defaults: %s", explicit_config);
      if (current_config_file_path) free(current_config_file_path);
      current_config_file_path = strdup(explicit_config);
      return;
    }
    log_info("Loading config from: %s", explicit_config);
    if (current_config_file_path) free(current_config_file_path);
    current_config_file_path = strdup(explicit_config);
    // Load from explicit file (rest of function below)
    char line[1024];
    int  line_num = 0;
    while (fgets(line, sizeof(line), f)) {
      line_num++;

      // Trim whitespace from both ends
      char* trimmed = trim(line);

      // Skip empty lines and comments
      if (trimmed[0] == '\0' || trimmed[0] == '#') continue;

      // Find '=' separator
      char* eq = strchr(trimmed, '=');
      if (!eq) {
        log_warn("Warning: Invalid line %d in config file (no '=' found)", (uint32_t) line_num);
        continue;
      }

      // Split into key and value
      *eq       = '\0';
      char* key = trim(trimmed);
      char* val = trim(eq + 1);

      // Skip if key or value is empty
      if (strlen(key) == 0 || strlen(val) == 0) {
        log_warn("Warning: Empty key or value on line %d in config file", (uint32_t) line_num);
        continue;
      }

      // Set environment variable (will be overridden by actual env vars and command line)
      setenv(key, val, 0); // 0 = don't overwrite existing env vars
    }
    fclose(f);
    return;
  }

  // Default search paths if no explicit config specified
  const char* config_paths[] = {
      "./server.conf",
      "/etc/colibri/server.conf",
      "/usr/local/etc/colibri/server.conf",
      NULL};

#ifdef _WIN32
  char        win_path[512];
  const char* programdata = getenv("PROGRAMDATA");
  if (programdata) {
    snprintf(win_path, sizeof(win_path), "%s\\Colibri\\server.conf", programdata);
    config_paths[1] = win_path;
  }
#endif

  // Try to find and load config file
  FILE* f    = NULL;
  int   path = 0;
  while (config_paths[path] != NULL) {
    f = fopen(config_paths[path], "r");
    if (f) {
      log_info("Loading config from: %s", config_paths[path]);
      current_config_file_path = strdup(config_paths[path]);
      break;
    }
    path++;
  }

  if (!f) {
    // No config file found, use defaults
    return;
  }

  char line[2048];
  int  line_num = 0;

  while (fgets(line, sizeof(line), f)) {
    line_num++;

    // Trim whitespace
    char* trimmed = trim(line);

    // Skip empty lines and comments
    if (trimmed[0] == '\0' || trimmed[0] == '#') continue;

    // Find '=' separator
    char* eq = strchr(trimmed, '=');
    if (!eq) {
      log_warn("Warning: Invalid line %d in config file (no '=' found)", (uint32_t) line_num);
      continue;
    }

    // Split into key and value
    *eq       = '\0';
    char* key = trim(trimmed);
    char* val = trim(eq + 1);

    if (strlen(key) == 0 || strlen(val) == 0) {
      log_warn("Warning: Empty key or value on line %d in config file", (uint32_t) line_num);
      continue;
    }

    // Set the environment variable (will be picked up by get_* functions)
    // Only set if not already set (env vars and cmd line take precedence)
    if (!getenv(key)) {
      setenv(key, val, 0);
    }
  }

  fclose(f);
}

/**
 * Get the current config file path
 *
 * @return Path to the config file that was loaded, or NULL if no file was loaded
 */
const char* c4_get_config_file_path() {
  return current_config_file_path;
}

/**
 * Save configuration to file
 *
 * Creates a backup of the existing config file and writes the new configuration.
 *
 * @param updates Buffer containing key=value pairs to update (can be partial)
 * @return 0 on success, -1 on error
 */
int c4_save_config_file(const char* updates) {
  if (!current_config_file_path) {
    log_error("Error: No config file path available for saving");
    return -1;
  }

  // Create backup of existing config
  char backup_path[1024];
  snprintf(backup_path, sizeof(backup_path), "%s.backup", current_config_file_path);

  // Read existing config into memory (optional)
  FILE* original     = fopen(current_config_file_path, "r");
  bool  has_original = (original != NULL);

// Parse updates into a simple key-value map with dynamic storage
#define MAX_UPDATES      50
#define MAX_KEY_LENGTH   128
#define MAX_VALUE_LENGTH 8192 // Support long values like RPC lists

  struct {
    char* key;
    char* value;
  } update_map[MAX_UPDATES];
  int update_count = 0;

  // Parse updates string (format: "KEY1=VALUE1\nKEY2=VALUE2\n...")
  char* updates_copy = strdup(updates);
  char* line         = strtok(updates_copy, "\n");
  while (line && update_count < MAX_UPDATES) {
    char* eq = strchr(line, '=');
    if (eq) {
      *eq       = '\0';
      char* key = trim(line);
      char* val = trim(eq + 1);

      if (strlen(key) == 0) continue;

      // Validate key length
      if (strlen(key) >= MAX_KEY_LENGTH) {
        log_error("Error: Config key too long (max %d chars): %s", (uint32_t) (MAX_KEY_LENGTH - 1), key);
        free(updates_copy);
        for (int i = 0; i < update_count; i++) {
          free(update_map[i].key);
          free(update_map[i].value);
        }
        return -1;
      }

      // Validate value length
      if (strlen(val) >= MAX_VALUE_LENGTH) {
        log_error("Error: Config value too long (max %d chars) for key: %s", (uint32_t) (MAX_VALUE_LENGTH - 1), key);
        free(updates_copy);
        for (int i = 0; i < update_count; i++) {
          free(update_map[i].key);
          free(update_map[i].value);
        }
        return -1;
      }

      update_map[update_count].key   = strdup(key);
      update_map[update_count].value = strdup(val);
      update_count++;
    }
    line = strtok(NULL, "\n");
  }
  free(updates_copy);

  // Write updated config to temporary file
  char temp_path[1024];
  snprintf(temp_path, sizeof(temp_path), "%s.tmp", current_config_file_path);
  FILE* temp = fopen(temp_path, "w");
  if (!temp) {
    log_error("Error: Could not create temporary config file: %s", temp_path);
    fclose(original);
    return -1;
  }

  // Read original config line by line, updating values as needed (if it exists)
  if (has_original) {
    char line_buf[2048];
    while (fgets(line_buf, sizeof(line_buf), original)) {
      // Make a copy for trimming (since trim() modifies in-place)
      char line_copy[2048];
      strncpy(line_copy, line_buf, sizeof(line_copy) - 1);
      line_copy[sizeof(line_copy) - 1] = '\0';

      char* trimmed = trim(line_copy);

      // Keep comments and empty lines as-is
      if (trimmed[0] == '\0' || trimmed[0] == '#') {
        fprintf(temp, "%s", line_buf);
        continue;
      }

      // Check if this line should be updated
      char* eq = strchr(trimmed, '=');
      if (eq) {
        *eq           = '\0';
        char* key     = trim(trimmed);
        bool  updated = false;

        for (int i = 0; i < update_count; i++) {
          if (update_map[i].key && strcmp(key, update_map[i].key) == 0) {
            fprintf(temp, "%s=%s\n", update_map[i].key, update_map[i].value);
            free(update_map[i].key);
            free(update_map[i].value);
            update_map[i].key   = NULL; // Mark as written
            update_map[i].value = NULL;
            updated             = true;
            break;
          }
        }

        if (!updated) {
          fprintf(temp, "%s", line_buf);
        }
      }
      else {
        fprintf(temp, "%s", line_buf);
      }
    }
  }

  // Add any new keys that weren't in the original file
  for (int i = 0; i < update_count; i++) {
    if (update_map[i].key != NULL) {
      fprintf(temp, "%s=%s\n", update_map[i].key, update_map[i].value);
      free(update_map[i].key);
      free(update_map[i].value);
    }
  }

  if (has_original) fclose(original);
  fclose(temp);

  // Create backup if original existed
  if (has_original) {
    rename(current_config_file_path, backup_path);
  }

  // Move temp file to config file
  if (rename(temp_path, current_config_file_path) != 0) {
    log_error("Error: Could not write new config file");
    // Restore backup if we had one
    if (has_original) rename(backup_path, current_config_file_path);
    return -1;
  }

  log_info("Config file updated: %s (backup: %s)", current_config_file_path, backup_path);
  return 0;
}
void c4_init_config(int argc, char* argv[]) {
  args       = argv;
  args_count = argc;
  load_config_file();
}

void c4_write_usage() {
  fprintf(stderr, "Usage: %s [options]\n", args[0]);
  fprintf(stderr, "  -h, --help                                                               show this help message\n");
  fprintf(stderr, "  -f, --config                           CONFIG_FILE                       path to config file (default: search in ./server.conf, /etc/colibri/server.conf, /usr/local/etc/colibri/server.conf)\n");
  fprintf(stderr, "%s\n", help_buffer.data.data);
  // In TEST builds, don't exit the process so tests can capture output
#ifdef TEST
  buffer_free(&help_buffer);
  return;
#else
  exit(0);
#endif
}

void c4_write_config() {
  buffer_free(&help_buffer);

  buffer_t line         = {0};
  int      max_name_len = 0;
  for (int i = 0; i < config_params_count; i++) {
    config_param_t* p = &config_params[i];
    if (strlen(p->arg_name) > max_name_len)
      max_name_len = strlen(p->arg_name);
  }

  log_info(stderr, "Starting server with config:");
  for (int i = 0; i < config_params_count; i++) {
    config_param_t* p = config_params + i;
    buffer_reset(&line);
    bprintf(&line, "  %s", p->arg_name);
    while (line.data.len < max_name_len + 2) buffer_add_chars(&line, " ");
    switch (p->type) {
      case CONFIG_PARAM_INT:
        bprintf(&line, ": %d", *(int*) p->value_ptr ? *(int*) p->value_ptr : p->min);
        break;
      case CONFIG_PARAM_STRING:
        bprintf(&line, ": %s", *(char**) p->value_ptr);
        break;
      case CONFIG_PARAM_KEY:
        bprintf(&line, ": %s", (*(void**) p->value_ptr) ? "********" : "");
        break;
    }
    log_info("%s", line.data.data);
  }
  buffer_free(&line);
}

void c4_configure_add_section(char* name) {
  bprintf(&help_buffer, "\n::: %s\n\n", name);
}
