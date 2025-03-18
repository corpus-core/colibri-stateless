#ifndef ETH_ACCOUNT_H
#define ETH_ACCOUNT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "verify.h"
#define STATE_ROOT_GINDEX 802
typedef enum {
  ETH_ACCOUNT_NONE         = 0,
  ETH_ACCOUNT_NONCE        = 1,
  ETH_ACCOUNT_BALANCE      = 2,
  ETH_ACCOUNT_STORAGE_HASH = 3,
  ETH_ACCOUNT_CODE_HASH    = 4,
} eth_account_field_t;

typedef struct call_code {
  bytes32_t         hash;
  bytes_t           code;
  bool              free;
  struct call_code* next;
} call_code_t;

bool        eth_verify_account_proof_exec(verify_ctx_t* ctx, ssz_ob_t* proof, bytes32_t state_root, eth_account_field_t field, bytes32_t value);
bool        eth_get_storage_value(ssz_ob_t storage, bytes32_t value);
void        eth_get_account_value(ssz_ob_t account, eth_account_field_t field, bytes32_t value);
c4_status_t eth_get_call_codes(verify_ctx_t* ctx, call_code_t** call_codes, ssz_ob_t accounts);
void        eth_free_codes(call_code_t* call_codes);
#ifdef __cplusplus
}
#endif

#endif
