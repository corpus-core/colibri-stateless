//! Storage plugin -- persists the sync committee state so a verifier can
//! resume across process restarts.
//!
//! The C core stores per-chain state under short opaque keys (a few dozen
//! bytes each). All bindings implement the same three-callback interface
//! (`get` / `set` / `del`); in Rust this maps to the [`Storage`] trait
//! plus a static bridge that hands the callbacks over to the C core via
//! [`c4_set_storage_config`](crate::ffi::c4_set_storage_config).
//!
//! # Example: in-memory storage
//!
//! ```no_run
//! use colibri_stateless::{Colibri, ColibriConfig, MemoryStorage, MAINNET};
//!
//! # async fn run() -> Result<(), colibri_stateless::ColibriError> {
//! let client = Colibri::builder(MAINNET)
//!     .storage(MemoryStorage::default())
//!     .build();
//! # let _ = client;
//! # Ok(()) }
//! ```

pub(crate) mod ffi;

use std::collections::HashMap;
use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::Mutex;

use crate::types::{ColibriError, StorageError};

/// Storage backend used by the C core to persist sync-committee state.
///
/// Implementations must be `Send + Sync` because the C core may invoke
/// the callbacks from arbitrary threads. Errors from the underlying
/// medium are absorbed silently (the C core treats a `None`/no-op as
/// "not cached"), so callers should log inside their implementations if
/// they need visibility.
pub trait Storage: Send + Sync {
    /// Retrieve the value for `key`, or `None` when absent.
    fn get(&self, key: &str) -> Option<Vec<u8>>;

    /// Persist `value` under `key`, overwriting any existing entry.
    fn set(&self, key: &str, value: &[u8]);

    /// Remove `key` from storage. No-op when absent.
    fn delete(&self, key: &str);
}

/// Thread-safe in-memory storage.
#[derive(Default)]
pub struct MemoryStorage {
    data: Mutex<HashMap<String, Vec<u8>>>,
}

impl MemoryStorage {
    /// Create an empty in-memory storage.
    pub fn new() -> Self {
        Self::default()
    }

    /// Number of entries currently stored.
    pub fn size(&self) -> usize {
        self.data.lock().unwrap().len()
    }

    /// Drop all stored entries.
    pub fn clear(&self) {
        self.data.lock().unwrap().clear();
    }
}

impl Storage for MemoryStorage {
    fn get(&self, key: &str) -> Option<Vec<u8>> {
        self.data.lock().unwrap().get(key).cloned()
    }
    fn set(&self, key: &str, value: &[u8]) {
        self.data
            .lock()
            .unwrap()
            .insert(key.to_string(), value.to_vec());
    }
    fn delete(&self, key: &str) {
        self.data.lock().unwrap().remove(key);
    }
}

/// File-based storage that mirrors the default behaviour of the CLI /
/// Python bindings.
///
/// State files are stored in `base_dir` (defaults to `$C4_STATES_DIR`,
/// falling back to `<temp>/colibri_states`). Keys are sanitised so they
/// can be used as filenames.
pub struct FileStorage {
    base_dir: PathBuf,
}

impl FileStorage {
    /// Create a file storage rooted at `base_dir`. When `base_dir` is
    /// `None` the value of `$C4_STATES_DIR` is used, or
    /// `<temp>/colibri_states` as a last resort.
    pub fn new(base_dir: Option<PathBuf>) -> Result<Self, ColibriError> {
        let base_dir = base_dir.unwrap_or_else(|| {
            env::var("C4_STATES_DIR")
                .map(PathBuf::from)
                .unwrap_or_else(|_| env::temp_dir().join("colibri_states"))
        });
        fs::create_dir_all(&base_dir).map_err(|e| {
            StorageError::WriteFailed(format!("failed to create storage directory: {e}"))
        })?;
        Ok(Self { base_dir })
    }

    fn path_for(&self, key: &str) -> PathBuf {
        // Sanitise the key: keep alphanumerics + `.`, `_`, `-`.
        let safe: String = key
            .chars()
            .map(|c| {
                if c.is_ascii_alphanumeric() || c == '.' || c == '_' || c == '-' {
                    c
                } else {
                    '_'
                }
            })
            .collect();
        self.base_dir.join(if safe.is_empty() {
            "_empty_".to_string()
        } else {
            safe
        })
    }
}

impl Storage for FileStorage {
    fn get(&self, key: &str) -> Option<Vec<u8>> {
        fs::read(self.path_for(key)).ok()
    }
    fn set(&self, key: &str, value: &[u8]) {
        let _ = fs::write(self.path_for(key), value);
    }
    fn delete(&self, key: &str) {
        let _ = fs::remove_file(self.path_for(key));
    }
}

/// Type alias for the default storage returned by [`default_storage`].
pub type DefaultStorage = FileStorage;

/// Create a default [`FileStorage`] rooted at the standard location.
pub fn default_storage() -> Result<DefaultStorage, ColibriError> {
    FileStorage::new(None)
}

/// Register `storage` as the global backend for the C core. Idempotent
/// -- subsequent calls replace the previous instance while keeping the
/// callbacks pointing at the same trampolines.
pub fn register_storage(storage: impl Storage + 'static) {
    ffi::register_global_storage(Box::new(storage));
}

/// Helper: register `storage` and also ensure `base_dir` (if given)
/// exists on disk. Returns the storage so it can be used for local
/// mock-reads in tests.
pub fn register_storage_at(dir: impl AsRef<Path>) -> Result<(), ColibriError> {
    let dir = dir.as_ref().to_path_buf();
    let storage = FileStorage::new(Some(dir))?;
    register_storage(storage);
    Ok(())
}
