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

#include "chains.h"
#include <stdlib.h>
#include <string.h>

// Definitionen der Chain-Konstanten
const chain_id_t C4_CHAIN_MAINNET       = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 1);
const chain_id_t C4_CHAIN_GNOSIS        = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 100);
const chain_id_t C4_CHAIN_SEPOLIA       = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 11155111);
const chain_id_t C4_CHAIN_PLATABERGET   = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 7091047534);
const chain_id_t C4_CHAIN_GNOSIS_CHIADO = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 10200);

const chain_id_t C4_CHAIN_BTC_MAINNET  = CHAIN_ID(C4_CHAIN_TYPE_BITCOIN, 0);
const chain_id_t C4_CHAIN_BTC_TESTNET  = CHAIN_ID(C4_CHAIN_TYPE_BITCOIN, 1);
const chain_id_t C4_CHAIN_BTC_DEVNET   = CHAIN_ID(C4_CHAIN_TYPE_BITCOIN, 2);
const chain_id_t C4_CHAIN_SOL_MAINNET  = CHAIN_ID(C4_CHAIN_TYPE_SOLANA, 101);
const chain_id_t C4_CHAIN_BSC          = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 56);
const chain_id_t C4_CHAIN_POLYGON      = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 137);
const chain_id_t C4_CHAIN_BASE         = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 8453);
const chain_id_t C4_CHAIN_ARBITRUM     = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 42161);
const chain_id_t C4_CHAIN_OPTIMISM     = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 10);
const chain_id_t C4_CHAIN_CRONOS       = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 25);
const chain_id_t C4_CHAIN_FUSE         = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 122);
const chain_id_t C4_CHAIN_AVALANCHE    = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 43114);
const chain_id_t C4_CHAIN_MOONRIVER    = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 1285);
const chain_id_t C4_CHAIN_MOONBEAM     = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 1284);
const chain_id_t C4_CHAIN_TELOS        = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 40);
const chain_id_t C4_CHAIN_HAIFA        = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 10200);
const chain_id_t C4_CHAIN_BOLT         = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 1021);
const chain_id_t C4_CHAIN_BOLT_TESTNET = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 1022);
const chain_id_t C4_CHAIN_BOLT_DEVNET  = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 1023);
const chain_id_t C4_CHAIN_BOLT_STAGING = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 1024);
const chain_id_t C4_CHAIN_BOLT_MAINNET = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 1025);

// Extra mappings populated by `c4_register_chain_family` / `c4_register_l1_for_l2`.
// Built-in switch cases always win; the tables are only consulted on the `default`
// branch. 32 slots are enough for every chain family that colibri is likely to
// ship, plus room for `EXTRA_CHAIN` plugins to add their own.
typedef struct {
  chain_id_t   chain_id;
  chain_type_t type;
} c4_chain_family_entry_t;

typedef struct {
  chain_id_t l2;
  chain_id_t l1;
} c4_chain_l1_entry_t;

static c4_chain_family_entry_t family_registry[C4_CHAIN_REGISTRY_MAX];
static uint32_t                family_registry_len = 0;

static c4_chain_l1_entry_t l1_registry[C4_CHAIN_REGISTRY_MAX];
static uint32_t            l1_registry_len = 0;

