#ifndef rlp_h__
#define rlp_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "bytes.h"
#include "crypto.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
  RLP_SUCCESS      = 0,
  RLP_OUT_OF_RANGE = -1,
  RLP_NOT_FOUND    = -2,
  RLP_ITEM         = 1,
  RLP_LIST         = 2
} rlp_type_t;

rlp_type_t rlp_decode(bytes_t* data, int index, bytes_t* target);

#ifdef __cplusplus
}
#endif

#endif
