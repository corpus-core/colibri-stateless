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

#include "beacon_types.h"
#include "ssz.h"

// the fork epochs for the different chains. index 0 is the the first fork or the epcoh of the ALTAIR fork. Must be NULL-Terminated
static const uint64_t eth_mainnet_fork_epochs[] = {74240ULL, 144896ULL, 194048ULL, 269568ULL, 364032ULL, 411392ULL, 0ULL};
static const uint64_t eth_gnosis_fork_epochs[]  = {512ULL, 385536ULL, 648704ULL, 889856ULL, 1337856ULL, 1714688ULL, 0ULL};
static const uint64_t eth_sepolia_fork_epochs[] = {50L, 100L, 56832L, 132608L, 222464L, 272640L, 0ULL};
static const uint64_t eth_chiado_fork_epochs[]  = {90L, 180L, 244224L, 516608L, 948224L, 1353216L , 0ULL};

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
     .chain_id                 = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 1ULL),
     .fork_epochs              = eth_mainnet_fork_epochs,
     .genesis_validators_root  = "\x4b\x36\x3d\xb9\x4e\x28\x61\x20\xd7\x6e\xb9\x05\x34\x0f\xdd\x4e\x54\xbf\xe9\xf0\x6b\xf3\x3f\xf6\xcf\x5a\xd2\x7f\x51\x1b\xfe\x95",
     .zk_sync_keys_root        = "\xc0\x23\x61\xcb\x34\xfe\xce\x1e\xae\x2c\x74\xbd\x67\x5d\x38\x76\xc5\x3b\x93\xa7\xe8\x00\x15\x74\xf5\x49\xd2\x8c\xa8\x9c\xfb\x9b", // v6 anchor: current_keys_root of period 1784 proof (oldKeys HTR, = period 1783 committee)
     .slots_per_epoch_bits     = 5,
     .epochs_per_period_bits   = 8,
     .weak_subjectivity_epochs = 3682,
     .fork_version_func        = mainnet_fork_version},
    {// Sepolia
     .chain_id                 = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 11155111),
     .fork_epochs              = eth_sepolia_fork_epochs,
     .genesis_validators_root  = "\xd8\xea\x17\x1f\x3c\x94\xae\xa2\x1e\xbc\x42\xa1\xed\x61\x05\x2a\xcf\x3f\x92\x09\xc0\x0e\x4e\xfb\xaa\xdd\xac\x09\xed\x9b\x80\x78",
     .zk_sync_keys_root        = "\x82\xb2\x41\xf5\x2b\x29\x0f\x82\x78\x81\x11\xbd\x79\x74\xee\x87\xd9\xbb\xac\xfb\xe5\xd0\x84\xa3\x70\x31\x7f\x34\xe7\xb7\xfa\x84", // v6 anchor: current_keys_root of period 1287 proof (oldKeys HTR, = period 1286 committee)
     .slots_per_epoch_bits     = 5,
     .epochs_per_period_bits   = 8,
     .weak_subjectivity_epochs = 3682,
     .fork_version_func        = sepolia_fork_version},
    {// Gnosis
     .chain_id                 = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 100ULL),
     .fork_epochs              = eth_gnosis_fork_epochs,
     .genesis_validators_root  = "\xf5\xdc\xb5\x56\x4e\x82\x9a\xab\x27\x26\x4b\x9b\xec\xd5\xdf\xaa\x01\x70\x85\x61\x12\x24\xcb\x30\x36\xf5\x73\x36\x8d\xbb\x9d\x47",
     .zk_sync_keys_root        = "\x91\x0C\x45\x91\xFF\x6B\x25\x8D\x80\x59\xB0\xB3\xF3\x51\xA7\x3C\x13\x94\xE8\x06\xA7\x68\xD8\xA6\xB8\x23\x05\x6F\xF2\x7E\x3F\x69", // period 3111
     .slots_per_epoch_bits     = 4,
     .epochs_per_period_bits   = 9,
     .weak_subjectivity_epochs = 1500,
     .fork_version_func        = gnosis_fork_version},
    {// Gnosis chiado
     .chain_id                 = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 10200ULL),
     .fork_epochs              = eth_chiado_fork_epochs,
     .genesis_validators_root  = "\x9d\x64\x2d\xac\x73\x05\x8f\xbf\x39\xc0\xae\x41\xab\x1e\x34\xe4\xd8\x89\x04\x3c\xb1\x99\x85\x1d\xed\x70\x95\xbc\x99\xeb\x4c\x1e",
     .slots_per_epoch_bits     = 4,
     .epochs_per_period_bits   = 9,
     .weak_subjectivity_epochs = 1500,
     .fork_version_func        = gnosis_fork_version

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

#ifdef PROVER
const ssz_def_t* c4_eth_execution_payload_def(chain_id_t chain_id) {
  return eth_ssz_type_for_denep(ETH_SSZ_EXECUTION_PAYLOAD_CONTAINER, chain_id);
}
#endif

const ssz_def_t* eth_ssz_type_for_fork(eth_ssz_type_t type, fork_id_t fork, chain_id_t chain_id) {
  switch (fork) {
    case C4_FORK_DENEB: return eth_ssz_type_for_denep(type, chain_id);
    case C4_FORK_ELECTRA: return eth_ssz_type_for_electra(type, chain_id);
    case C4_FORK_FULU: return eth_ssz_type_for_electra(type, chain_id);
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

const gindex_t* c4_block_header_gindexes(chain_id_t chain_id, uint64_t slot) {
  // EP at gindex 25 in BeaconBlockBody (index 9, depth 4), field index i in EP (depth 5) → 25*32+i
  // Deneb: body has 12 fields, EP has 17 fields → EP gindex=25, same layout
  // Electra: body has 13 fields, EP has 17 fields → EP gindex=25, same layout
  static const gindex_t deneb_gindexes[BLOCK_HEADER_FIELD_COUNT] = {
      800,  // parentHash      (EP index 0)
      802,  // stateRoot       (EP index 2)
      803,  // receiptsRoot    (EP index 3)
      804,  // logsBloom       (EP index 4)
      806,  // blockNumber     (EP index 6)
      807,  // gasLimit        (EP index 7)
      808,  // gasUsed         (EP index 8)
      809,  // timestamp       (EP index 9)
      811,  // baseFeePerGas   (EP index 11)
      812,  // blockHash       (EP index 12)
      815,  // blobGasUsed     (EP index 15)
      816,  // excessBlobGas   (EP index 16)
      801,  // feeRecipient    (EP index 1)
      813}; // transactionsRoot (EP index 13, leaf = ssz_hash_tree_root(transactions))
  const chain_spec_t* spec = c4_eth_get_chain_spec(chain_id);
  fork_id_t           fork = c4_chain_fork_id(chain_id, epoch_for_slot(slot, spec));
  (void) fork;
  return deneb_gindexes;
}

const gindex_t* c4_call_block_context_gindexes(void) {
  // Order matches leaf layout: stateRoot, blockNumber, timestamp, coinbase, prevRandao, baseFeePerGas, blockHash, gasLimit, excessBlobGas
  static const gindex_t gindexes[CALL_BLOCK_CONTEXT_FIELD_COUNT] = {
      802,  // stateRoot     (EP index 2)
      806,  // blockNumber   (EP index 6)
      809,  // timestamp     (EP index 9)
      801,  // feeRecipient  (EP index 1)
      805,  // prevRandao    (EP index 5)
      811,  // baseFeePerGas (EP index 11)
      812,  // blockHash     (EP index 12)
      807,  // gasLimit      (EP index 7)
      816   // excessBlobGas (EP index 16)
  };
  return gindexes;
}
