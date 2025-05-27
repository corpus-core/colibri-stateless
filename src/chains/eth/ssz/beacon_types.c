#include "beacon_types.h"
#include "ssz.h"

typedef struct {
  chain_id_t      chain_id;
  const uint64_t* fork_epochs;
  const bytes32_t genesis_validators_root;
} chain_data_t;

static const uint64_t eth_mainnet_fork_epochs[] = {74240ULL, 144896ULL, 194048ULL, 269568ULL, 364032ULL, 0ULL};
static const uint64_t eth_gnosis_fork_epochs[]  = {512ULL, 385536ULL, 648704ULL, 889856ULL, 1337856ULL, 0ULL};

static const chain_data_t chain_data[] = {
    {.chain_id                = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 1ULL),
     .fork_epochs             = eth_mainnet_fork_epochs,
     .genesis_validators_root = "\x4b\x36\x3d\xb9\x4e\x28\x61\x20\xd7\x6e\xb9\x05\x34\x0f\xdd\x4e\x54\xbf\xe9\xf0\x06\xbf\x33\xff\xf6\xcf\x5a\xd2\x7f\x51\x1b\xfe\x95"},
    {.chain_id                = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 100ULL),
     .fork_epochs             = eth_gnosis_fork_epochs,
     .genesis_validators_root = "\xf5\xdc\xb5\x56\x4e\x82\x9a\xab\x27\x26\x4b\x9b\xec\xd5\xdf\xaa\x01\x70\x85\x61\x12\x24\xcb\x30\x36\xf5\x73\x36\x8d\xbb\x9d\x47"},
};

static inline const chain_data_t* get_chain_data(chain_id_t id) {
  for (int i = 0; i < sizeof(chain_data) / sizeof(chain_data[0]); i++) {
    if (chain_data[i].chain_id == id)
      return chain_data + i;
  }
  return NULL;
}
// const uint64_t eth_mainnet_fork_epochs[] = {74240, 144896, 194048, 269568, 364032, 0};
// const uint64_t eth_gnosis_fork_epochs[]  = {512, 385536, 648704, 889856, 1337856, 0};

const ssz_def_t* eth_ssz_type_for_fork(eth_ssz_type_t type, fork_id_t fork) {
  switch (fork) {
    case C4_FORK_DENEB: return eth_ssz_type_for_denep(type);
    case C4_FORK_ELECTRA: return eth_ssz_type_for_electra(type);
    default: return NULL;
  }
}

bool c4_chain_genesis_validators_root(chain_id_t chain_id, bytes32_t genesis_validators_root) {
  const chain_data_t* data = get_chain_data(chain_id);
  if (data) {
    memcpy(genesis_validators_root, data->genesis_validators_root, 32);
    return true;
  }
  return false;
}

fork_id_t c4_chain_fork_id(chain_id_t chain_id, uint64_t epoch) {
  const chain_data_t* data = get_chain_data(chain_id);
  if (!data) return C4_FORK_ALTAIR;

  int i = 0;
  while (data->fork_epochs[i] && epoch >= data->fork_epochs[i]) i++;
  return (fork_id_t) i;
}

void c4_chain_fork_version(chain_id_t chain_id, fork_id_t fork, uint8_t* version) {
  uint64_t id = c4_chain_specific_id(chain_id);
  if (id == 1) id = 0;
  version[0] = (uint8_t) fork;
  version[1] = (uint8_t) ((id >> 16) & 0xff);
  version[2] = (uint8_t) ((id >> 8) & 0xff);
  version[3] = (uint8_t) (id & 0xff);
}
