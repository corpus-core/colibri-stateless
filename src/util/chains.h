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

bool c4_chain_genesis_validators_root(chain_id_t chain_id, bytes32_t genesis_validators_root);
int  c4_chain_fork_id(chain_id_t chain_id, uint64_t epoch);

#ifdef __cplusplus
}
#endif
#endif
