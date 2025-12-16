/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */
#include "period_store_zk_prover.h"
#include "../zk_verifier/zk_verifier.h"
#include "bytes.h"
#include "eth_conf.h"
#include "logger.h"
#include "period_store.h"
#include "server.h"
#include <ctype.h>
#include <stdio.h>
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

// Prevent concurrent proof generation runs.
static bool     g_prover_running          = false;
static uint64_t g_prover_running_period   = 0;
static uint64_t g_prover_running_start_ms = 0;

// Context for spawn callback
typedef struct {
  uint64_t  period;
  uint64_t  start_time;
  uv_pipe_t stdout_pipe;
  uv_pipe_t stderr_pipe;
  buffer_t  stdout_buf;
  buffer_t  stderr_buf;
  int       refcount;      // lifetime: 1=process handle + 1=stdout pipe + 1=stderr pipe
  bool      stdout_closed; // true after close callback ran
  bool      stderr_closed; // true after close callback ran
} zk_prover_ctx_t;

static void c4_period_prover_spawn(uint64_t target_period, uint64_t prev_period);
static void c4_period_prover_spawn_host(uint64_t target_period, uint64_t prev_period);

typedef struct {
  uint64_t target_period;
  uint64_t prev_period;
  char*    period_dir;
  char*    sync_path;
} sync_gen_ctx_t;

typedef struct {
  char* host_bin;
  char* elf_path;
  char* period_dir;
  char* sync_path;
  char* prev_dir;
  char* prev_proof;
  char* prev_vk;
  // Output paths (match run_zk_proof.sh conventions)
  char* proof_groth16;
  char* proof_raw;
  char* vk_groth16;
  char* pub_values;
  char* proof_comp;
  char* vk_comp;
} zk_host_paths_t;

static void prover_reset_running(void) {
  g_prover_running          = false;
  g_prover_running_period   = 0;
  g_prover_running_start_ms = 0;
}

static void zk_host_paths_free(zk_host_paths_t* p) {
  if (!p) return;
  safe_free(p->host_bin);
  safe_free(p->elf_path);
  safe_free(p->period_dir);
  safe_free(p->sync_path);
  safe_free(p->prev_dir);
  safe_free(p->prev_proof);
  safe_free(p->prev_vk);
  safe_free(p->proof_groth16);
  safe_free(p->proof_raw);
  safe_free(p->vk_groth16);
  safe_free(p->pub_values);
  safe_free(p->proof_comp);
  safe_free(p->vk_comp);
  memset(p, 0, sizeof(*p));
}

static bool file_exists_min_size(const char* path, size_t min_bytes) {
  if (!path) return false;
  struct stat st;
  if (stat(path, &st) != 0) return false;
  return st.st_size >= (off_t) min_bytes;
}

// Minimal expected sizes for artifacts to avoid treating empty/truncated files as valid.
// These values are intentionally conservative and based on real outputs:
// - sync.ssz is typically ~tens of KB
// - zk_proof.bin is typically ~MB
// - zk_vk_raw.bin can be ~234 bytes (so 256 would be too strict)
static const size_t ZK_SYNC_MIN_BYTES       = 1024;
static const size_t ZK_PREV_PROOF_MIN_BYTES = 1024;
static const size_t ZK_PREV_VK_MIN_BYTES    = 128;

static char* trim_key_string(bytes_t key_bytes) {
  if (!key_bytes.data || key_bytes.len == 0) return NULL;
  // Copy only non-whitespace chars; key files often contain trailing newline.
  char*  out = (char*) safe_malloc(key_bytes.len + 1);
  size_t n   = 0;
  for (uint32_t i = 0; i < key_bytes.len; i++) {
    unsigned char c = key_bytes.data[i];
    if (!isspace(c)) out[n++] = (char) c;
  }
  out[n] = 0;
  if (n == 0) {
    safe_free(out);
    return NULL;
  }
  return out;
}

static bool c4_verify_proof_files(const char* proof_path, const char* pub_path) {
  if (!proof_path || !pub_path) return false;
  bytes_t proof = bytes_read((char*) proof_path);
  bytes_t pub   = bytes_read((char*) pub_path);
  bool    valid = proof.data && pub.data && verify_zk_proof(proof, pub);
  safe_free(proof.data);
  safe_free(pub.data);
  return valid;
}

static void sync_gen_ctx_fail(sync_gen_ctx_t* ctx, data_request_t* res, const char* msg) {
  if (msg) log_error("%s", msg);
  if (res) c4_request_free(res);
  if (ctx) {
    safe_free(ctx->period_dir);
    safe_free(ctx->sync_path);
    safe_free(ctx);
  }
  prover_reset_running();
}

