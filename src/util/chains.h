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
#include "chains.h"
#include "crypto.h"

#define CHAIN(id)                ((chain_id_t) ((uint64_t) id))
#define CHAIN_ID(chain_type, id) ((chain_id_t) (((uint64_t) chain_type) << 56 | id))

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

typedef uint64_t chain_id_t;

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

chain_type_t c4_chain_type(chain_id_t chain_id);
uint64_t     c4_chain_specific_id(chain_id_t chain_id);
#ifdef __cplusplus
}
#endif
#endif
