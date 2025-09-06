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

#include "chains.h"
#include <stdlib.h>
#include <string.h>

// Definitionen der Chain-Konstanten
const chain_id_t C4_CHAIN_MAINNET       = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 1);
const chain_id_t C4_CHAIN_GNOSIS        = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 100);
const chain_id_t C4_CHAIN_SEPOLIA       = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 11155111);
const chain_id_t C4_CHAIN_GNOSIS_CHIADO = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 10200);

const chain_id_t C4_CHAIN_OP_MAINNET    = 10;
const chain_id_t C4_CHAIN_OP_BASE       = 8453;
const chain_id_t C4_CHAIN_OP_WORLDCHAIN = 480;
const chain_id_t C4_CHAIN_OP_ZORA       = 7777777;
const chain_id_t C4_CHAIN_OP_UNICHAIN   = 130;
const chain_id_t C4_CHAIN_OP_PGN        = 424;
const chain_id_t C4_CHAIN_OP_ORDERLY    = 291;
const chain_id_t C4_CHAIN_OP_MODE       = 34443;
const chain_id_t C4_CHAIN_OP_FRAXTAL    = 252;
const chain_id_t C4_CHAIN_OP_MANTLE     = 5000;
const chain_id_t C4_CHAIN_OP_KLAYTN     = 8217;

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

chain_type_t c4_chain_type(chain_id_t chain_id) {
  // OP Stack chains - use switch for better performance
  switch (chain_id) {
    case C4_CHAIN_OP_MAINNET:    // OP Mainnet (10)
    case C4_CHAIN_OP_BASE:       // Base (8453)
    case C4_CHAIN_OP_WORLDCHAIN: // Worldchain (480)
    case C4_CHAIN_OP_ZORA:       // Zora (7777777)
    case C4_CHAIN_OP_UNICHAIN:   // Unichain (130)
    case C4_CHAIN_OP_PGN:        // PGN (424)
    case C4_CHAIN_OP_ORDERLY:    // Orderly Network (291)
    case C4_CHAIN_OP_MODE:       // Mode Network (34443)
    case C4_CHAIN_OP_FRAXTAL:    // Fraxtal (252)
    case C4_CHAIN_OP_MANTLE:     // Mantle (5000)
    case C4_CHAIN_OP_KLAYTN:     // Klaytn (8217)
      return C4_CHAIN_TYPE_OP;

    default:
      return (chain_id >> 56) & 0xff;
  }
}

uint64_t c4_chain_specific_id(chain_id_t chain_id) {
  return chain_id & 0xffffffffffffff;
}
