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

#ifndef C4_CHAIN_H
#define C4_CHAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "bytes.h"
#include "crypto.h"
#include "json.h"

// Forward declaration so chains.h can expose `c4_register_chain` without
// including state.h (state.h already includes chains.h and would cycle).
// The matching full definition lives in state.h and must stay tagged.
#ifndef C4_STATE_T_FWD
#define C4_STATE_T_FWD
struct c4_state;
typedef struct c4_state c4_state_t;
#endif

/**
 * Builds a legacy `chain_id_t` from an EVM numeric chain id only (type bits zero).
 *
 * Prefer `CHAIN_ID` for new code so the high byte encodes `chain_type_t`.
 */
#define CHAIN(id) ((chain_id_t) ((uint64_t) id))

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
 * The same applies to EEZ (Ethereum Economic Zone) L2s, which map to
 * `C4_CHAIN_TYPE_EEZ`, and to Arbitrum One (`C4_CHAIN_ARB_ONE`), which maps to
 * `C4_CHAIN_TYPE_ARBITRUM`.
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
  C4_CHAIN_TYPE_EEZ       = 8, // Ethereum Economic Zone L2s; the L2 block hash is proven by an L1 storage proof
  C4_CHAIN_TYPE_CRONOS    = 9,
  C4_CHAIN_TYPE_FUSE      = 10,
  C4_CHAIN_TYPE_AVALANCHE = 11,
  C4_CHAIN_TYPE_MOONRIVER = 12,
  C4_CHAIN_TYPE_MOONBEAM  = 13,
  C4_CHAIN_TYPE_TELOS     = 14,
} chain_type_t;

/** Packed chain identity: top 8 bits = `chain_type_t`, low 56 bits = network-specific id. */
#ifndef C4_CHAIN_ID_T_DEFINED
#define C4_CHAIN_ID_T_DEFINED
typedef uint64_t chain_id_t;
#endif

/**
 * Ethereum L1 networks as integer constant expressions, so `c4_chain_type()` can use them as `case` labels.
 *
 * `C4_CHAIN_TYPE_ETHEREUM` is 0, so `CHAIN(id)` is the same value as the previous
 * `CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, id)` objects. Other networks stay `extern const` in `chains.c`.
 */
#define C4_CHAIN_MAINNET       CHAIN(1)
#define C4_CHAIN_SEPOLIA       CHAIN(11155111)
#define C4_CHAIN_GNOSIS        CHAIN(100)
#define C4_CHAIN_GNOSIS_CHIADO CHAIN(10200)
#define C4_CHAIN_PLATABERGET   CHAIN(7091047534)

#define C4_CHAIN_OP_MAINNET    CHAIN(10)
#define C4_CHAIN_OP_BASE       CHAIN(8453)
#define C4_CHAIN_OP_WORLDCHAIN CHAIN(480)
#define C4_CHAIN_OP_ZORA       CHAIN(7777777)
#define C4_CHAIN_OP_UNICHAIN   CHAIN(130)
#define C4_CHAIN_OP_INK        CHAIN(57073)
#define C4_CHAIN_OP_PGN        CHAIN(424)
#define C4_CHAIN_OP_ORDERLY    CHAIN(291)
#define C4_CHAIN_OP_MODE       CHAIN(34443)
#define C4_CHAIN_OP_FRAXTAL    CHAIN(252)
#define C4_CHAIN_OP_MANTLE     CHAIN(5000)
#define C4_CHAIN_OP_KLAYTN     CHAIN(8217)

/** Arbitrum One. `c4_chain_type()` maps this id to `C4_CHAIN_TYPE_ARBITRUM`. */
#define C4_CHAIN_ARB_ONE CHAIN(42161)

/**
 * EEZ (Ethereum Economic Zone) L2s. Each id listed here must also be handled in
 * `c4_chain_type()` and `c4_get_l1_chain_id_for_l2()` (`chains.c`); the detailed
 * per-chain config (proxy contract, rollup id) lives in the EEZ chain module.
 */
