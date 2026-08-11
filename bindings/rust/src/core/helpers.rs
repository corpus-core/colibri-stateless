//! Free-standing helpers around the C API that don't need a context.

use crate::ffi;
use crate::types::{ColibriError, MethodType};
use std::ffi::CString;
use std::ptr;

/// Query whether an RPC method can be handled by Colibri for the given
/// chain, and how (see [`MethodType`]).
///
/// Wraps [`c4_get_method_support`](crate::ffi::c4_get_method_support).
/// `params` (JSON array string) and `flags` are forwarded to the C core
/// -- they only matter for the small set of methods whose classification
/// depends on PAP/cache state (e.g. `eth_call` may become
/// `MethodType::Local` when its result is cached).
pub fn get_method_support(
    chain_id: u64,
    method: &str,
    params: Option<&str>,
    flags: u32,
) -> Result<MethodType, ColibriError> {
    let c_method = CString::new(method)?;
    let c_params = params.map(CString::new).transpose()?;
    let params_ptr = c_params
        .as_ref()
        .map(|c| c.as_ptr() as *mut _)
        .unwrap_or(ptr::null_mut());
    let raw = unsafe {
        ffi::c4_get_method_support(chain_id, c_method.as_ptr() as *mut _, params_ptr, flags)
    };
    Ok(MethodType::from_support_code(raw))
}

/// Convenience wrapper: no params, no verify flags.
pub fn get_method_type(chain_id: u64, method: &str) -> Result<MethodType, ColibriError> {
    get_method_support(chain_id, method, None, 0)
}

/// Return the compiled-in Colibri version number
/// (`c4_get_current_version_number`).
pub fn get_current_version_number() -> u32 {
    unsafe { ffi::c4_get_current_version_number() }
}

// ---------------------------------------------------------------------
// bytes_t helpers used by the low-level context wrappers.
// ---------------------------------------------------------------------

/// Copy the memory referenced by `bytes` into an owned [`Vec<u8>`].
pub(crate) fn bytes_to_vec(bytes: ffi::bytes_t) -> Vec<u8> {
    if bytes.data.is_null() || bytes.len == 0 {
        Vec::new()
    } else {
        unsafe { std::slice::from_raw_parts(bytes.data, bytes.len as usize).to_vec() }
    }
}

/// Build a `bytes_t` **view** over the given slice. The returned
/// struct does not own its memory -- the caller must keep `data`
/// alive for the duration of the FFI call.
pub(crate) fn slice_as_bytes(data: &[u8]) -> ffi::bytes_t {
    ffi::bytes_t {
        len: data.len() as u32,
        data: data.as_ptr() as *mut u8,
    }
}
