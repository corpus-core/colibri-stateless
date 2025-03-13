#ifndef verify_h__
#define verify_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "../util/bytes.h"
#include "../util/chains.h"
#include "../util/crypto.h"
#include "../util/json.h"
#include "../util/ssz.h"
#include "../util/state.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
  char*      method;
  json_t     args;
  ssz_ob_t   proof;
  ssz_ob_t   data;
  ssz_ob_t   sync_data;
  uint64_t   first_missing_period;
  uint64_t   last_missing_period;
  bool       success;
  c4_state_t state;
  chain_id_t chain_id;
} verify_ctx_t;

const ssz_def_t* c4_get_request_type(chain_type_t chain_type);
chain_type_t     c4_get_chain_type_from_req(bytes_t request);
const ssz_def_t* c4_get_req_type_from_req(bytes_t request);
void             c4_verify(verify_ctx_t* ctx);
void             c4_verify_from_bytes(verify_ctx_t* ctx, bytes_t request, char* method, json_t args, chain_id_t chain_id);

#pragma endregion
#ifdef MESSAGES
#define RETURN_VERIFY_ERROR(ctx, msg)                                                                           \
  do {                                                                                                          \
    ctx->state.error = ctx->state.error == NULL ? strdup(msg) : bprintf(NULL, "%s\n%s", ctx->state.error, msg); \
    ctx->success     = false;                                                                                   \
    return false;                                                                                               \
  } while (0)
#else
#define RETURN_VERIFY_ERROR(ctx, msg) \
  do {                                \
    ctx->state.error = "E";           \
    return false;                     \
  } while (0)
#endif

#ifdef __cplusplus
}
#endif

#endif