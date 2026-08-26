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

#ifndef ETH_ACCOUNT_H
#define ETH_ACCOUNT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "beacon_types.h"
#include "verify.h"

extern const uint8_t* EMPTY_HASH;
extern const uint8_t* EMPTY_ROOT_HASH;
typedef enum {
  ETH_ACCOUNT_NONE         = 0,
  ETH_ACCOUNT_NONCE        = 1,
  ETH_ACCOUNT_BALANCE      = 2,
  ETH_ACCOUNT_STORAGE_HASH = 3,
  ETH_ACCOUNT_CODE_HASH    = 4,
  ETH_ACCOUNT_PROOF        = 5,
} eth_account_field_t;

struct call_account;
typedef struct call_account call_account_t;

bool                eth_verify_account_proof_exec(verify_ctx_t* ctx, ssz_ob_t* proof, bytes32_t state_root, eth_account_field_t field, bytes_t value);
bool                eth_get_storage_value(ssz_ob_t storage, const bytes32_t key, bytes32_t value);
eth_account_field_t eth_account_get_field(verify_ctx_t* ctx);
bool                eth_account_verify_data(verify_ctx_t* ctx, address_t verified_address, eth_account_field_t field, bytes_t values);
c4_status_t         eth_fetch_account_code(verify_ctx_t* ctx, call_account_t* ac);
/**
 * Resolves code for all accounts that have `ACCOUNT_HAS_CODE_HASH` but not `ACCOUNT_HAS_CODE`.
 *
 * Sources (in priority order): storage plugin cache, `eth_getCode` RPC.
 * When code is heap-allocated (cache or RPC), `ACCOUNT_FREE_CODE` is set on the account.
 * Inline SSZ code is expected to be set already by the caller (e.g. `call_accounts_from_ssz`).
 *
 * @param ctx      verification context
 * @param accounts unified account list (modified in-place)
 * @return C4_SUCCESS, C4_PENDING (RPC fetch needed), or C4_ERROR
 */
c4_status_t eth_resolve_account_codes(verify_ctx_t* ctx, struct call_account* accounts);

#ifdef __cplusplus
}
#endif

#endif
