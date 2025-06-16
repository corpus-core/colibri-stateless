#ifndef C4_BEACON_H
#define C4_BEACON_H

#include "../util/json.h"
#include "../util/ssz.h"
#include "proofer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FINALITY_KEY "FinalityRoots"
#define DEFAULT_TTL  (3600 * 24) // 1 day
// beacon block including the relevant parts for the proof

typedef struct {
  uint64_t  slot; // slot of the block
  bytes32_t root; // root of the block
} beacon_head_t;

typedef struct {
  uint64_t  slot;             // slot of the block
  ssz_ob_t  header;           // block header
  ssz_ob_t  execution;        // execution payload of the block
  ssz_ob_t  body;             // body of the block
  ssz_ob_t  sync_aggregate;   // sync aggregate with the signature of the block
  bytes32_t sign_parent_root; // the parentRoot of the block containing the signature
  bytes32_t data_block_root;  // the blockroot used for the data block
} beacon_block_t;

// get the beacon block for the given eth block number or hash
c4_status_t c4_eth_get_signblock_and_parent(proofer_ctx_t* ctx, bytes32_t sig_root, bytes32_t data_root, ssz_ob_t* sig_block, ssz_ob_t* data_block, bytes32_t data_root_result);
c4_status_t c4_beacon_get_block_for_eth(proofer_ctx_t* ctx, json_t block, beacon_block_t* beacon_block);

// creates a new header with the body_root passed and returns the ssz_builder_t, which must be freed
ssz_builder_t c4_proof_add_header(ssz_ob_t header, bytes32_t body_root);

c4_status_t c4_send_beacon_json(proofer_ctx_t* ctx, char* path, char* query, uint32_t ttl, json_t* result);
c4_status_t c4_send_beacon_ssz(proofer_ctx_t* ctx, char* path, char* query, const ssz_def_t* def, uint32_t ttl, ssz_ob_t* result);
c4_status_t c4_send_internal_request(proofer_ctx_t* ctx, char* path, char* query, uint32_t ttl, bytes_t* result);
#ifdef PROOFER_CACHE
c4_status_t c4_eth_update_finality(proofer_ctx_t* ctx);
void        c4_beacon_cache_update_blockdata(proofer_ctx_t* ctx, beacon_block_t* beacon_block, uint64_t latest_timestamp, bytes32_t block_root);
#endif

#ifdef __cplusplus
}
#endif

#endif