#define C4_CHAIN_EEZ_ROLLUPX CHAIN(0xaa29f) // RollupX (696991), settles on Ethereum mainnet

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
 * EEZ rollups listed as `C4_CHAIN_EEZ_*` macros map to `C4_CHAIN_TYPE_EEZ`;
 * `C4_CHAIN_ARB_ONE` maps to `C4_CHAIN_TYPE_ARBITRUM`;
 * other ids use the type byte embedded in `chain_id`.
 *
 * @param chain_id packed chain identifier
 * @return chain type enum value
 */
chain_type_t c4_chain_type(chain_id_t chain_id);

/**
 * Returns the chain id whose consensus secures `chain`.
 *
 * For an EEZ L2 (`C4_CHAIN_EEZ_*`) this is the L1 the rollup settles on: the L2
 * block hash is proven by an L1 storage proof, so sync-committee state, light
 * client updates and the client state all belong to that L1. Arbitrum One uses
 * the same mapping: the batch-poster account proof is an L1 `clProof`, so its
 * sync-committee state is Ethereum mainnet. Every other chain (L1s and
 * OP-Stack L2s) is returned unchanged.
 *
 * The mapping is a switch in `chains.c` on purpose: `util` must not depend on a
 * chain module, and callers need the answer before any module is initialised
 * (RPC routing, client state lookup, bindings).
 *
 * | `chain`                | result                   |
 * |------------------------|--------------------------|
 * | `C4_CHAIN_EEZ_ROLLUPX` | `C4_CHAIN_MAINNET` (`1`) |
 * | `C4_CHAIN_ARB_ONE`     | `C4_CHAIN_MAINNET` (`1`) |
 * | anything else          | `chain`                  |
 *
 * @param chain client chain id
 * @return L1 chain id for an EEZ L2 or Arbitrum One, otherwise `chain` itself
 */
chain_id_t c4_get_l1_chain_id_for_l2(chain_id_t chain);

/**
 * Extracts the lower 56 bits of `chain_id` (EVM chain id or chain-specific number).
 *
 * @param chain_id packed chain identifier
 * @return network-specific id (e.g. `1` for mainnet, `8453` for Base)
 */
uint64_t c4_chain_specific_id(chain_id_t chain_id);

/** Maximum number of additional chain-id to chain-type (or L1) mappings. */
#define C4_CHAIN_REGISTRY_MAX 32

/**
 * Number of slots in every `chain_type_t`-indexed hook table
 * (`verify_block_hooks`, `prover_hooks`, `sync_hooks`, …).
 *
 * Centralised so `c4_register_chain_family()` and the hook tables in
 * `beacon_header.c` / `eth_tools.c` cannot drift out of sync.
 */
#define C4_BLOCK_PROOF_HOOKS 16

/**
 * Registers an extra `chain_id -> chain_type` mapping consulted by
 * `c4_chain_type()` when the built-in switch does not match.
 *
 * Idempotent: a second call with the same `chain_id` overwrites the type.
 *
 * Built-in chain ids are **hard-rejected**: any id that `chains.h` declares
 * as a `C4_CHAIN_*` constant (`C4_CHAIN_MAINNET`, `C4_CHAIN_OP_*`,
 * `C4_CHAIN_EEZ_*`, `C4_CHAIN_ARB_ONE`, `C4_CHAIN_POLYGON`, `C4_CHAIN_BSC`,
 * every Bolt/Haifa/Telos/Moon/Fuse/Cronos/Avalanche variant, …) cannot be
 * rerouted. This preserves the core-routing invariant even when a plugin
 * tries to claim a built-in id. See `c4_reset_chain_registry` for tests.
 *
 * The runtime hook tables (see `beacon_header.c` and `eth_tools.c`) are
 * indexed by `chain_type_t` with `C4_BLOCK_PROOF_HOOKS` slots, so values
 * `>= C4_BLOCK_PROOF_HOOKS` are rejected as well.
 *
 * Intended for `EXTRA_CHAIN` plugins and for `add_chain_registration`
 * dispatcher functions; a plugin that needs to claim a brand-new id
 * (anything that is **not** already a `C4_CHAIN_*` constant) calls this
 * once at startup.
 *
 * @param chain_id packed chain identifier (must be non-zero and not a built-in id)
 * @param type chain family the module handles (must be `< C4_BLOCK_PROOF_HOOKS`)
 * @return true if stored or updated, false on invalid input, a built-in id, or a full table
 */
