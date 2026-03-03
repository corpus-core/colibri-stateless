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

#ifndef C4_ETH_STATE_OVERRIDES_H
#define C4_ETH_STATE_OVERRIDES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../../verifier/verify.h"

typedef struct eth_storage_override {
  bytes32_t                    key;
  bytes32_t                    value;
  struct eth_storage_override* next;
} eth_storage_override_t;

typedef struct eth_account_override {
  address_t                    address;
  bool                         has_balance;
  bytes32_t                    balance; // Big-endian uint256, left padded to 32 bytes
  bool                         has_code;
  bytes_t                      code;       // Raw EVM bytecode
  bool                         full_state; // true for `state` overrides, false for `stateDiff`
  eth_storage_override_t*      storage;    // Overridden storage slots (state/stateDiff)
  struct eth_account_override* next;
} eth_account_override_t;

typedef struct eth_state_overrides {
  eth_account_override_t* accounts;
} eth_state_overrides_t;

/**
 * Parse and validate a state override set (Geth/Besu-style), without nonce support.
 *
 * Supported per-account fields:
 * - `balance`: hexuint
 * - `code`: bytes
 * - `state`: object of { slot(bytes32) : value(bytes32) } (full storage override)
 * - `stateDiff`: object of { slot(bytes32) : value(bytes32) } (patch storage override)
 *
 * Unsupported fields (cause C4_ERROR): `nonce`, `movePrecompileToAddress`, `blockOverrides`, any unknown keys.
 *
 * @param ctx Verification context used for error reporting
 * @param overrides JSON object mapping address -> override object (or JSON_TYPE_NOT_FOUND/NULL for none)
 * @param out Parsed override set (must be freed via eth_state_overrides_free on success)
 * @return C4_SUCCESS on success, C4_ERROR on validation error
 */
c4_status_t eth_parse_state_overrides(verify_ctx_t* ctx, json_t overrides, eth_state_overrides_t* out);

/**
 * Same as eth_parse_state_overrides(), but accepts a raw c4_state_t for reuse in prover code.
 *
 * @param state State used for error reporting
 * @param overrides JSON object mapping address -> override object (or JSON_TYPE_NOT_FOUND/NULL for none)
 * @param out Parsed override set (must be freed via eth_state_overrides_free on success)
 * @return C4_SUCCESS on success, C4_ERROR on validation error
 */
c4_status_t eth_parse_state_overrides_state(c4_state_t* state, json_t overrides, eth_state_overrides_t* out);

/**
 * Find an override entry for an address.
 *
 * @param overrides Parsed override set
 * @param address 20-byte address
 * @return Pointer to override entry or NULL if not found
 */
const eth_account_override_t* eth_state_overrides_find(const eth_state_overrides_t* overrides, const address_t address);

/**
 * Free all memory owned by the override set.
 *
 * @param overrides Override set to free
 */
void eth_state_overrides_free(eth_state_overrides_t* overrides);

#ifdef __cplusplus
}
#endif

#endif /* C4_ETH_STATE_OVERRIDES_H */
