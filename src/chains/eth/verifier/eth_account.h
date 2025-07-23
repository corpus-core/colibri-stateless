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
#define STATE_ROOT_GINDEX 802

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

typedef struct call_code {
  bytes32_t         hash;
  bytes_t           code;
  bool              free;
  struct call_code* next;
} call_code_t;

bool        eth_verify_state_proof(verify_ctx_t* ctx, ssz_ob_t state_proof, bytes32_t state_root);
bool        eth_verify_account_proof_exec(verify_ctx_t* ctx, ssz_ob_t* proof, bytes32_t state_root, eth_account_field_t field, bytes_t value);
bool        eth_get_storage_value(ssz_ob_t storage, bytes32_t value);
void        eth_get_account_value(ssz_ob_t account, eth_account_field_t field, bytes32_t value);
c4_status_t eth_get_call_codes(verify_ctx_t* ctx, call_code_t** call_codes, ssz_ob_t accounts);
void        eth_free_codes(call_code_t* call_codes);
gindex_t    eth_get_gindex_for_block(fork_id_t fork, json_t block);

#ifdef __cplusplus
}
#endif

#endif
