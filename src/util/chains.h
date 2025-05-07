#ifndef C4_CHAIN_H
#define C4_CHAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "bytes.h"
#include "crypto.h"
#include <stdbool.h>

typedef enum {
  C4_CHAIN_MAINNET      = 1,
  C4_CHAIN_SEPOLIA      = 11155111,
  C4_CHAIN_GNOSIS       = 100,
  C4_CHAIN_BSC          = 56,
  C4_CHAIN_POLYGON      = 137,
  C4_CHAIN_BASE         = 8453,
  C4_CHAIN_ARBITRUM     = 42161,
  C4_CHAIN_OPTIMISM     = 10,
  C4_CHAIN_CRONOS       = 25,
  C4_CHAIN_FUSE         = 122,
  C4_CHAIN_AVALANCHE    = 43114,
  C4_CHAIN_MOONRIVER    = 1285,
  C4_CHAIN_MOONBEAM     = 1284,
  C4_CHAIN_TELOS        = 40,
  C4_CHAIN_HAIFA        = 10200,
  C4_CHAIN_BOLT         = 1021,
  C4_CHAIN_BOLT_TESTNET = 1022,
  C4_CHAIN_BOLT_DEVNET  = 1023,
  C4_CHAIN_BOLT_STAGING = 1024,
  C4_CHAIN_BOLT_MAINNET = 1025,
} chain_id_t;

typedef enum {
  C4_CHAIN_TYPE_ETHEREUM  = 1,
  C4_CHAIN_TYPE_SOLANA    = 2,
  C4_CHAIN_TYPE_BITCOIN   = 3,
  C4_CHAIN_TYPE_POLKADOT  = 4,
  C4_CHAIN_TYPE_KUSAMA    = 5,
  C4_CHAIN_TYPE_POLYGON   = 6,
  C4_CHAIN_TYPE_BASE      = 7,
  C4_CHAIN_TYPE_ARBITRUM  = 8,
  C4_CHAIN_TYPE_OPTIMISM  = 9,
  C4_CHAIN_TYPE_CRONOS    = 10,
  C4_CHAIN_TYPE_FUSE      = 11,
  C4_CHAIN_TYPE_AVALANCHE = 12,
  C4_CHAIN_TYPE_MOONRIVER = 13,
  C4_CHAIN_TYPE_MOONBEAM  = 14,
  C4_CHAIN_TYPE_TELOS     = 15,
} chain_type_t;

chain_type_t c4_chain_type(chain_id_t chain_id);

#ifdef __cplusplus
}
#endif
#endif
