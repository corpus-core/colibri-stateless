pub mod core;
mod ffi;
pub mod storage;
pub mod types;

pub use core::{get_method_support, get_method_type};
pub use core::{ClientConfig, ColibriClient, Prover, Verifier};
pub use storage::{default_storage, DefaultStorage, FileStorage, MemoryStorage, Storage};
pub use types::{
    ColibriError, HTTPError, MethodType, ProofError, RPCError, StorageError, VerificationError,
    CHIADO, GNOSIS, MAINNET, SEPOLIA,
};
