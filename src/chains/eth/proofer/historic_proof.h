#ifndef C4_HISTORIC_PROOF_H
#define C4_HISTORIC_PROOF_H

#ifdef __cplusplus
extern "C" {
#endif

#include "beacon.h"
#include "proofer.h"
#include "ssz.h"

typedef struct {
  ssz_ob_t sync_aggregate;
  bytes_t  historic_proof;
  gindex_t gindex;
  bytes_t  proof_header;
} blockroot_proof_t;

c4_status_t c4_check_historic_proof(proofer_ctx_t* ctx, blockroot_proof_t* block_proof, uint64_t slot);
void        ssz_add_blockroot_proof(ssz_builder_t* builder, beacon_block_t* block_data, blockroot_proof_t block_proof);
void        c4_free_block_proof(blockroot_proof_t* block_proof);
#ifdef __cplusplus
}
#endif

#endif