#ifndef C4_PROOFER_H
#define C4_PROOFER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../util/bytes.h"
#include "../util/crypto.h"
#include "../util/json.h"

typedef enum {
  C4_DATA_TYPE_BEACON_API = 0,
  C4_DATA_TYPE_ETH_RPC    = 1,
  C4_DATA_TYPE_REST_API   = 2
} data_request_type_t;

typedef enum {
  C4_DATA_ENCODING_JSON = 0,
  C4_DATA_ENCODING_SSZ  = 1
} data_request_encoding_t;

typedef enum {
  C4_DATA_METHOD_GET    = 0,
  C4_DATA_METHOD_POST   = 1,
  C4_DATA_METHOD_PUT    = 2,
  C4_DATA_METHOD_DELETE = 3
} data_request_method_t;
typedef enum {
  C4_PROOFER_PENDING = 0,
  C4_PROOFER_WAITING = 1,
  C4_PROOFER_SUCCESS = 2,
  C4_PROOFER_ERROR   = 3
} c4_proofer_status_t;

typedef struct data_request {
  data_request_type_t     type;
  data_request_encoding_t encoding;
  char*                   url;
  data_request_method_t   method;
  bytes_t                 payload;
  bytes_t                 response;
  char*                   error;
  struct data_request*    next;
  bytes32_t               id;
} data_request_t;

typedef struct {
  char*           method;
  json_t          params;
  char*           error;
  bytes_t         proof;
  data_request_t* data_requests;
} proofer_ctx_t;

proofer_ctx_t*      c4_proofer_create(char* method, char* params);
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