bool c4_register_chain_family(chain_id_t chain_id, chain_type_t type);

/**
 * Registers an extra `L2 -> L1` mapping consulted by `c4_get_l1_chain_id_for_l2`
 * when the built-in switch does not match.
 *
 * Idempotent: a second call with the same `l2` overwrites the L1.
 *
 * Built-in `l2` ids (every `C4_CHAIN_*` constant) are hard-rejected for the
 * same reason as `c4_register_chain_family`: a plugin must not reroute the
 * sync-committee lookup for Ethereum mainnet, Arbitrum One, or any other
 * chain the core already knows about. `l2 == 0`, `l1 == 0` and `l2 == l1`
 * are rejected as well to prevent self-referential or sentinel lookups.
 *
 * @param l2 L2 chain id whose proofs anchor on `l1` (must be non-zero and not a built-in id)
 * @param l1 chain id that secures `l2` via consensus (must be non-zero and `!= l2`)
 * @return true if stored or updated, false on invalid input, a built-in `l2`, or a full table
 */
bool c4_register_l1_for_l2(chain_id_t l2, chain_id_t l1);

/**
 * Clears every mapping stored by `c4_register_chain_family` and
 * `c4_register_l1_for_l2`. Built-in mappings are not affected.
 *
 * Intended for unit tests; production code never calls this.
 */
void c4_reset_chain_registry(void);

/**
 * Registers chain-type-specific runtime configuration (fork epochs, blob
 * schedules, genesis validators root, …) from JSON.
 *
 * Dispatched to the module that owns `type` through the generated
 * `c4_register_chain` in `verifiers.h`: every `add_verifier(... REGISTER_CHAIN
 * <fn> ...)` registration contributes one branch in the switch. Modules that
 * do not expose `REGISTER_CHAIN` have no runtime configuration path.
 *
 * The JSON schema is defined by each chain module. For ETH, keys match the
 * fields of `chain_spec_t`; see `src/chains/eth/ssz/chain_spec.c` for the
 * authoritative schema. `fork_epochs` is an array ordered by fork id (Altair,
 * Bellatrix, Capella, Deneb, Electra, Fulu, Gloas, …); `fork_versions` is an
 * array of 4-byte hex strings, Phase0 first, so `len(fork_versions) ==
 * len(fork_epochs) + 1`. Example (ETH / Luxor L1):
 *
 * ```json
 * {
 *   "chain_id": 42,
 *   "genesis_validators_root": "0x4b363db9...",
 *   "slots_per_epoch_bits": 5,
 *   "epochs_per_period_bits": 8,
 *   "weak_subjectivity_epochs": 3682,
 *   "fork_epochs":   [0, 0, 0, 0, 0, 0, 1536],
 *   "fork_versions": [
 *     "0x00000000", "0x01000000", "0x02000000", "0x03000000",
 *     "0x04000000", "0x05000000", "0x06000000", "0x07000000"
 *   ],
 *   "max_blobs_per_block_electra": 9
 * }
 * ```
 *
 * On failure, `state->error` carries a human-readable message and the function
 * returns `false`. On success, subsequent calls to the chain's lookup
 * functions (e.g. `c4_eth_get_chain_spec`) return the registered entry.
 *
 * @param state state object that collects the error message (must not be NULL)
 * @param type chain family that owns the configuration
 * @param data parsed JSON body describing one chain
 * @return true on success, false if the chain type has no `REGISTER_CHAIN`
 *         handler or the handler rejected the payload (see `state->error`)
 */
bool c4_register_chain(c4_state_t* state, chain_type_t type, json_t data);

#ifdef __cplusplus
}
#endif
#endif
