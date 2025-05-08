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
  uint32_t last_period;
  uint32_t current_period;
  uint32_t highest_period;
  bytes_t  validators;
  bool     deserialized;
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
} c4_chain_state_t;

const c4_status_t c4_get_validators(verify_ctx_t* ctx, uint32_t period, c4_sync_state_t* state);
bool              c4_update_from_sync_data(verify_ctx_t* ctx);
bool              c4_handle_client_updates(verify_ctx_t* ctx, bytes_t client_updates, bytes32_t trusted_blockhash);
bool              c4_set_sync_period(uint64_t slot, bytes32_t blockhash, bytes_t validators, chain_id_t chain_id);
c4_chain_state_t  c4_get_chain_state(chain_id_t chain_id); // make sure to free the chain_state.blocks after use
void              c4_eth_set_trusted_blockhashes(chain_id_t chain_id, bytes_t blockhashes);
uint32_t          c4_eth_get_last_period(bytes_t state);
fork_id_t         c4_eth_get_fork_for_lcu(chain_id_t chain_id, bytes_t data);

#ifdef __cplusplus
}
#endif

#endif