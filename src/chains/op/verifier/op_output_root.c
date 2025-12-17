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

#include "op_output_root.h"
#include "crypto.h"
#include <string.h>
#include <stdbool.h>

void op_reconstruct_output_root(
    const bytes32_t version,
    const bytes32_t state_root,
    const bytes32_t message_passer_storage_root,
    const bytes32_t latest_block_hash,
    bytes32_t output_root
) {
    uint8_t concat[128];

    memcpy(concat + 0,  version, 32);
    memcpy(concat + 32, state_root, 32);
    memcpy(concat + 64, message_passer_storage_root, 32);
    memcpy(concat + 96, latest_block_hash, 32);

    // Compute keccak256 hash
    keccak(bytes(concat, 128), output_root);
}

void op_calculate_output_storage_slot(
    const uint256_t* output_index,
    const uint256_t* mapping_slot,
    bytes32_t storage_slot
) {
    uint8_t concat[64];

    memcpy(concat, output_index->bytes, 32);
    memcpy(concat + 32, mapping_slot->bytes, 32);
    keccak(bytes(concat, 64), storage_slot);
}

bool op_extract_output_root_from_storage(
    bytes_t storage_proof_value,
    bytes32_t output_root
) {
    if (storage_proof_value.len < 32) {
        return false;
    }

    // Extract the outputRoot (first 32 bytes)
    memcpy(output_root, storage_proof_value.data, 32);
    return true;
}