#![no_main]
sp1_zkvm::entrypoint!(main);

mod bls;

use eth_sync_common::{ProofData, VerificationOutput};
use eth_sync_common::merkle::{create_root_hash, verify_merkle_proof, verify_slot};
use bls::verify_signature;

pub fn main() {
    let proof_data = sp1_zkvm::io::read::<ProofData>();
    
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
        current_keys_root: proof_data.current_keys_root,
        next_keys_root: proof_data.next_keys_root,
        next_period: proof_data.next_period,
    };
    
    sp1_zkvm::io::commit(&output);
}
