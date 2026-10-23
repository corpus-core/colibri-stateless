#ifndef ZK_VERIFIER_H
#define ZK_VERIFIER_H

#include "../bn254/bn254.h"
#include "bytes.h" // from util
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define C4_ZK_PROGRAM_HASH_LEN                           32u
#define C4_ZK_VERIFICATION_OUTPUT_PROGRAM_HASH_OFFSET   136u
#define C4_ZK_VERIFICATION_OUTPUT_LEN \
  (C4_ZK_VERIFICATION_OUTPUT_PROGRAM_HASH_OFFSET + C4_ZK_PROGRAM_HASH_LEN)

/**
 * Structure representing a Groth16 Verification Key.
 */
typedef struct {
  uint8_t     program_hash[C4_ZK_PROGRAM_HASH_LEN];
  bn254_g1_t  alpha;
  bn254_g2_t  beta_neg;
  bn254_g2_t  gamma_neg;
  bn254_g2_t  delta_neg;
  size_t      ic_count;
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
 * Retrieves a registered Verification Key by its program hash, initializing the
 * compiled-in key before the lookup.
 * @param program_hash 32-byte SP1 program hash.
 * @return Pointer to the registered VK, or NULL when the hash is not pinned.
 */
const zk_vk_t* c4_zk_get_vk(const uint8_t* program_hash);

/**
 * Verifies a Groth16 Zero-Knowledge Proof for a specific Program.
 *
 * @param proof          The raw proof bytes (Groth16: A, B, C compressed).
 * @param public_inputs  Raw guest public values, hashed exactly as supplied.
 * @param program_hash   The program hash identifying the pinned Verification Key.
 * @return true if the proof is valid, false otherwise.
 */
bool c4_verify_zk_proof(bytes_t proof, bytes_t public_inputs, const uint8_t* program_hash);

/**
 * Legacy verification function using the default hardcoded VK.
 * This will lazy-initialize the default VK if not already registered.
 * The default key is the Colibri sync program: its public values must use the
 * current VerificationOutput layout and commit to that same pinned program hash.
 *
 * @param proof Raw SP1 v6 Groth16 proof.
 * @param public_inputs Serialized VerificationOutput, including the program identity.
 * @return true when the application identity and Groth16 proof both verify.
 */
bool verify_zk_proof(bytes_t proof, bytes_t public_inputs);

#ifdef __cplusplus
}
#endif

#endif // ZK_VERIFIER_H
