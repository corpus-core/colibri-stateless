#![no_main]
sp1_zkvm::entrypoint!(main);

mod bls;

use eth_sync_common::{VerificationOutput, SP1GuestInput};
use eth_sync_common::merkle::{create_root_hash, verify_merkle_proof, verify_slot};
use bls::verify_signature;
use sha2::{Sha256, Digest};

pub fn main() {
    let input = sp1_zkvm::io::read::<SP1GuestInput>();
    let proof_data = input.proof_data;
    
    // Initialize output current keys with the current step's input (Default: Step-by-step)
    let mut output_current_keys_root = proof_data.current_keys_root;
    
    // Recursion Check
    if let Some(rec_data) = input.recursion_data {
        // 1. Verify previous proof
        sp1_zkvm::lib::verify::verify_sp1_proof(&rec_data.vkey_hash, &rec_data.public_values_digest);
        
        // 2. Verify that public_values match the digest
        let mut hasher = Sha256::new();
        hasher.update(&rec_data.public_values);
        let hash_result = hasher.finalize();
        
        // SP1 digest format is [u8; 32]
        let calculated_digest: [u8; 32] = hash_result.into();
        
        if calculated_digest != rec_data.public_values_digest {
             panic!("Public Values Hash Mismatch");
        }
        
        // 3. Deserialize previous output
        let prev_output: VerificationOutput = bincode::deserialize(&rec_data.public_values).expect("Failed to deserialize prev public values");
        
        // AGGREGATION LOGIC:
        // If we are verifying recursively, our "start state" is the start state of the previous proof.
        // This allows us to prove a chain A->B->C with a single proof that says "A->C".
        output_current_keys_root = prev_output.current_keys_root;
        
        // 4. Chain Continuity Checks
        // Calculate current period from slot
        let current_period = u64::from_le_bytes(proof_data.slot_bytes) >> 13;
        
        // Check Period Continuity
        // The previous proof output 'next_period'. This must be our current period.
        if prev_output.next_period != current_period {
             panic!("Period mismatch: Prev target {} != Current {}", prev_output.next_period, current_period);
        }
        
        // Check Key Continuity
        if prev_output.next_keys_root != proof_data.current_keys_root {
             panic!("Key mismatch: Chain broken");
        }
    }
    
    // 1. Verify period
    let period = u64::from_le_bytes(proof_data.slot_bytes) >> 13;
    if period != proof_data.next_period - 1 {
        panic!("Invalid period: {} vs {}", period, proof_data.next_period - 1);
    }
    
    // 2. Verify slot
    if proof_data.proof.len() < 96 {
        panic!("Proof too short");
    }
    let proof_slice_start = proof_data.proof.len() - 96;
    let proof_element: [u8; 32] = proof_data.proof[proof_slice_start..proof_slice_start+32].try_into().unwrap();
    
    if !verify_slot(&proof_data.slot_bytes, &proof_data.proposer_bytes, &proof_element) {
         panic!("Invalid slot verification");
    }
    
    // 3. Verify current keys root hash
    let mut calculated_root = [0u8; 32];
    create_root_hash(&proof_data.current_keys, &mut calculated_root);
    if calculated_root != proof_data.current_keys_root {
        panic!("Invalid current keys root");
    }
    
    // 4. Verify new key root hash -> Merkle Proof -> Signing Root
    let mut root = proof_data.next_keys_root; 
    verify_merkle_proof(&proof_data.proof, proof_data.gidx, &mut root);
    
    // 5. Verify signature
    let signature_array: [u8; 96] = proof_data.signature.as_slice().try_into().expect("Signature must be 96 bytes");
    let signature_bits_array: [u8; 64] = proof_data.signature_bits.as_slice().try_into().expect("Signature bits must be 64 bytes");
    
    if !verify_signature(&root, 
                         &signature_array, 
                         &proof_data.current_keys, 
                         &signature_bits_array) {
        panic!("Invalid signature verification");
    }
    
    let output = VerificationOutput {
        current_keys_root: output_current_keys_root, // Use aggregated start or current start
        next_keys_root: proof_data.next_keys_root,
        next_period: proof_data.next_period,
    };
    
    sp1_zkvm::io::commit(&output);
}
