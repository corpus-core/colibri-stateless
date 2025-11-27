#![no_main]
sp1_zkvm::entrypoint!(main);

mod bls;

use eth_sync_common::{VerificationOutput, SP1GuestInput};
use eth_sync_common::merkle::{create_root_hash, verify_merkle_proof, verify_slot};
use bls::verify_signature;
use sha2::{Sha256, Digest};

/// The main entrypoint for the SP1 Guest Program.
///
/// This program verifies an Ethereum Sync Committee Light Client Update.
/// It supports two modes:
/// 1. **Direct Verification**: Verifies a single period transition (Period N -> Period N+1).
/// 2. **Recursive Verification**: Verifies a previous SP1 proof (covering Anchor -> Period N)
///    AND the current transition (Period N -> Period N+1), outputting an aggregated proof
///    (Anchor -> Period N+1).
pub fn main() {
    // Read the input structure from the host
    let input = sp1_zkvm::io::read::<SP1GuestInput>();
    let proof_data = input.proof_data;
    
    // Initialize output current keys with the current step's input (Default: Step-by-step)
    // If recursion is active, this will be overwritten by the previous proof's start root.
    let mut output_current_keys_root = proof_data.current_keys_root;
    
    // --- RECURSION LOGIC ---
    if let Some(rec_data) = input.recursion_data {
        // 1. Verify the previous SP1 proof
        // This syscall checks that 'rec_data.vkey_hash' (the program that ran)
        // and 'rec_data.public_values_digest' (the output it claimed) are valid.
        sp1_zkvm::lib::verify::verify_sp1_proof(&rec_data.vkey_hash, &rec_data.public_values_digest);
        
        // 2. Verify that the provided full 'public_values' match the verified digest
        let mut hasher = Sha256::new();
        hasher.update(&rec_data.public_values);
        let hash_result = hasher.finalize();
        let calculated_digest: [u8; 32] = hash_result.into();
        
        if calculated_digest != rec_data.public_values_digest {
             panic!("Public Values Hash Mismatch");
        }
        
        // 3. Deserialize the previous proof's output
        let prev_output: VerificationOutput = bincode::deserialize(&rec_data.public_values)
            .expect("Failed to deserialize prev public values");
        
        // AGGREGATION LOGIC:
        // If we are verifying recursively, our "start state" is the start state of the previous proof.
        // This allows us to prove a chain A->B->C with a single proof that attests "A->C".
        output_current_keys_root = prev_output.current_keys_root;
        
        // 4. Chain Continuity Checks
        
        // Calculate current period from the slot in the header
        // Slot is divided by 32 (slots per epoch) * 256 (epochs per period) = 8192
        let current_period = u64::from_le_bytes(proof_data.slot_bytes) >> 13; // / 8192
        
        // Check Period Continuity: The previous proof must have output the period we are currently in.
        if prev_output.next_period != current_period {
             panic!("Period mismatch: Prev target {} != Current {}", prev_output.next_period, current_period);
        }
        
        // Check Key Continuity: The 'next_keys' from the previous proof must match our 'current_keys'.
        if prev_output.next_keys_root != proof_data.current_keys_root {
             panic!("Key mismatch: Chain broken");
        }
    }
    
    // --- CORE VERIFICATION LOGIC ---

    // 1. Verify period consistency
    // The slot must belong to the period immediately preceding 'next_period'.
    let period = u64::from_le_bytes(proof_data.slot_bytes) >> 13;
    if period != proof_data.next_period - 1 {
        panic!("Invalid period: {} vs {}", period, proof_data.next_period - 1);
    }
    
    // 2. Verify slot and proposer index
    // The Merkle proof must connect the BeaconHeader to the state root.
    // The BeaconHeader starts with [slot, proposer_index, ...].
    // We verify that hash(slot, proposer) matches the first node in the provided proof branch.
    if proof_data.proof.len() < 96 {
        panic!("Proof too short");
    }
    // Extract the relevant proof node (at the correct depth/position)
    let proof_slice_start = proof_data.proof.len() - 96;
    let proof_element: [u8; 32] = proof_data.proof[proof_slice_start..proof_slice_start+32]
        .try_into().unwrap();
    
    if !verify_slot(&proof_data.slot_bytes, &proof_data.proposer_bytes, &proof_element) {
         panic!("Invalid slot verification");
    }
    
    // 3. Verify current keys root hash
    // Reconstruct the SSZ root of the provided 512 keys and ensure it matches the expected root.
    // This proves that the keys we use for signature verification are indeed the trusted committee.
    let mut calculated_root = [0u8; 32];
    create_root_hash(&proof_data.current_keys, &mut calculated_root);
    if calculated_root != proof_data.current_keys_root {
        panic!("Invalid current keys root");
    }
    
    // 4. Verify Merkle Proof: NextKeysRoot -> SigningRoot
    // 'root' starts as the leaf (next_keys_root) and is hashed up the tree using the 'proof'.
    // The result must match the SigningRoot (which acts as the message for the BLS signature).
    let mut root = proof_data.next_keys_root; 
    verify_merkle_proof(&proof_data.proof, proof_data.gidx, &mut root);
    
    // 5. Verify BLS Signature
    // Checks that the aggregated signature is valid for the 'root' (message)
    // and the subset of 'current_keys' indicated by 'signature_bits'.
    let signature_array: [u8; 96] = proof_data.signature.as_slice().try_into().expect("Signature must be 96 bytes");
    let signature_bits_array: [u8; 64] = proof_data.signature_bits.as_slice().try_into().expect("Signature bits must be 64 bytes");
    
    if !verify_signature(&root, 
                         &signature_array, 
                         &proof_data.current_keys, 
                         &signature_bits_array) {
        panic!("Invalid signature verification");
    }
    
    // --- OUTPUT COMMITMENT ---
    
    let output = VerificationOutput {
        current_keys_root: output_current_keys_root, // Aggregated start (A) or Current start (N)
        next_keys_root: proof_data.next_keys_root,   // New End (N+1)
        next_period: proof_data.next_period,         // Period Number
    };
    
    sp1_zkvm::io::commit(&output);
}
