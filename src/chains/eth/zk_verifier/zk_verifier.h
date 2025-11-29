#ifndef ZK_VERIFIER_H
#define ZK_VERIFIER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "bytes.h" // from util
#include "../bn254/bn254.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Structure representing a Groth16 Verification Key.
 */
typedef struct {
    uint8_t program_hash[32];
    bn254_g1_t alpha;
    bn254_g2_t beta_neg;
    bn254_g2_t gamma_neg;
    bn254_g2_t delta_neg;
    size_t ic_count;
    bn254_g1_t* ic;
} zk_vk_t;

/**
 * Registers a Verification Key globally.
 * Note: The vk pointer and its content (ic array) must remain valid (e.g. static or heap allocated).
 * The registry does not deep copy the vk struct itself, but might link it.
 * Actually, deep copying into an internal registry is safer if we expect stack usage.
 * But usually VKs are static constants. Let's assume the caller manages lifetime or we copy.
 * For simplicity given C constraints, let's assume we copy the struct into a linked list node.
 */
void c4_zk_register_vk(const zk_vk_t* vk);

/**
 * Retrieves a Verification Key by its program hash.
 * @param program_hash 32-byte hash
 * @return Pointer to VK or NULL if not found.
 */
const zk_vk_t* c4_zk_get_vk(const uint8_t* program_hash);

/**
 * Verifies a Groth16 Zero-Knowledge Proof for a specific Program.
 * 
 * @param proof          The raw proof bytes (Groth16: A, B, C compressed).
 * @param public_inputs  The public inputs (serialized).
 * @param program_hash   The program hash identifying the Verification Key.
 * @return true if the proof is valid, false otherwise.
 */
bool c4_verify_zk_proof(bytes_t proof, bytes_t public_inputs, const uint8_t* program_hash);

/**
 * Legacy verification function using the default hardcoded VK.
 * This will lazy-initialize the default VK if not already registered.
 */
bool verify_zk_proof(bytes_t proof, bytes_t public_inputs);

#ifdef __cplusplus
}
#endif

#endif // ZK_VERIFIER_H
