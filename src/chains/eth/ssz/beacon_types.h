#ifndef ETH_SSZ_TYPES_H
#define ETH_SSZ_TYPES_H

#include "chains.h"
#include "ssz.h"

typedef enum {
  ETH_SSZ_SIGNED_BEACON_BLOCK_CONTAINER = 1,
  ETH_SSZ_BEACON_BLOCK_BODY_CONTAINER   = 2,
} eth_ssz_type_t;

const ssz_def_t* eth_ssz_type_for_fork(eth_ssz_type_t type, fork_id_t fork);
#endif
