//! Safe answers to pending data requests.
//!
//! `execute()` on [`RpcCtx`](super::RpcCtx), [`Verifier`](super::Verifier)
//! and [`Prover`](super::Prover) advances the state machine one step and
//! returns a [`Step`]. A pending step carries [`PendingRequest`] values
//! that borrow the context they came from, so the context can be neither
//! advanced nor dropped while one of them is alive, and answering a
//! request consumes it. That is the invariant the raw
//! [`req_set_response`](crate::req_set_response) /
//! [`req_set_error`](crate::req_set_error) functions leave to the
//! caller, so this API needs no `unsafe`.
//!
//! ```no_run
//! use colibri_stateless::{ProverMode, RpcCtx, Step};
//!
//! # fn fetch(_: &colibri_stateless::DataRequest) -> Result<Vec<u8>, String> { unimplemented!() }
//! # fn main() -> Result<(), colibri_stateless::ColibriError> {
//! let mut ctx = RpcCtx::new("eth_blockNumber", "[]", 1, 0, 0, ProverMode::Remote)?;
//! loop {
//!     match ctx.execute()? {
//!         Step::Pending { requests } => {
//!             for request in requests {
//!                 match fetch(&request) {
//!                     Ok(body) => request.set_response(&body, 0),
//!                     Err(error) => request.set_error(&error, 0),
//!                 }
//!             }
//!         }
//!         Step::Success { result } => {
//!             println!("{result:?}");
//!             break;
//!         }
//!         Step::Revert { data } => {
//!             println!("reverted: {data}");
//!             break;
//!         }
//!         Step::Error { message } => return Err(colibri_stateless::ColibriError::Ffi(message)),
//!     }
//! }
//! # Ok(()) }
//! ```
//!
//! The borrow checker enforces the invariant. A context cannot advance
//! while a request of its last step is alive:
//!
//! ```compile_fail,E0499
//! # use colibri_stateless::{ProverMode, RpcCtx, Step};
//! # fn main() -> Result<(), colibri_stateless::ColibriError> {
//! let mut ctx = RpcCtx::new("eth_gasPrice", "[]", 1, 0, 0, ProverMode::Remote)?;
//! if let Step::Pending { requests } = ctx.execute()? {
//!     ctx.execute()?;
//!     drop(requests);
//! }
//! # Ok(()) }
//! ```
//!
//! nor be dropped:
//!
//! ```compile_fail,E0505
//! # use colibri_stateless::{ProverMode, RpcCtx, Step};
//! # fn main() -> Result<(), colibri_stateless::ColibriError> {
//! let mut ctx = RpcCtx::new("eth_gasPrice", "[]", 1, 0, 0, ProverMode::Remote)?;
//! if let Step::Pending { requests } = ctx.execute()? {
//!     drop(ctx);
//!     drop(requests);
//! }
//! # Ok(()) }
//! ```

use std::ffi::{c_void, CString};
use std::fmt;
use std::marker::PhantomData;
use std::ops::Deref;
use std::ptr::NonNull;

use serde_json::Value as JsonValue;

use super::helpers::slice_as_bytes;
use crate::ffi;
use crate::types::{ColibriError, DataRequest, Status};

/// One step of a context's state machine, returned by `execute()`.
///
/// Same shape as [`Status`], with the pending requests as
/// [`PendingRequest`] values tied to the context's lifetime.
#[derive(Debug)]
pub enum Step<'ctx> {
    /// The context needs external data. Answer every request with
    /// [`PendingRequest::set_response`] or [`PendingRequest::set_error`],
    /// then call `execute()` again. A request dropped without an answer
    /// stays pending and is returned again by the next step.
    Pending {
        /// The requests to answer.
        requests: Vec<PendingRequest<'ctx>>,
    },
    /// The context finished. `result` is `None` for a prover, whose proof
    /// is read with [`Prover::get_proof`](super::Prover::get_proof).
    Success {
        /// The verified result.
        result: Option<JsonValue>,
    },
    /// A verified `eth_call` whose EVM run reverted.
    Revert {
        /// Raw revert data as `0x`-prefixed hex.
        data: String,
    },
    /// The context ended with an error.
    Error {
        /// The C core's error message.
        message: String,
    },
}

