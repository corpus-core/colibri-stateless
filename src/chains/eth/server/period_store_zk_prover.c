/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */
#include "period_store_zk_prover.h"
#include "../zk_verifier/zk_verifier.h"
#include "bytes.h"
#include "eth_conf.h"
#include "logger.h"
#include "server.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <uv.h>

#ifdef _WIN32
#include <io.h>
#define access _access
#define unlink _unlink
#define X_OK   0
#else
#include <unistd.h>
#endif

prover_stats_t  prover_stats         = {0};
static uint64_t last_verified_period = 0;

// Context for spawn callback
typedef struct {
  uint64_t  period;
  uint64_t  start_time;
  uv_pipe_t stdout_pipe;
  uv_pipe_t stderr_pipe;
  buffer_t  stdout_buf;
  buffer_t  stderr_buf;
  int       pipes_closing; // number of pipes still expected to close (0..2)
} zk_prover_ctx_t;

static void prover_flush_lines(zk_prover_ctx_t* ctx, bool is_stderr, bool flush_partial) {
  buffer_t* b = is_stderr ? &ctx->stderr_buf : &ctx->stdout_buf;
  if (!b->data.data || b->data.len == 0) return;

  // Log complete lines (newline-delimited).
  for (;;) {
    uint8_t* nl = (uint8_t*) memchr(b->data.data, '\n', b->data.len);
    if (!nl) break;
    size_t line_len = (size_t) (nl - b->data.data);

    // Trim trailing '\r' (Windows-style line endings).
    while (line_len > 0 && b->data.data[line_len - 1] == '\r') line_len--;

    char* line = (char*) safe_malloc(line_len + 1);
    memcpy(line, b->data.data, line_len);
    line[line_len] = 0;

    if (is_stderr)
      log_warn("Prover: stderr: %s", line);
    else
      log_info("Prover: stdout: %s", line);

    safe_free(line);
    buffer_splice(b, 0, (uint32_t) ((nl - b->data.data) + 1), (bytes_t) {0});
  }

  // Optionally flush remaining partial line.
  if (flush_partial && b->data.len > 0) {
    // Trim trailing '\r'
    size_t line_len = b->data.len;
    while (line_len > 0 && b->data.data[line_len - 1] == '\r') line_len--;

    char* line = (char*) safe_malloc(line_len + 1);
    memcpy(line, b->data.data, line_len);
    line[line_len] = 0;

    if (is_stderr)
      log_warn("Prover: stderr: %s", line);
    else
      log_info("Prover: stdout: %s", line);

    safe_free(line);
    buffer_reset(b);
  }
}

static void prover_on_pipe_closed(uv_handle_t* handle) {
  zk_prover_ctx_t* ctx = (zk_prover_ctx_t*) handle->data;
  if (!ctx) return;
  ctx->pipes_closing--;
  if (ctx->pipes_closing <= 0) {
    buffer_free(&ctx->stdout_buf);
    buffer_free(&ctx->stderr_buf);
    free(ctx);
  }
}

static void prover_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
  (void) handle;
  size_t sz = suggested_size;
  if (sz < 4096) sz = 4096;
  buf->base = (char*) safe_malloc(sz);
  buf->len  = (unsigned int) sz;
}

static void prover_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
  if (nread > 0) {
    zk_prover_ctx_t* ctx       = (zk_prover_ctx_t*) ((uv_handle_t*) stream)->data;
    bool             is_stderr = (stream == (uv_stream_t*) &ctx->stderr_pipe);

    buffer_append(is_stderr ? &ctx->stderr_buf : &ctx->stdout_buf, bytes(buf->base, (uint32_t) nread));
    prover_flush_lines(ctx, is_stderr, false);
  }
  else if (nread < 0) {
    // EOF or error: stop reading and close the pipe.
    zk_prover_ctx_t* ctx       = (zk_prover_ctx_t*) ((uv_handle_t*) stream)->data;
    bool             is_stderr = (stream == (uv_stream_t*) &ctx->stderr_pipe);
    if (ctx) prover_flush_lines(ctx, is_stderr, true);

    uv_read_stop(stream);
    if (!uv_is_closing((uv_handle_t*) stream))
      uv_close((uv_handle_t*) stream, prover_on_pipe_closed);
  }

  if (buf && buf->base) safe_free(buf->base);
}

