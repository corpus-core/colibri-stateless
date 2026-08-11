//! Public types shared across the crate.
//!
//! Split into small focused sub-modules -- chain identifiers, error
//! taxonomy, method classification and the JSON status wire format.

/// Chain identifiers and default endpoint URLs.
pub mod chain;
/// Error types shared across the crate.
pub mod error;
/// Method classification (proofable, local, ...).
pub mod method;
/// Wire types for the JSON status protocol.
pub mod request;

pub use chain::{
    default_beacon_apis, default_checkpointz, default_eth_rpcs, default_provers, CHIADO, GNOSIS,
    MAINNET, SEPOLIA,
};
pub use error::{
    ColibriError, HttpError, ProofError, RevertError, RpcError, StorageError, VerificationError,
};
pub use method::{MethodType, PrivacyMode, ProverMode};
pub use request::{DataRequest, Encoding, HttpMethod, RequestType, Status};
