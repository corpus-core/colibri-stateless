use crate::ffi;
use crate::helpers::{bytes_to_vec, slice_to_bytes};
use crate::types::{Result, ColibriError};
use std::ffi::{CStr, CString};

pub struct Prover {
    ctx: *mut ffi::prover_t,
}

impl Prover {
    pub fn new(method: &str, params: &str, chain_id: u64, flags: u32) -> Result<Self> {
        let c_method = CString::new(method)?;
        let c_params = CString::new(params)?;

        let ctx = unsafe {
            ffi::c4_create_prover_ctx(
                c_method.as_ptr() as *mut i8,
                c_params.as_ptr() as *mut i8,
                chain_id,
                flags,
            )
        };

        if ctx.is_null() {
            return Err(ColibriError::NullPointer);
        }

        Ok(Self { ctx })
    }

    // Get execution status as JSON string
    pub fn execute_json_status(&mut self) -> Result<String> {
        unsafe {
            let ptr = ffi::c4_prover_execute_json_status(self.ctx);
            if ptr.is_null() {
                return Err(ColibriError::NullPointer);
            }

            let cstr = CStr::from_ptr(ptr);
            let result = cstr.to_str()?.to_string();

            // Free the C string
            libc::free(ptr as *mut libc::c_void);
            Ok(result)
        }
    }

    // Get the proof bytes
    pub fn get_proof(&mut self) -> Result<Vec<u8>> {
        unsafe {
            let proof_bytes = ffi::c4_prover_get_proof(self.ctx);
            Ok(bytes_to_vec(proof_bytes))
        }
    }

    // Set response for a request
    pub fn set_response(&mut self, request_ptr: usize, data: &[u8], node_index: u16) {
        unsafe {
            ffi::c4_req_set_response(
                request_ptr as *mut libc::c_void,
                slice_to_bytes(data),
                node_index,
            );
        }
    }

    // Set error for a request
    pub fn set_error(&mut self, request_ptr: usize, error: &str, node_index: u16) -> Result<()> {
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

impl Drop for Prover {
    fn drop(&mut self) {
        if !self.ctx.is_null() {
            unsafe {
                ffi::c4_free_prover_ctx(self.ctx);
            }
        }
    }
}

// Safety: Prover can be sent between threads
unsafe impl Send for Prover {}
unsafe impl Sync for Prover {}