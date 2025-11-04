#include "chains/eth/ssz/beacon_types.h"
#include "util/chains.h"

bool c4_eth_chain_props(chain_id_t chain_id, chain_properties_t* props) {
  if (!props) return false;
  if (c4_chain_type(chain_id) != C4_CHAIN_TYPE_ETHEREUM) return false;

  // Default block time for Ethereum execution chains (12s)
  props->block_time = 12000;
  props->chain_type = C4_CHAIN_TYPE_ETHEREUM;
  props->id         = chain_id;
  props->flags      = 0;

  uint64_t cid = c4_chain_specific_id(chain_id);
  if (cid == 1ULL)
    props->chain_name = "Ethereum Mainnet";
  else if (cid == 11155111ULL)
    props->chain_name = "Sepolia";
  else if (cid == 100ULL)
    props->chain_name = "Gnosis";
  else if (cid == 10200ULL)
    props->chain_name = "Gnosis Chiado";
  else
    props->chain_name = "Ethereum";
  return true;
}
