// op_preconf_capture.h
#pragma once
#include <stdbool.h>
#include <uv.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint64_t     chain_id;         // e.g. 10 (OP), 8453 (Base)
  int          hardfork_version; // 0..3 (v1..v4); default 3 for Isthmus
  const char*  out_dir;          // where files are written
  const char** bootnodes;        // array of libp2p multiaddrs: /ip4/…/tcp/…/p2p/<peerId>
  size_t       bootnodes_len;
  const char*  bridge_path; // path to opg_bridge binary
} op_chain_config;

typedef struct {
  uv_process_t proc;
  uv_pipe_t    stdout_pipe;
  uv_pipe_t    stderr_pipe;
  uv_loop_t*   loop;
  bool         running;
  char**       argv;       // für cleanup
  size_t       argv_count; // für cleanup
} op_capture_handle;

// Starts opg_bridge as a child process, returns 0 on success.
int op_preconf_start(uv_loop_t* loop, const op_chain_config* cfg, op_capture_handle** out);

// Sends SIGTERM and waits for exit; returns exit status if available.
int op_preconf_stop(op_capture_handle* h);

// Manual cleanup (nur verwenden wenn process bereits beendet ist)
void op_preconf_cleanup(op_capture_handle* h);

#ifdef __cplusplus
}
#endif
