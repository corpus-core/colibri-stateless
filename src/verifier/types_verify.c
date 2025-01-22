#include "../util/bytes.h"
#include "../util/crypto.h"
#include "../util/ssz.h"
#include "types_beacon.h"
#include <stdio.h>
#include <stdlib.h>

// the block hash proof is used as part of different other types since it contains all relevant
// proofs to validate the blockhash of the execution layer
const ssz_def_t BLOCK_HASH_PROOF[] = {
    SSZ_LIST("blockhash_proof", ssz_bytes32, 256),    // the merkle prooof from the executionPayload.blockhash down to the blockBodyRoot hash
    SSZ_CONTAINER("header", BEACON_BLOCK_HEADER),     // the header of the beacon block
    SSZ_BIT_VECTOR("sync_committee_bits", 512),       // the bits of the validators that signed the block
    SSZ_BYTE_VECTOR("sync_committee_signature", 96)}; // the signature of the sync committee

const ssz_def_t LIGHT_CLIENT_UPDATE_CONTAINER = SSZ_CONTAINER("LightClientUpdate", LIGHT_CLIENT_UPDATE);

// A List of possible types of data matching the Proofs
const ssz_def_t C4_REQUEST_DATA_UNION[] = {
    SSZ_NONE,
    SSZ_BYTES32("blockhash")};

// A List of possible types of proofs matching the Data
const ssz_def_t C4_REQUEST_PROOFS_UNION[] = {
    SSZ_NONE,
    SSZ_CONTAINER("BlockHashProof", BLOCK_HASH_PROOF)};

// A List of possible types of sync data used to update the sync state by verifying the transition from the last period to the required.
const ssz_def_t C4_REQUEST_SYNCDATA_UNION[] = {
    SSZ_NONE,
    SSZ_LIST("LightClientUpdate", LIGHT_CLIENT_UPDATE_CONTAINER, 512)}; // this light client update can be fetched directly from the beacon chain API

// the main container defining the incoming data processed by the verifier
const ssz_def_t C4_REQUEST[] = {
    SSZ_UNION("data", C4_REQUEST_DATA_UNION),           // the data to proof
    SSZ_UNION("proof", C4_REQUEST_PROOFS_UNION),        // the proof of the data
    SSZ_UNION("sync_data", C4_REQUEST_SYNCDATA_UNION)}; // the sync data containing proofs for the transition between the two periods

const ssz_def_t C4_REQUEST_CONTAINER = SSZ_CONTAINER("C4Request", C4_REQUEST);
