/*
 * Copyright (c) 2025,2026 corpus.core GmbH
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

#include "chain_spec.h"
#include "bytes.h"
#include "crypto.h"
#include "json.h"
#include "ssz.h"
#include "state.h"
#include <string.h>

// fork_epochs[n] = activation epoch of fork (n+1) (Altair ..).
// 0 = active at genesis. NOT_ASSIGNED_YET = not scheduled. Terminated by FORKS_END.
static const uint64_t eth_mainnet_fork_epochs[]     = {74240ULL, 144896ULL, 194048ULL, 269568ULL, 364032ULL, 411392ULL, NOT_ASSIGNED_YET, FORKS_END};
static const uint64_t eth_gnosis_fork_epochs[]      = {512ULL, 385536ULL, 648704ULL, 889856ULL, 1337856ULL, 1714688ULL, NOT_ASSIGNED_YET, FORKS_END};
static const uint64_t eth_sepolia_fork_epochs[]     = {50L, 100L, 56832L, 132608L, 222464L, 272640L, 353024L, NOT_ASSIGNED_YET, FORKS_END};
static const uint64_t eth_chiado_fork_epochs[]      = {90L, 180L, 244224L, 516608L, 948224L, 1353216L, NOT_ASSIGNED_YET, FORKS_END};
static const uint64_t eth_plataberget_fork_epochs[] = {0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 1536ULL, FORKS_END};

// EIP-7892 blob schedules, DESCENDING by activation timestamp so the first
// match wins. Timestamps come from go-ethereum's `params/config.go` (mainnet,
// sepolia, hoodi) and the associated EIP metas (7607/8134). Terminated by
// {0,0}. Post-Merge forks are timestamp-based (see geth `config.go`, "Fork
// scheduling was switched from blocks to timestamps here").
static const eth_blob_schedule_t eth_mainnet_blob_schedule[] = {
    {1767747671ULL, 11684671ULL}, // BPO2  (2026-01-07)
    {1765290071ULL, 8346193ULL},  // BPO1  (2025-12-09)
    {1764798551ULL, 5007716ULL},  // Fusaka/Osaka (2025-12-03), inherits Prague
    {1746612311ULL, 5007716ULL},  // Prague/Pectra (2025-05-07)
    {1710338135ULL, 3338477ULL},  // Cancun/Deneb  (2024-03-13)
    {0ULL, 0ULL},
};

static const eth_blob_schedule_t eth_sepolia_blob_schedule[] = {
    {1761607008ULL, 11684671ULL}, // BPO2  (2025-10-27)
    {1761017184ULL, 8346193ULL},  // BPO1  (2025-10-21)
    {1760427360ULL, 5007716ULL},  // Fusaka/Osaka (2025-10-14)
    {1741159776ULL, 5007716ULL},  // Prague/Pectra
    {1706655072ULL, 3338477ULL},  // Cancun/Deneb  (2024-01-30)
    {0ULL, 0ULL},
};

// Gnosis chains keep `baseFeeUpdateFraction = 1112826` across all forks and
// have no BPO scheduled (blob target/max stay at 1/2). Timestamps come from
// erigontech/erigon `chainspecs/gnosis.json` and `chainspecs/chiado.json`.
// NOTE: Gnosis' `minBlobGasPrice` is 1 gwei (1e9 wei), not 1 wei -- carried
// on the chain_spec via `min_blob_base_fee` (below).
static const eth_blob_schedule_t eth_gnosis_blob_schedule[] = {
    {1776168380ULL, 1112826ULL}, // Osaka
    {1746021820ULL, 1112826ULL}, // Prague/Pectra
    {1710181820ULL, 1112826ULL}, // Cancun/Deneb
    {0ULL, 0ULL},
};

static const eth_blob_schedule_t eth_chiado_blob_schedule[] = {
    {1773653580ULL, 1112826ULL}, // Osaka
    {1741254220ULL, 1112826ULL}, // Prague/Pectra
    {1706724940ULL, 1112826ULL}, // Cancun/Deneb
    {0ULL, 0ULL},
};

// Consensus-layer BLOB_SCHEDULE for `compute_fork_digest` / `get_blob_parameters`.
// DESCENDING by epoch so the first match wins. Terminated by max_blobs == 0.
// Official sources: consensus-specs configs/mainnet.yaml, eth-clients/sepolia,
// ethpandaops/glamsterdam-devnets (Platåberget / devnet-8). Gnosis has no BPO
// entries (empty schedule → Electra fallback).
static const eth_blob_params_t eth_mainnet_blob_params[] = {
    {419072ULL, 21ULL}, // BPO2
    {412672ULL, 15ULL}, // BPO1
    {0ULL, 0ULL},
};

static const eth_blob_params_t eth_sepolia_blob_params[] = {
    {275712ULL, 21ULL}, // BPO2
    {274176ULL, 15ULL}, // BPO1
    {0ULL, 0ULL},
};

static const eth_blob_params_t eth_plataberget_blob_params[] = {
    {0ULL, 21ULL}, // genesis-at-Fulu: single BLOB_SCHEDULE entry
    {0ULL, 0ULL},
};

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
     .chain_id                    = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 1ULL),
     .fork_epochs                 = eth_mainnet_fork_epochs,
     .genesis_validators_root     = "\x4b\x36\x3d\xb9\x4e\x28\x61\x20\xd7\x6e\xb9\x05\x34\x0f\xdd\x4e\x54\xbf\xe9\xf0\x6b\xf3\x3f\xf6\xcf\x5a\xd2\x7f\x51\x1b\xfe\x95",
     .zk_sync_keys_root           = "\xc6\x10\xd3\xcf\x3f\xf6\xf4\x02\x48\xad\xe8\x12\xe5\x70\x85\x7e\x74\x12\xaf\x35\x45\xcb\xee\x91\x75\xcf\x54\xcc\xcf\xa2\x21\x3c", // pubkeys-only HTR of period 1844 committee (= 1845 proof oldKeys / 1844 proof newKeys). Not the LCU SyncCommittee container root (that includes aggregatePubkey).
     .slots_per_epoch_bits        = 5,
     .epochs_per_period_bits      = 8,
     .weak_subjectivity_epochs    = 3682,
     .fork_version_func           = mainnet_fork_version,
     .blob_schedule               = eth_mainnet_blob_schedule,
     .blob_params                 = eth_mainnet_blob_params,
     .max_blobs_per_block_electra = 9ULL},
    {// Sepolia
     .chain_id                    = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 11155111),
     .fork_epochs                 = eth_sepolia_fork_epochs,
     .genesis_validators_root     = "\xd8\xea\x17\x1f\x3c\x94\xae\xa2\x1e\xbc\x42\xa1\xed\x61\x05\x2a\xcf\x3f\x92\x09\xc0\x0e\x4e\xfb\xaa\xdd\xac\x09\xed\x9b\x80\x78",
     .zk_sync_keys_root           = "\xee\x5c\x88\x0d\x52\x41\x66\xb4\xb1\xd3\xed\xda\xba\xea\xcb\x3f\xdf\x1e\x40\xc9\x00\x8f\x25\x6e\x35\x7e\x72\x2d\x80\xba\x97\x25", // pubkeys-only HTR of period 1347 committee (= 1348 proof oldKeys). Not the LCU SyncCommittee container root.
     .slots_per_epoch_bits        = 5,
     .epochs_per_period_bits      = 8,
     .weak_subjectivity_epochs    = 3682,
     .fork_version_func           = sepolia_fork_version,
     .blob_schedule               = eth_sepolia_blob_schedule,
     .blob_params                 = eth_sepolia_blob_params,
     .max_blobs_per_block_electra = 9ULL},
    {// Plataberget
     .chain_id                    = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 7091047534),
     .fork_epochs                 = eth_plataberget_fork_epochs,
     .genesis_validators_root     = "\xbb\x4a\x1a\x9e\x3f\x7f\x4e\x10\xed\xcd\x73\x4e\x4a\xcc\x3b\x5f\xfd\x4f\x83\x0e\xfe\x0a\xf2\x74\x8f\xa4\x58\xcf\xee\x5d\x26\x58",
     .zk_sync_keys_root           = "\x82\xb2\x41\xf5\x2b\x29\x0f\x82\x78\x81\x11\xbd\x79\x74\xee\x87\xd9\xbb\xac\xfb\xe5\xd0\x84\xa3\x70\x31\x7f\x34\xe7\xb7\xfa\x84", // TODO: replace Sepolia placeholder with Plataberget v6 anchor
     .slots_per_epoch_bits        = 5,
     .epochs_per_period_bits      = 8,
     .weak_subjectivity_epochs    = 3682,
     .fork_version_func           = plataberget_fork_version,
     .blob_params                 = eth_plataberget_blob_params,
     .max_blobs_per_block_electra = 9ULL},
    {// Gnosis
     .chain_id                    = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 100ULL),
     .fork_epochs                 = eth_gnosis_fork_epochs,
     .genesis_validators_root     = "\xf5\xdc\xb5\x56\x4e\x82\x9a\xab\x27\x26\x4b\x9b\xec\xd5\xdf\xaa\x01\x70\x85\x61\x12\x24\xcb\x30\x36\xf5\x73\x36\x8d\xbb\x9d\x47",
     .zk_sync_keys_root           = "\x19\x97\x24\x9f\x4d\xd2\xf3\x66\x53\x05\x2f\x43\x8c\xe4\x80\x9a\x2d\xb7\xfa\xb8\xa3\x3f\x49\xc2\x2f\x61\x32\xd2\xa1\x07\xb8\xe0", // pubkeys-only HTR of period 3642 committee (= 3643 proof oldKeys). Not the LCU SyncCommittee container root.
     .slots_per_epoch_bits        = 4,
     .epochs_per_period_bits      = 9,
     .weak_subjectivity_epochs    = 1500,
     .fork_version_func           = gnosis_fork_version,
     .blob_schedule               = eth_gnosis_blob_schedule,
     .min_blob_base_fee           = 1000000000ULL,
     .max_blobs_per_block_electra = 2ULL},
    {// Gnosis chiado
     .chain_id                    = CHAIN_ID(C4_CHAIN_TYPE_ETHEREUM, 10200ULL),
     .fork_epochs                 = eth_chiado_fork_epochs,
     .genesis_validators_root     = "\x9d\x64\x2d\xac\x73\x05\x8f\xbf\x39\xc0\xae\x41\xab\x1e\x34\xe4\xd8\x89\x04\x3c\xb1\x99\x85\x1d\xed\x70\x95\xbc\x99\xeb\x4c\x1e",
     .slots_per_epoch_bits        = 4,
     .epochs_per_period_bits      = 9,
     .weak_subjectivity_epochs    = 1500,
     .fork_version_func           = gnosis_fork_version,
     .blob_schedule               = eth_chiado_blob_schedule,
     .min_blob_base_fee           = 1000000000ULL,
     .max_blobs_per_block_electra = 2ULL},
};

// Additional chain specs registered at runtime through `c4_eth_register_chain`.
// Built-in `chain_data[]` entries always take precedence: the lookup below
// scans them first, so a plugin cannot override mainnet / sepolia / gnosis.
// The upper bound is intentionally small -- the real world does not need 32
// distinct Ethereum-fork chains in a single process. Bump if ever required.
#define ETH_CHAIN_SPEC_EXTRA_MAX 8

static chain_spec_t eth_chain_data_extra[ETH_CHAIN_SPEC_EXTRA_MAX];
static uint32_t     eth_chain_data_extra_len = 0;

const chain_spec_t* c4_eth_get_chain_spec(chain_id_t id) {
  for (int i = 0; i < sizeof(chain_data) / sizeof(chain_data[0]); i++) {
    if (chain_data[i].chain_id == id)
      return chain_data + i;
  }
  for (uint32_t i = 0; i < eth_chain_data_extra_len; i++) {
    if (eth_chain_data_extra[i].chain_id == id)
      return eth_chain_data_extra + i;
  }
  return NULL;
}

// Generic `fork_version_func_t` used for chains registered through
// `c4_eth_register_chain`. Reads the 4-byte version straight out of the
// chain's `fork_versions` table. Built-in chains keep their dedicated
// per-chain functions (mainnet / sepolia / gnosis / plataberget) because
// their on-wire version schemes depend on chain-id arithmetic.
static void generic_fork_version(chain_id_t chain_id, fork_id_t fork, uint8_t* version) {
  const chain_spec_t* spec = c4_eth_get_chain_spec(chain_id);
  if (!spec || !spec->fork_versions || fork < C4_FORK_PHASE0 || fork > C4_FORK_MAX) {
    memset(version, 0, 4);
    return;
  }
  memcpy(version, spec->fork_versions + ((size_t) fork) * 4, 4);
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

// ForkData used by `compute_fork_data_root` / `compute_fork_digest`.
static const ssz_def_t FORK_DATA[] = {
    SSZ_BYTE_VECTOR("version", 4),
    SSZ_BYTES32("state")};
static const ssz_def_t FORK_DATA_CONTAINER = SSZ_CONTAINER("ForkData", FORK_DATA);

static uint64_t fork_activation_epoch(const chain_spec_t* spec, fork_id_t fork) {
  if (fork <= C4_FORK_PHASE0) return 0;
  return spec->fork_epochs[(unsigned) fork - 1];
}

static fork_id_t fork_id_at_epoch(const chain_spec_t* spec, uint64_t epoch) {
  return c4_chain_fork_id(spec->chain_id, epoch);
}

bool c4_eth_fork_data_root(const chain_spec_t* spec, fork_id_t fork, bytes32_t out) {
  if (!spec || !spec->fork_version_func || !out || fork < C4_FORK_PHASE0 || fork > C4_FORK_MAX)
    return false;
  uint8_t buffer[36] = {0};
  spec->fork_version_func(spec->chain_id, fork, buffer);
  memcpy(buffer + 4, spec->genesis_validators_root, 32);
  ssz_hash_tree_root(ssz_ob(FORK_DATA_CONTAINER, bytes(buffer, 36)), out);
  return true;
}

static void get_blob_parameters(const chain_spec_t* spec, uint64_t epoch,
                                uint64_t* out_epoch, uint64_t* out_max_blobs) {
  uint64_t electra_epoch = fork_activation_epoch(spec, C4_FORK_ELECTRA);
  if (electra_epoch >= FORKS_END) electra_epoch = 0;
  *out_epoch     = electra_epoch;
  *out_max_blobs = spec->max_blobs_per_block_electra ? spec->max_blobs_per_block_electra : 9ULL;
  if (!spec->blob_params) return;
  for (const eth_blob_params_t* e = spec->blob_params; e->max_blobs_per_block != 0; e++) {
    if (epoch >= e->epoch) {
      *out_epoch     = e->epoch;
      *out_max_blobs = e->max_blobs_per_block;
      return;
    }
  }
}

static bool compute_digest_at_epoch(const chain_spec_t* spec, uint64_t epoch, uint8_t out[4]) {
  fork_id_t fork = fork_id_at_epoch(spec, epoch);
  bytes32_t base = {0};
  if (!c4_eth_fork_data_root(spec, fork, base)) return false;

  uint64_t fulu_epoch = fork_activation_epoch(spec, C4_FORK_FULU);
  if (fulu_epoch >= FORKS_END || epoch < fulu_epoch) {
    memcpy(out, base, 4);
    return true;
  }

  uint64_t bp_epoch = 0, max_blobs = 0;
  get_blob_parameters(spec, epoch, &bp_epoch, &max_blobs);
  uint8_t blob_in[16] = {0};
  uint64_to_le(blob_in, bp_epoch);
  uint64_to_le(blob_in + 8, max_blobs);
  bytes32_t mask = {0};
  sha256(bytes(blob_in, 16), mask);
  for (int i = 0; i < 4; i++)
    out[i] = (uint8_t) (base[i] ^ mask[i]);
  return true;
}

bool c4_eth_compute_fork_digest(chain_id_t chain_id, fork_id_t fork, uint8_t out[4]) {
  const chain_spec_t* spec = c4_eth_get_chain_spec(chain_id);
  if (!spec || !out || fork < C4_FORK_PHASE0 || fork > C4_FORK_MAX) return false;
  return compute_digest_at_epoch(spec, fork_activation_epoch(spec, fork), out);
}

// One digest per regular fork plus room for future BPO rows. Overflow must
// not mark the cache ready: a silent drop would fail-closed on a known fork.
#define MAX_FORK_DIGEST_CANDIDATES ((unsigned) C4_FORK_MAX + 1 + 16)

typedef struct {
  bool    ready;
  uint8_t count;
  uint8_t digests[MAX_FORK_DIGEST_CANDIDATES][4];
  int8_t  forks[MAX_FORK_DIGEST_CANDIDATES];
} fork_digest_cache_t;

static fork_digest_cache_t digest_caches[sizeof(chain_data) / sizeof(chain_data[0])];

static bool cache_add(fork_digest_cache_t* cache, const uint8_t digest[4], fork_id_t fork) {
  if (cache->count >= MAX_FORK_DIGEST_CANDIDATES) return false;
  for (uint8_t i = 0; i < cache->count; i++) {
    if (memcmp(cache->digests[i], digest, 4) == 0) return true;
  }
  memcpy(cache->digests[cache->count], digest, 4);
  cache->forks[cache->count] = (int8_t) fork;
  cache->count++;
  return true;
}

static void cache_build(const chain_spec_t* spec, fork_digest_cache_t* cache) {
  cache->count = 0;
  cache->ready = false;
  for (int f = 0; f <= (int) C4_FORK_MAX; f++) {
    fork_id_t fork = (fork_id_t) f;
    if (fork != C4_FORK_PHASE0 && !c4_chain_schedules_fork(spec->chain_id, fork))
      continue;
    uint8_t digest[4] = {0};
    if (!compute_digest_at_epoch(spec, fork_activation_epoch(spec, fork), digest))
      continue;
    if (!cache_add(cache, digest, fork)) return;
  }
  if (spec->blob_params) {
    for (const eth_blob_params_t* e = spec->blob_params; e->max_blobs_per_block != 0; e++) {
      uint8_t   digest[4] = {0};
      fork_id_t fork      = fork_id_at_epoch(spec, e->epoch);
      if (fork > C4_FORK_MAX) continue;
      if (fork != C4_FORK_PHASE0 && !c4_chain_schedules_fork(spec->chain_id, fork))
        continue;
      if (!compute_digest_at_epoch(spec, e->epoch, digest)) continue;
      if (!cache_add(cache, digest, fork)) return;
    }
  }
  cache->ready = true;
}

fork_id_t c4_eth_fork_from_digest(chain_id_t chain_id, const uint8_t digest[4]) {
  const chain_spec_t* spec = c4_eth_get_chain_spec(chain_id);
  if (!spec || !digest) return C4_FORK_INVALID;
  int idx = (int) (spec - chain_data);
  if (idx < 0 || idx >= (int) (sizeof(chain_data) / sizeof(chain_data[0])))
    return C4_FORK_INVALID;
  fork_digest_cache_t* cache = digest_caches + idx;
  if (!cache->ready) cache_build(spec, cache);
  for (uint8_t i = 0; i < cache->count; i++) {
    if (memcmp(cache->digests[i], digest, 4) == 0)
      return (fork_id_t) cache->forks[i];
  }
  return C4_FORK_INVALID;
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
// `current_justified_checkpoint` is field 19, the container immediately before
// `finalized_checkpoint` (field 20). `.root` is the right child of that
// container, so these sit two gindices left of the finalized-root constants
// and share their depth. `"safe"` pins this leaf; `"finalized"` pins the other.
#define DENEP_JUSTIFIED_ROOT_GINDEX   103
#define ELECTRA_JUSTIFIED_ROOT_GINDEX 167
#define GLOAS_JUSTIFIED_ROOT_GINDEX   733
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

gindex_t c4_current_justified_root_gindex(chain_id_t chain_id, uint64_t slot) {
  fork_id_t fork = fork_at_slot(chain_id, slot);
  if (fork >= C4_FORK_GLOAS) return GLOAS_JUSTIFIED_ROOT_GINDEX;
  if (fork >= C4_FORK_ELECTRA) return ELECTRA_JUSTIFIED_ROOT_GINDEX;
  return DENEP_JUSTIFIED_ROOT_GINDEX;
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

// JSON schema for a single chain registration. Kept near the parser so a
// field addition updates both pieces together. `fork_epochs` holds one entry
// per scheduled fork (Altair, Bellatrix, ...); `fork_versions` holds one
// entry per fork id including Phase0, so its length is `fork_epochs` + 1.
// `chain_id` accepts both JSON numbers (`42`) and hex-strings (`"0x2a"`),
// matching the Ethereum RPC convention used by wallets and node configs.
// `fork_versions` must each be a 4-byte hex string; `[bytes]` enforces the
// hex / 0x-prefix at validation time, the runtime loop below enforces the
// exact width of 4 bytes.
static const char* ETH_REGISTER_CHAIN_SCHEMA =
    "{chain_id:uint|hexuint,"
    "genesis_validators_root:bytes32,"
    "fork_epochs:[uint],"
    "fork_versions:[bytes],"
    "slots_per_epoch_bits?:uint,"
    "epochs_per_period_bits?:uint,"
    "weak_subjectivity_epochs?:uint,"
    "zk_sync_keys_root?:bytes32,"
    "min_blob_base_fee?:uint,"
    "max_blobs_per_block_electra?:uint,"
    "blob_schedule?:[[uint]],"
    "blob_params?:[[uint]]}";

// Decodes a JSON `[[uint, uint], ...]` into a newly allocated array of
// `{uint64_t, uint64_t}` pairs with a `{0, 0}`-sentinel appended. Both
// `eth_blob_schedule_t` and `eth_blob_params_t` happen to start with two
// `uint64_t` fields, so this helper covers both without typing.
//
// Returns NULL when the input node is missing or empty; `*ok` stays `true`
// in that case because an absent optional field is not an error. On parse
// failure the freshly-allocated array is released and `*ok` is set to false
// with an error pushed onto `state`.
static void* parse_u64_pair_array(c4_state_t* state, json_t node, size_t pair_size, bool* ok) {
  *ok = true;
  if (node.type == JSON_TYPE_NOT_FOUND || json_len(node) == 0) return NULL;
  size_t n     = json_len(node);
  void*  array = safe_calloc(n + 1, pair_size);
  size_t i     = 0;
  json_for_each_value(node, pair) {
    if (json_len(pair) != 2) {
      c4_state_add_error(state, "c4_eth_register_chain: expected pair of two uints");
      safe_free(array);
      *ok = false;
      return NULL;
    }
    uint64_t* dst = (uint64_t*) ((uint8_t*) array + i * pair_size);
    dst[0]        = json_as_uint64(json_at(pair, 0));
    dst[1]        = json_as_uint64(json_at(pair, 1));
    i++;
  }
  return array;
}

bool c4_eth_register_chain(c4_state_t* state, json_t data) {
  if (!state) return false;
  const char* schema_err = json_validate(data, ETH_REGISTER_CHAIN_SCHEMA, "c4_eth_register_chain");
  if (schema_err) {
    c4_state_add_error(state, schema_err);
    safe_free((void*) schema_err);
    return false;
  }

  chain_id_t chain_id = (chain_id_t) json_get_uint64(data, "chain_id");
  if (chain_id == 0) {
    c4_state_add_error(state, "c4_eth_register_chain: chain_id must not be 0");
    return false;
  }
  if (c4_eth_get_chain_spec(chain_id)) {
    c4_state_add_error(state, "c4_eth_register_chain: chain_id is already registered");
    return false;
  }
  if (eth_chain_data_extra_len >= ETH_CHAIN_SPEC_EXTRA_MAX) {
    c4_state_add_error(state, "c4_eth_register_chain: extra chain spec table is full");
    return false;
  }

  json_t fork_epochs_json   = json_get(data, "fork_epochs");
  json_t fork_versions_json = json_get(data, "fork_versions");
  size_t epoch_count        = json_len(fork_epochs_json);
  size_t version_count      = json_len(fork_versions_json);
  if (epoch_count == 0 || version_count != epoch_count + 1 || version_count > C4_FORK_MAX + 1) {
    c4_state_add_error(state, "c4_eth_register_chain: fork_versions must have fork_epochs+1 entries, at most C4_FORK_MAX+1");
    return false;
  }

  // Allocate side-tables (freed by `c4_eth_reset_chain_spec_registry`).
  uint64_t* fork_epochs   = safe_calloc(epoch_count + 1, sizeof(uint64_t));
  uint8_t*  fork_versions = safe_calloc(C4_FORK_MAX + 1, 4);
  for (size_t i = 0; i < epoch_count; i++)
    fork_epochs[i] = json_as_uint64(json_at(fork_epochs_json, i));
  fork_epochs[epoch_count] = FORKS_END;

  for (size_t i = 0; i < version_count; i++) {
    buffer_t tmp    = {0};
    bytes_t  parsed = json_as_bytes(json_at(fork_versions_json, i), &tmp);
    if (!parsed.data || parsed.len != 4) {
      buffer_free(&tmp);
      safe_free(fork_epochs);
      safe_free(fork_versions);
      c4_state_add_error(state, "c4_eth_register_chain: each fork_versions entry must be 4 bytes");
      return false;
    }
    memcpy(fork_versions + i * 4, parsed.data, 4);
    buffer_free(&tmp);
  }

  bool                 ok            = true;
  eth_blob_schedule_t* blob_schedule = (eth_blob_schedule_t*) parse_u64_pair_array(
      state, json_get(data, "blob_schedule"), sizeof(eth_blob_schedule_t), &ok);
  if (!ok) {
    safe_free(fork_epochs);
    safe_free(fork_versions);
    return false;
  }
  eth_blob_params_t* blob_params = (eth_blob_params_t*) parse_u64_pair_array(
      state, json_get(data, "blob_params"), sizeof(eth_blob_params_t), &ok);
  if (!ok) {
    safe_free(fork_epochs);
    safe_free(fork_versions);
    safe_free(blob_schedule);
    return false;
  }

  chain_spec_t* slot = &eth_chain_data_extra[eth_chain_data_extra_len];
  memset(slot, 0, sizeof(*slot));
  slot->chain_id = chain_id;
  json_t root    = json_get(data, "genesis_validators_root");
  if (!json_as_bytes32(root, slot->genesis_validators_root)) {
    safe_free(fork_epochs);
    safe_free(fork_versions);
    safe_free(blob_schedule);
    safe_free(blob_params);
    c4_state_add_error(state, "c4_eth_register_chain: genesis_validators_root must be 32 bytes");
    return false;
  }
  json_t zk_root = json_get(data, "zk_sync_keys_root");
  if (zk_root.type != JSON_TYPE_NOT_FOUND && !json_as_bytes32(zk_root, slot->zk_sync_keys_root)) {
    safe_free(fork_epochs);
    safe_free(fork_versions);
    safe_free(blob_schedule);
    safe_free(blob_params);
    c4_state_add_error(state, "c4_eth_register_chain: zk_sync_keys_root must be 32 bytes");
    return false;
  }

  slot->fork_epochs   = fork_epochs;
  slot->fork_versions = fork_versions;
  slot->blob_schedule = blob_schedule;
  slot->blob_params   = blob_params;

  json_t v                       = json_get(data, "slots_per_epoch_bits");
  slot->slots_per_epoch_bits     = (int) (v.type == JSON_TYPE_NOT_FOUND ? 5 : json_as_uint64(v));
  v                              = json_get(data, "epochs_per_period_bits");
  slot->epochs_per_period_bits   = (int) (v.type == JSON_TYPE_NOT_FOUND ? 8 : json_as_uint64(v));
  v                              = json_get(data, "weak_subjectivity_epochs");
  slot->weak_subjectivity_epochs = v.type == JSON_TYPE_NOT_FOUND ? 3682ULL : json_as_uint64(v);

  slot->min_blob_base_fee           = json_get_uint64(data, "min_blob_base_fee");
  slot->max_blobs_per_block_electra = json_get_uint64(data, "max_blobs_per_block_electra");
  slot->fork_version_func           = generic_fork_version;

  eth_chain_data_extra_len++;
  return true;
}

// Test-only reset hook. Frees every heap-allocated side table added by
// `c4_eth_register_chain` and empties the extras array. Production code
// never calls this; the extras live for the lifetime of the process.
void c4_eth_reset_chain_spec_registry(void) {
  for (uint32_t i = 0; i < eth_chain_data_extra_len; i++) {
    chain_spec_t* slot = &eth_chain_data_extra[i];
    safe_free((void*) slot->fork_epochs);
    safe_free((void*) slot->fork_versions);
    safe_free((void*) slot->blob_schedule);
    safe_free((void*) slot->blob_params);
    memset(slot, 0, sizeof(*slot));
  }
  eth_chain_data_extra_len = 0;
}
