//! # Colibri Stateless -- Rust bindings
//!
//! Rust bindings for [Colibri Stateless][repo], a high-performance
//! stateless prover / verifier for Ethereum and OP-Stack chains.
//!
//! The main entry point is [`Colibri`]. Configure it via
//! [`Colibri::builder`] and call [`Colibri::rpc`] to execute a
//! cryptographically verified RPC call:
//!
//! ```no_run
//! use colibri_stateless::{Colibri, MAINNET};
//! use serde_json::json;
//!
//! # async fn run() -> Result<(), colibri_stateless::ColibriError> {
//! let client = Colibri::builder(MAINNET).build();
//! let balance = client
//!     .rpc("eth_getBalance", &json!(["0xd8dA6BF26964aF9D7eEd9e03E53415D37aA96045", "latest"]))
//!     .await?;
//! println!("balance: {balance}");
//! # Ok(()) }
//! ```
//!
//! Lower-level building blocks are also exposed:
//!
//! - [`Prover`] / [`Verifier`] / [`RpcCtx`] wrap the corresponding C
//!   contexts and let you drive the state machine yourself.
//! - [`get_method_support`] classifies an RPC method (proofable, local,
//!   ...).
//! - [`Storage`] plus [`FileStorage`] / [`MemoryStorage`] persist the
//!   sync-committee state used by the verifier.
//!
//! For running the shared integration tests against `test/data`, see
//! the `testing` module (enabled by default in `cargo test`).
//!
//! [repo]: https://github.com/corpus-core/colibri-stateless

#![warn(missing_docs)]
#![allow(clippy::needless_doctest_main)]

pub mod core;
mod ffi;
pub mod storage;
pub mod testing;
pub mod types;

pub use core::{
    get_current_version_number, get_method_support, get_method_type, req_set_error,
    req_set_response, reset_caches, Colibri, ColibriBuilder, ColibriConfig, Prover, RequestHandler,
    RpcCtx, Verifier,
};
pub use storage::{
    default_storage, register_storage, register_storage_at, DefaultStorage, FileStorage,
    MemoryStorage, Storage,
};
pub use types::{
    default_beacon_apis, default_checkpointz, default_eth_rpcs, default_provers, ColibriError,
    DataRequest, Encoding, HttpError, HttpMethod, MethodType, PrivacyMode, ProofError, ProverMode,
    RequestType, RevertError, RpcError, Status, StorageError, VerificationError, CHIADO, GNOSIS,
    MAINNET, PLATABERGET, SEPOLIA,
};
