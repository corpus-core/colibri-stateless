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

#include "historic_proof.h"
#include "prover.h"
#include "version.h"
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

  // Dual-serve during the SP1 v5 -> v6 transition: clients >= 2.0.0 receive the v6
  // proof (`zk_proof_v6.ssz`, `ZKSyncDataV6` with a 356-byte proof), older clients
  // keep the legacy v5 `zk_proof.ssz` (`ZKSyncData` with a 260-byte proof). The two
  // groth16 proof sizes shift every following field, so the read def MUST match the
  // file format -- pick the union variant by client version. `ctx->version == 0`
  // (unknown/pre-versioning) counts as legacy. The WSP anchor for ZK sync data is
  // built at proof-assembly time (see `c4_get_syncdata_proof` in `historic_proof.c`)
  // from a freshly-fetched LightClientBootstrap, so the prover only needs to pick the
  // version-appropriate proof file here.
  bool is_v6               = ctx->version >= C4_ZK_FIRST_V6_VERSION;
  zk_proof->sync_proof.def = eth_ssz_verification_type(c4_zk_syncdata_type(ctx->version));

  const char* proof_name   = is_v6 ? "zk_proof_v6.ssz" : "zk_proof.ssz";
  c4_status_t proof_status = c4_send_internal_request(ctx, bprintf(&buf, "period_store/%l/%s", period, proof_name), NULL, 0, &zk_proof->sync_proof.bytes);
  // Once legacy v5 generation stops, an old client will no longer find its file.
  // Return an explicit upgrade hint instead of a generic "not found" error.
  if (proof_status == C4_ERROR)
    THROW_ERROR(is_v6 ? "no zk sync proof has not yet been generated for this period" : "zk sync proofs for verifier versions < " C4_ZK_FIRST_V6_VERSION_STR " are no longer supported; please upgrade colibri to >= " C4_ZK_FIRST_V6_VERSION_STR);
  TRY_ADD_ASYNC(status, proof_status);

  if (ctx->witness_key.len && ctx->witness_key.len % 20 == 0) {
    for (uint32_t i = 0; i < ctx->witness_key.len; i += 20) {
      buffer_reset(&buf);
      bytes_t     sig_data   = {0};
      c4_status_t sig_status = c4_send_internal_request(ctx, bprintf(&buf, "period_store/%l/sig_%x", period, bytes(ctx->witness_key.data + i, 20)), NULL, 0, &sig_data);
      if (sig_status == C4_ERROR) THROW_ERROR_WITH("There is no signature from 0x%x for period %l", bytes(ctx->witness_key.data + i, 20), period);
      TRY_ADD_ASYNC(status, sig_status);
      buffer_append(&signatures, sig_data);
    }
  }
  if (status == C4_SUCCESS)
    zk_proof->signatures = signatures.data.len ? bytes_dup(signatures.data) : NULL_BYTES;

  return status;
}