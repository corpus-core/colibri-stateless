use super::helpers::slice_to_bytes;
use crate::ffi;
use crate::types::{ColibriError, VerificationError};
use std::ffi::{CStr, CString};

pub struct Verifier {
    ctx: *mut libc::c_void,
}

impl Verifier {
    pub fn new(
        proof: &[u8],
        method: &str,
        args: &str,
        chain_id: u64,
        trusted_checkpoint: &str,
    ) -> Result<Self, ColibriError> {
        let c_method = CString::new(method)?;
        let c_args = CString::new(args)?;
        let c_checkpoint = CString::new(trusted_checkpoint)?;

        let ctx = unsafe {
            ffi::c4_verify_create_ctx(
                slice_to_bytes(proof),
                c_method.as_ptr() as *mut i8,
                c_args.as_ptr() as *mut i8,
                chain_id,
                c_checkpoint.as_ptr() as *mut i8,
            )
        };

        if ctx.is_null() {
            return Err(VerificationError::ContextCreation(format!(
                "Failed to create verification context for method '{}'",
                method
            ))
            .into());
        }

        Ok(Self { ctx })
    }

    pub fn execute_json_status(&mut self) -> Result<String, ColibriError> {
        unsafe {
            let ptr = ffi::c4_verify_execute_json_status(self.ctx);
            if ptr.is_null() {
                return Err(VerificationError::Failed(
                    "Verifier execution returned null status".to_string(),
                )
                .into());
            }

            let cstr = CStr::from_ptr(ptr);
            let result = cstr.to_str()?.to_string();

            libc::free(ptr as *mut libc::c_void);
            Ok(result)
        }
    }

    pub fn set_response(&mut self, request_ptr: usize, data: &[u8], node_index: u16) {
        unsafe {
            ffi::c4_req_set_response(
                request_ptr as *mut libc::c_void,
                slice_to_bytes(data),
                node_index,
            );
        }
    }

    pub fn set_error(&mut self, request_ptr: usize, error: &str, node_index: u16) -> Result<(), ColibriError> {
        let c_error = CString::new(error)?;

        unsafe {
            ffi::c4_req_set_error(
                request_ptr as *mut libc::c_void,
                c_error.as_ptr() as *mut i8,
                node_index,
            );
        }
        Ok(())
    }
}

impl Drop for Verifier {
    fn drop(&mut self) {
        if !self.ctx.is_null() {
            unsafe {
                ffi::c4_verify_free_ctx(self.ctx);
            }
        }
    }
}

unsafe impl Send for Verifier {}
unsafe impl Sync for Verifier {}
