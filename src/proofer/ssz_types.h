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

extern const ssz_def_t BEACON_BLOCK[5];
extern const ssz_def_t SIGNED_BEACON_BLOCK[2];
extern const ssz_def_t SIGNED_BEACON_BLOCK_CONTAINER;

#ifdef __cplusplus
}
#endif

#endif