static void on_sync_generated(client_t* client, void* data, data_request_t* res) {
  (void) client;
  sync_gen_ctx_t* ctx = (sync_gen_ctx_t*) data;
  if (!ctx) {
    if (res) c4_request_free(res);
    return;
  }

  if (res && res->error) {
    char* error = bprintf(NULL, "Prover: Failed to generate sync.ssz for period %l: %s", ctx->target_period, res->error);
    sync_gen_ctx_fail(ctx, res, error);
    safe_free(error);
    return;
  }

  if (!res || !res->response.data || res->response.len < 1024) {
    char* error = bprintf(NULL, "Prover: Failed to generate sync.ssz for period %l: empty/too small response", ctx->target_period);
    sync_gen_ctx_fail(ctx, res, error);
    safe_free(error);
    return;
  }

  FILE* f = fopen(ctx->sync_path, "wb");
  if (!f) {
    char* error = bprintf(NULL, "Prover: Failed to open sync.ssz for writing: %s", ctx->sync_path);
    sync_gen_ctx_fail(ctx, res, error);
    safe_free(error);
    return;
  }

  uint64_t written_len = (uint64_t) res->response.len;
  bytes_write(res->response, f, true);
  c4_request_free(res);

  log_info("Prover: Wrote sync.ssz (%l bytes) for period %l",
           written_len, ctx->target_period);

  // Continue pipeline: spawn rust host directly (no bash).
  uint64_t target = ctx->target_period;
  uint64_t prev   = ctx->prev_period;

  safe_free(ctx->period_dir);
  safe_free(ctx->sync_path);
  safe_free(ctx);

  c4_period_prover_spawn_host(target, prev);
}

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
      log_warn("Prover: %s", line);
    else
      log_info("Prover: %s", line);

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
      log_warn("Prover: %s", line);
    else
      log_info("Prover: %s", line);

    safe_free(line);
    buffer_reset(b);
  }
}

static void prover_release_ctx(zk_prover_ctx_t* ctx, const char* reason) {
  if (!ctx) return;
  ctx->refcount--;
  (void) reason;
  if (ctx->refcount <= 0) {
    buffer_free(&ctx->stdout_buf);
    buffer_free(&ctx->stderr_buf);
    free(ctx);
  }
}

static void prover_on_pipe_closed(uv_handle_t* handle) {
  zk_prover_ctx_t* ctx = (zk_prover_ctx_t*) handle->data;
  if (!ctx) return;
  if (handle == (uv_handle_t*) &ctx->stdout_pipe) ctx->stdout_closed = true;
  if (handle == (uv_handle_t*) &ctx->stderr_pipe) ctx->stderr_closed = true;
  prover_release_ctx(ctx, "release_pipe");
}

static void prover_on_process_closed(uv_handle_t* handle) {
  uv_process_t*    req = (uv_process_t*) handle;
  zk_prover_ctx_t* ctx = req ? (zk_prover_ctx_t*) req->data : NULL;
  prover_release_ctx(ctx, "release_process");
  free(req);
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
  g_prover_running          = false;
  g_prover_running_period   = 0;
  g_prover_running_start_ms = 0;

  prover_stats.last_run_timestamp   = current_unix_ms() / 1000;
  prover_stats.last_run_duration_ms = duration;
  prover_stats.last_run_status      = (exit_status == 0) ? 0 : 1;

  if (exit_status == 0) {
    log_info("Prover: Proof generation successful for period %l (duration: %l ms)", ctx->period, duration);
    prover_stats.total_success++;
    // Verify Groth16 proof with the built-in verifier before marking as verified.
    char* period_dir = bprintf(NULL, "%s/%l", eth_config.period_store, ctx->period);
    char* proof_path = bprintf(NULL, "%s/zk_proof_g16.bin", period_dir);
    char* pub_path   = bprintf(NULL, "%s/zk_pub.bin", period_dir);
    bool  valid      = c4_verify_proof_files(proof_path, pub_path);
    safe_free(period_dir);
    safe_free(proof_path);
    safe_free(pub_path);
    if (valid) {
      if (ctx->period > last_verified_period) last_verified_period = ctx->period;
    }
    else {
      log_error("Prover: Generated proof failed local verification for period %l", ctx->period);
      prover_stats.total_failure++;
    }
  }
  else {
    log_error("Prover: Proof generation failed for period %l (code: %ld, signal: %d)", ctx->period, exit_status, term_signal);
    prover_stats.total_failure++;
  }

  // Flush any remaining output and close pipes.
  prover_flush_lines(ctx, false, true);
  prover_flush_lines(ctx, true, true);
  // Note: uv_is_closing() only reports "close is in progress". After the close callback ran,
  // the handle is already closed and calling uv_close() again will assert/crash.
  if (!ctx->stdout_closed && !uv_is_closing((uv_handle_t*) &ctx->stdout_pipe)) {
    uv_read_stop((uv_stream_t*) &ctx->stdout_pipe);
    uv_close((uv_handle_t*) &ctx->stdout_pipe, prover_on_pipe_closed);
  }
  if (!ctx->stderr_closed && !uv_is_closing((uv_handle_t*) &ctx->stderr_pipe)) {
    uv_read_stop((uv_stream_t*) &ctx->stderr_pipe);
    uv_close((uv_handle_t*) &ctx->stderr_pipe, prover_on_pipe_closed);
  }

  uv_close((uv_handle_t*) req, prover_on_process_closed);
}

