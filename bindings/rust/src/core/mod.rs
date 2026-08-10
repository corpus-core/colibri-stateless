//! High-level Rust API around the C core.
//!
//! Structured similarly to [`bindings/python/src/colibri/client.py`][py]:
//! a [`Colibri`] host manages the network side, while low-level context
//! wrappers ([`Prover`], [`Verifier`], [`RpcCtx`]) expose the raw state
//! machine for callers who want to drive the loop themselves.
//!
//! [py]: https://github.com/corpus-core/colibri-stateless/blob/dev/bindings/python/src/colibri/client.py

mod client;
mod helpers;
mod prover;
mod request;
mod rpc;
mod verifier;

pub use client::{Colibri, ColibriBuilder, ColibriConfig, RequestHandler};
pub use helpers::{get_current_version_number, get_method_support, get_method_type};
pub use prover::Prover;
pub use request::{set_error as req_set_error, set_response as req_set_response};
pub use rpc::RpcCtx;
pub use verifier::Verifier;
