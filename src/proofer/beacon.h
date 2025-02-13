#ifndef C4_BEACON_H
#define C4_BEACON_H

#include "../util/json.h"
#include "../util/ssz.h"
#include "proofer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint64_t slot;
  ssz_ob_t header;
  ssz_ob_t execution;
  ssz_ob_t body;
  ssz_ob_t sync_aggregate;
} beacon_block_t;

// ssz_ob_t get_execution_payload(proofer_ctx_t* ctx, ssz_ob_t block);

// void        c4_proof_account(proofer_ctx_t* ctx);
c4_status_t   c4_beacon_get_block_for_eth(proofer_ctx_t* ctx, json_t block, beacon_block_t* beacon_block);
ssz_builder_t c4_proof_add_header(ssz_ob_t block, bytes32_t body_root);
bytes_t       c4_proofer_add_data(json_t data, const char* union_name, buffer_t* tmp);

// c4_status_t get_latest_block(proofer_ctx_t* ctx, ssz_ob_t* sig_block, ssz_ob_t* data_block);
#ifdef __cplusplus
}
#endif

#endif