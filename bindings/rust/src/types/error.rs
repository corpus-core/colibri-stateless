use thiserror::Error;

/// Error during proof generation
#[derive(Debug, Error)]
pub enum ProofError {
    #[error("Failed to create prover context: {0}")]
    ContextCreation(String),

    #[error("Proof generation failed: {0}")]
    Generation(String),

    #[error("Invalid proof data: {0}")]
    InvalidData(String),
}

/// Error during proof verification
#[derive(Debug, Error)]
pub enum VerificationError {
    #[error("Failed to create verification context: {0}")]
    ContextCreation(String),

    #[error("Verification failed: {0}")]
    Failed(String),

    #[error("Invalid proof format: {0}")]
    InvalidProof(String),
}

/// Error during RPC calls
#[derive(Debug, Error)]
pub struct RPCError {
    pub message: String,
    pub code: Option<i32>,
}

impl std::fmt::Display for RPCError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self.code {
            Some(code) => write!(f, "RPC error ({}): {}", code, self.message),
            None => write!(f, "RPC error: {}", self.message),
        }
    }
}

impl RPCError {
    pub fn new(message: impl Into<String>) -> Self {
        Self {
            message: message.into(),
            code: None,
        }
    }

    pub fn with_code(message: impl Into<String>, code: i32) -> Self {
        Self {
            message: message.into(),
            code: Some(code),
        }
    }
}

/// Error during HTTP requests
#[derive(Debug, Error)]
pub struct HTTPError {
    pub message: String,
    pub status_code: Option<u16>,
    pub url: Option<String>,
}

impl std::fmt::Display for HTTPError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match (self.status_code, &self.url) {
            (Some(code), Some(url)) => write!(f, "HTTP {} from {}: {}", code, url, self.message),
            (Some(code), None) => write!(f, "HTTP {}: {}", code, self.message),
            (None, Some(url)) => write!(f, "HTTP error from {}: {}", url, self.message),
            (None, None) => write!(f, "HTTP error: {}", self.message),
        }
    }
}

impl HTTPError {
    pub fn new(message: impl Into<String>) -> Self {
        Self {
            message: message.into(),
            status_code: None,
            url: None,
        }
    }

    pub fn with_status(message: impl Into<String>, status_code: u16) -> Self {
        Self {
            message: message.into(),
            status_code: Some(status_code),
            url: None,
        }
    }

    pub fn with_url(message: impl Into<String>, url: impl Into<String>) -> Self {
        Self {
            message: message.into(),
            status_code: None,
            url: Some(url.into()),
        }
    }

    pub fn full(message: impl Into<String>, status_code: u16, url: impl Into<String>) -> Self {
        Self {
            message: message.into(),
            status_code: Some(status_code),
            url: Some(url.into()),
        }
    }
}

/// Error during storage operations
#[derive(Debug, Error)]
pub enum StorageError {
    #[error("Storage read failed: {0}")]
    ReadFailed(String),

    #[error("Storage write failed: {0}")]
    WriteFailed(String),

    #[error("Storage not initialized")]
    NotInitialized,
}

/// Main error type for Colibri operations
#[derive(Debug, Error)]
pub enum ColibriError {
    #[error(transparent)]
    Proof(#[from] ProofError),

    #[error(transparent)]
    Verification(#[from] VerificationError),

    #[error(transparent)]
    Rpc(#[from] RPCError),

    #[error(transparent)]
    Http(#[from] HTTPError),

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

    #[error("Invalid C string")]
    CString(#[from] std::ffi::NulError),

    #[error("Method not supported: {0}")]
    MethodNotSupported(String),

    #[error("Configuration error: {0}")]
    Config(String),

    #[error("FFI error: {0}")]
    Ffi(String),
}
