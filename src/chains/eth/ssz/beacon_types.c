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
#define NOT_ASSIGNED_YET 0xffffffffffffffffULL
#define FORKS_END        0xfffffffffffffffeULL // must stay < NOT_ASSIGNED_YET

#include "beacon_types.h"
#include "ssz.h"

// fork_epochs[n] = activation epoch of fork (n+1) (Altair ..).
// 0 = active at genesis. NOT_ASSIGNED_YET = not scheduled. Terminated by FORKS_END.
static const uint64_t eth_mainnet_fork_epochs[]     = {74240ULL, 144896ULL, 194048ULL, 269568ULL, 364032ULL, 411392ULL, NOT_ASSIGNED_YET, FORKS_END};
static const uint64_t eth_gnosis_fork_epochs[]      = {512ULL, 385536ULL, 648704ULL, 889856ULL, 1337856ULL, 1714688ULL, NOT_ASSIGNED_YET, FORKS_END};
static const uint64_t eth_sepolia_fork_epochs[]     = {50L, 100L, 56832L, 132608L, 222464L, 272640L, NOT_ASSIGNED_YET, FORKS_END};
static const uint64_t eth_chiado_fork_epochs[]      = {90L, 180L, 244224L, 516608L, 948224L, 1353216L, NOT_ASSIGNED_YET, FORKS_END};
static const uint64_t eth_plataberget_fork_epochs[] = {0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 1536ULL, FORKS_END};

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

// Platåberget / glamsterdam-devnet-8: GENESIS_FORK_VERSION 0x10733183, then
// first byte steps 0x10 per fork (Altair 0x20 .. Gloas 0x80). See
// ethpandaops/glamsterdam-devnets network-configs/devnet-8/metadata/config.yaml
static void plataberget_fork_version(chain_id_t chain_id, fork_id_t fork, uint8_t* version) {
  (void) chain_id;
  version[0] = (uint8_t) ((fork + 1) << 4);
  version[1] = 0x73;
  version[2] = 0x31;
  version[3] = 0x83;
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
    {// Plataberget
     .chain_id                 = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 7091047534),
     .fork_epochs              = eth_plataberget_fork_epochs,
     .genesis_validators_root  = "\xbb\x4a\x1a\x9e\x3f\x7f\x4e\x10\xed\xcd\x73\x4e\x4a\xcc\x3b\x5f\xfd\x4f\x83\x0e\xfe\x0a\xf2\x74\x8f\xa4\x58\xcf\xee\x5d\x26\x58",
     .zk_sync_keys_root        = "\x82\xb2\x41\xf5\x2b\x29\x0f\x82\x78\x81\x11\xbd\x79\x74\xee\x87\xd9\xbb\xac\xfb\xe5\xd0\x84\xa3\x70\x31\x7f\x34\xe7\xb7\xfa\x84", // TODO: replace Sepolia placeholder with Plataberget v6 anchor
     .slots_per_epoch_bits     = 5,
     .epochs_per_period_bits   = 8,
     .weak_subjectivity_epochs = 3682,
     .fork_version_func        = plataberget_fork_version},
    {// Gnosis
     .chain_id                 = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 100ULL),
     .fork_epochs              = eth_gnosis_fork_epochs,
     .genesis_validators_root  = "\xf5\xdc\xb5\x56\x4e\x82\x9a\xab\x27\x26\x4b\x9b\xec\xd5\xdf\xaa\x01\x70\x85\x61\x12\x24\xcb\x30\x36\xf5\x73\x36\x8d\xbb\x9d\x47",
     .zk_sync_keys_root        = "\x1b\x33\xa3\x8a\x13\x18\x7f\xcf\x8c\x60\xec\xf9\xf5\xeb\x44\x2a\x1c\xa0\xd5\xbe\x17\x6b\x5b\x15\xea\xe9\xc2\xda\x72\x7c\xbb\x4a", // v6 anchor: current_keys_root of period 3498 proof (oldKeys HTR, = period 3497 committee)
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
    // Fulu keeps the Electra `BeaconBlockBody` / `ExecutionPayload` layout
    // (only the state gained `proposer_lookahead`, which we do not parse here).
    case C4_FORK_FULU: return eth_ssz_type_for_electra(type, chain_id);
    case C4_FORK_GLOAS: return eth_ssz_type_for_gloas(type, chain_id);
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

  // 0 is a valid activation epoch. FORKS_END and NOT_ASSIGNED_YET are both
  // >= FORKS_END, so a single upper bound stops the scan.
  int i = 0;
  while (data->fork_epochs[i] != FORKS_END && epoch >= data->fork_epochs[i])
    i++;
  return (fork_id_t) i;
}

