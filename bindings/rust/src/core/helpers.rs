use crate::ffi;
use crate::types::{MethodType, Result};
use std::ffi::{c_void, CString};

pub fn bytes_to_vec(bytes: ffi::bytes_t) -> Vec<u8> {
    if bytes.data.is_null() || bytes.len == 0 {
        Vec::new()
    } else {
        unsafe { std::slice::from_raw_parts(bytes.data, bytes.len as usize).to_vec() }
    }
}

pub fn slice_to_bytes(data: &[u8]) -> ffi::bytes_t {
    ffi::bytes_t {
        data: data.as_ptr() as *mut u8,
        len: data.len() as u32,
    }
}

pub fn set_request_response(req_ptr: u64, data: &[u8], node_index: u16) {
    unsafe {
        ffi::c4_req_set_response(req_ptr as *mut c_void, slice_to_bytes(data), node_index);
    }
}

/// Check if a method is supported on a given chain.
///
/// Returns an integer code indicating support level:
/// - 0: Not supported
/// - 1+: Proofable (can generate proofs)
///
/// # Example
///
/// ```rust
/// use colibri::get_method_support;
///
/// let support = get_method_support(1, "eth_blockNumber").unwrap();
/// assert!(support > 0); // eth_blockNumber is supported
/// ```
pub fn get_method_support(chain_id: u64, method: &str) -> Result<i32> {
    let c_method = CString::new(method)?;

    let support = unsafe { ffi::c4_get_method_support(chain_id, c_method.as_ptr() as *mut i8) };

    Ok(support)
}

/// Get the method type (Proofable, Local, NotSupported) for a given method.
///
/// # Example
///
/// ```rust
/// use colibri::{get_method_type, MethodType};
///
/// let method_type = get_method_type(1, "eth_blockNumber").unwrap();
/// assert_eq!(method_type, MethodType::Proofable);
/// ```
pub fn get_method_type(chain_id: u64, method: &str) -> Result<MethodType> {
    let support_code = get_method_support(chain_id, method)?;
    Ok(MethodType::from_support_code(support_code))
}