// Guard against plugins hijacking core routing: every chain id that has a
// dedicated `C4_CHAIN_*` symbol in `chains.h` is considered built-in and must
// not be reroutable at runtime. If a new `C4_CHAIN_*` constant is added to
// `chains.h`, extend this list and `test_chain_registry.c` together.
static bool is_builtin_chain_id(chain_id_t id) {
  return id == C4_CHAIN_MAINNET || id == C4_CHAIN_SEPOLIA || id == C4_CHAIN_GNOSIS || id == C4_CHAIN_GNOSIS_CHIADO || id == C4_CHAIN_PLATABERGET || id == C4_CHAIN_BTC_MAINNET || id == C4_CHAIN_BTC_TESTNET || id == C4_CHAIN_BTC_DEVNET || id == C4_CHAIN_SOL_MAINNET || id == C4_CHAIN_BSC || id == C4_CHAIN_POLYGON || id == C4_CHAIN_BASE || id == C4_CHAIN_ARBITRUM || id == C4_CHAIN_OPTIMISM || id == C4_CHAIN_CRONOS || id == C4_CHAIN_FUSE || id == C4_CHAIN_AVALANCHE || id == C4_CHAIN_MOONRIVER || id == C4_CHAIN_MOONBEAM || id == C4_CHAIN_TELOS || id == C4_CHAIN_HAIFA || id == C4_CHAIN_BOLT || id == C4_CHAIN_BOLT_TESTNET || id == C4_CHAIN_BOLT_DEVNET || id == C4_CHAIN_BOLT_STAGING || id == C4_CHAIN_BOLT_MAINNET
         // Macros from `chains.h` already handled by the switch above; listed
         // here so a plugin cannot override them through the registry either.
         || id == C4_CHAIN_OP_MAINNET || id == C4_CHAIN_OP_BASE || id == C4_CHAIN_OP_WORLDCHAIN || id == C4_CHAIN_OP_ZORA || id == C4_CHAIN_OP_UNICHAIN || id == C4_CHAIN_OP_INK || id == C4_CHAIN_OP_PGN || id == C4_CHAIN_OP_ORDERLY || id == C4_CHAIN_OP_MODE || id == C4_CHAIN_OP_FRAXTAL || id == C4_CHAIN_OP_MANTLE || id == C4_CHAIN_OP_KLAYTN || id == C4_CHAIN_ARB_ONE || id == C4_CHAIN_EEZ_ROLLUPX;
}
// The table lists every `CHAIN_DOC` entry in `c4_chain_type` whose numeric id
// is also registered in `scripts/chain_defaults/chains.json`. That file is what
// the bindings ship, so a chain appears here only once clients can select it
// without extra configuration. The prover column is taken from that file.
//

// `name` and `comment` are documentation only. The macro expands to the case
// label; `scripts/doc/parse_defs.js` reads the two strings for the GitBook table.

// : Supported chains
//
// Colibri currently supports those EVM L1 or L2-Chains:
//
#define CHAIN_DOC(id, name, comment) case id:

chain_type_t c4_chain_type(chain_id_t chain_id) {
  // OP Stack chains - use switch for better performance.
  // A new case is a newly supported chain. Also add it to the published list
  // with CHAIN_DOC, and to scripts/chain_defaults/chains.json, which the
  // overview requires. Arbitrum One is mapped here; its published section
  // lands with the feed proof.
  switch (chain_id) {
    CHAIN_DOC(C4_CHAIN_OP_MAINNET, "OP Mainnet", "Proof based on preconfs and sequence signatures")    // 10
    CHAIN_DOC(C4_CHAIN_OP_BASE, "Base", "Proof based on preconfs and sequence signatures")             // 8453
    CHAIN_DOC(C4_CHAIN_OP_WORLDCHAIN, "Worldchain", "Proof based on preconfs and sequence signatures") // 480
    CHAIN_DOC(C4_CHAIN_OP_ZORA, "Zora", "Proof based on preconfs and sequence signatures")             // 7777777
    CHAIN_DOC(C4_CHAIN_OP_UNICHAIN, "Unichain", "Proof based on preconfs and sequence signatures")     // 130
    CHAIN_DOC(C4_CHAIN_OP_INK, "Ink", "Proof based on preconfs and sequence signatures")               // 57073
    case C4_CHAIN_OP_PGN:                                                                              // PGN (424)
    case C4_CHAIN_OP_ORDERLY:                                                                          // Orderly Network (291)
    case C4_CHAIN_OP_MODE:                                                                             // Mode Network (34443)
    case C4_CHAIN_OP_FRAXTAL:                                                                          // Fraxtal (252)
    case C4_CHAIN_OP_MANTLE:                                                                           // Mantle (5000)
    case C4_CHAIN_OP_KLAYTN:                                                                           // Klaytn (8217)
      return C4_CHAIN_TYPE_OP;

      // EEZ (Ethereum Economic Zone) chains
      CHAIN_DOC(C4_CHAIN_EEZ_ROLLUPX, "RollupX", "L2 block hash proven by an L1 storage proof of the EEZ proxy") // 696991
      return C4_CHAIN_TYPE_EEZ;

      CHAIN_DOC(C4_CHAIN_ARB_ONE, "Arbitrum One", "Nitro feed signature checked against the L1 batch poster") // 42161
      return C4_CHAIN_TYPE_ARBITRUM;

      CHAIN_DOC(C4_CHAIN_MAINNET, "Ethereum Mainnet", "Sync-committee signature over the beacon block") // 1
      CHAIN_DOC(C4_CHAIN_SEPOLIA, "Sepolia", "Sync-committee signature over the beacon block")          // 11155111
      CHAIN_DOC(C4_CHAIN_GNOSIS, "Gnosis", "Sync-committee signature over the beacon block")            // 100
      CHAIN_DOC(C4_CHAIN_GNOSIS_CHIADO, "Chiado", "Sync-committee signature over the beacon block")     // 10200
      CHAIN_DOC(C4_CHAIN_PLATABERGET, "Platåberget", "Sync-committee signature over the beacon block")  // 7091047534
      return C4_CHAIN_TYPE_ETHEREUM;

    default:
      // Extra mapping from an `EXTRA_CHAIN` plugin or `add_chain_registration`.
      for (uint32_t i = 0; i < family_registry_len; i++)
        if (family_registry[i].chain_id == chain_id) return family_registry[i].type;
      return (chain_id >> 56) & 0xff;
  }
}

