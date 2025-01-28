#ifndef patricia_h__
#define patricia_h__

#include "bytes.h"
#include "ssz.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int patricia_verify(bytes32_t root, bytes_t* p, ssz_ob_t proof, bytes_t* expected);

uint8_t* patricia_to_nibbles(bytes_t path, bool use_prefix);

int patricia_match_nibbles(uint8_t* a, uint8_t* b);

#ifdef __cplusplus
}
#endif

#endif
