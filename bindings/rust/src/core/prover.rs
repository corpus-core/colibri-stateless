//! Safe wrapper around `c4_create_prover_ctx` / `c4_prover_*`.
//!
//! The prover context runs the state machine that turns an RPC request
//! into a cryptographic proof. Typical usage:
//!
//! ```no_run
//! use colibri_stateless::core::Prover;
//! use colibri_stateless::types::Status;
//!
//! # fn main() -> Result<(), colibri_stateless::ColibriError> {
//! let mut prover = Prover::new("eth_getBalance", r#"["0x0", "latest"]"#, 1, 0)?;
//! loop {
//!     let json = prover.execute_json_status()?;
//!     match Status::parse(&json)? {
//!         Status::Pending { requests: _ } => {
//!             // fetch external data and call req_set_response / req_set_error
//!             # break;
//!         }
//!         Status::Success { .. } => {
//!             let _proof = prover.get_proof();
//!             break;
//!         }
//!         Status::Revert { .. } => unreachable!("prover cannot revert"),
//!         Status::Error { message } => return Err(colibri_stateless::ColibriError::Ffi(message)),
//!     }
//! }
//! # Ok(()) }
//! ```

use crate::ffi;
use crate::types::{ColibriError, ProofError};
use std::ffi::{CStr, CString};

use super::helpers::bytes_to_vec;

/// RAII wrapper around a `prover_t*`.
pub struct Prover {
    ctx: *mut ffi::prover_t,
}

impl Prover {
    /// Create a new prover context. `params` must be a valid JSON array
    /// string (e.g. `r#"["0x0", "latest"]"#`).
    pub fn new(
        method: &str,
        params: &str,
        chain_id: u64,
        flags: u32,
    ) -> Result<Self, ColibriError> {
        let c_method = CString::new(method)?;
        let c_params = CString::new(params)?;
        let ctx = unsafe {
            ffi::c4_create_prover_ctx(
                c_method.as_ptr() as *mut _,
                c_params.as_ptr() as *mut _,
                chain_id,
                flags,
            )
        };
        if ctx.is_null() {
            return Err(ProofError::ContextCreation(format!(
                "failed to create prover context for '{method}'"
            ))
            .into());
        }
        Ok(Self { ctx })
    }

    /// Advance the state machine one step. See [`crate::types::Status`]
    /// for the shape of the returned JSON.
    pub fn execute_json_status(&mut self) -> Result<String, ColibriError> {
        unsafe {
            let ptr = ffi::c4_prover_execute_json_status(self.ctx);
            if ptr.is_null() {
                return Err(ProofError::Generation(
                    "prover_execute_json_status returned NULL".to_string(),
                )
                .into());
            }
            // Copy into an owned String, then free the C-side buffer.
            let s = CStr::from_ptr(ptr).to_string_lossy().into_owned();
            libc::free(ptr as *mut libc::c_void);
            Ok(s)
        }
    }

    /// Copy the proof out of the context. Only meaningful after
    /// [`execute_json_status`] reported `"status": "success"`.
    pub fn get_proof(&mut self) -> Result<Vec<u8>, ColibriError> {
        let bytes = unsafe { ffi::c4_prover_get_proof(self.ctx) };
        let vec = bytes_to_vec(bytes);
        if vec.is_empty() {
            Err(ProofError::InvalidData("generated proof is empty".into()).into())
        } else {
            Ok(vec)
        }
    }
}

impl Drop for Prover {
    fn drop(&mut self) {
        if !self.ctx.is_null() {
            unsafe { ffi::c4_free_prover_ctx(self.ctx) };
            self.ctx = std::ptr::null_mut();
        }
    }
}

// `prover_t` is opaque; the C core owns the internals. The context is
// not currently safe for concurrent execution from multiple threads,
// but moving it between threads is fine.
unsafe impl Send for Prover {}
