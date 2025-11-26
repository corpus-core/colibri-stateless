#ifndef ZK_VERIFIER_H
#define ZK_VERIFIER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Verifies an SP1 Zero-Knowledge Proof (Groth16/BN254)
 * 
 * @param proof_bytes Pointer to the serialized proof
 * @param proof_len Length of the proof
 * @param public_inputs Pointer to the public inputs (if any)
 * @param inputs_len Length of the public inputs
 * @return true if verification succeeds
 * @return false if verification fails
 */
bool verify_zk_proof(const uint8_t* proof_bytes, size_t proof_len, const uint8_t* public_inputs, size_t inputs_len);

#ifdef __cplusplus
}
#endif

#endif // ZK_VERIFIER_H


