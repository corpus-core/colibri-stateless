#ifndef verify_h__
#define verify_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "../util/bytes.h"
#include "../util/crypto.h"
#include "../util/ssz.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
  PROOF_TYPE_BEACON_HEADER,
  PROOF_TYPE_SYNC_COMMITTEE,
} proof_type_t;

typedef struct {
  proof_type_t type;
  ssz_ob_t     proof;
  ssz_ob_t     data;
  uint64_t     first_missing_period;
  uint64_t     last_missing_period;
  bool         success;
  char*        error;
} verify_ctx_t;

void c4_verify(verify_ctx_t* ctx);
bool verify_blockhash_proof(verify_ctx_t* ctx);
#define RETURN_VERIFY_ERROR(ctx, msg)                     \
  do {                                                    \
    ctx->error   = ctx->error == NULL ? msg : ctx->error; \
    ctx->success = false;                                 \
    return false;                                         \
  } while (0)

#ifdef __cplusplus
}
#endif

#endif