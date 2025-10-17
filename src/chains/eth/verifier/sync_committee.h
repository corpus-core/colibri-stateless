/*
 * Copyright (c) 2025 corpus.core
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef sync_committee_h__
#define sync_committee_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "beacon_types.h"
#include "bytes.h"
#include "state.h"
#include "verify.h"
#include <stdint.h>

typedef struct {
  uint32_t  lowest_period;  // the lowest period, but the closest period before the target.
  uint32_t  current_period; // the target period we are searching for.
  uint32_t  highest_period; // the highest period we have the keys for.
  bytes_t   validators;     // the found validators or NULL_BYTES if not found.
  uint64_t  last_checkpoint;
  bool      deserialized;
  bytes32_t previous_pubkeys_hash;
} c4_sync_state_t;

typedef struct {
  uint8_t   slot_bytes[8];
  bytes32_t blockhash;
  uint8_t   period_bytes[4];
  uint8_t   flags[4];
} c4_trusted_block_t;

typedef struct {
  c4_trusted_block_t* blocks;
  uint32_t            len;
  uint64_t            last_checkpoint;
} c4_chain_state_t;

const c4_status_t c4_get_validators(verify_ctx_t* ctx, uint32_t period, c4_sync_state_t* state, bytes32_t pubkey_hash);
bool              c4_update_from_sync_data(verify_ctx_t* ctx);
bool              c4_handle_client_updates(verify_ctx_t* ctx, bytes_t client_updates);
bool              c4_set_sync_period(uint32_t period, uint64_t slot, bytes32_t blockhash, ssz_ob_t sync_committee, chain_id_t chain_id, bytes32_t previous_pubkey_hash);
c4_chain_state_t  c4_get_chain_state(chain_id_t chain_id); // make sure to free the chain_state.blocks after use
void              c4_eth_set_trusted_blockhashes(chain_id_t chain_id, bytes_t blockhashes);
uint32_t          c4_eth_get_oldest_period(bytes_t state);
fork_id_t         c4_eth_get_fork_for_lcu(chain_id_t chain_id, bytes_t data);

#ifdef __cplusplus
}
#endif

#endif