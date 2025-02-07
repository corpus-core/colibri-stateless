#ifndef C4_PROOFER_H
#define C4_PROOFER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../util/chains.h"
#include "../util/request.h"

typedef struct {
  char*           method;
  json_t          params;
  char*           error;
  bytes_t         proof;
  data_request_t* data_requests;
  chain_id_t      chain_id;
} proofer_ctx_t;

proofer_ctx_t*      c4_proofer_create(char* method, char* params, chain_id_t chain_id);
void                c4_proofer_free(proofer_ctx_t* ctx);
c4_proofer_status_t c4_proofer_execute(proofer_ctx_t* ctx);
c4_proofer_status_t c4_proofer_status(proofer_ctx_t* ctx);

data_request_t* c4_proofer_get_pending_data_request(proofer_ctx_t* ctx);
data_request_t* c4_proofer_get_data_request_by_id(proofer_ctx_t* ctx, bytes32_t id);
void            c4_proofer_add_data_request(proofer_ctx_t* ctx, data_request_t* data_request);

typedef enum {
  C4_SUCCESS = 0,
  C4_ERROR   = -1,
  C4_PENDING = 2
} c4_status_t;

c4_status_t c4_send_eth_rpc(proofer_ctx_t* ctx, char* method, char* params, json_t* result);
c4_status_t c4_send_beacon_json(proofer_ctx_t* ctx, char* path, char* query, json_t* result);
c4_status_t c4_send_beacon_ssz(proofer_ctx_t* ctx, char* path, char* query, bytes_t* result);

#define TRY_ASYNC(fn)                      \
  do {                                     \
    c4_status_t state = fn;                \
    if (state != C4_SUCCESS) return state; \
  } while (0)

#define TRY_ASYNC_FINAL(fn, final)         \
  do {                                     \
    c4_status_t state = fn;                \
    final;                                 \
    if (state != C4_SUCCESS) return state; \
  } while (0)

#define TRY_ASYNC_CATCH(fn, cleanup) \
  do {                               \
    c4_status_t state = fn;          \
    if (state != C4_SUCCESS) {       \
      cleanup;                       \
      return state;                  \
    }                                \
  } while (0)

#ifdef __cplusplus
}
#endif

#endif