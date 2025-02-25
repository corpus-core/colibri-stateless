#ifndef C4_BEACON_H
#define C4_BEACON_H

#include "../util/json.h"
#include "../util/ssz.h"
#include "proofer.h"

#ifdef __cplusplus
extern "C" {
#endif

// beacon block including the relevant parts for the proof
typedef struct {
  uint64_t slot;           // slot of the block
  ssz_ob_t header;         // block header
  ssz_ob_t execution;      // execution payload of the block
  ssz_ob_t body;           // body of the block
  ssz_ob_t sync_aggregate; // sync aggregate with the signature of the block
} beacon_block_t;

// get the beacon block for the given eth block number or hash
c4_status_t c4_beacon_get_block_for_eth(proofer_ctx_t* ctx, json_t block, beacon_block_t* beacon_block);

// creates a new header with the body_root passed and returns the ssz_builder_t, which must be freed
ssz_builder_t c4_proof_add_header(ssz_ob_t header, bytes32_t body_root);

// creates the data based on the json as ssz object with the union_name passed and returns the bytes_t, which uses the buffer_t passed for memory
bytes_t c4_proofer_add_data(json_t data, const char* union_name, buffer_t* tmp);

c4_status_t c4_send_beacon_json(proofer_ctx_t* ctx, char* path, char* query, json_t* result);
c4_status_t c4_send_beacon_ssz(proofer_ctx_t* ctx, char* path, char* query, const ssz_def_t* def, ssz_ob_t* result);

#ifdef __cplusplus
}
#endif

#endif