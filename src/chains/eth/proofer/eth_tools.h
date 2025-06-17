#ifndef ETH_TOOLS_H
#define ETH_TOOLS_H

#include "beacon.h"
#include "eth_proofer.h"
#include "historic_proof.h"
#include "ssz.h"
#define NULL_SSZ_BUILDER      (ssz_builder_t){0}
#define FROM_JSON(data, type) ssz_builder_from(ssz_from_json(data, eth_ssz_verification_type(type)))

bytes_t       eth_create_proof_request(chain_id_t chain_id, ssz_builder_t data, ssz_builder_t proof, ssz_builder_t sync_data);
ssz_builder_t eth_ssz_create_state_proof(proofer_ctx_t* ctx, json_t block_number, beacon_block_t* block, blockroot_proof_t* historic_proof);

#ifdef PROOFER_CACHE
uint8_t* c4_eth_receipt_cachekey(bytes32_t target, bytes32_t blockhash);
#endif

#endif