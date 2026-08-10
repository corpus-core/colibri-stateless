//! Safe wrapper around `c4_verify_create_ctx` / `c4_verify_*`.

use crate::ffi;
use crate::types::{ColibriError, VerificationError};
use std::ffi::{c_void, CStr, CString};
use std::ptr;

use super::helpers::slice_as_bytes;

/// RAII wrapper around a verifier context (`void*` in C, opaque to us).
pub struct Verifier {
    ctx: *mut c_void,
}

impl Verifier {
    /// Create a verifier context.
    ///
    /// * `proof` -- the SSZ-encoded proof bytes to verify.
    /// * `args` -- JSON array string of the original RPC parameters.
    /// * `trusted_checkpoint` -- optional `0x`-prefixed 66-char hex
    ///   string. When `None`, the C core keeps the previously configured
    ///   checkpoint for `chain_id`.
    /// * `verify_flags` -- bitmask of `verify_flags_t` values (e.g. PAP).
    pub fn new(
        proof: &[u8],
        method: &str,
        args: &str,
        chain_id: u64,
        trusted_checkpoint: Option<&str>,
        verify_flags: u32,
    ) -> Result<Self, ColibriError> {
        let c_method = CString::new(method)?;
        let c_args = CString::new(args)?;
        let c_checkpoint = trusted_checkpoint.map(CString::new).transpose()?;
        let ctx = unsafe {
            ffi::c4_verify_create_ctx(
                slice_as_bytes(proof),
                c_method.as_ptr() as *mut _,
                c_args.as_ptr() as *mut _,
                chain_id,
                c_checkpoint
                    .as_ref()
                    .map(|s| s.as_ptr() as *mut _)
                    .unwrap_or(ptr::null_mut()),
                verify_flags,
            )
        };
        if ctx.is_null() {
            return Err(VerificationError::ContextCreation(format!(
                "failed to create verifier context for '{method}'"
            ))
            .into());
        }
        Ok(Self { ctx })
    }

    /// Set the lower bound (Unix seconds) for `block.timestamp` accepted
    /// when the proof references the `"latest"` tag. `0` disables the
    /// check.
    pub fn set_min_latest_block_ts(&mut self, ts: u64) {
        unsafe { ffi::c4_verify_set_min_latest_block_ts(self.ctx, ts) };
    }

    /// Advance the verifier state machine one step. See
    /// [`crate::types::Status`] for the shape of the returned JSON.
    pub fn execute_json_status(&mut self) -> Result<String, ColibriError> {
        unsafe {
            let ptr = ffi::c4_verify_execute_json_status(self.ctx);
            if ptr.is_null() {
                return Err(VerificationError::Failed(
                    "verify_execute_json_status returned NULL".to_string(),
                )
                .into());
            }
            let s = CStr::from_ptr(ptr).to_string_lossy().into_owned();
            libc::free(ptr as *mut libc::c_void);
            Ok(s)
        }
    }
}

impl Drop for Verifier {
    fn drop(&mut self) {
        if !self.ctx.is_null() {
            unsafe { ffi::c4_verify_free_ctx(self.ctx) };
            self.ctx = ptr::null_mut();
        }
    }
}

unsafe impl Send for Verifier {}
