#ifndef crypto_h__
#define crypto_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "bytes.h"
#include <stdbool.h>
#include <stdint.h>

typedef uint8_t address_t[20];
typedef uint8_t bytes32_t[32];
typedef uint8_t bls_pubkey_t[48];
typedef uint8_t bls_signature_t[96];
void            sha256(bytes_t data, uint8_t* out);
void            sha256_merkle(bytes_t data1, bytes_t data2, uint8_t* out);

bool blst_verify(bytes32_t       message,         /**< 32 bytes hashed message */
                 bls_signature_t signature,       /**< 96 bytes signature */
                 uint8_t*        public_keys,     /**< 48 bytes public key array */
                 int             num_public_keys, /**< number of public keys */
                 bytes_t         pibkey_bitmask);         /**< num_public_keys.len = num_public_keys/8 and indicates with the bits set which of the public keys are part of the signature */

#ifdef __cplusplus
}
#endif

#endif