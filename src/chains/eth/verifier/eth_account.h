#ifndef ETH_ACCOUNT_H
#define ETH_ACCOUNT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "verify.h"
#define STATE_ROOT_GINDEX 802

bool eth_verify_account_proof_exec(verify_ctx_t* ctx, ssz_ob_t* proof, bytes32_t state_root);

#ifdef __cplusplus
}
#endif

#endif
