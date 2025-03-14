#ifndef PRECOMPILES_H
#define PRECOMPILES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "bytes.h"
#include "crypto.h"
#include "json.h"
#include "state.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
  PRE_SUCCESS         = 0,
  PRE_ERROR           = 1,
  PRE_OUT_OF_BOUNDS   = 2,
  PRE_INVALID_INPUT   = 3,
  PRE_INVALID_ADDRESS = 4,
  PRE_NOT_SUPPORTED   = 5,
} pre_result_t;

// Main precompile execution function
pre_result_t eth_execute_precompile(const uint8_t* address, const bytes_t input, buffer_t* output, uint64_t* gas_used);

#ifdef __cplusplus
}
#endif

#endif