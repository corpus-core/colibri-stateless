#include "../util/bytes.h"
#include "../util/crypto.h"
#include "../util/ssz.h"
#include <stdio.h>
#include <stdlib.h>

const ssz_def_t BEACON_BLOCK_HEADER[] = {
    SSZ_UINT64("slot"),
    SSZ_UINT64("proposerIndex"),
    SSZ_BYTES32("parentRoot"),
    SSZ_BYTES32("stateRoot"),
    SSZ_BYTES32("bodyRoot")};

const ssz_def_t BEACON_BLOCK_HEADER_CONTAINER = SSZ_CONTAINER("BeaconBlockHeader", BEACON_BLOCK_HEADER);

const ssz_def_t BLOCK_HASH_PROOF[] = {
    SSZ_LIST("blockhash_proof", ssz_bytes32, 256),
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER),
    SSZ_BIT_VECTOR("sync_committee_bits", 512),
    SSZ_BYTE_VECTOR("sync_committee_signature", 96)};

const ssz_def_t BLOCK_HASH_PROOF_CONTAINER = SSZ_CONTAINER("BlockHashProof", BLOCK_HASH_PROOF);

const ssz_def_t C4_PROOFS[] = {
    SSZ_NONE,
    SSZ_CONTAINER("BlockHashProof", BLOCK_HASH_PROOF)};

const ssz_def_t C4_PROOFS_CONTAINER = SSZ_UNION("C4Proofs", C4_PROOFS);