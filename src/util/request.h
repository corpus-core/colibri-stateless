#ifndef C4_REQUEST_H
#define C4_REQUEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "bytes.h"
#include "crypto.h"
#include "json.h"

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

#ifdef __cplusplus
}
#endif

#endif
