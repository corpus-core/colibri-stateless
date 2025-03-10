#include "beacon_types.h"
#include "ssz.h"

const ssz_def_t* eth_ssz_type_for_fork(eth_ssz_type_t type, fork_id_t fork) {
  switch (fork) {
    case C4_FORK_DENEB: return eth_ssz_type_for_denep(type);
    case C4_FORK_ELECTRA: return eth_ssz_type_for_electra(type);
    default: return NULL;
  }
}
