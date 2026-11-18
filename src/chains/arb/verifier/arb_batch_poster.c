/*
 * Copyright (c) 2025,2026 corpus.core GmbH
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

#include "arb_batch_poster.h"
#include "bytes.h"
#include "crypto.h"
#include "json.h"
#include "plugin.h"
#include "state.h"
#include <string.h>

static const uint8_t ARB_ONE_SEQUENCER_INBOX[20] = {
    0x1c, 0x47, 0x96, 0x75, 0xad, 0x55, 0x9d, 0xc1, 0x51, 0xf6,
    0xec, 0x7e, 0xd3, 0xfb, 0xf8, 0xce, 0xe7, 0x95, 0x82, 0xb6};

// Additional Arbitrum rollup configs registered at runtime through
// `c4_arb_register_chain`. The hard-coded Arbitrum One mapping above is
// always consulted first, so a plugin cannot override it.
#define ARB_CHAIN_CONFIG_EXTRA_MAX 8

typedef struct {
  chain_id_t chain_id;
  uint8_t    sequencer_inbox[20];
} arb_chain_config_t;

static arb_chain_config_t arb_chain_configs_extra[ARB_CHAIN_CONFIG_EXTRA_MAX];
static uint32_t           arb_chain_configs_extra_len = 0;

bool arb_sequencer_inbox(chain_id_t chain_id, uint8_t address[20]) {
  if (!address) return false;
  if (chain_id == C4_CHAIN_ARB_ONE) {
    memcpy(address, ARB_ONE_SEQUENCER_INBOX, 20);
    return true;
  }
  for (uint32_t i = 0; i < arb_chain_configs_extra_len; i++) {
    if (arb_chain_configs_extra[i].chain_id == chain_id) {
      memcpy(address, arb_chain_configs_extra[i].sequencer_inbox, 20);
      return true;
    }
  }
  return false;
}

bool c4_arb_register_chain(c4_state_t* state, json_t data) {
  if (!state) return false;
  // `chain_id` accepts both JSON numbers and hex-strings so wallet-style
  // configs (`"0x66eee"`) and plain JSON numbers (`421614`) both work.
  const char* schema_err = json_validate(
      data,
      "{chain_id:uint|hexuint,sequencer_inbox:address}",
      "c4_arb_register_chain");
  if (schema_err) {
    c4_state_add_error(state, schema_err);
    safe_free((void*) schema_err);
    return false;
  }

  chain_id_t chain_id = (chain_id_t) json_get_uint64(data, "chain_id");
  if (chain_id == 0) {
    c4_state_add_error(state, "c4_arb_register_chain: chain_id must not be 0");
    return false;
  }
  uint8_t existing[20] = {0};
  if (arb_sequencer_inbox(chain_id, existing)) {
    c4_state_add_error(state, "c4_arb_register_chain: chain_id is already registered");
    return false;
  }
  if (arb_chain_configs_extra_len >= ARB_CHAIN_CONFIG_EXTRA_MAX) {
    c4_state_add_error(state, "c4_arb_register_chain: extra chain config table is full");
    return false;
  }

  buffer_t buf    = {0};
  bytes_t  parsed = json_as_bytes(json_get(data, "sequencer_inbox"), &buf);
  if (!parsed.data || parsed.len != 20) {
    buffer_free(&buf);
    c4_state_add_error(state, "c4_arb_register_chain: sequencer_inbox must be 20 bytes");
    return false;
  }
  arb_chain_config_t* slot = &arb_chain_configs_extra[arb_chain_configs_extra_len];
  slot->chain_id           = chain_id;
  memcpy(slot->sequencer_inbox, parsed.data, 20);
  buffer_free(&buf);
  arb_chain_configs_extra_len++;
  return true;
}

void c4_arb_reset_chain_config_registry(void) {
  memset(arb_chain_configs_extra, 0, sizeof(arb_chain_configs_extra));
  arb_chain_configs_extra_len = 0;
}

void arb_batch_poster_storage_key(const uint8_t poster[20], uint8_t out[32]) {
  uint8_t preimage[64] = {0};
  if (!poster || !out) return;
  memcpy(preimage + 12, poster, 20);
  preimage[63] = (uint8_t) ARB_IS_BATCH_POSTER_SLOT;
  keccak(bytes(preimage, 64), out);
}

bool arb_batch_poster_l1_is_current(chain_id_t chain_id, uint64_t l1_number) {
  uint8_t  address[20] = {0};
  uint64_t timestamp   = 0;
  uint64_t stored      = 0;
  if (!c4_get_cached_batch_poster(chain_id, address, &timestamp, &stored)) return true;
  return l1_number >= stored;
}
