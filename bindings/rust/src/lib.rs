mod ffi;
pub mod core;
pub mod storage;
pub mod types;

pub use core::{ColibriClient, ClientConfig, Prover, Verifier};
pub use core::{get_method_support, get_method_type};
pub use types::{ColibriError, Result, MethodType, MAINNET, SEPOLIA, GNOSIS, CHIADO};
pub use storage::{Storage, MemoryStorage, FileStorage, DefaultStorage, default_storage};
