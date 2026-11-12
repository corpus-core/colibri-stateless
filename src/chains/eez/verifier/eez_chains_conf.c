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

const eez_chain_config_t* eez_get_chain_config(chain_id_t chain_id) {
  for (size_t i = 0; i < sizeof(eez_chain_configs) / sizeof(eez_chain_configs[0]); i++) {
    if (eez_chain_configs[i].chain_id == chain_id) return &eez_chain_configs[i];
  }
  return NULL;
}

const eez_chain_config_t* eez_get_all_chain_configs(size_t* count) {
  if (count) *count = sizeof(eez_chain_configs) / sizeof(eez_chain_configs[0]);
  return eez_chain_configs;
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
