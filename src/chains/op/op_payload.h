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

#ifndef OP_PAYLOAD_H
#define OP_PAYLOAD_H

#include "json.h"
#include "prover.h"
#include "ssz.h"
#include "verify.h"

/**
 * Decompresses and verifies the sequencer-signed OP preconfirmation payload.
 *
 * @param ctx verifier context (used for errors)
 * @param block_proof SSZ object for preconf (`OP_PRECONF` union variant under `block_proof`)
 * @param block_number optional RPC block identifier for consistency checks (may be NULL)
 * @param parent_hash optional output for the 32-byte parent hash prefix before the execution payload
 * @return heap-allocated pointer to `ssz_ob_t` embedded at the start of decompressed storage; caller must `safe_free()`
 */
ssz_ob_t* op_extract_verified_execution_payload(verify_ctx_t* ctx, ssz_ob_t block_proof, json_t* block_number, bytes32_t parent_hash);

/**
 * Decompresses ZSTD preconfirmation payload bytes from an `ssz_builder_t` produced by `c4_op_create_block_proof`.
 *
 * @param block_proof builder holding compressed payload (fixed or dynamic buffer)
 * @return heap pointer to embedded `ssz_ob_t` + backing storage; caller must `safe_free()`
 */
ssz_ob_t* op_decode_preconf_builder_execution_payload(ssz_builder_t* block_proof);

#endif
