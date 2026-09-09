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

#include "beacon_types.h"

#ifdef PROVER
const ssz_def_t* c4_eth_execution_payload_def(chain_id_t chain_id) {
  return eth_ssz_type_for_denep(ETH_SSZ_EXECUTION_PAYLOAD_CONTAINER, chain_id);
}
#endif

const ssz_def_t* eth_ssz_type_for_fork(eth_ssz_type_t type, fork_id_t fork, chain_id_t chain_id) {
  switch (fork) {
    case C4_FORK_DENEB: return eth_ssz_type_for_denep(type, chain_id);
    case C4_FORK_ELECTRA: return eth_ssz_type_for_electra(type, chain_id);
    // Fulu keeps the Electra `BeaconBlockBody` / `ExecutionPayload` layout
    // (only the state gained `proposer_lookahead`, which we do not parse here).
    case C4_FORK_FULU: return eth_ssz_type_for_electra(type, chain_id);
    case C4_FORK_GLOAS: return eth_ssz_type_for_gloas(type, chain_id);
    default: return NULL;
  }
}
