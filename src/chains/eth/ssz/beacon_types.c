#include "beacon_types.h"
#include "ssz.h"

// the fork epochs for the different chains. index 0 is the the first fork or the epcoh of the ALTAIR fork. Must be NULL-Terminated
static const uint64_t eth_mainnet_fork_epochs[] = {74240ULL, 144896ULL, 194048ULL, 269568ULL, 364032ULL, 0ULL};
static const uint64_t eth_gnosis_fork_epochs[]  = {512ULL, 385536ULL, 648704ULL, 889856ULL, 1337856ULL, 0ULL};
static const uint64_t eth_sepolia_fork_epochs[] = {50L, 100L, 56832L, 132608L, 222464L, 0ULL};
static const uint64_t eth_chiado_fork_epochs[]  = {90L, 180L, 244224L, 516608L, 948224L, 0ULL};

static void mainnet_fork_version(chain_id_t chain_id, fork_id_t fork, uint8_t* version) {
  version[0] = (uint8_t) fork;
  version[1] = 0x00;
  version[2] = 0x00;
  version[3] = 0x00;
}

static void gnosis_fork_version(chain_id_t chain_id, fork_id_t fork, uint8_t* version) {
  uint64_t id = c4_chain_specific_id(chain_id);
  if (id == 10200) id = 0x6f;
  version[0] = (uint8_t) fork;
  version[1] = (uint8_t) ((id >> 16) & 0xff);
  version[2] = (uint8_t) ((id >> 8) & 0xff);
  version[3] = (uint8_t) (id & 0xff);
}

static void sepolia_fork_version(chain_id_t chain_id, fork_id_t fork, uint8_t* version) {
  uint64_t id = 0x6f + (uint64_t) fork;
  version[0]  = (uint8_t) 0x90;
  version[1]  = (uint8_t) ((id >> 16) & 0xff);
  version[2]  = (uint8_t) ((id >> 8) & 0xff);
  version[3]  = (uint8_t) (id & 0xff);
}

static const chain_spec_t chain_data[] = {
    {// Mainnet
     .chain_id                = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 1ULL),
     .fork_epochs             = eth_mainnet_fork_epochs,
     .genesis_validators_root = "\x4b\x36\x3d\xb9\x4e\x28\x61\x20\xd7\x6e\xb9\x05\x34\x0f\xdd\x4e\x54\xbf\xe9\xf0\x6b\xf3\x3f\xf6\xcf\x5a\xd2\x7f\x51\x1b\xfe\x95",
     .slots_per_epoch_bits    = 5,
     .epochs_per_period_bits  = 8,
     .fork_version_func       = mainnet_fork_version},
    {// Sepolia
     .chain_id                = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 11155111),
     .fork_epochs             = eth_sepolia_fork_epochs,
     .genesis_validators_root = "\xd8\xea\x17\x1f\x3c\x94\xae\xa2\x1e\xbc\x42\xa1\xed\x61\x05\x2a\xcf\x3f\x92\x09\xc0\x0e\x4e\xfb\xaa\xdd\xac\x09\xed\x9b\x80\x78",
     .slots_per_epoch_bits    = 5,
     .epochs_per_period_bits  = 8,
     .fork_version_func       = sepolia_fork_version},
    {// Gnosis
     .chain_id                = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 100ULL),
     .fork_epochs             = eth_gnosis_fork_epochs,
     .genesis_validators_root = "\xf5\xdc\xb5\x56\x4e\x82\x9a\xab\x27\x26\x4b\x9b\xec\xd5\xdf\xaa\x01\x70\x85\x61\x12\x24\xcb\x30\x36\xf5\x73\x36\x8d\xbb\x9d\x47",
     .slots_per_epoch_bits    = 4,
     .epochs_per_period_bits  = 9,
     .fork_version_func       = gnosis_fork_version},
    {// Gnosis chiado
     .chain_id                = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 10200ULL),
     .fork_epochs             = eth_chiado_fork_epochs,
     .genesis_validators_root = "\x9d\x64\x2d\xac\x73\x05\x8f\xbf\x39\xc0\xae\x41\xab\x1e\x34\xe4\xd8\x89\x04\x3c\xb1\x99\x85\x1d\xed\x70\x95\xbc\x99\xeb\x4c\x1e",
     .slots_per_epoch_bits    = 4,
     .epochs_per_period_bits  = 9,
     .fork_version_func       = gnosis_fork_version

    },
};

const chain_spec_t* c4_eth_get_chain_spec(chain_id_t id) {
  for (int i = 0; i < sizeof(chain_data) / sizeof(chain_data[0]); i++) {
    if (chain_data[i].chain_id == id)
      return chain_data + i;
  }
  return NULL;
}
// const uint64_t eth_mainnet_fork_epochs[] = {74240, 144896, 194048, 269568, 364032, 0};
// const uint64_t eth_gnosis_fork_epochs[]  = {512, 385536, 648704, 889856, 1337856, 0};

const ssz_def_t* eth_ssz_type_for_fork(eth_ssz_type_t type, fork_id_t fork, chain_id_t chain_id) {
  switch (fork) {
    case C4_FORK_DENEB: return eth_ssz_type_for_denep(type, chain_id);
    case C4_FORK_ELECTRA: return eth_ssz_type_for_electra(type, chain_id);
    default: return NULL;
  }
}

bool c4_chain_genesis_validators_root(chain_id_t chain_id, bytes32_t genesis_validators_root) {
  const chain_spec_t* data = c4_eth_get_chain_spec(chain_id);
  if (data) {
    memcpy(genesis_validators_root, data->genesis_validators_root, 32);
    return true;
  }
  return false;
}

fork_id_t c4_chain_fork_id(chain_id_t chain_id, uint64_t epoch) {
  const chain_spec_t* data = c4_eth_get_chain_spec(chain_id);
  if (!data) return C4_FORK_ALTAIR;

  int i = 0;
  while (data->fork_epochs[i] && epoch >= data->fork_epochs[i]) i++;
  return (fork_id_t) i;
}
