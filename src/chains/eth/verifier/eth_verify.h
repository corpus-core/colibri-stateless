#ifndef eth_verify_h__
#define eth_verify_h__

#include "verify.h"

bool verify_blockhash_proof(verify_ctx_t* ctx);
bool verify_account_proof(verify_ctx_t* ctx);
bool verify_tx_proof(verify_ctx_t* ctx);
bool verify_receipt_proof(verify_ctx_t* ctx);
bool verify_logs_proof(verify_ctx_t* ctx);
bool verify_call_proof(verify_ctx_t* ctx);

// helper
c4_status_t c4_verify_blockroot_signature(verify_ctx_t* ctx, ssz_ob_t* header, ssz_ob_t* sync_committee_bits, ssz_ob_t* sync_committee_signature, uint64_t slot);

#endif // eth_verify_h__