static char* find_next_to_exe(const char* filename) {
  if (!filename) return NULL;
  char   exepath[1024];
  size_t len = sizeof(exepath);
  if (uv_exepath(exepath, &len) != 0 || len == 0) return NULL;
  exepath[len] = 0;
  char* slash  = strrchr(exepath, '/');
#ifdef _WIN32
  char* bslash = strrchr(exepath, '\\');
  if (!slash || (bslash && bslash > slash)) slash = bslash;
#endif
  if (!slash) return NULL;
  *slash = 0;
  return bprintf(NULL, "%s/%s", exepath, filename);
}

static char* find_host_binary() {
  // Preferred: same directory as colibri-server
  char* p = find_next_to_exe("eth-sync-script");
  if (p && access(p, X_OK) == 0) return p;
  if (p) safe_free(p);
#ifdef _WIN32
  p = find_next_to_exe("eth-sync-script.exe");
  if (p && access(p, X_OK) == 0) return p;
  if (p) safe_free(p);
#endif
  // Docker default
  p = strdup("/app/eth-sync-script");
  if (access(p, X_OK) == 0) return p;
  safe_free(p);
  log_error("Prover: eth-sync-script not found next to executable or /app");
  return NULL;
}

static char* find_guest_elf() {
  char* p = find_next_to_exe("eth_sync_program");
  if (p && access(p, X_OK) == 0) return p;
  if (p) safe_free(p);
  p = strdup("/app/eth_sync_program");
  if (access(p, X_OK) == 0) return p;
  safe_free(p);
  log_error("Prover: eth_sync_program not found next to executable or /app");
  return NULL;
}

static void c4_period_prover_spawn(uint64_t target_period, uint64_t prev_period) {
  if (g_prover_running) {
    log_warn("Prover: already running (period=%l, running_period=%l), skipping", target_period, g_prover_running_period);
    return;
  }

  log_info("Prover: Starting proof generation for period %l", target_period);

  g_prover_running          = true;
  g_prover_running_period   = target_period;
  g_prover_running_start_ms = current_ms();

  // Ensure period directory exists (sync.ssz is typically the first file).
  char* period_dir = c4_ps_ensure_period_dir(target_period);
  char* sync_path  = bprintf(NULL, "%s/sync.ssz", period_dir);
  if (!file_exists_min_size(sync_path, ZK_SYNC_MIN_BYTES)) {
    // Generate sync.ssz in-process by calling the existing prover method `eth_proof_sync`.
    char params[64];
    sbprintf(params, "[%l]", target_period);
    prover_ctx_t* pctx = c4_prover_create("eth_proof_sync", params, (chain_id_t) http_server.chain_id,
                                          C4_PROVER_FLAG_UV_SERVER_CTX | http_server.prover_flags);

    request_t*      req_obj = (request_t*) safe_calloc(1, sizeof(request_t));
    sync_gen_ctx_t* sctx    = (sync_gen_ctx_t*) safe_calloc(1, sizeof(sync_gen_ctx_t));
    sctx->target_period     = target_period;
    sctx->prev_period       = prev_period;
    sctx->period_dir        = period_dir;
    sctx->sync_path         = sync_path;

    req_obj->start_time = current_ms();
    req_obj->client     = NULL;
    req_obj->cb         = c4_prover_handle_request;
    req_obj->ctx        = pctx;
    req_obj->parent_ctx = sctx;
    req_obj->parent_cb  = on_sync_generated;

    // Kick off: will fetch pending requests via c4_start_curl_requests and call our callback on success/error.
    req_obj->cb(req_obj);
    return;
  }

  safe_free(period_dir);
  safe_free(sync_path);

  // sync.ssz already present: continue directly.
  c4_period_prover_spawn_host(target_period, prev_period);
}

