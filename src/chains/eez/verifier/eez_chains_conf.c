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

#include "eez_chains_conf.h"
#include "bytes.h"
#include "json.h"
#include "state.h"
#include <stddef.h>
#include <string.h>

// All known EEZ rollups. A new chain needs an entry here and a `C4_CHAIN_EEZ_*`
// id in `util/chains.h` that is handled in `c4_chain_type()`. The L1 this
// rollup settles on is not stored in the entry: every chain added to this
// table also needs its own case in `c4_get_l1_chain_id_for_l2()`.
static const eez_chain_config_t eez_chain_configs[] = {
    {
        .chain_id  = C4_CHAIN_EEZ_ROLLUPX,
        .rollup_id = 2,
        // 0x4b2760f84b83d6b3829e30a7573b0172b6d6402c
        .proxy = {0x4b, 0x27, 0x60, 0xf8, 0x4b, 0x83, 0xd6, 0xb3, 0x82, 0x9e, 0x30, 0xa7, 0x57, 0x3b, 0x01, 0x72, 0xb6, 0xd6, 0x40, 0x2c},
        .name  = "RollupX",
    },
};

// Additional EEZ rollups registered at runtime through `c4_eez_register_chain`.
// Built-ins in `eez_chain_configs[]` always take precedence: the lookup below
// scans them first, so a plugin cannot override a known rollup.
#define EEZ_CHAIN_CONFIG_EXTRA_MAX 8

static eez_chain_config_t eez_chain_configs_extra[EEZ_CHAIN_CONFIG_EXTRA_MAX];
static uint32_t           eez_chain_configs_extra_len = 0;

const eez_chain_config_t* eez_get_chain_config(chain_id_t chain_id) {
  for (size_t i = 0; i < sizeof(eez_chain_configs) / sizeof(eez_chain_configs[0]); i++) {
    if (eez_chain_configs[i].chain_id == chain_id) return &eez_chain_configs[i];
  }
  for (uint32_t i = 0; i < eez_chain_configs_extra_len; i++) {
    if (eez_chain_configs_extra[i].chain_id == chain_id) return &eez_chain_configs_extra[i];
  }
  return NULL;
}

const eez_chain_config_t* eez_get_all_chain_configs(size_t* count) {
  if (count) *count = sizeof(eez_chain_configs) / sizeof(eez_chain_configs[0]);
  return eez_chain_configs;
}

bool c4_eez_register_chain(c4_state_t* state, json_t data) {
  if (!state) return false;
  // `chain_id` accepts both JSON numbers and hex-strings so wallet-style
  // configs (`"0xfeedbeef"`) and plain JSON numbers (`42`) both work.
  const char* schema_err = json_validate(
      data,
      "{chain_id:uint|hexuint,rollup_id:uint|hexuint,proxy:address,name?:string}",
      "c4_eez_register_chain");
  if (schema_err) {
    c4_state_add_error(state, schema_err);
    safe_free((void*) schema_err);
    return false;
  }

  chain_id_t chain_id = (chain_id_t) json_get_uint64(data, "chain_id");
  if (chain_id == 0) {
    c4_state_add_error(state, "c4_eez_register_chain: chain_id must not be 0");
    return false;
  }
  if (eez_get_chain_config(chain_id)) {
    c4_state_add_error(state, "c4_eez_register_chain: chain_id is already registered");
    return false;
  }
  if (eez_chain_configs_extra_len >= EEZ_CHAIN_CONFIG_EXTRA_MAX) {
    c4_state_add_error(state, "c4_eez_register_chain: extra chain config table is full");
    return false;
  }

  eez_chain_config_t* slot = &eez_chain_configs_extra[eez_chain_configs_extra_len];
  memset(slot, 0, sizeof(*slot));
  slot->chain_id  = chain_id;
  slot->rollup_id = json_get_uint64(data, "rollup_id");

  // Parse the 20-byte proxy address. `address_t` is `uint8_t[20]`.
  buffer_t proxy_buf = {0};
  bytes_t  parsed    = json_as_bytes(json_get(data, "proxy"), &proxy_buf);
  if (!parsed.data || parsed.len != 20) {
    buffer_free(&proxy_buf);
    c4_state_add_error(state, "c4_eez_register_chain: proxy must be 20 bytes");
    return false;
  }
  memcpy(slot->proxy, parsed.data, 20);
  buffer_free(&proxy_buf);

  // Optional human-readable name. We duplicate the JSON string onto the heap
  // because the parsed JSON buffer is caller-owned.
  json_t name_val = json_get(data, "name");
  slot->name      = name_val.type == JSON_TYPE_NOT_FOUND ? NULL : json_new_string(name_val);

  eez_chain_configs_extra_len++;
  return true;
}

void c4_eez_reset_chain_config_registry(void) {
  for (uint32_t i = 0; i < eez_chain_configs_extra_len; i++) {
    safe_free((void*) eez_chain_configs_extra[i].name);
    memset(&eez_chain_configs_extra[i], 0, sizeof(eez_chain_configs_extra[i]));
  }
  eez_chain_configs_extra_len = 0;
}

C4_STATIC_UNLESS_TEST void eez_bytes32_add(bytes32_t value, uint8_t add) {
  uint16_t carry = add;
  for (int i = 31; i >= 0 && carry; i--) {
    carry += value[i];
    value[i] = (uint8_t) carry;
    carry >>= 8;
  }
}

void eez_root_slot(uint64_t rollup_id, bytes32_t slot) {
  // Solidity mapping slot: keccak256(abi.encode(key, mapping_slot)), both left-padded to 32 bytes.
  uint8_t preimage[64] = {0};
  uint64_to_be(preimage + 24, rollup_id);
  preimage[63] = EEZ_ROLLUPS_MAPPING_SLOT;
  keccak(bytes(preimage, sizeof(preimage)), slot);
  eez_bytes32_add(slot, EEZ_ROLLUP_ROOT_OFFSET);
}
