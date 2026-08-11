use thiserror::Error;

/// Errors raised during proof generation.
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum ProofError {
    #[error("Failed to create prover context: {0}")]
    ContextCreation(String),

    #[error("Proof generation failed: {0}")]
    Generation(String),

    #[error("Invalid proof data: {0}")]
    InvalidData(String),
}

/// Errors raised during proof verification.
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum VerificationError {
    #[error("Failed to create verification context: {0}")]
    ContextCreation(String),

    #[error("Verification failed: {0}")]
    Failed(String),

    #[error("Invalid proof format: {0}")]
    InvalidProof(String),
}

/// A verified EVM revert (`eth_call` / `eth_estimateGas`).
///
/// This is a fully proven outcome -- the EVM ran to completion but the
/// contract explicitly reverted. Callers typically ABI-decode `data`
/// against the contract's error definitions.
///
/// Maps to the Geth-style JSON-RPC error `{ "code": 3, "message":
/// "execution reverted", "data": "0x..." }` (also used by
/// EIP-3668/CCIP-Read).
#[derive(Debug, Clone, Error)]
#[error("execution reverted ({data})")]
#[allow(missing_docs)]
pub struct RevertError {
    /// Raw revert data as a `0x`-prefixed hex string (`"0x"` when
    /// empty).
    pub data: String,
}

/// Errors returned by the JSON-RPC layer (transport or protocol level).
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub struct RpcError {
    pub message: String,
    pub code: Option<i32>,
}

impl RpcError {
    /// Create a plain [`RpcError`] with a message.
    pub fn new(message: impl Into<String>) -> Self {
        Self {
            message: message.into(),
            code: None,
        }
    }
    /// Create an [`RpcError`] with a JSON-RPC error code.
    pub fn with_code(message: impl Into<String>, code: i32) -> Self {
        Self {
            message: message.into(),
            code: Some(code),
        }
    }
}

impl std::fmt::Display for RpcError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self.code {
            Some(code) => write!(f, "RPC error ({code}): {}", self.message),
            None => write!(f, "RPC error: {}", self.message),
        }
    }
}

/// Errors from underlying HTTP transport.
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub struct HttpError {
    pub message: String,
    pub status_code: Option<u16>,
    pub url: Option<String>,
}

impl HttpError {
    /// Create a bare [`HttpError`] with just a message.
    pub fn new(message: impl Into<String>) -> Self {
        Self {
            message: message.into(),
            status_code: None,
            url: None,
        }
    }
    /// Create an [`HttpError`] carrying an HTTP status code.
    pub fn with_status(message: impl Into<String>, status_code: u16) -> Self {
        Self {
            message: message.into(),
            status_code: Some(status_code),
            url: None,
        }
    }
    /// Create a fully populated [`HttpError`] (message + status + URL).
    pub fn full(message: impl Into<String>, status_code: u16, url: impl Into<String>) -> Self {
        Self {
            message: message.into(),
            status_code: Some(status_code),
            url: Some(url.into()),
        }
    }
}

impl std::fmt::Display for HttpError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match (self.status_code, self.url.as_deref()) {
            (Some(code), Some(url)) => write!(f, "HTTP {code} from {url}: {}", self.message),
            (Some(code), None) => write!(f, "HTTP {code}: {}", self.message),
            (None, Some(url)) => write!(f, "HTTP error from {url}: {}", self.message),
            (None, None) => write!(f, "HTTP error: {}", self.message),
        }
    }
}

/// Errors from the storage plugin.
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum StorageError {
    #[error("Storage read failed: {0}")]
    ReadFailed(String),

    #[error("Storage write failed: {0}")]
    WriteFailed(String),

    #[error("Storage not initialized")]
    NotInitialized,
}

/// Umbrella error type used across the crate.
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum ColibriError {
    #[error(transparent)]
    Proof(#[from] ProofError),

    #[error(transparent)]
    Verification(#[from] VerificationError),

    #[error(transparent)]
    Revert(#[from] RevertError),

    #[error(transparent)]
    Rpc(#[from] RpcError),

    #[error(transparent)]
    Http(#[from] HttpError),

    #[error(transparent)]
    Storage(#[from] StorageError),

    #[error("JSON parse error: {0}")]
    Json(#[from] serde_json::Error),

    #[error("HTTP client error: {0}")]
    HttpClient(#[from] reqwest::Error),

    #[error("Invalid UTF-8: {0}")]
    Utf8(#[from] std::str::Utf8Error),

    #[error("Null pointer")]
    NullPointer,

    #[error("Invalid C string: {0}")]
    CString(#[from] std::ffi::NulError),

    #[error("Method not supported: {0}")]
    MethodNotSupported(String),

    #[error("Configuration error: {0}")]
    Config(String),

    #[error("FFI error: {0}")]
    Ffi(String),
}
