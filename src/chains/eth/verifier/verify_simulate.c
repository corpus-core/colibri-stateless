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

// : Ethereum

// :: Colibri RPC-Methods
// These RPC-Methods are special RPC-Methods in addition to the standards.

// ::: colibri_simulateTransaction
//
// Simulates a Transaction before signing it. The input arguments are the same as eth_call, but the result represents the events created when executing the transaction.

#include "bytes.h"
#include "call_ctx.h"
#include "ssz.h"
#include "verify_data_types.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

const char* eth_decode_known_event(const emitted_log_t* log, ssz_builder_t* inputs_builder);

ssz_ob_t eth_build_simulation_result_ssz(bytes_t call_result, emitted_log_t* logs, bool success, uint64_t gas_used, ssz_ob_t* execution_payload) {
  ssz_builder_t builder = ssz_builder_for_def(eth_ssz_verification_type(ETH_SSZ_DATA_SIMULATION));

  // Build with minimal mask - only essential fields will be shown in JSON
  ssz_add_uint32(&builder, ETH_SIMULATION_RESULT_MASK_GAS_USED | ETH_SIMULATION_RESULT_MASK_LOGS | ETH_SIMULATION_RESULT_MASK_STATUS | ETH_SIMULATION_RESULT_MASK_RETURN_VALUE); // _optmask - corrected bits: 3,4,6,9
  ssz_add_uint64(&builder, execution_payload ? ssz_get_uint64(execution_payload, "blockNumber") : 0);                                                                            // blockNumber (hidden by mask)
  ssz_add_uint64(&builder, gas_used);                                                                                                                                            // cumulativeGasUsed (hidden by mask)
  ssz_add_uint64(&builder, gas_used);                                                                                                                                            // gasUsed (visible)

  // 5. logs (Index 4) - List
  ssz_builder_t logs_builder = ssz_builder_for_def(ssz_get_def(builder.def, "logs"));

  // determine the size beforehand
  size_t log_count = 0;
  for (emitted_log_t* log = logs; log; log = log->next) log_count++;

  for (emitted_log_t* log = logs; log; log = log->next) {
    ssz_builder_t log_builder = ssz_builder_for_def(logs_builder.def->def.vector.type);

    // Try to decode known event (ERC20, ERC721, Uniswap, WETH)
    ssz_builder_t inputs_builder = ssz_builder_for_def(ssz_get_def(log_builder.def, "inputs"));
    const char*   event_name     = eth_decode_known_event(log, &inputs_builder);

    if (event_name) {
      // Decoded event: show inputs, name, and raw fields
      ssz_add_uint16(&log_builder, ETH_SIMULATION_LOG_MASK_INPUTS | ETH_SIMULATION_LOG_MASK_NAME | ETH_SIMULATION_LOG_MASK_RAW);
      ssz_add_uint8(&log_builder, 0); // anonymous = false
      ssz_add_builders(&log_builder, "inputs", inputs_builder);
      ssz_add_bytes(&log_builder, "name", bytes((uint8_t*) event_name, strlen(event_name)));
    }
    else {
      // Unknown event: only show raw field
      ssz_add_uint16(&log_builder, ETH_SIMULATION_LOG_MASK_RAW);
      ssz_add_uint8(&log_builder, 0);
      ssz_add_bytes(&log_builder, "inputs", NULL_BYTES);
      ssz_add_bytes(&log_builder, "name", NULL_BYTES);
    }

    // raw (always visible)
    ssz_builder_t raw_builder = ssz_builder_for_def(ssz_get_def(log_builder.def, "raw"));
    ssz_add_bytes(&raw_builder, "address", bytes(log->address, 20));
    ssz_add_bytes(&raw_builder, "data", log->data);

    ssz_builder_t topics_builder = {0};
    topics_builder.def           = (ssz_def_t*) ssz_get_def(raw_builder.def, "topics");

    for (size_t i = 0; i < log->topics_count; i++)
      ssz_add_dynamic_list_bytes(&topics_builder, log->topics_count, bytes(log->topics[i], 32));
    ssz_add_builders(&raw_builder, "topics", topics_builder);
    ssz_add_builders(&log_builder, "raw", raw_builder);
    ssz_add_dynamic_list_builders(&logs_builder, log_count, log_builder);
  }

  // Add logs list to main builder
  ssz_add_builders(&builder, "logs", logs_builder);    // logs (visible)
  ssz_add_bytes(&builder, "logsBloom", NULL_BYTES);    // logsBloom (hidden by mask)
  ssz_add_uint8(&builder, success ? 1 : 0);            // status (visible)
  ssz_add_bytes(&builder, "trace", NULL_BYTES);        // trace (hidden by mask)
  ssz_add_uint8(&builder, 0);                          // type (hidden by mask)
  ssz_add_bytes(&builder, "returnValue", call_result); // returnValue (visible)

  // Build and return the SSZ object
  return ssz_builder_to_bytes(&builder);
}