static void on_prover_exit(uv_process_t* req, int64_t exit_status, int term_signal) {
  zk_prover_ctx_t* ctx      = (zk_prover_ctx_t*) req->data;
  uint64_t         duration = current_ms() - ctx->start_time;

  prover_stats.last_run_timestamp   = current_unix_ms() / 1000;
  prover_stats.last_run_duration_ms = duration;
  prover_stats.last_run_status      = (exit_status == 0) ? 0 : 1;

  if (exit_status == 0) {
    log_info("Prover: Proof generation successful for period %l (duration: %l ms)", ctx->period, duration);
    prover_stats.total_success++;
    // We verified implicitly by generating it successfully (script checks it)
    if (ctx->period > last_verified_period) last_verified_period = ctx->period;
  }
  else {
    log_error("Prover: Proof generation failed for period %l (code: %ld, signal: %d)", ctx->period, exit_status, term_signal);
    prover_stats.total_failure++;
  }

  // Flush any remaining output and close pipes.
  prover_flush_lines(ctx, false, true);
  prover_flush_lines(ctx, true, true);
  if (!uv_is_closing((uv_handle_t*) &ctx->stdout_pipe)) {
    uv_read_stop((uv_stream_t*) &ctx->stdout_pipe);
    uv_close((uv_handle_t*) &ctx->stdout_pipe, prover_on_pipe_closed);
  }
  if (!uv_is_closing((uv_handle_t*) &ctx->stderr_pipe)) {
    uv_read_stop((uv_stream_t*) &ctx->stderr_pipe);
    uv_close((uv_handle_t*) &ctx->stderr_pipe, prover_on_pipe_closed);
  }

  uv_close((uv_handle_t*) req, (uv_close_cb) free);
}

static char* find_script() {
  // Find script
  char* script = "/app/run_zk_proof.sh";
  if (access(script, X_OK) == 0) return script;
  // Try local dev path relative to CWD (usually build/default)
  // ../../../scripts/run_zk_proof.sh

  script = "../../scripts/run_zk_proof.sh";
  if (access(script, X_OK) == 0) return script;
  // Try current dir

  script = "./run_zk_proof.sh";
  if (access(script, X_OK) == 0) return script;

  log_error("Prover: Script not found (checked /app, ../../../scripts, .)");
  return NULL;
}

