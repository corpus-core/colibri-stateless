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

#include "../../eth/prover/eth_prover.h"
#include "beacon_types.h"
#include "chains.h"
#include "json.h"
#include "op_prover.h"
#include "state.h"
#include <stdlib.h>
#include <string.h>

bool op_prover_execute(prover_ctx_t* ctx) {
  if (c4_chain_type(ctx->chain_id) != C4_CHAIN_TYPE_OP) return false;
  return eth_prover_execute(ctx);
}
/*
0x000000010db094e0 "Error when calling eth-rpc for eth_createAccessList (params:
[{\"to\":\"0x833589fcd6edb6e08f4c7c32d4f71b54bda02913\",\"data\":\"0x313ce567\"},\"0x21ce40c\"]) : failed to apply transaction:
0x0cc9aaa46f2254571550da29f63f2bdd4e5adfa63740a63b2e574663f0317eff err: insufficient funds for gas * price + value: address 0x0000000000000000000000000000000000000000 have 12716509454172597237 want 42342674793536978484327214"
*/