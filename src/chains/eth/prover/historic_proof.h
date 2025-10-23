/*
 * Copyright (c) 2025 corpus.core
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef C4_HISTORIC_PROOF_H
#define C4_HISTORIC_PROOF_H

#ifdef __cplusplus
extern "C" {
#endif

#include "beacon.h"
#include "prover.h"
#include "ssz.h"

typedef enum {
  HISTORIC_PROOF_NONE   = 0,
  HISTORIC_PROOF_DIRECT = 1,
  HISTORIC_PROOF_HEADER = 2,
} historic_proof_type_t;

typedef struct {
  historic_proof_type_t type;
  ssz_ob_t              sync_aggregate;
  bytes_t               historic_proof;
  gindex_t              gindex;
  bytes_t               proof_header;
} blockroot_proof_t;

c4_status_t c4_check_historic_proof(prover_ctx_t* ctx, blockroot_proof_t* block_proof, beacon_block_t* block);
void        ssz_add_header_proof(ssz_builder_t* builder, beacon_block_t* block_data, blockroot_proof_t block_proof);
void        c4_free_block_proof(blockroot_proof_t* block_proof);
#ifdef __cplusplus
}
#endif

#endif