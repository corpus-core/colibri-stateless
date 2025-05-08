#ifndef ETH_PROOFER_H
#define ETH_PROOFER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "proofer.h"

c4_status_t c4_proof_account(proofer_ctx_t* ctx);     // creates an account proof
c4_status_t c4_proof_transaction(proofer_ctx_t* ctx); // creates a transaction proof
c4_status_t c4_proof_receipt(proofer_ctx_t* ctx);     // creates a receipt proof
c4_status_t c4_proof_logs(proofer_ctx_t* ctx);        // creates a logs proof
c4_status_t c4_proof_call(proofer_ctx_t* ctx);
c4_status_t c4_proof_sync(proofer_ctx_t* ctx);
c4_status_t c4_proof_block(proofer_ctx_t* ctx);
c4_status_t c4_proof_block_number(proofer_ctx_t* ctx);
#ifdef __cplusplus
}
#endif

#endif