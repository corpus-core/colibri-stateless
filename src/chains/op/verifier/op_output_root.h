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

#ifndef OP_OUTPUT_ROOT_H
#define OP_OUTPUT_ROOT_H

#include "bytes.h"
#include "crypto.h"
#include "libs/intx/intx_c_api.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define L2_TO_L1_MESSAGE_PASSER_ADDRESS "0x4200000000000000000000000000000000000016"

/**
 * @brief Reconstructs the OutputRoot for an OP Stack L2 block.
 *
 * @param version                      32 bytes of 0x00 (protocol version)
 * @param state_root                   L2 state root from execution payload
 * @param message_passer_storage_root  L2ToL1MessagePasser contract storage root
 * @param latest_block_hash            Hash of the L2 block
 * @param output_root                  Reconstructed OutputRoot (32 bytes)
 */
void op_reconstruct_output_root(
    const bytes32_t version,
    const bytes32_t state_root,
    const bytes32_t message_passer_storage_root,
    const bytes32_t latest_block_hash,
    bytes32_t output_root
);

/**
 * @brief Calculates the storage slot for a specific output index in L2OutputOracle.
 *
 * @param output_index   The L2 output index
 * @param mapping_slot   The storage slot of l2Outputs mapping in L2OutputOracle
 * @param storage_slot   Calculated storage slot (32 bytes)
 */
void op_calculate_output_storage_slot(
    const uint256_t* output_index,
    const uint256_t* mapping_slot,
    bytes32_t storage_slot
);

/**
 * @brief Extracts the OutputRoot from L2OutputOracle storage proof data.
 *
 * @param storage_proof_value The value from the storage proof
 * @param output_root        Output: extracted OutputRoot (32 bytes)
 * @return true if extraction successful, false otherwise
 */
bool op_extract_output_root_from_storage(
    bytes_t storage_proof_value,
    bytes32_t output_root
);

#ifdef __cplusplus
}
#endif

#endif // OP_OUTPUT_ROOT_H