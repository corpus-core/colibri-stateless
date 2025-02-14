#ifndef proofer_ssz_types_beacon_h__
#define proofer_ssz_types_beacon_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "../util/bytes.h"
#include "../util/crypto.h"
#include "../util/ssz.h"
#include <stdio.h>
#include <stdlib.h>

// the beacon block is a list of slot, proposer index, parent root, state root and body
extern const ssz_def_t BEACON_BLOCK[5];
// a signed beacon block is a beacon block and a signature
extern const ssz_def_t SIGNED_BEACON_BLOCK[2];
// a container for a signed beacon block
extern const ssz_def_t SIGNED_BEACON_BLOCK_CONTAINER;
// a container for the body of a beacon block
extern const ssz_def_t BEACON_BLOCK_BODY_CONTAINER;

#ifdef __cplusplus
}
#endif

#endif