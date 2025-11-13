/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "../util/chains.h"
#include "logger.h"
#include "server.h"
#include "tracing.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(PROVER_CACHE) && defined(CHAIN_ETH)
#include "chains/eth/prover/logs_cache.h"
#endif
#ifdef _WIN32
#include "../util/win_compat.h"
#endif

http_server_t http_server = {0};
static void   config();
static void   load_config_file();

static char**   args        = NULL;
static int      args_count  = 0;
static buffer_t help_buffer = {0};

// Track the config file path for saving changes
static char* current_config_file_path = NULL;

// Config parameter registry for dynamic Web-UI
#define MAX_CONFIG_PARAMS 50
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
  for (int i = 0; i < args_count - (has_value ? 1 : 0); i++) {
    if (args[i][0] == '-') {
      if (args[i][1] == '-') {
        if (strcmp(args[i] + 2, name) == 0) return has_value ? args[i + 1] : (char*) "true";
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
  bprintf(&help_buffer, "  -%c, --%s", shortcut, name);
  while (help_buffer.data.len - l < 25) bprintf(&help_buffer, " ");
  l = help_buffer.data.len;
  buffer_add_chars(&help_buffer, env_name);
  while (help_buffer.data.len - l < 20) bprintf(&help_buffer, " ");
  bprintf(&help_buffer, "%s ( default:%s )\n", descr, default_value);
}

static int get_string(char** target, char* env_name, char* arg_nane, char shortcut, char* descr) {
  add_help_line(shortcut, arg_nane, env_name, descr, *target);
  register_config_param(env_name, arg_nane, descr, CONFIG_PARAM_STRING, target, 0, 0);
  char* val       = getenv(env_name);
  char* arg_value = get_arg(arg_nane, shortcut, true);
  if (arg_value)
    val = arg_value;
  if (val) *target = val;
  return 0;
}

static int get_key(bytes32_t target, char* env_name, char* arg_nane, char shortcut, char* descr) {
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

static int get_int(int* target, char* env_name, char* arg_nane, char shortcut, char* descr, int min, int max) {
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

void c4_configure(int argc, char* argv[]) {
  args       = argv;
  args_count = argc;

  config();

  if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
    log_info("Usage: %s [options]", args[0]);
    log_info("  -h, --help                                 show this help message");
    log_info("  -f, --config           CONFIG_FILE         path to config file (default: search in ./server.conf, /etc/colibri/server.conf, /usr/local/etc/colibri/server.conf)");
    log_info("%s", help_buffer.data.data);
    // In TEST builds, don't exit the process so tests can capture output
#ifdef TEST
    buffer_free(&help_buffer);
    return;
#else
    exit(0);
#endif
  }
  else {
    log_info("Starting server with config:");
    log_info("  host          : %s", http_server.host);
    log_info("  port          : %d", (uint32_t) http_server.port);
    log_info("  memcached_host: %s", http_server.memcached_host);
    log_info("  memcached_port: %d", (uint32_t) http_server.memcached_port);
    log_info("  memcached_pool: %d", (uint32_t) http_server.memcached_pool);
    log_info("  loglevel      : %d", (uint32_t) http_server.loglevel);
    log_info("  req_timeout   : %d", (uint32_t) http_server.req_timeout);
    log_info("  chain_id      : %d", (uint32_t) http_server.chain_id);
    log_info("  rpc_nodes     : %s", http_server.rpc_nodes);
    log_info("  beacon_nodes  : %s", http_server.beacon_nodes);
    log_info("  prover_nodes : %s", http_server.prover_nodes);
    log_info("  beacon_events : %d", (uint32_t) http_server.stream_beacon_events);
    log_info("  period_store  : %s", http_server.period_store);
    log_info("  web_ui_enabled: %d", (uint32_t) http_server.web_ui_enabled);

    // Show OP Stack preconf configuration if this is an OP Stack chain
    if (c4_chain_type(http_server.chain_id) == C4_CHAIN_TYPE_OP) {
      log_info("  --- OP Stack Preconf Configuration ---");
      log_info("  preconf_storage_dir    : %s", http_server.preconf_storage_dir ? http_server.preconf_storage_dir : "(null)");
      log_info("  preconf_ttl_minutes    : %d", (uint32_t) http_server.preconf_ttl_minutes);
      log_info("  preconf_cleanup_interval: %d", (uint32_t) http_server.preconf_cleanup_interval_minutes);
      log_info("  preconf_mode           : automatic (HTTP fallback until gossip active)");
    }
  }
  buffer_free(&help_buffer);

  c4_set_log_level(http_server.loglevel);
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

static void config() {
  // Set default values
  http_server.host                             = "127.0.0.1"; // Localhost only by default (security best practice)
  http_server.port                             = 8090;
  http_server.memcached_host                   = ""; // Empty by default - memcached is optional
  http_server.memcached_port                   = 11211;
  http_server.memcached_pool                   = 20;
  http_server.loglevel                         = LOG_WARN;
  http_server.req_timeout                      = 120;
  http_server.chain_id                         = 1;
  http_server.rpc_nodes                        = "https://nameless-sly-reel.quiknode.pro/5937339c28c09a908994b74e2514f0f6cfdac584/,https://eth-mainnet.g.alchemy.com/v2/B8W2IZrDkCkkjKxQOl70XNIy4x4PT20S,https://rpc.ankr.com/eth/33d0414ebb46bda32a461ecdbd201f9cf5141a0acb8f95c718c23935d6febfcd";
  http_server.beacon_nodes                     = "https://lodestar-mainnet.chainsafe.io/";
  http_server.prover_nodes                     = "";
  http_server.checkpointz_nodes                = "https://sync-mainnet.beaconcha.in,https://beaconstate.info,https://sync.invis.tools,https://beaconstate.ethstaker.cc";
  http_server.stream_beacon_events             = 0;
  http_server.period_store                     = NULL;
  http_server.preconf_storage_dir              = "./preconfs";
  http_server.preconf_ttl_minutes              = 30; // 30 minutes TTL
  http_server.preconf_cleanup_interval_minutes = 5;  // Cleanup every 5 minutes
  // preconf_use_gossip removed - now using automatic HTTP fallback

  // Web UI (disabled by default for security)
  http_server.web_ui_enabled = 0;

  // Heuristic load-balancing defaults
  http_server.max_concurrency_default    = 8;
  http_server.max_concurrency_cap        = 64;
  http_server.latency_target_ms          = 200;
  http_server.conc_cooldown_ms           = 30000;
  http_server.overflow_slots             = 1;
  http_server.saturation_wait_ms         = 100;
  http_server.method_stats_half_life_sec = 60;
  http_server.block_availability_window  = 512;
  http_server.block_availability_ttl_sec = 300;
  // 0 = auto (use chain block_time as default)
  http_server.rpc_head_poll_interval_ms = 0;
  http_server.rpc_head_poll_enabled     = 1;
  // Latency bias defaults
  http_server.latency_bias_power_x100         = 200; // 2.0
  http_server.latency_backpressure_power_x100 = 200; // 2.0
  http_server.latency_bias_offset_ms          = 50;  // ms

  // cURL pool defaults
  http_server.curl.http2_enabled         = 1;
  http_server.curl.pool_max_host         = 4;
  http_server.curl.pool_max_total        = 64;
  http_server.curl.pool_maxconnects      = 128;
  http_server.curl.upkeep_interval_ms    = 15000;
  http_server.curl.tcp_keepalive_enabled = 1;
  http_server.curl.tcp_keepidle_s        = 30;
  http_server.curl.tcp_keepintvl_s       = 15;

#ifdef TEST
  http_server.test_dir = NULL;
#endif
  // Tracing defaults
  http_server.tracing_enabled        = 0;
  http_server.tracing_url            = "";
  http_server.tracing_service_name   = "colibri-stateless";
  http_server.tracing_sample_percent = 10; // 10%

  // Load config file (if exists)
  // Priority: defaults < config file < env vars < command line
  load_config_file();

  get_string(&http_server.host, "HOST", "host", 'h', "Host/IP address to bind to (127.0.0.1=localhost only, 0.0.0.0=all interfaces)");
  get_int(&http_server.port, "PORT", "port", 'p', "Port to listen on", 1, 65535);
  get_string(&http_server.memcached_host, "MEMCACHED_HOST", "memcached_host", 'm', "hostnane of the memcached server");
  get_key(http_server.witness_key, "WITNESS_KEY", "witness_key", 'w', "hexcode or path to a private key used as signer for the witness");
  get_int(&http_server.memcached_port, "MEMCACHED_PORT", "memcached_port", 'P', "port of the memcached server", 1, 65535);
  get_int(&http_server.memcached_pool, "MEMCACHED_POOL", "memcached_pool", 'S', "pool size of the memcached server", 1, 100);
  get_int(&http_server.loglevel, "LOG_LEVEL", "log_level", 'l', "log level", 0, 5);
  get_int(&http_server.req_timeout, "REQUEST_TIMEOUT", "req_timeout", 't', "request timeout", 1, 300);
  get_int(&http_server.chain_id, "CHAIN_ID", "chain_id", 'c', "chain id", 1, 0xFFFFFFF);
  get_string(&http_server.rpc_nodes, "RPC", "rpc", 'r', "list of rpc endpoints");
  get_string(&http_server.beacon_nodes, "BEACON", "beacon", 'b', "list of beacon nodes api endpoints");
  get_string(&http_server.prover_nodes, "PROVER", "prover", 'R', "list of remote prover endpoints");
  get_string(&http_server.checkpointz_nodes, "CHECKPOINTZ", "checkpointz", 'z', "list of checkpointz server endpoints");
  get_int(&http_server.stream_beacon_events, "BEACON_EVENTS", "beacon_events", 'e', "activates beacon event streaming", 0, 1);
  // Optional logs cache size in blocks (default 0 = disabled). Only enabled when beacon events are active.
  int eth_logs_cache_blocks = 0;
  get_int(&eth_logs_cache_blocks, "ETH_LOGS_CACHE_BLOCKS", "eth_logs_cache_blocks", 0, "max number of contiguous blocks to cache logs for eth_getLogs", 0, 131072);
#if defined(PROVER_CACHE) && defined(CHAIN_ETH)
  if (http_server.stream_beacon_events && eth_logs_cache_blocks > 0) {
    c4_eth_logs_cache_enable((uint32_t) eth_logs_cache_blocks);
    log_info("eth_logs_cache enabled with capacity: %d blocks", (uint32_t) eth_logs_cache_blocks);
  }
  else {
    c4_eth_logs_cache_disable();
    log_info("eth_logs_cache disabled (beacon_events=%d, capacity=%d)", (uint32_t) http_server.stream_beacon_events, (uint32_t) eth_logs_cache_blocks);
  }
#endif
  get_string(&http_server.period_store, "DATA", "data", 'd', "path to the data-directory holding blockroots and light client updates");
  get_string(&http_server.preconf_storage_dir, "PRECONF_DIR", "preconf_dir", 'P', "directory for storing preconfirmations");
  get_int(&http_server.preconf_ttl_minutes, "PRECONF_TTL", "preconf_ttl", 'T', "TTL for preconfirmations in minutes", 1, 1440);
  get_int(&http_server.preconf_cleanup_interval_minutes, "PRECONF_CLEANUP_INTERVAL", "preconf_cleanup_interval", 'C', "cleanup interval in minutes", 1, 60);
  // preconf_use_gossip option removed - now using automatic HTTP fallback

  get_int(&http_server.web_ui_enabled, "WEB_UI_ENABLED", "web_ui_enabled", 'u', "enable web-based configuration UI (0=disabled, 1=enabled)", 0, 1);

  // Heuristic load-balancing configuration (ENV/args)
  get_int(&http_server.max_concurrency_default, "C4_MAX_CONCURRENCY_DEFAULT", "max_concurrency_default", 'M', "default per-server max concurrency", 1, 4096);
  get_int(&http_server.max_concurrency_cap, "C4_MAX_CONCURRENCY_CAP", "max_concurrency_cap", 'K', "cap for dynamic concurrency", 1, 65535);
  get_int(&http_server.latency_target_ms, "C4_LATENCY_TARGET_MS", "latency_target_ms", 'L', "target latency for AIMD (ms)", 10, 100000);
  get_int(&http_server.conc_cooldown_ms, "C4_CONC_COOLDOWN_MS", "conc_cooldown_ms", 'o', "cooldown for concurrency adjustments (ms)", 0, 600000);
  get_int(&http_server.overflow_slots, "C4_OVERFLOW_SLOTS", "overflow_slots", 'v', "overflow slots per server when saturated", 0, 16);
  get_int(&http_server.saturation_wait_ms, "C4_SATURATION_WAIT_MS", "saturation_wait_ms", 'W', "short wait on saturation before overflow (ms)", 0, 10000);
  get_int(&http_server.method_stats_half_life_sec, "C4_METHOD_STATS_HALF_LIFE_SEC", "method_stats_half_life_sec", 'H', "half-life for method stats (sec)", 1, 3600);
  get_int(&http_server.block_availability_window, "C4_BLOCK_AVAIL_WINDOW", "block_availability_window", 'B', "block availability window size", 64, 8192);
  get_int(&http_server.block_availability_ttl_sec, "C4_BLOCK_AVAIL_TTL_SEC", "block_availability_ttl_sec", 'J', "block availability TTL (sec)", 10, 86400);
  get_int(&http_server.rpc_head_poll_interval_ms, "C4_RPC_HEAD_POLL_INTERVAL_MS", "rpc_head_poll_interval_ms", 'q', "interval for eth_blockNumber polling (ms)", 500, 60000);
  get_int(&http_server.rpc_head_poll_enabled, "C4_RPC_HEAD_POLL_ENABLED", "rpc_head_poll_enabled", 'Q', "enable head polling (0/1)", 0, 1);
  // Latency bias/backpressure tuning
  get_int(&http_server.latency_bias_power_x100, "C4_LATENCY_BIAS_POWER_X100", "latency_bias_power_x100", 0, "exponent*100 for latency bias (e.g. 200=2.0)", 50, 1000);
  get_int(&http_server.latency_backpressure_power_x100, "C4_LATENCY_BACKPRESSURE_POWER_X100", "latency_backpressure_power_x100", 0, "exponent*100 for backpressure penalty (e.g. 200=2.0)", 50, 1000);
  get_int(&http_server.latency_bias_offset_ms, "C4_LATENCY_BIAS_OFFSET_MS", "latency_bias_offset_ms", 0, "offset added to latency for stability (ms)", 0, 1000);

  // cURL pool configuration (ENV/args)
  get_int(&http_server.curl.http2_enabled, "C4_HTTP2", "http2", 0, "enable HTTP/2 (0/1)", 0, 1);
  get_int(&http_server.curl.pool_max_host, "C4_POOL_MAX_HOST", "pool_max_host", 0, "max connections per host", 1, 1024);
  get_int(&http_server.curl.pool_max_total, "C4_POOL_MAX_TOTAL", "pool_max_total", 0, "max total connections", 1, 65536);
  get_int(&http_server.curl.pool_maxconnects, "C4_POOL_MAXCONNECTS", "pool_maxconnects", 0, "connection cache size", 1, 65536);
  get_int(&http_server.curl.upkeep_interval_ms, "C4_UPKEEP_MS", "upkeep_ms", 0, "upkeep interval (ms)", 0, 600000);
  get_int(&http_server.curl.tcp_keepalive_enabled, "C4_TCP_KEEPALIVE", "tcp_keepalive", 0, "TCP keepalive (0/1)", 0, 1);
  get_int(&http_server.curl.tcp_keepidle_s, "C4_TCP_KEEPIDLE", "tcp_keepidle", 0, "TCP keepidle seconds", 1, 3600);
  get_int(&http_server.curl.tcp_keepintvl_s, "C4_TCP_KEEPINTVL", "tcp_keepintvl", 0, "TCP keepintvl seconds", 1, 3600);

#ifdef TEST
  get_string(&http_server.test_dir, "TEST_DIR", "test_dir", 'x', "TEST MODE: record all responses to TESTDATA_DIR/server/<test_dir>/");
#endif

  // Tracing (ENV/args)
  get_int(&http_server.tracing_enabled, "C4_TRACING_ENABLED", "tracing_enabled", 0, "enable tracing (0/1)", 0, 1);
  get_string(&http_server.tracing_url, "C4_TRACING_URL", "tracing_url", 0, "Zipkin v2 endpoint (e.g. http://localhost:9411/api/v2/spans)");
  get_string(&http_server.tracing_service_name, "C4_TRACING_SERVICE", "tracing_service", 0, "Tracing service name");
  get_int(&http_server.tracing_sample_percent, "C4_TRACING_SAMPLE_PERCENT", "tracing_sample_percent", 0, "Tracing sample rate percent (0..100)", 0, 100);

  // Apply tracing configuration
  tracing_configure(http_server.tracing_enabled != 0,
                    http_server.tracing_url,
                    http_server.tracing_service_name,
                    (double) http_server.tracing_sample_percent / 100.0);
}
