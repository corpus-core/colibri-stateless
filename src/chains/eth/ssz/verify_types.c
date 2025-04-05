
#include "beacon_types.h"
#include "ssz.h"
#include <stdio.h>
#include <stdlib.h>

#include "verify_data_types.h"
#include "verify_proof_types.h"
#include "verify_union_types.h"

static inline size_t array_idx(const ssz_def_t* array, size_t len, const ssz_def_t* target) {
  for (size_t i = 0; i < len; i++) {
    if (array[i].type >= SSZ_TYPE_CONTAINER && array[i].def.container.elements == target) return i;
  }
  return 0;
}
#define ARRAY_IDX(a, target)  array_idx(a, sizeof(a) / sizeof(ssz_def_t), target)
#define ARRAY_TYPE(a, target) a + array_idx(a, sizeof(a) / sizeof(ssz_def_t), target)

const ssz_def_t* eth_ssz_verification_type(eth_ssz_type_t type) {
  switch (type) {
    case ETH_SSZ_VERIFY_LIGHT_CLIENT_UPDATE_LIST:
      return ARRAY_TYPE(C4_REQUEST_SYNCDATA_UNION, &LIGHT_CLIENT_UPDATE_CONTAINER);
    case ETH_SSZ_VERIFY_LIGHT_CLIENT_UPDATE:
      return &LIGHT_CLIENT_UPDATE_CONTAINER;
    case ETH_SSZ_VERIFY_REQUEST:
      return &C4_REQUEST_CONTAINER;
    case ETH_SSZ_VERIFY_ACCOUNT_PROOF:
      return ARRAY_TYPE(C4_REQUEST_PROOFS_UNION, ETH_ACCOUNT_PROOF);
    case ETH_SSZ_VERIFY_TRANSACTION_PROOF:
      return ARRAY_TYPE(C4_REQUEST_PROOFS_UNION, ETH_TRANSACTION_PROOF);
    case ETH_SSZ_VERIFY_RECEIPT_PROOF:
      return ARRAY_TYPE(C4_REQUEST_PROOFS_UNION, ETH_RECEIPT_PROOF);
    case ETH_SSZ_VERIFY_LOGS_PROOF:
      return ARRAY_TYPE(C4_REQUEST_PROOFS_UNION, &ETH_LOGS_BLOCK_CONTAINER);
    case ETH_SSZ_VERIFY_CALL_PROOF:
      return ARRAY_TYPE(C4_REQUEST_PROOFS_UNION, ETH_CALL_PROOF);
    case ETH_SSZ_VERIFY_SYNC_PROOF:
      return ARRAY_TYPE(C4_REQUEST_PROOFS_UNION, ETH_SYNC_PROOF);
    case ETH_SSZ_VERIFY_BLOCK_PROOF:
      return ARRAY_TYPE(C4_REQUEST_PROOFS_UNION, ETH_BLOCK_PROOF);
    case ETH_SSZ_VERIFY_STATE_PROOF:
      return &ETH_STATE_PROOF_CONTAINER;
    case ETH_SSZ_DATA_NONE:
      return C4_REQUEST_DATA_UNION;
    case ETH_SSZ_DATA_HASH32:
      return C4_REQUEST_DATA_UNION + 1;
    case ETH_SSZ_DATA_BYTES:
      return C4_REQUEST_DATA_UNION + 2;
    case ETH_SSZ_DATA_UINT256:
      return C4_REQUEST_DATA_UNION + 3;
    case ETH_SSZ_DATA_TX:
      return C4_REQUEST_DATA_UNION + 4;
    case ETH_SSZ_DATA_RECEIPT:
      return C4_REQUEST_DATA_UNION + 5;
    case ETH_SSZ_DATA_LOGS:
      return C4_REQUEST_DATA_UNION + 6;
    case ETH_SSZ_DATA_BLOCK:
      return C4_REQUEST_DATA_UNION + 7;
    default: return NULL;
  }
}
