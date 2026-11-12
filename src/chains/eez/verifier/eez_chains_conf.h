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

#ifndef eez_chains_conf_h__
#define eez_chains_conf_h__

#include "chains.h"
#include "crypto.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Storage slot of the `rollups` mapping in the EEZ proxy contract.
 * `rollups[rollupId]` starts at `keccak256(pad32(rollupId) || pad32(EEZ_ROLLUPS_MAPPING_SLOT))`.
 */
#define EEZ_ROLLUPS_MAPPING_SLOT 2

/**
 * Offset of the `root` field inside one `rollups` entry.
 * Layout of an entry: `+0 rollupContract`, `+1 root` (the L2 block hash), `+2 etherBalance`.
 */
#define EEZ_ROLLUP_ROOT_OFFSET 1

/**
 * Static description of one EEZ (Ethereum Economic Zone) rollup.
 *
 * Everything the verifier needs to locate the L2 block hash on L1 is fixed per
 * chain and compiled in, so a prover cannot point the verifier at another
 * contract or another storage slot. The L1 the rollup settles on is
 * `c4_get_l1_chain_id_for_l2(chain_id)`, which is the only place that names it.
 */
typedef struct {
  chain_id_t  chain_id;  // L2 chain id (key of the table)
  uint64_t    rollup_id; // key of this rollup in the `rollups` mapping of the proxy
  address_t   proxy;     // EEZ proxy contract on L1 whose storage holds the L2 block hash
  const char* name;      // human-readable name, used in error messages only
} eez_chain_config_t;

/**
 * Looks up the configuration of an EEZ rollup.
 *
 * @param chain_id L2 chain id
 * @return the config, or `NULL` if `chain_id` is not a known EEZ rollup
 */
const eez_chain_config_t* eez_get_chain_config(chain_id_t chain_id);

/**
 * Returns all known EEZ rollups.
 *
 * @param count receives the number of entries (may be `NULL`)
 * @return pointer to the first entry of the static table
 */
const eez_chain_config_t* eez_get_all_chain_configs(size_t* count);

/**
 * Computes the L1 storage slot that holds the hash of the last L2 block posted to L1:
 *
 * ```
 * slot = keccak256(pad32(rollup_id) || pad32(EEZ_ROLLUPS_MAPPING_SLOT)) + EEZ_ROLLUP_ROOT_OFFSET
 * ```
 *
 * The addition is a 256-bit big-endian addition, so a carry runs through all 32 bytes.
 *
 * @param rollup_id key of the rollup in the `rollups` mapping
 * @param slot receives the 32-byte big-endian storage key
 */
void eez_root_slot(uint64_t rollup_id, bytes32_t slot);

/**
 * Adds a small value to a 256-bit big-endian number in place (wraps on overflow).
 *
 * Exposed for `eez_root_slot` and its unit test, which checks the carry.
 * Built with external linkage when `TEST` is defined and as a `static` function
 * otherwise (`C4_STATIC_UNLESS_TEST`).
 *
 * @param value 32-byte big-endian number, modified in place
 * @param add value to add
 */
C4_STATIC_UNLESS_TEST void eez_bytes32_add(bytes32_t value, uint8_t add);

#ifdef __cplusplus
}
#endif

#endif // eez_chains_conf_h__
