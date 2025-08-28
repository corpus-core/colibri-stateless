#include "witness.h"

const ssz_def_t C4_BLOCK_HASH_WITNESS[3] = {
    SSZ_UINT64("chainId"),     // the chainId
    SSZ_UINT64("blockNumber"), // blocknumber
    SSZ_BYTES32("blockHash")}; // the blockhash seen

static const ssz_def_t C4_WITNESS_UNION[] = {
    SSZ_CONTAINER(C4_BLOCK_HASH_WITNESS_ID, C4_BLOCK_HASH_WITNESS), // the blockhash
};

const ssz_def_t C4_WITNESS_CONTAINER[2] = {
    SSZ_UNION("data", C4_WITNESS_UNION), // the data seen
    SSZ_BYTE_VECTOR("signature", 65),    // the signature of the witness
};
