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
