/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef PERIOD_STORE_INTERNAL_H
#define PERIOD_STORE_INTERNAL_H

#include "../ssz/beacon_types.h"
#include "bytes.h"
#include "eth_clients.h"
#include "eth_conf.h"
#include "json.h"
#include "logger.h"
#include "period_store.h"
#include "server.h"
#include "ssz.h"
#include "state.h"
#include "sync_committee.h"
#include "uv_util.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <uv.h>

#define HEADER_SCHEMA    "{data:{root:bytes32,header:{message:{slot:suint,proposer_index:suint,parent_root:bytes32,state_root:bytes32,body_root:bytes32}}}}"
#define SLOTS_PER_PERIOD 8192u
#define HEADER_SIZE      112

// SSZ definition for blocks.ssz: 8192 block roots (bytes32 each)
extern const ssz_def_t BLOCKS;

typedef struct {
  uint64_t  slot;
  bytes32_t root;
  uint8_t   header[HEADER_SIZE];
  bytes32_t parent_root;
} block_t;

typedef struct {
  uint64_t period;  // the period for blocks and headers
  uint8_t* blocks;  // 8192 * 32 bytes block roots
  uint8_t* headers; // 8192 * 112 bytes headers
} period_data_t;

typedef struct {
  block_t current; // current block, which is moving backwards while backfilling
  block_t parent;  // this is only set, when the parent is missing and was fetched.

  period_data_t current_period;
  period_data_t previous_period;

  uint64_t started_ts;
  uint64_t end_slot;
  uint64_t start_slot;
  bool     done;
} backfill_ctx_t;

typedef struct write_task_s {
  block_t              block;
  bool                 run_backfill;
  struct write_task_s* next;
} write_task_t;

typedef struct {
  char*         base_dir;
  uint64_t      last_checked_period;
  write_task_t* head;
  write_task_t* tail;
} write_queue_ctx_t;

typedef struct {
  // lifecycle context across async fs ops
  write_task_t* task;
  char*         blocks_path;
  char*         headers_path;
  uv_file       blocks_fd;
  uv_file       headers_fd;
  uint64_t      blocks_offset;
  uint64_t      headers_offset;
  buffer_t      tmp;
} fs_ctx_t;

// Global state variables (defined in period_store.c)
extern backfill_ctx_t    bf_ctx;
extern write_queue_ctx_t queue;
extern uint64_t          latest_head_slot;
extern uint64_t          latest_hist_period;

// Helper functions shared across period_store modules (namespaced with c4_ps_)

// Returns true if the given file exists in the period store directory for the given period.
bool c4_ps_file_exists(uint64_t period, const char* filename);

// Ensures that the period directory exists on disk and returns its absolute path (caller must free).
char* c4_ps_ensure_period_dir(uint64_t period);

// Finalizes a write operation for blocks/headers and advances the write queue.
void c4_ps_finish_write(fs_ctx_t* ctx, bool ok);

// Drives the backfill state machine a single step.
void c4_ps_backfill(void);

// Marks backfill as finished and frees associated resources.
void c4_ps_backfill_done(void);

// Decides whether and where to (re)start backfill given the current head block.
void c4_ps_backfill_check(block_t* head);

// Executes the pending write_task at the head of the queue.
void c4_ps_run_write_block_queue(void);

// Schedules backfill execution on the event loop.
void c4_ps_enqueue_backfill(void);

// Schedules fetching of a light client update for the given period.
void c4_ps_schedule_fetch_lcu(uint64_t period);

// Schedules fetching of a light client bootstrap for the given checkpoint/period.
void c4_ps_fetch_lcb_for_checkpoint(bytes32_t checkpoint, uint64_t period);

// Schedules fetching of a light client bootstrap for the given period by first
// requesting a LightClientUpdate and deriving the finalized checkpoint from it.
void c4_ps_schedule_fetch_lcb(uint64_t period);

// Queues a block write and optionally triggers backfill.
void c4_ps_set_block(block_t* block, bool run_backfill);

// Schedules verification of cached blocks.ssz against historical_summaries roots.
void c4_ps_schedule_verify_all_blocks_for_historical(uint64_t hist_period);

// Utility macros
#define UVX_CHECK(op, expr, cleanup, retstmt)                                                  \
  do {                                                                                         \
    int _rc = (expr);                                                                          \
    if (_rc < 0) {                                                                             \
      log_error("period_store: %s failed: %s (%s)", (op), uv_strerror(_rc), uv_err_name(_rc)); \
      cleanup;                                                                                 \
      retstmt;                                                                                 \
    }                                                                                          \
  } while (0)

#endif /* PERIOD_STORE_INTERNAL_H */
