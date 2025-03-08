#include "beacon_types.h"
#include "ssz.h"
#include "types_beacon.h"
#include "types_verify.h"

#include "beacon_denep.c"

const ssz_def_t* eth_ssz_type_for_fork(eth_ssz_type_t type, fork_id_t fork) {
  switch (type) {
    case ETH_SSZ_SIGNED_BEACON_BLOCK_CONTAINER:
      return &SIGNED_BEACON_BLOCK_CONTAINER;
    case ETH_SSZ_BEACON_BLOCK_BODY_CONTAINER:
      return &BEACON_BLOCK_BODY_CONTAINER;
    default:
      return NULL;
  }
}
