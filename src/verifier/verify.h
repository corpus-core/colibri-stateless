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

typedef uint32_t verify_flags_t;

typedef enum {
  VERIFY_FLAG_FREE_DATA = 1 << 0,
} verify_flag_t;

typedef struct {
  char*          method;
  json_t         args;
  ssz_ob_t       proof;
  ssz_ob_t       data;
  ssz_ob_t       sync_data;
  bool           success;
  c4_state_t     state;
  chain_id_t     chain_id;
  verify_flags_t flags;
} verify_ctx_t;

typedef enum {
  METHOD_UNDEFINED     = 0,
  METHOD_PROOFABLE     = 1,
  METHOD_UNPROOFABLE   = 2,
  METHOD_NOT_SUPPORTED = 3,
  METHOD_LOCAL         = 4
} method_type_t;

const ssz_def_t* c4_get_request_type(chain_type_t chain_type);
chain_type_t     c4_get_chain_type_from_req(bytes_t request);
const ssz_def_t* c4_get_req_type_from_req(bytes_t request);
c4_status_t      c4_verify(verify_ctx_t* ctx);
c4_status_t      c4_verify_from_bytes(verify_ctx_t* ctx, bytes_t request, char* method, json_t args, chain_id_t chain_id);
void             c4_verify_free_data(verify_ctx_t* ctx);
c4_status_t      c4_verify_init(verify_ctx_t* ctx, bytes_t request, char* method, json_t args, chain_id_t chain_id);
method_type_t    c4_get_method_type(chain_id_t chain_id, char* method);

#pragma endregion
#ifdef MESSAGES
#define RETURN_VERIFY_ERROR(ctx, msg)     \
  do {                                    \
    c4_state_add_error(&ctx->state, msg); \
    ctx->success = false;                 \
    return false;                         \
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