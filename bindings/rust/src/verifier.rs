use crate::ffi;
use crate::helpers::slice_to_bytes;
use crate::types::{Result, ColibriError};
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
    ) -> Result<Self> {
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
            return Err(ColibriError::NullPointer);
        }

        Ok(Self { ctx })
    }

    pub fn execute_json_status(&mut self) -> Result<String> {
        unsafe {
            let ptr = ffi::c4_verify_execute_json_status(self.ctx);
            if ptr.is_null() {
                return Err(ColibriError::NullPointer);
            }

            let cstr = CStr::from_ptr(ptr);
            let result = cstr.to_str()?.to_string();

            libc::free(ptr as *mut libc::c_void);
            Ok(result)
        }
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