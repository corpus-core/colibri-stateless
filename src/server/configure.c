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

static int get_int(int* target, char* env_name, char* arg_nane, char shortcut, char* descr, int min, int max) {
  char* default_value = bprintf(NULL, "%d", *target);
  add_help_line(shortcut, arg_nane, env_name, descr, default_value);
  free(default_value);
  char* env_value = getenv(env_name);
  char* arg_value = get_arg(arg_nane, shortcut, true);
  int   val       = 0;
  bool  set       = false;
  if (env_value) {
    val = atoi(env_value);
    set = true;
  }
  if (arg_value) {
    val = atoi(arg_value);
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
    printf("Usage: %s [options]\n", args[0]);
    printf("  -h, --help                                 show this help message\n");
    printf("%s\n", help_buffer.data.data);
    exit(0);
  }
  buffer_free(&help_buffer);
}

static void config() {
  http_server.port           = 8080;
  http_server.memcached_host = "localhost";
  http_server.memcached_port = 11211;
  http_server.memcached_pool = 10;
  http_server.loglevel       = 0;
  http_server.req_timeout    = 120;
  http_server.chain_id       = 1;
  http_server.rpc_nodes      = "https://nameless-sly-reel.quiknode.pro/5937339c28c09a908994b74e2514f0f6cfdac584/,https://eth-mainnet.g.alchemy.com/v2/B8W2IZrDkCkkjKxQOl70XNIy4x4PT20S,https://rpc.ankr.com/eth/33d0414ebb46bda32a461ecdbd201f9cf5141a0acb8f95c718c23935d6febfcd";
  http_server.beacon_nodes   = "https://lodestar-mainnet.chainsafe.io/";

  get_int(&http_server.port, "PORT", "port", 'p', "Port to listen on", 1, 65535);
  get_string(&http_server.memcached_host, "MEMCACHED_HOST", "memcached_host", 'm', "hostnane of the memcached server");
  get_int(&http_server.memcached_port, "MEMCACHED_PORT", "memcached_port", 'P', "port of the memcached server", 1, 65535);
  get_int(&http_server.memcached_pool, "MEMCACHED_POOL", "memcached_pool", 'S', "pool size of the memcached server", 1, 100);
  get_int(&http_server.loglevel, "LOG_LEVEL", "log_level", 'l', "log level", 0, 3);
  get_int(&http_server.req_timeout, "REQUEST_TIMEOUT", "req_timeout", 't', "request timeout", 1, 300);
  get_int(&http_server.chain_id, "CHAIN_ID", "chain_id", 'c', "chain id", 1, 0xFFFFFFF);
  get_string(&http_server.rpc_nodes, "RPC", "rpc", 'r', "list of rpc endpoints");
  get_string(&http_server.beacon_nodes, "BEACON", "beacon", 'b', "list of beacon nodes api endpoints");
}
