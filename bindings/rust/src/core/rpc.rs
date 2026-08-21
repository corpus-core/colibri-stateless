//! Safe wrapper around `c4_create_rpc_ctx` and friends -- the "unified
//! RPC" entry point that combines proof generation and verification in
//! one context.

use crate::ffi;
use crate::types::{ColibriError, ProverMode};
use std::ffi::{c_void, CStr, CString};
use std::ptr;

/// RAII wrapper around an RPC context (`void*` in C).
pub struct RpcCtx {
    ctx: *mut c_void,
}

impl RpcCtx {
    /// Create an RPC context.
    pub fn new(
        method: &str,
        params: &str,
        chain_id: u64,
        prover_flags: u32,
        verify_flags: u32,
        prover_mode: ProverMode,
    ) -> Result<Self, ColibriError> {
        let c_method = CString::new(method)?;
        let c_params = CString::new(params)?;
        let ctx = unsafe {
            ffi::c4_create_rpc_ctx(
                c_method.as_ptr() as *mut _,
                c_params.as_ptr() as *mut _,
                chain_id,
                prover_flags,
                verify_flags,
                prover_mode.as_native(),
            )
        };
        if ctx.is_null() {
            return Err(ColibriError::Ffi(format!(
                "c4_create_rpc_ctx returned NULL for '{method}'"
            )));
        }
        Ok(Self { ctx })
    }

    /// Advance the RPC state machine one step.
    pub fn execute_json_status(&mut self) -> Result<String, ColibriError> {
        unsafe {
            let ptr = ffi::c4_rpc_execute_json_status(self.ctx);
            if ptr.is_null() {
                return Err(ColibriError::Ffi(
                    "rpc_execute_json_status returned NULL".into(),
                ));
            }
            let s = CStr::from_ptr(ptr).to_string_lossy().into_owned();
            libc::free(ptr as *mut libc::c_void);
            Ok(s)
        }
    }

    /// Configure witness signer keys (hex string, `0x`-prefixed) for
    /// verifying alternative trust anchors.
    pub fn set_witness_keys(&mut self, keys: &str) -> Result<(), ColibriError> {
        let c = CString::new(keys)?;
        unsafe { ffi::c4_rpc_set_witness_keys(self.ctx, c.as_ptr()) };
        Ok(())
    }

    /// Configure proxy endpoints (`ProverMode::Proxy`). Both arguments
    /// are comma-separated URL lists.
    pub fn set_proxy_urls(
        &mut self,
        rpc_urls: &str,
        beacon_urls: &str,
    ) -> Result<(), ColibriError> {
        let c_rpc = CString::new(rpc_urls)?;
        let c_bcn = CString::new(beacon_urls)?;
        unsafe { ffi::c4_rpc_set_proxy_urls(self.ctx, c_rpc.as_ptr(), c_bcn.as_ptr()) };
        Ok(())
    }

    /// Update the freshness lower bound (`min_latest_block_ts`).
    pub fn set_min_latest_block_ts(&mut self, ts: u64) {
        unsafe { ffi::c4_rpc_set_min_latest_block_ts(self.ctx, ts) };
    }
}

impl Drop for RpcCtx {
    fn drop(&mut self) {
        if !self.ctx.is_null() {
            unsafe { ffi::c4_free_rpc_ctx(self.ctx) };
            self.ctx = ptr::null_mut();
        }
    }
}

unsafe impl Send for RpcCtx {}

/// Configure the trusted checkpoint for `chain_id` globally. Applies to
/// all future contexts (mirrors `c4_set_checkpoint`).
pub fn set_checkpoint(chain_id: u64, checkpoint: &str) -> Result<(), ColibriError> {
    let c = CString::new(checkpoint)?;
    unsafe { ffi::c4_set_checkpoint(chain_id, c.as_ptr()) };
    Ok(())
}

/// Clears in-process prover/verifier caches. Call between fixture-backed
/// tests that share one process. Persistent storage is left untouched.
pub fn reset_caches() {
    unsafe { ffi::c4_reset_caches() };
}
