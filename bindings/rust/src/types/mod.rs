pub mod error;
pub mod status;
pub mod method;
pub mod chain;
pub mod request;

pub use error::{ColibriError, Result, ProofError, VerificationError, RPCError, HTTPError, StorageError};
pub use status::{Status, HttpRequest};
pub use method::MethodType;
pub use chain::{
    // Chain IDs
    MAINNET, SEPOLIA, GNOSIS, CHIADO,
    // ETH RPC URLs
    MAINNET_ETH_RPC, SEPOLIA_ETH_RPC, GNOSIS_ETH_RPC, CHIADO_ETH_RPC,
    // Beacon API URLs
    MAINNET_BEACON_API, SEPOLIA_BEACON_API, GNOSIS_BEACON_API, CHIADO_BEACON_API,
    // Checkpointz URLs
    MAINNET_CHECKPOINTZ_1, MAINNET_CHECKPOINTZ_2, MAINNET_CHECKPOINTZ_3, MAINNET_CHECKPOINTZ_4,
    // Prover URLs
    MAINNET_PROVER, SEPOLIA_PROVER, GNOSIS_PROVER, CHIADO_PROVER, DEFAULT_PROVER,
};
pub use request::{RequestType, HttpMethod, Encoding};
