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

bytes_t rlp_decode(bytes_t* data, int i);

#ifdef __cplusplus
}
#endif

#endif
