/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "../util/chains.h"
#include "server.h"
#include <stdlib.h>

http_server_t http_server = {0};
static void   config();

static char**   args        = NULL;
static int      args_count  = 0;
static buffer_t help_buffer = {0};

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
  char* val       = getenv(env_name);
  char* arg_value = get_arg(arg_nane, shortcut, true);
  if (arg_value)
    val = arg_value;
  if (val) *target = val;
  return 0;
}

static int get_key(bytes32_t target, char* env_name, char* arg_nane, char shortcut, char* descr) {
  add_help_line(shortcut, arg_nane, env_name, descr, "");
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
  char* env_value = getenv(env_name);
  char* arg_value = get_arg(arg_nane, shortcut, max != 1);
  int   val       = 0;
  bool  set       = false;
  if (env_value) {
    val = max == 1 ? (strcmp(env_value, "true") == 0) : atoi(env_value);
    set = true;
  }
  if (arg_value) {
    val = max == 1 ? (strcmp(arg_value, "true") == 0) : atoi(arg_value);
    set = true;
  }
  if (!set) return 0;
  if (val < min || val > max) {
    fprintf(stderr, "Invalid value for %s: %d (must be between %d and %d)\n", env_name, val, min, max);
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
    fprintf(stderr, "Usage: %s [options]\n", args[0]);
    fprintf(stderr, "  -h, --help                                 show this help message\n");
    fprintf(stderr, "%s\n", help_buffer.data.data);
    exit(0);
  }
  else {
    fprintf(stderr, "Starting server with config:\n");
    fprintf(stderr, "  port          : %d\n", http_server.port);
    fprintf(stderr, "  memcached_host: %s\n", http_server.memcached_host);
    fprintf(stderr, "  memcached_port: %d\n", http_server.memcached_port);
    fprintf(stderr, "  memcached_pool: %d\n", http_server.memcached_pool);
    fprintf(stderr, "  loglevel      : %d\n", http_server.loglevel);
    fprintf(stderr, "  req_timeout   : %d\n", http_server.req_timeout);
    fprintf(stderr, "  chain_id      : %d\n", http_server.chain_id);
    fprintf(stderr, "  rpc_nodes     : %s\n", http_server.rpc_nodes);
    fprintf(stderr, "  beacon_nodes  : %s\n", http_server.beacon_nodes);
    fprintf(stderr, "  prover_nodes : %s\n", http_server.prover_nodes);
    fprintf(stderr, "  beacon_events : %d\n", http_server.stream_beacon_events);
    fprintf(stderr, "  period_store  : %s\n", http_server.period_store);

    // Show OP Stack preconf configuration if this is an OP Stack chain
    if (c4_chain_type(http_server.chain_id) == C4_CHAIN_TYPE_OP) {
      fprintf(stderr, "  --- OP Stack Preconf Configuration ---\n");
      fprintf(stderr, "  preconf_storage_dir    : %s\n", http_server.preconf_storage_dir ? http_server.preconf_storage_dir : "(null)");
      fprintf(stderr, "  preconf_ttl_minutes    : %d\n", http_server.preconf_ttl_minutes);
      fprintf(stderr, "  preconf_cleanup_interval: %d\n", http_server.preconf_cleanup_interval_minutes);
      fprintf(stderr, "  preconf_mode           : automatic (HTTP fallback until gossip active)\n");
    }
  }
  buffer_free(&help_buffer);
}

static void config() {
  http_server.port                             = 8090;
  http_server.memcached_host                   = "localhost";
  http_server.memcached_port                   = 11211;
  http_server.memcached_pool                   = 20;
  http_server.loglevel                         = 0;
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
#ifdef TEST
  http_server.test_dir = NULL;
#endif

  get_int(&http_server.port, "PORT", "port", 'p', "Port to listen on", 1, 65535);
  get_string(&http_server.memcached_host, "MEMCACHED_HOST", "memcached_host", 'm', "hostnane of the memcached server");
  get_key(http_server.witness_key, "WITNESS_KEY", "witness_key", 'w', "hexcode or path to a private key used as signer for the witness");
  get_int(&http_server.memcached_port, "MEMCACHED_PORT", "memcached_port", 'P', "port of the memcached server", 1, 65535);
  get_int(&http_server.memcached_pool, "MEMCACHED_POOL", "memcached_pool", 'S', "pool size of the memcached server", 1, 100);
  get_int(&http_server.loglevel, "LOG_LEVEL", "log_level", 'l', "log level", 0, 3);
  get_int(&http_server.req_timeout, "REQUEST_TIMEOUT", "req_timeout", 't', "request timeout", 1, 300);
  get_int(&http_server.chain_id, "CHAIN_ID", "chain_id", 'c', "chain id", 1, 0xFFFFFFF);
  get_string(&http_server.rpc_nodes, "RPC", "rpc", 'r', "list of rpc endpoints");
  get_string(&http_server.beacon_nodes, "BEACON", "beacon", 'b', "list of beacon nodes api endpoints");
  get_string(&http_server.prover_nodes, "PROVER", "prover", 'R', "list of remote prover endpoints");
  get_string(&http_server.checkpointz_nodes, "CHECKPOINTZ", "checkpointz", 'z', "list of checkpointz server endpoints");
  get_int(&http_server.stream_beacon_events, "BEACON_EVENTS", "beacon_events", 'e', "activates beacon event streaming", 0, 1);
  get_string(&http_server.period_store, "DATA", "data", 'd', "path to the data-directory holding blockroots and light client updates");
  get_string(&http_server.preconf_storage_dir, "PRECONF_DIR", "preconf_dir", 'P', "directory for storing preconfirmations");
  get_int(&http_server.preconf_ttl_minutes, "PRECONF_TTL", "preconf_ttl", 'T', "TTL for preconfirmations in minutes", 1, 1440);
  get_int(&http_server.preconf_cleanup_interval_minutes, "PRECONF_CLEANUP_INTERVAL", "preconf_cleanup_interval", 'C', "cleanup interval in minutes", 1, 60);
  // preconf_use_gossip option removed - now using automatic HTTP fallback
#ifdef TEST
  get_string(&http_server.test_dir, "TEST_DIR", "test_dir", 'x', "TEST MODE: record all responses to TESTDATA_DIR/server/<test_dir>/");
#endif
}
