use thiserror::Error;

#[derive(Debug, Error)]
pub enum ColibriError {
    #[error("FFI error: {0}")]
    Ffi(String),

    #[error("JSON parse error: {0}")]
    Json(#[from] serde_json::Error),

    #[error("HTTP error: {0}")]
    Http(#[from] reqwest::Error),

    #[error("Invalid UTF-8: {0}")]
    Utf8(#[from] std::str::Utf8Error),

    #[error("Null pointer")]
    NullPointer,

    #[error("Invalid C string")]
    CString(#[from] std::ffi::NulError),
}

pub type Result<T> = std::result::Result<T, ColibriError>;