static void c4_period_prover_spawn(uint64_t target_period, uint64_t prev_period) {
  log_info("Prover: Starting proof generation for period %l", target_period);

  // Spawn
  char* script = find_script();
  if (!script) {
    log_error("Prover: Script not found");
    return;
  }

  uv_process_t*    req = (uv_process_t*) malloc(sizeof(uv_process_t));
  zk_prover_ctx_t* ctx = (zk_prover_ctx_t*) safe_calloc(1, sizeof(zk_prover_ctx_t));
  ctx->period          = target_period;
  ctx->start_time      = current_ms();
  req->data            = ctx;

  // Setup stdout/stderr pipes for logging.
  uv_loop_t* loop = uv_default_loop();
  uv_pipe_init(loop, &ctx->stdout_pipe, 0);
  uv_pipe_init(loop, &ctx->stderr_pipe, 0);
  ctx->stdout_pipe.data = ctx;
  ctx->stderr_pipe.data = ctx;
  ctx->pipes_closing    = 2;

  uv_process_options_t options = {0};
  options.exit_cb              = on_prover_exit;
  options.file                 = script;

  char target_str[32];
  char prev_str[32];
  sbprintf(target_str, "%l", target_period);
  sbprintf(prev_str, "%l", prev_period);

  char* args[13];
  int   arg_i   = 0;
  args[arg_i++] = script;
  args[arg_i++] = "--period";
  args[arg_i++] = target_str;
  args[arg_i++] = "--prev-period";
  args[arg_i++] = prev_str;
  args[arg_i++] = "--prove";
  args[arg_i++] = "--groth16";
  args[arg_i++] = "--network";
  args[arg_i++] = "--output";
  args[arg_i++] = eth_config.period_store;
  args[arg_i++] = NULL;

  options.args = args;

  uv_stdio_container_t stdio[3] = {0};
  stdio[0].flags                = UV_IGNORE;
  stdio[1].flags                = (uv_stdio_flags) (UV_CREATE_PIPE | UV_WRITABLE_PIPE);
  stdio[1].data.stream          = (uv_stream_t*) &ctx->stdout_pipe;
  stdio[2].flags                = (uv_stdio_flags) (UV_CREATE_PIPE | UV_WRITABLE_PIPE);
  stdio[2].data.stream          = (uv_stream_t*) &ctx->stderr_pipe;
  options.stdio_count           = 3;
  options.stdio                 = stdio;

  extern char** environ;
  int           env_count = 0;
  while (environ[env_count]) env_count++;

  // +2 for our new var and NULL
  char** new_env = malloc((env_count + 2) * sizeof(char*));
  for (int i = 0; i < env_count; i++)
    new_env[i] = environ[i];

  char* key_env_str = NULL;
  if (eth_config.period_prover_key_file) {
    key_env_str            = bprintf(NULL, "SP1_PRIVATE_KEY_FILE=%s", eth_config.period_prover_key_file);
    new_env[env_count]     = key_env_str;
    new_env[env_count + 1] = NULL;
  }
  else
    new_env[env_count] = NULL;

  options.env = new_env;

  int r = uv_spawn(uv_default_loop(), req, &options);

  if (key_env_str) safe_free(key_env_str);
  safe_free(new_env);

  if (r) {
    log_error("Prover: Failed to spawn script: %s", uv_strerror(r));
    free(req);
    if (!uv_is_closing((uv_handle_t*) &ctx->stdout_pipe))
      uv_close((uv_handle_t*) &ctx->stdout_pipe, prover_on_pipe_closed);
    if (!uv_is_closing((uv_handle_t*) &ctx->stderr_pipe))
      uv_close((uv_handle_t*) &ctx->stderr_pipe, prover_on_pipe_closed);
    prover_stats.total_failure++;
  }
  else {
    // Start capturing output.
    uv_read_start((uv_stream_t*) &ctx->stdout_pipe, prover_alloc_cb, prover_read_cb);
    uv_read_start((uv_stream_t*) &ctx->stderr_pipe, prover_alloc_cb, prover_read_cb);
  }
}

void c4_period_prover_on_checkpoint(uint64_t period) {
  // Slave check or no store check
  if (eth_config.period_master_url) return;
  if (!eth_config.period_store) return;

  bool     run_prover               = false;
  uint64_t target_period            = period + 1;
  prover_stats.last_check_timestamp = current_unix_ms() / 1000;
  prover_stats.current_period       = target_period;

  if (target_period <= last_verified_period) return;

  // Paths
  char* period_dir = bprintf(NULL, "%s/%l", eth_config.period_store, target_period);
  char* proof_path = bprintf(NULL, "%s/zk_proof_g16.bin", period_dir);
  char* pub_path   = bprintf(NULL, "%s/zk_pub.bin", period_dir);
  safe_free(period_dir);

  // Check if exists
  struct stat st;
  if (stat(proof_path, &st) == 0) {
    // Exists, verify
    log_info("Prover: Verifying existing proof for period %l", target_period);

    // Read files
    bytes_t proof = bytes_read(proof_path);
    bytes_t pub   = bytes_read(pub_path);

    bool valid = proof.data && pub.data && verify_zk_proof(proof, pub);

    safe_free(proof.data);
    safe_free(pub.data);

    if (valid) {
      log_info("Prover: Existing proof valid for period %l", target_period);
      last_verified_period = target_period;
    }
    else {
      log_warn("Prover: Existing proof INVALID for period %l", target_period);

      // Check age (1 hour = 3600 seconds)
      time_t now     = time(NULL);
      double age_sec = difftime(now, st.st_mtime);

      if (age_sec < 3600) {
        log_error("Prover: Proof is fresh (%f s old), NOT retrying to avoid loop", age_sec);
        prover_stats.total_failure++;
      }
      else {
        log_warn("Prover: Proof is old (%f s old), deleting and retrying", age_sec);
        unlink(proof_path);
        run_prover = true;
      }
    }
  }
  else
    run_prover = true;

  safe_free(proof_path);
  safe_free(pub_path);

  if (run_prover)
    c4_period_prover_spawn(target_period, period);
}
