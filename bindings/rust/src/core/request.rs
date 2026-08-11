//! `unsafe` wrappers around `c4_req_set_response` / `c4_req_set_error`.
//!
//! These are the raw hooks the state machine loop uses to deliver
//! data (or an error) back to a pending request. They deliberately
//! stay `unsafe` because `req_ptr` is an untyped C pointer whose
//! validity the caller must uphold -- see the safety notes on each
//! function.

use crate::ffi;
use crate::types::ColibriError;
use std::ffi::CString;

use super::helpers::slice_as_bytes;

/// Deliver a successful response for the pending request identified by
/// `req_ptr` (obtained from the JSON status blob).
///
/// The C core copies the bytes; the caller retains ownership of
/// `data`.
///
/// # Safety
///
/// `req_ptr` **must** originate from a `pending` status returned by
/// the currently executing context (`Prover`, `Verifier` or
/// `RpcCtx`). It becomes dangling as soon as the owning context is
/// dropped or advanced past that pending state. Passing stale,
/// foreign or zero pointers is undefined behaviour and can result in
/// arbitrary-pointer writes on the C side.
pub unsafe fn set_response(req_ptr: u64, data: &[u8], node_index: u16) {
    ffi::c4_req_set_response(req_ptr as *mut _, slice_as_bytes(data), node_index);
}

/// Report a failure for the pending request identified by `req_ptr`.
///
/// The C core copies the string; the caller retains ownership of
/// `error`.
///
/// # Safety
///
/// See [`set_response`].
pub unsafe fn set_error(req_ptr: u64, error: &str, node_index: u16) -> Result<(), ColibriError> {
    let c_err = CString::new(error)?;
    ffi::c4_req_set_error(req_ptr as *mut _, c_err.as_ptr() as *mut _, node_index);
    Ok(())
}
