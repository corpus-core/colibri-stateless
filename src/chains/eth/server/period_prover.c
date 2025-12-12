/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */
#include "period_prover.h"
#include "../zk_verifier/zk_verifier.h"
#include "bytes.h"
#include "eth_conf.h"
#include "logger.h"
#include "server.h"
#include <stdlib.h>
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
  uint64_t period;
  uint64_t start_time;
} zk_prover_ctx_t;

// Removed file_read_bytes as we use bytes_read from util/bytes.h

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

  uv_close((uv_handle_t*) req, (uv_close_cb) free);
  free(ctx);
}

static char* find_script() {
  // Find script
  char* script = "/app/run_zk_proof.sh";
  if (access(script, X_OK) == 0) return script;
  // Try local dev path relative to CWD (usually build/default)
  // ../../../scripts/run_zk_proof.sh

  script = "../../../scripts/run_zk_proof.sh";
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
  if (!script) return;

  uv_process_t*    req = (uv_process_t*) malloc(sizeof(uv_process_t));
  zk_prover_ctx_t* ctx = (zk_prover_ctx_t*) malloc(sizeof(zk_prover_ctx_t));
  ctx->period          = target_period;
  ctx->start_time      = current_ms();
  req->data            = ctx;

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
    free(ctx);
    prover_stats.total_failure++;
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
