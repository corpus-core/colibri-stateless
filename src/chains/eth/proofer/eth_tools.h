#ifndef ETH_TOOLS_H
#define ETH_TOOLS_H

#include "eth_proofer.h"
#include "ssz.h"

#define NULL_SSZ_BUILDER      (ssz_builder_t){0}
#define FROM_JSON(data, type) ssz_builder_from(ssz_from_json(data, eth_ssz_verification_type(type)))

bytes_t eth_create_proof_request(chain_id_t chain_id, ssz_builder_t data, ssz_builder_t proof, ssz_builder_t sync_data);

#endif