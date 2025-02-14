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

// get the block receipts for the given block
c4_status_t eth_getBlockReceipts(proofer_ctx_t* ctx, json_t block, json_t* receipts_array);

// serialize the receipt for the given json using the buffer to allocate memory
bytes_t c4_serialize_receipt(json_t r, buffer_t* buf);

c4_status_t c4_send_eth_rpc(proofer_ctx_t* ctx, char* method, char* params, json_t* result);

#ifdef __cplusplus
}
#endif
#endif