uint64_t c4_chain_specific_id(chain_id_t chain_id) {
  return chain_id & 0xffffffffffffff;
}

chain_id_t c4_get_l1_chain_id_for_l2(chain_id_t chain) {
  // EEZ L2s and Arbitrum One are remapped: their L1 proofs use the sync-committee
  // state of that L1. This switch is the only place that names that L1. Every
  // chain in the EEZ config table needs a case here.
  switch (chain) {
    case C4_CHAIN_EEZ_ROLLUPX: // RollupX settles on Ethereum mainnet
    case C4_CHAIN_ARB_ONE:     // Arbitrum One batch-poster proof is an L1 account proof
      return C4_CHAIN_MAINNET;

    default:
      for (uint32_t i = 0; i < l1_registry_len; i++)
        if (l1_registry[i].l2 == chain) return l1_registry[i].l1;
      return chain;
  }
}

bool c4_register_chain_family(chain_id_t chain_id, chain_type_t type) {
  // Core-routing invariant: a plugin must not reroute any chain id that
  // `chains.h` already assigns a `C4_CHAIN_*` symbol to (see H-1 in the
  // security review). Hook tables in `beacon_header.c` and `eth_tools.c`
  // only index [0, C4_BLOCK_PROOF_HOOKS).
  if (!chain_id || is_builtin_chain_id(chain_id) || (unsigned) type >= C4_BLOCK_PROOF_HOOKS) return false;

  // Idempotent: update an existing entry instead of growing the table.
  for (uint32_t i = 0; i < family_registry_len; i++) {
    if (family_registry[i].chain_id == chain_id) {
      family_registry[i].type = type;
      return true;
    }
  }
  if (family_registry_len >= C4_CHAIN_REGISTRY_MAX) return false;
  family_registry[family_registry_len].chain_id = chain_id;
  family_registry[family_registry_len].type     = type;
  family_registry_len++;
  return true;
}

bool c4_register_l1_for_l2(chain_id_t l2, chain_id_t l1) {
  // Same core-routing invariant as `c4_register_chain_family`: no plugin may
  // reroute the sync-committee lookup for any built-in `l2`. `l1 == 0` is
  // rejected because the lookup result is used as a sentinel for "unknown
  // L1" by callers (would silently point sync-committee state at nothing).
  if (!l2 || !l1 || l1 == l2 || is_builtin_chain_id(l2)) return false;

  for (uint32_t i = 0; i < l1_registry_len; i++) {
    if (l1_registry[i].l2 == l2) {
      l1_registry[i].l1 = l1;
      return true;
    }
  }
  if (l1_registry_len >= C4_CHAIN_REGISTRY_MAX) return false;
  l1_registry[l1_registry_len].l2 = l2;
  l1_registry[l1_registry_len].l1 = l1;
  l1_registry_len++;
  return true;
}

void c4_reset_chain_registry(void) {
  family_registry_len = 0;
  l1_registry_len     = 0;
}
