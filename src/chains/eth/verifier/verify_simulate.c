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

static bytes_t bytes32_trimmed(const bytes32_t val) {
  uint32_t i = 0;
  while (i < 31 && val[i] == 0) i++;
  return bytes((uint8_t*) val + i, 32 - i);
}

static bytes_t uint64_as_bytes(uint64_t val, uint8_t buf[8]) {
  if (val == 0) return bytes(buf, 0);
  uint32_t len = 0;
  for (int i = 7; i >= 0; i--) {
    buf[i] = (uint8_t) (val & 0xFF);
    val >>= 8;
  }
  while (len < 7 && buf[len] == 0) len++;
  return bytes(buf + len, 8 - len);
}

static keccak_entry_t* find_keccak_preimage(keccak_entry_t* entries, const bytes32_t key) {
  for (keccak_entry_t* e = entries; e; e = e->next)
    if (memcmp(e->hash, key, 32) == 0) return e;
  return NULL;
}

static bool account_has_changes(const call_account_t* acc) {
  if (acc->flags & (ACCOUNT_BALANCE_MODIFIED | ACCOUNT_NONCE_MODIFIED)) return true;
  for (call_storage_t* s = acc->storage; s; s = s->next)
    if (s->modified) return true;
  return false;
}

static void build_state_changes(ssz_builder_t* builder, call_account_t* accounts, keccak_entry_t* keccak_entries) {
  size_t account_count = 0;
  for (call_account_t* acc = accounts; acc; acc = acc->next)
    if (account_has_changes(acc)) account_count++;
  if (!account_count) {
    ssz_add_bytes(builder, "stateChanges", NULL_BYTES);
    return;
  }

  ssz_builder_t changes_builder = ssz_builder_for_def(ssz_get_def(builder->def, "stateChanges"));

  for (call_account_t* acc = accounts; acc; acc = acc->next) {
    if (!account_has_changes(acc)) continue;

    ssz_builder_t acc_builder = ssz_builder_for_def(changes_builder.def->def.vector.type);

    uint8_t  acc_mask = ETH_SIMULATION_ACCOUNT_CHANGE_MASK_ADDRESS;
    uint32_t storage_change_count = 0;
    for (call_storage_t* s = acc->storage; s; s = s->next)
      if (s->modified) storage_change_count++;

    if (storage_change_count)                      acc_mask |= ETH_SIMULATION_ACCOUNT_CHANGE_MASK_STORAGE;
    if (acc->flags & ACCOUNT_NONCE_MODIFIED)   acc_mask |= ETH_SIMULATION_ACCOUNT_CHANGE_MASK_NONCE;
    if (acc->flags & ACCOUNT_BALANCE_MODIFIED) acc_mask |= ETH_SIMULATION_ACCOUNT_CHANGE_MASK_BALANCE;

    ssz_add_uint8(&acc_builder, acc_mask);
    ssz_add_bytes(&acc_builder, "address", bytes(acc->address, 20));

    // storage changes
    ssz_builder_t storage_builder = ssz_builder_for_def(ssz_get_def(acc_builder.def, "storage"));
    for (call_storage_t* s = acc->storage; s; s = s->next) {
      if (!s->modified) continue;

      ssz_builder_t slot_builder = ssz_builder_for_def(storage_builder.def->def.vector.type);
      keccak_entry_t* preimage   = find_keccak_preimage(keccak_entries, s->key);

      uint8_t slot_mask = ETH_SIMULATION_STORAGE_CHANGE_MASK_BASE;
      if (preimage) slot_mask |= ETH_SIMULATION_STORAGE_CHANGE_MASK_SLOT_SOURCE;
      ssz_add_uint8(&slot_builder, slot_mask);
      ssz_add_bytes(&slot_builder, "slot", bytes(s->key, 32));
      ssz_add_bytes(&slot_builder, "previousValue", bytes(s->src_value, 32));
      ssz_add_bytes(&slot_builder, "newValue", bytes(s->post_value, 32));
      if (preimage && preimage->input.len <= 1024)
        ssz_add_bytes(&slot_builder, "slotSource", preimage->input);
      else
        ssz_add_bytes(&slot_builder, "slotSource", NULL_BYTES);

      ssz_add_dynamic_list_builders(&storage_builder, storage_change_count, slot_builder);
    }
    ssz_add_builders(&acc_builder, "storage", storage_builder);

    // nonce change
    {
      ssz_builder_t nonce_builder = ssz_builder_for_def(ssz_get_def(acc_builder.def, "nonce"));
      uint8_t       prev_buf[8], new_buf[8];
      ssz_add_bytes(&nonce_builder, "previousValue", uint64_as_bytes(acc->src_nonce, prev_buf));
      ssz_add_bytes(&nonce_builder, "newValue", uint64_as_bytes(acc->nonce, new_buf));
      ssz_add_builders(&acc_builder, "nonce", nonce_builder);
    }

    // balance change
    {
      ssz_builder_t balance_builder = ssz_builder_for_def(ssz_get_def(acc_builder.def, "balance"));
      ssz_add_bytes(&balance_builder, "previousValue", bytes32_trimmed(acc->src_balance));
      ssz_add_bytes(&balance_builder, "newValue", bytes32_trimmed(acc->balance));
      ssz_add_builders(&acc_builder, "balance", balance_builder);
    }

    ssz_add_dynamic_list_builders(&changes_builder, account_count, acc_builder);
  }

  ssz_add_builders(builder, "stateChanges", changes_builder);
}

ssz_ob_t eth_build_simulation_result_ssz(bytes_t call_result, emitted_log_t* logs, bool success, uint64_t gas_used, ssz_ob_t* execution_payload, call_account_t* accounts, keccak_entry_t* keccak_entries) {
  ssz_builder_t builder = ssz_builder_for_def(eth_ssz_verification_type(ETH_SSZ_DATA_SIMULATION));

  bool has_state_changes = false;
  for (call_account_t* a = accounts; a && !has_state_changes; a = a->next)
    has_state_changes = account_has_changes(a);

  uint32_t result_mask = ETH_SIMULATION_RESULT_MASK_GAS_USED | ETH_SIMULATION_RESULT_MASK_LOGS | ETH_SIMULATION_RESULT_MASK_STATUS | ETH_SIMULATION_RESULT_MASK_RETURN_VALUE;
  if (has_state_changes) result_mask |= ETH_SIMULATION_RESULT_MASK_STATE_CHANGES;
  ssz_add_uint32(&builder, result_mask);
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

  build_state_changes(&builder, accounts, keccak_entries);

  // Build and return the SSZ object
  return ssz_builder_to_bytes(&builder);
}
