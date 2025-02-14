#ifndef ETH_TX_H
#define ETH_TX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "verify.h"

bool    c4_tx_create_from_address(verify_ctx_t* ctx, bytes_t raw_tx, uint8_t* address);
bool    c4_tx_verify_tx_data(verify_ctx_t* ctx, ssz_ob_t tx_data, bytes_t serialized_tx, bytes32_t block_hash, uint64_t block_number);
bool    c4_tx_verify_tx_hash(verify_ctx_t* ctx, bytes_t raw);
bool    c4_tx_verify_receipt_data(verify_ctx_t* ctx, ssz_ob_t receipt_data, bytes32_t block_hash, uint64_t block_number, uint32_t tx_index, bytes_t tx_raw, bytes_t receipt_raw);
bool    c4_tx_verify_receipt_proof(verify_ctx_t* ctx, ssz_ob_t receipt_proof, uint32_t tx_index, bytes32_t receipt_root, bytes_t* receipt_raw);
bytes_t c4_eth_create_tx_path(uint32_t tx_index, buffer_t* buf);

#ifdef __cplusplus
}
#endif

#endif
