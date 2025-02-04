#ifndef C4_PROOFS_H
#define C4_PROOFS_H

#include "proofer.h"

#ifdef __cplusplus
extern "C" {
#endif

c4_status_t c4_proof_account(proofer_ctx_t* ctx);
c4_status_t c4_proof_transaction(proofer_ctx_t* ctx);
#ifdef __cplusplus
}
#endif

#endif