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

#ifndef C4_CHAIN_H
#define C4_CHAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "bytes.h"
#include "crypto.h"

/**
 * Builds a legacy `chain_id_t` from an EVM numeric chain id only (type bits zero).
 *
 * Prefer `CHAIN_ID` for new code so the high byte encodes `chain_type_t`.
 */
#define CHAIN(id)                ((chain_id_t) ((uint64_t) id))

/**
 * Encodes a chain type in the top byte and the network-specific id in the lower 56 bits.
 *
 * Example: Ethereum mainnet is `CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 1)`.
 */
#define CHAIN_ID(chain_type, id) ((chain_id_t) (((uint64_t) chain_type) << 56 | id))

/**
 * High-level chain family used for module dispatch (verifier/prover plugins).
 *
 * Values 0–14 are reserved; OP-Stack L2s may also map to `C4_CHAIN_TYPE_OP` via
 * `c4_chain_type()` even when the numeric id uses `C4_CHAIN_TYPE_ETHEREUM` encoding.
 */
typedef enum {
  C4_CHAIN_TYPE_ETHEREUM  = 0,
  C4_CHAIN_TYPE_SOLANA    = 1,
  C4_CHAIN_TYPE_BITCOIN   = 2,
  C4_CHAIN_TYPE_POLKADOT  = 3,
  C4_CHAIN_TYPE_KUSAMA    = 4,
  C4_CHAIN_TYPE_POLYGON   = 5,
  C4_CHAIN_TYPE_OP        = 6,
  C4_CHAIN_TYPE_ARBITRUM  = 7,
  C4_CHAIN_TYPE_CRONOS    = 9,
  C4_CHAIN_TYPE_FUSE      = 10,
  C4_CHAIN_TYPE_AVALANCHE = 11,
  C4_CHAIN_TYPE_MOONRIVER = 12,
  C4_CHAIN_TYPE_MOONBEAM  = 13,
  C4_CHAIN_TYPE_TELOS     = 14,
} chain_type_t;

/** Packed chain identity: top 8 bits = `chain_type_t`, low 56 bits = network-specific id. */
typedef uint64_t chain_id_t;

/** Well-known Ethereum L1 testnets and networks (see `chains.c` for full list). */
extern const chain_id_t C4_CHAIN_MAINNET;
extern const chain_id_t C4_CHAIN_SEPOLIA;
extern const chain_id_t C4_CHAIN_GNOSIS_CHIADO;
extern const chain_id_t C4_CHAIN_GNOSIS;

#define C4_CHAIN_OP_MAINNET    CHAIN(10)
#define C4_CHAIN_OP_BASE       CHAIN(8453)
#define C4_CHAIN_OP_WORLDCHAIN CHAIN(480)
#define C4_CHAIN_OP_ZORA       CHAIN(7777777)
#define C4_CHAIN_OP_UNICHAIN   CHAIN(130)
#define C4_CHAIN_OP_PGN        CHAIN(424)
#define C4_CHAIN_OP_ORDERLY    CHAIN(291)
#define C4_CHAIN_OP_MODE       CHAIN(34443)
#define C4_CHAIN_OP_FRAXTAL    CHAIN(252)
#define C4_CHAIN_OP_MANTLE     CHAIN(5000)
#define C4_CHAIN_OP_KLAYTN     CHAIN(8217)

extern const chain_id_t C4_CHAIN_BTC_MAINNET;
extern const chain_id_t C4_CHAIN_BTC_TESTNET;
extern const chain_id_t C4_CHAIN_BTC_DEVNET;
extern const chain_id_t C4_CHAIN_SOL_MAINNET;
extern const chain_id_t C4_CHAIN_BSC;
extern const chain_id_t C4_CHAIN_POLYGON;
extern const chain_id_t C4_CHAIN_BASE;
extern const chain_id_t C4_CHAIN_ARBITRUM;
extern const chain_id_t C4_CHAIN_OPTIMISM;
extern const chain_id_t C4_CHAIN_CRONOS;
extern const chain_id_t C4_CHAIN_FUSE;
extern const chain_id_t C4_CHAIN_AVALANCHE;
extern const chain_id_t C4_CHAIN_MOONRIVER;
extern const chain_id_t C4_CHAIN_MOONBEAM;
extern const chain_id_t C4_CHAIN_TELOS;
extern const chain_id_t C4_CHAIN_HAIFA;
extern const chain_id_t C4_CHAIN_BOLT;
extern const chain_id_t C4_CHAIN_BOLT_TESTNET;
extern const chain_id_t C4_CHAIN_BOLT_DEVNET;
extern const chain_id_t C4_CHAIN_BOLT_STAGING;
extern const chain_id_t C4_CHAIN_BOLT_MAINNET;
extern const chain_id_t C4_CHAIN_PLATABERGET;

/**
 * Generic chain properties (extensible).
 *
 * Populated by `c4_chains_get_props` from the generated chain-properties table
 * (`chain_props.h` / CMake `CHAIN_PROPS_PATH`).
 */
typedef struct {
  uint64_t     block_time; // in ms
  char*        chain_name;
  chain_type_t chain_type;
  chain_id_t   id;
  uint32_t     flags; // reserved
} chain_properties_t;

/**
 * Returns true if the chain_id is known and the properties have been set.
 *
 * @param chain_id chain to resolve
 * @param props output structure filled on success (must not be NULL)
 * @return true if `chain_id` is known and `props` was filled, false otherwise
 */
static inline bool c4_chains_get_props(chain_id_t chain_id, chain_properties_t* props);

/**
 * Returns the chain family for dispatch (Ethereum, OP-Stack, Bitcoin, …).
 *
 * OP-Stack rollups listed as `C4_CHAIN_OP_*` macros map to `C4_CHAIN_TYPE_OP`;
 * other ids use the type byte embedded in `chain_id`.
 *
 * @param chain_id packed chain identifier
 * @return chain type enum value
 */
chain_type_t c4_chain_type(chain_id_t chain_id);

/**
 * Extracts the lower 56 bits of `chain_id` (EVM chain id or chain-specific number).
 *
 * @param chain_id packed chain identifier
 * @return network-specific id (e.g. `1` for mainnet, `8453` for Base)
 */
uint64_t c4_chain_specific_id(chain_id_t chain_id);
#ifdef __cplusplus
}
#endif
#endif
