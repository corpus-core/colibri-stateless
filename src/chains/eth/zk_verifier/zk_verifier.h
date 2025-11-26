#ifndef ZK_VERIFIER_H
#define ZK_VERIFIER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "bytes.h" // from util

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Verifies a Groth16 Zero-Knowledge Proof for Ethereum Sync Committee updates.
 * 
 * @param proof          The raw proof bytes (Groth16: A, B, C compressed).
 * @param public_inputs  The public inputs (serialized: next_keys_root || next_period).
 * @return true if the proof is valid, false otherwise.
 */
bool verify_zk_proof(bytes_t proof, bytes_t public_inputs);

#ifdef __cplusplus
}
#endif

#endif // ZK_VERIFIER_H