static void c4_period_prover_spawn_host(uint64_t target_period, uint64_t prev_period) {
  // Preconditions: pipeline already marked as running.
  if (!g_prover_running) {
    g_prover_running          = true;
    g_prover_running_period   = target_period;
    g_prover_running_start_ms = current_ms();
  }

  zk_host_paths_t p = {0};
  p.host_bin        = find_host_binary();
  p.elf_path        = find_guest_elf();
  if (!p.host_bin || !p.elf_path) {
    log_error("Prover: Missing ZK artifacts: eth-sync-script=%s, eth_sync_program=%s",
              p.host_bin ? "ok" : "missing",
              p.elf_path ? "ok" : "missing");
    zk_host_paths_free(&p);
    prover_reset_running();
    return;
  }

  p.period_dir = bprintf(NULL, "%s/%l", eth_config.period_store, target_period);
  p.sync_path  = bprintf(NULL, "%s/sync.ssz", p.period_dir);
  p.prev_dir   = bprintf(NULL, "%s/%l", eth_config.period_store, prev_period);
  p.prev_proof = bprintf(NULL, "%s/zk_proof.bin", p.prev_dir);
  p.prev_vk    = bprintf(NULL, "%s/zk_vk_raw.bin", p.prev_dir);

  if (!file_exists_min_size(p.sync_path, ZK_SYNC_MIN_BYTES)) {
    log_error("Prover: sync.ssz missing for period %l (expected %s)", target_period, p.sync_path);
    zk_host_paths_free(&p);
    prover_reset_running();
    return;
  }

  if (!file_exists_min_size(p.prev_proof, ZK_PREV_PROOF_MIN_BYTES) || !file_exists_min_size(p.prev_vk, ZK_PREV_VK_MIN_BYTES)) {
    log_error("Prover: prev artifacts missing for recursion (prev=%l). Need %s and %s", prev_period, p.prev_proof, p.prev_vk);
    zk_host_paths_free(&p);
    prover_reset_running();
    return;
  }

  // Read SP1 private key from file and pass to rust host via env.
  bytes_t key_bytes = bytes_read(eth_config.period_prover_key_file);
  char*   key       = trim_key_string(key_bytes);
  safe_free(key_bytes.data);
  if (!key) {
    log_error("Prover: Failed to read SP1 private key from %s", eth_config.period_prover_key_file);
    zk_host_paths_free(&p);
    prover_reset_running();
    return;
  }

  // Output paths (match run_zk_proof.sh conventions)
  p.proof_groth16 = bprintf(NULL, "%s/zk_groth16.bin", p.period_dir);
  p.proof_raw     = bprintf(NULL, "%s/zk_proof_g16.bin", p.period_dir);
  p.vk_groth16    = bprintf(NULL, "%s/zk_vk.bin", p.period_dir);
  p.pub_values    = bprintf(NULL, "%s/zk_pub.bin", p.period_dir);
  p.proof_comp    = bprintf(NULL, "%s/zk_proof.bin", p.period_dir);
  p.vk_comp       = bprintf(NULL, "%s/zk_vk_raw.bin", p.period_dir);

  uv_process_t*    proc = (uv_process_t*) malloc(sizeof(uv_process_t));
  zk_prover_ctx_t* ctx  = (zk_prover_ctx_t*) safe_calloc(1, sizeof(zk_prover_ctx_t));
  ctx->period           = target_period;
  ctx->start_time       = current_ms();
  proc->data            = ctx;

  // Setup stdout/stderr pipes for logging.
  uv_loop_t* loop = uv_default_loop();
  uv_pipe_init(loop, &ctx->stdout_pipe, 0);
  uv_pipe_init(loop, &ctx->stderr_pipe, 0);
  ctx->stdout_pipe.data = ctx;
  ctx->stderr_pipe.data = ctx;
  ctx->refcount         = 3;

  uv_process_options_t options = {0};
  options.exit_cb              = on_prover_exit;
  options.file                 = p.host_bin;

  char* args[16];
  int   ai     = 0;
  args[ai++]   = p.host_bin;
  args[ai++]   = "--prove";
  args[ai++]   = "--groth16";
  args[ai++]   = "--input-file";
  args[ai++]   = p.sync_path;
  args[ai++]   = "--prev-proof";
  args[ai++]   = p.prev_proof;
  args[ai++]   = "--prev-vk";
  args[ai++]   = p.prev_vk;
  args[ai++]   = NULL;
  options.args = args;

  uv_stdio_container_t stdio[3] = {0};
  stdio[0].flags                = UV_IGNORE;
  stdio[1].flags                = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
  stdio[1].data.stream          = (uv_stream_t*) &ctx->stdout_pipe;
  stdio[2].flags                = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
  stdio[2].data.stream          = (uv_stream_t*) &ctx->stderr_pipe;
  options.stdio_count           = 3;
  options.stdio                 = stdio;

  // Extend environment for rust host.
  extern char** environ;
  int           env_count = 0;
  while (environ[env_count]) env_count++;

  // We append a handful of env vars + NULL terminator.
  char** new_env = (char**) malloc((env_count + 16) * sizeof(char*));
  for (int i = 0; i < env_count; i++)
    new_env[i] = environ[i];

  // Do not log secrets.
  char* env_elf         = bprintf(NULL, "ELF_PATH=%s", p.elf_path);
  char* env_sp1_key     = bprintf(NULL, "SP1_PRIVATE_KEY=%s", key);
  char* env_net_key     = bprintf(NULL, "NETWORK_PRIVATE_KEY=%s", key);
  char* env_sp1_prover  = "SP1_PROVER=network";
  char* env_skip_verify = "SP1_SKIP_VERIFY=1";

  char* env_proof_out  = bprintf(NULL, "PROOF_OUTPUT_FILE=%s", p.proof_groth16);
  char* env_proof_raw  = bprintf(NULL, "PROOF_RAW_FILE=%s", p.proof_raw);
  char* env_vk_out     = bprintf(NULL, "VK_OUTPUT_FILE=%s", p.vk_groth16);
  char* env_pub_out    = bprintf(NULL, "PUBLIC_VALUES_FILE=%s", p.pub_values);
  char* env_proof_comp = bprintf(NULL, "PROOF_COMPRESSED_OUTPUT_FILE=%s", p.proof_comp);
  char* env_vk_comp    = bprintf(NULL, "VK_COMPRESSED_OUTPUT_FILE=%s", p.vk_comp);

  int e        = env_count;
  new_env[e++] = env_sp1_prover;
  new_env[e++] = env_skip_verify;
  new_env[e++] = env_elf;
  new_env[e++] = env_sp1_key;
  new_env[e++] = env_net_key;
  new_env[e++] = env_proof_out;
  new_env[e++] = env_proof_raw;
  new_env[e++] = env_vk_out;
  new_env[e++] = env_pub_out;
  new_env[e++] = env_proof_comp;
  new_env[e++] = env_vk_comp;
  new_env[e++] = NULL;
  options.env  = new_env;

  int r = uv_spawn(uv_default_loop(), proc, &options);

  // Cleanup (do not free string literals)
  safe_free(key);
  safe_free(env_elf);
  safe_free(env_sp1_key);
  safe_free(env_net_key);
  safe_free(env_proof_out);
  safe_free(env_proof_raw);
  safe_free(env_vk_out);
  safe_free(env_pub_out);
  safe_free(env_proof_comp);
  safe_free(env_vk_comp);
  safe_free(new_env);

  zk_host_paths_free(&p);

  if (r) {
    log_error("Prover: Failed to spawn eth-sync-script: %s", uv_strerror(r));
    free(proc);
    prover_reset_running();
    // No process handle was created, so lifetime is only the pipes.
    ctx->refcount = 2;
    if (!uv_is_closing((uv_handle_t*) &ctx->stdout_pipe))
      uv_close((uv_handle_t*) &ctx->stdout_pipe, prover_on_pipe_closed);
    if (!uv_is_closing((uv_handle_t*) &ctx->stderr_pipe))
      uv_close((uv_handle_t*) &ctx->stderr_pipe, prover_on_pipe_closed);
    prover_stats.total_failure++;
  }
  else {
    uv_read_start((uv_stream_t*) &ctx->stdout_pipe, prover_alloc_cb, prover_read_cb);
    uv_read_start((uv_stream_t*) &ctx->stderr_pipe, prover_alloc_cb, prover_read_cb);
  }
}

void c4_period_prover_on_checkpoint(uint64_t period) {
  // Slave check or no store check
  if (eth_config.period_master_url) return;
  if (!eth_config.period_store) return;
  if (!eth_config.period_prover_key_file) return;

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
    bool valid = c4_verify_proof_files(proof_path, pub_path);

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
