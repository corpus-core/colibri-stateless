/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef C4_ETH_PERIOD_STORE_H
#define C4_ETH_PERIOD_STORE_H

#include "bytes.h"
#include "server.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
#define HEADER_SIZE 112

typedef struct {
  uint64_t  slot;
  bytes32_t root;
  uint8_t   header[HEADER_SIZE];
  bytes32_t parent_root;
} block_t;

void     c4_ps_schedule_verify_all_blocks_for_historical();
bool     c4_ps_backfill_done();
uint64_t c4_ps_backfill_start_slot();
void     c4_ps_set_block(block_t* block, bool run_backfill);
bool     c4_ps_file_exists(uint64_t period, const char* filename);
void     c4_ps_schedule_fetch_lcb(uint64_t period);
void     c4_ps_fetch_lcb_for_checkpoint(bytes32_t checkpoint, uint64_t period);
void     c4_ps_schedule_fetch_lcu(uint64_t period);
void     c4_ps_schedule_fetch_historical_root(uint64_t period);
char*    c4_ps_ensure_period_dir(uint64_t period);

/**
 * Callback for delivering concatenated LightClientUpdates (SSZ bytes).
 *
 * @param user_data Opaque pointer given to the request function
 * @param updates   Concatenated SSZ bytes of updates (caller takes ownership and must free updates.data)
 * @param error     Optional error message (owned by caller; NULL on success)
 */
typedef void (*light_client_cb)(void* user_data, bytes_t updates, char* error);

/**
 * Called on each new head to persist block root and 112-byte header at the slot position.
 * Safe against reorgs by overwriting the slot index within the current period.
 *
 * @param slot       Beacon slot number
 * @param block_root 32-byte root
 * @param header112  112-byte serialized header (slot, proposer_index, parent_root, state_root, body_root)
 */
void c4_period_sync_on_head(uint64_t slot, const uint8_t block_root[32], const uint8_t header112[112]);

/**
 * Assemble LightClientUpdates from cache for a contiguous range of periods.
 * Missing periods are fetched from Beacon API and saved to cache as lcu.ssz.
 *
 * @param user_data Passed to callback
 * @param period    Start period
 * @param count     Number of consecutive periods
 * @param cb        Completion callback
 */
void c4_get_light_client_updates(void* user_data, uint64_t period, uint32_t count, light_client_cb cb);

/**
 * Handles the period store request.
 *
 * @param r The single request to handle.
 * @return True if the request was handled, false otherwise.
 */
bool c4_handle_period_store(single_request_t* r);

/**
 * Syncs the period store on a checkpoint.
 *
 * @param checkpoint The checkpoint to sync.
 * @param slot The slot of the checkpoint.
 */
void c4_period_sync_on_checkpoint(bytes32_t checkpoint, uint64_t slot);

#ifdef __cplusplus
}
#endif

#endif /* C4_ETH_PERIOD_STORE_H */
