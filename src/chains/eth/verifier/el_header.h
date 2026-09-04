/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */
#ifndef el_header_h__
#define el_header_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "beacon_types.h"
#include "bytes.h"
#include "json.h"
#include "ssz.h"

/**
 * Field names of the RLP-encoded execution layer block header (execution payload naming).
 * Use these constants when reading header fields via `eth_el_header_get()` /
 * `eth_el_header_get_uint64()` to avoid typos in the string lookup.
 */
#define EL_PARENT_HASH              "parentHash"
#define EL_SHA3_UNCLES              "sha3Uncles"
#define EL_FEE_RECIPIENT            "feeRecipient"
#define EL_STATE_ROOT               "stateRoot"
#define EL_TRANSACTIONS_ROOT        "transactionsRoot"
#define EL_RECEIPTS_ROOT            "receiptsRoot"
#define EL_LOGS_BLOOM               "logsBloom"
#define EL_DIFFICULTY               "difficulty"
#define EL_BLOCK_NUMBER             "blockNumber"
#define EL_GAS_LIMIT                "gasLimit"
#define EL_GAS_USED                 "gasUsed"
#define EL_TIMESTAMP                "timestamp"
#define EL_EXTRA_DATA               "extraData"
#define EL_PREV_RANDAO              "prevRandao"
#define EL_NONCE                    "nonce"
#define EL_BASE_FEE_PER_GAS         "baseFeePerGas"
#define EL_WITHDRAWALS_ROOT         "withdrawalsRoot"
#define EL_BLOB_GAS_USED            "blobGasUsed"
#define EL_EXCESS_BLOB_GAS          "excessBlobGas"
#define EL_PARENT_BEACON_BLOCK_ROOT "parentBeaconBlockRoot"
#define EL_REQUESTS_HASH            "requestsHash"
#define EL_BLOCK_ACCESS_LIST_HASH   "blockAccessListHash"
#define EL_SLOT_NUMBER              "slotNumber"

typedef struct {
  ssz_ob_t    execution_payload;
  ssz_ob_t    execution_requests;
  fork_id_t   fork;
  c4_state_t* state;
  chain_id_t  chain_id;
  bytes32_t   parent_root;
  ssz_ob_t    beacon_block;
  // Optional extras for chains without a beacon body (OP-Stack preconf).
  // Used when the SSZ payload does not carry the field (requestsHash is never
  // in the payload; slot is used only if the payload has no slotNumber).
  bool      has_requests_hash;
  bytes32_t requests_hash;
  bool      has_slot;
  uint64_t  slot;
} eth_el_header_ctx_t;
void eth_get_withdrawals_root(bytes32_t out_hash, ssz_ob_t withdrawals);
void eth_get_transactions_root(bytes32_t out_hash, ssz_ob_t txs);
/**
 * Computes `block_access_list_hash` as defined in EIP-7928:
 * `keccak256(rlp.encode(block_access_list))`.
 * `rlp_encoded_bal` is the already-RLP-encoded payload field. An empty or
 * missing list is treated as `rlp.encode([])` (`0xc0`).
 *
 * @param out_hash 32-byte output buffer
 * @param rlp_encoded_bal RLP bytes of the BAL (may be empty)
 */
void eth_get_block_access_list_hash(bytes32_t out_hash, bytes_t rlp_encoded_bal);

bytes_t     eth_el_header_get(bytes_t header, char* name);
uint64_t    eth_el_header_get_uint64(bytes_t header, char* name);

// EIP-4844 constants exposed for callers computing blob-related receipt fields.
#define ETH_GAS_PER_BLOB                  131072u  // gas per blob (EIP-4844)
#define ETH_BLOB_BASE_FEE_UPDATE_FRACTION 3338477u // Cancun default; use eth_blob_base_fee_update_fraction() for fork-aware value
#define ETH_MIN_BLOB_BASE_FEE             1u       // wei

/**
 * Fork-aware `BLOB_BASE_FEE_UPDATE_FRACTION` per EIP-7892 (Blob Parameter Only
 * hardforks). The value depends on the containing block's timestamp because
 * mainnet and testnets ratchet it up in successive BPO forks (Cancun → Prague
 * → Osaka → BPO1 → BPO2 ...). Unknown chains fall back to the Cancun value.
 *
 * @param chain_id target chain identifier
 * @param block_timestamp Unix seconds of the containing block's EL header
 * @return blob-base-fee update fraction to feed into `eth_fake_exponential`
 */
uint64_t eth_blob_base_fee_update_fraction(chain_id_t chain_id, uint64_t block_timestamp);

/**
 * EIP-4844 blob base fee: MIN_BLOB_BASE_FEE * e^(excess_blob_gas / BLOB_BASE_FEE_UPDATE_FRACTION),
 * approximated via a Taylor series. Same algorithm the execution layer uses to price blob gas.
 *
 * @param factor Base factor (typically ETH_MIN_BLOB_BASE_FEE).
 * @param numerator Exponent numerator (typically excess_blob_gas).
 * @param denominator Exponent denominator (fork-aware; see `eth_blob_base_fee_update_fraction`).
 * @return factor * e^(numerator/denominator), rounded down.
 */
uint64_t eth_fake_exponential(uint64_t factor, uint64_t numerator, uint64_t denominator);
c4_status_t eth_el_header_build_from_ep(bytes_t* el_header, eth_el_header_ctx_t* ctx);
c4_status_t eth_el_header_build_from_json(c4_state_t* state, bytes_t* el_header, fork_id_t fork, json_t block);
c4_status_t eth_el_header_get_from_raw_block(c4_state_t* state, bytes_t raw_block, bytes_t* el_header, ssz_builder_t* body_builder);

#ifdef __cplusplus
}
#endif

#endif
