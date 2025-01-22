#include "sync_committee.h"
#include "../util/ssz.h"
#include "beacon_types.h"
#include "default_synccommittee.h"
const ssz_def_t SYNC_STATE[] = {
    SSZ_VECTOR("validators", ssz_bls_pubky, 512),
    SSZ_UINT32("period")};

const ssz_def_t SYNC_STATE_CONTAINER = SSZ_CONTAINER("SyncState", SYNC_STATE);

const c4_sync_state_t c4_get_validators(uint64_t period) {
  ssz_ob_t sync_state_ob = ssz_ob(SYNC_STATE_CONTAINER, bytes((uint8_t*) default_synccommittee, default_synccommittee_len));
  uint32_t last_period   = ssz_get_uint32(&sync_state_ob, "period");
  return (c4_sync_state_t) {
      .current_period = period,
      .last_period    = last_period,
      .validators     = last_period != period ? NULL_BYTES : ssz_get(&sync_state_ob, "validators").bytes};
}
