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

bytes_t     eth_el_header_get(bytes_t header, char* name);
c4_status_t eth_el_header_build_from_ep(c4_state_t* state, bytes_t* el_header, fork_id_t fork, ssz_ob_t ep);
c4_status_t eth_el_header_build_from_json(c4_state_t* state, bytes_t* el_header, fork_id_t fork, json_t block);

#ifdef __cplusplus
}
#endif

#endif
