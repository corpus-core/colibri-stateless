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
#define EL_PARENT_HASH             "parentHash"
#define EL_SHA3_UNCLES             "sha3Uncles"
#define EL_FEE_RECIPIENT           "feeRecipient"
#define EL_STATE_ROOT              "stateRoot"
#define EL_TRANSACTIONS_ROOT       "transactionsRoot"
#define EL_RECEIPTS_ROOT           "receiptsRoot"
#define EL_LOGS_BLOOM              "logsBloom"
#define EL_DIFFICULTY              "difficulty"
#define EL_BLOCK_NUMBER            "blockNumber"
#define EL_GAS_LIMIT               "gasLimit"
#define EL_GAS_USED                "gasUsed"
#define EL_TIMESTAMP               "timestamp"
#define EL_EXTRA_DATA              "extraData"
#define EL_PREV_RANDAO             "prevRandao"
#define EL_NONCE                   "nonce"
#define EL_BASE_FEE_PER_GAS        "baseFeePerGas"
#define EL_WITHDRAWALS_ROOT        "withdrawalsRoot"
#define EL_BLOB_GAS_USED           "blobGasUsed"
#define EL_EXCESS_BLOB_GAS         "excessBlobGas"
#define EL_PARENT_BEACON_BLOCK_ROOT "parentBeaconBlockRoot"
#define EL_REQUESTS_HASH           "requestsHash"
#define EL_BLOCK_ACCESS_LIST_HASH  "blockAccessListHash"
#define EL_SLOT_NUMBER             "slotNumber"

typedef struct {
    ssz_ob_t execution_payload;
    fork_id_t fork;
    c4_state_t* state;
    chain_id_t chain_id;
    bytes32_t parent_root;
    ssz_ob_t beacon_block;
} eth_el_header_ctx_t;

bytes_t     eth_el_header_get(bytes_t header, char* name);
uint64_t    eth_el_header_get_uint64(bytes_t header, char* name);
c4_status_t eth_el_header_build_from_ep(bytes_t* el_header, eth_el_header_ctx_t* ctx);
c4_status_t eth_el_header_build_from_json(c4_state_t* state, bytes_t* el_header, fork_id_t fork, json_t block);

#ifdef __cplusplus
}
#endif

#endif
