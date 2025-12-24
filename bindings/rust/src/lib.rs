mod ffi;
pub mod helpers;
mod prover;
mod verifier;
mod client;
pub mod types;

pub use prover::Prover;
pub use verifier::Verifier;
pub use client::ProverClient;
pub use types::{ColibriError, Result};

use std::ffi::CString;

// Helper function to check method support
pub fn get_method_support(chain_id: u64, method: &str) -> Result<i32> {
    let c_method = CString::new(method)?;

    let support = unsafe {
        ffi::c4_get_method_support(chain_id, c_method.as_ptr() as *mut i8)
    };

    Ok(support)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_method_support() {
        // Test eth_getBalance support
        let support = get_method_support(1, "eth_getBalance").unwrap();
        assert!(support >= 0);
    }
}