bool c4_chain_schedules_fork(chain_id_t chain_id, fork_id_t fork) {
  const chain_spec_t* spec = c4_eth_get_chain_spec(chain_id);
  if (!spec || !spec->fork_epochs || fork <= C4_FORK_PHASE0) return false;
  // fork_epochs[n] activates fork n+1. Stop at FORKS_END so an out-of-range
  // fork id cannot read past the table. NOT_ASSIGNED_YET is > FORKS_END.
  unsigned want = (unsigned) fork - 1;
  unsigned n    = 0;
  while (spec->fork_epochs[n] != FORKS_END) {
    if (n == want) return spec->fork_epochs[n] < FORKS_END;
    n++;
  }
  return false;
}

// Generalized indices for the fork-specific `BeaconState` layout used by the
// light client. Values are the source of truth for both the verifier and the
// prover, and are cross-verified against the consensus spec constants (see
// `specs/gloas/light-client/sync-protocol.md`).
#define DENEP_CURRENT_SYNC_COMMITTEE_GINDEX   54
#define ELECTRA_CURRENT_SYNC_COMMITTEE_GINDEX 86
#define GLOAS_CURRENT_SYNC_COMMITTEE_GINDEX   2945
#define DENEP_NEXT_SYNC_COMMITTEE_GINDEX      55
#define ELECTRA_NEXT_SYNC_COMMITTEE_GINDEX    87
#define GLOAS_NEXT_SYNC_COMMITTEE_GINDEX      2946
#define DENEP_FINALIZED_ROOT_GINDEX           105
#define ELECTRA_FINALIZED_ROOT_GINDEX         169
#define GLOAS_FINALIZED_ROOT_GINDEX           735
// `historical_summaries` is field 27 of `BeaconState`. Pre-Gloas it sits in a
// classical container (depth 5 -> 32, depth 6 -> 64 after Electra). Gloas turns
// `BeaconState` into a `ProgressiveContainer`, so field 27 lives at
// `ssz_add_gindex(2, prog_chunk_gindex(27)) = ssz_add_gindex(2, 1926) = 2950`
// (see `prog_chunk_gindex` in `ssz_merkle.c`).
#define DENEP_HISTORICAL_SUMMARIES_GINDEX   59
#define ELECTRA_HISTORICAL_SUMMARIES_GINDEX 91
#define GLOAS_HISTORICAL_SUMMARIES_GINDEX   2950
// Position of the EL block-hash leaf inside `BeaconBlockBody` that the CL
// block-hash proof anchors against. The leaf identity differs between forks:
// - 812  (Deneb..Fulu): `execution_payload.block_hash` inside the classical
//                       `BeaconBlockBody` container (depth 10, field 812).
// - 2856 (Gloas):       `signed_execution_payload_bid.message.parent_block_hash`
//                       inside the ProgressiveContainer body (EIP-7732 ePBS).
// Both are pinned by `specs/*/light-client/sync-protocol.md` and cross-checked
// in `test_gloas_execution_block_hash_gindex`.
#define DENEP_EXECUTION_BLOCK_HASH_GINDEX 812
#define GLOAS_EXECUTION_BLOCK_HASH_GINDEX 2856

// Resolves the fork active at `slot` on `chain_id`. Centralised here so that
// callers reduce to a single lookup and the fork-detection code cannot drift.
static inline fork_id_t fork_at_slot(chain_id_t chain_id, uint64_t slot) {
  const chain_spec_t* spec = c4_eth_get_chain_spec(chain_id);
  return c4_chain_fork_id(chain_id, epoch_for_slot(slot, spec));
}

gindex_t c4_current_sync_committee_gindex(chain_id_t chain_id, uint64_t slot) {
  fork_id_t fork = fork_at_slot(chain_id, slot);
  if (fork >= C4_FORK_GLOAS) return GLOAS_CURRENT_SYNC_COMMITTEE_GINDEX;
  if (fork >= C4_FORK_ELECTRA) return ELECTRA_CURRENT_SYNC_COMMITTEE_GINDEX;
  return DENEP_CURRENT_SYNC_COMMITTEE_GINDEX;
}

gindex_t c4_next_sync_committee_gindex(chain_id_t chain_id, uint64_t slot) {
  fork_id_t fork = fork_at_slot(chain_id, slot);
  if (fork >= C4_FORK_GLOAS) return GLOAS_NEXT_SYNC_COMMITTEE_GINDEX;
  if (fork >= C4_FORK_ELECTRA) return ELECTRA_NEXT_SYNC_COMMITTEE_GINDEX;
  return DENEP_NEXT_SYNC_COMMITTEE_GINDEX;
}

