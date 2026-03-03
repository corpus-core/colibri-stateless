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
#include "bytes.h"
#include "call_ctx.h"
#include "eth_verify.h"
#include "op_types.h"
#include "op_verify.h"
#include "ssz.h"
#include <stdbool.h>
#include <string.h>

bool op_verify_call_proof(verify_ctx_t* ctx) {
  evm_call_ctx_t evm         = {0};
  ssz_ob_t       block_proof = ssz_get(&ctx->proof, "block_proof");

  bool success = verify_evm_call(ctx, &evm);

  if (success) {
    ssz_ob_t* execution_payload = op_extract_verified_execution_payload(ctx, block_proof, NULL, NULL);
    if (!execution_payload)
      success = false;
    else {
      success = memcmp(evm.state_root, ssz_get(execution_payload, "stateRoot").bytes.data, 32) == 0;
      safe_free(execution_payload);
      if (!success) ctx->state.error = strdup("State root mismatch");
    }
  }

  evm_call_ctx_free(&evm);
  ctx->success = success;
  return ctx->success;
}
