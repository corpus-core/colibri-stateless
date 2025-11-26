use serde::{Deserialize, Serialize};

pub mod merkle;

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct ProofData {
    pub current_keys_root: [u8; 32],
    pub next_keys_root: [u8; 32],
    pub next_period: u64,
    pub current_keys: Vec<u8>,
    pub signature_bits: Vec<u8>,
    pub signature: Vec<u8>,
    pub slot_bytes: [u8; 8],
    pub proposer_bytes: [u8; 8],
    pub proof: Vec<u8>,
    pub gidx: u32,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct VerificationOutput {
    pub current_keys_root: [u8; 32],
    pub next_keys_root: [u8; 32],
    pub next_period: u64,
}

// Extension trait to help with slice copying in merkle.rs if needed, 
// or just use standard copy_from_slice
pub trait SliceHelper {
    fn copy_from_slice_safe(&mut self, src: &[u8]);
}

impl SliceHelper for [u8] {
    fn copy_from_slice_safe(&mut self, src: &[u8]) {
        if src.len() != self.len() {
            panic!("Slice length mismatch");
        }
        self.copy_from_slice(src);
    }
}
