#ifndef SERVERIO_CONFIGURE_H
#define SERVERIO_CONFIGURE_H

#include "crypto.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  CONFIG_PARAM_INT,
  CONFIG_PARAM_STRING,
  CONFIG_PARAM_KEY
} config_param_type_t;

typedef struct {
  char*               name;        // env variable name
  char*               arg_name;    // command line arg name
  char*               description; // human-readable description
  config_param_type_t type;        // parameter type
  void*               value_ptr;   // pointer to actual value
  int                 min;         // min value (for int)
  int                 max;         // max value (for int)
} config_param_t;

/**
 * Returns the registry of configuration parameters (for Web UI and tooling).
 *
 * @param count receives the number of entries (must not be NULL)
 * @return array of registered parameters (valid until next `c4_init_config` / process exit)
 */
const config_param_t* c4_get_config_params(int* count);

/**
 * Parses argv, loads `server.conf`, applies env/CLI overrides, and logs the effective config.
 *
 * Calls `c4_init_config`, chain-specific `config()` hooks, then `apply_config()`.
 * When `argv[1]` is `--help` or `-h`, prints usage to stderr (does not exit in `TEST` builds).
 *
 * @param argc argument count passed to `main`
 * @param argv argument vector passed to `main`
 */
void c4_configure(int argc, char* argv[]);

/**
 * Prints CLI usage (options list) to stderr and exits the process unless `TEST` is defined.
 */
void c4_write_usage();

/**
 * Stores argv and loads configuration from `--config` or default search paths into the environment.
 *
 * Does not apply values to `http_server` yet; call chain `config()` functions after this.
 *
 * @param argc argument count
 * @param argv argument vector
 */
void c4_init_config(int argc, char* argv[]);

/**
 * Logs the effective configuration (all registered parameters) at INFO level.
 */
void c4_write_config();

/**
 * Adds a section heading to the generated help text (GitBook-style `::: name` marker).
 *
 * @param name section title shown in `--help` output
 */
void c4_configure_add_section(char* name);

/**
 * Binds a string setting to env var, CLI flag, help text, and the Web UI registry.
 *
 * Precedence: CLI value beats environment; environment beats the current `*target` default.
 *
 * @param target pointer to the `char*` field to update
 * @param env_name environment variable name (e.g. `HOST`)
 * @param arg_nane long option name without `--` (typo preserved for API stability)
 * @param shortcut single-letter CLI flag, or `0` if none
 * @param descr help string
 * @return always `0`
 */
int conf_string(char** target, char* env_name, char* arg_nane, char shortcut, char* descr);

/**
 * Binds a 32-byte hex key to env/CLI (optional `0x` prefix, 66 characters).
 *
 * @param target 32-byte output buffer
 * @param env_name environment variable name
 * @param arg_nane long option name without `--`
 * @param shortcut single-letter CLI flag, or `0` if none
 * @param descr help string
 * @return always `0`
 */
int conf_key(bytes32_t target, char* env_name, char* arg_nane, char shortcut, char* descr);

/**
 * Binds a signed integer with range validation (rejects overflow and trailing garbage).
 *
 * @param target pointer to the `int` field to update
 * @param env_name environment variable name
 * @param arg_nane long option name without `--`
 * @param shortcut single-letter CLI flag, or `0` if none
 * @param descr help string
 * @param min inclusive minimum accepted value
 * @param max inclusive maximum accepted value
 * @return `0` on success, `1` if the parsed value is invalid (target unchanged)
 */
int conf_int(int* target, char* env_name, char* arg_nane, char shortcut, char* descr, int min, int max);

/**
 * Binds an unsigned 64-bit integer with range validation.
 *
 * @param target pointer to the `uint64_t` field to update
 * @param env_name environment variable name
 * @param arg_nane long option name without `--`
 * @param shortcut single-letter CLI flag, or `0` if none
 * @param descr help string
 * @param min inclusive minimum accepted value
 * @param max inclusive maximum accepted value
 * @return `0` on success, `1` if the parsed value is invalid (target unchanged)
 */
int conf_uint64(uint64_t* target, char* env_name, char* arg_nane, char shortcut, char* descr, uint64_t min, uint64_t max);

/** Boolean helper: `conf_int` with range `0..1`. */
#define conf_bool(target, env_name, arg_nane, shortcut, descr) conf_int((int*) target, env_name, arg_nane, shortcut, descr, 0, 1)

/**
 * Returns the path of the config file loaded by `c4_init_config` (explicit `--config` or discovered path).
 *
 * @return path string owned by the configure module, or NULL if no file was loaded
 */
const char* c4_get_config_file_path();

/**
 * Merges `KEY=VALUE` lines into the current config file (creates backup when file existed).
 *
 * @param updates newline-separated updates (same format as `server.conf`)
 * @return `0` on success, `-1` on error (message logged)
 */
int c4_save_config_file(const char* updates);

#ifdef __cplusplus
}
#endif

#endif