gindex_t c4_finalized_root_gindex(chain_id_t chain_id, uint64_t slot) {
  fork_id_t fork = fork_at_slot(chain_id, slot);
  if (fork >= C4_FORK_GLOAS) return GLOAS_FINALIZED_ROOT_GINDEX;
  if (fork >= C4_FORK_ELECTRA) return ELECTRA_FINALIZED_ROOT_GINDEX;
  return DENEP_FINALIZED_ROOT_GINDEX;
}

gindex_t c4_historical_summaries_gindex(chain_id_t chain_id, uint64_t slot) {
  fork_id_t fork = fork_at_slot(chain_id, slot);
  if (fork >= C4_FORK_GLOAS) return GLOAS_HISTORICAL_SUMMARIES_GINDEX;
  if (fork >= C4_FORK_ELECTRA) return ELECTRA_HISTORICAL_SUMMARIES_GINDEX;
  return DENEP_HISTORICAL_SUMMARIES_GINDEX;
}

gindex_t c4_execution_block_hash_gindex(chain_id_t chain_id, uint64_t slot) {
  fork_id_t fork = fork_at_slot(chain_id, slot);
  if (fork >= C4_FORK_GLOAS) return GLOAS_EXECUTION_BLOCK_HASH_GINDEX;
  return DENEP_EXECUTION_BLOCK_HASH_GINDEX;
}

// SSZ shape of the two intermediate levels of the historic-block proof. These
// are re-declared here (identical copies exist in the prover and in the server's
// period_store) because the gindex helper is the single source of truth for
// what a well-formed proof MUST hash against. Any drift between the copies is
// caught by `test_gloas_state_gindexes` / prover unit tests.
static const ssz_def_t HISTORIC_HISTORICAL_SUMMARY[] = {
    SSZ_BYTES32("block_summary_root"),
    SSZ_BYTES32("state_summary_root")};
static const ssz_def_t HISTORIC_HISTORICAL_SUMMARY_CONTAINER =
    SSZ_CONTAINER("HISTORICAL_SUMMARY", HISTORIC_HISTORICAL_SUMMARY);
static const ssz_def_t HISTORIC_SUMMARIES_LIST =
    SSZ_LIST("summaries", HISTORIC_HISTORICAL_SUMMARY_CONTAINER, 1 << 24);
static const ssz_def_t HISTORIC_BLOCK_ROOTS_VECTOR =
    SSZ_VECTOR("blocks", ssz_bytes32, 8192);

gindex_t c4_historic_block_gindex(chain_id_t chain_id, uint64_t block_slot, uint64_t state_slot) {
  const chain_spec_t* chain = c4_eth_get_chain_spec(chain_id);
  if (!chain) return 0;

  // Historic-direct proofs only exist for blocks whose slot has a corresponding
  // entry in `historical_summaries`, which was introduced with Capella. Note the
  // `fork_epochs` off-by-one: `fork_epochs[n]` holds the activation epoch of
  // fork `n+1` (fork_epochs[0] = Altair, fork_epochs[1] = Bellatrix,
  // fork_epochs[2] = Capella), so Capella is `fork_epochs[C4_FORK_CAPELLA - 1]`.
  uint64_t capella_epoch = chain->fork_epochs[C4_FORK_CAPELLA - 1];
  if (capella_epoch >= FORKS_END) return 0;
  uint64_t offset_period = capella_epoch >> chain->epochs_per_period_bits;
  uint64_t block_period  = block_slot >> (chain->slots_per_epoch_bits + chain->epochs_per_period_bits);
  if (block_period < offset_period) return 0;

  uint64_t summary_idx = block_period - offset_period;
  uint64_t block_idx   = block_slot & 0x1FFFULL; // slot % 8192

  gindex_t summaries_gidx = c4_historical_summaries_gindex(chain_id, state_slot);
  gindex_t period_gidx    = ssz_gindex(&HISTORIC_SUMMARIES_LIST, 2, (int) summary_idx, "block_summary_root");
  gindex_t block_gidx     = ssz_gindex(&HISTORIC_BLOCK_ROOTS_VECTOR, 1, (int) block_idx);
  if (!summaries_gidx || !period_gidx || !block_gidx) return 0;

  return ssz_add_gindex(ssz_add_gindex(summaries_gidx, period_gidx), block_gidx);
}
