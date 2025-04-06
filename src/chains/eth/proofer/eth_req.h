#ifndef eth_req_h__
#define eth_req_h__

#ifdef __cplusplus
extern "C" {
#endif
#include "../util/json.h"
#include "../util/state.h"
#include "../verifier/eth_tx.h"
#include "proofer.h"

// get the eth transaction for the given hash
c4_status_t get_eth_tx(proofer_ctx_t* ctx, json_t txhash, json_t* tx_data);

c4_status_t eth_get_proof(proofer_ctx_t* ctx, json_t address, json_t storage_key, json_t* proof, uint64_t block_number);

c4_status_t eth_get_code(proofer_ctx_t* ctx, json_t address, json_t* code, uint64_t block_number);
c4_status_t eth_debug_trace_call(proofer_ctx_t* ctx, json_t tx, json_t* trace, uint64_t block_number);
// get the logs
c4_status_t eth_get_logs(proofer_ctx_t* ctx, json_t params, json_t* logs);

// get the block receipts for the given block
c4_status_t eth_getBlockReceipts(proofer_ctx_t* ctx, json_t block, json_t* receipts_array);

// serialize the receipt for the given json using the buffer to allocate memory
bytes_t c4_serialize_receipt(json_t r, buffer_t* buf);

c4_status_t c4_send_eth_rpc(proofer_ctx_t* ctx, char* method, char* params, uint32_t ttl, json_t* result);

c4_status_t eth_call(proofer_ctx_t* ctx, json_t tx, json_t* result, uint64_t block_number);

c4_status_t get_eth_tx_by_hash_and_index(proofer_ctx_t* ctx, json_t block_hash, uint32_t index, json_t* tx_data);

#ifdef __cplusplus
}
#endif
#endif
