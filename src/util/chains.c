#include "chains.h"
#include <stdlib.h>
#include <string.h>

// Definitionen der Chain-Konstanten
const chain_id_t C4_CHAIN_MAINNET = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 1);
const chain_id_t C4_CHAIN_GNOSIS  = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 100);

const chain_id_t C4_CHAIN_SEPOLIA      = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 11155111);
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
  return (chain_id >> 56) & 0xff;
}

uint64_t c4_chain_specific_id(chain_id_t chain_id) {
  return chain_id & 0xffffffffffffff;
}
