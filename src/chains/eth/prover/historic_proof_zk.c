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

#include "../server/eth_clients.h"
#include "../zk_verifier/zk_verifier_constants.h"
#include "beacon.h"
#include "beacon_types.h"
#include "eth_req.h"
#include "eth_tools.h"
#include "historic_proof.h"
#include "json.h"
#include "logger.h"
#include "prover.h"
#include "ssz.h"
#include "sync_committee.h"
#include "version.h"
#include <inttypes.h> // Include this header for PRIu64 and PRIx64
#include <stdlib.h>
#include <string.h>
#define MAX_SIGNATURES   5
#define SIGNATURE_LENGTH 65

c4_status_t c4_fetch_zk_proof_data(prover_ctx_t* ctx, zk_proof_data_t* zk_proof, uint64_t period) {
  c4_status_t status                                        = C4_SUCCESS;
  char        sig_buffer[MAX_SIGNATURES * SIGNATURE_LENGTH] = {0};
  buffer_t    signatures                                    = stack_buffer(sig_buffer);
  char        buffer[1000]                                  = {0};
  buffer_t    buf                                           = stack_buffer(buffer);
  zk_proof->sync_proof.def                                  = C4_ETH_REQUEST_SYNCDATA_UNION + 2;

  TRY_ADD_ASYNC(status, c4_send_internal_request(ctx, bprintf(&buf, "period_store/%l/zk_proof.ssz", period), NULL, 0, &zk_proof->sync_proof.bytes)); // get the blockd
  if (ctx->witness_key.len && ctx->witness_key.len % 20 == 0) {
    for (int i = 0; i < ctx->witness_key.len; i += 20) {
      buffer_reset(&buf);
      bytes_t sig_data = {0};
      TRY_ADD_ASYNC(status, c4_send_internal_request(ctx, bprintf(&buf, "period_store/%l/sig_%x", period - 1, bytes(ctx->witness_key.data + i, 20)), NULL, 0, &sig_data)); // get the blockd
      buffer_append(&signatures, sig_data);
    }
  }
  if (status == C4_SUCCESS)
    zk_proof->signatures = signatures.data.len ? bytes_dup(signatures.data) : NULL_BYTES;

  return status;
}