#ifndef eth_verify_h__
#define eth_verify_h__

#include "verify.h"

bool verify_blockhash_proof(verify_ctx_t* ctx);
bool verify_account_proof(verify_ctx_t* ctx);
bool verify_tx_proof(verify_ctx_t* ctx);
bool verify_receipt_proof(verify_ctx_t* ctx);
bool verify_logs_proof(verify_ctx_t* ctx);
bool verify_call_proof(verify_ctx_t* ctx);
bool verify_block_proof(verify_ctx_t* ctx);
bool verify_eth_local(verify_ctx_t* ctx);
// helper
c4_status_t c4_verify_blockroot_signature(verify_ctx_t* ctx, ssz_ob_t* header, ssz_ob_t* sync_committee_bits, ssz_ob_t* sync_committee_signature, uint64_t slot);
bool        eth_calculate_domain(chain_id_t chain_id, uint64_t slot, bytes32_t domain);
#endif // eth_verify_h__
