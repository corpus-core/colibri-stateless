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

c4_status_t c4_fetch_zk_proof_data(prover_ctx_t* ctx, zk_proof_data_t* zk_proof, uint64_t period) {
  c4_status_t status       = C4_SUCCESS;
  char        buffer[1000] = {0};
  buffer_t    buf          = stack_buffer(buffer);
  memcpy(zk_proof->vk, VK_PROGRAM_HASH, 32);
  ssz_ob_t bootstrap = {0};
  TRY_ADD_ASYNC(status, c4_send_internal_request(ctx, bprintf(&buf, "period_store/%l/zk_proof_g16.bin", period), NULL, 0, &zk_proof->proof)); // get the blockd
  buffer_reset(&buf);
  TRY_ADD_ASYNC(status, c4_send_internal_request(ctx, bprintf(&buf, "period_store/%l/lcb.ssz", period), NULL, 0, &bootstrap.bytes)); // get the blockd

  if (status == C4_SUCCESS) {
    const ssz_def_t* bootstrap_union_def = ssz_get_def(C4_ETH_REQUEST_SYNCDATA_UNION + 2, "bootstrap");
    fork_id_t        fork                = c4_eth_get_fork_for_lcu(ctx->chain_id, bootstrap.bytes);
    bootstrap.def                        = &bootstrap_union_def->def.container.elements[fork == C4_FORK_DENEB ? 1 : 2]; // get the correct bootstrap definition for the fork
    if (!ssz_is_valid(bootstrap, true, &ctx->state)) THROW_ERROR("Invalid bootstrap data!");

    zk_proof->bootstrap = bootstrap;
  }
  return status;
}