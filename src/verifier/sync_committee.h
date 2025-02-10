#ifndef sync_committee_h__
#define sync_committee_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "../util/bytes.h"
#include "../util/request.h"
#include "verify.h"
#include <stdint.h>

typedef struct {
  uint32_t last_period;
  uint32_t current_period;
  bytes_t  validators;
} c4_sync_state_t;

typedef struct {
  uint64_t  slot;
  bytes32_t blockhash;
  uint32_t  period;
} c4_trusted_block_t;

typedef struct {
  c4_trusted_block_t* blocks;
  uint32_t            len;
} c4_chain_state_t;

const c4_sync_state_t c4_get_validators(uint32_t period, chain_id_t chain_id);
bool                  c4_update_from_sync_data(verify_ctx_t* ctx);
bool                  c4_handle_client_updates(bytes_t client_updates, chain_id_t chain_id, bytes32_t trusted_blockhash);
data_request_t*       c4_set_trusted_blocks(json_t blocks, chain_id_t chain_id, data_request_t* requests, char** error);
bool                  c4_set_sync_period(uint64_t slot, bytes32_t blockhash, bytes_t validators, chain_id_t chain_id);
c4_chain_state_t      c4_get_chain_state(chain_id_t chain_id); // make sure to free the chain_state.blocks after use
#ifdef __cplusplus
}
#endif

#endif