#ifndef ETH_SSZ_TYPES_H
#define ETH_SSZ_TYPES_H

#include "chains.h"
#include "ssz.h"

typedef enum {
  C4_FORK_PHASE0    = 0,
  C4_FORK_ALTAIR    = 1,
  C4_FORK_BELLATRIX = 2,
  C4_FORK_CAPELLA   = 3,
  C4_FORK_DENEB     = 4,
  C4_FORK_ELECTRA   = 5,
  C4_FORK_FULU      = 6,

  C4_FORK_INVALID = -1
} fork_id_t;

typedef enum {
  // beacon
  ETH_SSZ_SIGNED_BEACON_BLOCK_CONTAINER = 1,
  ETH_SSZ_BEACON_BLOCK_BODY_CONTAINER   = 2,
  ETH_SSZ_BEACON_BLOCK_HEADER           = 3,
  // verify
  ETH_SSZ_VERIFY_REQUEST           = 4,
  ETH_SSZ_VERIFY_BLOCK_HASH_PROOF  = 5,
  ETH_SSZ_VERIFY_ACCOUNT_PROOF     = 6,
  ETH_SSZ_VERIFY_TRANSACTION_PROOF = 7,
  ETH_SSZ_VERIFY_RECEIPT_PROOF     = 8,
  ETH_SSZ_VERIFY_LOGS_PROOF        = 9,
  //  ETH_SSZ_VERIFY_LIGHT_CLIENT_UPDATE_LIST = 10,
  //  ETH_SSZ_VERIFY_LIGHT_CLIENT_UPDATE      = 11,
  ETH_SSZ_VERIFY_STATE_PROOF        = 12,
  ETH_SSZ_VERIFY_CALL_PROOF         = 13,
  ETH_SSZ_VERIFY_SYNC_PROOF         = 14,
  ETH_SSZ_VERIFY_BLOCK_PROOF        = 15,
  ETH_SSZ_VERIFY_BLOCK_NUMBER_PROOF = 16,
  // data types
  ETH_SSZ_DATA_NONE    = 17,
  ETH_SSZ_DATA_HASH32  = 18,
  ETH_SSZ_DATA_BYTES   = 19,
  ETH_SSZ_DATA_UINT256 = 20,
  ETH_SSZ_DATA_TX      = 21,
  ETH_SSZ_DATA_RECEIPT = 22,
  ETH_SSZ_DATA_LOGS    = 23,
  ETH_SSZ_DATA_BLOCK   = 24,
  ETH_SSZ_DATA_PROOF   = 25
} eth_ssz_type_t;

typedef struct {
  chain_id_t      chain_id;
  const uint64_t* fork_epochs;
  const bytes32_t genesis_validators_root;
  const int       slots_per_epoch_bits;   // 5 = 32 slots per epoch
  const int       epochs_per_period_bits; // 8 = 256 epochs per period
} chain_spec_t;

bool                c4_chain_genesis_validators_root(chain_id_t chain_id, bytes32_t genesis_validators_root);
fork_id_t           c4_chain_fork_id(chain_id_t chain_id, uint64_t epoch);
const chain_spec_t* c4_eth_get_chain_spec(chain_id_t id);
const ssz_def_t*    eth_ssz_type_for_fork(eth_ssz_type_t type, fork_id_t fork, chain_id_t chain_id);

// forks
const ssz_def_t* eth_ssz_type_for_denep(eth_ssz_type_t type, chain_id_t chain_id);
const ssz_def_t* eth_ssz_type_for_electra(eth_ssz_type_t type, chain_id_t chain_id);
const ssz_def_t* eth_get_light_client_update_list(fork_id_t fork);
void             c4_chain_fork_version(chain_id_t chain_id, fork_id_t fork, uint8_t* version);
// c4 specific
const ssz_def_t*       eth_ssz_verification_type(eth_ssz_type_t type);
extern const ssz_def_t ssz_transactions_bytes;
extern const ssz_def_t BEACON_BLOCK_HEADER[5];
extern const ssz_def_t LIGHT_CLIENT_HEADER[3];
extern const ssz_def_t SYNC_COMMITTEE[2];
extern const ssz_def_t SYNC_AGGREGATE[2];
extern const ssz_def_t DENEP_LIGHT_CLIENT_UPDATE[7];
extern const ssz_def_t ELECTRA_LIGHT_CLIENT_UPDATE[7];
extern const ssz_def_t DENEP_EXECUTION_PAYLOAD[17];
extern const ssz_def_t GNOSIS_EXECUTION_PAYLOAD[17];
extern const ssz_def_t DENEP_WITHDRAWAL_CONTAINER;
extern const ssz_def_t ELECTRA_EXECUTION_PAYLOAD[17];
extern const ssz_def_t ELECTRA_WITHDRAWAL_CONTAINER;

#define epoch_for_slot(slot, chain_spec)  ((slot) >> (chain_spec ? chain_spec->slots_per_epoch_bits : 5))
#define period_for_slot(slot, chain_spec) ((slot) >> (chain_spec ? chain_spec->epochs_per_period_bits : 8))

#define slot_for_epoch(epoch, chain_spec)   ((epoch) << (chain_spec ? chain_spec->slots_per_epoch_bits : 5))
#define slot_for_period(period, chain_spec) ((period) << (chain_spec ? chain_spec->epochs_per_period_bits : 8))

#define ssz_builder_for_type(typename) \
  {.def = eth_ssz_verification_type(typename), .dynamic = {0}, .fixed = {0}}

#endif
