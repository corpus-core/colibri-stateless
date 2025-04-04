#ifndef ETH_SSZ_TYPES_H
#define ETH_SSZ_TYPES_H

#include "chains.h"
#include "ssz.h"

typedef enum {
  // beacon
  ETH_SSZ_SIGNED_BEACON_BLOCK_CONTAINER = 1,
  ETH_SSZ_BEACON_BLOCK_BODY_CONTAINER   = 2,
  ETH_SSZ_BEACON_BLOCK_HEADER           = 3,
  // verify
  ETH_SSZ_VERIFY_REQUEST                  = 4,
  ETH_SSZ_VERIFY_BLOCK_HASH_PROOF         = 5,
  ETH_SSZ_VERIFY_ACCOUNT_PROOF            = 6,
  ETH_SSZ_VERIFY_TRANSACTION_PROOF        = 7,
  ETH_SSZ_VERIFY_RECEIPT_PROOF            = 8,
  ETH_SSZ_VERIFY_LOGS_PROOF               = 9,
  ETH_SSZ_VERIFY_LIGHT_CLIENT_UPDATE_LIST = 10,
  ETH_SSZ_VERIFY_LIGHT_CLIENT_UPDATE      = 11,
  ETH_SSZ_VERIFY_STATE_PROOF              = 12,
  ETH_SSZ_VERIFY_CALL_PROOF               = 13,
  ETH_SSZ_VERIFY_SYNC_PROOF               = 14,
  ETH_SSZ_VERIFY_BLOCK_PROOF              = 15,
  // data types
  ETH_SSZ_DATA_NONE    = 16,
  ETH_SSZ_DATA_HASH32  = 17,
  ETH_SSZ_DATA_BYTES   = 18,
  ETH_SSZ_DATA_UINT256 = 19,
  ETH_SSZ_DATA_TX      = 20,
  ETH_SSZ_DATA_RECEIPT = 21,
  ETH_SSZ_DATA_LOGS    = 22,
  ETH_SSZ_DATA_BLOCK   = 23
} eth_ssz_type_t;

const ssz_def_t* eth_ssz_type_for_fork(eth_ssz_type_t type, fork_id_t fork);

// forks
const ssz_def_t* eth_ssz_type_for_denep(eth_ssz_type_t type);
const ssz_def_t* eth_ssz_type_for_electra(eth_ssz_type_t type);

// c4 specific
const ssz_def_t*       eth_ssz_verification_type(eth_ssz_type_t type);
extern const ssz_def_t ssz_transactions_bytes;
extern const ssz_def_t BEACON_BLOCK_HEADER[5];
extern const ssz_def_t LIGHT_CLIENT_UPDATE[7];
extern const ssz_def_t DENEP_EXECUTION_PAYLOAD[17];
extern const ssz_def_t DENEP_WITHDRAWAL_CONTAINER;

#define ssz_builder_for_type(typename) \
  {.def = eth_ssz_verification_type(typename), .dynamic = {0}, .fixed = {0}}

#endif