impl<'ctx> Step<'ctx> {
    /// Parse the JSON status a context's `execute_json_status` returned.
    ///
    /// # Safety
    ///
    /// `json` must be the status the context borrowed for `'ctx` returned
    /// from its latest step, so that every `req_ptr` in it points at a
    /// pending request owned by that context.
    pub(crate) unsafe fn from_json(json: &str) -> Result<Self, ColibriError> {
        Ok(match Status::parse(json)? {
            Status::Pending { requests } => Step::Pending {
                requests: requests
                    .into_iter()
                    .map(PendingRequest::new)
                    .collect::<Result<_, _>>()?,
            },
            Status::Success { result } => Step::Success { result },
            Status::Revert { data } => Step::Revert { data },
            Status::Error { message } => Step::Error { message },
        })
    }
}

/// A data request the C core is waiting for, borrowed from the context
/// whose step returned it.
///
/// Dereferences to the parsed [`DataRequest`]. Answering consumes the
/// value, so a request is answered at most once, and the borrow keeps
/// the context from advancing or being dropped before the answer.
pub struct PendingRequest<'ctx> {
    request: DataRequest,
    ptr: NonNull<c_void>,
    _ctx: PhantomData<&'ctx mut ()>,
}

// SAFETY: each `PendingRequest` points at a distinct `data_request_t` of a
// context that stays mutably borrowed while the value lives. Answering
// writes only that request (the C core copies the data), so requests of
// one step can be answered from different threads. `&PendingRequest`
// exposes only the parsed `DataRequest`, never the pointer.
unsafe impl Send for PendingRequest<'_> {}
unsafe impl Sync for PendingRequest<'_> {}

impl PendingRequest<'_> {
    fn new(request: DataRequest) -> Result<Self, ColibriError> {
        let addr = usize::try_from(request.req_ptr)
            .map_err(|_| ColibriError::Ffi(format!("req_ptr {} out of range", request.req_ptr)))?;
        let ptr = NonNull::new(addr as *mut c_void).ok_or(ColibriError::NullPointer)?;
        Ok(Self {
            request,
            ptr,
            _ctx: PhantomData,
        })
    }

    /// The parsed request: endpoint type, URL, payload, encoding, ...
    pub fn request(&self) -> &DataRequest {
        &self.request
    }

    /// Answer the request with the response body `data`, received from
    /// the server at `node_index` in the endpoint list. The C core copies
    /// the bytes.
    pub fn set_response(self, data: &[u8], node_index: u16) {
        // SAFETY: `ptr` came from the latest step of the context this value
        // borrows, which cannot have advanced or been dropped since.
        unsafe { ffi::c4_req_set_response(self.ptr.as_ptr(), slice_as_bytes(data), node_index) };
    }

    /// Answer the request with an error. The C core copies the message;
    /// a message with a NUL byte is cut at it.
    pub fn set_error(self, error: &str, node_index: u16) {
        let end = error.find('\0').unwrap_or(error.len());
        let message = CString::new(&error[..end]).expect("no NUL before `end`");
        // SAFETY: as in `set_response`; the C core copies the string.
        unsafe { ffi::c4_req_set_error(self.ptr.as_ptr(), message.as_ptr() as *mut _, node_index) };
    }
}

impl Deref for PendingRequest<'_> {
    type Target = DataRequest;

    fn deref(&self) -> &DataRequest {
        &self.request
    }
}

impl fmt::Debug for PendingRequest<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_tuple("PendingRequest")
            .field(&self.request)
            .finish()
    }
}
