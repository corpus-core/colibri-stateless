#ifndef eth_req_h__
#define eth_req_h__

#ifdef __cplusplus
extern "C" {
#endif
#include "../util/json.h"
#include "../util/state.h"
#include "proofer.h"

c4_status_t get_eth_tx(proofer_ctx_t* ctx, json_t txhash, json_t* tx_data);
c4_status_t eth_getBlockReceipts(proofer_ctx_t* ctx, json_t block, json_t* receipts_array);
#ifdef __cplusplus
}
#endif
#endif
