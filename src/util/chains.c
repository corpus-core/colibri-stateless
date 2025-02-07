#include "chains.h"
#include <stdlib.h>
#include <string.h>

const uint64_t eth_mainnet_fork_epochs[] = {74240, 144896, 194048, 269568, 0};

bool c4_chain_genesis_validators_root(chain_id_t chain_id, bytes32_t genesis_validators_root) {
  void* hash;
  switch (chain_id) {
    case C4_CHAIN_MAINNET:
      hash = "\x4b\x36\x3d\xb9\x4e\x28\x61\x20\xd7\x6e\xb9\x05\x34\x0f\xdd\x4e\x54\xbf\xe9\xf0\x6b\xf3\x3f\xf6\xcf\x5a\xd2\x7f\x51\x1b\xfe\x95";
      break;

    default: return false;
  }

  memcpy(genesis_validators_root, hash, 32);
  return true;
}

fork_id_t c4_chain_fork_id(chain_id_t chain_id, uint64_t epoch) {
  const uint64_t* fork_epochs = NULL;
  int             i           = 0;

  switch (chain_id) {
    case C4_CHAIN_MAINNET: {
      fork_epochs = eth_mainnet_fork_epochs;
      break;
    }
    default: return C4_FORK_ALTAIR;
  }

  while (fork_epochs && fork_epochs[i] && epoch >= fork_epochs[i]) i++;
  return (fork_id_t) i;
}
