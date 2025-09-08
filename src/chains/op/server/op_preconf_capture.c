// op_preconf_capture.c
#include "op_preconf_capture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void cleanup_handle(op_capture_handle* h) {
  if (h->argv) {
    for (size_t i = 0; i < h->argv_count; ++i) {
      // Nur die mit strdup() allozierten Strings freigeben
      // Das sind die Indizes 2 (chain_buf) und 4 (hf_buf)
      if (h->argv[i] && (i == 2 || i == 4)) {
        free(h->argv[i]);
      }
    }
    free(h->argv);
    h->argv = NULL;
  }
}

static void on_process_exit(uv_process_t* req, int64_t exit_status, int term_signal) {
  op_capture_handle* h = (op_capture_handle*) req->data;
  h->running           = false;

  // Cleanup memory
  cleanup_handle(h);

  // Close handles
  uv_close((uv_handle_t*) &h->stdout_pipe, NULL);
  uv_close((uv_handle_t*) &h->stderr_pipe, NULL);
  uv_close((uv_handle_t*) req, NULL);
}

static void alloc_cb(uv_handle_t* handle, size_t suggested, uv_buf_t* buf) {
  buf->base = (char*) malloc(suggested);
  buf->len  = suggested;
}

static void read_log_cb(uv_stream_t* s, ssize_t nread, const uv_buf_t* buf) {
  if (nread > 0) {
    // Unterscheide stdout/stderr über die Handle-Struktur
    op_capture_handle* h      = (op_capture_handle*) s->data;
    FILE*              target = (s == (uv_stream_t*) &h->stdout_pipe) ? stdout : stderr;
    fwrite(buf->base, 1, nread, target);
    fflush(target);
  }
  if (buf->base) free(buf->base);
}

int op_preconf_start(uv_loop_t* loop, const op_chain_config* cfg, op_capture_handle** out) {
  if (!loop || !cfg || !cfg->bridge_path || !cfg->out_dir) return -1;

  op_capture_handle* h = (op_capture_handle*) calloc(1, sizeof(*h));
  if (!h) return -1;

  h->loop    = loop;
  h->running = true;

  // Initialize pipes mit handle-referenz für saubere stdout/stderr-unterscheidung
  // Wichtig: Zuerst memset um sicherzustellen, dass alle Felder auf 0 stehen
  memset(&h->stdout_pipe, 0, sizeof(h->stdout_pipe));
  memset(&h->stderr_pipe, 0, sizeof(h->stderr_pipe));
  memset(&h->proc, 0, sizeof(h->proc));

  int rc = uv_pipe_init(loop, &h->stdout_pipe, 0);
  if (rc != 0) {
    free(h);
    return rc;
  }

  rc = uv_pipe_init(loop, &h->stderr_pipe, 0);
  if (rc != 0) {
    uv_close((uv_handle_t*) &h->stdout_pipe, NULL);
    free(h);
    return rc;
  }

  h->stdout_pipe.data = h;
  h->stderr_pipe.data = h;

  uv_process_options_t opts;
  memset(&opts, 0, sizeof(opts));

  // Build argv - add space for new parameters
  size_t argc = 14 + cfg->bootnodes_len * 2 + (cfg->use_gossip ? 1 : 0); // +6 for new params
  char** args = (char**) calloc(argc + 1, sizeof(char*));
  size_t i    = 0;
  args[i++]   = (char*) cfg->bridge_path;

  char chain_buf[32], hf_buf[16];
  snprintf(chain_buf, sizeof(chain_buf), "%llu", (unsigned long long) cfg->chain_id);
  snprintf(hf_buf, sizeof(hf_buf), "%d", cfg->hardfork_version);

  args[i++] = "--chain-id";
  args[i++] = strdup(chain_buf);
  args[i++] = "--hf";
  args[i++] = strdup(hf_buf);
  args[i++] = "--out-dir";
  args[i++] = (char*) cfg->out_dir;

  // Pass chain-specific configuration from centralized C config
  if (cfg->chain_name) {
    args[i++] = "--chain-name";
    args[i++] = (char*) cfg->chain_name;
  }
  if (cfg->http_endpoint) {
    args[i++] = "--http-endpoint";
    args[i++] = (char*) cfg->http_endpoint;
  }
  if (cfg->sequencer_address) {
    args[i++] = "--sequencer-address";
    args[i++] = (char*) cfg->sequencer_address;
  }

  // Add --use-http=false for gossip mode (HTTP is default)
  if (cfg->use_gossip) {
    args[i++] = "--use-http=false";
  }

  for (size_t k = 0; k < cfg->bootnodes_len; ++k) {
    args[i++] = "--bootnode";
    args[i++] = (char*) cfg->bootnodes[k];
  }
  args[i] = NULL;

  // Store argv für cleanup
  h->argv       = args;
  h->argv_count = argc;

  opts.file        = cfg->bridge_path;
  opts.args        = args;
  opts.flags       = 0; // Entferne UV_PROCESS_DETACHED für saubere integration
  opts.stdio_count = 3;
  opts.exit_cb     = on_process_exit; // Wichtig: Exit-Handler setzen

  uv_stdio_container_t stdio[3];
  memset(stdio, 0, sizeof(stdio));
  stdio[0].flags       = UV_IGNORE;
  stdio[1].flags       = (uv_stdio_flags) (UV_CREATE_PIPE | UV_WRITABLE_PIPE);
  stdio[1].data.stream = (uv_stream_t*) &h->stdout_pipe;
  stdio[2].flags       = (uv_stdio_flags) (UV_CREATE_PIPE | UV_WRITABLE_PIPE);
  stdio[2].data.stream = (uv_stream_t*) &h->stderr_pipe;
  opts.stdio           = stdio;

  h->proc.data = h;
  rc           = uv_spawn(loop, &h->proc, &opts);
  if (rc != 0) {
    uv_close((uv_handle_t*) &h->stdout_pipe, NULL);
    uv_close((uv_handle_t*) &h->stderr_pipe, NULL);
    cleanup_handle(h);
    free(h);
    return rc;
  }

  // Start reading pipes - jetzt sauber ohne loop->data zu manipulieren
  uv_read_start((uv_stream_t*) &h->stdout_pipe, alloc_cb, read_log_cb);
  uv_read_start((uv_stream_t*) &h->stderr_pipe, alloc_cb, read_log_cb);

  *out = h;
  return 0;
}

int op_preconf_stop(op_capture_handle* h) {
  if (!h || !h->running) return 0;
  h->running = false;

  // Sende SIGTERM zum child process
  int rc = uv_process_kill(&h->proc, SIGTERM);

  // Der on_exit callback wird das cleanup übernehmen
  return rc;
}

void op_preconf_cleanup(op_capture_handle* h) {
  if (!h) return;

  cleanup_handle(h);
  free(h);
}
