#ifndef types_verify_h__
#define types_verify_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "../util/bytes.h"
#include "../util/crypto.h"
#include "../util/ssz.h"
#include <stdio.h>
#include <stdlib.h>

extern const ssz_def_t LIGHT_CLIENT_UPDATE_CONTAINER;
extern const ssz_def_t BLOCK_HASH_PROOF[4];
extern const ssz_def_t ETH_ACCOUNT_PROOF[8];
extern const ssz_def_t ETH_TRANSACTION_PROOF[8];
extern const ssz_def_t ETH_RECEIPT_PROOF[9];
extern const ssz_def_t C4_REQUEST_DATA_UNION[6];
extern const ssz_def_t C4_REQUEST_PROOFS_UNION[6];
extern const ssz_def_t C4_REQUEST_SYNCDATA_UNION[2];
extern const ssz_def_t C4_REQUEST[];

extern const ssz_def_t C4_REQUEST_CONTAINER;
extern const ssz_def_t ETH_ACCOUNT_PROOF_CONTAINER;
extern const ssz_def_t ETH_TRANSACTION_PROOF_CONTAINER;
extern const ssz_def_t ssz_transactions_bytes;
// extern const ssz_def_t BLOCK_HASH_PROOF_CONTAINER;
extern const ssz_def_t ETH_LOGS_BLOCK_CONTAINER;
extern const ssz_def_t ETH_LOGS_TX_CONTAINER;
extern const ssz_def_t ETH_STATE_PROOF_CONTAINER;
#ifdef __cplusplus
}
#endif

#endif