#ifndef C4_BEACON_H
#define C4_BEACON_H

#include "../util/json.h"
#include "proofer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint64_t slot;
  uint64_t propert_index;
  json_t   header;
  json_t   execution;
  json_t   body;
  uint8_t  signature[96];
  uint8_t  signature_bits[64];
} beacon_block_t;

void c4_proof_account(proofer_ctx_t* ctx);

#ifdef __cplusplus
}
#endif

#endif